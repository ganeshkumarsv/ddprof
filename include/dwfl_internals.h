// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// This is pretty horrible.  First of all, this file is similar to ddres.h in
// that it can be called in C mode from a C++ file, so we're inside of an
// `extern "C" {` block.
// However, we need to provide a language-appropriate implementation of the
// common C++11/C11 atomics standard.
#ifdef __cplusplus
extern "C++" {
#include <cstdint>
#include <atomic>
#define _Atomic(X) std::atomic< X >
#define atomic_uintptr_t std::atomic<std::uintptr_t>
#define atomic_size_t std::atomic<std::size_t>
}
#define _STDATOMIC_H
extern "C" {
#endif

#include "config.h"
#include "libdwflP.h"

#ifdef __cplusplus
}
#endif
