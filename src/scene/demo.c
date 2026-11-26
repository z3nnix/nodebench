#include <stdio.h>

#include "core/config.h"
#include "core/math.h"
#include "core/platform.h"
#include "render/color.h"
#include "render/framebuffer.h"
#include "render/mesh.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "scene/demo.h"

static const float pi = 3.14159265f;

static void demo_configure_camera(renderer_t* renderer, const render_config_t* config, mat4_t* projection, mat4_t* view) {
	mat4_perspective(projection, 60.0f * pi / 180.0f, (float)config->width / (float)config->height, config->near_plane, config->far_plane);

	vec3_t eye = { 0.0f, 0.0f, 5.0f };
	vec3_t center = { 0.0f, 0.0f, 0.0f };
	vec3_t up = { 0.0f, 1.0f, 0.0f };

	mat4_look_at(view, eye, center, up);
	renderer_set_camera(renderer, view, projection);
}

static void demo_print_toggles(const render_config_t* config) {
	fprintf(stderr, "affine=%.2f dither=%d fog=%d cull=%d clip=%d\n", config->affine_strength, config->dither_enabled, config->fog_enabled, config->cull_backfaces, config->reject_near_triangles);
}

int demo_run(int argc, char** argv) {
	render_config_t config;
	render_config_init(&config);

	if (!config_load_file(&config, "config.ini")) { return 1; }

	int cli_status = config_apply_cli(&config, argc, argv);
	if (cli_status == 0) { return 1; }
	if (cli_status == 2) { return 0; }

	platform_desc_t platform_desc = { "NodeBench", config.width, config.height, 3, 0 };
	platform_t platform;
	if (!platform_init(&platform, &platform_desc)) { return 1; }

	uint32_t* output = platform_pixels(&platform);
	framebuffer_t framebuffer;
	if (!framebuffer_init(&framebuffer, &config)) {
		platform_shutdown(&platform);
		return 1;
	}

	renderer_t renderer;
	if (!renderer_init(&renderer, &framebuffer)) {
		framebuffer_shutdown(&framebuffer);
		platform_shutdown(&platform);
		return 1;
	}

	vertex_t vertices[24];
	triangle_t triangles[12];
	mesh_t mesh;
	mesh_create_cube(&mesh, vertices, 24, triangles, 12, 1.0f);

	uint8_t texture_pixels[64 * 64 / 2];
	uint16_t palette[16];
	texture_t texture;
	texture_generate_demo(texture_pixels, palette, 64, 64);
	texture_init(&texture, 64, 64, NB_TEXTURE_4BIT, texture_pixels, palette, -1);

	mat4_t projection;
	mat4_t view;
	mat4_t model;
	mat4_t rotation_x;
	mat4_t rotation_y;

	demo_configure_camera(&renderer, &config, &projection, &view);

	uint16_t background_top = rgb555_pack(6, 10, 22);
	uint16_t background_bottom = rgb555_pack(18, 26, 44);

	float angle = 0.0f;
	int should_quit = 0;

	demo_print_toggles(&config);

	while (!should_quit) {
		float dt = platform_delta_time(&platform);

		platform_pump_events(&platform, &should_quit);

		platform_key_t key;
		while (platform_pop_key(&platform, &key)) {
			if (key == NB_KEY_1) {
				config.affine_strength = config.affine_strength > 0.5f ? 0.0f : 1.0f;
			}

			else if (key == NB_KEY_2) {
				config.dither_enabled = !config.dither_enabled;
			}

			else if (key == NB_KEY_3) {
				config.fog_enabled = !config.fog_enabled;
			}

			else if (key == NB_KEY_4) {
				config.cull_backfaces = !config.cull_backfaces;
			}

			else if (key == NB_KEY_5) {
				config.reject_near_triangles = !config.reject_near_triangles;
			}

			else if (key == NB_KEY_R) {
				angle = 0.0f;
				demo_configure_camera(&renderer, &config, &projection, &view);
			}

			demo_print_toggles(&config);
		}

		angle += dt * 0.8f;

		framebuffer_clear_gradient(&framebuffer, background_top, background_bottom);

		mat4_rotate_y(&rotation_y, angle);
		mat4_rotate_x(&rotation_x, angle * 0.6f);
		mat4_multiply(&model, &rotation_y, &rotation_x);

		renderer_draw_mesh(&renderer, &mesh, &texture, &model, config.blend_mode);
		framebuffer_resolve(&framebuffer, output);
		platform_present(&platform);
	}

	renderer_shutdown(&renderer);
	framebuffer_shutdown(&framebuffer);
	platform_shutdown(&platform);

	return 0;
}
