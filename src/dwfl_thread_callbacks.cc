#include "dwfl_thread_callbacks.hpp"

#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != NULL) {
    return 0;
  }
  struct UnwindState *us = reinterpret_cast<UnwindState *>(arg);
  *thread_argp = arg;
  return us->pid;
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = reinterpret_cast<UnwindState *>(arg);
  // Use the current instead of initial registers to ensure we can call several
  // unwinding loops. Only 3 registers are used in the unwinding
#ifdef __x86_64__
  Dwarf_Word regs[17] = {0};
  regs[6] = us->current_regs.ebp;
  regs[7] = us->current_regs.esp;
  regs[16] = us->current_regs.eip;
  return dwfl_thread_state_registers(thread, 0, 17, regs);
#elif __aarch64__
  Dwarf_Word regs[33] = {0};
  regs[29] = us->current_regs.ebp;
  regs[30] = us->current_regs.lr; // WTF?!
  regs[31] = us->current_regs.esp;
  regs[32] = us->current_regs.eip; // WTF?!
  return dwfl_thread_state_registers(thread, 0, 33, regs);
#endif
}

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg) {
  (void)dwfl;
  return ddprof::memory_read(addr, result, arg);
}
