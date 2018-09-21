/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include "aes.h"
#include "aes-gcm.h"
#include "console.h"
#include "common.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Temporary buffer, to avoid using too much stack space. */
static uint8_t tmp[512];

static int test_aes_gcm_raw(const uint8_t *key, int key_size,
	const uint8_t *plaintext, const uint8_t *ciphertext, int plaintext_size,
	const uint8_t *nonce, int nonce_size,
	const uint8_t *tag, int tag_size) {

	uint8_t *out = tmp;
	static AES_KEY aes_key;
	static GCM128_CONTEXT ctx;

	TEST_ASSERT(plaintext_size <= sizeof(tmp));

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	CRYPTO_gcm128_encrypt(&ctx, &aes_key, plaintext, out, plaintext_size);
	TEST_ASSERT(CRYPTO_gcm128_finish(&ctx, tag, tag_size));
	TEST_ASSERT_ARRAY_EQ(ciphertext, out, plaintext_size);

	CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
	memset(out, 0, plaintext_size);
	CRYPTO_gcm128_decrypt(&ctx, &aes_key, ciphertext, out, plaintext_size);
	TEST_ASSERT(CRYPTO_gcm128_finish(&ctx, tag, tag_size));
	TEST_ASSERT_ARRAY_EQ(plaintext, out, plaintext_size);

	return EC_SUCCESS;
}

static int test_aes_gcm(void)
{
	/*
	 * Test vectors from BoringSSL crypto/fipsmodule/modes/gcm_tests.txt
	 * (only the ones with actual data, and no additional data).
	 */
	static const uint8_t key1[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t plain1[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t nonce1[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t cipher1[] = {
		0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
		0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78,
	};
	static const uint8_t tag1[] = {
		0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
		0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf,
	};

	static const uint8_t key2[] = {
		0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
		0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
	};
	static const uint8_t plain2[] = {
		0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
		0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
		0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
		0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
		0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
		0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
		0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
		0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55,
	};
	static const uint8_t nonce2[] = {
		0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
		0xde, 0xca, 0xf8, 0x88,
	};
	static const uint8_t cipher2[] = {
		0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24,
		0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4, 0x9c,
		0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
		0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e,
		0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c,
		0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
		0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97,
		0x3d, 0x58, 0xe0, 0x91, 0x47, 0x3f, 0x59, 0x85,
	};
	static const uint8_t tag2[] = {
		0x4d, 0x5c, 0x2a, 0xf3, 0x27, 0xcd, 0x64, 0xa6,
		0x2c, 0xf3, 0x5a, 0xbd, 0x2b, 0xa6, 0xfa, 0xb4,
	};

	static const uint8_t key3[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t plain3[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t nonce3[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t cipher3[] = {
		0x98, 0xe7, 0x24, 0x7c, 0x07, 0xf0, 0xfe, 0x41,
		0x1c, 0x26, 0x7e, 0x43, 0x84, 0xb0, 0xf6, 0x00,
	};
	static const uint8_t tag3[] = {
		0x2f, 0xf5, 0x8d, 0x80, 0x03, 0x39, 0x27, 0xab,
		0x8e, 0xf4, 0xd4, 0x58, 0x75, 0x14, 0xf0, 0xfb,
	};

	static const uint8_t key4[] = {
		0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
		0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
		0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
	};
	static const uint8_t plain4[] = {
		0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
		0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
		0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
		0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
		0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
		0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
		0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
		0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55,
	};
	static const uint8_t nonce4[] = {
		0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
		0xde, 0xca, 0xf8, 0x88,
	};
	static const uint8_t cipher4[] = {
		0x39, 0x80, 0xca, 0x0b, 0x3c, 0x00, 0xe8, 0x41,
		0xeb, 0x06, 0xfa, 0xc4, 0x87, 0x2a, 0x27, 0x57,
		0x85, 0x9e, 0x1c, 0xea, 0xa6, 0xef, 0xd9, 0x84,
		0x62, 0x85, 0x93, 0xb4, 0x0c, 0xa1, 0xe1, 0x9c,
		0x7d, 0x77, 0x3d, 0x00, 0xc1, 0x44, 0xc5, 0x25,
		0xac, 0x61, 0x9d, 0x18, 0xc8, 0x4a, 0x3f, 0x47,
		0x18, 0xe2, 0x44, 0x8b, 0x2f, 0xe3, 0x24, 0xd9,
		0xcc, 0xda, 0x27, 0x10, 0xac, 0xad, 0xe2, 0x56,
	};
	static const uint8_t tag4[] = {
		0x99, 0x24, 0xa7, 0xc8, 0x58, 0x73, 0x36, 0xbf,
		0xb1, 0x18, 0x02, 0x4d, 0xb8, 0x67, 0x4a, 0x14,
	};

	static const uint8_t key5[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t plain5[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t nonce5[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t cipher5[] = {
		0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e,
		0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18,
	};
	static const uint8_t tag5[] = {
		0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0,
		0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19,
	};

	static const uint8_t key6[] = {
		0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
		0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
		0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
		0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
	};
	static const uint8_t plain6[] = {
		0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
		0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
		0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
		0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
		0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
		0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
		0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
		0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55,
	};
	static const uint8_t nonce6[] = {
		0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
		0xde, 0xca, 0xf8, 0x88,
	};
	static const uint8_t cipher6[] = {
		0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07,
		0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
		0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9,
		0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
		0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d,
		0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
		0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a,
		0xbc, 0xc9, 0xf6, 0x62, 0x89, 0x80, 0x15, 0xad,
	};
	static const uint8_t tag6[] = {
		0xb0, 0x94, 0xda, 0xc5, 0xd9, 0x34, 0x71, 0xbd,
		0xec, 0x1a, 0x50, 0x22, 0x70, 0xe3, 0xcc, 0x6c,
	};

	static const uint8_t key7[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t plain7[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	/* This nonce results in 0xfff in counter LSB. */
	static const uint8_t nonce7[] = {
		0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	static const uint8_t cipher7[] = {
		0x56, 0xb3, 0x37, 0x3c, 0xa9, 0xef, 0x6e, 0x4a,
		0x2b, 0x64, 0xfe, 0x1e, 0x9a, 0x17, 0xb6, 0x14,
		0x25, 0xf1, 0x0d, 0x47, 0xa7, 0x5a, 0x5f, 0xce,
		0x13, 0xef, 0xc6, 0xbc, 0x78, 0x4a, 0xf2, 0x4f,
		0x41, 0x41, 0xbd, 0xd4, 0x8c, 0xf7, 0xc7, 0x70,
		0x88, 0x7a, 0xfd, 0x57, 0x3c, 0xca, 0x54, 0x18,
		0xa9, 0xae, 0xff, 0xcd, 0x7c, 0x5c, 0xed, 0xdf,
		0xc6, 0xa7, 0x83, 0x97, 0xb9, 0xa8, 0x5b, 0x49,
		0x9d, 0xa5, 0x58, 0x25, 0x72, 0x67, 0xca, 0xab,
		0x2a, 0xd0, 0xb2, 0x3c, 0xa4, 0x76, 0xa5, 0x3c,
		0xb1, 0x7f, 0xb4, 0x1c, 0x4b, 0x8b, 0x47, 0x5c,
		0xb4, 0xf3, 0xf7, 0x16, 0x50, 0x94, 0xc2, 0x29,
		0xc9, 0xe8, 0xc4, 0xdc, 0x0a, 0x2a, 0x5f, 0xf1,
		0x90, 0x3e, 0x50, 0x15, 0x11, 0x22, 0x13, 0x76,
		0xa1, 0xcd, 0xb8, 0x36, 0x4c, 0x50, 0x61, 0xa2,
		0x0c, 0xae, 0x74, 0xbc, 0x4a, 0xcd, 0x76, 0xce,
		0xb0, 0xab, 0xc9, 0xfd, 0x32, 0x17, 0xef, 0x9f,
		0x8c, 0x90, 0xbe, 0x40, 0x2d, 0xdf, 0x6d, 0x86,
		0x97, 0xf4, 0xf8, 0x80, 0xdf, 0xf1, 0x5b, 0xfb,
		0x7a, 0x6b, 0x28, 0x24, 0x1e, 0xc8, 0xfe, 0x18,
		0x3c, 0x2d, 0x59, 0xe3, 0xf9, 0xdf, 0xff, 0x65,
		0x3c, 0x71, 0x26, 0xf0, 0xac, 0xb9, 0xe6, 0x42,
		0x11, 0xf4, 0x2b, 0xae, 0x12, 0xaf, 0x46, 0x2b,
		0x10, 0x70, 0xbe, 0xf1, 0xab, 0x5e, 0x36, 0x06,
		0x87, 0x2c, 0xa1, 0x0d, 0xee, 0x15, 0xb3, 0x24,
		0x9b, 0x1a, 0x1b, 0x95, 0x8f, 0x23, 0x13, 0x4c,
		0x4b, 0xcc, 0xb7, 0xd0, 0x32, 0x00, 0xbc, 0xe4,
		0x20, 0xa2, 0xf8, 0xeb, 0x66, 0xdc, 0xf3, 0x64,
		0x4d, 0x14, 0x23, 0xc1, 0xb5, 0x69, 0x90, 0x03,
		0xc1, 0x3e, 0xce, 0xf4, 0xbf, 0x38, 0xa3, 0xb6,
		0x0e, 0xed, 0xc3, 0x40, 0x33, 0xba, 0xc1, 0x90,
		0x27, 0x83, 0xdc, 0x6d, 0x89, 0xe2, 0xe7, 0x74,
		0x18, 0x8a, 0x43, 0x9c, 0x7e, 0xbc, 0xc0, 0x67,
		0x2d, 0xbd, 0xa4, 0xdd, 0xcf, 0xb2, 0x79, 0x46,
		0x13, 0xb0, 0xbe, 0x41, 0x31, 0x5e, 0xf7, 0x78,
		0x70, 0x8a, 0x70, 0xee, 0x7d, 0x75, 0x16, 0x5c,
	};
	static const uint8_t tag7[] = {
		0x8b, 0x30, 0x7f, 0x6b, 0x33, 0x28, 0x6d, 0x0a,
		0xb0, 0x26, 0xa9, 0xed, 0x3f, 0xe1, 0xe8, 0x5f,
	};

	TEST_ASSERT(!test_aes_gcm_raw(key1, sizeof(key1),
			plain1, cipher1, sizeof(plain1),
			nonce1, sizeof(nonce1), tag1, sizeof(tag1)));
	TEST_ASSERT(!test_aes_gcm_raw(key2, sizeof(key2),
			plain2, cipher2, sizeof(plain2),
			nonce2, sizeof(nonce2), tag2, sizeof(tag2)));
	TEST_ASSERT(!test_aes_gcm_raw(key3, sizeof(key3),
			plain3, cipher3, sizeof(plain3),
			nonce3, sizeof(nonce3), tag3, sizeof(tag3)));
	TEST_ASSERT(!test_aes_gcm_raw(key4, sizeof(key4),
			plain4, cipher4, sizeof(plain4),
			nonce4, sizeof(nonce4), tag4, sizeof(tag4)));
	TEST_ASSERT(!test_aes_gcm_raw(key5, sizeof(key5),
			plain5, cipher5, sizeof(plain5),
			nonce5, sizeof(nonce5), tag5, sizeof(tag5)));
	TEST_ASSERT(!test_aes_gcm_raw(key6, sizeof(key6),
			plain6, cipher6, sizeof(plain6),
			nonce6, sizeof(nonce6), tag6, sizeof(tag6)));
	TEST_ASSERT(!test_aes_gcm_raw(key7, sizeof(key7),
			plain7, cipher7, sizeof(plain7),
			nonce7, sizeof(nonce7), tag7, sizeof(tag7)));

	return EC_SUCCESS;
}

static void test_aes_gcm_speed(void)
{
	int i;
	static const uint8_t key[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	const int key_size = sizeof(key);
	static const uint8_t plaintext[512] = { 0 };
	const int plaintext_size = sizeof(plaintext);
	static const uint8_t nonce[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	const int nonce_size = sizeof(nonce);
	uint8_t tag[16] = {0};
	const int tag_size = sizeof(tag);

	uint8_t *out = tmp;
	static AES_KEY aes_key;
	static GCM128_CONTEXT ctx;
	timestamp_t t0, t1;

	assert(plaintext_size <= sizeof(tmp));

	t0 = get_time();
	for (i = 0; i < 1000; i++) {
		AES_set_encrypt_key(key, 8 * key_size, &aes_key);
		CRYPTO_gcm128_init(&ctx, &aes_key, (block128_f)AES_encrypt, 0);
		CRYPTO_gcm128_setiv(&ctx, &aes_key, nonce, nonce_size);
		CRYPTO_gcm128_encrypt(&ctx, &aes_key, plaintext, out,
				plaintext_size);
		CRYPTO_gcm128_tag(&ctx, tag, tag_size);
	}
	t1 = get_time();
	ccprintf("AES-GCM duration %ld us\n", t1.val - t0.val);
}

static int test_aes_raw(const uint8_t *key, int key_size,
			const uint8_t *plaintext, const uint8_t *ciphertext)
{
	AES_KEY aes_key;
	uint8_t *block = tmp;

	TEST_ASSERT(AES_BLOCK_SIZE <= sizeof(tmp));

	TEST_ASSERT(AES_set_encrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test encryption. */
	AES_encrypt(plaintext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, AES_BLOCK_SIZE);

	/* Test in-place encryption. */
	memcpy(block, plaintext, AES_BLOCK_SIZE);
	AES_encrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(ciphertext, block, AES_BLOCK_SIZE);

	TEST_ASSERT(AES_set_decrypt_key(key, 8 * key_size, &aes_key) == 0);

	/* Test decryption. */
	AES_decrypt(ciphertext, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, AES_BLOCK_SIZE);

	/* Test in-place decryption. */
	memcpy(block, ciphertext, AES_BLOCK_SIZE);
	AES_decrypt(block, block, &aes_key);
	TEST_ASSERT_ARRAY_EQ(plaintext, block, AES_BLOCK_SIZE);

	return EC_SUCCESS;
}

static int test_aes(void)
{
	/* Test vectors from FIPS-197, Appendix C. */
	static const uint8_t key1[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	static const uint8_t plain1[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher1[] = {
		0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
		0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
	};

	static const uint8_t key2[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	};
	static const uint8_t plain2[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher2[] = {
		0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0,
		0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91,
	};

	static const uint8_t key3[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	};
	static const uint8_t plain3[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};
	static const uint8_t cipher3[] = {
		0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
		0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89,
	};

	TEST_ASSERT(!test_aes_raw(key1, sizeof(key1), plain1, cipher1));
	TEST_ASSERT(!test_aes_raw(key2, sizeof(key2), plain2, cipher2));
	TEST_ASSERT(!test_aes_raw(key3, sizeof(key3), plain3, cipher3));

	return EC_SUCCESS;
}

static void test_aes_speed(void)
{
	int i;
	/* Test vectors from FIPS-197, Appendix C. */
	static const uint8_t key[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};
	const int key_size = sizeof(key);
	static const uint8_t plaintext[] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
	};

	AES_KEY aes_key;
	uint8_t block[AES_BLOCK_SIZE];
	timestamp_t t0, t1;

	AES_set_encrypt_key(key, 8 * key_size, &aes_key);
	AES_encrypt(plaintext, block, &aes_key);
	t0 = get_time();
	for (i = 0; i < 1000; i++)
		AES_encrypt(block, block, &aes_key);
	t1 = get_time();
	ccprintf("AES duration %ld us\n", t1.val - t0.val);
}

void run_test(void)
{
	watchdog_reload();

	/* do not check result, just as a benchmark */
	test_aes_speed();

	watchdog_reload();
	RUN_TEST(test_aes);

	/* do not check result, just as a benchmark */
	test_aes_gcm_speed();

	watchdog_reload();
	RUN_TEST(test_aes_gcm);

	test_print_result();
}
