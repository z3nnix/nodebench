#ifndef NB_RENDER_FRAMEBUFFER_H
#define NB_RENDER_FRAMEBUFFER_H

#include <stdint.h>

#include "core/config.h"
#include "render/color.h"

typedef struct {
	int width;
	int height;
	uint16_t* color;
	render_config_t config;
} framebuffer_t;

int framebuffer_init(framebuffer_t* framebuffer, const render_config_t* config);
void framebuffer_shutdown(framebuffer_t* framebuffer);
void framebuffer_clear(framebuffer_t* framebuffer, uint16_t color);
void framebuffer_clear_gradient(framebuffer_t* framebuffer, uint16_t top, uint16_t bottom);
void framebuffer_write(framebuffer_t* framebuffer, int x, int y, float r, float g, float b, nb_blend_mode_t blend);
void framebuffer_resolve(framebuffer_t* framebuffer, uint32_t* output);

#endif
