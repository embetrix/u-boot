// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ML-DSA verification tests
 *
 * Copyright 2025 Google LLC
 *
 * Based on Linux lib/crypto/tests/mldsa_kunit.c
 * at commit:
 * https://git.kernel.org/linus/ed894faccb8de55cd755e093c4b0971f190d384d
 *
 * Ported to U-Boot by:
 * Ayoub Zaki <ayoub.zaki@embetrix.com>
 */

#include <command.h>
#include <asm/unaligned.h>
#include <crypto/mldsa.h>
#include <crypto/sha3.h>
#include <malloc.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>

#include "mldsa-testvecs.h"

#define Q 8380417

static const struct {
	int sig_len;
	int pk_len;
	int k;
	int lambda;
	int gamma1;
	int beta;
	int omega;
} params[] = {
	[MLDSA44] = {
		.sig_len = MLDSA44_SIGNATURE_SIZE,
		.pk_len = MLDSA44_PUBLIC_KEY_SIZE,
		.k = 4,
		.lambda = 128,
		.gamma1 = 1 << 17,
		.beta = 78,
		.omega = 80,
	},
	[MLDSA65] = {
		.sig_len = MLDSA65_SIGNATURE_SIZE,
		.pk_len = MLDSA65_PUBLIC_KEY_SIZE,
		.k = 6,
		.lambda = 192,
		.gamma1 = 1 << 19,
		.beta = 196,
		.omega = 55,
	},
	[MLDSA87] = {
		.sig_len = MLDSA87_SIGNATURE_SIZE,
		.pk_len = MLDSA87_PUBLIC_KEY_SIZE,
		.k = 8,
		.lambda = 256,
		.gamma1 = 1 << 19,
		.beta = 120,
		.omega = 75,
	},
};

static int lib_shake(struct unit_test_state *uts)
{
	static const u8 shake128_empty[32] = {
		0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d,
		0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e,
		0xd7, 0x3b, 0x80, 0x93, 0xf6, 0xef, 0xbc, 0x88,
		0xeb, 0x1a, 0x6e, 0xac, 0xfa, 0x66, 0xef, 0x26,
	};
	static const u8 shake256_empty[64] = {
		0x46, 0xb9, 0xdd, 0x2b, 0x0b, 0xa8, 0x8d, 0x13,
		0x23, 0x3b, 0x3f, 0xeb, 0x74, 0x3e, 0xeb, 0x24,
		0x3f, 0xcd, 0x52, 0xea, 0x62, 0xb8, 0x1b, 0x82,
		0xb5, 0x0c, 0x27, 0x64, 0x6e, 0xd5, 0x76, 0x2f,
		0xd7, 0x5d, 0xc4, 0xdd, 0xd8, 0xc0, 0xf2, 0x00,
		0xcb, 0x05, 0x01, 0x9d, 0x67, 0xb5, 0x92, 0xf6,
		0xfc, 0x82, 0x1c, 0x49, 0x47, 0x9a, 0xb4, 0x86,
		0x40, 0x29, 0x2e, 0xac, 0xb3, 0xb7, 0xc4, 0xbe,
	};
	struct shake_ctx ctx;
	u8 out[64];

	shake128(NULL, 0, out, sizeof(shake128_empty));
	ut_asserteq_mem(shake128_empty, out, sizeof(shake128_empty));
	shake256_init(&ctx);
	shake_update(&ctx, NULL, 0);
	shake_squeeze(&ctx, out, 17);
	shake_squeeze(&ctx, out + 17, sizeof(out) - 17);
	ut_asserteq_mem(shake256_empty, out, sizeof(shake256_empty));

	return 0;
}

LIB_TEST(lib_shake, 0);

static int test_mldsa_z_range(struct unit_test_state *uts,
			      const struct mldsa_testvector *tv)
{
	u8 *sig = memdup(tv->sig, tv->sig_len);
	const int lambda = params[tv->alg].lambda;
	const s32 gamma1 = params[tv->alg].gamma1;
	const int beta = params[tv->alg].beta;
	u8 *z_ptr;
	u32 z_data;
	u32 mask;
	const s32 out_of_range_coeffs[] = {
		-gamma1 + 1,
		-(gamma1 - beta),
		gamma1,
		gamma1 - beta,
	};
	const s32 in_range_coeffs[] = {
		-(gamma1 - beta - 1),
		0,
		gamma1 - beta - 1,
	};

	ut_assertnonnull(sig);
	z_ptr = &sig[lambda / 4];
	z_data = get_unaligned_le32(z_ptr);
	mask = (gamma1 << 1) - 1;

	for (int i = 0; i < ARRAY_SIZE(out_of_range_coeffs); i++) {
		const s32 c = out_of_range_coeffs[i];

		put_unaligned_le32((z_data & ~mask) | (mask & (gamma1 - c)),
				   z_ptr);
		ut_asserteq(-EBADMSG,
			    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
					 tv->msg_len, tv->pk, tv->pk_len));
	}

	for (int i = 0; i < ARRAY_SIZE(in_range_coeffs); i++) {
		const s32 c = in_range_coeffs[i];

		put_unaligned_le32((z_data & ~mask) | (mask & (gamma1 - c)),
				   z_ptr);
		ut_asserteq(-EKEYREJECTED,
			    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
					 tv->msg_len, tv->pk, tv->pk_len));
	}

	free(sig);
	return 0;
}

static int test_mldsa_bad_hints(struct unit_test_state *uts,
				const struct mldsa_testvector *tv)
{
	const int omega = params[tv->alg].omega;
	const int k = params[tv->alg].k;
	u8 *sig = memdup(tv->sig, tv->sig_len);
	u8 *hintvec;
	u8 h;

	ut_assertnonnull(sig);
	hintvec = &sig[tv->sig_len - omega - k];

	/* Cumulative hint count exceeds omega. */
	hintvec[omega + k - 1] = omega + 1;
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
				 tv->msg_len, tv->pk, tv->pk_len));

	/* Cumulative hint count decreases. */
	memcpy(sig, tv->sig, tv->sig_len);
	ut_assert(hintvec[omega + k - 2] >= 1);
	hintvec[omega + k - 1] = hintvec[omega + k - 2] - 1;
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
				 tv->msg_len, tv->pk, tv->pk_len));

	/* Hint indices are out of order. */
	memcpy(sig, tv->sig, tv->sig_len);
	ut_assert(hintvec[omega] >= 2);
	h = hintvec[0];
	hintvec[0] = hintvec[1];
	hintvec[1] = h;
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
				 tv->msg_len, tv->pk, tv->pk_len));

	/* An unused hint index must be zero. */
	memcpy(sig, tv->sig, tv->sig_len);
	ut_assert(hintvec[omega + k - 1] < omega);
	hintvec[omega - 1] = 0xff;
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, sig, tv->sig_len, tv->msg,
				 tv->msg_len, tv->pk, tv->pk_len));

	free(sig);
	return 0;
}

static u32 test_rand(u32 *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;

	return *state;
}

static int test_mldsa_mutation(struct unit_test_state *uts,
			       const struct mldsa_testvector *tv)
{
	const int num_iter = 200;
	u8 *sig = memdup(tv->sig, tv->sig_len);
	u8 *msg = memdup(tv->msg, tv->msg_len);
	u8 *pk = memdup(tv->pk, tv->pk_len);
	u32 rand_state = tv->alg + 1;

	ut_assertnonnull(sig);
	ut_assertnonnull(msg);
	ut_assertnonnull(pk);

	for (int i = 0; i < num_iter; i++) {
		size_t pos = test_rand(&rand_state) % tv->sig_len;
		u8 bit = 1 << (test_rand(&rand_state) % 8);

		sig[pos] ^= bit;
		ut_assert(mldsa_verify(tv->alg, sig, tv->sig_len, msg,
				       tv->msg_len, pk, tv->pk_len) != 0);
		sig[pos] ^= bit;
	}

	for (int i = 0; i < num_iter; i++) {
		size_t pos = test_rand(&rand_state) % tv->msg_len;
		u8 bit = 1 << (test_rand(&rand_state) % 8);

		msg[pos] ^= bit;
		ut_assert(mldsa_verify(tv->alg, sig, tv->sig_len, msg,
				       tv->msg_len, pk, tv->pk_len) != 0);
		msg[pos] ^= bit;
	}

	for (int i = 0; i < num_iter; i++) {
		size_t pos = test_rand(&rand_state) % tv->pk_len;
		u8 bit = 1 << (test_rand(&rand_state) % 8);

		pk[pos] ^= bit;
		ut_assert(mldsa_verify(tv->alg, sig, tv->sig_len, msg,
				       tv->msg_len, pk, tv->pk_len) != 0);
		pk[pos] ^= bit;
	}

	ut_asserteq(0, mldsa_verify(tv->alg, sig, tv->sig_len, msg,
				    tv->msg_len, pk, tv->pk_len));
	free(pk);
	free(msg);
	free(sig);
	return 0;
}

static int test_mldsa(struct unit_test_state *uts,
		      const struct mldsa_testvector *tv)
{
	ut_asserteq(params[tv->alg].sig_len, tv->sig_len);
	ut_asserteq(params[tv->alg].pk_len, tv->pk_len);
	ut_asserteq(0, mldsa_verify(tv->alg, tv->sig, tv->sig_len,
				    tv->msg, tv->msg_len, tv->pk, tv->pk_len));
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, tv->sig, tv->sig_len - 1,
				 tv->msg, tv->msg_len, tv->pk, tv->pk_len));
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, tv->sig, tv->sig_len + 1,
				 tv->msg, tv->msg_len, tv->pk, tv->pk_len));
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, tv->sig, tv->sig_len,
				 tv->msg, tv->msg_len, tv->pk, tv->pk_len - 1));
	ut_asserteq(-EBADMSG,
		    mldsa_verify(tv->alg, tv->sig, tv->sig_len,
				 tv->msg, tv->msg_len, tv->pk, tv->pk_len + 1));
	ut_asserteq(-EKEYREJECTED,
		    mldsa_verify(tv->alg, tv->sig, tv->sig_len,
				 tv->msg, tv->msg_len - 1, tv->pk, tv->pk_len));
	ut_assertok(test_mldsa_z_range(uts, tv));
	ut_assertok(test_mldsa_bad_hints(uts, tv));
	ut_assertok(test_mldsa_mutation(uts, tv));

	return 0;
}

static int lib_mldsa44(struct unit_test_state *uts)
{
	return test_mldsa(uts, &mldsa44_testvector);
}

LIB_TEST(lib_mldsa44, 0);

static int lib_mldsa65(struct unit_test_state *uts)
{
	return test_mldsa(uts, &mldsa65_testvector);
}

LIB_TEST(lib_mldsa65, 0);

static int lib_mldsa87(struct unit_test_state *uts)
{
	return test_mldsa(uts, &mldsa87_testvector);
}

LIB_TEST(lib_mldsa87, 0);

static int lib_mldsa_invalid_alg(struct unit_test_state *uts)
{
	const struct mldsa_testvector *tv = &mldsa44_testvector;

	ut_asserteq(-EINVAL,
		    mldsa_verify(MLDSA87 + 1, tv->sig, tv->sig_len,
				 tv->msg, tv->msg_len, tv->pk, tv->pk_len));

	return 0;
}

LIB_TEST(lib_mldsa_invalid_alg, 0);

static s32 mod(s32 a, s32 m)
{
	a %= m;
	if (a < 0)
		a += m;

	return a;
}

static s32 symmetric_mod(s32 a, s32 m)
{
	a = mod(a, m);
	if (a > m / 2)
		a -= m;

	return a;
}

static void decompose_ref(s32 r, s32 gamma2, s32 *r0, s32 *r1)
{
	s32 rplus = mod(r, Q);

	*r0 = symmetric_mod(rplus, 2 * gamma2);
	if (rplus - *r0 == Q - 1) {
		*r1 = 0;
		*r0 -= 1;
	} else {
		*r1 = (rplus - *r0) / (2 * gamma2);
	}
}

static s32 use_hint_ref(u8 h, s32 r, s32 gamma2)
{
	s32 m = (Q - 1) / (2 * gamma2);
	s32 r0;
	s32 r1;

	decompose_ref(r, gamma2, &r0, &r1);
	if (h == 1 && r0 > 0)
		return mod(r1 + 1, m);
	if (h == 1 && r0 <= 0)
		return mod(r1 - 1, m);

	return r1;
}

static int lib_mldsa_use_hint(struct unit_test_state *uts)
{
	for (int i = 0; i < 2; i++) {
		const s32 gamma2 = (Q - 1) / (i == 0 ? 88 : 32);

		for (u8 h = 0; h < 2; h++) {
			for (s32 r = 0; r < Q; r++)
				ut_asserteq(use_hint_ref(h, r, gamma2),
					    mldsa_use_hint(h, r, gamma2));
		}
	}

	return 0;
}

LIB_TEST(lib_mldsa_use_hint, 0);
