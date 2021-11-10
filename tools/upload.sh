# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# Helper functions
print_help() {
  echo "ddprof upload tool

  -f
    path to local file
  -n
    what to name the object in S3
  -p
    prefix for key (e.g., ddprof/release)
  -b
    S3 bucket (default: binaries.ddbuild.io)
  -h
    print this help and exit
"
}

# Parameters
BUCKET="binaries.ddbuild.io"
PRE="ddprof/release"
NAME=""
FILE=""
DRY="no"
CMD="aws s3 cp --region us-east-1 --sse AES256"

if [ $# -eq 0 ]; then print_help && exit 0; fi
while getopts ":f:n:b:p:dh" arg; do
  case $arg in
    f)
      FILE=${OPTARG}
      ;;
    n)
      NAME=${OPTARG}
      ;;
    b)
      BUCKET=${OPTARG}
      ;;
    p)
      PRE=${OPTARG}
      ;;
    d)
      DRY="yes"
      ;;
    h)
      print_help
      exit 0
      ;;
  esac
done

if [ -z "$NAME" ]; then
  echo "No name (-n) given, error"
  exit -1
fi

if [ -z "$FILE" ]; then
  echo "No file (-f) given, error"
  exit -1
fi

if [ ! -f "$FILE" ]; then 
  echo "File ($FILE) does not exist, error"
  exit -1
fi

echo "############################ SHA 256 ###########################"
sha256sum ${FILE}
echo "################################################################"

if [ "yes" == ${DRY} ]; then
  echo ${CMD} ${FILE} s3://${BUCKET}/${PRE}/${NAME}
else
  eval ${CMD} ${FILE} s3://${BUCKET}/${PRE}/${NAME}
fi
