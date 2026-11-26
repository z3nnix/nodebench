#include <stdlib.h>

#include "core/engine.h"
#include "core/math.h"
#include "render/color.h"
#include "render/framebuffer.h"
#include "render/mesh.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "resource/loader.h"
#include "resource/resource.h"
#include "scene/demo.h"

static const float pi = 3.14159265f;

typedef struct {
	mesh_id mesh;
	texture_id texture;
	material_id material;
	float angle;
	float previous_angle;
} demo_scene_t;

static void demo_configure_camera(engine_t* engine) {
	float aspect = (float)engine->config.width / (float)engine->config.height;
	mat4_t projection;
	mat4_t view;

	mat4_perspective(&projection, 60.0f * pi / 180.0f, aspect, engine->config.near_plane, engine->config.far_plane);

	vec3_t eye = { 0.0f, 0.0f, 5.0f };
	vec3_t center = { 0.0f, 0.0f, 0.0f };
	vec3_t up = { 0.0f, 1.0f, 0.0f };

	mat4_look_at(&view, eye, center, up);
	renderer_set_camera(&engine->renderer, &view, &projection);
}

static nb_result_t demo_scene_init(engine_t* engine, void** context) {
	if (!engine || !context) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	demo_scene_t* scene = (demo_scene_t*)malloc(sizeof(demo_scene_t));
	if (!scene) { return NB_RESULT_ERROR_SCENE; }

	scene->mesh = NB_RESOURCE_INVALID_ID;
	scene->texture = NB_RESOURCE_INVALID_ID;
	scene->material = NB_RESOURCE_INVALID_ID;
	scene->angle = 0.0f;
	scene->previous_angle = 0.0f;

	nb_result_t result = NB_RESULT_ERROR_RESOURCE;
	image_t image = { 0, 0, NULL };
	palette_t palette = { NULL, 0 };
	uint8_t* texture_pixels = NULL;

	mesh_t mesh;
	if (!obj_load("assets/cube.obj", &mesh)) {
		engine_log(engine, "failed to load assets/cube.obj");
		goto cleanup;
	}

	scene->mesh = resource_add_mesh(&engine->resources, &mesh);
	mesh_destroy(&mesh);
	if (scene->mesh == NB_RESOURCE_INVALID_ID) {
		engine_log(engine, "failed to add mesh resource");
		goto cleanup;
	}

	if (!image_load("assets/cube.ppm", &image)) {
		engine_log(engine, "failed to load assets/cube.ppm");
		goto cleanup;
	}

	if (!clut_load("assets/cube.clut", &palette)) {
		engine_log(engine, "failed to load assets/cube.clut");
		goto cleanup;
	}

	if (!image_quantize(&image, &palette, NB_TEXTURE_4BIT, &texture_pixels)) {
		engine_log(engine, "failed to quantize assets/cube.ppm");
		goto cleanup;
	}

	texture_t texture_source;
	texture_init(&texture_source, image.width, image.height, NB_TEXTURE_4BIT, texture_pixels, palette.colors, -1);

	scene->texture = resource_add_texture(&engine->resources, &texture_source);
	if (scene->texture == NB_RESOURCE_INVALID_ID) {
		engine_log(engine, "failed to add texture resource");
		goto cleanup;
	}

	material_t material;
	material.blend_mode = engine->config.blend_mode;
	material.shade_mode = engine->config.shade_mode;
	material.texture = scene->texture;

	scene->material = resource_add_material(&engine->resources, &material);
	if (scene->material == NB_RESOURCE_INVALID_ID) {
		engine_log(engine, "failed to add material resource");
		goto cleanup;
	}

	mesh_t* loaded_mesh = resource_get_mesh(&engine->resources, scene->mesh);
	if (!loaded_mesh) {
		goto cleanup;
	}

	engine_log(engine, "loaded mesh=%u verts=%d tris=%d texture=%u material=%u vram=%d", (unsigned)scene->mesh, loaded_mesh->vertex_count, loaded_mesh->triangle_count, (unsigned)scene->texture, (unsigned)scene->material, resource_vram_used(&engine->resources));

	demo_configure_camera(engine);
	*context = scene;
	result = NB_RESULT_OK;

cleanup:
	image_free(&image);
	clut_free(&palette);
	free(texture_pixels);

	if (result != NB_RESULT_OK) { free(scene); }

	return result;
}

static void demo_scene_update(engine_t* engine, void* context, float dt) {
	(void)engine;

	demo_scene_t* scene = (demo_scene_t*)context;
	if (!scene) { return; }

	scene->previous_angle = scene->angle;
	scene->angle += dt * 0.8f;
}

static void demo_scene_render(engine_t* engine, void* context, float alpha) {
	demo_scene_t* scene = (demo_scene_t*)context;
	if (!engine || !scene) { return; }

	if (alpha < 0.0f) { alpha = 0.0f; }
	if (alpha > 1.0f) { alpha = 1.0f; }

	mesh_t* mesh = resource_get_mesh(&engine->resources, scene->mesh);
	texture_t* texture = resource_get_texture(&engine->resources, scene->texture);
	material_t* material = resource_get_material(&engine->resources, scene->material);
	if (!mesh || !texture || !material) { return; }

	uint16_t background_top = rgb555_pack(6, 10, 22);
	uint16_t background_bottom = rgb555_pack(18, 26, 44);
	framebuffer_clear_gradient(&engine->framebuffer, background_top, background_bottom);

	float angle = scene->previous_angle + (scene->angle - scene->previous_angle) * alpha;
	mat4_t rotation_x;
	mat4_t rotation_y;
	mat4_t model;

	mat4_rotate_y(&rotation_y, angle);
	mat4_rotate_x(&rotation_x, angle * 0.6f);
	mat4_multiply(&model, &rotation_y, &rotation_x);

	renderer_draw_mesh(&engine->renderer, mesh, texture, &model, material->blend_mode);
}

static void demo_scene_reset(engine_t* engine, void* context) {
	demo_scene_t* scene = (demo_scene_t*)context;
	if (!engine || !scene) { return; }

	scene->angle = 0.0f;
	scene->previous_angle = 0.0f;
	demo_configure_camera(engine);
}

static void demo_scene_shutdown(engine_t* engine, void* context) {
	(void)engine;
	free(context);
}

void demo_scene_register(engine_t* engine) {
	if (!engine) { return; }

	engine->scene.context = NULL;
	engine->scene.init = demo_scene_init;
	engine->scene.update = demo_scene_update;
	engine->scene.render = demo_scene_render;
	engine->scene.reset = demo_scene_reset;
	engine->scene.shutdown = demo_scene_shutdown;
}
