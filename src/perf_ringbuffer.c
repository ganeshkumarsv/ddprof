#include "perf_ringbuffer.h"

#include <stdlib.h>

#include "logger.h"

bool rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size) {
  // Assumes storage has already been allocated in rb->wrbuf
  rb->meta_size = get_page_size();
  rb->region = page;
  rb->start = (const char *)page + rb->meta_size;
  rb->size = size;
  rb->mask = get_mask_from_size(size);

  // If already allocated just free it
  if (rb->wrbuf)
    free(rb->wrbuf);

  // Allocate room for a watcher-held buffer.  This is for linearizing
  // ringbuffer elements and depends on the per-watcher configuration for
  // perf_event_open().  Eventually this size will be non-static.
  uint64_t buf_sz = PERF_REGS_COUNT + PERF_SAMPLE_STACK_SIZE;
  buf_sz += sizeof(perf_event_sample);
  unsigned char *wrbuf = malloc(buf_sz);
  if (!wrbuf)
    return false;
  rb->wrbuf = wrbuf;

  return true;
}

void rb_clear(RingBuffer *rb) { memset(rb, 0, sizeof(*rb)); }
void rb_free(RingBuffer *rb) {
  if (rb->wrbuf)
    free(rb->wrbuf);
  rb->wrbuf = NULL;
}

uint64_t rb_next(RingBuffer *rb) {
  rb->offset = (rb->offset + sizeof(uint64_t)) & (rb->mask);
  return *(uint64_t *)(rb->start + rb->offset);
}

struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset) {
  struct perf_event_header *ret;
  rb->offset = (unsigned long)offset & (rb->mask);
  ret = rb->start + rb->offset;

  // Now check whether we overrun the end of the ringbuffer.  If so, pass in a
  // buffer rather than the raw event.
  // The terms 'left' and 'right' below refer to the regions in the
  // linearized buffer.  In the index space of the ringbuffer, these terms
  // would be reversed.
  if ((rb->size - rb->meta_size) < ret->size) {
    uint64_t left_sz = rb->size - rb->meta_size - rb->offset;
    uint64_t right_sz = ret->size - left_sz;
    memcpy(rb->wrbuf, rb->start + rb->offset, left_sz);
    memcpy(rb->wrbuf + left_sz, rb->start, right_sz);
    ret = (struct perf_event_header *)wrbuf;
  }

  return ret;
}

// This union is an implementation trick to make splitting apart an 8-byte
// aligned block into two 4-byte blocks easier
typedef union flipper {
  uint64_t full;
  uint32_t half[2];
} flipper;

#define SZ_CHECK                                                               \
  if ((sz += 8) >= sz_hdr)                                                     \
  return false
bool samp2hdr(struct perf_event_header *hdr, perf_event_sample *sample,
              size_t sz_hdr, uint64_t mask) {
  // There is absolutely no point for this interface except testing

  // Presumes that the user has allocated enough room for the whole sample
  if (!hdr || !sz_hdr || sz_hdr < sizeof(struct perf_event_header))
    return NULL;
  memset(hdr, 0, sz_hdr);

  // Initiate
  size_t sz = sizeof(*hdr);
  *hdr = sample->header;
  hdr->size = 0; // update size at end

  uint64_t *buf = (uint64_t *)&hdr[1]; // buffer starts at sample

  if (sz >= sz_hdr)
    return false;

  if (PERF_SAMPLE_IDENTIFIER & mask) {
    *buf++ = sample->sample_id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_IP & mask) {
    *buf++ = sample->ip;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_TID & mask) {
    ((flipper *)buf)->half[0] = sample->pid;
    ((flipper *)buf)->half[1] = sample->tid;
    buf++;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_TIME & mask) {
    *buf++ = sample->time;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_ADDR & mask) {
    *buf++ = sample->addr;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_ID & mask) {
    *buf++ = sample->id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_STREAM_ID & mask) {
    *buf++ = sample->stream_id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_CPU & mask) {
    ((flipper *)buf)->half[0] = sample->cpu;
    ((flipper *)buf)->half[1] = sample->res;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_PERIOD & mask) {
    *buf++ = sample->period;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_READ & mask) {
    *((struct read_format *)buf) = *sample->v;
    buf += sizeof(struct read_format) / 8; // read_format is uint64_t's
    sz += sizeof(struct read_format);
    if (sz >= sz_hdr)
      return false;
  }
  if (PERF_SAMPLE_CALLCHAIN & mask) {
    *buf++ = sample->nr;
    SZ_CHECK;

    // Copy the values
    sz += 8 * sample->nr; // early check
    if (sz >= sz_hdr)
      return false;
    memcpy(buf, sample->ips, sample->nr);
    buf += sample->nr;
  }
  if (PERF_SAMPLE_RAW & mask) {}
  if (PERF_SAMPLE_BRANCH_STACK & mask) {}
  if (PERF_SAMPLE_REGS_USER & mask) {
    *buf++ = sample->abi;
    SZ_CHECK;

    // Copy the values
    sz += 8 * PERF_REGS_COUNT; // TODO pass this in the watcher
    if (sz >= sz_hdr)
      return false;
    memcpy(buf, sample->regs, PERF_REGS_COUNT);
    buf += PERF_REGS_COUNT;
  }
  if (PERF_SAMPLE_STACK_USER & mask) {
    *buf++ = sample->size_stack;
    SZ_CHECK;

    if (sample->size_stack) {
      sz += sample->size_stack;
      if (sz >= sz_hdr)
        return false;
      memcpy(buf, sample->data_stack, sample->size_stack);
      buf += ((sample->size_stack + 0x7) & ~0x7) / 8; // align and convert
      *buf++ = sample->dyn_size_stack;
      SZ_CHECK;
    }
  }
  if (PERF_SAMPLE_WEIGHT & mask) {}
  if (PERF_SAMPLE_DATA_SRC & mask) {}
  if (PERF_SAMPLE_TRANSACTION & mask) {}
  if (PERF_SAMPLE_REGS_INTR & mask) {}

  hdr->size = sz;
  return true;
}

perf_event_sample *hdr2samp(struct perf_event_header *hdr, uint64_t mask) {
  static perf_event_sample sample = {0};
  memset(&sample, 0, sizeof(sample));

  sample.header = *hdr;

  uint64_t *buf = (uint64_t *)&hdr[1]; // sample starts after header

  if (PERF_SAMPLE_IDENTIFIER & mask) {
    sample.sample_id = *buf++;
  }
  if (PERF_SAMPLE_IP & mask) {
    sample.ip = *buf++;
  }
  if (PERF_SAMPLE_TID & mask) {
    sample.pid = ((flipper *)buf)->half[0];
    sample.tid = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_TIME & mask) {
    sample.time = *buf++;
  }
  if (PERF_SAMPLE_ADDR & mask) {
    sample.addr = *buf++;
  }
  if (PERF_SAMPLE_ID & mask) {
    sample.id = *buf++;
  }
  if (PERF_SAMPLE_STREAM_ID & mask) {
    sample.stream_id = *buf++;
  }
  if (PERF_SAMPLE_CPU & mask) {
    sample.cpu = ((flipper *)buf)->half[0];
    sample.res = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_PERIOD & mask) {
    sample.period = *buf++;
  }
  if (PERF_SAMPLE_READ & mask) {
    sample.v = (struct read_format *)buf++;
  }
  if (PERF_SAMPLE_CALLCHAIN & mask) {
    sample.nr = *buf++;
    sample.ips = buf;
    buf += sample.nr;
  }
  if (PERF_SAMPLE_RAW & mask) {}
  if (PERF_SAMPLE_BRANCH_STACK & mask) {}
  if (PERF_SAMPLE_REGS_USER & mask) {
    sample.abi = *buf++;
    sample.regs = buf;
    buf += PERF_REGS_COUNT;
  }
  if (PERF_SAMPLE_STACK_USER & mask) {
    uint64_t size_stack = *buf++;

    // Empirically, it seems that the size of the static stack is either 0 or
    // the amount requested in the call to `perf_event_open()`.  We don't check
    // for that, since there isn't much we'd be able to do anyway.
    if (size_stack == 0) {
      sample.size_stack = 0;
      sample.dyn_size_stack = 0;
      sample.data_stack = NULL;
    } else {
      uint64_t dynsz_stack = 0;
      sample.data_stack = (char *)buf;
      buf = (uint64_t *)(sample.data_stack + size_stack);

      // If the size was specified, we also have a dyn_size
      dynsz_stack = *buf++;

      // If the dyn_size is too big, zero out the stack size since it is likely
      // an error
      sample.size_stack = size_stack < dynsz_stack ? 0 : dynsz_stack;
      sample.dyn_size_stack = dynsz_stack; // for debugging
    }
  }
  if (PERF_SAMPLE_WEIGHT & mask) {}
  if (PERF_SAMPLE_DATA_SRC & mask) {}
  if (PERF_SAMPLE_TRANSACTION & mask) {}
  if (PERF_SAMPLE_REGS_INTR & mask) {}

  // Ensure buf can be used in a semantically correct way without worrying
  // whether we've implemented the next consumer.  This is to keep static
  // analysis and checkers happy.
  (void)buf;

  return &sample;
}
