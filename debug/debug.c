#include <arm_neon.h>
#include <stdio.h>

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
		"ushll v0.2d, v4.2s, #16\n"
		"shl v4.2d, v0.2d, #16\n"
		/* reverse v1 */
		"rev64 v5.2s, v1.2s\n"
		"ushll v1.2d, v5.2s, #0\n"
		/* combine revsered v0 and v1 together, and store in v4 */
		"orr v3.16b, v4.16b, v1.16b\n"
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
	tmp1 = vshlq_n_u64(tmp1, 32);
	xacc_vec = vaddq_u64(xacc_vec, tmp1);

	key_hi = vrev64_u32(data_hi);
	uint64x2_t tmp2 = vshll_n_u32(key_hi, 0);
	vst1q_u64((uint64_t *)out2, tmp2);
	xacc_vec = vaddq_u64(xacc_vec, tmp2);

	vst1q_u64((uint64_t *)out1, xacc_vec);

	dump_128bit("IN1", in1);
	dump_128bit("IN2", in2);
	dump_128bit("OUT1", out1);
	dump_128bit("OUT2", out2);
}

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
		/* reverse v0: 00AB --> B0A0 */
		"rev64 v4.2s, v0.2s\n"
		"ushll v0.2d, v4.2s, #0\n"
		"shl v4.2d, v0.2d, #32\n"
		"add v4.2d, v2.2d, v4.2d\n"
		/* reverse v3: 00CD --> 0D0C */
		"rev64 v5.2s, v1.2s\n"
		"ushll v1.2d, v5.2s, #0\n"
		"add v4.2d, v4.2d, v1.2d\n"
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

void vld2_08(void)
{
	/* in */
	unsigned char in1[16], in2[16];
	unsigned char out1[16], out2[16];
	unsigned char *q1 = in1, *q2 = in2, *p1 = out1, *p2 = out2;
	uint64_t tmp;

	printf("Test in %s:\n", __func__);
	init_buf(in1);
	set_buf(in2, 0x55);
	set_buf(out1, 0x11);
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
		"umlal v1.2d, v5.2s, v6.2s\n"
		/* reverse v0 */
		"rev64 v4.2s, v0.2s\n"
		"ushll v0.2d, v4.2s, #0\n"
		"shl v4.2d, v0.2d, #32\n"
		/* reverse v3 */
		"rev64 v5.2s, v3.2s\n"
		"ushll v3.2d, v5.2s, #0\n"
		/* combine revsered v0 and v3 together, and store in v4 */
		"orr v3.16b, v4.16b, v3.16b\n"
		"add v4.2d, v1.2d, v3.2d\n"
#if 1
		"str q1, [%3]\n"
		"str q4, [%4]\n"
#endif
#if 0
		"add v0.2d, v1.2d, v0.2d\n"
		"str q2, [%3]\n"
		"str q0, [%4]\n"
#endif
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

void scalar(void)
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

int main(void)
{
	//uint128_t in = 0x1122334455667788aabbccddeeff;	// not exists
	//uint64x2_t in = { 0x1122334455667788, 0xaabbccddeeff };

	scalar();
	//vld2_05();
	vext_01();
	vushll_01();
	vushll_02();
	vrev64_01();
	vrev64_02();
	vld2_05();
	vld2_06();
	vld2_07();
	return 0;
}
