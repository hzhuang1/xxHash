#!/bin/bash -e
CLANG13_PATH=/opt/toolchain/clang+llvm-13.0.0-aarch64-linux-gnu/bin/
OPTION="clang13+sve+arch"
#OPTION="clang13+neon+arch"
#OPTION="clang13+scalar+arch"
#OPTION="clang13+sve"
#OPTION="clang13+scalar"
#OPTION="gcc11+sve+arch"
#OPTION="gcc11+neon+arch"
#OPTION="gcc11+scalar+arch"
#OPTION="gcc11+sve"
#OPTION="gcc11+neon"
#OPTION="gcc11+scalar"
DEBUG_NOSTORE=1

PWD=`pwd`
WORKSPACE=$PWD

check_option() {
	if [ ${DEBUG_NOSTORE} ]; then
		export TEST_FLAGS="-DDEBUG_NOSTORE=1"
	fi
	case $OPTION in
	"clang13+sve+arch")
		export PATH=$PATH:$CLANG13_PATH
		export CC=clang
		export CPP_FLAGS=XXH_VECTOR=XXH_SVE
		export CFLAGS="-O3 -march=armv8-a+sve -fPIC"
		;;
	"clang13+neon+arch")
		export PATH=$PATH:$CLANG13_PATH
		export CC=clang
		export CPP_FLAGS=XXH_VECTOR=XXH_NEON
		export CFLAGS="-O3 -march=armv8-a+simd -fPIC"
		;;
	"clang13+scalar+arch")
		export PATH=$PATH:$CLANG13_PATH
		export CC=clang
		export CPP_FLAGS=XXH_VECTOR=XXH_SCALAR
		export CFLAGS="-O3 -march=armv8-a+nosimd -fPIC"
		;;
	"clang13+sve")
		export PATH=$PATH:$CLANG13_PATH
		export CC=clang
		export CPP_FLAGS=XXH_VECTOR=XXH_SVE
		export CFLAGS="-O3 -fPIC"
		;;
	"clang13+scalar")
		export PATH=$PATH:$CLANG13_PATH
		export CC=clang
		export CPP_FLAGS=XXH_VECTOR=XXH_SCALAR
		export CFLAGS="-O3 -fPIC"
		;;
	"gcc11+sve+arch")
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_SVE
		export CFLAGS="-O3 -march=armv8-a+sve -fPIC"
		;;
	"gcc11+neon+arch")
		export PATH=$PATH:$CLANG13_PATH
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_NEON
		export CFLAGS="-O3 -march=armv8-a+simd -fPIC"
		;;
	"gcc11+scalar+arch")
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_SCALAR
		export CFLAGS="-O3 -march=armv8-a+nosimd -fPIC"
		;;
	"gcc11+sve")
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_SVE
		export CFLAGS="-O3 -fPIC"
		;;
	"gcc11+neon")
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_NEON
		export CFLAGS="-O3 -fPIC"
		;;
	"gcc11+scalar")
		export CC=gcc
		export CPP_FLAGS=XXH_VECTOR=XXH_SCALAR
		export CFLAGS="-O3 -fPIC"
		;;
	esac
}

build_xxhash() {
	cd $WORKSPACE
	make clean
	make test
}

build_bench() {
	cd $WORKSPACE/tests/bench
	rm -f ./benchHash
	make clean
	make
}

run_bench() {
	./benchHash --n=1 --mins=2 --maxs=2
}

build_debug() {
	cd $WORKSPACE/debug
	rm -f debug_sve
	$CC $CFLAGS $TEST_FLAGS -o debug_sve debug_sve.c
}

run_debug() {
	cd $WORKSPACE/debug
	./debug_sve -p
}

check_option
build_xxhash
build_debug
run_debug
build_bench
run_bench

#make clean
#PATH=$PATH:$CLANG13_PATH CPP_FLAGS=XXH_VECTOR=XXH_SVE DEBUGFLAGS="-march=armv8-a+sve" make test

#make clean
#PATH=$PATH:$CLANG13_PATH CPP_FLAGS=XXH_VECTOR=XXH_SVE CFLAGS="-march=armv8-a+sve" make clangtest
#PATH=$PATH:$CLANG13_PATH CPP_FLAGS=XXH_VECTOR=XXH_SVE CFLAGS="-march=armv8.6-a+sve -msve-vector-bits=256" make clangtest

#cd tests/bench
#echo "run make 3"
#make clean
#make
#CFLAGS="-march=armv8-a+nosimd" make
#CFLAGS="-march=armv8-a+nosimd" make
#PATH=$PATH:$CLANG13_PATH CC=clang CFLAGS="-march=armv8-a+sve" make
#PATH=$PATH:$CLANG13_PATH CC=clang CFLAGS="-march=armv8-a+nosimd" make
#PATH=$PATH:$CLANG13_PATH CC=clang CFLAGS="-march=armv8-a+sve -no-fvectorize" make
#PATH=$PATH:$CLANG13_PATH CC=clang CFLAGS="-march=armv8.6-a+sve -msve-vector-bits=256" make

#./benchHash --n=1 --mins=2 --maxs=2
