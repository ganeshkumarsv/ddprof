// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>
#include <string>

#include <cstdio>
#include <cstdlib>

extern "C" {
#include <fcntl.h>
#include <perf.h>
#include <sys/mman.h>
#include <unistd.h>
}

// Simple test to see if mlock fails for large sizes
TEST(MMapTest, Mlock32KB) {
  const int alloc_size = 32 * 1024; // 32 KB
  char *memory = (char *)malloc(alloc_size);
  ASSERT_TRUE(memory);
  int ret = mlock(memory, alloc_size);
  EXPECT_TRUE(ret == 0);
  munlock(memory, alloc_size);
  free(memory);
}

TEST(MMapTest, PerfOpen) {
  int pid = getpid();
  std::cerr << "Pid number" << pid << std::endl;

  long page_size = sysconf(_SC_PAGESIZE);
  std::cerr << "Page size is :" << page_size << std::endl;

  int cpu = 0;

  for (int i = 0; i < perfoptions_nb_presets(); ++i) {
    std::cerr << "#######################################" << std::endl;
    std::cerr << "-->" << i << " " << perfoptions_preset(i)->desc << std::endl;

    int perf_fd = perfopen(pid, perfoptions_preset(i), cpu, false);
    if (i == 10 || i == 11) { // Expected not to fail for CPU / WALL profiling
      EXPECT_TRUE(perf_fd != -1);
    }
    if (perf_fd == -1) {
      std::cerr << "ERROR ---> :" << errno << "=" << strerror(errno)
                << std::endl;
      continue;
    }
    // default perfown is 4k * (64 + 1)
    size_t mmap_size = 0;
    void *region = NULL;
    int buf_size_shift = 10;
    while (region == nullptr && --buf_size_shift > 0) {
      mmap_size = perf_mmap_size(buf_size_shift);
      std::cerr << "mmap size attempt --> " << mmap_size << "("
                << buf_size_shift << ")";

      region = perfown_sz(perf_fd, mmap_size);

      if (!region) {
        std::cerr << " = FAILURE !!!" << std::endl;
        continue;
      }
      std::cerr << " = SUCCESS !!!" << std::endl;
      break;
    }
    if (region) {
      std::cerr << "FULL SUCCESS (size=" << mmap_size << ")" << std::endl;
    }
    ASSERT_TRUE(region);
    perfdisown(region, mmap_size);
    close(perf_fd);
  }
}
