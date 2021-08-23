#pragma once

#include "perf_option.h"
#include "proc_state.h"
#include <sys/types.h>

/// Maximum number of different watcher types
#define MAX_TYPE_WATCHER 10

// forward declarations
typedef struct DDReq DDReq;
typedef struct DProf DProf;

typedef struct DDProfContext {
  DProf *dp;
  DDReq *ddr;

  // Parameters for interpretation
  char *agent_host;
  char *prefix;
  char *tags;
  char *logmode;
  char *loglevel;

  // Input parameters
  char *printargs;
  char *count_samples;
  char *enable;
  char *native_enable;
  char *upload_period;
  char *profprofiler;
  char *faultinfo;
  char *coredumps;
  char *nice;
  char *sendfinal;
  char *pid;
  char *global;
  char *worker_period;
  char *cache_period;
  struct {
    bool count_samples;
    bool enable;
    double upload_period;
    bool profprofiler;
    bool faultinfo;
    bool coredumps;
    int nice;
    bool sendfinal;
    pid_t pid;
    bool global;
    uint32_t worker_period; // exports between worker refreshes
    uint32_t cache_period;  // exports between cache clears
  } params;
  PerfOption watchers[MAX_TYPE_WATCHER];
  int num_watchers;
  struct UnwindState *us;
  ProcState proc_state;
  int64_t send_nanos;    // Last time an export was sent
  uint32_t count_worker; // exports since last cache clear
  uint32_t count_cache;  // exports since worker was spawned
} DDProfContext;