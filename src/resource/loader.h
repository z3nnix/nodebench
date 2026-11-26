#ifndef NB_RESOURCE_LOADER_H
#define NB_RESOURCE_LOADER_H

#include <stdint.h>

#include "render/mesh.h"
#include "render/texture.h"

typedef struct {
	int width;
	int height;
	uint8_t* rgb;
} image_t;

typedef struct {
	uint16_t* colors;
	int count;
} palette_t;

int image_load(const char* path, image_t* image);
void image_free(image_t* image);
int clut_load(const char* path, palette_t* palette);
void clut_free(palette_t* palette);
int image_quantize(const image_t* image, const palette_t* palette, texture_format_t format, uint8_t** pixels);
int obj_load(const char* path, mesh_t* mesh);

#endif
