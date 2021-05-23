#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)
ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
CMD_BASE=${DIR}/release/ddprof
CMD=${CMD_BASE}
JOB="redis-runner.sh"
for arg in "$@"; do
  if [[ ${arg} == "debug" ]]; then CMD="gdb -ex run -ex 'set follow-fork-mode child' -ex 'set print pretty on' --args ${CMD_BASE}"; fi
  if [[ ${arg} == "strace" ]]; then CMD="strace -f -o /tmp/test.out -s 2500 -v ${CMD_BASE}"; fi
  if [[ ${arg} == "ltrace" ]]; then CMD="ltrace -f -o /tmp/test.out -s 2500 -n 2 -x '*' -e malloc+free ${CMD_BASE}"; fi
  if [[ ${arg} == "redis" ]]; then JOB="redis-runner.sh"; fi
  if [[ ${arg} == "collatz" ]]; then JOB="collrunner.sh"; fi
done

# Do service version stuff
VERFILE="tmp/runner.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

# Run it!
rm -rf debuglog.out
export DD_API_KEY=8ed8bb1e8a15efdb66697ac23fc00d0c
export DD_SERVICE=native-testservice_${VER}
export DD_AGENT_HOST=intake.profile.datad0g.com
#export MALLOC_TRACE=/tmp/foo
#export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so
#export MALLOC_CONF=prof:true,lg_prof_interval:25,lg_prof_sample:17
eval ${CMD} \
  -u 60.0 \
  -l debug \
  -o debuglog.out \
  ${DIR}/bench/runners/${JOB}
