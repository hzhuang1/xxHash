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

PWD=`pwd`
WORKSPACE=$PWD

check_option() {
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
	make AARCH64_DISPATCH=1 test
}

build_bench() {
	cd $WORKSPACE/tests/bench
	rm -f ./benchHash
	make clean
	make
}

run_bench() {
	./benchHash --mins=2 --maxs=2 --minl=9 --maxl=12
}

build_debug() {
	cd $WORKSPACE/debug
	rm -f debug_sve *.o ../*.o
	as -o cal_sve.o cal_sve.S
	$CC $CFLAGS $TEST_FLAGS -o xxh_aarch64dispatch.o -c ../xxh_aarch64dispatch.S
	$CC $CFLAGS $TEST_FLAGS -o debug_sve debug_sve.c cal_sve.o xxh_aarch64dispatch.o
}

run_debug() {
	cd $WORKSPACE/debug
	./debug_sve $SVE_OPT
}

usage() {
	echo "./sve.sh -b         \"Run xxhash bench case\""
	echo "         -t         \"Run xxhash test case\""
	echo "         -d func    \"Run debug case to verify function\""
	echo "         -d perf    \"Run debug case for performance\""
	echo "         -d nostore \"Run debug case without store\""
}

while getopts 'bhtd:' arg
do
	case $arg in
	b)
		ACTION="xxhash_bench"
		;;
	h)
		usage
		;;
	t)
		ACTION="xxhash_test"
		;;
	d)
		ACTION="debug"
		case $OPTARG in
		"func")
			;;
		"nostore")
			# performance test without store instructions
			export TEST_FLAGS="-DDEBUG_NOSTORE=1"
			export SVE_OPT="-p"
			;;
		"perf")
			export SVE_OPT="-p"
			;;
		esac
		;;
	esac
done

check_option

[ -z $ACTION ] && usage

case $ACTION in
	"xxhash_bench")
		build_bench
		run_bench
		;;
	"xxhash_test")
		build_xxhash
		;;
	"debug")
		build_debug
		run_debug
		;;
esac
