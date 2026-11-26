#ifndef NB_RENDER_TEXTURE_H
#define NB_RENDER_TEXTURE_H

#include <stdint.h>

#define NB_TEXTURE_TRANSPARENT 0xFFFFu

typedef enum {
	NB_TEXTURE_4BIT,
	NB_TEXTURE_8BIT
} texture_format_t;

typedef struct {
	int width;
	int height;
	texture_format_t format;
	const uint8_t* pixels;
	const uint16_t* palette;
	int transparent_index;
} texture_t;

void texture_init(texture_t* texture, int width, int height, texture_format_t format, const uint8_t* pixels, const uint16_t* palette, int transparent_index);
uint16_t texture_sample(const texture_t* texture, float u, float v);
void texture_generate_demo(uint8_t* pixels, uint16_t* palette, int width, int height);

#endif
