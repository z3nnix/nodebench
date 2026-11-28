#ifndef NB_CORE_CONFIG_H
#define NB_CORE_CONFIG_H

#include "core/log.h"

#define NB_CONFIG_BINDINGS_MAX 128
#define NB_CONFIG_BINDING_NAME_MAX 32
#define NB_CONFIG_BINDING_KEYS_MAX 96

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
	char action[NB_CONFIG_BINDING_NAME_MAX];
	char keys[NB_CONFIG_BINDING_KEYS_MAX];
} nb_key_binding_t;

#define NB_CONFIG_MODE_MAX 16
#define NB_CONFIG_PROJECT_NAME_MAX 128

typedef struct {
	int width;
	int height;
	int window_width;
	int window_height;
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
	int sky_top_rgb[3];
	int sky_bottom_rgb[3];
	int dither_enabled;
	const unsigned char* dither_matrix;
	int cull_backfaces;
	int reject_near_triangles;
	int subdivide_large_triangles;
	float subdivide_area_threshold;
	nb_blend_mode_t blend_mode;
	nb_shade_mode_t shade_mode;
	float light_direction[3];
	float light_color[3];
	int texture_vram_limit;
	int wireframe_enabled;
	int xray_enabled;
	int hud_enabled;
	int hud_debug_enabled;
	int editor_enabled;
	int max_frames;
	nb_log_level_t log_level;
	char log_file[256];
	char project_root[256];
	char scene_path[256];
	char project_name[NB_CONFIG_PROJECT_NAME_MAX];
	char mode[NB_CONFIG_MODE_MAX];
	nb_key_binding_t key_bindings[NB_CONFIG_BINDINGS_MAX];
	int key_binding_count;
} render_config_t;

void render_config_init(render_config_t* config);
int config_cli_selects_session(int argc, char** argv);
int config_cli_requests_launcher(int argc, char** argv);
int config_resolve_project_root(int argc, char** argv, char* out, size_t size);
int config_load_file(render_config_t* config, const char* path);
int config_load_buffer(render_config_t* config, const char* data, size_t size);
int config_apply_cli(render_config_t* config, int argc, char** argv);
void config_print_usage(const char* program);

#endif
