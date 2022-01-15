#include "asmdefs.h"

.data

xxh3_prime_1:	.dword	0x9E3779B1U

.text
.globl XXH_OK
.globl XXH3_128bits_update

#define XXH_ACCEPT_NULL_INPUT_POINTER 2
ENTRY (XXH3_aarch64_update)
	PTR_ARG (0)
	#cmp	r1, #0
	#b.eq	
	#bl XXH3_128bits_update
	ret
END (XXH3_aarch64_update)

# accept 8 parameters
# x0: acc
# x1 nbSStripeSoFarPtr
# x2: nbStripesPerBlock
# x3: input
# x4: nbStripes
ENTRY (XXH3_aarch64_consumeStripes)
	#stp	x19, x30, [sp, #-16]	// Keep x19 and x30 (link register)
	PTR_ARG (0)
	ldr	x9, [x1]
	sub	x8, x2, x7		// nbStripesToEndofBlock = nbStripesPerBlock - *nbStripeSoFarPtr
	sub	x7, x4, x8		// nbStripeAfterBlock = nbStripes - nbStripeToEndofBlock
	# Don't touch x0-x8
	cmp	x8, x4
	b.gt	.LNoScramble
	# accum & scramble
	ret
.LNoScramble:
	# accum only
	ret
END (XXH3_aarch64_consumeStripes)

/*
 * Load data from pointer x0 into z0 register.
 * Input Registers: x0
 * Output Registers: p7 (true for D), z0
 * Modified Registers: p7, z0
 */
ENTRY (XXH3_aarch64_sve_init_acc)
	ptrue	p7.d
	ld1d	{z0.d}, p7/z, [x0]
	ret
END (XXH3_aarch64_sve_init_acc)

ENTRY (XXH3_aarch64_sve_deinit_acc)
	st1d	{z0.d}, p7, [x0]
	ret
END (XXH3_aarch64_sve_deinit_acc)

/*
 * Calculate index [1,0,3,2,...] & save it in register z7.
 * Input Registers: p7 (true for D)
 * Output Registers: z7 (index)
 * Modified Registers: p1, z1, z7
 */
ENTRY (XXH3_aarch64_sve_init_accum)
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d
	ret
END (XXH3_aarch64_sve_init_accum)

/*
 * Input Registers: x0 (acc), x1 (input), x2 (secret), z0 (xacc), z7 (index), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z6, x11-x12
 */
ENTRY (XXH3_aarch64_sve_acc512)
	mov	w12, #0xffffffff
	mov	x11, xzr
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	//ld1d	{z0.d}, p7/z, [x0, x11, lsl #3]
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	ld1d	{z2.d}, p7/z, [x2, x11, lsl #3]
	// swapped = SWAP(input)
	tbl	z5.d, {z1.d}, z7.d
	// mixed = input EOR secret
	eor	z1.d, p7/m, z1.d, z2.d
	mov	z6.d, z1.d
	// mixed_lo = mixed AND 0xffffffff
	and	z1.d, p7/m, z1.d, z3.d
	// mixed_hi = mixed >> 32
	lsr	z6.d, p7/m, z6.d, z4.d
	// mixed_lo = mixed_hi * acc + mixed_lo
	mad	z1.d, p7/m, z6.d, z0.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	// always store acc in z0
	mov	z0.d, z1.d
	ret
END (XXH3_aarch64_sve_acc512)

/*
 * Input Registers: x0 (acc), x1 (input), x2 (secret), x3 (nbStripes),
 *		    z0 (xacc), z7 (index), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z6, x1-x2, x10-x12
 */
ENTRY (XXH3_aarch64_sve_accumulate)
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	mov	x10, xzr
	cmp	x10, x3
	b.ge	1f
0:
	bl	XXH3_aarch64_sve_acc512
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	0b
1:
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve_accumulate)

/*
 * Input Registers: x0 (acc), x1 (secret), z0 (xacc), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z3, x10-x12
 */
ENTRY (XXH3_aarch64_sve_scramble)
	ldr	x10, address_of_xxh3_prime_1
	mov	x11, xzr
	ldr	x12, [x10]
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	mov	z3.d, x12
	eor	z1.d, z0.d, z1.d
	lsr	z2.d, z0.d, #47
	eor	z0.d, z1.d, z2.d
	mul	z0.d, p7/m, z0.d, z3.d
	ret
END (XXH3_aarch64_sve_scramble)

/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *		    x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit),
 *		    z0 (xacc), z7 (index), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z6, x1-x8, x10-x16
 */
ENTRY (XXH3_aarch64_sve_consume_stripes)
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	mov	x7, x1		// save x1 (nbStripesSoFarPtr)
	ldr	x13, [x7]	// x13: nbStripesSoFarPtr[0]
	sub	x14, x2, x13	// x14: nbStripesToEndofBlock
	mov	x1, x3		// overwrite x1 to store input
	mov	x8, x3		// save x3 (input)
	lsl	x15, x13, #3	// x15: nbStripesSoFarPtr[0] * 8
	cmp	x14, x4
	b.le	0f
	add	x2, x5, x15	// overwrite x2 to store secret
	mov	x3, x4		// overwrite x3 to store nbStripes
	bl	XXH3_aarch64_sve_accumulate
	add	x13, x13, x4
	str	x13, [x7]
	st1d	{z0.d}, p7, [x0, x11, lsl #3]
	ldp	x29, x30, [sp], #16
	ret
0:
	/* need a scrambling operation */
	sub	x16, x4, x14	// x16: nbStripesAfterBlock
	add	x2, x5, x15	// overwrite x2 to store secret
	mov	x3, x14		// overwirte x3 to store nbStripesToEndofBlock
	bl	XXH3_aarch64_sve_accumulate
	add	x1, x5, x6	// overwrite x1 to store secret
	bl	XXH3_aarch64_sve_scramble
	lsl	x2, x14, #6	// x2: nbStripesToEndofBlock * XXH_STRIPE_LEN
	add	x1, x8, x2	// overwrite x1 to store input
	mov	x2, x5		// overwrite x2 to store secret
	mov	x3, x16		// overwrite x3 to store nbStripesAfterBlock
	bl	XXH3_aarch64_sve_accumulate
	str	x16, [x7]
	st1d	{z0.d}, p7, [x0, x11, lsl #3]
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve_consume_stripes)

/*
 * Input Registers: x0 (acc), x1 (input), x2 (len), x3 (secret), x4 (secretSize),
 *		    z0 (xacc), z7 (index), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z6, x1-x8, x10-x16
 */
ENTRY (XXH3_aarch64_sve_internal_loop)
	stp	x29, x30, [sp, #-32]!
	stp	x19, x20, [sp, #16]
	mov	x29, sp

	mov	x13, x1		// save input to x13
	mov	x14, x2		// save len to x14
	mov	x15, x3		// save secret to x15
	mov	x19, x4		// save secretSize to x19
	/*
	 * nbStripesPerBlock =
	 * (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
	 */
	sub	x5, x4, #64
	lsr	x5, x5, #3	// x5: nbStripesPerBlock
	/* block_len = XXH_STRIPE_LEN * nbStripesPerBlock; */
	lsl	x6, x5, #6	// x6: block_len
	sub	x7, x2, #1	// x7: len - 1
	/* nb_blocks = (len - 1) / block_len; */
	udiv	x20, x7, x6	// x20: nb_blocks
	mov	x9, xzr		// x9: n
	cmp	x9, x20
	b.ge	1f
0:
	mul	x1, x6, x9	// x1: n * block_len
	add	x1, x1, x13	// x1: input + n * block_len
	mov	x2, x15		// x2: secret
	mov	x3, x5		// x3: nbStripesPerBlock
	bl	XXH3_aarch64_sve_accumulate
	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	bl	XXH3_aarch64_sve_scramble
	add	x9, x9, #1
	cmp	x9, x20
	b.lt	0b
1:
	/*
	 * nbStripes =
	 * ((len - 1) - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
	 */
	mul	x9, x6, x20	// overwrite x9 (n)
	sub	x7, x7, x9
	lsr	x3, x7, #6	// x3: nbStripes
	mov	x2, x15		// x2: secret
	add	x1, x13, x9	// x1: input
	bl	XXH3_aarch64_sve_accumulate
	/* p = input + len - XXH_STRIPE_LEN; */
	add	x1, x13, x14
	sub	x1, x1, #64	// x1: p
	/* secret + secretSize - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START); */
	add	x2, x15, x19
	sub	x2, x2, #64
	sub	x2, x2, #7
	bl	XXH3_aarch64_sve_acc512
	//st1d	z0.d, p7, [x0]
	ldp	x19, x20, [sp, #16]
	ldp	x29, x30, [sp], #32
	ret
END (XXH3_aarch64_sve_internal_loop)

ENTRY (XXH3_aarch64_sve_internal_loop2)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	mov	x29, sp
	addvl	sp, sp, #-1
	str	z8, [sp]	// save z8

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	ldr	x10, address_of_xxh3_prime_1
	ldr	x12, [x10]
	mov	x11, xzr
	mov	z8.d, x12	// save XXH3_PRIME_1 into z8

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	ptrue	p7.d
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d

	ld1d	{z0.d}, p7/z, [x0, x11, lsl #3]
	/*
	 * nbStripesPerBlock =
	 * (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
	 */
	sub	x5, x4, #64
	lsr	x5, x5, #3	// x5: nbStripesPerBlock
	/* block_len = XXH_STRIPE_LEN * nbStripesPerBlock; */
	lsl	x6, x5, #6	// x6: block_len
	sub	x7, x2, #1	// x7: len - 1
	/* nb_blocks = (len - 1) / block_len; */
	udiv	x20, x7, x6	// x20: nb_blocks
	mov	x9, xzr		// x9: n
	cmp	x9, x20
	b.ge	1f
0:
	mul	x1, x6, x9	// x1: n * block_len
	add	x1, x1, x13	// x1: input + n * block_len
	mov	x2, x15		// x2: secret
	mov	x3, x5		// x3: nbStripesPerBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	mov	w12, #0xffffffff
	mov	x11, xzr
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	//ld1d	{z0.d}, p7/z, [x0, x11, lsl #3]
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	ld1d	{z2.d}, p7/z, [x2, x11, lsl #3]
	// swapped = SWAP(input)
	tbl	z5.d, {z1.d}, z7.d
	// mixed = input EOR secret
	eor	z1.d, p7/m, z1.d, z2.d
	mov	z6.d, z1.d
	// mixed_lo = mixed AND 0xffffffff
	and	z1.d, p7/m, z1.d, z3.d
	// mixed_hi = mixed >> 32
	lsr	z6.d, p7/m, z6.d, z4.d
	// mixed_lo = mixed_hi * acc + mixed_lo
	mad	z1.d, p7/m, z6.d, z0.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	// always store acc in z0
	mov	z0.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:
	/* end accumulate function */

	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	#bl	XXH3_aarch64_sve_scramble
	/* start scramble function */
	mov	x11, xzr
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	eor	z1.d, z0.d, z1.d
	lsr	z2.d, z0.d, #47
	eor	z0.d, z1.d, z2.d
	mul	z0.d, p7/m, z0.d, z8.d
	/* end scramble function */
	add	x9, x9, #1
	cmp	x9, x20
	b.lt	0b
1:
	/*
	 * nbStripes =
	 * ((len - 1) - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
	 */
	mul	x9, x6, x20	// overwrite x9 (n)
	sub	x7, x7, x9
	lsr	x3, x7, #6	// x3: nbStripes
	mov	x2, x15		// x2: secret
	add	x1, x13, x9	// x1: input
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	mov	w12, #0xffffffff
	mov	x11, xzr
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	//ld1d	{z0.d}, p7/z, [x0, x11, lsl #3]
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	ld1d	{z2.d}, p7/z, [x2, x11, lsl #3]
	// swapped = SWAP(input)
	tbl	z5.d, {z1.d}, z7.d
	// mixed = input EOR secret
	eor	z1.d, p7/m, z1.d, z2.d
	mov	z6.d, z1.d
	// mixed_lo = mixed AND 0xffffffff
	and	z1.d, p7/m, z1.d, z3.d
	// mixed_hi = mixed >> 32
	lsr	z6.d, p7/m, z6.d, z4.d
	// mixed_lo = mixed_hi * acc + mixed_lo
	mad	z1.d, p7/m, z6.d, z0.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	// always store acc in z0
	mov	z0.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	20b
21:
	/* end accumulate function */

	/* p = input + len - XXH_STRIPE_LEN; */
	add	x1, x13, x14
	sub	x1, x1, #64	// x1: p
	/* secret + secretSize - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START); */
	add	x2, x15, x19
	sub	x2, x2, #64
	sub	x2, x2, #7
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	mov	w12, #0xffffffff
	mov	x11, xzr
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	//ld1d	{z0.d}, p7/z, [x0, x11, lsl #3]
	ld1d	{z1.d}, p7/z, [x1, x11, lsl #3]
	ld1d	{z2.d}, p7/z, [x2, x11, lsl #3]
	// swapped = SWAP(input)
	tbl	z5.d, {z1.d}, z7.d
	// mixed = input EOR secret
	eor	z1.d, p7/m, z1.d, z2.d
	mov	z6.d, z1.d
	// mixed_lo = mixed AND 0xffffffff
	and	z1.d, p7/m, z1.d, z3.d
	// mixed_hi = mixed >> 32
	lsr	z6.d, p7/m, z6.d, z4.d
	// mixed_lo = mixed_hi * acc + mixed_lo
	mad	z1.d, p7/m, z6.d, z0.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	// always store acc in z0
	mov	z0.d, z1.d
	/* end acc512 function */
	st1d	z0.d, p7, [x0]
	ldr	z8, [sp]
	addvl	sp, sp, #1
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve_internal_loop2)

address_of_xxh3_prime_1:	.dword	xxh3_prime_1
