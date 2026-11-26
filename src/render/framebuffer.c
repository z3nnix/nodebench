#include <stdlib.h>

#include "render/framebuffer.h"

static float clamp_channel(float value) {
	if (value < 0.0f) { return 0.0f; }
	if (value > 255.0f) { return 255.0f; }
	return value;
}

static float dither_offset(const framebuffer_t* framebuffer, int x, int y) {
	if (!framebuffer->config.dither_enabled || !framebuffer->config.dither_matrix) { return 0.0f; }

	unsigned char threshold = framebuffer->config.dither_matrix[(y & 3) * 4 + (x & 3)];
	return ((float)threshold - 7.5f) * 0.5f;
}

int framebuffer_init(framebuffer_t* framebuffer, const render_config_t* config) {
	if (!framebuffer || !config) { return 0; }

	framebuffer->width = config->width;
	framebuffer->height = config->height;
	framebuffer->color = NULL;
	framebuffer->config = *config;

	if (framebuffer->width <= 0 || framebuffer->height <= 0) { return 0; }

	framebuffer->color = (uint16_t*)calloc((size_t)framebuffer->width * (size_t)framebuffer->height, sizeof(uint16_t));
	if (!framebuffer->color) { return 0; }

	return 1;
}

void framebuffer_shutdown(framebuffer_t* framebuffer) {
	if (!framebuffer) { return; }

	free(framebuffer->color);
	framebuffer->color = NULL;
	framebuffer->width = 0;
	framebuffer->height = 0;
}

void framebuffer_clear(framebuffer_t* framebuffer, uint16_t color) {
	if (!framebuffer || !framebuffer->color) { return; }

	for (int i = 0; i < framebuffer->width * framebuffer->height; i++) { framebuffer->color[i] = color; }
}

void framebuffer_clear_gradient(framebuffer_t* framebuffer, uint16_t top, uint16_t bottom) {
	if (!framebuffer || !framebuffer->color) { return; }

	int top_r = 0;
	int top_g = 0;
	int top_b = 0;
	int bottom_r = 0;
	int bottom_g = 0;
	int bottom_b = 0;

	rgb555_unpack(top, &top_r, &top_g, &top_b);
	rgb555_unpack(bottom, &bottom_r, &bottom_g, &bottom_b);

	for (int y = 0; y < framebuffer->height; y++) {
		float t = framebuffer->height > 1 ? (float)y / (float)(framebuffer->height - 1) : 0.0f;
		float r = (float)top_r + ((float)bottom_r - (float)top_r) * t;
		float g = (float)top_g + ((float)bottom_g - (float)top_g) * t;
		float b = (float)top_b + ((float)bottom_b - (float)top_b) * t;

		for (int x = 0; x < framebuffer->width; x++) { framebuffer_write(framebuffer, x, y, r, g, b, NB_BLEND_NONE); }
	}
}

void framebuffer_write(framebuffer_t* framebuffer, int x, int y, float r, float g, float b, nb_blend_mode_t blend) {
	if (!framebuffer || !framebuffer->color) { return; }
	if (x < 0 || x >= framebuffer->width || y < 0 || y >= framebuffer->height) { return; }

	float offset = dither_offset(framebuffer, x, y);
	float sr = clamp_channel(r + offset);
	float sg = clamp_channel(g + offset);
	float sb = clamp_channel(b + offset);

	int old_r = 0;
	int old_g = 0;
	int old_b = 0;

	rgb555_unpack(framebuffer->color[y * framebuffer->width + x], &old_r, &old_g, &old_b);

	if (blend == NB_BLEND_ALPHA_50) {
		sr = ((float)old_r + sr) * 0.5f;
		sg = ((float)old_g + sg) * 0.5f;
		sb = ((float)old_b + sb) * 0.5f;
	}

	else if (blend == NB_BLEND_ADD) {
		sr = (float)old_r + sr;
		sg = (float)old_g + sg;
		sb = (float)old_b + sb;
	}

	else if (blend == NB_BLEND_SUBTRACT) {
		sr = (float)old_r - sr;
		sg = (float)old_g - sg;
		sb = (float)old_b - sb;
	}

	else if (blend == NB_BLEND_ADD_QUARTER) {
		sr = (float)old_r + sr * 0.25f;
		sg = (float)old_g + sg * 0.25f;
		sb = (float)old_b + sb * 0.25f;
	}

	framebuffer->color[y * framebuffer->width + x] = rgb555_pack((int)clamp_channel(sr + 4.0f), (int)clamp_channel(sg + 4.0f), (int)clamp_channel(sb + 4.0f));
}

void framebuffer_resolve(framebuffer_t* framebuffer, uint32_t* output) {
	if (!framebuffer || !framebuffer->color || !output) { return; }

	for (int i = 0; i < framebuffer->width * framebuffer->height; i++) {
		int r = 0;
		int g = 0;
		int b = 0;

		rgb555_unpack(framebuffer->color[i], &r, &g, &b);
		output[i] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 255u;
	}
}
