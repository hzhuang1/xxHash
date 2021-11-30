#include <arm_neon.h>
#include <stdio.h>
#include <string.h>

void init_buf(unsigned char *buf)
{
	int i;
	unsigned char c = 0;
	for (i = 0; i < 16; i++) {
		buf[i] = c;
		c += 0x11;
	}
}

void clear_buf(unsigned char *buf)
{
	int i;
	for (i = 0; i < 16; i++)
		buf[i] = 0;
}

void set_buf(unsigned char *buf, unsigned char c)
{
	int i;
	for (i = 0; i < 16; i++)
		buf[i] = c;
}

void dump_64bit(char *name, unsigned char *buf)
{
	int i;
	printf("64-bit data [%s]:", name);
	for (i = 0; i < 7; i++) {
		printf("%02x-", buf[i]);
	}
	printf("%02x\n", buf[i]);
}

void dump_128bit(char *name, unsigned char *buf)
{
	int i;
	printf("128-bit data [%s]:", name);
	for (i = 0; i < 15; i++) {
		printf("%02x-", buf[i]);
	}
	printf("%02x\n", buf[i]);
}

void vld1(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in VLD1:\n");
	init_buf(q);
	clear_buf(p);
	asm volatile (
		"ldr q0, [%0]\n"
		"str q0, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);

	init_buf(q);
	clear_buf(p);
	asm volatile (
		"ldr d0, [%0]\n"
		"str d0, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

/* Test load into two arrays. */
void vld2_01(void)
{
	unsigned char in[16], out1[32], out2[16];
	unsigned char *q = in, *p1 = out1, *p2 = out2;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p1);
	clear_buf(p2);
	asm volatile (
		"ld2 {v0.2s, v1.2s}, [%0]\n"
		"str q0, [%1]\n"
		"str q1, [%2]\n"
		: "=r"(q), "=r"(p1), "=r"(p2)
		: "0"(q), "1"(p1), "2"(p2)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

/* Test to load into two arrays. Then do UMLAL calculation. */
void vld2_02(void)
{
	unsigned char in[16], out1[32], out2[16];
	unsigned char *q = in, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p1);
	clear_buf(p2);
	/*
	 * Notice: In the input operand list, they can't be declared as "r".
	 * Since they're NEON registers, not general ARM registers.
	 */
	asm volatile (
		"mov %0, #0\n"
		"dup v2.2d, %0\n"
		"ld2 {v0.2s, v1.2s}, [%1]\n"
		"umlal v2.2d, v0.2s, v1.2s\n"
		"str q2, [%2]\n"
		: "=r"(tmp), "=r"(q), "=r"(p1)
		: "0"(tmp), "1"(q), "2"(p1)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

void vld2_03(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	clear_buf(in2);
	clear_buf(out1);
	clear_buf(out2);
	asm volatile (
		"mov %0, #0\n"
		"dup v2.2d, %0\n"
		"ld2 {v0.2s, v1.2s}, [%1]\n"
		"ld2 {v3.2s, v4.2s}, [%2]\n"
		"eor v3.8b, v0.8b, v3.8b\n"
		"eor v4.8b, v1.8b, v4.8b\n"
		/*
		"umlal v2.2d, v3.2s, v4.2s\n"
		"str q2, [%3]\n"
		*/
		"str q3, [%3]\n"
		"str q4, [%4]\n"
		: "=r"(tmp), "=r"(q1), "=r"(q2), "=r"(p1), "=r"(p2)
		: "0"(tmp), "1"(q1), "2"(q2), "3"(p1), "4"(p2)
		: "v0"
	);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

void vld2_04(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	clear_buf(in2);
	clear_buf(out1);
	clear_buf(out2);
	asm volatile (
		"mov %0, #0\n"
		"dup v2.2d, %0\n"
		"ld2 {v0.2s, v1.2s}, [%1]\n"
		"ld2 {v3.2s, v4.2s}, [%2]\n"
		"ldr q2, [%3]\n"
		"eor v3.8b, v0.8b, v3.8b\n"
		"eor v4.8b, v1.8b, v4.8b\n"
		"umlal v2.2d, v3.2s, v4.2s\n"
		"str q2, [%3]\n"
		: "=r"(tmp), "=r"(q1), "=r"(q2), "=r"(p1), "=r"(p2)
		: "0"(tmp), "1"(q1), "2"(q2), "3"(p1), "4"(p2)
		: "v0"
	);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

/* try to implement xxhash 512 neon */
void vld2_05(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	clear_buf(out1);
	clear_buf(out2);
	/* v1 is used only once. So it could be overwriten. */
	asm volatile (
		"mov %0, #0\n"
		"dup v2.2d, %0\n"
		/* split load */
		"ld2 {v0.2s, v1.2s}, [%1]\n"
		"ld2 {v3.2s, v4.2s}, [%2]\n"
		"ldr q2, [%3]\n"
		/* xor and multiply and accumulate on source q2 */
		"eor v5.8b, v0.8b, v3.8b\n"
		"eor v6.8b, v1.8b, v4.8b\n"
		"umlal v2.2d, v5.2s, v6.2s\n"
		/* reverse v0 */
		"rev64 v4.2s, v0.2s\n"
		"ushll v0.2d, v4.2s, #0\n"
		/* reverse v1 */
		"rev64 v5.2s, v1.2s\n"
		"ushll v1.2d, v5.2s, #16\n"
		"shl v4.2d, v1.2d, #16\n"
		/* combine revsered v0 and v1 together, and store in v4 */
		"orr v3.16b, v4.16b, v0.16b\n"
		"add v4.2d, v2.2d, v3.2d\n"
		"str q2, [%3]\n"
		"str q4, [%4]\n"
		: "=r"(tmp), "=r"(q1), "=r"(q2), "=r"(p1), "=r"(p2)
		: "0"(tmp), "1"(q1), "2"(q2), "3"(p1), "4"(p2)
		: "v0"
	);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

/* same as vld2_05 */
void vld2_06(void)
{
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	void *xin1 = (void *)in1;
	void *xin2 = (void *)in2;
	void *xout1 = (void *)out1;
	void *xout2 = (void *)out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	//set_buf(out1, 0x11);
	clear_buf(out1);
	clear_buf(out2);

	uint64x2_t xacc_vec    = vld1q_u64((const uint64_t *)out1);
	uint32x2x2_t data_vec  = vld2_u32((const uint32_t *)xin1);
	uint32x2_t data_lo     = data_vec.val[0];
	uint32x2_t data_hi     = data_vec.val[1];
	uint32x2x2_t key_vec   = vld2_u32((const uint32_t *)xin2);
	uint32x2_t key_lo      = key_vec.val[0];
	uint32x2_t key_hi      = key_vec.val[1];
	/* data_key = data_vec ^ key_vec;
	 * Overwrite key_lo and key_hi.
	 */
	key_lo = veor_u32(key_lo, data_lo);
	key_hi = veor_u32(key_hi, data_hi);
	xacc_vec = vmlal_u32(xacc_vec, key_lo, key_hi);

	key_lo = vrev64_u32(data_lo);
	uint64x2_t tmp1 = vshll_n_u32(key_lo, 0);
	xacc_vec = vaddq_u64(xacc_vec, tmp1);

	key_hi = vrev64_u32(data_hi);
	uint64x2_t tmp2 = vshll_n_u32(key_hi, 0);
	tmp2 = vshlq_n_u64(tmp2, 32);
	vst1q_u64((uint64_t *)out2, tmp2);
	xacc_vec = vaddq_u64(xacc_vec, tmp2);

	vst1q_u64((uint64_t *)out1, xacc_vec);

	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

/* same as vld2_05 */
void vld2_07(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	clear_buf(out1);
	//set_buf(out1, 0x11);
	clear_buf(out2);
	/* v1 is used only once. So it could be overwriten. */
	asm volatile (
		/* split load */
		"ld2 {v0.2s, v1.2s}, [%1]\n"
		"ld2 {v3.2s, v4.2s}, [%2]\n"
		"ldr q2, [%3]\n"
		/* xor and multiply and accumulate on source q2 */
		"eor v5.8b, v0.8b, v3.8b\n"
		"eor v6.8b, v1.8b, v4.8b\n"
		"umlal v2.2d, v5.2s, v6.2s\n"
		/* reverse v0: 00AB --> 0B0A */
		"rev64 v4.2s, v0.2s\n"
		"ushll v0.2d, v4.2s, #0\n"
		"add v4.2d, v2.2d, v0.2d\n"
		/* reverse v3: 00CD --> D0C0 */
		"rev64 v5.2s, v1.2s\n"
		"ushll v1.2d, v5.2s, #0\n"
		"shl v2.2d, v1.2d, #32\n"
		"add v4.2d, v4.2d, v2.2d\n"
		"str q2, [%3]\n"
		"str q4, [%4]\n"
		: "=r"(tmp), "=r"(q1), "=r"(q2), "=r"(p1), "=r"(p2)
		: "0"(tmp), "1"(q1), "2"(q2), "3"(p1), "4"(p2)
		: "v0"
	);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

void vext_01(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	/* exchange lower 64bit and high 64bit of v0, and store in v2 */
	asm volatile (
		"ldr q0, [%0]\n"
		"ext v2.16b, v0.16b, v0.16b, #8\n"
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

void vext_02(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	/* exchange lower 32bit and high 32bit of v0, and store in v2 */
	asm volatile (
		"ldr q0, [%0]\n"
		"ext v2.8b, v0.8b, v0.8b, #4\n"
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

void vushll_01(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	/* get lanes from v0, and store them into lower 32bit of new 64bit v2 */
	asm volatile (
		"ldr q0, [%0]\n"
		"ushll v2.2d, v0.2s, #0\n" 
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

/* get higher lanes from v0, and store them into lower 32bit of new 64bit v2 */
void vushll_02(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	asm volatile (
		"ldr q0, [%0]\n"
		"ushll2 v2.2d, v0.4s, #0\n" 
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

/* exchange higher 32-bit and lower 32-bit in v0, and store them in new v2 */
void vrev64_01(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	asm volatile (
		"ldr q0, [%0]\n"
		"rev64 v2.2s, v0.2s\n"
		"str q2, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

/* Exchange higher 32-bit and lower 32-bit in v0 (64-bit),
 * and store it into v2 (64-bit).
 *     ABCD --> CDAB
 * Extend v2 (64-bit) to v1 (128-bit).
 *     CDAB --> 0C0D0A0B
 */
void vrev64_02(void)
{
	unsigned char in[16], out[16];
	unsigned char *q = in, *p = out;

	printf("Test in %s:\n", __func__);
	init_buf(q);
	clear_buf(p);
	asm volatile (
		"ldr q0, [%0]\n"
		"rev64 v2.2s, v0.2s\n"
		"ushll v1.2d, v2.2s, #0\n" 
		"str q1, [%1]\n"
		: "=r"(q), "=r"(p)
		: "0"(q), "1"(p)
		: "v0"
	);
	dump_128bit("IN", in);
	dump_128bit("OUT", out);
}

/* just do the multiplication */
void scalar_01(void)
{
	unsigned char in[16], out1[16], out2[16];
	unsigned char *q = in, *p1 = out1, *p2 = out2;
	struct type2 {
		uint32_t v0;
		uint32_t v1;
	};

	printf("Test in SCALAR:\n");
	init_buf(q);
	clear_buf(p1);
	clear_buf(p2);
	asm volatile (
		"ld2 {v0.2s, v1.2s}, [%0]\n"
		"str q0, [%1]\n"
		"str q1, [%2]\n"
		: "=r"(q), "=r"(p1), "=r"(p2)
		: "0"(q), "1"(p1), "2"(p2)
		: "v0"
	);
	struct type2 *s1 = (struct type2 *)p1;
	struct type2 *s2 = (struct type2 *)p2;
	printf("lower: %lx, higher: %lx\n",
		(uint64_t)s1->v0 * (uint64_t)s2->v0,
		(uint64_t)s1->v1 * (uint64_t)s2->v1);
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
        //for (i=0; i < XXH_STRIPE_LEN / sizeof(uint64x2_t); i++) {
	for (i=0; i < 1; i++) {
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

void scalar_02(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	clear_buf(out1);
	//set_buf(out1, 0x11);
	clear_buf(out2);

	XXH3_accumulate_512_scalar((void *)out1, (void *)in1, (void *)in2);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

void neon_01(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	clear_buf(out1);
	//set_buf(out1, 0x11);
	clear_buf(out2);

	XXH3_accumulate_512_neon((void *)out1, (void *)in1, (void *)in2);
	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

int main(void)
{
	//uint128_t in = 0x1122334455667788aabbccddeeff;	// not exists
	//uint64x2_t in = { 0x1122334455667788, 0xaabbccddeeff };

	scalar_02();
	vext_01();
	vushll_01();
	vushll_02();
	vrev64_01();
	vrev64_02();
	vld2_05();
	vld2_06();
	neon_01();
	return 0;
}
