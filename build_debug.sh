#!/bin/sh
CLANG_13_PATH=/opt/toolchain/clang+llvm-13.0.0-aarch64-linux-gnu/bin/
cd debug
rm ./a.out
PATH=$PATH:$CLANG_13_PATH \
	clang -march=armv8.6-a+simd -O3 -g debug.c
./a.out
