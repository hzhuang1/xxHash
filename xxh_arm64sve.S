	.arch armv8-a+sve
#include "asmdefs.h"

#define PRFO		128	/* prefetch offset */

	// Since SVE support in C compilers is fairly new and not very optimized, the SVE routines
	// are written in assembly.

	/// Perform a single round of XXH_accumulate_512().
	///     \acc = XXH3_accumulate_512_round(\acc, LDR(x1, \memoffs), LDR(x2, \memoffs))
	/// The code is specialized to the various SVE vector sizes to avoid loading from memory.
.macro ACCRND acc, memoffs:vararg
	// load input
	ld1d	{z4.d}, p7/z, [x1, \memoffs]
	// load secret
	ld1d	{z5.d}, p7/z, [x2, \memoffs]
	// mixed = secret EOR input
	eor	z5.d, p7/m, z5.d, z4.d
	// swapped = SWAP(input)
	tbl	z4.d, {z4.d}, z7.d
	// mixed_lo = mixed AND 0xffffffff (technically (u64)(u32) mixed)
	uxtw	z6.d, p7/m, z5.d
	// mixed_hi = mixed >> 32
	lsr	z5.d, p7/m, z5.d, #32
	// mixed_lo = mixed_hi * mixed_lo + swapped (distributive property)
	mad	z5.d, p7/m, z6.d, z4.d
	// acc += mixed_lo
	add	\acc, p7/m, \acc, z5.d
.endm
        .text
        /// Compatible with XXH3_accumulate()
        /// void XXH3_accumulate_sve_acc(
        ///     xxh_u64 *XXH_RESTRICT acc,          // x0
        ///     const xxh_u8 *XXH_RESTRICT input,   // x1
        ///     const xxh_u8 *XXH_RESTRICT secret,  // x2
        ///     size_t nbBlocks,                    // x3
        ///     XXH3_f_accumulate_512 {ignored}     // x4
        /// );
        /// Clobbers x0-x4, z0-z7, p7
        /// TODO: XXH_NAMESPACE
ENTRY (XXH3_accumulate_sveasm)
        // if nbBlocks is 0, return
        cbz     x3, L(acc.ret)
        // set z7 to [ 1, 0, 3, 2, 5, 4, ... ] for tbl to swap adjacent lanes
        index   z7.d, #0, #1        // z7 = [ 0, 1, 2, 3, 4, 5... ]
        eor     z7.d, z7.d, #1      // z7 = [ 1, 0, 3, 2, 5, 4... ]
        // Determine the SVE vector size so the loop can be unrolled.
        //cntd    x4
        //cmp     x4, #2 // 128 bits exact
        //b.eq    L(acc.sve128)
        //cmp     x4, #8 // 256-384 bits
        //b.lo    L(acc.sve256)
        // FALLTHROUGH: 512+ bits
        // SVE512 and larger (e.g. Fujitsu A64FX)
        // This is the simplest version as it only requires one iteration per stripe.
L(acc.sve512):
        // Limit to 512 bits.
        ptrue   p7.d, VL8
        // Load accumulators into z0
        ld1d    {z0.d}, p7/z, [x0]      // svuint64_t xacc = read512(acc)
1:                                      // do {
        prfm    pldl1strm, [x1, #PRFO]  //     XXH_PREFETCH(input + PRFO)
        ACCRND  z0.d, #0, MUL VL // 0   //     xacc = XXH3_accumulate_512(...)
        add     x1, x1, #64             //     input += XXH3_STRIPE_LEN
        add     x2, x2, #8              //     secret += XXH3_SECRET_CONSUME_RATE
        subs    x3, x3, #1              //     nbBlocks--
        b.ne    1b                      // } while (nbBlocks != 0)
2:
        // Store back
        st1d    {z0.d}, p7, [x0]        // write512(acc, xacc)
L(acc.ret): // reuse this ret for the zero check above
        ret

        // SVE128 (e.g. Cortex-X2)
        // Pretty much the same code as before but it stores the accumulator in multiple
        // registers to avoid reloads from memory.
L(acc.sve128):
        ptrue   p7.d
        // Load accumulators into z0-z3.
        ld1d    {z0.d}, p7/z, [x0]
        ld1d    {z1.d}, p7/z, [x0, #1, MUL VL]
        ld1d    {z2.d}, p7/z, [x0, #2, MUL VL]
        ld1d    {z3.d}, p7/z, [x0, #3, MUL VL]
1:
        prfm    pldl1strm, [x1, #PRFO]
        // Perform rounds on each of the accumulators
        ACCRND  z0.d, #0, MUL VL // 0
        ACCRND  z1.d, #1, MUL VL // 16
        ACCRND  z2.d, #2, MUL VL // 32
        ACCRND  z3.d, #3, MUL VL // 48
        add     x1, x1, #64
        add     x2, x2, #8
        subs    x3, x3, #1
        b.ne    1b
2:
        // Store back
        st1d    {z0.d}, p7, [x0]
        st1d    {z1.d}, p7, [x0, #1, MUL VL]
        st1d    {z2.d}, p7, [x0, #2, MUL VL]
        st1d    {z3.d}, p7, [x0, #3, MUL VL]
        ret

        // SVE256 and SVE384
        // It is unlikely that anyone will use SVE384 in practice, but this codepath is made compatible anyways.
L(acc.sve256):
        // Limit the vector size to 256 bits
        ptrue   p7.d, VL4
        // Don't use MUL VL, instead force it to a 32 byte offset
        mov     w4, #32 >> 3
        // Load accumulators into z0-z1.
        ld1d    {z0.d}, p7/z, [x0]
        ld1d    {z1.d}, p7/z, [x0, x4, LSL #3]
1:
        prfm    pldl1strm, [x1, #PRFO]
        // Perform rounds on each of the accumulators
        ACCRND  z0.d, #0, MUL VL // 0
        ACCRND  z1.d, x4, LSL #3 // 32
        add     x1, x1, #64
        add     x2, x2, #8
        subs    x3, x3, #1
        b.ne    1b
2:
        // Store back
        st1d    {z0.d}, p7, [x0]
        st1d    {z1.d}, p7, [x0, x4, LSL #3]
        ret
END (XXH3_accumulate_sveasm)

.macro init_index
	mov	z1.d, #2
	pfalse	p1.b
	index	z7.d, #1, #1
	trn1	p1.d, p1.d, p7.d
	sub	z7.d, p1/m, z7.d, z1.d
.endm

.macro calc_acc512 acc
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
	mad	z1.d, p7/m, z6.d, z5.d
	// acc = mixed_lo + swapped
	add	\acc, p7/m, \acc, z1.d
.endm

.macro calc_scram acc
	eor	z1.d, \acc, z1.d
	lsr	z5.d, \acc, #47
	eor	\acc, z1.d, z5.d
	mul	\acc, p7/m, \acc, z16.d
.endm

.macro ldr_prime
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
ENTRY (XXH3_aarch64_sve128_internal_loop)
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
	ldr_prime
	ld1d	{z17.d}, p7/z, [x0]
	ld1d	{z18.d}, p7/z, [x0, #1, MUL VL]
	ld1d	{z19.d}, p7/z, [x0, #2, MUL VL]
	ld1d	{z20.d}, p7/z, [x0, #3, MUL VL]
	mov	z16.d, x12			// save XXH3_PRIME_1

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	init_index

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
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
	calc_acc512 z19.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
	calc_acc512 z20.d
	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:

	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	ld1d	{z1.d}, p7/z, [x1]
	eor	z1.d, z17.d, z1.d
	lsr	z2.d, z17.d, #47
	eor	z17.d, z1.d, z2.d
	mul	z17.d, p7/m, z17.d, z16.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	eor	z1.d, z18.d, z1.d
	lsr	z2.d, z18.d, #47
	eor	z18.d, z1.d, z2.d
	mul	z18.d, p7/m, z18.d, z16.d
	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	eor	z1.d, z19.d, z1.d
	lsr	z2.d, z19.d, #47
	eor	z19.d, z1.d, z2.d
	mul	z19.d, p7/m, z19.d, z16.d
	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	eor	z1.d, z20.d, z1.d
	lsr	z2.d, z20.d, #47
	eor	z20.d, z1.d, z2.d
	mul	z20.d, p7/m, z20.d, z16.d
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
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
	calc_acc512 z19.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
	calc_acc512 z20.d
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
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

	ld1d	{z1.d}, p7/z, [x1, #2, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #2, MUL VL]
	calc_acc512 z19.d

	ld1d	{z1.d}, p7/z, [x1, #3, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #3, MUL VL]
	calc_acc512 z20.d

	st1d	{z17.d}, p7, [x0]
	st1d	{z18.d}, p7, [x0, #1, MUL VL]
	st1d	{z19.d}, p7, [x0, #2, MUL VL]
	st1d	{z20.d}, p7, [x0, #3, MUL VL]

	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve128_internal_loop)

/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *                  x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit)
 * Output Registers: None
 * Modified Registers: p1, p7, z0-z7, z16-z18, x1-x4, x6-x17
 */
ENTRY (XXH3_aarch64_sve256_internal_loop)
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
	ldr_prime
	ld1d	{z17.d}, p7/z, [x0]
	ld1d	{z18.d}, p7/z, [x0, #1, MUL VL]
	mov	z16.d, x12	// save XXH3_PRIME_1

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	init_index

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
	mov	x10, xzr
	cmp	x10, x3
	b.ge	11f
10:
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

	add	x1, x1, #64
	add	x2, x2, #8
	add	x10, x10, #1
	cmp	x10, x3
	b.lt	10b
11:

	/* secret + secretSize - XXH_STRIPE_LEN */
	add	x1, x15, x4
	sub	x1, x1, #64
	ld1d	{z1.d}, p7/z, [x1]
	calc_scram	z17.d
	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	calc_scram	z18.d
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
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

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
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z17.d

	ld1d	{z1.d}, p7/z, [x1, #1, MUL VL]
	ld1d	{z2.d}, p7/z, [x2, #1, MUL VL]
	calc_acc512 z18.d

	st1d	{z17.d}, p7, [x0]
	st1d	{z18.d}, p7, [x0, #1, MUL VL]

	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve256_internal_loop)

/*
 * Input Registers: x0 (acc), x1 (nbStripeSoFarPtr), x2 (nbStripesPerBlock),
 *                  x3 (input), x4 (nbStripes), x5 (secret), x6 (secretLimit)
 * Output Registers: None
 * Modified Registers: p1, p7, z0-z7, z16, x1-x4, x6-x17
 */
ENTRY (XXH3_aarch64_sve512_internal_loop)
	stp	x29, x30, [sp, #-48]!
	stp	x19, x20, [sp, #16]
	stp	x21, x22, [sp, #32]
	mov	x29, sp

	/* save input parameters */
	mov	x13, x1		// save input into x13
	mov	x14, x2		// save len into x14
	mov	x15, x3		// save secret into x15
	mov	x19, x4		// save secretSize into x19
	ldr_prime
	mov	z16.d, x12	// save XXH3_PRIME_1 into z16

	/*
	 * prepare index
	 * p7 and z7 are reserved to use.
	 */
	ptrue	p7.d, VL8
	init_index

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
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z0.d
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
	ld1d	{z1.d}, p7/z, [x1]
	calc_scram	z0.d
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
	/* start acc512 function */
	// z3 and z4 are initialized already
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z0.d
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
	sub	x2, x2, #64
	sub	x2, x2, #7
	/* start acc512 function */
	ld1d	{z1.d}, p7/z, [x1]
	ld1d	{z2.d}, p7/z, [x2]
	calc_acc512 z0.d
	// always store acc in z0
	/* end acc512 function */
	st1d	z0.d, p7, [x0]
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END (XXH3_aarch64_sve512_internal_loop)

.section ".note.GNU-stack", "" // Ensures that this won't mark the stack as executable

