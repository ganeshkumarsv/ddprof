// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ddprof_context.h"
#include "worker_attr.h"

typedef struct MLWorkerFlags {
  volatile bool restart_worker;
  volatile bool errors;
} MLWorkerFlags;

/**
 * Continuously poll for new events and process them accordingly
 *
 * The main loop handles forking to contain potential memory growth.
 *
 * @param pevent_hdr objects to manage incoming events and api with
 * perf_event_opem
 * @param WorkerAttr set of functions to customize worker behaviour
 *
 * @return
 */
DDRes main_loop(const WorkerAttr *, DDProfContext *);

// Same as main loop without any forks
void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx);

#ifdef __cplusplus
}
#endif
