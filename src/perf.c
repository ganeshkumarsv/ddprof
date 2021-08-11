#include "perf.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logger.h"

#define rmb() __asm__ volatile("lfence" ::: "memory")

#define DEFAULT_PAGE_SIZE 4096 // Concerned about hugepages?

#define DEFAULT_BUFF_SIZE_SHIFT 6
#define RETRY_BUFF_SIZE_SHIFT 3

static long s_page_size = 0;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = DEFAULT_SAMPLE_TYPE,
    .precise_ip = 2,
    .disabled = 1,
    .inherit = 1,
    .inherit_stat = 0,
    .mmap = 0, // keep track of executable mappings
    .task = 0, // Follow fork/stop events
    .comm = 0, // Follow exec()
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    .exclude_kernel = 1,
    .exclude_hv = 1,
};

static long get_page_size(void) {
  if (!s_page_size) {
    s_page_size = sysconf(_SC_PAGESIZE);
    // log if we have an unusual page size
    if (s_page_size != DEFAULT_PAGE_SIZE)
      LG_WRN("Page size is %ld", s_page_size);
  }
  return s_page_size;
}

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int gfd,
                    unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, gfd, flags);
}

int perfopen(pid_t pid, const PerfOption *opt, int cpu, bool extras) {
  struct perf_event_attr attr = g_dd_native_attr;
  attr.type = opt->type;
  attr.config = opt->config;
  attr.sample_period = opt->sample_period; // Equivalently, freq
  attr.exclude_kernel = !(opt->include_kernel);
  attr.freq = opt->freq;

  // Breakpoint
  if (opt->type & PERF_TYPE_BREAKPOINT) {
    attr.config = 0; // as per perf_event_open() manpage
    attr.bp_type = opt->bp_type;
  }

  // Extras
  if (extras) {
    attr.mmap = 1;
    attr.task = 1;
    attr.comm = 1;
  }

  int fd = perf_event_open(&attr, pid, cpu, -1, PERF_FLAG_FD_CLOEXEC);
  if (-1 == fd && EACCES == errno) {
    return -1;
  } else if (-1 == fd) {
    return -1;
  }

  return fd;
}

size_t perf_mmap_size(int buf_size_shift) {
  // size of buffers are constrained to a power of 2 + 1
  return ((1U << buf_size_shift) + 1) * get_page_size();
}

size_t get_mask_from_size(size_t size) {
  // assumption is that we used a (power of 2) + 1 (refer to perf_mmap_size)
  return (size - get_page_size() - 1);
}

void *perfown_sz(int fd, size_t size_of_buffer) {
  void *region;

  // Map in the region representing the ring buffer
  // TODO what to do about hugepages?
  region =
      mmap(NULL, size_of_buffer, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (MAP_FAILED == region || !region)
    return NULL;

  fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK);

  return region;
}

// returns region, size is updated with the attempted size
// On failure, returns NULL
void *perfown(int fd, size_t *size) {
  *size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
  void *reg = perfown_sz(fd, *size);
  if (reg)
    return reg;
  *size = perf_mmap_size(RETRY_BUFF_SIZE_SHIFT);
  return perfown_sz(fd, *size);
}

int perfdisown(void *region, size_t size) { return munmap(region, size); }

void rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size) {
  rb->start = (const char *)page + get_page_size();
  rb->size = size;
  rb->mask = get_mask_from_size(size);
}

uint64_t rb_next(RingBuffer *rb) {
  rb->offset = (rb->offset + sizeof(uint64_t)) & (rb->mask);
  return *(uint64_t *)(rb->start + rb->offset);
}

struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset) {
  rb->offset = (unsigned long)offset & (rb->mask);
  return (struct perf_event_header *)(rb->start + rb->offset);
}

void main_loop(PEvent *pes, int pe_len, perfopen_attr *attr, void *arg) {
  struct pollfd pfd[100];
  assert(attr->msg_fun);

  if (pe_len > 100)
    pe_len = 100;

  // Setup poll() to watch perf_event file descriptors
  for (int i = 0; i < pe_len; i++) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN | POLLERR | POLLHUP;
  }

  // Handle the processing in a fork, so we can clean up unfreeable state.
  // TODO we probably lose events when we switch workers.  It's only a blip,
  //      but it's still slightly annoying...
  pid_t child_pid;
  volatile bool *continue_profiling =
      mmap(0, sizeof(bool), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,
           -1, 0);

  // If the malloc fails, then try to profile without resetting the worker
  if (!continue_profiling) {
    LG_ERR("[PERF] Could not initialize worker process coordinator, profiling "
           "will probably fail");
  } else {
    while ((child_pid = fork())) {
      LG_WRN("[PERF] Created child %d", child_pid);
      waitpid(child_pid, NULL, 0);

      // Harvest the exit state of the child process.  We will always reset it
      // to false so that a child who segfaults or exits erroneously does not
      // cause a pointless loop of spawning
      if (!*continue_profiling) {
        LG_WRN("[PERF] Stop profiling!");
        return;
      } else
        *continue_profiling = false;
      LG_NTC("[PERF] Refreshing worker process");
    }
  }

  // If we're here, then we are a child spawned during the previous operation.
  // That means we need to iterate through the perf_event_open() handles and
  // get the mmaps
  for (int k = 0; k < pe_len; k++) {
    if (!(pes[k].region = perfown(pes[k].fd))) {
      close(pes[k].fd);
      pes[k].fd = -1;
      LG_ERR("Worker could not register handle %d (%s)", k, strerror(errno));
    }
  }

  while (1) {
    int n = poll(pfd, pe_len, PSAMPLE_DEFAULT_WAKEUP);

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR)
      continue;
    else if (-1 == n)
      return;

    // If no file descriptors, call time-out
    if (0 == n && attr->timeout_fun) {

      // We don't return from here, only exit, since we don't want to log
      // shutdown messages (if we're shutting down, the outer process will
      // emit those loglines)
      if (!attr->timeout_fun(continue_profiling, arg)) {

        // Cleanup the regions, since the next worker will use them
        for (int k = 0; k < pe_len; k++) {
          if (!pes[k].region)
            continue;
          munmap(pes[k].region, PAGE_SIZE + PSAMPLE_SIZE);
          pes[k].region = NULL;
        }
        exit(0);
      }

      // If we didn't have to shut down, then go back to poll()
      continue;
    }

    for (int i = 0; i < pe_len; i++) {
      if (!pfd[i].revents)
        continue;
      if (pfd[i].revents & POLLHUP)
        return;

      // Drain the ringbuffer and dispatch to callback, as needed
      // The head and tail are taken literally (without wraparound), since they
      // don't wrap in the underlying object.  Instead, the rb_* interfaces
      // wrap when accessing.
      uint64_t head = pes[i].region->data_head;
      rmb();
      uint64_t tail = pes[i].region->data_tail;
      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region, pes[i].reg_size);

      while (head > tail) {
        struct perf_event_header *hdr = rb_seek(rb, tail);
        if ((char *)pes[i].region + pes[i].reg_size < (char *)hdr + hdr->size) {
          // LG_WRN("[UNWIND] OUT OF BOUNDS");
        } else {

          // Same deal as the call to timeout_fun
          if (!attr->msg_fun(hdr, pes[i].pos, continue_profiling, arg)) {
            for (int k = 0; k < pe_len; k++) {
              if (!pes[k].region)
                continue;
              munmap(pes[k].region, PAGE_SIZE + PSAMPLE_SIZE);
              pes[k].region = NULL;
            }
            exit(0);
          }
        }
        tail += hdr->size;
      }

      // We tell the kernel how much we read.  This *should* be the same as
      // the current tail, but in the case of an error head will be a safe
      // restart position.
      pes[i].region->data_tail = head;

      if (head != tail)
        LG_NTC("Head/tail buffer mismatch");
    }
  }
}
