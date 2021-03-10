#ifndef _H_unwind
#define _H_unwind

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"
#include <dwarf.h>
#include <stdbool.h>

// TODO select dwfl_internals based on preproc directive
#include "dwfl_internals.h"
#include "procutils.h"

#define MAX_STACK 1024
typedef struct FunLoc {
  uint64_t ip;        // Relative to file, not VMA
  uint64_t map_start; // Start address of mapped region
  uint64_t map_end;   // End
  uint64_t map_off;   // Offset into file
  char *funname;      // name of the function (mangled, possibly)
  char *srcpath;      // name of the source file, if known
  char *sopath;  // name of the file where the symbol is interned (e.g., .so)
  uint32_t line; // line number in file
  uint32_t disc; // discriminator
} FunLoc;

struct UnwindState {
  Dwfl *dwfl;
  pid_t pid;
  char *stack;
  size_t stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp;
      uint64_t esp;
      uint64_t eip;
    };
  };
  Map *map;
  int max_stack; // ???
  uint64_t ips[MAX_STACK];
  FunLoc locs[MAX_STACK];
  uint64_t idx;
};

#ifdef D_UNWDBG
#define D(...)                                                                 \
  fprintf(stderr, "<%s:%d> ", __FUNCTION__, __LINE__);                         \
  fprintf(stderr, __VA_ARGS__);                                                \
  fprintf(stderr, "\n")
#else
#define D(...)                                                                 \
  do {                                                                         \
  } while (0);
#endif

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != NULL) {
    return 0;
  }
  struct UnwindState *us = arg;
  *thread_argp = arg;
  return us->pid;
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Word regs[17] = {0};

  // I only save three lol
  regs[6] = us->ebp;
  regs[7] = us->esp;
  regs[16] = us->eip;

  return dwfl_thread_state_registers(thread, 0, 17, regs);
}

bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg) {
  (void)dwfl;
  struct UnwindState *us = arg;

  uint64_t sp_start = us->esp;
  uint64_t sp_end = sp_start + us->stack_sz;

  // Check for overflow, like perf
  if (addr + sizeof(Dwarf_Word) < addr) {
    return false;
  }

  if (addr < sp_start || addr + sizeof(Dwarf_Word) > sp_end) {
    // If we're here, we're not in the stack.  We should interpet addr as an
    // address in VM, not as a file offset.  This also rather strongly assumes
    // that `addr` is an address in the instrumented process' space--in other
    // words, it represents a segment that was actually mapped into the
    // given page.  If, for instance, a segment could have been mapped, but
    // wasn't, we fail (does this even make sense?)
    Map *map = procfs_MapMatch(us->pid, addr);

    // I didn't read past dso.c:dso__data_read_offset(), but it doesn't look
    // like perf will do anything to try to correct the address.
    // TODO verify?
    if (!map) {
      return false;
    }

    // TODO, signify end of stack?  dd false stack?
    return -1 != procfs_MapRead(map, result, sizeof(Dwarf_Word), addr);
  }

  *result = *(Dwarf_Word *)(&us->stack[addr - sp_start]);
  return true;
}

int frame_cb(Dwfl_Frame *state, void *arg) {
  struct UnwindState *us = arg;
  Dwarf_Addr pc = 0;
  bool isactivation = false;

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    D("%s", dwfl_errmsg(-1));
    return DWARF_CB_ABORT;
  }

  Dwarf_Addr newpc = pc - !!isactivation;

  Dwfl_Thread *thread = dwfl_frame_thread(state);
  if (!thread) {
    D("dwfl_frame_thread was zero: (%s)", dwfl_errmsg(-1));
  }
  Dwfl *dwfl = dwfl_thread_dwfl(thread);
  if (!dwfl) {
    D("dwfl_thread_dwfl was zero: (%s)", dwfl_errmsg(-1));
  }
  Dwfl_Module *mod = dwfl_addrmodule(dwfl, newpc);
  const char *symname = NULL;

  // Now we register
  if (mod) {
    GElf_Off offset = {0};
    GElf_Sym sym = {0};
    GElf_Word shndxp = {0};
    Elf *elfp = NULL;
    Dwarf_Addr bias = {0};

    symname =
        dwfl_module_addrinfo(mod, newpc, &offset, &sym, &shndxp, &elfp, &bias);

    // TODO
    us->locs[us->idx].ip = pc;
    us->locs[us->idx].map_start = mod->low_addr;
    us->locs[us->idx].map_end = mod->high_addr;
    ;
    us->locs[us->idx].map_off = offset;
    us->locs[us->idx].funname = strdup(symname ? symname : "??");
    //    us->locs[us->idx].srcpath = ;
    char *sname = strrchr(mod->name, '/');
    us->locs[us->idx].sopath = strdup(sname ? sname + 1 : mod->name);
    us->idx++;
  } else {
    D("dwfl_addrmodule was zero: (%s)", dwfl_errmsg(-1));
  }
  return DWARF_CB_OK;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

int unwindstate__unwind(struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .memory_read = memory_read,
      .set_initial_registers = set_initial_registers,
  };
  static char *debuginfo_path;
  static const Dwfl_Callbacks proc_callbacks = {
      .find_debuginfo =
          dwfl_standard_find_debuginfo, // TODO, update this for containers?
      .debuginfo_path = &debuginfo_path,
      .find_elf = dwfl_linux_proc_find_elf,
  };
  for (int i = 0; i < MAX_STACK; i++) {
    if (us->locs[i].funname)
      free(us->locs[i].funname);
    if (us->locs[i].sopath)
      free(us->locs[i].sopath);
    if (us->locs[i].srcpath)
      free(us->locs[i].srcpath);
  }
  memset(us->locs, 0, sizeof(uint64_t) * us->max_stack);

  // Initialize ELF
  D("Gonna unwind at %d (my PID is %d)\n", us->pid, getpid());
  elf_version(EV_CURRENT);

  // TODO, probably need to cache this on a pid-by-pid basis
  if (!us->dwfl && !(us->dwfl = dwfl_begin(&proc_callbacks))) {
    D("There was a problem getting the Dwfl");
    return -1;
  }

  if (dwfl_linux_proc_report(us->dwfl, us->pid)) {
    D("There was a problem reporting the module.");
    return -1;
  }

  // TODO, all of this needs to go in a PID cache, until then we're not going
  //      to cleanup since it doesn't matter
  //  if (dwfl_report_end(us->dwfl, NULL, NULL)) {
  //    D("dwfl_end was nonzero (%s)", dwfl_errmsg(-1));
  //    return -1;
  //  }

  if (!dwfl_attach_state(us->dwfl, NULL, us->pid, &dwfl_callbacks, us)) {
    //    TODO, same as above, no reason to care if attaching fails since we
    //          never detach and the first time always seems to work so far.
    //    D("Could not attach (%s)", dwfl_errmsg(-1));
    //    return -1;
  }

  if (dwfl_getthreads(us->dwfl, tid_cb, us)) {
    D("Could not get thread frames.");
    return -1;
  }

  // TODO same as above
  // dwfl_end(us->dwfl);

  return 0;
}

#endif
