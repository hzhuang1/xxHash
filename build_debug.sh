#!/bin/sh
CLANG_13_PATH=/opt/toolchain/clang+llvm-13.0.0-aarch64-linux-gnu/bin/
cd debug
rm -f ./a.out debug_neon debug_sve
PATH=$PATH:$CLANG_13_PATH \
	clang -march=armv8.6-a+simd -O3 -o debug_neon -g debug.c
./debug_neon
PATH=$PATH:$CLANG_13_PATH \
	clang -march=armv8.6-a+sve -O3 -o debug_sve -g debug_sve.c
./debug_sve
