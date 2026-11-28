#include "core/sha256.h"

#include <string.h>

static const uint32_t sha256_k[64] = {
	0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
	0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
	0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
	0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
	0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
	0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
	0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
	0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
	0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
	0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
	0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
	0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
	0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
	0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
	0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
	0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

typedef struct {
	uint32_t h[8];
	uint8_t block[64];
	size_t block_len;
	uint64_t total;
} sha256_ctx_t;

static uint32_t sha256_rotr(uint32_t value, int bits) {
	return (value >> bits) | (value << (32 - bits));
}

static void sha256_transform(sha256_ctx_t* ctx, const uint8_t block[64]) {
	uint32_t words[64];

	for (int i = 0; i < 16; i++) {
		words[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
	}

	for (int i = 16; i < 64; i++) {
		uint32_t s0 = sha256_rotr(words[i - 15], 7) ^ sha256_rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
		uint32_t s1 = sha256_rotr(words[i - 2], 17) ^ sha256_rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
		words[i] = words[i - 16] + s0 + words[i - 7] + s1;
	}

	uint32_t a = ctx->h[0];
	uint32_t b = ctx->h[1];
	uint32_t c = ctx->h[2];
	uint32_t d = ctx->h[3];
	uint32_t e = ctx->h[4];
	uint32_t f = ctx->h[5];
	uint32_t g = ctx->h[6];
	uint32_t h = ctx->h[7];

	for (int i = 0; i < 64; i++) {
		uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
		uint32_t choice = (e & f) ^ ((~e) & g);
		uint32_t t1 = h + s1 + choice + sha256_k[i] + words[i];
		uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx->h[0] += a;
	ctx->h[1] += b;
	ctx->h[2] += c;
	ctx->h[3] += d;
	ctx->h[4] += e;
	ctx->h[5] += f;
	ctx->h[6] += g;
	ctx->h[7] += h;
}

static void sha256_init(sha256_ctx_t* ctx) {
	ctx->h[0] = 0x6A09E667u;
	ctx->h[1] = 0xBB67AE85u;
	ctx->h[2] = 0x3C6EF372u;
	ctx->h[3] = 0xA54FF53Au;
	ctx->h[4] = 0x510E527Fu;
	ctx->h[5] = 0x9B05688Cu;
	ctx->h[6] = 0x1F83D9ABu;
	ctx->h[7] = 0x5BE0CD19u;
	ctx->block_len = 0;
	ctx->total = 0;
}

static void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t size) {
	ctx->total += (uint64_t)size;

	while (size > 0) {
		size_t take = 64 - ctx->block_len;
		if (take > size) { take = size; }

		memcpy(ctx->block + ctx->block_len, data, take);
		ctx->block_len += take;
		data += take;
		size -= take;

		if (ctx->block_len == 64) {
			sha256_transform(ctx, ctx->block);
			ctx->block_len = 0;
		}
	}
}

static void sha256_final(sha256_ctx_t* ctx, uint8_t out[32]) {
	uint64_t bits = ctx->total * 8;
	uint8_t padding = 0x80;

	sha256_update(ctx, &padding, 1);

	uint8_t zero = 0;
	while (ctx->block_len != 56) {
		sha256_update(ctx, &zero, 1);
	}

	uint8_t length[8];
	for (int i = 0; i < 8; i++) {
		length[i] = (uint8_t)(bits >> (56 - i * 8));
	}

	sha256_update(ctx, length, 8);

	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(ctx->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)ctx->h[i];
	}
}

void sha256_compute(const void* data, size_t size, uint8_t out[32]) {
	sha256_ctx_t ctx;
	sha256_init(&ctx);

	if (data && size > 0) {
		sha256_update(&ctx, (const uint8_t*)data, size);
	}

	sha256_final(&ctx, out);
}

void sha256_hex(const uint8_t digest[32], char out[65]) {
	static const char digits[] = "0123456789abcdef";

	for (int i = 0; i < 32; i++) {
		out[i * 2] = digits[digest[i] >> 4];
		out[i * 2 + 1] = digits[digest[i] & 0x0Fu];
	}

	out[64] = '\0';
}
