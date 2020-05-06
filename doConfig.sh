#!/bin/bash
[[ $# -ne 1 ]] && echo "Usage: $0 <path to source>" && return 1
src=$1

cmake -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
      -DCUDA_NVCC_FLAGS:STRING="-G -Xcompiler -rdynamic" \
      -DCMAKE_CXX_COMPILER=mpicxx \
      -DCMAKE_BUILD_TYPE=Debug \
      -DIS_TESTING=OFF \
      -DTEST_DATA_DIR=$src/gitrm-data \
      $src

