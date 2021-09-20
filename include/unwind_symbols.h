#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "ddprof_defs.h"
#include "ddres_def.h"
#include "string_view.h"

#include <sys/types.h>

struct Dwfl_Module;
struct UnwindSymbolsHdr;

/// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
typedef enum ipinfo_lookup_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} ipinfo_lookup_setting;

DDRes unwind_symbols_hdr_init(struct UnwindSymbolsHdr **symbols_hdr);

void unwind_symbols_hdr_free(struct UnwindSymbolsHdr *symbols_hdr);

DDRes unwind_symbols_hdr_clear(struct UnwindSymbolsHdr *symbols_hdr);

// Takes a dwarf module and an instruction pointer
// Lookup if this instruction pointer was already encountered. If not, create a
// new element in the table
DDRes ipinfo_lookup_get(struct UnwindSymbolsHdr *symbols_hdr,
                        struct Dwfl_Module *mod, ElfAddress_t newpc, pid_t pid,
                        IPInfoIdx_t *ipinfo_idx);

DDRes mapinfo_lookup_get(struct UnwindSymbolsHdr *symbols_hdr,
                         const struct Dwfl_Module *mod,
                         MapInfoIdx_t *map_info_idx);

#ifdef __cplusplus
}
#endif