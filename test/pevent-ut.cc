#include <gtest/gtest.h>

extern "C" {
#include "ddprof_context.h"
#include "perf_option.h"
#include "pevent_lib.h"

#include <sys/sysinfo.h>
#include <unistd.h>
}

void mock_ddprof_context(DDProfContext *ctx) {
  ctx->num_watchers = 1;
  ctx->params.enable = true;
  ctx->watchers[0] = *perfoptions_preset(10); // 10 is cpu time
}

TEST(PeventTest, setup_cleanup) {
  PEventHdr pevent_hdr;
  DDProfContext ctx = {0};
  pid_t mypid = getpid();
  mock_ddprof_context(&ctx);
  pevent_init(&pevent_hdr);
  DDRes res = pevent_setup(&ctx, mypid, get_nprocs(), &pevent_hdr);
  ASSERT_TRUE(IsDDResOK(res));
  ASSERT_TRUE(pevent_hdr.size == static_cast<unsigned>(get_nprocs()));
  res = pevent_cleanup(&pevent_hdr);
  ASSERT_TRUE(IsDDResOK(res));
}