// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>
#include <stdbool.h>

#include "ddres.h"
#include "pevent.h"

typedef struct DDProfContext DDProfContext;

DDRes ddprof_worker_init(DDProfContext *arg);
DDRes ddprof_worker_free(DDProfContext *);
DDRes ddprof_worker_maybe_export(DDProfContext *arg, int64_t now_ns,
                                 bool *restart_worker);
DDRes ddprof_worker_cycle(DDProfContext *ctx, int64_t now,
                          bool synchronous_export);
DDRes ddprof_worker_process_event(struct perf_event_header *hdr, int pos,
                                  DDProfContext *arg);

// Only init unwinding elements
DDRes worker_library_init(DDProfContext *ctx);
DDRes worker_library_free(DDProfContext *ctx);
