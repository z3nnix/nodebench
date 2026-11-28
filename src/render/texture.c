#include "render/color.h"
#include "render/texture.h"

void texture_init(texture_t* texture, int width, int height, texture_format_t format, const uint8_t* pixels, const uint16_t* palette, int transparent_index) {
	if (!texture) { return; }

	texture->width = width;
	texture->height = height;
	texture->format = format;
	texture->pixels = pixels;
	texture->palette = palette;
	texture->transparent_index = transparent_index;
}

uint16_t texture_sample(const texture_t* texture, float u, float v) {
	if (!texture || !texture->pixels || !texture->palette) { return 0; }
	if (texture->width <= 0 || texture->height <= 0) { return 0; }

	int x = (int)(u * (float)texture->width);
	int y = (int)(v * (float)texture->height);

	x &= texture->width - 1;
	y &= texture->height - 1;

	int index = 0;

	if (texture->format == NB_TEXTURE_4BIT) {
		uint8_t packed = texture->pixels[y * (texture->width / 2) + x / 2];
		index = (x & 1) ? (packed & 15) : (packed >> 4);
	}

	else {
		index = texture->pixels[y * texture->width + x];
	}

	if (index == texture->transparent_index) { return NB_TEXTURE_TRANSPARENT; }

	return texture->palette[index];
}

void texture_generate_demo(uint8_t* pixels, uint16_t* palette, int width, int height) {
	if (!pixels || !palette || width != 64 || height != 64) { return; }

	palette[0] = rgb555_pack(0, 0, 0);
	palette[1] = rgb555_pack(24, 44, 68);
	palette[2] = rgb555_pack(48, 80, 116);
	palette[3] = rgb555_pack(180, 104, 52);
	palette[4] = rgb555_pack(232, 188, 112);

	for (int i = 5; i < 16; i++) { palette[i] = rgb555_pack(16 + i * 8, 32 + i * 6, 56 + i * 4); }

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int checker = ((x / 8) + (y / 8)) & 1;
			int band = ((x + y) / 8) & 1;
			int index = 0;

			if (band == 0 && ((x & 7) == 0 || (y & 7) == 0)) { index = 4; }

			else if (band == 0) { index = 3; }

			else { index = checker ? 2 : 1; }

			uint8_t* byte = &pixels[y * (width / 2) + x / 2];

			if (x & 1) { *byte = (uint8_t)((*byte & 0xF0u) | (uint8_t)index); }

			else { *byte = (uint8_t)((*byte & 0x0Fu) | (uint8_t)(index << 4)); }
		}
	}
}

void texture_generate_fallback(uint8_t* pixels, uint16_t* palette, int width, int height) {
	if (!pixels || !palette || width != 64 || height != 64) { return; }
	for (int i = 0; i < 16; i++) { palette[i] = 0; }
	palette[0] = rgb555_pack(40, 48, 60);
	palette[1] = rgb555_pack(56, 66, 82);
	palette[2] = rgb555_pack(110, 145, 185);
	palette[3] = rgb555_pack(175, 215, 255);
	palette[4] = rgb555_pack(225, 242, 255);
	palette[5] = rgb555_pack(255, 255, 255);
	for (int i = 0; i < 2048; i++) { pixels[i] = 0; }
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int index = ((x / 8) + (y / 8)) & 1;
			if ((x & 7) == 0 || (y & 7) == 0) { index = 2; }
			if ((x & 31) == 0 || (y & 31) == 0) { index = 3; }
			if (x == 0 || y == 0) { index = 4; }
			if (x == 0 && y == 0) { index = 5; }
			uint8_t* byte = &pixels[y * (width / 2) + x / 2];

			if (x & 1) { *byte = (uint8_t)((*byte & 0xF0u) | (uint8_t)index); }

			else { *byte = (uint8_t)((*byte & 0x0Fu) | (uint8_t)(index << 4)); }
		}
	}
}
