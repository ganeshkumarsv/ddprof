extern "C" {
#include "ddprof_worker.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

#include "ddprof_context.h"
#include "ddprof_stats.h"
#include "dso.h"
#include "exporter/ddprof_exporter.h"
#include "perf.h"
#include "pevent_lib.h"
#include "pprof/ddprof_pprof.h"
#include "stack_handler.h"
#include "unwind.h"
#include "unwind_output.h"
}

#ifdef DBG_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif

static const DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_TICKS, STATS_EVENT_LOST, STATS_SAMPLE_COUNT};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))

/// Human readable runtime information
static void print_diagnostics() {
  ddprof_stats_print();
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

static inline int64_t now_nanos() {
  static struct timeval tv = {0};
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

DDRes worker_unwind_init(DDProfContext *ctx) {

  // Set the initial time
  export_time_set(ctx);
  // Make sure worker-related counters are reset
  ctx->worker_ctx.count_worker = 0;
  ctx->worker_ctx.count_cache = 0;

  ctx->worker_ctx.us = (UnwindState *)calloc(1, sizeof(UnwindState));
  if (!ctx->worker_ctx.us) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                           "Error when creating unwinding state");
  }

  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

  // If we're here, then we are a child spawned during the startup operation.
  // That means we need to iterate through the perf_event_open() handles and
  // get the mmaps
  DDRES_CHECK_FWD(pevent_mmap(pevent_hdr));

  // Initialize the unwind state and library
  DDRES_CHECK_FWD(unwind_init(ctx->worker_ctx.us));
  return ddres_init();
}

DDRes worker_unwind_free(DDProfContext *ctx) {
  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

  unwind_free(ctx->worker_ctx.us);
  DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
  free(ctx->worker_ctx.us);
  return ddres_init();
}

/// Retrieve cpu / memory info
static DDRes ddprof_procfs_scrape(ProcStatus *procstat) {
  DDRES_CHECK_FWD(proc_read(procstat));
  // Update the procstats, but first snapshot the utime so we can compute the
  // diff for the utime metric
  long utime_old = procstat->utime;
  ddprof_stats_set(STATS_PROCFS_RSS, 1024 * procstat->rss);
  ddprof_stats_set(STATS_PROCFS_UTIME, procstat->utime - utime_old);
  return ddres_init();
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
DDRes ddprof_pr_sample(DDProfContext *ctx, perf_event_sample *sample, int pos) {
  // Before we do anything else, copy the perf_event_header into a sample
  struct UnwindState *us = ctx->worker_ctx.us;
  ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL);
  us->pid = sample->pid;
  us->stack = NULL;
  us->stack_sz = sample->size_stack;
  us->stack = sample->data_stack;

  memcpy(&us->regs[0], sample->regs, PERF_REGS_COUNT * sizeof(uint64_t));
  uw_output_clear(&us->output);
  unsigned long this_ticks_unwind = __rdtsc();
  if (IsDDResNotOK(unwindstate__unwind(us))) {
    Dso *dso = dso_find(us->pid, us->eip);
    if (!dso) {
      LG_WRN("Could not localize top-level IP: [%d](0x%lx)", us->pid, us->eip);
      analyze_unwinding_error(us->pid, us->eip);
    } else {
      LG_WRN("Failed unwind: %s [%d](0x%lx)", dso_path(dso), us->pid, us->eip);
    }
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR, "Error unwinding sample.");
  }
  DDRES_CHECK_FWD(ddprof_stats_add(STATS_UNWIND_TICKS,
                                   __rdtsc() - this_ticks_unwind, NULL));

  // in lib mode we don't aggregate (protect to avoid link failures)
#ifndef DDPROF_NATIVE_LIB
  DDProfPProf *pprof = ctx->worker_ctx.pprof;
  DDRES_CHECK_FWD(pprof_aggregate(&us->output, us->symbols_hdr, sample->period,
                                  pos, pprof));

#else
  if (ctx->stack_handler) {
    if (!ctx->stack_handler->apply(&us->output, ctx, pos)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_STACK_HANDLE,
                             "Stack handler returning errors");
    }
  }
#endif
  return ddres_init();
}

static void ddprof_cycle_stats() {
  for (unsigned i = 0; i < cycled_stats_sz; ++i) {
    ddprof_stats_clear(s_cycled_stats[i]);
  }
}

/// Cycle operations : export, sync metrics, update counters
static DDRes ddprof_worker_cycle(DDProfContext *ctx, int64_t now) {

  // Scrape procfs for process usage statistics
  DDRES_CHECK_FWD(ddprof_procfs_scrape(&ctx->worker_ctx.proc_status));

  // And emit diagnostic output (if it's enabled)
  print_diagnostics();
  DDRES_CHECK_FWD(ddprof_stats_send(ctx->params.internalstats));

#ifndef DDPROF_NATIVE_LIB
  // Take the current pprof contents and ship them to the backend.  This also
  // clears the pprof for reuse
  DDRES_CHECK_FWD(ddprof_exporter_export(ctx->worker_ctx.pprof->_profile,
                                         ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(pprof_reset(ctx->worker_ctx.pprof));

#endif

  // Increase the counts of exports
  ctx->worker_ctx.count_worker += 1;
  ctx->worker_ctx.count_cache += 1;

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

  // Reset stats relevant to a single cycle
  ddprof_cycle_stats();

  return ddres_init();
}

void ddprof_pr_mmap(DDProfContext *ctx, perf_event_mmap *map, int pos) {
  (void)ctx;
  if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
    LG_DBG("[PERF]<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    DsoIn in = *(DsoIn *)&map->addr;
    in.filename = map->filename;
    pid_add(map->pid, &in);
  }
}

void ddprof_pr_lost(DDProfContext *ctx, perf_event_lost *lost, int pos) {
  (void)ctx;
  (void)pos;
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, NULL);
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  (void)ctx;
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("[PERF]<%d>(COMM)%d", pos, comm->pid);
    pid_free(comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(FORK)%d -> %d", pos, frk->ppid, frk->pid);
  if (frk->ppid != frk->pid) {
    pid_fork(frk->ppid, frk->pid);
  } else {
    pid_free(frk->pid);
    pid_backpopulate(frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(EXIT)%d", pos, ext->pid);
  pid_free(ext->pid);
}

/****************************** other functions *******************************/
static DDRes reset_state(DDProfContext *ctx,
                         volatile bool *continue_profiling) {
  if (!ctx) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW, "[DDPROF] Invalid context in %s",
                           __FUNCTION__);
  }

  // Check to see whether we need to clear the whole worker
  // NOTE: we do not reset the counters here, since clearing the worker
  //       1. is nonlocalized to this function; we just send a return value
  //          which informs the caller to refresh the worker
  //       2. new worker should be initialized with a fresh state, so clearing
  //          it here is irrelevant anyway
  if (ctx->params.worker_period <= ctx->worker_ctx.count_worker) {
    *continue_profiling = true;
    DDRES_RETURN_WARN_LOG(DD_WHAT_WORKER_RESET, "%s: cnt=%u - stop worker (%s)",
                          __FUNCTION__, ctx->worker_ctx.count_worker,
                          (*continue_profiling) ? "continue" : "stop");
  }

  // If we haven't hit the hard cap, have we hit the soft cap?
  if (ctx->params.cache_period <= ctx->worker_ctx.count_cache) {
    ctx->worker_ctx.count_cache = 0;
    DDRES_CHECK_FWD(dwfl_caches_clear(ctx->worker_ctx.us));
  }

  return ddres_init();
}

/********************************** callbacks *********************************/
DDRes ddprof_worker_timeout(volatile bool *continue_profiling,
                            DDProfContext *ctx) {
  int64_t now = now_nanos();
  if (now > ctx->worker_ctx.send_nanos) {
    DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, now));
    // reset state defines if we should reboot the worker
    DDRes res = reset_state(ctx, continue_profiling);
    // A warning can be returned for a reset and should not be ignored
    if (IsDDResNotOK(res)) {
      return res;
    }
  }
  return ddres_init();
}

#ifndef DDPROF_NATIVE_LIB
DDRes ddprof_worker_init(DDProfContext *ctx) {

  ctx->worker_ctx.exp = (DDProfExporter *)malloc(sizeof(DDProfExporter));
  if (!ctx->worker_ctx.exp) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error when creating exporter");
  }
  ctx->worker_ctx.pprof = (DDProfPProf *)malloc(sizeof(DDProfPProf));
  if (!ctx->worker_ctx.pprof) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                           "Error when creating pprof structure");
  }

  DDRES_CHECK_FWD(ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(ddprof_exporter_new(ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(worker_unwind_init(ctx));
  DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof, ctx->watchers,
                                       ctx->num_watchers));
  return ddres_init();
}

DDRes ddprof_worker_finish(DDProfContext *ctx) {

  DDRES_CHECK_FWD(ddprof_exporter_free(ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(worker_unwind_free(ctx));
  DDRES_CHECK_FWD(pprof_free_profile(ctx->worker_ctx.pprof));
  free(ctx->worker_ctx.pprof);
  free(ctx->worker_ctx.exp);
  return ddres_init();
}
#endif

DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, DDProfContext *ctx) {

  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    DDRES_CHECK_FWD(ddprof_pr_sample(ctx, hdr2samp(hdr), pos));
    break;
  case PERF_RECORD_MMAP:
    ddprof_pr_mmap(ctx, (perf_event_mmap *)hdr, pos);
    break;
  case PERF_RECORD_LOST:
    ddprof_pr_lost(ctx, (perf_event_lost *)hdr, pos);
    break;
  case PERF_RECORD_COMM:
    ddprof_pr_comm(ctx, (perf_event_comm *)hdr, pos);
    break;
  case PERF_RECORD_EXIT:
    ddprof_pr_exit(ctx, (perf_event_exit *)hdr, pos);
    break;
  case PERF_RECORD_FORK:
    ddprof_pr_fork(ctx, (perf_event_fork *)hdr, pos);
    break;
  default:
    break;
  }

  // Click the timer at the end of processing, since we always add the sampling
  // rate to the last time.
  int64_t now = now_nanos();

  if (now > ctx->worker_ctx.send_nanos) {
    DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, now));
    // reset state defines if we should reboot the worker
    DDRes res = reset_state(ctx, continue_profiling);
    // A warning can be returned for a reset and should not be ignored
    if (IsDDResNotOK(res)) {
      return res;
    }
  }
  return ddres_init();
}
