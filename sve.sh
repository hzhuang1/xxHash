#!/bin/bash -e
CLANG_PATH=/opt/toolchain/clang+llvm-13.0.0-aarch64-linux-gnu/bin/
#CLANG_PATH=/opt/toolchain/clang+llvm-15.0.6-aarch64-linux-gnu/bin/
OPTION="sve+asm"
#OPTION="sve"
#OPTION="neon+scalar"
#OPTION="neon"
#OPTION="scalar"
NEON_FULL_LANES="-DXXH3_NEON_LANES=8"
#CPU_OPTION="-mcpu=neoverse-n1"
#CPU_OPTION="-mcpu=neoverse-v1"
#SVE_BITS="-msve-vector-bits=128"
ARCH_OPTION=1
export CC=clang
#export CC=gcc

PWD=`pwd`
WORKSPACE=$PWD

check_option() {
	case $OPTION in
	"sve+asm")
		if [ $ARCH_OPTION ]; then
			MARCH="-march=armv8-a+sve"
		fi
		export PATH=$PATH:$CLANG_PATH
		export CPP_FLAGS="-DXXH_VECTOR=XXH_SVE"
		export CFLAGS="-O3 $MARCH -fPIC -DXXH_VECTOR=XXH_SVE"
		export DISPATCH=1
		;;
	"sve")
		if [ $ARCH_OPTION ]; then
			MARCH="-march=armv8-a+sve"
		fi
		export PATH=$PATH:$CLANG_PATH
		export CC=clang
		export CPP_FLAGS="-DXXH_VECTOR=XXH_SVE"
		export CFLAGS="-O3 $MARCH -fPIC -DXXH_VECTOR=XXH_SVE"
		;;
	"neon+scalar")
		if [ $ARCH_OPTION ]; then
			MARCH="-march=armv8-a+simd"
		fi
		export PATH=$PATH:$CLANG_PATH
		export CC=clang
		export CPP_FLAGS="-DXXH_VECTOR=XXH_NEON"
		export CFLAGS="-O3 $MARCH -fPIC -DXXH_VECTOR=XXH_NEON"
		;;
	"neon")
		if [ $ARCH_OPTION ]; then
			MARCH="-march=armv8-a+simd"
		fi
		export PATH=$PATH:$CLANG_PATH
		export CC=clang
		export CPP_FLAGS="-DXXH_VECTOR=XXH_NEON $NEON_FULL_LANES"
		export CFLAGS="-O3 $MARCH -fPIC -DXXH_VECTOR=XXH_NEON $NEON_FULL_LANES"
		;;
	"scalar")
		if [ $ARCH_OPTION ]; then
			MARCH="-march=armv8-a+nosimd"
		fi
		export PATH=$PATH:$CLANG_PATH
		export CC=clang
		export CPP_FLAGS="-DXXH_VECTOR=XXH_SCALAR"
		export CFLAGS="-O3 $MARCH -fPIC -DXXH_VECTOR=XXH_SCALAR"
		;;
	esac
}

build_xxhash() {
	cd $WORKSPACE
	make clean test
	make clean xxhsum
	make clean c90test
	#make clean test-mem
	make clean usan
	make clean lint-unicode
	#make clean cppcheck
}

build_bench() {
	cd $WORKSPACE/tests/bench
	rm -f ./benchHash
	make clean
	make
}

run_bench() {
	#./benchHash --mins=2 --maxs=2 --minl=9 --maxl=12
	./benchHash --mins=2 --maxs=2
}

build_debug() {
	cd $WORKSPACE/debug
	rm -f debug_sve *.o ../*.o
	$CC $CFLAGS -o cal_sve.o -c cal_sve.S
	$CC $CFLAGS $TEST_FLAGS -o xxh_aarch64dispatch.o -c ../xxh_aarch64dispatch.S
	$CC $CFLAGS $TEST_FLAGS -o debug_sve.o -c debug_sve.c
	$CC $CFLAGS $TEST_FLAGS -o debug_sve debug_sve.o cal_sve.o xxh_aarch64dispatch.o
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

# speaker beep
echo -ne '\007'
