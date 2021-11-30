#include <arm_sve.h>
#include <stdio.h>

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
	svuint64_t* const xin = (const svuint64_t *)in1;
	svuint64_t* const xout = (const svuint64_t *)out;
	svuint16_t tmp = svdup_u16(0x82);
	svbool_t pg = svwhilelt_b32(0, 1);

	svst1(pg, xout, svreinterpret_u64_u16(tmp));
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
	svuint64_t* const xin = (const svuint64_t *)in1;
	svuint64_t* const xout = (const svuint64_t *)out;
	svuint16_t tmp = svdup_u16(0x82);
	svbool_t pg = svwhilelt_b32(0, 14);

	svst1(pg, xout, svreinterpret_u64_u16(tmp));
}

void test(char *name, ftest fn)
{
	unsigned char in1[64], in2[64];
	unsigned char out1[64];

	printf("Test in %s\n", name);
	init_buf(in1, 512);
	clear_buf(in2, 512);
	clear_buf(out1, 512);
	fn(out1, in1, in2);
	dump_bits("IN1", in1, 512);
	dump_bits("OUT1", out1, 512);
}

int main(void)
{
	test("svld1_01", svld1_01);
	test("svld1_02", svld1_02);
	return 0;
}
