#include "pevent_lib.h"

#include "ddres.h"
#include "perf.h"
#include "user_override.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

DDRes pevent_init(PEventHdr *pevent_hdr) {
  memset(pevent_hdr, 0, sizeof(PEventHdr));
  pevent_hdr->max_size = MAX_NB_WATCHERS;
  for (int k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;

    // Allocate room for a watcher-held buffer.  This is for linearizing
    // ringbuffer elements and depends on the per-watcher configuration for
    // perf_event_open().  Right now these are globally held, but these lines
    // need to be updated when the reg count, stack size, or other dynamic
    // quantities become variable.
    uint64_t buf_sz = PERF_REGS_COUNT + PERF_SAMPLE_STACK_SIZE;
    buf_sz += sizeof(perf_event_sample);
    pevent_hdr->pes[k].wrbuf = malloc(buf_sz);
    if (!pevent_hdr->pes[k].wrbuf) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PEINIT, "Error allocating storage for pevent watcher %d", k);
    }
  }
  return ddres_init();
}

DDRes pevent_open(DDProfContext *ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  assert(pevent_hdr->size == 0); // check for previous init
  for (int i = 0; i < ctx->num_watchers && ctx->params.enable; ++i) {
    for (int j = 0; j < num_cpu; ++j) {
      int k = pevent_hdr->size;
      ++(pevent_hdr->size);
      pes[k].pos = i;
      pes[k].fd = perfopen(pid, &ctx->watchers[i], j, true);
      if (pes[k].fd == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error calling perfopen on watcher %d.%d (%s)",
                               i, j, strerror(errno));
      }
      if (pevent_hdr->size >= pevent_hdr->max_size) {
        LG_WRN("Reached max number of watchers (%lu)", pevent_hdr->max_size);
        return ddres_init();
      }
    }
  }
  return ddres_init();
}

DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override) {
  // Switch user if needed (when root switch to nobody user)
  UIDInfo info;
  if (use_override)
    DDRES_CHECK_FWD(user_override(&info));

  PEvent *pes = pevent_hdr->pes;
  for (int k = 0; k < pevent_hdr->size; ++k) {
    if (pes[k].fd != -1) {
      if (!(pes[k].region = perfown(pes[k].fd, &pes[k].reg_size))) {
        LG_ERR("Could not finalize watcher (idx#%d): registration (%s)", k,
               strerror(errno));
        goto REGION_CLEANUP;
      }
    }
  }

  // Success : Revert user and exit
  if (use_override)
    DDRES_CHECK_FWD(revert_override(&info));
  return ddres_init();

REGION_CLEANUP:
  // Failure : attempt to clean and send error
  DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
  if (use_override)
    DDRES_CHECK_FWD(revert_override(&info));
  return ddres_error(DD_WHAT_PERFMMAP);
}

DDRes pevent_setup(DDProfContext *ctx, pid_t pid, int num_cpu,
                   PEventHdr *pevent_hdr) {
  DDRES_CHECK_FWD(pevent_open(ctx, pid, num_cpu, pevent_hdr));
  DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, true));
  return ddres_init();
}

DDRes pevent_enable(PEventHdr *pevent_hdr) {
  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (int i = 0; i < pevent_hdr->size; ++i) {
    DDRES_CHECK_INT(ioctl(pevent_hdr->pes[i].fd, PERF_EVENT_IOC_ENABLE),
                    DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%d)",
                    pevent_hdr->pes[i].fd, i);
  }
  return ddres_init();
}

/// Clean the mmap buffer
DDRes pevent_munmap(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  for (int k = 0; k < pevent_hdr->size; ++k) {
    if (pes[k].region) {
      if (perfdisown(pes[k].region, pes[k].reg_size) != 0) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                               "Error when using perfdisown %d", k);
      } else {
        pes[k].region = NULL;
      }
    }
  }
  return ddres_init();
}

DDRes pevent_close(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  for (int k = 0; k < pevent_hdr->size; ++k) {
    if (pes[k].fd != -1) {
      if (close(pes[k].fd) == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error when closing fd=%d (idx#%d) (%s)",
                               pes[k].fd, k, strerror(errno));
      }
      pes[k].fd = -1;
    }
  }
  pevent_hdr->size = 0;
  return ddres_init();
}

// returns the number of successful cleans
DDRes pevent_cleanup(PEventHdr *pevent_hdr) {
  for (int k = 0; k < pevent_hdr->size; ++k) {
    DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
    DDRES_CHECK_FWD(pevent_close(pevent_hdr));
  }
  return ddres_init();
}
