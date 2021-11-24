// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

extern "C" {
#include "procutils.h"
#include <unistd.h>
}

TEST(ProcUtilsTest, proc_read) {

  ProcStatus procstat;
  DDRes res = proc_read(&procstat);
  ASSERT_TRUE(IsDDResOK(res));
  printf("pid: %d\n", procstat.pid);
  printf("rss: %lu\n", procstat.rss);
  printf("user: %lu\n", procstat.utime);
  printf("cuser: %lu\n", procstat.cutime);
}

TEST(ProcUtilsTest, check_file_type) {
  char buf[1024] = {0};
  snprintf(buf, 1024, "/proc/%d/maps", getpid());
  ASSERT_TRUE(check_file_type(buf, S_IFMT));
  ASSERT_FALSE(check_file_type(buf, S_IFDIR));
  snprintf(buf, 1024, "/proc/%d", getpid());
  // directory are also files
  ASSERT_TRUE(check_file_type(buf, S_IFMT));
  ASSERT_TRUE(check_file_type(buf, S_IFDIR));
}
