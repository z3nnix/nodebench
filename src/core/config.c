#include "core/config.h"

static const unsigned char bayer_4x4[16] = {
	0, 8, 2, 10,
	12, 4, 14, 6,
	3, 11, 1, 9,
	15, 7, 13, 5
};

void render_config_init(render_config_t* config) {
	if (!config) { return; }

	config->width = 320;
	config->height = 240;
	config->snap_grid_width = 320;
	config->snap_grid_height = 240;
	config->near_plane = 0.1f;
	config->far_plane = 100.0f;
	config->affine_strength = 1.0f;
	config->color_depth = 15;
	config->fog_near = 8.0f;
	config->fog_far = 25.0f;
	config->fog_color_rgb[0] = 8;
	config->fog_color_rgb[1] = 14;
	config->fog_color_rgb[2] = 28;
	config->dither_enabled = 1;
	config->dither_matrix = bayer_4x4;
	config->cull_backfaces = 1;
	config->reject_near_triangles = 1;
	config->subdivide_large_triangles = 0;
	config->subdivide_area_threshold = 4096.0f;
	config->blend_mode = NB_BLEND_NONE;
	config->shade_mode = NB_SHADE_GOURAUD;
	config->light_direction[0] = 0.4f;
	config->light_direction[1] = 0.7f;
	config->light_direction[2] = 1.0f;
	config->texture_vram_limit = 1024 * 1024;
}
