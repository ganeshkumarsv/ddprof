// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "ddprof_stats.h"
}

#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

TEST(ddprof_statsTest, Connect) {
  const char path_listen[] = "/tmp/my_statsd_listener";
  unlink(path_listen); // make sure node is available, OK if this fails

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));

  // Connect to server using ddprof_stats rather than statsd directly
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));

  // Can we teardown correctly?
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));

  // We're done
  close(fd_listener);
}

TEST(ddprof_statsTest, Reconnect) {
  const char path_listen[] = "/tmp/my_statsd_listener";
  unlink(path_listen); // make sure node is available, OK if this fails

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));

  // Connect to server using ddprof_stats rather than statsd directly
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));

  // Can we teardown correctly?
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));

  // Can we connect and teardown again safely?
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));

  // We're done
  close(fd_listener);
}

TEST(ddprof_statsTest, ConnectAndSet) {
  const char path_listen[] = "/tmp/my_statsd_listener";
  unlink(path_listen); // make sure node is available, OK if this fails

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));

  // Connect to server using ddprof_stats rather than statsd directly
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));

  // Set a valid stat
  const long stats_test_val = 12345;
  long stats_check_val = 0;
  EXPECT_TRUE(IsDDResOK(ddprof_stats_set(STATS_SAMPLE_COUNT, stats_test_val)));

  // Make sure it actually got set!
  EXPECT_TRUE(
      IsDDResOK(ddprof_stats_get(STATS_SAMPLE_COUNT, &stats_check_val)));
  EXPECT_EQ(stats_test_val, stats_check_val);

  // Disconnect and close
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));
  close(fd_listener);
}

TEST(ddprof_statsTest, Arithmetic) {
  const char path_listen[] = IPC_TEST_DATA "/my_statsd_listener.sock";
  unlink(path_listen); // make sure node is available, OK if this fails

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));

  // Connect to server using ddprof_stats rather than statsd directly
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));

  // Set a valid stat
  const long stats_test_val = 12345;
  long stats_check_val = 0;
  EXPECT_TRUE(IsDDResOK(ddprof_stats_set(STATS_SAMPLE_COUNT, stats_test_val)));

  // Make sure it actually got set!
  EXPECT_TRUE(
      IsDDResOK(ddprof_stats_get(STATS_SAMPLE_COUNT, &stats_check_val)));
  EXPECT_EQ(stats_test_val, stats_check_val);

  // Increment and compare
  EXPECT_TRUE(IsDDResOK(
      ddprof_stats_add(STATS_SAMPLE_COUNT, stats_test_val, &stats_check_val)));
  EXPECT_EQ(2 * stats_test_val, stats_check_val);

  EXPECT_TRUE(IsDDResOK(ddprof_stats_send(path_listen)));

  // Disconnect and close
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));
  close(fd_listener);
}

TEST(ddprof_statsTest, BadConnection) {
  const char path_listen[] = "/tmp/my_statsd_listener";
  const char path_try[] = "/tmp/my_statsd_Listener";
  unlink(path_listen); // make sure node is available, OK if this fails
  unlink(path_try);

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));
  EXPECT_TRUE(IsDDResOK(ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL)));

  // Only prints a log
  EXPECT_TRUE(IsDDResOK(ddprof_stats_send(path_try)));

  close(fd_listener);
}

TEST(ddprof_statsTest, MiscellaneousStats) {
  const char path_listen[] = "/tmp/my_statsd_listener";
  unlink(path_listen); // make sure node is available, OK if this fails

  // Initiate "server"
  int fd_listener;
  EXPECT_TRUE(
      IsDDResOK(statsd_listen(path_listen, strlen(path_listen), &fd_listener)));
  EXPECT_TRUE(IsDDResOK(ddprof_stats_init()));

  // Submit a bad metric
  EXPECT_FALSE(IsDDResOK(ddprof_stats_set(STATS_LEN, 404)));

  // Set a value, receiving into NULL
  EXPECT_TRUE(IsDDResOK(ddprof_stats_set(STATS_SAMPLE_COUNT, 1)));
  EXPECT_TRUE(IsDDResOK(ddprof_stats_get(STATS_SAMPLE_COUNT, NULL)));

  // Increment a value, receiving into NULL
  EXPECT_TRUE(IsDDResOK(ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL)));

  // We're done
  EXPECT_TRUE(IsDDResOK(ddprof_stats_free()));
  close(fd_listener);
}
