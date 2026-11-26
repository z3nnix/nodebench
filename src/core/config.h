#ifndef NB_CORE_CONFIG_H
#define NB_CORE_CONFIG_H

#include "core/log.h"

typedef enum {
	NB_BLEND_NONE,
	NB_BLEND_ALPHA_50,
	NB_BLEND_ADD,
	NB_BLEND_SUBTRACT,
	NB_BLEND_ADD_QUARTER
} nb_blend_mode_t;

typedef enum {
	NB_SHADE_FLAT,
	NB_SHADE_GOURAUD
} nb_shade_mode_t;

typedef struct {
	int width;
	int height;
	int snap_grid_width;
	int snap_grid_height;
	float near_plane;
	float far_plane;
	float affine_strength;
	int color_depth;
	int fog_enabled;
	float fog_near;
	float fog_far;
	int fog_color_rgb[3];
	int dither_enabled;
	const unsigned char* dither_matrix;
	int cull_backfaces;
	int reject_near_triangles;
	int subdivide_large_triangles;
	float subdivide_area_threshold;
	nb_blend_mode_t blend_mode;
	nb_shade_mode_t shade_mode;
	float light_direction[3];
	int texture_vram_limit;
	int wireframe_enabled;
	int xray_enabled;
	int hud_enabled;
	int hud_debug_enabled;
	nb_log_level_t log_level;
	char log_file[256];
} render_config_t;

void render_config_init(render_config_t* config);
int config_load_file(render_config_t* config, const char* path);
int config_apply_cli(render_config_t* config, int argc, char** argv);
void config_print_usage(const char* program);

#endif
