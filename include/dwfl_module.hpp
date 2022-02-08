#pragma once

extern "C" {
#include "dwfl_internals.h"
}

#include "ddprof_file_info.hpp"
#include "dso.hpp"

namespace ddprof {

struct DDProfMod {
  enum Status {
    kUnknown,
    kInconsistent,
  };

  DDProfMod() : _mod(nullptr), _low_addr(0), _high_addr(0), _status(kUnknown) {}

  explicit DDProfMod(Status inconsistent) : DDProfMod() {
    _status = inconsistent;
  }

  Dwfl_Module *_mod;
  ProcessAddress_t _low_addr;
  ProcessAddress_t _high_addr;
  Status _status;
};

// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
DDProfMod update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                        const FileInfoValue &fileInfoValue);

} // namespace ddprof
