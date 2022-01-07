#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#if defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#endif	/* __ARM_SVE__ */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define XXH_FORCE_INLINE	inline
#define XXH_ASSERT(c)		((void)0)
#define xxh_u64			uint64_t
#define xxh_u8			uint8_t
#define XXH_RESTRICT		restrict
#define XXH_ACC_ALIGN		16	/* 16*8 = 128 */
#define XXH_STRIPE_LEN		64
#define XXH_SECRET_CONSUME_RATE 8   /* nb of secret bytes consumed at each accumulation */
#define XXH_ACC_NB		(XXH_STRIPE_LEN / sizeof(xxh_u64))
#define XXH_readLE64(c)		XXH_read64(c)

#  define XXH_SPLIT_IN_PLACE(in, outLo, outHi)                                            \
    do {                                                                                  \
      (outLo) = vmovn_u64    (in);                                                        \
      (outHi) = vshrn_n_u64  ((in), 32);                                                  \
    } while (0)

#define XXH_PRIME32_1  0x9E3779B1U  /*!< 0b10011110001101110111100110110001 */
#define XXH_PRIME32_2  0x85EBCA77U  /*!< 0b10000101111010111100101001110111 */
#define XXH_PRIME32_3  0xC2B2AE3DU  /*!< 0b11000010101100101010111000111101 */
#define XXH_PRIME32_4  0x27D4EB2FU  /*!< 0b00100111110101001110101100101111 */
#define XXH_PRIME32_5  0x165667B1U  /*!< 0b00010110010101100110011110110001 */

#define MEASURE_LOOPS  0x10000000

typedef void (*XXH3_f_accumulate_512)(void* XXH_RESTRICT, const void*, const void*);
typedef void (*XXH3_f_scrambleAcc)(void* XXH_RESTRICT, const void*);
typedef void (*XXH3_f_initCustomSecret)(void* XXH_RESTRICT, xxh_u64);

typedef void (*f_accum)(void* XXH_RESTRICT,
		const void* XXH_RESTRICT,
		const void* XXH_RESTRICT);
typedef void (*f_scrum)(void* XXH_RESTRICT,
		const void* XXH_RESTRICT);
typedef void (*f_void)(void);

extern int asvload_03(int op);
extern int XXH3_aarch64_sve_init_accum(void);
extern void XXH3_aarch64_sve_acc512(void* XXH_RESTRICT,
				const void* XXH_RESTRICT,
				const void* XXH_RESTRICT);
extern void XXH3_aarch64_sve_accumulate(void* XXH_RESTRICT,
				const void* XXH_RESTRICT,
				const void* XXH_RESTRICT,
				size_t);
extern void XXH3_aarch64_sve_scramble(void* XXH_RESTRICT,
				const void* XXH_RESTRICT);

static unsigned char in1[64] __attribute__((aligned(256)));
static unsigned char in2[64] __attribute__((aligned(256)));
static unsigned char out1[64] __attribute__((aligned(256)));

void init_buf(unsigned char *buf, int blen)
{
	int i, len;
	unsigned char c = 0x0;
	if ((blen < 16) || (blen % 16)) {
		printf("blen is invalid:%d\n", blen);
		return;
	}
	len = blen >> 3;
	for (i = 0; i < len; i++) {
		if ((i % len) == 0)
			c += 0x10;
		buf[i] = c;
		c += 0x1;
	}
}

void clear_buf(unsigned char *buf, int blen)
{
	int i, len;
	if ((blen < 16) || (blen % 16)) {
		printf("blen is invalid:%d\n", blen);
		return;
	}
	len = blen >> 3;
	for (i = 0; i < len; i++)
		buf[i] = 0;
}

void set_buf(unsigned char *buf, unsigned char c, int blen)
{
	int i, len;
	if ((blen < 16) || (blen % 16)) {
		printf("blen is invalid:%d\n", blen);
		return;
	}
	len = blen >> 3;
	for (i = 0; i < len; i++)
		buf[i] = c;
}

void dump_bits(char *name, unsigned char *buf, int blen)
{
	int i, len;
	if ((blen < 16) || (blen % 16)) {
		printf("blen is invalid:%d\n", blen);
		return;
	}
	printf("%d-bit data [%s]:\n", blen, name);
	len = blen >> 3;
	/* Each line contains 128-bit data at most. */
	for (i = 0; i < len; i++) {
		if ((i % 16) == 15)
			printf("%02x\n", buf[i]);
		else
			printf("%02x-", buf[i]);
	}
}

#if defined(__ARM_FEATURE_SVE)

/*
 * Store data into buffer with variable length.
 * svwhilelt_b32(0,1) == svwhilelt_b32(0,2)
 * svwhilelt_b32(0,3) == svwhilelt_b32(0,4)
 * Since pg is set for xout that is associated with svuint64_t, and
 * svwhilelt_b32 sets lanes with 32-bit.
 */
void svld1_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint16_t tmp = svdup_u16(0x82);
	svbool_t pg = svwhilelt_b32(0, 1);

	svst1_u64(pg, out, svreinterpret_u64_u16(tmp));
}

/*
 * Store data into buffer with variable length.
 * svld1_02() only changes new data length.
 * If we dump the object code, we could find that the assembly
 * code of both svld1_01() and svld1_02() are exactly same.
 * That's the key point of SVE.
 */
void svld1_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint16_t tmp = svdup_u16(0x82);
	svbool_t pg = svwhilelt_b32(0, 14);

	svst1_u64(pg, out, svreinterpret_u64_u16(tmp));
}

/*
 * Swap high 32-bit data with low 32-bit data in vector.
 * The vector length is 256-bit. Since there're four svld1 options.
 */
void svswap_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xin, xout;
	svuint64_t op2 = svdup_u64(32);
	svbool_t pg = svwhilelt_b64(0, 4);
	svuint64_t swapped;

	xin = svld1_u64(pg, (uint64_t *)in1);
	xout = svlsr_u64_z(pg, xin, op2);
	swapped = svlsl_u64_z(pg, xin, op2);
	xout = svorr_u64_z(pg, xout, swapped);
	svst1_u64(pg, (uint64_t *)out, xout);
}

/*
 * Swap high 64-bit data with low 64-bit data in vector.
 * The vector length is 256-bit. Since there're two svld2 options.
 */
void svswap_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64x2_t xin, xout;
	svuint64_t swapped;
	svbool_t pg = svwhilelt_b64(0, 2);

	xin = svld2_u64(pg, (uint64_t *)in1);
	xout = svld2_u64(pg, (uint64_t *)out);
	swapped = svget2_u64(xin, 1);
	xout = svset2_u64(xout, 0, swapped);
	swapped = svget2_u64(xin, 0);
	xout = svset2_u64(xout, 1, swapped);
	svst2_u64(pg, (uint64_t *)out, xout);
}

void svlsl_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint32_t xin, xout;
	svbool_t pg = svwhilelt_b32(0, 4);
	svuint64_t shift = svdup_u64(32);

	xin = svld1_u32(pg, (uint32_t *)in1);
	xout = svlsl_wide_u32_m(pg, xin, shift);
	svst1_u32(pg, (uint32_t *)out, xout);
}

/*
 * Reverse the order of high 32-bit and low 32-bit.
 * It works as svswap_01(). But it's more efficient.
 */
void svrevw_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xin1, xin2, xout;
	svbool_t pg = svwhilelt_b64(0, 2);

	xin1 = svld1_u64(pg, (uint64_t *)in1);
	xout = svrevw_u64_z(pg, xin1);
	svst1_u64(pg, (uint64_t *)out, xout);
}

/*
 * Reverse the order of all 64-bit data.
 * For SVE-128, it reverses the order of 64-bit data in [1-2].
 * For SVE-256, it reverses the order of 64-bit data in [1-4] and [5-8].
 * For SVE-512, it reverses the order of 64-bit data in [1-8].
 * For SVE-1024 & SVE-2048, all data are cleared to 0.
 */
void svrev_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xin1, xin2, xout;
	svbool_t pg;
	int i;

	for (i = 0; i < 8; i += svcntd()) {
		pg = svwhilelt_b64(i, 8);
		xin1 = svld1_u64(pg, (uint64_t *)in1 + i);
		xout = svrev_u64(xin1);
		svst1_u64(pg, (uint64_t *)out + i, xout);
	}
}

/*
 * SVE EXT is different from NEON EXT.
 * NEON EXT concatnate the low bits from first vector and high bits from
 * second vector.
 * SVE EXT is based on variable vector length. If vector length is larger
 * than 128, the effect is totally different from NEON EXT.
 */
void svext_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint32_t xin1, xin2, xout;
	// Totally 4 lines (512-bit) data is used.
	// This first line is moved to the fourth line, other lines are
	// moved forward. Don't understand the logic.
	svbool_t pg = svwhilelt_b32(0, 16);

	xin1 = svld1_u32(pg, (uint32_t *)in1);
	xout = svext_u32(xin1, xin1, 4);
	svst1_u32(pg, (uint32_t *)out, xout);
}

// For SVE-128, it works like NEON.
// For SVE-256, the 1st 64-bit is shifted to the 4th; 2nd is shifted
// to 1st; 3rd is shifted to 2nd; 4th is shifted to 3rd.
// For SVE-512, the 1st 64-bit is shifted to the 8th; 2nd is shifted
// to 1st; ...; 8th is shifted to 7th.
// For SVE-1024, the 1st 64-bit is lost; 2nd is shifted to 1st; ...;
// 8th is shifted to 7th; other bits are all 0.
// For SVE-2048, the 1st 64-bit is lost; 2nd is shifted to 1st; ...;
// 8th is shifted to 7th; other bits are all 0.
void svext_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xin1, xin2, xout;
	svbool_t pg;

	for (int i = 0; i < 8; i += svcntd()) {
		pg = svwhilelt_b64(i, 8);
		xin1 = svld1_u64(pg, (uint64_t *)in1 + i);
		xout = svext_u64(xin1, xin1, 1);
		svst1_u64(pg, (uint64_t *)out + i, xout);
	}
}

void svmad_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t data, key, mix, mix_hi, mix_lo;
	svuint64_t acc, swapped;
	svuint64_t shift = svdup_u64(32);
	svbool_t pg = svwhilelt_b64(0, 2);

	acc  = svld1_u64(pg, (uint64_t *)out);
	data = svld1_u64(pg, (uint64_t *)in1);
	key  = svld1_u64(pg, (uint64_t *)in2);
	mix  = sveor_u64_z(pg, data, key); 
	mix_hi = svlsr_u64_z(pg, mix, shift);
	mix_lo = svextw_u64_z(pg, mix);
	mix = svmad_u64_z(pg, mix_lo, mix_hi, acc);
#if 0
	swapped = svlsl_u64_z(pg, data, shift);
	acc  = svadd_u64_z(pg, mix, swapped);
	swapped = svlsr_u64_z(pg, data, shift);
	acc  = svadd_u64_z(pg, acc, swapped);
#endif
	svst1(pg, (uint64_t *)out, acc);
}

void svmad_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64x2_t xout, xin1, xin2;
	svuint64_t acc0, acc1, data0, data1, key0, key1, mix, mix_hi, mix_lo;
	svuint64_t shift = svdup_u64(32);
	svbool_t pg = svwhilelt_b64(0, 1);

	xout  = svld2_u64(pg, (uint64_t *)out);
	xin1  = svld2_u64(pg, (uint64_t *)in1);
	xin2  = svld2_u64(pg, (uint64_t *)in2);
	acc0  = svget2_u64(xout, 0);
	acc1  = svget2_u64(xout, 1);
	data0 = svget2_u64(xin1, 0);
	data1 = svget2_u64(xin1, 1);
	key0  = svget2_u64(xin2, 0);
	key1  = svget2_u64(xin2, 1);
	mix    = sveor_u64_z(pg, data0, key0);
	mix_hi = svlsr_u64_z(pg, mix, shift);
	mix_lo = svextw_u64_z(pg, mix);
	mix    = svmad_u64_z(pg, mix_lo, mix_hi, acc0);
	xout   = svset2_u64(xout, 0, mix);
	mix    = sveor_u64_z(pg, data1, key1);
	mix_hi = svlsr_u64_z(pg, mix, shift);
	mix_lo = svextw_u64_z(pg, mix);
	mix    = svmad_u64_z(pg, mix_lo, mix_hi, acc1);
	xout   = svset2_u64(xout, 1, mix);
	svst2_u64(pg, (uint64_t *)out, xout);
}

/*
 * Although more variables are declared in svmad_03(), the assembly code of
 * both svmad_02() and svmad_03() are exactly same. So let compiler to optimize
 * variables.
 */
void svmad_03(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64x2_t xout, xin1, xin2;
	svuint64_t acc0, acc1, data0, data1, key0, key1;
	svuint64_t mix0, mix1, mix_hi0, mix_hi1, mix_lo0, mix_lo1;
	svuint64_t shift = svdup_u64(32);
	svbool_t pg = svwhilelt_b64(0, 1);

	xout  = svld2_u64(pg, (uint64_t *)out);
	xin1  = svld2_u64(pg, (uint64_t *)in1);
	xin2  = svld2_u64(pg, (uint64_t *)in2);
	acc0  = svget2_u64(xout, 0);
	acc1  = svget2_u64(xout, 1);
	data0 = svget2_u64(xin1, 0);
	data1 = svget2_u64(xin1, 1);
	key0  = svget2_u64(xin2, 0);
	key1  = svget2_u64(xin2, 1);
	mix0    = sveor_u64_z(pg, data0, key0);
	mix_hi0 = svlsr_u64_z(pg, mix0, shift);
	mix_lo0 = svextw_u64_z(pg, mix0);
	mix0    = svmad_u64_z(pg, mix_lo0, mix_hi0, acc0);
	xout    = svset2_u64(xout, 0, mix0);
	mix1    = sveor_u64_z(pg, data1, key1);
	mix_hi1 = svlsr_u64_z(pg, mix1, shift);
	mix_lo1 = svextw_u64_z(pg, mix1);
	mix1    = svmad_u64_z(pg, mix_lo1, mix_hi1, acc1);
	xout    = svset2_u64(xout, 1, mix1);
	svst2_u64(pg, (uint64_t *)out, xout);
}

void svmad_04(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64x2_t xout, xin1, xin2;
	svuint64_t acc0, acc1, data0, data1, key0, key1, mix, mix_hi, mix_lo;
	svuint64_t shift = svdup_u64(32);
	svbool_t pg;
	int i;

	for (i = 0; i < 4; i += svcntd()) {
		pg = svwhilelt_b64(i, 4);
		xout  = svld2_u64(pg, (uint64_t *)out);
		xin1  = svld2_u64(pg, (uint64_t *)in1);
		xin2  = svld2_u64(pg, (uint64_t *)in2);
		acc0  = svget2_u64(xout, 0);
		acc1  = svget2_u64(xout, 1);
		data0 = svget2_u64(xin1, 0);
		data1 = svget2_u64(xin1, 1);
		key0  = svget2_u64(xin2, 0);
		key1  = svget2_u64(xin2, 1);
		mix    = sveor_u64_z(pg, data0, key0);
		mix_hi = svlsr_u64_z(pg, mix, shift);
		mix_lo = svextw_u64_z(pg, mix);
		mix    = svmad_u64_z(pg, mix_lo, mix_hi, acc0);
		mix    = svadd_u64_z(pg, mix, data1);
		xout   = svset2_u64(xout, 0, mix);
		mix    = sveor_u64_z(pg, data1, key1);
		mix_hi = svlsr_u64_z(pg, mix, shift);
		mix_lo = svextw_u64_z(pg, mix);
		mix    = svmad_u64_z(pg, mix_lo, mix_hi, acc1);
		mix    = svadd_u64_z(pg, mix, data0);
		xout   = svset2_u64(xout, 1, mix);
		svst2_u64(pg, (uint64_t *)out, xout);
	}
}

void svmad_05(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t data, key, mix, mix_hi, mix_lo;
	svuint64_t acc, idx, swapped;
	svuint64_t shift = svdup_u64(32);
	svuint64_t subv = svdup_u64(2);
	svbool_t p0 = svpfalse_b();
	svbool_t p1 = svptrue_b64();
	svbool_t pg;
	int i;

	/* create index from 1 with step 1 */
	idx = svindex_u64(1, 1);
	/* convert from sequence [1,2,3,4,...] to [1,0,3,2,...] */
	p0 = svtrn1_b64(p0, p1);
	idx = svsub_u64_m(p0, idx, subv);
	for (i = 0; svptest_first(p1, pg=svwhilelt_b64(i,8)); i += svcntd()) {
		acc  = svld1_u64(pg, (uint64_t *)out + i);
		data = svld1_u64(pg, (uint64_t *)in1 + i);
		key  = svld1_u64(pg, (uint64_t *)in2 + i);
		mix  = sveor_u64_m(pg, data, key);
		mix_hi = svlsr_u64_m(pg, mix, shift);
		mix_lo = svand_n_u64_m(pg, mix, 0xffffffff);
		mix = svmad_u64_m(pg, mix_lo, mix_hi, acc);
		/* reorder all elements in one vector by new index value */
		swapped = svtbl_u64(data, idx);
		acc = svadd_u64_m(pg, swapped, mix);
		svst1(pg, (uint64_t *)out + i, acc);
	}
}

static uint64_t u64_idx[32] __attribute__((aligned(8)));

void svacc_init(void)
{
	/* create index from 1 with step 1 */
	svuint64_t idx = svindex_u64(1, 1);
	svuint64_t subv = svdup_u64(2);
	svbool_t p0 = svpfalse_b();
	svbool_t p1 = svptrue_b64();
	svbool_t pg = svwhilelt_b64(0, 8);;
	/* convert from sequence [1,2,3,4,...] to [1,0,3,2,...] */
	p0 = svtrn1_b64(p0, p1);
	idx = svsub_u64_m(p0, idx, subv);
	svst1(p1, (uint64_t *)&u64_idx[0], idx);
}

/* call assembly code to init */
int svacc_init2(void)
{
	void *out = u64_idx;
	memset(u64_idx, 0, 32);
	XXH3_aarch64_sve_init_accum();
	/* save z7 into u64_idx */
	asm volatile (
		"st1d	z7.d, p7, [%[out]]\n\t"
		:
		: [out] "r" (out)
		: "z7", "p0"
	);
	dump_bits("IDX", u64_idx, 512);
	return 0;
}

#if 0

void svmul_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2,
	const uint64_t loop)
{
	uint64_t i;
	asm volatile (
		"ptrue		p0.d\n\t"
		"ld1d		z0.d, p0/z, [%[in1]]\n\t"
		"ld1d		z1.d, p0/z, [%[in2]]\n\t"
		".Lloop%=:\n\t"
		"mul		z2.d, p0/m, z0.d, z1.d\n\t"
		"cmp		[
		: /* no output */
		: [loop] "r" (loop), [i] "r" (i)
		: "p0", "z0", "z1", "z2"
	);
}
#endif

void svempty_01(void)
{
	uint64_t cnt = 0xffffffff;
	asm volatile (
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"b.ne		.Lloop%=\n\t"
		: /* no output */
		: [cnt] "r" (cnt)
		:
	);
}

/*
 * Test svst1d_01 costs    243435153 counts
 *static unsigned char array[64] __attribute__((aligned(32)));
 * So it's caused by address alignment.
 * aligned(x) means aligning with x bytes.
 * aligned(8): 64-bit aligned
 * aligned(32): 256-bit aligned
 * aligned(256): 2048-bit aligned
 */
static unsigned char array[64] __attribute__((aligned(256)));
/*
 *  Counts are either 482827600 or 243501407.
 *  Test svst1d_01 costs    482827600 counts
 *  haojian@landingteam-sve:~/xxhash$ ./sve.sh -d test
 *  Test svst1d_01 costs    243501407 counts
 *static unsigned char array[64] __attribute__((aligned(8)));
 */
/*
 * Test st1d instruction to save one vector. The test vector size is 512-bit.
 */
void svstore_01(void)
{
	uint64_t cnt = 0;
	asm volatile (
		"ptrue		p0.d\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"st1d		z0.d, p0, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (array),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "p0"
	);
}

/*
 * Four store instructions are used in svstore_02(). It needs four times of
 * svstore_01().
 * It could explain that the execution throughput of ST1D is 1.
 */
void svstore_02(void)
{
	uint64_t cnt = 0;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"str		q0, [%[out]]\n\t"
		"str		q1, [%[out], #16]\n\t"
		"str		q2, [%[out], #32]\n\t"
		"str		q3, [%[out], #48]\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (array),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "v0"
	);
}

void svstore_03(void)
{
	uint64_t cnt = 0;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"str		x0, [%[out]]\n\t"
		"str		x1, [%[out], #8]\n\t"
		"str		x2, [%[out], #16]\n\t"
		"str		x3, [%[out], #24]\n\t"
		"str		x0, [%[out], #32]\n\t"
		"str		x1, [%[out], #40]\n\t"
		"str		x2, [%[out], #48]\n\t"
		"str		x3, [%[out], #56]\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (array),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "v0"
	);
}

void svload_01(void)
{
	uint64_t cnt = 0;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (array),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "p0", "z0"
	);
}

void svload_02(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [lcnt] "i" (MEASURE_LOOPS),
		  [out] "r" (out1)
		: "memory", "cc", "p0", "z0"
	);
}

void svload_03(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "p0", "z0", "z1"
	);
}

/*
 * svload_02() just loads one Q register.
 * svload_03() just loads two Q registers.
 * svload_04() just loads three Q registers.
 * Test svload_02  costs 16922130 counts. Average loop costs 0.06304 counts.
 * Test svload_03  costs 16931816 counts. Average loop costs 0.06308 counts.
 * Test svload_04  costs 30251482 counts. Average loop costs 0.11270 counts.
 * We can find that svload_02() and svload_03() are nearly same. svload_04()
 * nearly costs twice than svload_02(). It could explain that the execution
 * throughput of LD1D is 2.
 */
void svload_04(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"ld1d		z2.d, p0/z, [%[in2], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "p0", "z0", "z1", "z2"
	);
}

void svldst_01(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "p0", "z0", "z1", "z2"
	);
}

void svdup_01(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"mov		z0.d, x0\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [lcnt] "i" (MEASURE_LOOPS)
		: "z0"
	);
}

void sve_scram_01(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_02(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_03(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_04(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_05(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"mov		z3.d, %[prm]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_06(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"mov		z3.d, %[prm]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, p0/m, z0.d, z3.d\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void sve_scram_07(void)
{
	uint64_t cnt, i, j;
	uint64_t prime = XXH_PRIME32_1;
	/* [z0-z2] iteration #1
	 * [z4-z6] iteration #2
	 */
	asm volatile (
		"mov		%[i], xzr\n\t"
		"add		%[j], %[i], #4\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"mov		z3.d, %[prm]\n\t"
		"ptrue		p0.d\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"ld1d		z4.d, p0/z, [%[out], %[j], lsl #3]\n\t"
		"ld1d		z5.d, p0/z, [%[in1], %[j], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, p0/m, z0.d, z3.d\n\t"
		"eor		z5.d, z4.d, z5.d\n\t"
		"lsr		z6.d, z4.d, #47\n\t"
		"eor		z4.d, z5.d, z6.d\n\t"
		"mul		z4.d, p0/m, z4.d, z3.d\n\t"
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"st1d		z4.d, p0, [%[out], %[j], lsl #3]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i), [j] "+&r" (j)
		: [out] "r" (out1), [in1] "r" (in1),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "p0", "z0", "z1", "z2", "z3"
	);
}

void svmul_01(void)
{
	uint64_t cnt = 0;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"ptrue		p0.d\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ld1d		z0.d, p0/z, [%[out]]\n\t"
		"mov		z1.d, %[prm]\n\t"
		".Lloop%=:\n\t"
		"mov		z2.d, z0.d\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"mul		z2.d, p0/m, z2.d, z1.d\n\t"
		"b.ne		.Lloop%=\n\t"
		: /* no output */
		: [cnt] "r" (cnt), [out] "r" (&array[0]), [prm] "r" (prime),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "v0"
	);
}

/*
 * svmul_01() multiply between 64-bit vector. svmul_02() multiply between
 * 32-bit vector. And we cost same time.
 *
 * Test svmul_01 costs     376494720 counts
 * Test svmul_02 costs     376423487 counts
 */
void svmul_02(void)
{
	uint64_t cnt = 0;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"ptrue		p0.d\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		"ld1d		z0.d, p0/z, [%[out]]\n\t"
		"mov		z1.d, %[prm]\n\t"
		".Lloop%=:\n\t"
		"mov		z2.d, z0.d\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"mul		z2.s, p0/m, z2.s, z1.s\n\t"
		"b.ne		.Lloop%=\n\t"
		: /* no output */
		: [cnt] "r" (cnt), [out] "r" (&array[0]), [prm] "r" (prime),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "v0"
	);
}

/*
 * Reserve bit<31:4> in each 64-bit data lane.
 * Can't insert any '0' bits in the bitmask operand of AND instruction.
 */
void svand_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	asm volatile (
		"ptrue		p0.d\n\t"
		"ld1d		z1.d, p0/z, [%[in1]]\n\t"
		"and		z1.d, z1.d, #0xfffffff0\n\t"
		"st1d		z1.d, p0, [%[out]]\n\t"
		: /* no output */
		: [out] "r" (out), [in1] "r" (in1)
		: "p0", "z1"
	);
}

/*
 * Reserve bit<15:0> in each 64-bit data lane.
 * Can't insert any '0' bits in the bitmask operand of AND instruction.
 */
void svand_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	asm volatile (
		"ptrue		p0.d\n\t"
		"ld1d		z1.d, p0/z, [%[in1]]\n\t"
		"and		z1.d, z1.d, #0xffff\n\t"
		"st1d		z1.d, p0, [%[out]]\n\t"
		: /* no output */
		: [out] "r" (out), [in1] "r" (in1)
		: "p0", "z1"
	);
}

/* svmad_06() works with svacc_init() together */
void svmad_06(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t data, key, mix, mix_hi, mix_lo;
	svuint64_t acc, idx, swapped;
	svuint64_t shift = svdup_u64(32);
	svbool_t p1 = svptrue_b64();
	svbool_t pg;
	int i;

	idx = svld1_u64(p1, (uint64_t *)&u64_idx[0]);
	for (i = 0; svptest_first(p1, pg=svwhilelt_b64(i,8)); i += svcntd()) {
		acc  = svld1_u64(pg, (uint64_t *)out + i);
		data = svld1_u64(pg, (uint64_t *)in1 + i);
		key  = svld1_u64(pg, (uint64_t *)in2 + i);
		mix  = sveor_u64_m(pg, data, key);
		mix_hi = svlsr_u64_m(pg, mix, shift);
		mix_lo = svand_n_u64_m(pg, mix, 0xffffffff);
		mix = svmad_u64_m(pg, mix_lo, mix_hi, acc);
		/* reorder all elements in one vector by new index value */
		swapped = svtbl_u64(data, idx);
		acc = svadd_u64_m(pg, swapped, mix);
		svst1(pg, (uint64_t *)out + i, acc);
	}
}

#if 0
void svacc_07(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	uint64_t len;

	asm volatile (
		"ptrue		p0.d\n\t"
		"mov		%w[len], #8\n\t"
		"mov		z6.d, #32\n\t"
		".Lloop%=:\n\t"
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		"ld1d		z2.d, p0/z, [%[in2], %[i], lsl #3]\n\t"
		/* mix = data ^ key */
		"eor		z3.d, p0/m, z1.d, z2.d\n\t"
		/* mix_hi = mix >> 32 */
		"lsr		z5.d, p0/m, z3.d, z6.d\n\t"
		/* mix_lo = mix & 0xffffffff */
		"and		z4.d, p0/m, z3.d,
		/* index was stored in z7 before */
		"tbl		z6.d, z1.d, z7.d\n\t"
		:
		: [out] "r" (out), [in1] "r" (in1), [in2] "r" (in2), [len] "r" (len)
	);
}
#endif

void svest_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svbool_t pg;
	int i;
	for (i = 0; i < 4; i += svcntd()) {
	}
}

/*
 * For no optiomization comipler option.
 * Test empty_accum costs 0 sec and 17137205 nsec
 * Test empty_scrum costs 0 sec and 17126304 nsec
 * Test svest_01 costs	  0 sec and 28954641 nsec
 * Test svest_02 costs	  0 sec and 28989951 nsec
 * From this log, we could know that iteration numbers on i impact little.
 */
void svest_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svbool_t pg;
	int i;
	for (i = 0; i < 8; i += svcntd()) {
	}
}

/*
 * For no optiomization comipler option.
 * Test svest_01 costs	0 sec and 29203579 nsec
 * Test svest_02 costs	0 sec and 29039659 nsec
 * Test svest_03 costs	0 sec and 28039421 nsec
 * It's better that only use one whilelt. It costs a lot if more are used.
 */
void svest_03(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;

	__asm__ __volatile__ (
		".Lloop%=:\n\t"
		"incd		%[i]\n\t"
		"whilelt	p0.s, %w[i], %w[len]\n\t"
		"b.first	.Lloop%=\n\t"
		:			/* output */
		: [out] "r" (out), "r" (in1), [i] "r" (i), [len] "r" (len)
		: "cc", "p0" /* clobber register */
	);
}

void svest_04(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;

	__asm__ __volatile__ (
		"ptrue		p0.b\n\t"
		/* In clang, it'll occur segment fault if clearing i is missing. */
		"mov		%[i], #0\n\t"
		".Lloop%=:\n\t"
		/* load in1 */
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"incd		%[i]\n\t"
		"whilelt	p0.s, %w[i], %w[len]\n\t"
		"b.first	.Lloop%=\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len)
		: "cc", "p0", "z0", "z1", "memory" /* clobber register */
	);
}

/*
 * For no optiomization comipler option.
 * Test svest_01 costs	0 sec and 29543111 nsec
 * Test svest_02 costs	0 sec and 29670381 nsec
 * Test svest_03 costs	0 sec and 28461213 nsec
 * Test svest_04 costs	0 sec and 31380568 nsec
 * Test svest_05 costs	0 sec and 170740947 nsec
 * From this log, we can know that saving cost much more than loading.
 */
void svest_05(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;

	__asm__ __volatile__ (
		"ptrue		p0.b\n\t"
		/* In clang, it'll occur segment fault if clearing i is missing. */
		"mov		%[i], #0\n\t"
		".Lloop%=:\n\t"
		/* load in1 */
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		/* save out */
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"incd		%[i]\n\t"
		"whilelt	p0.s, %w[i], %w[len]\n\t"
		"b.first	.Lloop%=\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len)
		: "cc", "p0", "z0", "z1", "memory" /* clobber register */
	);
}

void svest_06(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		/* load prime32_1 */
		"ptrue		p0.b\n\t"
		/* In clang, it'll occur segment fault if clearing i is missing. */
		"mov		%[i], #0\n\t"
		"mov		z3.d, %[prm]\n\t"
		".Lloop%=:\n\t"
		/* load in1 */
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, p0/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"incd		%[i]\n\t"
		"whilelt	p0.s, %w[i], %w[len]\n\t"
		"b.first	.Lloop%=\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len), [prm] "r" (prime)
		: "cc", "p0", "z0", "z1", "memory" /* clobber register */
	);
}

void svest_07(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		/* load prime32_1 */
		"ptrue		p0.b\n\t"
		/* In clang, it'll occur segment fault if clearing i is missing. */
		"mov		%[i], #0\n\t"
		"mov		z3.d, %[prm]\n\t"
		".Lloop%=:\n\t"
		/* load in1 */
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z4.d, z1.d, z2.d\n\t"
		"mul		z0.d, p0/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		"incd		%[i]\n\t"
		"whilelt	p0.s, %w[i], %w[len]\n\t"
		"b.first	.Lloop%=\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len), [prm] "r" (prime)
		: "cc", "p0", "z0", "z1", "memory" /* clobber register */
	);
}

/*
 * svest_08() equals to svest_06().
 * svest_08() avoids to use whilelt inctd since it's on SVE-512 CPU. It could
 * improves a little. If I move "ptrue p0.d, vl8" to svest_06(), it would
 * cost a little more CPU resources.
 */
void svest_08(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		"ptrue		p0.d, VL8\n\t"
		/* load prime32_1 */
		"mov		z3.d, %[prm]\n\t"
		/* load in1 */
		"ld1d		z1.d, p0/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, p0/z, [%[out], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, p0/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, p0, [%[out], %[i], lsl #3]\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len), [prm] "r" (prime)
		: "cc", "p0", "z0", "z1", "memory" /* clobber register */
	);
}

#if defined(DEBUG_NOSTORE)
#pragma GCC push_options
#pragma GCC optimize ("O0")
inline
void svest_09(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		"ptrue		%[pg].d, VL8\n\t"
		/* load prime32_1 */
		"mov		z3.d, %[prm]\n\t"
		/* load in1 */
		"ld1d		z1.d, %[pg]/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, %[pg]/z, [%[out], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, %[pg]/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, %[pg], [%[out], %[i], lsl #3]\n\t"
		: /* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len), [prm] "r" (prime), [pg] "Upl" (pg)
		: "cc", "z0", "z1" /* clobber register */
	);
}
#pragma GCC pop_options
#else
void svest_09(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i = 0, len = 8, cnt = 0;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		"ptrue		%[pg].d\n\t"
		/* load in1 */
		"ld1d		z1.d, %[pg]/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, %[pg]/z, [%[out], %[i], lsl #3]\n\t"
		/* load prime32_1 */
		"mov		z3.d, %[prm]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, %[pg]/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, %[pg], [%[out], %[i], lsl #3]\n\t"
		:			/* output */
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [len] "r" (len), [prm] "r" (prime), [pg] "Upl" (pg)
		: "cc", "z0", "z1" /* clobber register */
	);
}
#endif	/* DEBUG_NOSTORE */

void svest_10(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1)
{
	svbool_t pg;
	uint64_t i;
	uint64_t prime = XXH_PRIME32_1;

	__asm__ __volatile__ (
		"mov		%[i], xzr\n\t"
		"ptrue		%[pg].d\n\t"
		/* load prime32_1 */
		"mov		z3.d, %[prm]\n\t"
		/* load in1 */
		"ld1d		z1.d, %[pg]/z, [%[in1], %[i], lsl #3]\n\t"
		/* load out */
		"ld1d		z0.d, %[pg]/z, [%[out], %[i], lsl #3]\n\t"
		"eor		z1.d, z0.d, z1.d\n\t"
		"lsr		z2.d, z0.d, #47\n\t"
		"eor		z0.d, z1.d, z2.d\n\t"
		"mul		z0.d, %[pg]/m, z0.d, z3.d\n\t"
		/* save out */
		"st1d		z0.d, %[pg], [%[out], %[i], lsl #3]\n\t"
		: [pg] "=&Upl" (pg)
		: [out] "r" (out), [in1] "r" (in1), [i] "r" (i), [prm] "r" (prime)
		: "cc", "memory", "z0", "z1"
	);
}

/*
 * Reorder 64-bit data by index & tbl.
 * Proposed by Guodong.
 */
void svidx_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xidx, xin, xout;
	svuint64_t subv = svdup_u64(2);
	svbool_t p0 = svpfalse_b();
	svbool_t p1 = svptrue_b64();
	svbool_t pg;
	int i;

	/* create index from 1 with step 1 */
	xidx = svindex_u64(1, 1);
	/* convert from sequence [1,2,3,4,...] to [1,0,3,2,...] */
	p1 = svtrn1_b64(p0, p1);
	xidx = svsub_u64_m(p1, xidx, subv);
	for (i = 0; i < 32; i += svcntd()) {
		pg = svwhilelt_b64(i, 32);
		xin = svld1_u64(pg, (uint64_t *)in1 + i);
		/* reorder all elements in one vector by new index value */
		xout = svtbl_u64(xin, xidx);
		svst1(pg, (uint64_t *)out + i, xout);
	}
}

void scrum_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in)
{
	svuint64_t xin, acc, data;
	svbool_t pg;
	int i, len;

	len = XXH_ACC_NB;
	for (i = 0; i < len; i += svcntd()) {
		pg = svwhilelt_b64(i, len);
		xin  = svld1_u64(pg, (uint64_t *)in + i);
		acc  = svld1_u64(pg, (uint64_t *)out + i);
		data = svlsr_n_u64_m(pg, acc, 47);
		acc  = sveor_u64_m(pg, xin, acc);
		acc = sveor_u64_m(pg, data, acc);
		acc = svmul_n_u64_m(pg, acc, XXH_PRIME32_1);
		svst1(pg, (uint64_t *)out + i, acc);
	}
}

void scrum_02(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in)
{
	svuint64_t xin, acc, data;
	svbool_t p1 = svptrue_b64();
	svbool_t pg;
	int i;

	for (i = 0; svptest_first(p1, pg=svwhilelt_b64(i,8)); i += svcntd()) {
		xin  = svld1_u64(pg, (uint64_t *)in + i);
		acc  = svld1_u64(pg, (uint64_t *)out + i);
		data = svlsr_n_u64_m(pg, acc, 47);
		acc  = sveor_u64_m(pg, xin, acc);
		acc = sveor_u64_m(pg, data, acc);
		acc = svmul_n_u64_m(pg, acc, XXH_PRIME32_1);
		svst1(pg, (uint64_t *)out + i, acc);
	}
}
#endif	/* __ARM_SVE__ */

#if defined(__ARM_FEATURE_SVE2)
/*
 * XAR is SVE2 instruction.
 * Rotate only in the same lane.
 */
void svxar_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	svuint64_t xin1, xin2, xout;
	svbool_t pg = svwhilelt_b64(0, 2);

	xin1 = svld1_u64(pg, (uint64_t *)in1);
	xin2 = svld1_u64(pg, (uint64_t *)in2);
	xout = svxar_n_u64(xin1, xin2, 32);
	svst1_u64(pg, (uint64_t *)out, xout);
}
#endif	/* __ARM_SVE2__ */

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
void vext_01(void* XXH_RESTRICT out,
	const void* XXH_RESTRICT in1,
	const void* XXH_RESTRICT in2)
{
	unsigned char *q = (unsigned char *)in1, *p = out;

	/* exchange lower 64bit and high 64bit of v0, and store in v2 */
	asm volatile (
		"ldr q0, [%0]\n"
		"ext v2.16b, v0.16b, v0.16b, #8\n"
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
}

void vscrum_01(void* XXH_RESTRICT out,
	 const void* XXH_RESTRICT in)
{
	uint64_t prime = XXH_PRIME32_1;

	/*
	 * MUL instruction doesn't support value D of .<T> field.
	 * It only supports value B, H and S of .<T> field.
	 */
	asm volatile (
		"dup v0.2d, %[prm]\n\t"
		"ld1 {v2.2d}, [%[out]]\n\t"
		"ld1 {v1.2d}, [%[in]]\n\t"
		"ushr v3.2d, v2.2d, #47\n\t"
		"eor v2.16b, v1.16b, v2.16b\n\t"
		"eor v2.16b, v3.16b, v2.16b\n\t"
		"xtn v3.2s, v2.2d\n\t"
		"shrn v2.2s, v2.2d, #32\n\t"
		"umull v2.2d, v2.2s, v0.2s\n\t"
		"shl v2.2d, v2.2d, #32\n\t"
		"umlal v2.2d, v3.2s, v0.2s\n\t"
		"st1 {v2.2d}, [%[out]]\n\t"
		: /* no output */
		: [out] "r" (out), [in] "r" (in), [prm] "r" (prime)
		: "v0" /* prm */, "v1" /* key */, "v2" /* acc */, "v3" /* shf */
	);
}

#pragma GCC push_options
#pragma GCC optimize ("O0")
#if defined(DEBUG_NOSTORE)
void vscrum_02(void* XXH_RESTRICT out,
	 const void* XXH_RESTRICT in)
{
	uint64_t prime = XXH_PRIME32_1;
	uint64_t offset;

	/*
	 * MUL instruction doesn't support value D of .<T> field.
	 * It only supports value B, H and S of .<T> field.
	 */
	asm volatile (
		"dup	v0.4s, %w[prm]\n\t"
		"mov	%[off], #0\n\t"
		".Lloop%=:\n\t"
		"ldr	q2, [%[out], %[off]]\n\t"
		"ldr	q1, [%[in], %[off]]\n\t"
		"ushr	v3.2d, v2.2d, #47\n\t"
		"eor	v2.16b, v1.16b, v2.16b\n\t"
		"eor	v2.16b, v3.16b, v2.16b\n\t"
		"xtn	v3.2s, v2.2d\n\t"	// low 64-bit of v3 is used
		"shrn	v2.2s, v2.2d, #32\n\t"	// low 64-bit of v2 is used
		"umull	v2.2d, v2.2s, v0.2s\n\t"
		"shl	v2.2d, v2.2d, #32\n\t"
		"umlal	v2.2d, v3.2s, v0.2s\n\t"
		//"str	q2, [%[out], %[off]]\n\t"
		"add	%[off], %[off], #0x10\n\t"
		"cmp	%[off], #0x40\n\t"
		"b.ne	.Lloop%=\n\t"
		: /* no output */
		: [out] "r" (out), [in] "r" (in), [prm] "r" (prime), [off] "r" (offset)
		: "v0" /* prm */, "v1" /* key */, "v2" /* acc */, "v3" /* shf */
	);
}
#else
void vscrum_02(void* XXH_RESTRICT out,
	 const void* XXH_RESTRICT in)
{
	uint64_t prime = XXH_PRIME32_1;
	uint64_t offset;

	/*
	 * MUL instruction doesn't support value D of .<T> field.
	 * It only supports value B, H and S of .<T> field.
	 */
	asm volatile (
		"dup	v0.4s, %w[prm]\n\t"
		"mov	%[off], #0\n\t"
		".Lloop%=:\n\t"
		"ldr	q2, [%[out], %[off]]\n\t"
		"ldr	q1, [%[in], %[off]]\n\t"
		"ushr	v3.2d, v2.2d, #47\n\t"
		"eor	v2.16b, v1.16b, v2.16b\n\t"
		"eor	v2.16b, v3.16b, v2.16b\n\t"
		"xtn	v3.2s, v2.2d\n\t"	// low 64-bit of v3 is used
		"shrn	v2.2s, v2.2d, #32\n\t"	// low 64-bit of v2 is used
		"umull	v2.2d, v2.2s, v0.2s\n\t"
		"shl	v2.2d, v2.2d, #32\n\t"
		"umlal	v2.2d, v3.2s, v0.2s\n\t"
		"str	q2, [%[out], %[off]]\n\t"
		"add	%[off], %[off], #0x10\n\t"
		"cmp	%[off], #0x40\n\t"
		"b.ne	.Lloop%=\n\t"
		: /* no output */
		: [out] "r" (out), [in] "r" (in), [prm] "r" (prime), [off] "r" (offset)
		: "v0" /* prm */, "v1" /* key */, "v2" /* acc */, "v3" /* shf */
	);
}
#endif	/* DEBUG_NOSTORE */
#pragma GCC pop_options
#endif	/* __ARM_NEON__ */

void base_ld_01(void)
{
	uint64_t cnt, i, data;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldr		%[data], [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i), [data] "=&r" (data)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc"
	);
}

void base_ld_02(void)
{
	uint64_t cnt, i, data;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i), [data] "=&r" (data)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1"
	);
}

void base_st_01(void)
{
	uint64_t cnt, i, data;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"str		%[data], [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [data] "r" (data),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc"
	);
}

void base_ldst_01(void)
{
	uint64_t cnt, i, data;
	asm volatile (
		"mov		%[i], xzr\n\t"
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldr		%[data], [%[out]]\n\t"
		"str		%[data], [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt), [i] "+&r" (i), [data] "+&r" (data)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc"
	);
}

void base_ldst_02(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1"
	);
}

void base_scram_01(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1", "x2", "x3"
	);
}

void base_scram_02(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"eor		x3, x1, x3\n\t"
		"eor		x2, x0, x2\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1", "x2", "x3"
	);
}

void base_scram_03(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"eor		x3, x1, x3\n\t"
		"lsr		x5, x1, #47\n\t"
		"eor		x2, x0, x2\n\t"
		"lsr		x4, x0, #47\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1", "x2", "x3", "x4", "x5"
	);
}

void base_scram_04(void)
{
	uint64_t cnt, i;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"eor		x3, x1, x3\n\t"
		"lsr		x5, x1, #47\n\t"
		"eor		x1, x3, x5\n\t"
		"eor		x2, x0, x2\n\t"
		"lsr		x4, x0, #47\n\t"
		"eor		x0, x2, x4\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1", "x2", "x3", "x4", "x5"
	);
}

void base_scram_05(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"eor		x3, x1, x3\n\t"
		"lsr		x5, x1, #47\n\t"
		"eor		x1, x3, x5\n\t"
		"eor		x2, x0, x2\n\t"
		"lsr		x4, x0, #47\n\t"
		"eor		x0, x2, x4\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "x0", "x1", "x2", "x3", "x4", "x5"
	);
}

void base_scram_06(void)
{
	uint64_t cnt, i;
	uint64_t prime = XXH_PRIME32_1;
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"eor		x3, x1, x3\n\t"
		"lsr		x5, x1, #47\n\t"
		"eor		x1, x3, x5\n\t"
		"mul		x1, x1, %[prm]\n\t"
		"eor		x2, x0, x2\n\t"
		"lsr		x4, x0, #47\n\t"
		"eor		x0, x2, x4\n\t"
		"mul		x0, x0, %[prm]\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS),
		  [prm] "r" (prime)
		: "memory", "cc", "x0", "x1", "x2", "x3", "x4", "x5"
	);
}

void base_scram_07(void)
{
	uint64_t cnt, i;
	/* [x0-x5] iteration #1
	 * [x6-x11] iteration #2
	 */
	asm volatile (
		"mov		%[cnt], %[lcnt]\n\t"
		"movk		w13, #0x9e37, lsl #16\n\t"
		".Lloop%=:\n\t"
		"subs		%[cnt], %[cnt], #1\n\t"
		"ldp		x0, x1, [%[out]]\n\t"
		"ldp		x2, x3, [%[in1]]\n\t"
		"ldp		x6, x7, [%[out], #16]\n\t"
		"ldp		x8, x9, [%[in1], #16]\n\t"
		"eor		x3, x1, x3\n\t"
		"lsr		x5, x1, #47\n\t"
		"eor		x1, x3, x5\n\t"
		"eor		x2, x0, x2\n\t"
		"lsr		x4, x0, #47\n\t"
		"eor		x0, x2, x4\n\t"
		"eor		x9, x7, x9\n\t"
		"lsr		x11, x7, #47\n\t"
		"eor		x7, x9, x11\n\t"
		"eor		x8, x6, x8\n\t"
		"lsr		x10, x6, #47\n\t"
		"eor		x6, x8, x10\n\t"
		"mul		x1, x1, x13\n\t"
		"mul		x0, x0, x13\n\t"
		"mul		x7, x7, x13\n\t"
		"mul		x6, x6, x13\n\t"
		"stp		x0, x1, [%[out]]\n\t"
		"stp		x6, x7, [%[out], #16]\n\t"
		"b.ne		.Lloop%=\n\t"
		: [cnt] "+&r" (cnt)
		: [out] "r" (out1), [in1] "r" (in1), [in2] "r" (in2),
		  [lcnt] "i" (MEASURE_LOOPS)
		: "memory", "cc", "x0", "x1", "x2", "x3", "x4", "x5"
	);
}

static xxh_u64 XXH_read64(const void* memPtr)
{
	xxh_u64 val;
	memcpy(&val, memPtr, sizeof(val));
	return val;
}

XXH_FORCE_INLINE xxh_u64
XXH_mult32to64(xxh_u64 x, xxh_u64 y)
{
	return (x & 0xFFFFFFFF) * (y & 0xFFFFFFFF);
}

XXH_FORCE_INLINE xxh_u64 XXH_xorshift64(xxh_u64 v64, int shift)
{
    XXH_ASSERT(0 <= shift && shift < 64);
    return v64 ^ (v64 >> shift);
}

#if defined(DEBUG_NOSTORE)
/*XXH_FORCE_INLINE*/ void __attribute__((optnone))
XXH3_accumulate_512_scalar(void* XXH_RESTRICT acc,
		const void* XXH_RESTRICT input,
		const void* XXH_RESTRICT secret)
{
	xxh_u64* const xacc = (xxh_u64*) acc; /* presumed aligned */
	const xxh_u8* const xinput  = (const xxh_u8*) input;  /* no alignment restriction */
	const xxh_u8* const xsecret = (const xxh_u8*) secret;   /* no alignment restriction */
	size_t i;
	xxh_u64 acc0, acc1;

	XXH_ASSERT(((size_t)acc & (XXH_ACC_ALIGN-1)) == 0);
	for (i=0; i < XXH_ACC_NB; i++) {
	//for (i=0; i < 2; i++) {
		xxh_u64 data_val;
		xxh_u64 data_key;
		memcpy(&data_val, xinput + 8*i, sizeof(xxh_u64));
		memcpy(&data_key, xsecret + 8*i, sizeof(xxh_u64));
		data_key = data_val ^ data_key;
		acc0 = xacc[i ^ 1];
		acc1 = xacc[i];
		acc0 += data_val; /* swap adjacent lanes */
		acc1 += (data_key & 0xFFFFFFFF) * ((data_key >> 32) & 0xFFFFFFFF);
	}
}

/*XXH_FORCE_INLINE*/ void __attribute__((optnone))
XXH3_scrambleAcc_scalar(void* XXH_RESTRICT acc, const void* XXH_RESTRICT secret)
{
    xxh_u64* const xacc = (xxh_u64*) acc;   /* presumed aligned */
    const xxh_u8* const xsecret = (const xxh_u8*) secret;   /* no alignment restriction */
    size_t i;
    XXH_ASSERT((((size_t)acc) & (XXH_ACC_ALIGN-1)) == 0);
    for (i=0; i < XXH_ACC_NB; i++) {
	xxh_u64 key64;
        xxh_u64 acc64 = xacc[i];
	memcpy(&key64, xsecret + 8*i, sizeof(xxh_u64));
	acc64 = acc64 ^ (acc64 >> 47);
        acc64 ^= key64;
        acc64 *= XXH_PRIME32_1;
        //xacc[i] = acc64;
    }
}
#else
/*XXH_FORCE_INLINE*/ void
XXH3_accumulate_512_scalar(void* XXH_RESTRICT acc,
		const void* XXH_RESTRICT input,
		const void* XXH_RESTRICT secret)
{
	xxh_u64* const xacc = (xxh_u64*) acc; /* presumed aligned */
	const xxh_u8* const xinput  = (const xxh_u8*) input;  /* no alignment restriction */
	const xxh_u8* const xsecret = (const xxh_u8*) secret;   /* no alignment restriction */
	size_t i;
	XXH_ASSERT(((size_t)acc & (XXH_ACC_ALIGN-1)) == 0);
	for (i=0; i < XXH_ACC_NB; i++) {
	//for (i=0; i < 2; i++) {
		xxh_u64 const data_val = XXH_readLE64(xinput + 8*i);
		xxh_u64 const data_key = data_val ^ XXH_readLE64(xsecret + i*8);
		xacc[i ^ 1] += data_val; /* swap adjacent lanes */
		xacc[i] += XXH_mult32to64(data_key & 0xFFFFFFFF, data_key >> 32);
	}
}

/*XXH_FORCE_INLINE*/ void
XXH3_scrambleAcc_scalar(void* XXH_RESTRICT acc, const void* XXH_RESTRICT secret)
{
    xxh_u64* const xacc = (xxh_u64*) acc;   /* presumed aligned */
    const xxh_u8* const xsecret = (const xxh_u8*) secret;   /* no alignment restriction */
    size_t i;
    XXH_ASSERT((((size_t)acc) & (XXH_ACC_ALIGN-1)) == 0);
    for (i=0; i < XXH_ACC_NB; i++) {
        xxh_u64 const key64 = XXH_readLE64(xsecret + 8*i);
        xxh_u64 acc64 = xacc[i];
        acc64 = XXH_xorshift64(acc64, 47);
        acc64 ^= key64;
        acc64 *= XXH_PRIME32_1;
        xacc[i] = acc64;
    }
}
#endif	/* DEBUG_NOSTORE */

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#if defined(DEBUG_NOSTORE)
/*XXH_FORCE_INLINE*/ void __attribute__((optnone))
XXH3_accumulate_512_neon( void* XXH_RESTRICT acc,
                    const void* XXH_RESTRICT input,
                    const void* XXH_RESTRICT secret)
{
    XXH_ASSERT((((size_t)acc) & 15) == 0);
    {
        uint64x2_t* const xacc = (uint64x2_t *) acc;
        /* We don't use a uint32x4_t pointer because it causes bus errors on ARMv7. */
        uint8_t const* const xinput = (const uint8_t *) input;
        uint8_t const* const xsecret  = (const uint8_t *) secret;

        size_t i;
        for (i=0; i < XXH_STRIPE_LEN / sizeof(uint64x2_t); i++) {
	//for (i=0; i < 1; i++) {
            /* data_vec = xinput[i]; */
            uint8x16_t data_vec    = vld1q_u8(xinput  + (i * 16));
            /* key_vec  = xsecret[i];  */
            uint8x16_t key_vec     = vld1q_u8(xsecret + (i * 16));
            uint64x2_t data_key;
            uint32x2_t data_key_lo, data_key_hi;
            /* xacc[i] += swap(data_vec); */
            uint64x2_t const data64  = vreinterpretq_u64_u8(data_vec);
            uint64x2_t const swapped = vextq_u64(data64, data64, 1);
            xacc[i] = vaddq_u64 (xacc[i], swapped);
            /* data_key = data_vec ^ key_vec; */
            data_key = vreinterpretq_u64_u8(veorq_u8(data_vec, key_vec));
            /* data_key_lo = (uint32x2_t) (data_key & 0xFFFFFFFF);
             * data_key_hi = (uint32x2_t) (data_key >> 32);
             * data_key = UNDEFINED; */
            XXH_SPLIT_IN_PLACE(data_key, data_key_lo, data_key_hi);
            /* xacc[i] += (uint64x2_t) data_key_lo * (uint64x2_t) data_key_hi; */
            xacc[i] = vmlal_u32 (xacc[i], data_key_lo, data_key_hi);

        }
    }
}

/*XXH_FORCE_INLINE*/ void __attribute__((optnone))
XXH3_scrambleAcc_neon(void* XXH_RESTRICT acc, const void* XXH_RESTRICT secret)
{
    XXH_ASSERT((((size_t)acc) & 15) == 0);

    {   uint64x2_t* xacc       = (uint64x2_t*) acc;
        uint8_t const* xsecret = (uint8_t const*) secret;
        uint32x2_t prime       = vdup_n_u32 (XXH_PRIME32_1);

        size_t i;
        for (i=0; i < XXH_STRIPE_LEN/sizeof(uint64x2_t); i++) {
            /* xacc[i] ^= (xacc[i] >> 47); */
            uint64x2_t acc_vec  = xacc[i];
            uint64x2_t shifted  = vshrq_n_u64 (acc_vec, 47);
            uint64x2_t data_vec = veorq_u64   (acc_vec, shifted);

            /* xacc[i] ^= xsecret[i]; */
            uint8x16_t key_vec  = vld1q_u8(xsecret + (i * 16));
            uint64x2_t data_key = veorq_u64(data_vec, vreinterpretq_u64_u8(key_vec));

            /* xacc[i] *= XXH_PRIME32_1 */
            uint32x2_t data_key_lo, data_key_hi;
            /* data_key_lo = (uint32x2_t) (xacc[i] & 0xFFFFFFFF);
             * data_key_hi = (uint32x2_t) (xacc[i] >> 32);
             * xacc[i] = UNDEFINED; */
            XXH_SPLIT_IN_PLACE(data_key, data_key_lo, data_key_hi);
            {   /*
                 * prod_hi = (data_key >> 32) * XXH_PRIME32_1;
                 *
                 * Avoid vmul_u32 + vshll_n_u32 since Clang 6 and 7 will
                 * incorrectly "optimize" this:
                 *   tmp     = vmul_u32(vmovn_u64(a), vmovn_u64(b));
                 *   shifted = vshll_n_u32(tmp, 32);
                 * to this:
                 *   tmp     = "vmulq_u64"(a, b); // no such thing!
                 *   shifted = vshlq_n_u64(tmp, 32);
                 *
                 * However, unlike SSE, Clang lacks a 64-bit multiply routine
                 * for NEON, and it scalarizes two 64-bit multiplies instead.
                 *
                 * vmull_u32 has the same timing as vmul_u32, and it avoids
                 * this bug completely.
                 * See https://bugs.llvm.org/show_bug.cgi?id=39967
                 */
                uint64x2_t prod_hi = vmull_u32 (data_key_hi, prime);
                /* xacc[i] = prod_hi << 32; */
                xacc[i] = vshlq_n_u64(prod_hi, 32);
                /* xacc[i] += (prod_hi & 0xFFFFFFFF) * XXH_PRIME32_1; */
                xacc[i] = vmlal_u32(xacc[i], data_key_lo, prime);
            }
    }   }
}
#else
/*XXH_FORCE_INLINE*/ void
XXH3_accumulate_512_neon( void* XXH_RESTRICT acc,
                    const void* XXH_RESTRICT input,
                    const void* XXH_RESTRICT secret)
{
    XXH_ASSERT((((size_t)acc) & 15) == 0);
    {
        uint64x2_t* const xacc = (uint64x2_t *) acc;
        /* We don't use a uint32x4_t pointer because it causes bus errors on ARMv7. */
        uint8_t const* const xinput = (const uint8_t *) input;
        uint8_t const* const xsecret  = (const uint8_t *) secret;

        size_t i;
        for (i=0; i < XXH_STRIPE_LEN / sizeof(uint64x2_t); i++) {
	//for (i=0; i < 1; i++) {
            /* data_vec = xinput[i]; */
            uint8x16_t data_vec    = vld1q_u8(xinput  + (i * 16));
            /* key_vec  = xsecret[i];  */
            uint8x16_t key_vec     = vld1q_u8(xsecret + (i * 16));
            uint64x2_t data_key;
            uint32x2_t data_key_lo, data_key_hi;
            /* xacc[i] += swap(data_vec); */
            uint64x2_t const data64  = vreinterpretq_u64_u8(data_vec);
            uint64x2_t const swapped = vextq_u64(data64, data64, 1);
            xacc[i] = vaddq_u64 (xacc[i], swapped);
            /* data_key = data_vec ^ key_vec; */
            data_key = vreinterpretq_u64_u8(veorq_u8(data_vec, key_vec));
            /* data_key_lo = (uint32x2_t) (data_key & 0xFFFFFFFF);
             * data_key_hi = (uint32x2_t) (data_key >> 32);
             * data_key = UNDEFINED; */
            XXH_SPLIT_IN_PLACE(data_key, data_key_lo, data_key_hi);
            /* xacc[i] += (uint64x2_t) data_key_lo * (uint64x2_t) data_key_hi; */
            xacc[i] = vmlal_u32 (xacc[i], data_key_lo, data_key_hi);

        }
    }
}

/*XXH_FORCE_INLINE*/ void
XXH3_scrambleAcc_neon(void* XXH_RESTRICT acc, const void* XXH_RESTRICT secret)
{
    XXH_ASSERT((((size_t)acc) & 15) == 0);

    {   uint64x2_t* xacc       = (uint64x2_t*) acc;
        uint8_t const* xsecret = (uint8_t const*) secret;
        uint32x2_t prime       = vdup_n_u32 (XXH_PRIME32_1);

        size_t i;
        for (i=0; i < XXH_STRIPE_LEN/sizeof(uint64x2_t); i++) {
            /* xacc[i] ^= (xacc[i] >> 47); */
            uint64x2_t acc_vec  = xacc[i];
            uint64x2_t shifted  = vshrq_n_u64 (acc_vec, 47);
            uint64x2_t data_vec = veorq_u64   (acc_vec, shifted);

            /* xacc[i] ^= xsecret[i]; */
            uint8x16_t key_vec  = vld1q_u8(xsecret + (i * 16));
            uint64x2_t data_key = veorq_u64(data_vec, vreinterpretq_u64_u8(key_vec));

            /* xacc[i] *= XXH_PRIME32_1 */
            uint32x2_t data_key_lo, data_key_hi;
            /* data_key_lo = (uint32x2_t) (xacc[i] & 0xFFFFFFFF);
             * data_key_hi = (uint32x2_t) (xacc[i] >> 32);
             * xacc[i] = UNDEFINED; */
            XXH_SPLIT_IN_PLACE(data_key, data_key_lo, data_key_hi);
            {   /*
                 * prod_hi = (data_key >> 32) * XXH_PRIME32_1;
                 *
                 * Avoid vmul_u32 + vshll_n_u32 since Clang 6 and 7 will
                 * incorrectly "optimize" this:
                 *   tmp     = vmul_u32(vmovn_u64(a), vmovn_u64(b));
                 *   shifted = vshll_n_u32(tmp, 32);
                 * to this:
                 *   tmp     = "vmulq_u64"(a, b); // no such thing!
                 *   shifted = vshlq_n_u64(tmp, 32);
                 *
                 * However, unlike SSE, Clang lacks a 64-bit multiply routine
                 * for NEON, and it scalarizes two 64-bit multiplies instead.
                 *
                 * vmull_u32 has the same timing as vmul_u32, and it avoids
                 * this bug completely.
                 * See https://bugs.llvm.org/show_bug.cgi?id=39967
                 */
                uint64x2_t prod_hi = vmull_u32 (data_key_hi, prime);
                /* xacc[i] = prod_hi << 32; */
                xacc[i] = vshlq_n_u64(prod_hi, 32);
                /* xacc[i] += (prod_hi & 0xFFFFFFFF) * XXH_PRIME32_1; */
                xacc[i] = vmlal_u32(xacc[i], data_key_lo, prime);
            }
    }   }
}
#endif	/* DEBUG_NOSTORE */
#endif	/* __ARM_NEON__ */

XXH_FORCE_INLINE void
XXH3_accumulate(     xxh_u64* XXH_RESTRICT acc,
                const xxh_u8* XXH_RESTRICT input,
                const xxh_u8* XXH_RESTRICT secret,
                      size_t nbStripes,
                      XXH3_f_accumulate_512 f_acc512)
{
    size_t n;
    for (n = 0; n < nbStripes; n++ ) {
        const xxh_u8* const in = input + n*XXH_STRIPE_LEN;
        //XXH_PREFETCH(in + XXH_PREFETCH_DIST);
        f_acc512(acc,
                 in,
                 secret + n*XXH_SECRET_CONSUME_RATE);
    }
}

int svacc_01(void* XXH_RESTRICT acc,
	const void* XXH_RESTRICT input,
	const void* XXH_RESTRICT secret)
{
	/* load acc into z0 */
	asm volatile (
		"ld1d	z0.d, p7/z, [%[out]]\n\t"
		:
		: [out] "r" (acc)
		: "z0", "p7"
		);
	XXH3_aarch64_sve_init_accum();
	/* acc occupies x0. Other two parameters are using x1 and x2. */
	XXH3_aarch64_sve_acc512(acc, input, secret);
	return 0;
}

int svacc_02(void* XXH_RESTRICT acc,
	const void* XXH_RESTRICT input,
	const void* XXH_RESTRICT secret,
	size_t nbStripes)
{
	//int reg;
	/* load acc into z0 */
	asm volatile (
		"ld1d	z0.d, p7/z, [%[out]]\n\t"
		:
		: [out] "r" (out1)
		: "z0", "p7"
		);
	XXH3_aarch64_sve_init_accum();
	/* acc occupies x0. Other parameters are using x1-x3. */
	XXH3_aarch64_sve_accumulate(acc, input, secret, nbStripes);
/*
	asm volatile (
		"mov	%[reg], x10\n\t"
		: [reg] "=r" (reg)
		:
		:
		);
	printf("reg=0x%x\n", reg);
*/
	return 0;
}

int svscramble_01(void* XXH_RESTRICT acc,
		const void* XXH_RESTRICT secret)
{
	//int reg;
	/* load acc into z0 */
	asm volatile (
		"ptrue	p7.d\n\t"
		"ld1d	z0.d, p7/z, [%[out]]\n\t"
		:
		: [out] "r" (out1)
		: "z0", "p7"
		);
	XXH3_aarch64_sve_scramble(acc, secret);
/*
	asm volatile (
		"mov	%[reg], x12\n\t"
		: [reg] "=r" (reg)
		:
		:
		);
	printf("reg=0x%x\n", reg);
*/
	return 0;
}
void test_svacc(int bits)
{
	int i;
	unsigned char vout[64];
	void *out = vout;
	size_t nbStripes;
	void *out1, *in1, *in2;
	int bytes;

	if (bits < 512) {
		printf("The minimum bits should be 512.\n");
		return;
	}
	nbStripes = bits / 512;
	bytes = (bits + 7) >> 3;
	in1 = malloc(bytes);
	if (!in1) {
		printf("Not enough memory for in1 (%d-bit)!\n", bits);
		goto out_in1;
	}
	in2 = malloc(bytes);
	if (!in2) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_in2;
	}
	out1 = malloc(bytes);
	if (!out1) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_out1;
	}
	set_buf(in1, 0x55, bits);
	init_buf(in2, bits);
	clear_buf(out1, 512);
	//svacc_01(out1, in1, in2);
	//svacc_02(out1, in1, in2, nbStripes);
	svscramble_01(out1, in2);
#if 1
	/* dump z0 */
	asm volatile (
		"st1d	z0.d, p7, [%[out]]\n\t"
		"dsb	sy\n\t"
		:
		: [out] "r" (out)
		: "z0", "p7", "memory"
	);
	dump_bits("ACC", out, 512);
	memset(out, 0, 64);
#endif
#if 0
	/* save z1 */
	asm volatile (
		"st1d	z1.d, p7, [%[out]]\n\t"
		"dsb	sy\n\t"
		:
		: [out] "r" (out)
		: "z1", "p7", "memory"
	);
	dump_bits("INPUT", out, 512);
#endif
#if 0
	/* save z2 into in2 */
	asm volatile (
		"st1d	z2.d, p7, [%[out]]\n\t"
		"dsb	sy\n\t"
		:
		: [out] "r" (out)
		: "z2", "p7", "memory"
	);
	dump_bits("SECRET", out, 512);
#endif
	free(in1);
	free(in2);
	free(out1);
	return;
out_out1:
	free(in2);
out_in2:
	free(in1);
out_in1:
	return;
}

void test_xxh3_accum(int bits)
{
	void *out1, *in1, *in2;
	int bytes;
	size_t nbStripes;

	if (bits < 512) {
		printf("The minimum bits should be 512.\n");
		return;
	}
	nbStripes = bits / 512;
	bytes = (bits + 7) >> 3;
	in1 = malloc(bytes);
	if (!in1) {
		printf("Not enough memory for in1 (%d-bit)!\n", bits);
		goto out_in1;
	}
	in2 = malloc(bytes);
	if (!in2) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_in2;
	}
	out1 = malloc(bytes);
	if (!out1) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_out1;
	}

	printf("Test in %s\n", __func__);
	set_buf(in1, 0x55, bits);
	init_buf(in2, bits);
	clear_buf(out1, 512);
	XXH3_accumulate((xxh_u64*)out1, in1, in2, nbStripes,
			XXH3_accumulate_512_scalar);
	dump_bits("ACC", out1, 512);
	free(in1);
	free(in2);
	free(out1);
	return;
out_out1:
	free(in2);
out_in2:
	free(in1);
out_in1:
	return;
}

void test_xxh3_scramble(int bits)
{
	void *out1, *in1, *in2;
	int bytes;
	size_t nbStripes;

	if (bits < 512) {
		printf("The minimum bits should be 512.\n");
		return;
	}
	nbStripes = bits / 512;
	bytes = (bits + 7) >> 3;
	in1 = malloc(bytes);
	if (!in1) {
		printf("Not enough memory for in1 (%d-bit)!\n", bits);
		goto out_in1;
	}
	in2 = malloc(bytes);
	if (!in2) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_in2;
	}
	out1 = malloc(bytes);
	if (!out1) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_out1;
	}

	printf("Test in %s\n", __func__);
	set_buf(in1, 0x55, bits);
	init_buf(in2, bits);
	clear_buf(out1, 512);
	XXH3_scrambleAcc_scalar(out1, in2);
	dump_bits("ACC", out1, 512);
	free(in1);
	free(in2);
	free(out1);
	return;
out_out1:
	free(in2);
out_in2:
	free(in1);
out_in1:
	return;
}

void empty_accum( void* XXH_RESTRICT acc,
	const void* XXH_RESTRICT input,
	const void* XXH_RESTRICT secret)
{
}

void empty_scrum(void* XXH_RESTRICT acc, const void* XXH_RESTRICT secret)
{
}

void test_accum(char *name, f_accum fn, int bits)
{
	void *in1, *in2, *out1;
	int bytes;

	if (bits < 0)
		return;
	bytes = (bits + 7) >> 3;
	in1 = malloc(bytes);
	if (!in1) {
		printf("Not enough memory for in1 (%d-bit)!\n", bits);
		goto out_in1;
	}
	in2 = malloc(bytes);
	if (!in2) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_in2;
	}
	out1 = malloc(bytes);
	if (!out1) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_out1;
	}

	printf("Test in %s\n", name);
	init_buf(in1, bits);
	set_buf(in2, 0x55, bits);
	clear_buf(out1, bits);
	fn(out1, in1, in2);
	dump_bits("IN1", in1, bits);
	dump_bits("OUT1", out1, bits);
	free(in1);
	free(in2);
	free(out1);
	return;
out_out1:
	free(in2);
out_in2:
	free(in1);
out_in1:
	return;
}

void test_scrum(char *name, f_scrum fn, int bits)
{
	void *in1, *out1;
	int bytes;

	if (bits < 0)
		return;
	bytes = (bits + 7) >> 3;
	in1 = malloc(bytes);
	if (!in1) {
		printf("Not enough memory for in1 (%d-bit)!\n", bits);
		goto out_in1;
	}
	out1 = malloc(bytes);
	if (!out1) {
		printf("Not enough memory for in2 (%d-bit)!\n", bits);
		goto out_out1;
	}

	printf("Test in %s\n", name);
	init_buf(in1, bits);
	set_buf(out1, 0x55, bits);
	fn(out1, in1);
	dump_bits("IN1", in1, bits);
	dump_bits("OUT1", out1, bits);
	free(in1);
	free(out1);
	return;
out_out1:
	free(in1);
out_in1:
	return;
}

#define LOOP_CNT	10000000

void __attribute__((optnone))
perf_accum(char *name, f_accum fn)
{
	int i;
#if defined(__AARCH64_CMODEL_SMALL__)
	uint64_t t1, t2;
#else
	struct timespec start, end;
	uint64_t us, ue;
#endif

	printf("Test %s ", name);
	init_buf(in1, 512);
	clear_buf(in2, 512);
	clear_buf(out1, 512);
#if defined(__AARCH64_CMODEL_SMALL__)
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t1));
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1, in2);
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t2));
	printf("costs\t%ld counts\n", t2 - t1);
#else
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1, in2);
	clock_gettime(CLOCK_REALTIME, &end);
	ue = end.tv_nsec + end.tv_sec * 1000000000;
	us = start.tv_nsec + start.tv_sec * 1000000000;
	printf("costs\t%ld sec and %ld nsec\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
#endif
}

void __attribute__((optnone))
perf_scrum(char *name, f_scrum fn)
{
	int i;
#if defined(__AARCH64_CMODEL_SMALL__)
	uint64_t t1, t2;
#else
	struct timespec start, end;
	uint64_t us, ue;
#endif

	printf("Test %s ", name);
	init_buf(in1, 512);
	clear_buf(out1, 512);
#if defined(__AARCH64_CMODEL_SMALL__)
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t1));
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1);
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t2));
	printf("costs\t%ld counts\n", t2 - t1);
#else
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1);
	clock_gettime(CLOCK_REALTIME, &end);
	ue = end.tv_nsec + end.tv_sec * 1000000000;
	us = start.tv_nsec + start.tv_sec * 1000000000;
	printf("costs\t%ld sec and %ld nsec\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
#endif
}

void measure_fn(char *name, f_void fn)
{
#if defined(__AARCH64_CMODEL_SMALL__)
	uint64_t t1, t2;
#else
	struct timespec start, end;
	uint64_t us, ue;
#endif

	printf("Test %s ", name);
#if defined(__AARCH64_CMODEL_SMALL__)
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t1));
	fn();
	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (t2));
	printf("costs %ld counts. Average loop costs %.5f counts.\n",
		t2 - t1,
		(double)(t2 - t1) / MEASURE_LOOPS);
#else
	clock_gettime(CLOCK_REALTIME, &start);
	fn();
	clock_gettime(CLOCK_REALTIME, &end);
	ue = end.tv_nsec + end.tv_sec * 1000000000;
	us = start.tv_nsec + start.tv_sec * 1000000000;
	printf("costs %ld sec and %ld nsec.\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
#endif
}

int main(int argc, char **argv)
{
	int op, flag_perf = 0;

	test_svacc(1024);
	//test_accum("svmad_05", svmad_05, 1024);
	//test_xxh3_accum(2048);
	test_xxh3_scramble(1024);
	return 0;
	while ((op = getopt(argc, argv, ":p")) != -1) {
		switch (op) {
		case 'p':
			flag_perf = 1;
			break;
		default:
			break;
		}
	}

	for (int i = 0; i < 64; i++) {
		array[i] = 0x30 + i;
	}
	svacc_init2();
	memset(u64_idx, 0, 32);
	svacc_init();
	dump_bits("IDX", u64_idx, 512);
	measure_fn("base_ld_01", base_ld_01);
	measure_fn("base_ld_02", base_ld_02);
	measure_fn("base_st_01", base_st_01);
	measure_fn("base_ldst_01", base_ldst_01);
	measure_fn("base_ldst_02", base_ldst_02);
	op = 5;
	op = asvload_03(op);
	printf("op:%d\n", op);
	return 0;
	/*
	measure_fn("base_scram_01", base_scram_01);
	measure_fn("base_scram_02", base_scram_02);
	measure_fn("base_scram_03", base_scram_03);
	measure_fn("base_scram_04", base_scram_04);
	measure_fn("base_scram_05", base_scram_05);
	measure_fn("base_scram_06", base_scram_06);
	//measure_fn("base_scram_07", base_scram_07);
	*/
#if defined(__ARM_FEATURE_SVE)
	/*
	measure_fn("svstore_01", svstore_01);
	measure_fn("svstore_02", svstore_02);
	measure_fn("svstore_03", svstore_03);
	measure_fn("svmul_01", svmul_01);
	measure_fn("svload_02", svload_02);
	measure_fn("svload_03", svload_03);
	measure_fn("svload_04", svload_04);
	measure_fn("svldst_01", svldst_01);
	measure_fn("sve_scram_01", sve_scram_01);
	measure_fn("sve_scram_02", sve_scram_02);
	measure_fn("sve_scram_03", sve_scram_03);
	measure_fn("sve_scram_04", sve_scram_04);
	measure_fn("sve_scram_05", sve_scram_05);
	measure_fn("sve_scram_06", sve_scram_06);
	measure_fn("sve_scram_07", sve_scram_07);
	*/
	//measure_fn("svmul_02", svmul_02);
	//measure_fn("svempty_01", svempty_01);
	svacc_init();
	if (flag_perf) {
		/*
		//perf_accum("svmad_04", svmad_04);
		*/
#if !defined(DEBUG_NOSTORE)
		perf_accum("svmad_05", svmad_05);
		perf_accum("svmad_06", svmad_06);
		perf_scrum("scrum_02", scrum_02);
#endif	/* DEBUG_NOSTORE */
		/*
		perf_scrum("scrum_01", scrum_01);
		perf_accum("empty_accum", empty_accum);
		perf_scrum("svest_03", svest_03);
		perf_scrum("svest_04", svest_04);
		perf_scrum("svest_05", svest_05);
		perf_scrum("svest_06", svest_06);
		perf_scrum("svest_07", svest_07);
		perf_scrum("svest_08", svest_08);
		*/
		perf_scrum("empty_scrum", empty_scrum);
		perf_scrum("svest_09", svest_09);
		perf_scrum("svest_10", svest_10);
	} else {
#if !defined(DEBUG_NOSTORE)
		test_accum("svmad_05", svmad_05, 1024);
		test_accum("svmad_06", svmad_06, 1024);
		test_scrum("scrum_02", scrum_02, 1024);
#endif	/* DEBUG_NOSTORE */
		test_scrum("svest_09", svest_09, 1024);
		//test_accum("svext_01", svext_01, 512);
		//test_accum("svrev_01", svrev_01, 2048);
		//test_scrum("scrum_01", scrum_01, 1024);
	}
#endif	/* __ARM_SVE__ */
#if defined(__ARM_FEATURE_SVE2)
	if (!flag_perf)
		test_accum("svxar_01", svxar_01);
#endif	/* __ARM_SVE2__ */
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
	if (flag_perf) {
		perf_accum("accum neon", XXH3_accumulate_512_neon);
		perf_scrum("scrum neon", XXH3_scrambleAcc_neon);
		perf_scrum("vscrum_01", vscrum_01);
		perf_scrum("vscrum_02", vscrum_02);
	} else {
		//test_accum("vext_01", vext_01);
		test_accum("accum neon", XXH3_accumulate_512_neon, 512);
		test_scrum("scrum neon", XXH3_scrambleAcc_neon, 512);
		test_scrum("vscrum_01", vscrum_01, 512);
		test_scrum("vscrum_02", vscrum_02, 512);
	}
#endif	/* ARM_NEON */
	if (flag_perf) {
		perf_accum("accum scalar", XXH3_accumulate_512_scalar);
		perf_scrum("scrum scalar", XXH3_scrambleAcc_scalar);
	} else {
		test_accum("accum scalar", XXH3_accumulate_512_scalar, 512);
		test_scrum("scrum scalar", XXH3_scrambleAcc_scalar, 512);
	}
	return 0;
}
