#include "unwind_helpers.hpp"
#include "ddres.h"
#include "dso_hdr.hpp"
#include "unwind_symbols.hpp"

namespace ddprof {

bool max_stack_depth_reached(UnwindState *us) {
  UnwindOutput *output = &us->output;
  // +2 to keep room for common base frame
  if (output->nb_locs + 2 >= DD_MAX_STACK_DEPTH) {
    // ensure we don't overflow
    output->nb_locs = DD_MAX_STACK_DEPTH - 2;
    add_common_frame(us, CommonSymbolLookup::LookupCases::truncated_stack);
    return true;
  }
  return false;
}

static void add_virtual_frame(UnwindState *us, SymbolIdx_t symbol_idx) {
  UnwindOutput *output = &us->output;
  int64_t current_loc_idx = output->nb_locs;
  output->locs[current_loc_idx]._symbol_idx = symbol_idx;

  // API in lib mode should clarify this
  output->locs[current_loc_idx].ip = 0;

  // just add an empty element for mapping info
  output->locs[current_loc_idx]._map_info_idx =
      us->symbols_hdr->_common_mapinfo_lookup.get_or_insert(
          CommonMapInfoLookup::LookupCases::empty,
          us->symbols_hdr->_mapinfo_table);

  ++output->nb_locs;
}

void add_common_frame(UnwindState *us,
                      CommonSymbolLookup::LookupCases lookup_case) {
  add_virtual_frame(us,
                    us->symbols_hdr->_common_symbol_lookup.get_or_insert(
                        lookup_case, us->symbols_hdr->_symbol_table));
}

void add_dso_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc) {
  add_virtual_frame(us,
                    us->symbols_hdr->_dso_symbol_lookup.get_or_insert(
                        pc, dso, us->symbols_hdr->_symbol_table));
}

void add_virtual_base_frame(UnwindState *us) {
  add_virtual_frame(us,
                    us->symbols_hdr->_base_frame_symbol_lookup.get_or_insert(
                        us->pid, us->symbols_hdr->_symbol_table,
                        us->symbols_hdr->_dso_symbol_lookup, *us->dso_hdr));
}

// read a word from the given stack
bool memory_read(ProcessAddress_t addr, ElfWord_t *result, void *arg) {
  *result = 0;
  struct UnwindState *us = (UnwindState *)arg;

  if ((addr & 0x7) != 0) {
    // The address is not 8-bit aligned here
    LG_WRN("Addr is not aligned 0x%lx", addr);
    return false;
  }

  // Check for overflow, which won't be captured by the checks below.  Sometimes
  // addr is un-physically high and we don't know why yet.
  if (addr > addr + sizeof(ElfWord_t)) {
    LG_WRN("Overflow in addr 0x%lx", addr);
    return false;
  }

  // stack grows down, so end of stack is start
  uint64_t sp_start = us->initial_regs.esp;
  uint64_t sp_end = sp_start + us->stack_sz;

  if (addr < sp_start && addr > sp_start - 4096) {
    // libdwfl might try to read values which are before our snapshot of the
    // stack.  Because the stack has the growsdown property and has a max size,
    // the pages before the current top of the stack are safe (no DSOs will
    // ever be mapped there on Linux, even if they actually did fit in the
    // single page before the top of the stack).  Avoiding these reads allows
    // us to prevent unnecessary backpopulate calls.
    LG_DBG("Invalid stack access:%lu before ESP", sp_start - addr);
    return false;
  } else if (addr < sp_start || addr + sizeof(ElfWord_t) > sp_end) {
    // If we're here, we're not in the stack.  We should interpet addr as an
    // address in VM, not as a file offset.
    // Strongly assumes we're also in an executable region?
    DsoFindRes find_res =
        us->dso_hdr->pid_read_dso(us->pid, result, sizeof(ElfWord_t), addr);
    if (!find_res.second) {
      // Some regions are not handled
      LG_DBG("Couldn't get read 0x%lx from %d, (0x%lx, 0x%lx)[%p, %p]", addr,
             us->pid, sp_start, sp_end, us->stack, us->stack + us->stack_sz);
    }
    return find_res.second;
  }

  // If we're here, we're going to read from the stack.  Just the same, we need
  // to protect stack reads carefully, so split the indexing into a
  // precomputation followed by a bounds check
  uint64_t stack_idx = addr - sp_start;
  if (stack_idx > addr) {
    LG_WRN("Stack miscalculation: %lu - %lu != %lu", addr, sp_start, stack_idx);
    return false;
  }
  *result = *(ElfWord_t *)(us->stack + stack_idx);
  return true;
}

} // namespace ddprof
