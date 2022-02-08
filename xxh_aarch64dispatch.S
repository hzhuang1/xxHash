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
 * Load data from pointer x0 into z0 register.
 * Input Registers: x0
 * Output Registers: p7 (true for D), z0
 * Modified Registers: p7, z8-z12, x10, x12
 */
ENTRY (XXH3_aarch64_sve128_init_acc)
	ptrue	p7.d
	addvl	sp, sp, #-5
	ldr	x10, address_of_xxh3_prime_1
	st1d	{z8.d}, p7, [sp]		// save z8
	st1d	{z9.d}, p7, [sp, #1, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #2, MUL VL]	// save z10
	st1d	{z11.d}, p7, [sp, #3, MUL VL]	// save z11
	st1d	{z12.d}, p7, [sp, #4, MUL VL]	// save z12
	ldr	x12, [x10]
	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z11.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z12.d}, p7/z, [x0, #3, MUL VL]
	mov	z8.d, x12			// save XXH3_PRIME_1
	ret
END (XXH3_aarch64_sve128_init_acc)

ENTRY (XXH3_aarch64_sve128_deinit_acc)
	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
	st1d	{z11.d}, p7, [x0, #2, MUL VL]
	st1d	{z12.d}, p7, [x0, #3, MUL VL]
	ld1d	{z8.d}, p7/z, [sp]
	ld1d	{z9.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z11.d}, p7/z, [sp, #3, MUL VL]
	ld1d	{z12.d}, p7/z, [sp, #4, MUL VL]
	addvl	sp, sp, #5
	ret
END (XXH3_aarch64_sve128_deinit_acc)

ENTRY (XXH3_aarch64_sve256_init_acc)
	ptrue	p7.d
	addvl	sp, sp, #-5
	ldr	x10, address_of_xxh3_prime_1
	st1d	{z8.d}, p7, [sp]		// save z8
	st1d	{z9.d}, p7, [sp, #1, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #2, MUL VL]	// save z10
	st1d	{z11.d}, p7, [sp, #3, MUL VL]	// save z11
	st1d	{z12.d}, p7, [sp, #4, MUL VL]	// save z12
	ldr	x12, [x10]
	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z11.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z12.d}, p7/z, [x0, #3, MUL VL]
	mov	z8.d, x12			// save XXH3_PRIME_1
	ret
END (XXH3_aarch64_sve256_init_acc)

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
 * Input Registers: x0 (acc), x1 (input), x2 (secret), z0 (xacc), z7 (index), p7
 * Output Registers: z9-z12 (xacc)
 * Modified Registers: z0-z6, z9-z12, x12
 */
ENTRY (XXH3_aarch64_sve128_acc512)
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	mov	z0.d, z11.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mov	z11.d, z1.d

	mov	z0.d, z12.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mov	z12.d, z1.d
	ret
END (XXH3_aarch64_sve128_acc512)

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
 * Input Registers: x0 (acc), x1 (input), x2 (secret), x3 (nbStripes),
 *		    z0 (xacc), z7 (index), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z0-z6, z9-z12, x1-x2, x10, x12
 */
ENTRY (XXH3_aarch64_sve128_accumulate)
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	mov	x10, xzr
	cmp	x10, x3
	b.ge	1f
0:
	bl	XXH3_aarch64_sve128_acc512
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	0b
1:
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve128_accumulate)

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
 * Input Registers: x0 (acc), x1 (secret), z0 (xacc), p7
 * Output Registers: z0 (xacc)
 * Modified Registers: z1-z2, z9-z12, x10-x12
 */
ENTRY (XXH3_aarch64_sve128_scramble)
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z9.d, z1.d
	lsr	z2.d, z9.d, #47
	eor	z9.d, z1.d, z2.d
	mul	z9.d, p7/m, z9.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z10.d, z1.d
	lsr	z2.d, z10.d, #47
	eor	z10.d, z1.d, z2.d
	mul	z10.d, p7/m, z10.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	eor	z1.d, z11.d, z1.d
	lsr	z2.d, z11.d, #47
	eor	z11.d, z1.d, z2.d
	mul	z11.d, p7/m, z11.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	eor	z1.d, z12.d, z1.d
	lsr	z2.d, z12.d, #47
	eor	z12.d, z1.d, z2.d
	mul	z12.d, p7/m, z12.d, z8.d
	ret
END (XXH3_aarch64_sve128_scramble)

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

	mov	x8, x1		// save nbStripeSoFarPtr to x8
	mov	x9, x2		// save nbStripesPerBlock to x9
	mov	x13, x3		// save input to x13
	mov	x14, x4		// save nbStripes to x14
	mov	x15, x5		// save secret to x15
	mov	x16, x6		// save secretLimit to x16

	ldr	x17, [x8]	// x17: nbStripesSoFarPtr[0]
	subs	x7, x2, x17	// x7: nbStripesToEndofBlock
	b.mi	0f		// error & exit
	subs	x6, x4, x7	// x6: nbStripesAfterBlock
	mov	x1, x13		// x1: input
	mov	x4, #8
	madd	x2, x17, x4, x15 // x2: secret + nbStripesSoFarPtr[0] * XXH_SECRET_CONSUME_RATE
	b.mi	1f		// no scramble
	/* need scramble */
	mov	x3, x7		// x3: nbStripesToEndofBlock
	bl	XXH3_aarch64_sve_accumulate
	mov	x1, x15
	add	x1, x1, x16	// x1: secret + secretLimit
	bl	XXH3_aarch64_sve_scramble
	mov	x4, #64
	madd	x1, x7, x4, x13	// x1: input + nbStripesToEndofBlock * XXH_STRIPE_LEN
	mov	x2, x15		// x2: secret
	mov	x3, x6		// x3: nbStripesAfterBlock
	bl	XXH3_aarch64_sve_accumulate
	str	x6, [x8]

	st1d	{z0.d}, p7, [x0]
	ldp	x29, x30, [sp], #16
	ret

1:
	/* no scramble */
	mov	x3, x14		// x3: nbStripes
	bl	XXH3_aarch64_sve_accumulate
	add	x17, x17, x14
	str	x17, [x8]
	st1d	{z0.d}, p7, [x0]
0:
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve_consume_stripes)

ENTRY (XXH3_aarch64_sve128_consume_stripes)
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	addvl	sp, sp, #-6
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

	st1d	{z8.d}, p7, [sp, #1, MUL VL]	// save z8
	st1d	{z9.d}, p7, [sp, #2, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #3, MUL VL]	// save z10
	st1d	{z11.d}, p7, [sp, #4, MUL VL]	// save z11
	st1d	{z12.d}, p7, [sp, #5, MUL VL]	// save z12

	mov	x8, x1		// save nbStripeSoFarPtr to x8
	mov	x9, x2		// save nbStripesPerBlock to x9
	mov	x13, x3		// save input to x13
	mov	x14, x4		// save nbStripes to x14
	mov	x15, x5		// save secret to x15
	mov	x16, x6		// save secretLimit to x16
	ldr	x10, address_of_xxh3_prime_1
	ldr	x12, [x10]
	mov	z8.d, x12	// save XXH3_PRIME_1 into z8
	mov	w12, #0xffffffff // x12: -1
	// set -1 in z3
	mov	z3.d, x12
	mov	z4.d, #32

	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z11.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z12.d}, p7/z, [x0, #3, MUL VL]

	ldr	x17, [x8]	// x17: nbStripesSoFarPtr[0]
	subs	x7, x2, x17	// x7: nbStripesToEndofBlock
	b.mi	0f		// error & exit
	subs	x6, x4, x7	// x6: nbStripesAfterBlock
	mov	x1, x13		// x1: input
	mov	x4, #8
	madd	x2, x17, x4, x15 // x2: secret + nbStripesSoFarPtr[0] * XXH_SECRET_CONSUME_RATE
	b.mi	1f		// no scramble
	/* need scramble */
	mov	x3, x7		// x3: nbStripesToEndofBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mad	z1.d, p7/m, z6.d, z9.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z9.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z10.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z10.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z11.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z11.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z12.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z12.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:
	/* end accumulate function */
	mov	x1, x15
	add	x1, x1, x16	// x1: secret + secretLimit
	#bl	XXH3_aarch64_sve_scramble
	/* start scramble function */
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z9.d, z1.d
	lsr	z5.d, z9.d, #47
	eor	z9.d, z1.d, z5.d
	mul	z9.d, p7/m, z9.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z10.d, z1.d
	lsr	z5.d, z10.d, #47
	eor	z10.d, z1.d, z5.d
	mul	z10.d, p7/m, z10.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	eor	z1.d, z11.d, z1.d
	lsr	z5.d, z11.d, #47
	eor	z11.d, z1.d, z5.d
	mul	z11.d, p7/m, z11.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	eor	z1.d, z12.d, z1.d
	lsr	z5.d, z12.d, #47
	eor	z12.d, z1.d, z5.d
	mul	z12.d, p7/m, z12.d, z8.d
	/* end scramble function */
	mov	x4, #64
	madd	x1, x7, x4, x13	// x1: input + nbStripesToEndofBlock * XXH_STRIPE_LEN
	mov	x2, x15		// x2: secret
	mov	x3, x6		// x3: nbStripesAfterBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mad	z1.d, p7/m, z6.d, z9.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z9.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z10.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z10.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z11.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z11.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z12.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z12.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	20b
21:
	/* end accumulate function */
	str	x6, [x8]

	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
	st1d	{z11.d}, p7, [x0, #2, MUL VL]
	st1d	{z12.d}, p7, [x0, #3, MUL VL]
	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	ld1d	{z11.d}, p7/z, [sp, #4, MUL VL]
	ld1d	{z12.d}, p7/z, [sp, #5, MUL VL]
	addvl	sp, sp, #6
	ldp	x29, x30, [sp], #16
	ret

1:
	/* no scramble */
	mov	x3, x14		// x3: nbStripes
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	31f
30:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mad	z1.d, p7/m, z6.d, z9.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z9.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z10.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z10.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z11.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z11.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z12.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z12.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	30b
31:
	/* end accumulate function */
	add	x17, x17, x14
	str	x17, [x8]
	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
	st1d	{z11.d}, p7, [x0, #2, MUL VL]
	st1d	{z12.d}, p7, [x0, #3, MUL VL]
0:
	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	ld1d	{z11.d}, p7/z, [sp, #4, MUL VL]
	ld1d	{z12.d}, p7/z, [sp, #5, MUL VL]
	addvl	sp, sp, #6
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve128_consume_stripes)

ENTRY (XXH3_aarch64_sve256_consume_stripes)
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	addvl	sp, sp, #-4
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

	st1d	{z8.d}, p7, [sp, #1, MUL VL]	// save z8
	st1d	{z9.d}, p7, [sp, #2, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #3, MUL VL]	// save z10

	mov	x8, x1		// save nbStripeSoFarPtr to x8
	mov	x9, x2		// save nbStripesPerBlock to x9
	mov	x13, x3		// save input to x13
	mov	x14, x4		// save nbStripes to x14
	mov	x15, x5		// save secret to x15
	mov	x16, x6		// save secretLimit to x16
	ldr	x10, address_of_xxh3_prime_1
	ldr	x12, [x10]
	mov	z8.d, x12	// save XXH3_PRIME_1 into z8
	mov	w12, #0xffffffff // x12: -1
	// set -1 in z3
	mov	z3.d, x12
	mov	z4.d, #32

	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]

	ldr	x17, [x8]	// x17: nbStripesSoFarPtr[0]
	subs	x7, x2, x17	// x7: nbStripesToEndofBlock
	b.mi	0f		// error & exit
	subs	x6, x4, x7	// x6: nbStripesAfterBlock
	mov	x1, x13		// x1: input
	mov	x4, #8
	madd	x2, x17, x4, x15 // x2: secret + nbStripesSoFarPtr[0] * XXH_SECRET_CONSUME_RATE
	b.mi	1f		// no scramble
	/* need scramble */
	mov	x3, x7		// x3: nbStripesToEndofBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mad	z1.d, p7/m, z6.d, z9.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z9.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z10.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z10.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:
	/* end accumulate function */
	mov	x1, x15
	add	x1, x1, x16	// x1: secret + secretLimit
	#bl	XXH3_aarch64_sve_scramble
	/* start scramble function */
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z9.d, z1.d
	lsr	z5.d, z9.d, #47
	eor	z9.d, z1.d, z5.d
	mul	z9.d, p7/m, z9.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z10.d, z1.d
	lsr	z5.d, z10.d, #47
	eor	z10.d, z1.d, z5.d
	mul	z10.d, p7/m, z10.d, z8.d
	/* end scramble function */
	mov	x4, #64
	madd	x1, x7, x4, x13	// x1: input + nbStripesToEndofBlock * XXH_STRIPE_LEN
	mov	x2, x15		// x2: secret
	mov	x3, x6		// x3: nbStripesAfterBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	20b
21:
	/* end accumulate function */
	str	x6, [x8]

	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	addvl	sp, sp, #4
	ldp	x29, x30, [sp], #16
	ret

1:
	/* no scramble */
	mov	x3, x14		// x3: nbStripes
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	31f
30:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mad	z1.d, p7/m, z6.d, z9.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z9.d, z1.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mad	z1.d, p7/m, z6.d, z10.d
	// acc = mixed_lo + swapped
	add	z1.d, p7/m, z1.d, z5.d
	mov	z10.d, z1.d
	/* end acc512 function */
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	30b
31:
	/* end accumulate function */
	add	x17, x17, x14
	str	x17, [x8]
	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
0:
	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	addvl	sp, sp, #4
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve256_consume_stripes)

ENTRY (XXH3_aarch64_sve512_consume_stripes)
	stp	x29, x30, [sp, #-16]!
	addvl	sp, sp, #-1
	mov	x29, sp
	str	z8, [sp]	// save z8

	mov	x8, x1		// save nbStripeSoFarPtr to x8
	mov	x9, x2		// save nbStripesPerBlock to x9
	mov	x13, x3		// save input to x13
	mov	x14, x4		// save nbStripes to x14
	mov	x15, x5		// save secret to x15
	mov	x16, x6		// save secretLimit to x16
	ldr	x10, address_of_xxh3_prime_1
	ldr	x12, [x10]
	mov	z8.d, x12	// save XXH3_PRIME_1 into z8
	mov	w12, #0xffffffff // x12: -1
	// set -1 in z3
	mov	z3.d, x12
	mov	z4.d, #32

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	ptrue	p7.d, VL8
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d

	ld1d	{z0.d}, p7/z, [x0]

	ldr	x17, [x8]	// x17: nbStripesSoFarPtr[0]
	subs	x7, x2, x17	// x7: nbStripesToEndofBlock
	b.mi	0f		// error & exit
	subs	x6, x4, x7	// x6: nbStripesAfterBlock
	mov	x1, x13		// x1: input
	mov	x4, #8
	madd	x2, x17, x4, x15 // x2: secret + nbStripesSoFarPtr[0] * XXH_SECRET_CONSUME_RATE
	b.mi	1f		// no scramble
	/* need scramble */
	mov	x3, x7		// x3: nbStripesToEndofBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	x1, x15
	add	x1, x1, x16	// x1: secret + secretLimit
	#bl	XXH3_aarch64_sve_scramble
	/* start scramble function */
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z0.d, z1.d
	lsr	z5.d, z0.d, #47
	eor	z0.d, z1.d, z5.d
	mul	z0.d, p7/m, z0.d, z8.d
	/* end scramble function */
	mov	x4, #64
	madd	x1, x7, x4, x13	// x1: input + nbStripesToEndofBlock * XXH_STRIPE_LEN
	mov	x2, x15		// x2: secret
	mov	x3, x6		// x3: nbStripesAfterBlock
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	str	x6, [x8]

	st1d	{z0.d}, p7, [x0]
	ldr	z8, [sp]
	addvl	sp, sp, #1
	ldp	x29, x30, [sp], #16
	ret

1:
	/* no scramble */
	mov	x3, x14		// x3: nbStripes
	#bl	XXH3_aarch64_sve_accumulate
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	31f
30:
	#bl	XXH3_aarch64_sve_acc512
	/* start acc512 function */
	// z3 and z4 are initialized already
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	b.lt	30b
31:
	/* end accumulate function */
	add	x17, x17, x14
	str	x17, [x8]
	st1d	{z0.d}, p7, [x0]
0:
	ldr	z8, [sp]
	addvl	sp, sp, #1
	ldp	x29, x30, [sp], #16
	ret
END (XXH3_aarch64_sve512_consume_stripes)

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

ENTRY (XXH3_aarch64_sve128_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	addvl	sp, sp, #-6
	mov	x29, sp
	ptrue	p7.d
	ldr	x10, address_of_xxh3_prime_1
	st1d	{z8.d}, p7, [sp, #1, MUL VL]	// save z8
	st1d	{z9.d}, p7, [sp, #2, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #3, MUL VL]	// save z10
	st1d	{z11.d}, p7, [sp, #4, MUL VL]	// save z11
	st1d	{z12.d}, p7, [sp, #5, MUL VL]	// save z12

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	ldr	x12, [x10]
	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z11.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z12.d}, p7/z, [x0, #3, MUL VL]
	mov	z8.d, x12			// save XXH3_PRIME_1

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d

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
	#bl	XXH3_aarch64_sve128_accumulate
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve128_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	mov	z0.d, z11.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mov	z11.d, z1.d

	mov	z0.d, z12.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mov	z12.d, z1.d
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:

	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	#bl	XXH3_aarch64_sve128_scramble
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z9.d, z1.d
	lsr	z2.d, z9.d, #47
	eor	z9.d, z1.d, z2.d
	mul	z9.d, p7/m, z9.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z10.d, z1.d
	lsr	z2.d, z10.d, #47
	eor	z10.d, z1.d, z2.d
	mul	z10.d, p7/m, z10.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	eor	z1.d, z11.d, z1.d
	lsr	z2.d, z11.d, #47
	eor	z11.d, z1.d, z2.d
	mul	z11.d, p7/m, z11.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	eor	z1.d, z12.d, z1.d
	lsr	z2.d, z12.d, #47
	eor	z12.d, z1.d, z2.d
	mul	z12.d, p7/m, z12.d, z8.d
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
	#bl	XXH3_aarch64_sve128_accumulate
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve128_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	mov	z0.d, z11.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mov	z11.d, z1.d

	mov	z0.d, z12.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mov	z12.d, z1.d
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	20b
21:
	/* p = input + len - XXH_STRIPE_LEN; */
	add	x1, x13, x14
	sub	x1, x1, #64	// x1: p
	/* secret + secretSize - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START); */
	add	x2, x15, x19
	sub	x2, x2, #64
	sub	x2, x2, #7
	#bl	XXH3_aarch64_sve128_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	mov	z0.d, z11.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
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
	mov	z11.d, z1.d

	mov	z0.d, z12.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
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
	mov	z12.d, z1.d

	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]
	st1d	{z11.d}, p7, [x0, #2, MUL VL]
	st1d	{z12.d}, p7, [x0, #3, MUL VL]

	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	ld1d	{z11.d}, p7/z, [sp, #4, MUL VL]
	ld1d	{z12.d}, p7/z, [sp, #5, MUL VL]
	addvl	sp, sp, #6
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve128_internal_loop)

ENTRY (XXH3_aarch64_sve256_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	addvl	sp, sp, #-4
	mov	x29, sp
	ptrue	p7.d
	ldr	x10, address_of_xxh3_prime_1
	st1d	{z8.d}, p7, [sp, #1, MUL VL]	// save z8
	st1d	{z9.d}, p7, [sp, #2, MUL VL]	// save z9
	st1d	{z10.d}, p7, [sp, #3, MUL VL]	// save z10

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	ldr	x12, [x10]
	ld1d	{z9.d}, p7/z, [x0]
	ld1d	{z10.d}, p7/z, [x0, #1, MUL VL]
	mov	z8.d, x12	// save XXH3_PRIME_1

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d

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
	#bl	XXH3_aarch64_sve256_accumulate
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	#bl	XXH3_aarch64_sve256_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:

	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	#bl	XXH3_aarch64_sve256_scramble
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z9.d, z1.d
	lsr	z2.d, z9.d, #47
	eor	z9.d, z1.d, z2.d
	mul	z9.d, p7/m, z9.d, z8.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z10.d, z1.d
	lsr	z2.d, z10.d, #47
	eor	z10.d, z1.d, z2.d
	mul	z10.d, p7/m, z10.d, z8.d
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
	#bl	XXH3_aarch64_sve256_accumulate
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	#bl	XXH3_aarch64_sve256_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	20b
21:
	/* p = input + len - XXH_STRIPE_LEN; */
	add	x1, x13, x14
	sub	x1, x1, #64	// x1: p
	/* secret + secretSize - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START); */
	add	x2, x15, x19
	sub	x2, x2, #64
	sub	x2, x2, #7
	#bl	XXH3_aarch64_sve256_acc512
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	// make z0 loaded outside
	mov	z0.d, z9.d
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	mov	z9.d, z1.d

	mov	z0.d, z10.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
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
	mov	z10.d, z1.d

	st1d	{z9.d}, p7, [x0]
	st1d	{z10.d}, p7, [x0, #1, MUL VL]

	ld1d	{z8.d}, p7/z, [sp, #1, MUL VL]
	ld1d	{z9.d}, p7/z, [sp, #2, MUL VL]
	ld1d	{z10.d}, p7/z, [sp, #3, MUL VL]
	addvl	sp, sp, #4
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve256_internal_loop)

ENTRY (XXH3_aarch64_sve512_internal_loop)
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
	mov	z8.d, x12	// save XXH3_PRIME_1 into z8

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	ptrue	p7.d, VL8
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d

	ld1d	{z0.d}, p7/z, [x0]
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
	mov	w12, #0xffffffff // x12: -1
	mov	x9, xzr		// x9: n
	// set -1 in z3
	mov	z3.d, x12
	mov	z4.d, #32
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
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z0.d, z1.d
	lsr	z5.d, z0.d, #47
	eor	z0.d, z1.d, z5.d
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
	// z3 and z4 are initialized already
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
	// make z0 loaded outside
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
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
END (XXH3_aarch64_sve512_internal_loop)

address_of_xxh3_prime_1:	.dword	xxh3_prime_1
