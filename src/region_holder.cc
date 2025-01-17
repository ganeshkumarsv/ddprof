// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "region_holder.hpp"

extern "C" {
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ddres.h"
#include "logger.h"
}

namespace ddprof {
RegionHolder::RegionHolder() : _region(nullptr), _sz(0), _type(dso::kUndef) {}

RegionHolder::RegionHolder(const std::string &full_path, size_t sz,
                           uint64_t pgoff, dso::DsoType path_type)
    : _region(nullptr), _sz(0), _type(path_type) {
  if (path_type == dso::kVdso) {
    LG_DBG("Found a VDSO region");
    // use auxiliary vector
    _region = (void *)(uintptr_t)getauxval(AT_SYSINFO_EHDR);
    _sz = 4096;
  } else if (path_type == dso::kVsysCall) {
    LG_DBG("Found a VSYSCALL region");
    _region = (void *)0xffffffffff600000;
    _sz = 4096;
  } else if (path_type == dso::kStandard) {
    int fd = open(full_path.c_str(), O_RDONLY);
    if (fd != -1) { //
      _region = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, pgoff);
      close(fd);
      if (!_region) {
        LG_ERR("Unable to mmap region");
      } else {
        _sz = sz;
      }
    } else {
      LG_ERR("Unable to read file : %s", full_path.c_str());
    }
  } else {
    LG_ERR("Attempt to map unknown file type: %s (%lx/%lx)", full_path.c_str(),
           sz, pgoff);
  }
}

RegionHolder::~RegionHolder() {
  if (_type == dso::kStandard && _region) {
    if (munmap(_region, _sz) == -1) {
      LG_ERR("Bad parameters when munmap %p - %lu", _region, _sz);
    }
  }
}

} // namespace ddprof
