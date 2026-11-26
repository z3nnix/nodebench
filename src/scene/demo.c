#include <stdio.h>
#include <stdlib.h>

#include "core/config.h"
#include "core/math.h"
#include "core/platform.h"
#include "render/color.h"
#include "render/framebuffer.h"
#include "render/mesh.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "resource/loader.h"
#include "resource/resource.h"
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

	int platform_ready = 1;
	int framebuffer_ready = 0;
	int renderer_ready = 0;
	int resources_ready = 0;
	int status = 1;

	image_t image = { 0, 0, NULL };
	palette_t palette = { NULL, 0 };
	uint8_t* texture_pixels = NULL;

	uint32_t* output = platform_pixels(&platform);
	framebuffer_t framebuffer;
	if (!framebuffer_init(&framebuffer, &config)) { goto cleanup; }
	framebuffer_ready = 1;

	renderer_t renderer;
	if (!renderer_init(&renderer, &framebuffer)) { goto cleanup; }
	renderer_ready = 1;

	resource_store_t resources;
	resource_store_init(&resources, config.texture_vram_limit);
	resources_ready = 1;

	mesh_t mesh;
	if (!obj_load("assets/cube.obj", &mesh)) {
		fprintf(stderr, "failed to load assets/cube.obj\n");
		goto cleanup;
	}

	mesh_id mesh_handle = resource_add_mesh(&resources, &mesh);
	mesh_destroy(&mesh);
	if (mesh_handle == NB_RESOURCE_INVALID_ID) {
		fprintf(stderr, "failed to add mesh resource\n");
		goto cleanup;
	}

	if (!image_load("assets/cube.ppm", &image)) {
		fprintf(stderr, "failed to load assets/cube.ppm\n");
		goto cleanup;
	}

	if (!clut_load("assets/cube.clut", &palette)) {
		fprintf(stderr, "failed to load assets/cube.clut\n");
		goto cleanup;
	}

	if (!image_quantize(&image, &palette, NB_TEXTURE_4BIT, &texture_pixels)) {
		fprintf(stderr, "failed to quantize assets/cube.ppm\n");
		goto cleanup;
	}

	texture_t texture_source;
	texture_init(&texture_source, image.width, image.height, NB_TEXTURE_4BIT, texture_pixels, palette.colors, -1);

	texture_id texture_handle = resource_add_texture(&resources, &texture_source);
	if (texture_handle == NB_RESOURCE_INVALID_ID) {
		fprintf(stderr, "failed to add texture resource\n");
		goto cleanup;
	}

	material_t material;
	material.blend_mode = config.blend_mode;
	material.shade_mode = config.shade_mode;
	material.texture = texture_handle;

	material_id material_handle = resource_add_material(&resources, &material);
	if (material_handle == NB_RESOURCE_INVALID_ID) {
		fprintf(stderr, "failed to add material resource\n");
		goto cleanup;
	}

	mesh_t* loaded_mesh = resource_get_mesh(&resources, mesh_handle);
	texture_t* loaded_texture = resource_get_texture(&resources, texture_handle);
	material_t* loaded_material = resource_get_material(&resources, material_handle);
	if (!loaded_mesh || !loaded_texture || !loaded_material) { goto cleanup; }

	fprintf(stderr, "loaded mesh=%u verts=%d tris=%d texture=%u material=%u vram=%d\n", (unsigned)mesh_handle, loaded_mesh->vertex_count, loaded_mesh->triangle_count, (unsigned)texture_handle, (unsigned)material_handle, resource_vram_used(&resources));

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

		renderer_draw_mesh(&renderer, loaded_mesh, loaded_texture, &model, loaded_material->blend_mode);
		framebuffer_resolve(&framebuffer, output);
		platform_present(&platform);
	}

	status = 0;

cleanup:
	if (renderer_ready) { renderer_shutdown(&renderer); }
	if (framebuffer_ready) { framebuffer_shutdown(&framebuffer); }
	if (platform_ready) { platform_shutdown(&platform); }
	if (resources_ready) { resource_store_shutdown(&resources); }

	image_free(&image);
	clut_free(&palette);
	free(texture_pixels);

	return status;
}
