#include <arm_neon.h>
#include <arm_sve.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

typedef void (*ftest)(void* XXH_RESTRICT,
		const void* XXH_RESTRICT,
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
	svbool_t pg = svwhilelt_b64(0, 8);

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

#if 0
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
	//for (i=0; i < XXH_ACC_NB; i++) {
	for (i=0; i < 2; i++) {
		xxh_u64 const data_val = XXH_readLE64(xinput + 8*i);
		xxh_u64 const data_key = data_val ^ XXH_readLE64(xsecret + i*8);
		xacc[i ^ 1] += data_val; /* swap adjacent lanes */
		xacc[i] += XXH_mult32to64(data_key & 0xFFFFFFFF, data_key >> 32);
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
#endif

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

void test(char *name, ftest fn)
{
	unsigned char in1[64], in2[64];
	unsigned char out1[64];

	printf("Test in %s\n", name);
	init_buf(in1, 512);
	set_buf(in2, 0x55, 512);
	clear_buf(out1, 512);
	fn(out1, in1, in2);
	dump_bits("IN1", in1, 512);
	dump_bits("OUT1", out1, 512);
}

#define LOOP_CNT	100000000

void perf(char *name, ftest fn)
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
	printf("costs %ld sec and %ld nsec\n",
		(ue - us) / 1000000000,
		(ue - us) % 1000000000);
}

int main(void)
{
#if 0
	test("svld1_01", svld1_01);
	test("svld1_02", svld1_02);
#endif
	test("svswap_01", svswap_01);
	test("svswap_02", svswap_02);
	test("svmad_02", svmad_02);
	test("svmad_04", svmad_04);
	test("svlsl_01", svlsl_01);
	//test("scalar", XXH3_accumulate_512_scalar);
	//test("neon", XXH3_accumulate_512_neon);
	//perf("svmad_04", svmad_04);
	//perf("scalar", XXH3_accumulate_512_scalar);
	perf("neon", XXH3_accumulate_512_neon);
	return 0;
}
