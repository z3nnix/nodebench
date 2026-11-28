#ifndef NB_RESOURCE_LOADER_H
#define NB_RESOURCE_LOADER_H

#include <stddef.h>
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
int image_load_mem(const void* data, size_t size, image_t* image);
void image_free(image_t* image);
int clut_load(const char* path, palette_t* palette);
int clut_load_mem(const void* data, size_t size, palette_t* palette);
void clut_free(palette_t* palette);
int obj_load(const char* path, mesh_t* mesh);
int obj_load_mem(const void* data, size_t size, mesh_t* mesh);
int image_quantize(const image_t* image, const palette_t* palette, texture_format_t format, uint8_t** pixels);

#endif
