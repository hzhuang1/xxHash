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
        cntd    x4
        cmp     x4, #2 // 128 bits exact
        b.eq    L(acc.sve128)
        cmp     x4, #8 // 256-384 bits
        b.lo    L(acc.sve256)
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

.section ".note.GNU-stack", "" // Ensures that this won't mark the stack as executable

