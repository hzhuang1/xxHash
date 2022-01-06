#include "asmdefs.h"

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
ENTRY (XXH3_aarch64_sve_accum512)
	mov	w10, #0x8
	whilelt	p0.d, wzr, w10
	ptrue	p1.d
	//b.pl	??
	whilelo	p2.d, xzr, xzr
	mov	w11, #0xffffffff
	rdvl	x12, #1
	mov	z3.d, #2
END (XXH3_aarch64_sve_accum512)
*/


ENTRY (XXH3_aarch64_accumulate)
	#loop
	mov	x9, #0
.LaccumLoop:
	# call accumulate
END (XXH3_aarch64_accumulate)

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
 * Calculate index [1,0,3,2,...] & save it in register z7.
 * Input Registers: none
 * Output Registers: z7 (index), p7 (true for D)
 * Modified Registers: p1, p7, z1, z7
 */
ENTRY (XXH3_aarch64_sve_init_accum)
	ptrue	p7.d
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
ENTRY (XXH3_aarch64_sve_accum512)
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
END (XXH3_aarch64_sve_accum512)

#XXH3_aarch64_consumeStripes(xxh_u64* XXH_RESTRICT acc, size_t* XXH_RESTRICT nbStripeSoFarPtr, size_t nbStripesPerBlock, const xxh_u8* XXH_RESTRICT input, size_t nbScripes, const xxh_u8* XXH_RESTRICT secret, size_t secretLimit)
