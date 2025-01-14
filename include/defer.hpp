// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <utility>

namespace details {

struct DeferDummy {};

template <typename F> class DeferHolder {
public:
  DeferHolder(DeferHolder &&) = default;
  DeferHolder(const DeferHolder &) = delete;
  DeferHolder &operator=(DeferHolder &&) = delete;
  DeferHolder &operator=(const DeferHolder &) = delete;

  template <typename T>
  explicit DeferHolder(T &&f) : _func(std::forward<T>(f)) {}

  ~DeferHolder() {
    if (_active) {
      _func();
    }
  }

  void release() { _active = false; }

private:
  F _func;
  bool _active = true;
};

template <class F> DeferHolder<F> operator*(DeferDummy, F &&f) {
  return DeferHolder<F>{std::forward<F>(f)};
}

} // namespace details

template <class F> details::DeferHolder<F> make_defer(F &&f) {
  return details::DeferHolder<F>{std::forward<F>(f)};
}

#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer                                                                  \
  [[gnu::unused]] const auto &DEFER(__COUNTER__) = details::DeferDummy{} *[&]()
