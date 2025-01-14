// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>

#include "ddres.h"
#include "logger.h"
#include "statsd.h"

#define X_ENUM(a, b, c) STATS_##a,
#define STATS_TABLE(X)                                                         \
  X(EVENT_COUNT, "event.count", STAT_GAUGE)                                    \
  X(EVENT_LOST, "event.lost", STAT_GAUGE)                                      \
  X(SAMPLE_COUNT, "sample.count", STAT_GAUGE)                                  \
  X(CPU_TIME, "cpu.time", STAT_GAUGE)                                          \
  X(UNWIND_TICKS, "unwind.ticks", STAT_GAUGE)                                  \
  X(UNWIND_FRAMES, "unwind.frames", STAT_GAUGE)                                \
  X(UNWIND_ERRORS, "unwind.errors", STAT_GAUGE)                                \
  X(PROCFS_RSS, "procfs.rss", STAT_GAUGE)                                      \
  X(PROCFS_UTIME, "procfs.utime", STAT_GAUGE)                                  \
  X(PPROF_ST_ELEMS, "pprof.st_elements", STAT_GAUGE)                           \
  X(DSO_UNHANDLED_SECTIONS, "dso.unhandled_sections", STAT_GAUGE)              \
  X(DSO_NEW_DSO, "dso.new", STAT_GAUGE)                                        \
  X(DSO_SIZE, "dso.size", STAT_GAUGE)                                          \
  X(DSO_MAPPED, "dso.mapped", STAT_GAUGE)

// Expand the enum/index for the individual stats
typedef enum DDPROF_STATS { STATS_TABLE(X_ENUM) STATS_LEN } DDPROF_STATS;
#undef X_ENUM

// Necessary for initializing the backend store for stats.  It's necessary that
// this is called prior to any fork() calls where the children might want to use
// stats, but it's fine to call this after forks have spawned.
DDRes ddprof_stats_init(void);

// Whereas the regions are inherited by forks, freeing the backend store is not.
// That means that free() must be called from every process wishing to clean
// its own store (although this is a small array of longs, so probably not too
// important)
DDRes ddprof_stats_free();

// The add operator is multithread- and multiprocess-safe.  `out` can be NULL.
DDRes ddprof_stats_add(unsigned int stat, long in, long *out);

// Setting and clearing are last-through-the-gate operations with ties broken
// by whatever the CPU is executing at that time.
DDRes ddprof_stats_set(unsigned int stat, long in);
DDRes ddprof_stats_clear(unsigned int stat);

// Merely gets the value of the statistic.
DDRes ddprof_stats_get(unsigned int stat, long *out);

// Send all the registered values
DDRes ddprof_stats_send(const char *statsd_socket);

// Print all known stats to the configured log
void ddprof_stats_print();
