
//.section .text
	.arch armv8-a+sve
#include "../asmdefs.h"

ENTRY(asvload_03)
	add	x0, x0, #3
	ret
END(asvload_03)

/*
 * Create sequence [1,0,3,2,...].
 */
.macro INIT_INDEX
	// set z7 to [ 1, 0, 3, 2, 5, 4, ... ] for tbl to swap adjacent lanes
	index   z7.d, #0, #1        // z7 = [ 0, 1, 2, 3, 4, 5... ]
	eor     z7.d, z7.d, #1      // z7 = [ 1, 0, 3, 2, 5, 4... ]
.endm

/*
 * Load the prime value into x12.
 */
.macro LDR_PRM
	movz	x12, #0x9E37, LSL #16
	mov	x10, #0x79B1
	orr	x12, x12, x10
.endm

	//sum *= 0x165667919E3779F9ULL;
.macro LDR_PRIME64
	movz	x12, #0x1656, LSL #48
	movz	x10, #0x6791, LSL #32
	movz	x7, #0x9E37, LSL #16
	mov	x6, #0x79F9
	orr	x12, x12, x10
	orr	x7, x7, x6
	orr	x12, x7, x12
.endm

ENTRY(asvmacc_01)
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
	pfalse	p6.b
	INIT_INDEX
	trn1	p5.d, p7.d, p6.d
	mov	w12, #0xffffffff
	// set -1 in z4
	mov	z3.d, x12
	mov	z4.d, #32

	ld1d	{z0.d}, p7/z, [x0]

	/* start macc */
	LDR_PRIME64
	ld1d	{z2.d}, p7/z, [x2]
	//mixed = sveor_u64_x(p1, xacc, xsec);
	eor	z2.d, p7/m, z2.d, z0.d
	//swapped = svtbl_u64(mixed, kSwap);
	tbl	z6.d, {z2.d}, z7.d
	//mov	z1.d, z6.d
	mov	z7.d, z6.d
	//mul_lo = svmul_u64_x(p2, mixed, swapped);
	mul	z6.d, p5/m, z6.d, z2.d
	//mul_hi = svmulh_u64_x(p2, mixed, swapped);
	umulh	z7.d, p5/m, z7.d, z2.d
	//mixed = sveor_u64_x(p2, mul_lo, mul_hi);
	eor	z7.d, p5/m, z7.d, z6.d
	//sum = svaddv_u64(p2, mixed);
	//assign to v0.d
	uaddv	d6, p5, z7.d
	/* end macc */
	st1d	z7.d, p5, [x0]
	umov	x6, v6.d[0]
	add	x6, x6, x3
	eor	x6, x6, x6, LSR #37
	mul	x6, x6, x12
	eor	x0, x6, x6, LSR #32
	ldp	x19, x20, [sp, #16]
	ldp	x21, x22, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
END(asvmacc_01)
