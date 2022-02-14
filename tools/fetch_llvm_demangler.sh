#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
if [ $# == 0 ]; then
    echo "Specify the vendor directory to fetch llvm sources"
    exit 1
fi

mkdir -p $1
cd $1
VENDOR_PATH=${PWD}

### Nuke the old llvm dir
LLVM_ROOT=${VENDOR_PATH}/llvm
rm -rf $LLVM_ROOT
mkdir -p $LLVM_ROOT/include/llvm/
mkdir -p $LLVM_ROOT/lib/

### Pull in just the parts of the llvm repo which matter for demangling.
# we make use of the fact that Github allows certain SVN operations, because
# doing this with a sparse git checkout is slightly painful
svn export --quiet https://github.com/llvm/llvm-project/trunk/llvm/include/llvm/Demangle $LLVM_ROOT/include/llvm/Demangle
svn export --quiet https://github.com/llvm/llvm-project/trunk/llvm/lib/Demangle $LLVM_ROOT/lib/Demangle