/*
 * xxHash - Extremely Fast Hash algorithm
 * Copyright (C) 2020-2021 Yann Collet
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

#if defined (__cplusplus)
extern "C" {
#endif

#if !defined(__aarch64__)  && !defined(__arm64__) && !defined(_M_ARM64) && \
    !defined(_M_ARM64EC) && !defined(XXH_ARM64DISPATCH)
#error "Dispatching is currently only supported on aarch64."
#endif

#if defined(XXH_DISPATCH_NEON) || defined(XXH_DISPATCH_SVE)
#error "Dispatch macro shouldn't be defined outside."
#endif

#define XXH_INLINE_ALL
#define XXH_ARM64DISPATCH
#include "xxhash.h"

#define XXH_SVE_CPUID_MASK                 (0xFUL << 32)
#define XXH_NEON_CPUID_MASK                (0xFUL << 20)

#if defined(__ARM_NEON__) || defined(__ARM_NEON) \
   || defined(__aarch64__)  || defined(_M_ARM) \
   || defined(_M_ARM64)     || defined(_M_ARM64EC)
#    if ((defined(__GNUC__) || defined(__clang__)) \
       && defined(__ARM_FEATURE_SVE))
#        define XXH_DISPATCH_SVE    1
#    else
#        define XXH_DISPATCH_SVE    0
#    endif
#endif

extern void
XXH3_accumulate_sveasm(xxh_u64* XXH_RESTRICT acc,
                       const xxh_u8* XXH_RESTRICT input,
                       const xxh_u8* XXH_RESTRICT secret,
                       size_t nbStripes);

/* ===   Vector implementations   === */

/*!
 * @internal
 * @brief Defines the various dispatch functions.
 *
 * @param suffix The suffix for the functions, e.g. neon or scalar
 */
#define XXH_DEFINE_DISPATCH_FUNCS(suffix)                                     \
                                                                              \
/* ===   XXH3, default variants   === */                                      \
                                                                              \
XXH_NO_INLINE XXH64_hash_t                                                    \
XXHL64_default_##suffix(const void* XXH_RESTRICT input, size_t len)           \
{                                                                             \
    return XXH3_hashLong_64b_internal(                                        \
               input, len, XXH3_kSecret, sizeof(XXH3_kSecret),                \
               XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix            \
    );                                                                        \
}                                                                             \
                                                                              \
/* ===   XXH3, Seeded variants   === */                                       \
                                                                              \
XXH_NO_INLINE XXH64_hash_t                                                    \
XXHL64_seed_##suffix(const void* XXH_RESTRICT input, size_t len,              \
                     XXH64_hash_t seed)                                       \
{                                                                             \
    return XXH3_hashLong_64b_withSeed_internal(                               \
                    input, len, seed, XXH3_accumulate_##suffix,               \
                    XXH3_scrambleAcc_##suffix, XXH3_initCustomSecret_##suffix \
    );                                                                        \
}                                                                             \
                                                                              \
/* ===   XXH3, Secret variants   === */                                       \
                                                                              \
XXH_NO_INLINE XXH64_hash_t                                                    \
XXHL64_secret_##suffix(const void* XXH_RESTRICT input, size_t len,            \
                       const void* secret, size_t secretLen)                  \
{                                                                             \
    return XXH3_hashLong_64b_internal(                                        \
                    input, len, secret, secretLen,                            \
                    XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix       \
    );                                                                        \
}                                                                             \
                                                                              \
/* ===   XXH3 update variants   === */                                        \
                                                                              \
XXH_NO_INLINE XXH_errorcode                                                   \
XXH3_update_##suffix(XXH3_state_t* state, const void* input, size_t len)      \
{                                                                             \
    return XXH3_update(state, (const xxh_u8*)input, len,                      \
                    XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix);     \
}                                                                             \
                                                                              \
/* ===   XXH128 default variants   === */                                     \
                                                                              \
XXH_NO_INLINE XXH128_hash_t                                                   \
XXHL128_default_##suffix(const void* XXH_RESTRICT input, size_t len)          \
{                                                                             \
    return XXH3_hashLong_128b_internal(                                       \
                    input, len, XXH3_kSecret, sizeof(XXH3_kSecret),           \
                    XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix       \
    );                                                                        \
}                                                                             \
                                                                              \
/* ===   XXH128 Secret variants   === */                                      \
                                                                              \
XXH_NO_INLINE XXH128_hash_t                                                   \
XXHL128_secret_##suffix(const void* XXH_RESTRICT input, size_t len,           \
                        const void* XXH_RESTRICT secret, size_t secretLen)    \
{                                                                             \
    return XXH3_hashLong_128b_internal(                                       \
                    input, len, (const xxh_u8*)secret, secretLen,             \
                    XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix);     \
}                                                                             \
                                                                              \
/* ===   XXH128 Seeded variants   === */                                      \
                                                                              \
XXH_NO_INLINE XXH128_hash_t                                                   \
XXHL128_seed_##suffix(const void* XXH_RESTRICT input, size_t len,             \
                      XXH64_hash_t seed)                                      \
{                                                                             \
    return XXH3_hashLong_128b_withSeed_internal(input, len, seed,             \
                    XXH3_accumulate_##suffix, XXH3_scrambleAcc_##suffix,      \
                    XXH3_initCustomSecret_##suffix);                          \
}

/* End XXH_DEFINE_DISPATCH_FUNCS */

#define XXH3_scrambleAcc_sveasm      XXH3_scrambleAcc_scalar
#define XXH3_initCustomSecret_sveasm XXH3_initCustomSecret_scalar
#define XXH3_initCustomSecret_neon   XXH3_initCustomSecret_scalar

XXH_DEFINE_DISPATCH_FUNCS(scalar)
XXH_DEFINE_DISPATCH_FUNCS(neon)
#if XXH_DISPATCH_SVE
XXH_DEFINE_DISPATCH_FUNCS(sveasm)
#endif

/* ====    Dispatchers    ==== */

typedef XXH64_hash_t (*XXH3_dispatcharm64_hashLong64_default)(const void* XXH_RESTRICT, size_t);

typedef XXH64_hash_t (*XXH3_dispatcharm64_hashLong64_withSeed)(const void* XXH_RESTRICT, size_t, XXH64_hash_t);

typedef XXH64_hash_t (*XXH3_dispatcharm64_hashLong64_withSecret)(const void* XXH_RESTRICT, size_t, const void* XXH_RESTRICT, size_t);

typedef XXH_errorcode (*XXH3_dispatcharm64_update)(XXH3_state_t*, const void*, size_t);

typedef struct {
    XXH3_dispatcharm64_hashLong64_default    hashLong64_default;
    XXH3_dispatcharm64_hashLong64_withSeed   hashLong64_seed;
    XXH3_dispatcharm64_hashLong64_withSecret hashLong64_secret;
    XXH3_dispatcharm64_update                update;
} XXH_dispatchFunctions_s;

#define XXH_NB_DISPATCHES 3

/*!
 * @internal
 * @brief Table of dispatchers for @ref XXH3_64bits().
 *
 * @pre The indices must match @ref XXH_VECTOR_TYPE.
 */
static const XXH_dispatchFunctions_s XXH_kDispatch[XXH_NB_DISPATCHES] = {
    /* Scalar */ { XXHL64_default_scalar,  XXHL64_seed_scalar,  XXHL64_secret_scalar,  XXH3_update_scalar },
    /* NEON   */ { XXHL64_default_neon,    XXHL64_seed_neon,    XXHL64_secret_neon,    XXH3_update_neon },
#if XXH_DISPATCH_SVE
    /* SVE    */ { XXHL64_default_sveasm,  XXHL64_seed_sveasm,  XXHL64_secret_sveasm,  XXH3_update_sveasm },
#else
                 { NULL,                   NULL,                NULL,                  NULL }
#endif
};

/*!
 * @internal
 * @brief The selected dispatch table for @ref XXH3_64bits().
 */
static XXH_dispatchFunctions_s XXH_g_dispatch = { NULL, NULL, NULL, NULL };


typedef XXH128_hash_t (*XXH3_dispatcharm64_hashLong128_default)(const void* XXH_RESTRICT, size_t);

typedef XXH128_hash_t (*XXH3_dispatcharm64_hashLong128_withSeed)(const void* XXH_RESTRICT, size_t, XXH64_hash_t);

typedef XXH128_hash_t (*XXH3_dispatcharm64_hashLong128_withSecret)(const void* XXH_RESTRICT, size_t, const void* XXH_RESTRICT, size_t);

typedef struct {
    XXH3_dispatcharm64_hashLong128_default    hashLong128_default;
    XXH3_dispatcharm64_hashLong128_withSeed   hashLong128_seed;
    XXH3_dispatcharm64_hashLong128_withSecret hashLong128_secret;
    XXH3_dispatcharm64_update                 update;
} XXH_dispatch128Functions_s;


/*!
 * @internal
 * @brief Table of dispatchers for @ref XXH3_128bits().
 *
 * @pre The indices must match @ref XXH_VECTOR_TYPE.
 */
static const XXH_dispatch128Functions_s XXH_kDispatch128[XXH_NB_DISPATCHES] = {
    /* Scalar */ { XXHL128_default_scalar,  XXHL128_seed_scalar,  XXHL128_secret_scalar,  XXH3_update_scalar },
    /* NEON   */ { XXHL128_default_neon,    XXHL128_seed_neon,    XXHL128_secret_neon,    XXH3_update_neon },
#if XXH_DISPATCH_SVE
    /* SVE    */ { XXHL128_default_sveasm,  XXHL128_seed_sveasm,  XXHL128_secret_sveasm,  XXH3_update_sveasm },
#else
                 { NULL,                    NULL,                 NULL,                   NULL }
#endif
};

/*!
 * @internal
 * @brief The selected dispatch table for @ref XXH3_64bits().
 */
static XXH_dispatch128Functions_s XXH_g_dispatch128 = { NULL, NULL, NULL, NULL };

/*!
 * @internal
 * @brief Runs a CPUID check and sets the correct dispatch tables.
 */
static void XXH_setDispatch(void)
{
	uint64_t cpuid;

	__asm__ __volatile__("mrs %0, ID_AA64PFR0_EL1" : "=r"(cpuid));
#if XXH_DISPATCH_SVE
	if (cpuid & XXH_SVE_CPUID_MASK) {
		XXH_g_dispatch = XXH_kDispatch[2];
		XXH_g_dispatch128 = XXH_kDispatch128[2];
		return;
	}
#endif
	if ((cpuid & XXH_NEON_CPUID_MASK) == 0) {
		XXH_g_dispatch = XXH_kDispatch[1];
		XXH_g_dispatch128 = XXH_kDispatch128[1];
		return;
	}
	XXH_g_dispatch = XXH_kDispatch[0];
	XXH_g_dispatch128 = XXH_kDispatch128[0];
	return;
}

/* ====    XXH3 public functions    ==== */

static XXH64_hash_t
XXH3_hashLong_64b_defaultSecret_selection(const void* input,
                                          size_t len,
                                          XXH64_hash_t seed64,
                                          const xxh_u8* secret,
                                          size_t secretLen)
{
	(void)seed64; (void)secret; (void)secretLen;
	if (XXH_g_dispatch.hashLong64_default == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch.hashLong64_default(input, len);
}

XXH64_hash_t XXH3_64bits_dispatch(const void* input, size_t len)
{
	return XXH3_64bits_internal(input, len, 0, XXH3_kSecret,
                                    sizeof(XXH3_kSecret),
                                    XXH3_hashLong_64b_defaultSecret_selection);
}

static XXH64_hash_t
XXH3_hashLong_64b_withSeed_selection(const void* input, size_t len,
                                     XXH64_hash_t seed64,
                                     const xxh_u8* secret, size_t secretLen)
{
	(void)secret; (void)secretLen;
	if (XXH_g_dispatch.hashLong64_seed == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch.hashLong64_seed(input, len, seed64);
}

XXH64_hash_t
XXH3_64bits_withSeed_dispatch(const void* input, size_t len, XXH64_hash_t seed)
{
	return XXH3_64bits_internal(input, len, seed, XXH3_kSecret,
                                    sizeof(XXH3_kSecret),
                                    XXH3_hashLong_64b_withSeed_selection);
}

static XXH64_hash_t
XXH3_hashLong_64b_withSecret_selection(const void* input, size_t len,
                                       XXH64_hash_t seed64,
                                       const xxh_u8* secret, size_t secretLen)
{
	(void)seed64;
	if (XXH_g_dispatch.hashLong64_secret == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch.hashLong64_secret(input, len, secret, secretLen);
}

XXH64_hash_t
XXH3_64bits_withSecret_dispatch(const void* input, size_t len,
                                const void* secret, size_t secretLen)
{
	return XXH3_64bits_internal(input, len, 0, secret, secretLen,
                                    XXH3_hashLong_64b_withSecret_selection);
}

XXH_errorcode
XXH3_64bits_update_dispatch(XXH3_state_t* state, const void* input, size_t len)
{
	if (XXH_g_dispatch.update == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch.update(state, (const xxh_u8*)input, len);
}


/* ====    XXH128 public functions    ==== */

static XXH128_hash_t
XXH3_hashLong_128b_defaultSecret_selection(const void* input, size_t len,
                                           XXH64_hash_t seed64,
                                           const void* secret, size_t secretLen)
{
	(void)seed64; (void)secret; (void)secretLen;
	if (XXH_g_dispatch128.hashLong128_default == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch128.hashLong128_default(input, len);
}

XXH128_hash_t
XXH3_128bits_dispatch(const void* input, size_t len)
{
	return XXH3_128bits_internal(input, len, 0, XXH3_kSecret,
                                     sizeof(XXH3_kSecret),
                                     XXH3_hashLong_128b_defaultSecret_selection);
}

static XXH128_hash_t
XXH3_hashLong_128b_withSeed_selection(const void* input, size_t len,
                                      XXH64_hash_t seed64,
                                      const void* secret, size_t secretLen)
{
	(void)secret; (void)secretLen;
	if (XXH_g_dispatch128.hashLong128_seed == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch128.hashLong128_seed(input, len, seed64);
}

XXH128_hash_t
XXH3_128bits_withSeed_dispatch(const void* input, size_t len, XXH64_hash_t seed)
{
	return XXH3_128bits_internal(input, len, seed, XXH3_kSecret,
                                     sizeof(XXH3_kSecret),
                                     XXH3_hashLong_128b_withSeed_selection);
}

static XXH128_hash_t
XXH3_hashLong_128b_withSecret_selection(const void* input, size_t len,
                                        XXH64_hash_t seed64,
                                        const void* secret, size_t secretLen)
{
	(void)seed64;
	if (XXH_g_dispatch128.hashLong128_secret == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch128.hashLong128_secret(input, len, secret,
                                                    secretLen);
}

XXH128_hash_t
XXH3_128bits_withSecret_dispatch(const void* input, size_t len,
                                 const void* secret, size_t secretLen)
{
	return XXH3_128bits_internal(input, len, 0, secret, secretLen,
                                     XXH3_hashLong_128b_withSecret_selection);
}

XXH_errorcode
XXH3_128bits_update_dispatch(XXH3_state_t* state, const void* input, size_t len)
{
	if (XXH_g_dispatch128.update == NULL)
		XXH_setDispatch();
	return XXH_g_dispatch128.update(state, (const xxh_u8*)input, len);
}

#if defined (__cplusplus)
}
#endif
/*! @} */
