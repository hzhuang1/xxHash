#include <arm_acle.h>
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

typedef void (*f_accum)(void* XXH_RESTRICT,
		const void* XXH_RESTRICT,
		const void* XXH_RESTRICT);
typedef void (*f_scrum)(void* XXH_RESTRICT,
		const void* XXH_RESTRICT);

void init_buf(unsigned char *buf, int blen)
{
	int i, len;
	unsigned char c = 0;
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
	p1 = svtrn1_b64(p0, p1);
	idx = svsub_u64_m(p1, idx, subv);
	for (i = 0; i < 8; i += svcntd()) {
		pg = svwhilelt_b64(i, 8);
		acc  = svld1_u64(pg, (uint64_t *)out + i);
		data = svld1_u64(pg, (uint64_t *)in1 + i);
		key  = svld1_u64(pg, (uint64_t *)in2 + i);
		mix  = sveor_u64_m(pg, data, key); 
		mix_hi = svlsr_u64_m(pg, mix, shift);
		mix_lo = svand_n_u64_m(pg, mix, 0xffffffff);
		//mix_lo = svextw_u64_z(pg, mix);
		mix = svmad_u64_m(pg, mix_lo, mix_hi, acc);
		/* reorder all elements in one vector by new index value */
		swapped = svtbl_u64(data, idx);
		acc = svadd_u64_m(pg, swapped, mix);
		svst1(pg, (uint64_t *)out + i, acc);
	}
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
#endif	/* __ARM_NEON__ */


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

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
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
#endif

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

void perf_accum(char *name, f_accum fn)
{
	unsigned char in1[64], in2[64];
	unsigned char out1[64];
	struct timespec start, end;
	uint64_t us, ue;
	int i;

	printf("Test %s ", name);
	init_buf(in1, 512);
	clear_buf(in2, 512);
	clear_buf(out1, 512);
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1, in2);
	clock_gettime(CLOCK_REALTIME, &end);
	ue = end.tv_nsec + end.tv_sec * 1000000000;
	us = start.tv_nsec + start.tv_sec * 1000000000; 
	printf("costs\t%ld sec and %ld nsec\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
}

void perf_scrum(char *name, f_scrum fn)
{
	unsigned char in1[64];
	unsigned char out1[64];
	struct timespec start, end;
	uint64_t us, ue;
	int i;

	printf("Test %s ", name);
	init_buf(in1, 512);
	clear_buf(out1, 512);
	clock_gettime(CLOCK_REALTIME, &start);
	for (i = 0; i < LOOP_CNT; i++)
		fn(out1, in1);
	clock_gettime(CLOCK_REALTIME, &end);
	ue = end.tv_nsec + end.tv_sec * 1000000000;
	us = start.tv_nsec + start.tv_sec * 1000000000; 
	printf("costs\t%ld sec and %ld nsec\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
}

int main(int argc, char **argv)
{
	int op, flag_perf = 0;

	while ((op = getopt(argc, argv, ":p")) != -1) {
		switch (op) {
		case 'p':
			flag_perf = 1;
			break;
		default:
			break;
		}
	}

#if defined(__ARM_FEATURE_SVE)
	if (flag_perf) {
		//perf_accum("svmad_04", svmad_04);
		perf_accum("svmad_05", svmad_05);
		perf_scrum("scrum_01", scrum_01);
		perf_accum("empty_accum", empty_accum);
		perf_scrum("empty_scrum", empty_scrum);
	} else {
		test_accum("svmad_05", svmad_05, 1024);
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
	} else {
		//test_accum("vext_01", vext_01);
		test_accum("accum neon", XXH3_accumulate_512_neon, 512);
		test_scrum("scrum neon", XXH3_scrambleAcc_neon, 512);
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
