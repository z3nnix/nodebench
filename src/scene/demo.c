#include <stdlib.h>

#include "core/engine.h"
#include "core/input.h"
#include "core/math.h"
#include "render/color.h"
#include "render/draw_list.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "resource/resource.h"
#include "scene/camera_controller.h"
#include "scene/demo.h"
#include "scene/loader_tscn.h"
#include "scene/scene.h"

#define NB_DEMO_SCENE_PATH "assets/scene.tscn"

static const float pi = 3.14159265f;

typedef struct {
	scene_t scene;
	draw_list_t draw_list;
	camera_t camera;
	camera_controller_t camera_controller;
	int logged;
} demo_scene_t;

static void demo_scale_matrix(mat4_t* out, vec3_t scale) {
	mat4_identity(out);
	out->m[0] = scale.x;
	out->m[5] = scale.y;
	out->m[10] = scale.z;
}

static camera_t demo_make_camera(engine_t* engine, const scene_t* scene) {
	camera_t camera;

	if (scene && scene->camera_count > 0) {
		return scene->cameras[0];
	}

	camera.eye.x = 0.0f;
	camera.eye.y = 0.0f;
	camera.eye.z = 5.0f;
	camera.target.x = 0.0f;
	camera.target.y = 0.0f;
	camera.target.z = 0.0f;
	camera.up.x = 0.0f;
	camera.up.y = 1.0f;
	camera.up.z = 0.0f;
	camera.fov = 60.0f;
	camera.near_plane = engine->config.near_plane;
	camera.far_plane = engine->config.far_plane;

	return camera;
}

static void demo_configure_camera(engine_t* engine, draw_list_t* list, const camera_t* camera) {
	float aspect = (float)engine->config.width / (float)engine->config.height;
	mat4_t projection;
	mat4_t view;

	mat4_perspective(&projection, camera->fov * pi / 180.0f, aspect, camera->near_plane, camera->far_plane);
	mat4_look_at(&view, camera->eye, camera->target, camera->up);
	draw_list_set_camera(list, &view, &projection);
}

static void demo_configure_light(engine_t* engine, const scene_t* scene) {
	if (scene->light_count == 0) {
		return;
	}

	const light_t* light = &scene->lights[0];
	vec3_t direction = vec3_scale(light->direction, light->intensity);
	engine->config.light_direction[0] = direction.x;
	engine->config.light_direction[1] = direction.y;
	engine->config.light_direction[2] = direction.z;
}

static nb_result_t demo_scene_init(engine_t* engine, void** context) {
	if (!engine || !context) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	demo_scene_t* runtime = (demo_scene_t*)malloc(sizeof(demo_scene_t));
	if (!runtime) {
		return NB_RESULT_ERROR_SCENE;
	}

	scene_init(&runtime->scene);
	draw_list_init(&runtime->draw_list);
	runtime->logged = 0;

	nb_result_t result = scene_load_tscn(NB_DEMO_SCENE_PATH, engine, &runtime->scene);
	if (result != NB_RESULT_OK) {
		engine_log(engine, "failed to load %s: %s", NB_DEMO_SCENE_PATH, nb_error_string(result));
		scene_shutdown(&runtime->scene);
		free(runtime);

		return result;
	}

	demo_configure_light(engine, &runtime->scene);
	runtime->camera = demo_make_camera(engine, &runtime->scene);
	camera_controller_init(&runtime->camera_controller, &runtime->camera);
	*context = runtime;

	engine_log(engine, "loaded scene=%s entities=%d meshes=%d cameras=%d lights=%d vram=%d", NB_DEMO_SCENE_PATH, runtime->scene.entity_count, runtime->scene.mesh_renderer_count, runtime->scene.camera_count, runtime->scene.light_count, resource_vram_used(&engine->resources));

	return NB_RESULT_OK;
}

static void demo_scene_update(engine_t* engine, void* context, float dt) {
	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!engine || !runtime) {
		return;
	}

	if (input_was_pressed(&engine->input, NB_ACTION_CAMERA_MODE)) {
		camera_mode_t mode = camera_controller_get_mode(&runtime->camera_controller);
		camera_controller_set_mode(&runtime->camera_controller, mode == NB_CAMERA_MODE_FREE ? NB_CAMERA_MODE_ORBIT : NB_CAMERA_MODE_FREE);
		engine_log(engine, "camera mode=%s", mode == NB_CAMERA_MODE_FREE ? "orbit" : "free");
	}

	if (!engine->paused) {
		scene_t* scene = &runtime->scene;
		for (int i = 0; i < scene->transform_count; i++) {
			transform_t* transform = &scene->transforms[i];
			transform->previous_rotation = transform->rotation;
			transform->rotation.y += dt * transform->spin_speed;
			transform->rotation.x += dt * transform->spin_speed * 0.6f;
		}
	}

	camera_controller_update(&runtime->camera_controller, &runtime->camera, &engine->input, dt);
}

static void demo_scene_render(engine_t* engine, void* context, float alpha) {
	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!engine || !runtime) {
		return;
	}

	if (alpha < 0.0f) {
		alpha = 0.0f;
	}

	else if (alpha > 1.0f) {
		alpha = 1.0f;
	}

	scene_t* scene = &runtime->scene;
	draw_list_t* list = &runtime->draw_list;

	draw_list_clear(list);
	demo_configure_camera(engine, list, &runtime->camera);

	uint16_t background_top = rgb555_pack(6, 10, 22);
	uint16_t background_bottom = rgb555_pack(18, 26, 44);
	framebuffer_clear_gradient(&engine->framebuffer, background_top, background_bottom);

	for (int i = 0; i < scene->mesh_renderer_count; i++) {
		entity_id_t id = scene->mesh_renderer_entities[i];
		mesh_renderer_t* renderer_component = &scene->mesh_renderers[i];
		transform_t* transform = scene_get_transform(scene, id);
		mesh_t* mesh = resource_get_mesh(&engine->resources, renderer_component->mesh);
		material_t* material = resource_get_material(&engine->resources, renderer_component->material);
		texture_t* texture = NULL;

		if (!transform || !mesh || !material) {
			continue;
		}

		texture = resource_get_texture(&engine->resources, material->texture);
		if (!texture) {
			continue;
		}

		vec3_t rotation;
		rotation.x = transform->previous_rotation.x + (transform->rotation.x - transform->previous_rotation.x) * alpha;
		rotation.y = transform->previous_rotation.y + (transform->rotation.y - transform->previous_rotation.y) * alpha;
		rotation.z = transform->previous_rotation.z + (transform->rotation.z - transform->previous_rotation.z) * alpha;

		mat4_t translation;
		mat4_t rotation_x;
		mat4_t rotation_y;
		mat4_t scale;
		mat4_t model;

		mat4_translate(&translation, transform->position.x, transform->position.y, transform->position.z);
		mat4_rotate_y(&rotation_y, rotation.y);
		mat4_rotate_x(&rotation_x, rotation.x);
		demo_scale_matrix(&scale, transform->scale);
		mat4_multiply(&model, &translation, &rotation_y);
		mat4_multiply(&model, &model, &rotation_x);
		mat4_multiply(&model, &model, &scale);

		draw_list_push(list, mesh, texture, material, &model);
	}

	draw_list_sort(list);
	renderer_submit(&engine->renderer, list);
	renderer_flush(&engine->renderer);

	if (!runtime->logged) {
		engine_log(engine, "draw list: commands=%d", list->count);
		runtime->logged = 1;
	}
}

static void demo_scene_reset(engine_t* engine, void* context) {
	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!engine || !runtime) {
		return;
	}

	scene_t* scene = &runtime->scene;
	for (int i = 0; i < scene->transform_count; i++) {
		transform_t* transform = &scene->transforms[i];
		transform->rotation = transform->initial_rotation;
		transform->previous_rotation = transform->initial_rotation;
	}

	runtime->camera = demo_make_camera(engine, scene);
	camera_controller_reset(&runtime->camera_controller, &runtime->camera);
	demo_configure_camera(engine, &runtime->draw_list, &runtime->camera);
	demo_configure_light(engine, scene);
}

static void demo_scene_shutdown(engine_t* engine, void* context) {
	(void)engine;

	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!runtime) {
		return;
	}

	scene_shutdown(&runtime->scene);
	free(runtime);
}

void demo_scene_register(engine_t* engine) {
	if (!engine) {
		return;
	}

	engine->scene.context = NULL;
	engine->scene.init = demo_scene_init;
	engine->scene.update = demo_scene_update;
	engine->scene.render = demo_scene_render;
	engine->scene.reset = demo_scene_reset;
	engine->scene.shutdown = demo_scene_shutdown;
}
