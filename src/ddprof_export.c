#include "ddprof_export.h"

#include <x86intrin.h>

#include <ddprof/dd_send.h>
#include <ddprof/pprof.h>

#include "ddprof_consts.h"
#include "ddprof_stats.h"
#include "ddres.h"
#include "procutils.h"

void print_diagnostics() {

  ddprof_stats_print();
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

void ddprof_procfs_scrape(DDProfContext *ctx) {
  DDRes lddres = proc_read(&ctx->proc_status);
  if (!IsDDResFatal(lddres)) {
    ProcStatus *procstat = &ctx->proc_status;

    ddprof_stats_set(STATS_PROCFS_RSS, 1024 * procstat->rss);
    ddprof_stats_set(STATS_PROCFS_UTIME, procstat->utime);
  }
}

DDRes ddprof_export(DDProfContext *ctx, int64_t now) {
  DDReq *ddr = ctx->ddr;
  DProf *dp = ctx->dp;

  // Before any state gets reset, export metrics to statsd
  // TODO actually do that

  // And emit diagnostic output (if it's enabled)
  print_diagnostics();

  LG_NTC("Pushing samples to backend");
  int ret = 0;
  if ((ret = DDR_pprof(ddr, dp)))
    LG_ERR("Error enqueuing pprof (%s)", DDR_code2str(ret));
  DDR_setTimeNano(ddr, dp->pprof.time_nanos, now);
  if ((ret = DDR_finalize(ddr)))
    LG_ERR("Error finalizing export (%s)", DDR_code2str(ret));
  if ((ret = DDR_send(ddr)))
    LG_ERR("Error sending export (%s)", DDR_code2str(ret));
  if ((ret = DDR_watch(ddr, -1)))
    LG_ERR("Error watching (%d : %s)", ddr->res.code, DDR_code2str(ret));
  DDR_clear(ddr);

  // Prepare pprof for next window
  pprof_timeUpdate(dp);

  return ddres_init();
}

void ddprof_aggregate(const UnwindOutput *uw_output, uint64_t sample_period,
                      int pos, int num_watchers, DProf *dp) {
  uint64_t id_locs[DD_MAX_STACK] = {0};

  const FunLoc *locs = uw_output->locs;
  for (uint64_t i = 0, j = 0; i < uw_output->idx; i++) {
    const FunLoc *current_loc = &locs[i];
    uint64_t id_map, id_fun, id_loc;

    // Using the sopath instead of srcpath in locAdd for the DD UI
    id_map = pprof_mapAdd(dp, current_loc->map_start, current_loc->map_end,
                          current_loc->map_off, current_loc->sopath, "");
    id_fun = pprof_funAdd(dp, current_loc->funname, current_loc->funname,
                          current_loc->srcpath, 0);
    id_loc = pprof_locAdd(dp, id_map, 0, (uint64_t[]){id_fun},
                          (int64_t[]){current_loc->line}, 1);
    if (id_loc > 0)
      id_locs[j++] = id_loc;
  }
  int64_t sample_val[MAX_TYPE_WATCHER] = {0};
  sample_val[pos] = sample_period;
  pprof_sampleAdd(dp, sample_val, num_watchers, id_locs, uw_output->idx);
}
