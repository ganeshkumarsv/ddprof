// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "base_frame_symbol_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "dso_symbol_lookup.hpp"
#include "dwfl_symbol_lookup.hpp"

#include "common_mapinfo_lookup.hpp"
#include "mapinfo_lookup.hpp"

#include "ddres_def.h"
extern "C" {
#include "logger.h"
#include "stdlib.h"
}

/// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
typedef enum symbol_lookup_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} symbol_lookup_setting;

// out of namespace as these are visible on C side
// Minimal c++ structure to keep a style close to C
struct UnwindSymbolsHdr {
  void display_stats() const {
    _dwfl_symbol_lookup_v2._stats.display(_dwfl_symbol_lookup_v2.size());
  }
  void cycle() { _dwfl_symbol_lookup_v2._stats.reset(); }

  ddprof::BaseFrameSymbolLookup _base_frame_symbol_lookup;
  ddprof::CommonSymbolLookup _common_symbol_lookup;
  ddprof::DsoSymbolLookup _dso_symbol_lookup;

  ddprof::DwflSymbolLookup_V2 _dwfl_symbol_lookup_v2;
  ddprof::SymbolTable _symbol_table;

  ddprof::CommonMapInfoLookup _common_mapinfo_lookup;
  ddprof::MapInfoLookup _mapinfo_lookup;
  ddprof::MapInfoTable _mapinfo_table;

  struct ddprof::DwflSymbolLookupStats _stats;
  symbol_lookup_setting _setting;
};
