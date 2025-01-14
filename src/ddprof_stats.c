// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <ddprof_stats.h>

// Expand the statsd paths
#define X_PATH(a, b, c) "datadog.profiling.native." b,
static const char *stats_paths[] = {STATS_TABLE(X_PATH)};
#undef X_PATH

// Expand the types
#define X_TYPES(a, b, c) c,
static const unsigned int stats_types[] = {STATS_TABLE(X_TYPES)};
#undef X_TYPES

// Region (to be mmap'd here) for backend store
long *ddprof_stats = NULL;

DDRes ddprof_stats_init(void) {
  // This interface cannot be used to reset the existing mapping; to do so free
  // and then re-initialize.

  if (ddprof_stats)
    return ddres_init();

  ddprof_stats = mmap(NULL, sizeof(long) * STATS_LEN, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == ddprof_stats) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_DDPROF_STATS, "Unable to mmap for stats");
  }

  // When we initialize the stats, we should zero out the region
  memset(ddprof_stats, 0, sizeof(long) * STATS_LEN);

  // Perform other initialization (returns warnings on statsd failure)
  return ddres_init();
}

DDRes ddprof_stats_free() {
  if (ddprof_stats)
    DDRES_CHECK_INT(munmap(ddprof_stats, sizeof(long) * STATS_LEN),
                    DD_WHAT_DDPROF_STATS, "Error from munmap");
  ddprof_stats = NULL;

  return ddres_init();
}

DDRes ddprof_stats_add(unsigned int stat, long in, long *out) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");

  long retval = __sync_add_and_fetch(&ddprof_stats[stat], in);

  if (out)
    *out = retval;
  return ddres_init();
}

DDRes ddprof_stats_set(unsigned int stat, long n) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");
  ddprof_stats[stat] = n;
  return ddres_init();
}

DDRes ddprof_stats_clear(unsigned int stat) {
  return ddprof_stats_set(stat, 0);
}

DDRes ddprof_stats_clear_all() {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");

  // Note:  we leave the DDRes returns here uncollected, since the loop bounds
  //        are strongly within the ddprof_stats bounds and we've already
  //        verified the presence of the backend store.  These are the only two
  //        non-success criteria for the individual clear operations.
  for (int i = 0; i < STATS_LEN; i++)
    ddprof_stats_clear(i);

  return ddres_init();
}

DDRes ddprof_stats_get(unsigned int stat, long *out) {
  if (!ddprof_stats)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Stats backend uninitialized");
  if (stat >= STATS_LEN)
    DDRES_RETURN_WARN_LOG(DD_WHAT_DDPROF_STATS, "Invalid stat");

  if (out)
    *out = ddprof_stats[stat];
  return ddres_init();
}

DDRes ddprof_stats_send(const char *statsd_socket) {
  if (!statsd_socket) {
    LG_NTC("No statsd socket provided");
    return ddres_init();
  }
  int fd_statsd = -1;

  if (IsDDResNotOK(
          statsd_connect(statsd_socket, strlen(statsd_socket), &fd_statsd))) {
    // Invalid socket. No use trying to send data (and avoid flood of logs).
    return ddres_init();
  }

  for (unsigned int i = 0; i < STATS_LEN; i++) {
    DDRES_CHECK_FWD(statsd_send(fd_statsd, stats_paths[i], &ddprof_stats[i],
                                stats_types[i]));
  }

  return statsd_close(fd_statsd);
}

void ddprof_stats_print() {
  if (!ddprof_stats)
    return;
  for (unsigned int i = 0; i < STATS_LEN; ++i)
    LG_NTC("%s: %ld", stats_paths[i], ddprof_stats[i]);
}
