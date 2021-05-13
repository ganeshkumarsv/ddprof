#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# This script takes an executable and, presuming it ships a version string of
# the correct format and the right environment variables are in place, shoves
# it into S3
DIR=$(git rev-parse --show-toplevel)

# This script strongly assumes that the binary is executable and ships a version
# string of the exactly correct format
# Also rather strongly assumes this is running in CI... sorry!

if [ ! -z "${RELEASEBIN}" ]; then 
  $DIR/tools/upload.sh -p ${ANAME}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ /_/g' -e 's/\+/_/g')
  $DIR/tools/upload.sh -p ${ANAME}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ /_/g' -e 's/\+.*//g')
  $DIR/tools/upload.sh -p ${ANAME}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ .*//g')
fi

# No need to emit so many debug builds; just the patch is fine; for anything else we can grab it from CI
if [ ! -z "${DEBUGBIN}" ]; then
  $DIR/tools/upload.sh -p ${ANAME}/debug -f ${DEBUGBIN} -n $(${DEBUGBIN} --version | sed -e 's/ /_/g' -e 's/\+.*//g')
fi
