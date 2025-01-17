// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "ddprof_worker.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

#include "ddprof_context.h"
#include "ddprof_stats.h"
#include "logger.h"
#include "perf.h"
#include "pevent_lib.h"
#include "pprof/ddprof_pprof.h"
#include "procutils.h"
#include "stack_handler.h"
}

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "exporter/ddprof_exporter.h"
#include "tags.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <cassert>

#ifdef DBG_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif

#define DDPROF_EXPORT_TIMEOUT_MAX 60

using namespace ddprof;

static const DDPROF_STATS s_cycled_stats[] = {STATS_UNWIND_TICKS,
                                              STATS_EVENT_COUNT,
                                              STATS_EVENT_LOST,
                                              STATS_SAMPLE_COUNT,
                                              STATS_DSO_UNHANDLED_SECTIONS,
                                              STATS_CPU_TIME};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))

static const unsigned s_nb_samples_per_backpopulate = 200;

/// Human readable runtime information
static void print_diagnostics(const DsoHdr &dso_hdr) {
  LG_PRINT("Printing internal diagnostics");
  ddprof_stats_print();
  dso_hdr._stats.log();
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

static inline int64_t now_nanos() {
  static struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

static inline long export_time_convert(double upload_period) {
  return upload_period * 1000000000;
}

static inline void export_time_set(DDProfContext *ctx) {
  assert(ctx);
  ctx->worker_ctx.send_nanos =
      now_nanos() + export_time_convert(ctx->params.upload_period);
}

DDRes worker_library_init(DDProfContext *ctx) {
  try {
    // Set the initial time
    export_time_set(ctx);
    // Make sure worker-related counters are reset
    ctx->worker_ctx.count_worker = 0;
    // Make sure worker index is initialized correctly
    ctx->worker_ctx.i_current_pprof = 0;
    ctx->worker_ctx.exp_tid = {0};

    ctx->worker_ctx.us = new UnwindState();

    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

    // If we're here, then we are a child spawned during the startup operation.
    // That means we need to iterate through the perf_event_open() handles and
    // get the mmaps
    if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
      LG_NTC("Retrying attachment without user override");
      DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
    }
    // Initialize the unwind state and library
    unwind_init();
    ctx->worker_ctx.user_tags =
        new UserTags(ctx->params.tags, ctx->params.num_cpu);

    // Zero out pointers to dynamically allocated memory
    ctx->worker_ctx.exp[0] = nullptr;
    ctx->worker_ctx.exp[1] = nullptr;
    ctx->worker_ctx.pprof[0] = nullptr;
    ctx->worker_ctx.pprof[1] = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes worker_library_free(DDProfContext *ctx) {
  try {
    delete ctx->worker_ctx.user_tags;
    ctx->worker_ctx.user_tags = nullptr;

    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
    DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));

    delete ctx->worker_ctx.us;
    ctx->worker_ctx.us = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

/// Retrieve cpu / memory info
static DDRes worker_update_stats(ProcStatus *procstat, const DsoHdr *dso_hdr) {
  // Update the procstats, but first snapshot the utime so we can compute the
  // diff for the utime metric
  long utime_old = procstat->utime;
  DDRES_CHECK_FWD(proc_read(procstat));

  ddprof_stats_set(STATS_PROCFS_RSS, get_page_size() * procstat->rss);
  ddprof_stats_set(STATS_PROCFS_UTIME, procstat->utime - utime_old);
  ddprof_stats_set(STATS_DSO_UNHANDLED_SECTIONS,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kUnhandledDso));
  ddprof_stats_set(STATS_DSO_NEW_DSO,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kNewDso));
  ddprof_stats_set(STATS_DSO_SIZE, dso_hdr->get_nb_dso());
  ddprof_stats_set(STATS_DSO_MAPPED, dso_hdr->get_nb_mapped_dso());
  return ddres_init();
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
DDRes ddprof_pr_sample(DDProfContext *ctx, perf_event_sample *sample, int pos) {
  // Before we do anything else, copy the perf_event_header into a sample
  struct UnwindState *us = ctx->worker_ctx.us;
  ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL);

  // copy the sample context into the unwind structure
  unwind_init_sample(us, sample->regs, sample->pid, sample->size_stack,
                     sample->data_stack);

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (ctx->watchers[pos].config == PERF_COUNT_SW_TASK_CLOCK)
    ddprof_stats_add(STATS_CPU_TIME, sample->period, NULL);
  unsigned long this_ticks_unwind = __rdtsc();
  DDRes res = unwindstate__unwind(us);

  // Aggregate if unwinding went well (todo : fatal error propagation)
  if (!IsDDResFatal(res)) {
#ifndef DDPROF_NATIVE_LIB
    // in lib mode we don't aggregate (protect to avoid link failures)
    int i_export = ctx->worker_ctx.i_current_pprof;
    DDProfPProf *pprof = ctx->worker_ctx.pprof[i_export];
    DDRES_CHECK_FWD(pprof_aggregate(&us->output, &us->symbol_hdr,
                                    sample->period, pos, pprof));
#else
    // Call the user's stack handler
    if (ctx->stack_handler) {
      if (!ctx->stack_handler->apply(&us->output, ctx,
                                     ctx->stack_handler->callback_ctx, pos)) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_STACK_HANDLE,
                               "Stack handler returning errors");
      }
    }
#endif
  }
  DDRES_CHECK_FWD(ddprof_stats_add(STATS_UNWIND_TICKS,
                                   __rdtsc() - this_ticks_unwind, NULL));

  return ddres_init();
}

static void ddprof_reset_worker_stats() {
  for (unsigned i = 0; i < cycled_stats_sz; ++i) {
    ddprof_stats_clear(s_cycled_stats[i]);
  }
}

#ifndef DDPROF_NATIVE_LIB
void *ddprof_worker_export_thread(void *arg) {
  DDProfWorkerContext *worker = (DDProfWorkerContext *)arg;
  // export the one we are not writting to
  int i = 1 - worker->i_current_pprof;

  if (IsDDResFatal(
          ddprof_exporter_export(worker->pprof[i]->_profile, worker->exp[i]))) {
    LG_NFO("Failed to export from worker");
    worker->exp_error = true;
  }

  if (IsDDResNotOK(pprof_reset(worker->pprof[i]))) {
    worker->exp_error = true;
  }

  return nullptr;
}
#endif

/// Cycle operations : export, sync metrics, update counters
DDRes ddprof_worker_cycle(DDProfContext *ctx, int64_t now,
                          bool synchronous_export) {

  // Scrape procfs for process usage statistics
  DDRES_CHECK_FWD(worker_update_stats(&ctx->worker_ctx.proc_status,
                                      &ctx->worker_ctx.us->dso_hdr));

  // And emit diagnostic output (if it's enabled)
  print_diagnostics(ctx->worker_ctx.us->dso_hdr);
  if (IsDDResNotOK(ddprof_stats_send(ctx->params.internal_stats))) {
    LG_WRN("Unable to utilize to statsd socket.  Suppressing future stats.");
    free((void *)ctx->params.internal_stats);
    ctx->params.internal_stats = NULL;
  }

#ifndef DDPROF_NATIVE_LIB
  // Take the current pprof contents and ship them to the backend.  This also
  // clears the pprof for reuse
  // Dispatch happens in a thread, with the underlying data structure for
  // aggregation rotating between exports.  If we return to this point before
  // the previous thread has finished,  we wait for up to five seconds before
  // failing

  // If something is pending, return error
  if (ctx->worker_ctx.exp_tid) {
    struct timespec waittime;
    clock_gettime(CLOCK_REALTIME, &waittime);
    int waitsec = DDPROF_EXPORT_TIMEOUT_MAX - ctx->params.upload_period;
    waitsec = waitsec > 1 ? waitsec : 1;
    waittime.tv_sec += waitsec;
    if (pthread_timedjoin_np(ctx->worker_ctx.exp_tid, NULL, &waittime)) {
      LG_WRN("Exporter took too long");
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORT_TIMEOUT);
    }
    ctx->worker_ctx.exp_tid = 0;
  }
  if (ctx->worker_ctx.exp_error)
    return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);

  // Dispatch to thread
  ctx->worker_ctx.exp_error = false;

  // switch before we async export to avoid any possible race conditions (then
  // take into account the switch)
  ctx->worker_ctx.i_current_pprof = 1 - ctx->worker_ctx.i_current_pprof;
  if (!synchronous_export) {
    pthread_create(&ctx->worker_ctx.exp_tid, NULL, ddprof_worker_export_thread,
                   &ctx->worker_ctx);
  } else {
    ddprof_worker_export_thread(reinterpret_cast<void *>(&ctx->worker_ctx));
    if (ctx->worker_ctx.exp_error) {
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);
    }
  }
#endif

  // Increase the counts of exports
  ctx->worker_ctx.count_worker += 1;

  // allow new backpopulates
  ctx->worker_ctx.us->dso_hdr.reset_backpopulate_state();

  // Update the time last sent
  ctx->worker_ctx.send_nanos += export_time_convert(ctx->params.upload_period);

  // If the clock was frozen for some reason, we need to detect situations
  // where we'll have catchup windows and reset the export timer.  This can
  // easily happen under temporary load when the profiler is off-CPU, if the
  // process is put in the cgroup freezer, or if we're being emulated.
  if (now > ctx->worker_ctx.send_nanos) {
    LG_WRN("Timer skew detected; frequent warnings may suggest system issue");
    export_time_set(ctx);
  }
  unwind_cycle(ctx->worker_ctx.us);

  // Reset stats relevant to a single cycle
  ddprof_reset_worker_stats();

  return ddres_init();
}

void ddprof_pr_mmap(DDProfContext *ctx, perf_event_mmap *map, int pos) {
  if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
    LG_DBG("<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    ddprof::Dso new_dso(map->pid, map->addr, map->addr + map->len - 1,
                        map->pgoff, std::string(map->filename));
    ctx->worker_ctx.us->dso_hdr.insert_erase_overlap(std::move(new_dso));
  }
}

void ddprof_pr_lost(DDProfContext *, perf_event_lost *lost, int) {
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, NULL);
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  // Change in process name (assuming exec) : clear all associated dso
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("<%d>(COMM)%d -> %s", pos, comm->pid, comm->comm);
    unwind_pid_free(ctx->worker_ctx.us, comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  LG_DBG("<%d>(FORK)%d -> %d/%d", pos, frk->ppid, frk->pid, frk->tid);
  if (frk->ppid != frk->pid) {
    // Clear everything and populate at next error or with coming samples
    unwind_pid_free(ctx->worker_ctx.us, frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  // On Linux, it seems that the thread group leader is the one whose task ID
  // matches the process ID of the group.  Moreover, it seems that it is the
  // overwhelming convention that this thread is closed after the other threads
  // (upheld by both pthreads and runtimes).
  // We do not clear the PID at this time because we currently cleanup anyway.
  (void)ctx;
  if (ext->pid == ext->tid) {
    LG_DBG("<%d>(EXIT)%d", pos, ext->pid);
  } else {
    LG_DBG("<%d>(EXIT)%d/%d", pos, ext->pid, ext->tid);
  }
}

/********************************** callbacks *********************************/
DDRes ddprof_worker_maybe_export(DDProfContext *ctx, int64_t now_ns,
                                 bool *restart_worker) {
  try {
    if (now_ns > ctx->worker_ctx.send_nanos) {
      // restart worker if number of uploads is reached
      *restart_worker =
          (ctx->params.worker_period <= ctx->worker_ctx.count_worker);
      // when restarting worker, do a synchronous export
      DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, now_ns, *restart_worker));
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

#ifndef DDPROF_NATIVE_LIB
DDRes ddprof_worker_init(DDProfContext *ctx) {
  try {
    DDRES_CHECK_FWD(worker_library_init(ctx));
    ctx->worker_ctx.exp[0] =
        (DDProfExporter *)calloc(1, sizeof(DDProfExporter));
    ctx->worker_ctx.exp[1] =
        (DDProfExporter *)calloc(1, sizeof(DDProfExporter));
    ctx->worker_ctx.pprof[0] = (DDProfPProf *)calloc(1, sizeof(DDProfPProf));
    ctx->worker_ctx.pprof[1] = (DDProfPProf *)calloc(1, sizeof(DDProfPProf));
    if (!ctx->worker_ctx.exp[0] || !ctx->worker_ctx.exp[1]) {
      free(ctx->worker_ctx.exp[0]);
      free(ctx->worker_ctx.exp[1]);
      free(ctx->worker_ctx.pprof[0]);
      free(ctx->worker_ctx.pprof[1]);
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error creating exporter");
    }
    if (!ctx->worker_ctx.pprof[0] || !ctx->worker_ctx.pprof[1]) {
      free(ctx->worker_ctx.exp[0]);
      free(ctx->worker_ctx.exp[1]);
      free(ctx->worker_ctx.pprof[0]);
      free(ctx->worker_ctx.pprof[1]);
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error creating pprof holder");
    }
    DDRES_CHECK_FWD(
        ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp[0]));
    DDRES_CHECK_FWD(
        ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp[1]));
    // warning : depends on unwind init
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx->worker_ctx.user_tags, ctx->worker_ctx.exp[0]));
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx->worker_ctx.user_tags, ctx->worker_ctx.exp[1]));
    DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof[0],
                                         ctx->watchers, ctx->num_watchers));
    DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof[1],
                                         ctx->watchers, ctx->num_watchers));
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_worker_free(DDProfContext *ctx) {
  try {
    // First, see if there are any outstanding requests and give them a token
    // amount of time to complete
    if (ctx->worker_ctx.exp_tid) {
      struct timespec waittime;
      clock_gettime(CLOCK_REALTIME, &waittime);
      waittime.tv_sec += 5;
      if (pthread_timedjoin_np(ctx->worker_ctx.exp_tid, NULL, &waittime)) {
        pthread_cancel(ctx->worker_ctx.exp_tid);
      }
      ctx->worker_ctx.exp_tid = 0;
    }

    DDRES_CHECK_FWD(worker_library_free(ctx));
    for (int i = 0; i < 2; i++) {
      if (ctx->worker_ctx.exp[i]) {
        DDRES_CHECK_FWD(ddprof_exporter_free(ctx->worker_ctx.exp[i]));
        free(ctx->worker_ctx.exp[i]);
        ctx->worker_ctx.exp[i] = nullptr;
      }
      if (ctx->worker_ctx.pprof[i]) {
        DDRES_CHECK_FWD(pprof_free_profile(ctx->worker_ctx.pprof[i]));
        free(ctx->worker_ctx.pprof[i]);
        ctx->worker_ctx.pprof[i] = nullptr;
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
#endif

// Simple wrapper over perf_event_hdr in order to filter by PID in a uniform
// way.  Whenver PID is a valid concept for the given event type, the
// interface uniformly presents PID as the element immediately after the
// header.
struct perf_event_hdr_wpid : perf_event_header {
  uint32_t pid, tid;
};

DDRes ddprof_worker_process_event(struct perf_event_header *hdr, int pos,
                                  DDProfContext *ctx) {
  // global try catch to avoid leaking exceptions to main loop
  try {
    ddprof_stats_add(STATS_EVENT_COUNT, 1, NULL);
    struct perf_event_hdr_wpid *wpid = static_cast<perf_event_hdr_wpid *>(hdr);
    switch (hdr->type) {
    /* Cases where the target type has a PID */
    case PERF_RECORD_SAMPLE:
      if (wpid->pid) {
        perf_event_sample *sample = hdr2samp(hdr, DEFAULT_SAMPLE_TYPE);
        DDRES_CHECK_FWD(ddprof_pr_sample(ctx, sample, pos));
      }
      break;
    case PERF_RECORD_MMAP:
      if (wpid->pid)
        ddprof_pr_mmap(ctx, (perf_event_mmap *)hdr, pos);
      break;
    case PERF_RECORD_COMM:
      if (wpid->pid)
        ddprof_pr_comm(ctx, (perf_event_comm *)hdr, pos);
      break;
    case PERF_RECORD_EXIT:
      if (wpid->pid)
        ddprof_pr_exit(ctx, (perf_event_exit *)hdr, pos);
      break;
    case PERF_RECORD_FORK:
      if (wpid->pid)
        ddprof_pr_fork(ctx, (perf_event_fork *)hdr, pos);
      break;

    /* Cases where the target type might not have a PID */
    case PERF_RECORD_LOST:
      ddprof_pr_lost(ctx, (perf_event_lost *)hdr, pos);
      break;
    default:
      break;
    }

    // backpopulate if needed
    if (++ctx->worker_ctx.count_samples > s_nb_samples_per_backpopulate) {
      // allow new backpopulates and reset counter
      ctx->worker_ctx.us->dso_hdr.reset_backpopulate_state();
      ctx->worker_ctx.count_samples = 0;
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
