#ifndef _H_procutils
#define _H_procutils

#include <assert.h>
#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ISO C does not allow you to cast a function pointer to an object pointer.
// But it DOES allow you to cast a function pointer to a different, incompatible
// one, and unions don't care. Sometimes this is useful.
typedef union hackptr {
  void (*fun)(void);
  void *ptr;
  uint64_t num;
} hackptr;

typedef enum MapMode {
  PUMM_READ = 1 << 0,
  PUMM_WRITE = 1 << 1,
  PUMM_EXEC = 1 << 2,
  PUMM_COW = 1 << 3, // 0 if private (CoW), 1 if shared
  PUMM_HEAP = 1 << 4,
  PUMM_STACK = 1 << 5,
  PUMM_VDSO = 1 << 6,
  PUMM_ANON = 1 << 7, // Not a file and not special
  PUMM_SPECIAL = PUMM_STACK | PUMM_HEAP | PUMM_VDSO
} MapMode;

typedef struct Map {
  uint64_t start; // Start of the segment in virtual memory
  uint64_t end;   // End of the segment in virtual memory
  uint64_t off;   // Offset into the file of the segment
  char *path;     // path WITHIN THE PID MNT NS; has to be readjusted to caller
  MapMode mode;
  void *map; // an mmap() of the segment
} Map;

typedef struct ProcStatus {
  int pid;
  char comm[32];
  char state;
  int ppid;
  int pgrp;
  int session;
  int tty_nr;
  int tpgid;
  unsigned int flags;
  unsigned long minflt;
  unsigned long cminflt;
  unsigned long majflt;
  unsigned long cmajflt;
  unsigned long utime;
  unsigned long stime;
  unsigned long cutime;
  unsigned long cstime;
  unsigned long priority;
  unsigned long nice;
  unsigned long num_threads;
  unsigned long itrealvalue;
  unsigned long long starttime;
  unsigned long vsize;
  unsigned long rss;
  unsigned long rsslim;
  unsigned long startcode;
  unsigned long endcode;
  unsigned long startstack;
  unsigned long kstkesp;
  unsigned long kstkeip;
  unsigned long signal;
  unsigned long blocked;
  unsigned long sigignore;
  unsigned long sigcatch;
  unsigned long wchan;
  unsigned long nswap;
  unsigned long cnswap;
  int exit_signal;
  int processor;
  unsigned int rt_priority;
  unsigned int policy;
  unsigned long long delayacct_blkio_ticks;
  unsigned long guest_time;
  unsigned int cguest_time; // marked as unsigned int in docs
  unsigned long start_data;
  unsigned long end_data;
  unsigned long start_brk;
  unsigned long arg_start;
  unsigned long arg_end;
  unsigned long env_start;
  unsigned long env_end;
  int exit_code;
} ProcStatus;


#define PM_MAX 512
typedef struct PidMap {
  pid_t pid;
  Map map[PM_MAX]; // TODO make this dynamic
  size_t n_map;    // How many are populated
} PidMap;

/*
 *  Global Map Cache
 *
 *  Table-encoded tree of PID relationships and corresponding maps.  If a pid
 * entry has an empty map, that means inherit from parent.
 */
#define MC_MAX 1024
typedef struct MapCache {
  pid_t pid[MC_MAX];   // pid->index reverse lookup; TODO make this dynamic
  PidMap maps[MC_MAX]; // TODO make this dynamic
  size_t sz;           // How many are populated
  MapMode whitelist;   // Disallow these types
} MapCache;

size_t mapcache_Find(pid_t);
void pidmap_Set(pid_t, PidMap *);
void mapcache_MaskSet(MapMode);
size_t mapcache_Set(pid_t);
pid_t procfs_ppid(pid_t);
PidMap *mapcache_Get(pid_t);
int procfs_MapOpen(pid_t);
char *procfs_RootGet(pid_t);
char procfs_MmapGet(Map *);
ssize_t procfs_MapRead(Map *, void *, size_t, size_t);
void procfs_MapPrint(Map *);
void procfs_PidMapPrintProc(pid_t);
void procfs_PidMapPrint(pid_t);
Map *procfs_MapMatch(pid_t, uint64_t);
Map *procfs_MapScan(char *, Map *);
ProcStatus *proc_read();
#endif
