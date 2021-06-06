#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)

# We need to make sure this is set if we want ASAN to return symbol names
# instead of VM addresses
if [[ -z "${ASAN_SYMBOLIZER_PATH:-}" ]]; then
  if command -v llvm-symbolizer; then
    ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
  else
    ASAN_SYMBOLIZER_PATH=""
  fi
fi

# Overrides
if [[ -z "${CMD_BASE:-}" ]]; then CMD_BASE=${DIR}/release/ddprof; fi
if [[ -z "${USE_JEMALLOC:-}" ]]; then USE_JEMALLOC=""; fi
CMD=${CMD_BASE}
JOB="redis-runner.sh"

# Check for parameters
for arg in "$@"; do
  if [[ ${arg} == "debug" ]]; then CMD="gdb -ex run -ex 'set follow-fork-mode child' -ex 'set print pretty on' --args ${CMD_BASE}"; fi
  if [[ ${arg} == "strace" ]]; then CMD="strace -f -o /tmp/test.out -s 2500 -v ${CMD_BASE}"; fi
  if [[ ${arg} == "network" ]]; then CMD="strace -etrace=%network -f -o /tmp/test.out -s 2500 -v ${CMD_BASE}"; fi
  if [[ ${arg} == "ltrace" ]]; then CMD="ltrace -f -o /tmp/test.out -s 2500 -n 2 -x '*' -e malloc+free ${CMD_BASE}"; fi
  if [[ ${arg} == "jemalloc" ]]; then USE_JEMALLOC="yes"; fi
  if [[ ${arg} == "redis" ]]; then JOB="redis-runner.sh"; fi
  if [[ ${arg} == "collatz" ]]; then JOB="collrunner.sh"; fi
  if [[ ${arg} == "sleep" ]]; then JOB="sleep.sh"; fi
  if [[ ${arg} == "noexist" ]]; then JOB="fakejob.sh"; fi
  if [[ ${arg} == "noexec" ]]; then JOB="non_executable_job.sh"; fi
done

# Do service version stuff
VERFILE="tmp/runner.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

# Set any switchable environment variables
if [[ "yes" == "${USE_JEMALLOC,,}" ]]; then
  export MALLOC_TRACE=/tmp/foo
  export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so
  export MALLOC_CONF=prof:true,lg_prof_interval:25,lg_prof_sample:17
fi

# Run it!
rm -rf debuglog.out
export DD_API_KEY=8ed8bb1e8a15efdb66697ac23fc00d0c
export DD_SERVICE=native-testservice_${VER}
export DD_AGENT_HOST=intake.profile.datad0g.com
eval ${CMD} \
  -u 60.0 \
  -l debug \
  -o stderr \
  -a yes \
  ${DIR}/bench/runners/${JOB}
