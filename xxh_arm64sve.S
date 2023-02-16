/*
 * xxHash - Extremely Fast Hash algorithm
 *
 * Copyright (C) 2022 Linaro Ltd.
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at:
 *   - xxHash homepage: https://www.xxhash.com
 *   - xxHash source repository: https://github.com/Cyan4973/xxHash
 */

	.arch armv8-a+sve

#include "asmdefs.h"

/*
 * Since SVE support in C compilers is fairly new and not very optimized, the
 * SVE routines are written in assembly.
 * The code is to avoid loading/storing ACC from memory frequently. It could
 * improve performance by this way.
 */


/*
 * Create sequence [1,0,3,2,...].
 */
.macro INIT_INDEX
	// set z7 to [ 1, 0, 3, 2, 5, 4, ... ] for tbl to swap adjacent lanes
	index   z7.d, #0, #1        // z7 = [ 0, 1, 2, 3, 4, 5... ]
	eor     z7.d, z7.d, #1      // z7 = [ 1, 0, 3, 2, 5, 4... ]
.endm

/*
 * Perform a single round of XXH_accumulate_512().
 * \acc = XXH3_accumulate_512_round(\acc, LDR(x1, \memoffs), LDR(x2, \memoffs))
 */
.macro ACCRND acc, memoffs:vararg
	// load input
	ld1d	{z1.d}, p7/z, [x1, \memoffs]
	// load secret
	ld1d	{z2.d}, p7/z, [x2, \memoffs]
	// prefetch data
	PRFD    PLDL1STRM, p7, [x1, #31, MUL VL]
	// mixed = secret EOR input
	eor	z2.d, p7/m, z2.d, z1.d
	// swapped = SWAP(input)
	tbl	z1.d, {z1.d}, z7.d
	// mixed_lo = mixed AND 0xffffffff (technically (u64)(u32) mixed)
	uxtw	z6.d, p7/m, z2.d
	// mixed_hi = mixed >> 32
	lsr	z2.d, p7/m, z2.d, #32
	// mixed_lo = mixed_hi * mixed_lo + swapped (distributive property)
	mad	z2.d, p7/m, z6.d, z1.d
	// acc += mixed_lo
	add	\acc, p7/m, \acc, z2.d
.endm

/*
 * Perform a single round of XXH3_scrambleAcc().
 * \acc = XXH3_scrambleAcc(\acc, LDR(x1, \memoffs))
 */
.macro SCRAM acc, memoffs:vararg
	ld1d	{z1.d}, p7/z, [x1, \memoffs]
	eor	z1.d, \acc, z1.d
	lsr	z5.d, \acc, #47
	eor	\acc, z1.d, z5.d
	mul	\acc, p7/m, \acc, z16.d
.endm

/*
 * Load the prime value into x12.
 */
.macro LDR_PRM
	movz	x12, #0x9E37, LSL #16
	mov	x10, #0x79B1
	orr	x12, x12, x10
.endm


/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *                  x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit)
 * Output Registers: None
 * Modified Registers: p1, p7, z0-z7, z16-z20, x1-x4, x6-x17
 */
ENTRY (XXH3_sve128_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	mov	x29, sp
	ptrue	p7.d

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	LDR_PRM
	ld1d	{z17.d}, p7/z, [x0]
	ld1d	{z18.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z19.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z20.d}, p7/z, [x0, #3, MUL VL]
	mov	z16.d, x12			// save XXH3_PRIME_1

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
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
	ACCRND	z19.d, #2, MUL VL
	ACCRND	z20.d, #3, MUL VL
*/
	ldr	z21, [x1, #0, MUL VL]
	ldr	z25, [x2, #0, MUL VL]
		ldr	z22, [x1, #1, MUL VL]
		ldr	z26, [x2, #1, MUL VL]
		ldr	z23, [x1, #2, MUL VL]
		ldr	z27, [x2, #2, MUL VL]
		ldr	z24, [x1, #3, MUL VL]
		ldr	z28, [x2, #3, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z25.d, z25.d, z21.d
		eor	z26.d, z26.d, z22.d
		eor	z27.d, z27.d, z23.d
		eor	z28.d, z28.d, z24.d
	ext	z21.b, z21.b, z21.b, 8
		ext	z22.b, z22.b, z22.b, 8
		ext	z23.b, z23.b, z23.b, 8
		ext	z24.b, z24.b, z24.b, 8
	uxtw	z1.d, p7/m, z25.d
		uxtw	z2.d, p7/m, z26.d
		uxtw	z3.d, p7/m, z27.d
		uxtw	z4.d, p7/m, z28.d
	lsr	z25.d, p7/m, z25.d, #32
		lsr	z26.d, p7/m, z26.d, #32
		lsr	z27.d, p7/m, z27.d, #32
		lsr	z28.d, p7/m, z28.d, #32
	mad	z25.d, p7/m, z1.d, z21.d
		mad	z26.d, p7/m, z2.d, z22.d
		mad	z27.d, p7/m, z3.d, z23.d
		mad	z28.d, p7/m, z4.d, z24.d
	add	z17.d, p7/m, z17.d, z25.d
		add	z18.d, p7/m, z18.d, z26.d
		add	z19.d, p7/m, z19.d, z27.d
		add	z20.d, p7/m, z20.d, z28.d
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
	/* start scramble function */
/*
	SCRAM	z17.d, #0, MUL VL
	SCRAM	z18.d, #1, MUL VL
	SCRAM	z19.d, #2, MUL VL
	SCRAM	z20.d, #3, MUL VL
*/
	ldr	z21, [x1, #0, MUL VL]
		ldr	z22, [x1, #1, MUL VL]
		ldr	z23, [x1, #2, MUL VL]
		ldr	z24, [x1, #3, MUL VL]
	eor	z21.d, z17.d, z21.d
		eor	z22.d, z18.d, z22.d
		eor	z23.d, z19.d, z23.d
		eor	z24.d, z20.d, z24.d
	lsr	z25.d, z17.d, #47
		lsr	z26.d, z18.d, #47
		lsr	z27.d, z19.d, #47
		lsr	z28.d, z20.d, #47
	eor	z17.d, z21.d, z25.d
		eor	z18.d, z22.d, z26.d
		eor	z19.d, z23.d, z27.d
		eor	z20.d, z24.d, z28.d
	mul	z17.d, p7/m, z17.d, z16.d
		mul	z18.d, p7/m, z18.d, z16.d
		mul	z19.d, p7/m, z19.d, z16.d
		mul	z20.d, p7/m, z20.d, z16.d
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
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
	ACCRND	z19.d, #2, MUL VL
	ACCRND	z20.d, #3, MUL VL
*/
	ldr	z21, [x1, #0, MUL VL]
	ldr	z25, [x2, #0, MUL VL]
		ldr	z22, [x1, #1, MUL VL]
		ldr	z26, [x2, #1, MUL VL]
		ldr	z23, [x1, #2, MUL VL]
		ldr	z27, [x2, #2, MUL VL]
		ldr	z24, [x1, #3, MUL VL]
		ldr	z28, [x2, #3, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z25.d, z25.d, z21.d
		eor	z26.d, z26.d, z22.d
		eor	z27.d, z27.d, z23.d
		eor	z28.d, z28.d, z24.d
	ext	z21.b, z21.b, z21.b, 8
		ext	z22.b, z22.b, z22.b, 8
		ext	z23.b, z23.b, z23.b, 8
		ext	z24.b, z24.b, z24.b, 8
	uxtw	z1.d, p7/m, z25.d
		uxtw	z2.d, p7/m, z26.d
		uxtw	z3.d, p7/m, z27.d
		uxtw	z4.d, p7/m, z28.d
	lsr	z25.d, p7/m, z25.d, #32
		lsr	z26.d, p7/m, z26.d, #32
		lsr	z27.d, p7/m, z27.d, #32
		lsr	z28.d, p7/m, z28.d, #32
	mad	z25.d, p7/m, z1.d, z21.d
		mad	z26.d, p7/m, z2.d, z22.d
		mad	z27.d, p7/m, z3.d, z23.d
		mad	z28.d, p7/m, z4.d, z24.d
	add	z17.d, p7/m, z17.d, z25.d
		add	z18.d, p7/m, z18.d, z26.d
		add	z19.d, p7/m, z19.d, z27.d
		add	z20.d, p7/m, z20.d, z28.d
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
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
	ACCRND	z19.d, #2, MUL VL
	ACCRND	z20.d, #3, MUL VL
*/
	ldr	z21, [x1, #0, MUL VL]
	ldr	z25, [x2, #0, MUL VL]
		ldr	z22, [x1, #1, MUL VL]
		ldr	z26, [x2, #1, MUL VL]
		ldr	z23, [x1, #2, MUL VL]
		ldr	z27, [x2, #2, MUL VL]
		ldr	z24, [x1, #3, MUL VL]
		ldr	z28, [x2, #3, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z25.d, z25.d, z21.d
		eor	z26.d, z26.d, z22.d
		eor	z27.d, z27.d, z23.d
		eor	z28.d, z28.d, z24.d
	ext	z21.b, z21.b, z21.b, 8
		ext	z22.b, z22.b, z22.b, 8
		ext	z23.b, z23.b, z23.b, 8
		ext	z24.b, z24.b, z24.b, 8
	uxtw	z1.d, p7/m, z25.d
		uxtw	z2.d, p7/m, z26.d
		uxtw	z3.d, p7/m, z27.d
		uxtw	z4.d, p7/m, z28.d
	lsr	z25.d, p7/m, z25.d, #32
		lsr	z26.d, p7/m, z26.d, #32
		lsr	z27.d, p7/m, z27.d, #32
		lsr	z28.d, p7/m, z28.d, #32
	mad	z25.d, p7/m, z1.d, z21.d
		mad	z26.d, p7/m, z2.d, z22.d
		mad	z27.d, p7/m, z3.d, z23.d
		mad	z28.d, p7/m, z4.d, z24.d
	add	z17.d, p7/m, z17.d, z25.d
		add	z18.d, p7/m, z18.d, z26.d
		add	z19.d, p7/m, z19.d, z27.d
		add	z20.d, p7/m, z20.d, z28.d

	st1d	{z17.d}, p7, [x0]
	st1d	{z18.d}, p7, [x0, #1, MUL VL]
	st1d	{z19.d}, p7, [x0, #2, MUL VL]
	st1d	{z20.d}, p7, [x0, #3, MUL VL]

	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_sve128_internal_loop)

/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *                  x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit)
 * Output Registers: None
 * Modified Registers: p1, p7, z0-z7, z16-z18, x1-x4, x6-x17
 */
ENTRY (XXH3_sve256_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	mov	x29, sp
	ptrue	p7.d

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	LDR_PRM
	ld1d	{z17.d}, p7/z, [x0]
	ld1d	{z18.d}, p7/z, [x0, #1, MUL VL]
	mov	z16.d, x12	// save XXH3_PRIME_1

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	INIT_INDEX

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
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
*/
	ldr	z19, [x1, #0, MUL VL]
		ldr	z20, [x1, #1, MUL VL]
	ldr	z21, [x2, #0, MUL VL]
		ldr	z22, [x2, #1, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z21.d, z21.d, z19.d
		eor	z22.d, z22.d, z20.d
	tbl	z19.d, {z19.d}, z7.d
		tbl	z20.d, {z20.d}, z7.d
	uxtw	z23.d, p7/m, z21.d
		uxtw	z24.d, p7/m, z22.d
	lsr	z21.d, p7/m, z21.d, #32
		lsr	z22.d, p7/m, z22.d, #32
	mad	z21.d, p7/m, z23.d, z19.d
		mad	z22.d, p7/m, z24.d, z20.d
	add	z17.d, p7/m, z17.d, z21.d
		add	z18.d, p7/m, z18.d, z22.d

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
	/* start scramble function */
/*
	SCRAM	z17.d, #0, MUL VL
	SCRAM	z18.d, #1, MUL VL
*/
	ldr	z19, [x1, #0, MUL VL]
		ldr	z20, [x1, #1, MUL VL]
	eor	z19.d, z17.d, z19.d
		eor	z20.d, z18.d, z20.d
	lsr	z21.d, z17.d, #47
		lsr	z22.d, z18.d, #47
	eor	z17.d, z19.d, z21.d
		eor	z18.d, z20.d, z22.d
	mul	z17.d, p7/m, z17.d, z16.d
		mul	z18.d, p7/m, z18.d, z16.d
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
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
*/
	ldr	z19, [x1, #0, MUL VL]
		ldr	z20, [x1, #1, MUL VL]
	ldr	z21, [x2, #0, MUL VL]
		ldr	z22, [x2, #1, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z21.d, z21.d, z19.d
		eor	z22.d, z22.d, z20.d
	tbl	z19.d, {z19.d}, z7.d
		tbl	z20.d, {z20.d}, z7.d
	uxtw	z23.d, p7/m, z21.d
		uxtw	z24.d, p7/m, z22.d
	lsr	z21.d, p7/m, z21.d, #32
		lsr	z22.d, p7/m, z22.d, #32
	mad	z21.d, p7/m, z23.d, z19.d
		mad	z22.d, p7/m, z24.d, z20.d
	add	z17.d, p7/m, z17.d, z21.d
		add	z18.d, p7/m, z18.d, z22.d

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
/*
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ACCRND	z17.d, #0, MUL VL
	ACCRND	z18.d, #1, MUL VL
*/
	ldr	z19, [x1, #0, MUL VL]
		ldr	z20, [x1, #1, MUL VL]
	ldr	z21, [x2, #0, MUL VL]
		ldr	z22, [x2, #1, MUL VL]
	PRFD	PLDL1STRM, p7, [x1, #31, MUL VL]
	eor	z21.d, z21.d, z19.d
		eor	z22.d, z22.d, z20.d
	tbl	z19.d, {z19.d}, z7.d
		tbl	z20.d, {z20.d}, z7.d
	uxtw	z23.d, p7/m, z21.d
		uxtw	z24.d, p7/m, z22.d
	lsr	z21.d, p7/m, z21.d, #32
		lsr	z22.d, p7/m, z22.d, #32
	mad	z21.d, p7/m, z23.d, z19.d
		mad	z22.d, p7/m, z24.d, z20.d
	add	z17.d, p7/m, z17.d, z21.d
		add	z18.d, p7/m, z18.d, z22.d

	st1d	{z17.d}, p7, [x0]
	st1d	{z18.d}, p7, [x0, #1, MUL VL]

	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_sve256_internal_loop)

/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *                  x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit)
 * Output Registers: None
 * Modified Registers: p1, p7, z0-z7, z16, x1-x4, x6-x17
 */
ENTRY (XXH3_sve512_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	mov	x29, sp

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	LDR_PRM
	mov	z16.d, x12	// save XXH3_PRIME_1 into z16

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	ptrue	p7.d, VL8
	INIT_INDEX

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
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	/* start acc512 function */
	ACCRND	z0.d, #0, MUL VL
	// always store acc in z0
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
	/* start scramble function */
	SCRAM	z0.d, #0, MUL VL
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
	mov	x2, x15		// x2: secret
	sub	x7, x7, x9
	add	x1, x13, x9	// x1: input
	lsr	x3, x7, #6	// x3: nbStripes
	/* start accumulate function */
	mov	x10, xzr
	cmp	x10, x3
	b.ge	21f
20:
	/* start acc512 function */
	// z3 and z4 are initialized already
	ACCRND	z0.d, #0, MUL VL
	// always store acc in z0
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
	sub	x2, x2, #71	// 64 + 7
	/* start acc512 function */
	ACCRND	z0.d, #0, MUL VL
	// always store acc in z0
	/* end acc512 function */
	st1d	z0.d, p7, [x0]
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_sve512_internal_loop)

/*
 * Stack marking
 * ref: https://wiki.gentoo.org/wiki/Hardened/GNU_stack_quickstart
 */
#if defined(__GNUC__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
