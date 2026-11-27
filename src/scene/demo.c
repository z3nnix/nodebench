#include <stdlib.h>

#include "core/engine.h"
#include "core/math.h"
#include "render/color.h"
#include "render/draw_list.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "resource/resource.h"
#include "scene/demo.h"
#include "scene/loader_tscn.h"
#include "scene/scene.h"

static const float pi = 3.14159265f;

typedef struct {
	scene_t scene;
	draw_list_t draw_list;
	int logged;
} demo_scene_t;

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

	nb_result_t result = scene_load_tscn(engine->config.scene_path, engine, &runtime->scene);
	if (result != NB_RESULT_OK) {
		engine_log(engine, "failed to load %s: %s", engine->config.scene_path, nb_error_string(result));
		scene_shutdown(&runtime->scene);
		draw_list_free(&runtime->draw_list);
		free(runtime);

		return result;
	}

	engine->scene_graph = &runtime->scene;
	wren_vm_bind_scene(&engine->script_vm, &runtime->scene);
	int scripts = 0;
	for (int i = 0; i < runtime->scene.node_count; i++) {
		node_t* node = &runtime->scene.nodes[i];
		if (!(node->components & NB_NODE_COMPONENT_SCRIPT)) {
			continue;
		}

		const char* source = resource_get_script(&engine->resources, node->script);
		if (!source || !wren_vm_attach(&engine->script_vm, node->id, node->script_module, source)) {
			engine_log(engine, "failed to attach script to node=%s", node->name);
			scene_shutdown(&runtime->scene);
			draw_list_free(&runtime->draw_list);
			free(runtime);

			return NB_RESULT_ERROR_SCENE;
		}

		scripts++;
	}

	wren_vm_init_instances(&engine->script_vm);

	demo_configure_light(engine, &runtime->scene);
	*context = runtime;

	engine_log(engine, "loaded scene=%s entities=%d meshes=%d cameras=%d lights=%d scripts=%d vram=%d", engine->config.scene_path, runtime->scene.entity_count, runtime->scene.mesh_renderer_count, runtime->scene.camera_count, runtime->scene.light_count, scripts, resource_vram_used(&engine->resources));

	return NB_RESULT_OK;
}

static void demo_scene_update(engine_t* engine, void* context, float dt) {
	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!engine || !runtime) {
		return;
	}

	if (!engine->paused) {
		scene_t* scene = &runtime->scene;
		for (int i = 0; i < scene->node_count; i++) {
			node_t* node = &scene->nodes[i];
			if (!(node->components & NB_NODE_COMPONENT_TRANSFORM) || (node->components & NB_NODE_COMPONENT_SCRIPT)) {
				continue;
			}

			node->transform.previous_rotation = node->transform.rotation;
			node->transform.rotation.y += dt * node->transform.spin_speed;
			node->transform.rotation.x += dt * node->transform.spin_speed * 0.6f;
		}

		scene_update_world_transforms(scene);
		scene_rebuild_cache(scene);
	}
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
	camera_t* camera = scene_get_active_camera(scene);
	camera_t fallback = { 0 };
	if (!camera) {
		fallback.eye = (vec3_t){ 0.0f, 0.0f, 5.0f };
		fallback.target = (vec3_t){ 0.0f, 0.0f, 0.0f };
		fallback.up = (vec3_t){ 0.0f, 1.0f, 0.0f };
		fallback.fov = 60.0f;
		fallback.near_plane = engine->config.near_plane;
		fallback.far_plane = engine->config.far_plane;
		camera = &fallback;
	}

	demo_configure_camera(engine, list, camera);

	uint16_t background_top = rgb555_pack(engine->config.sky_top_rgb[0], engine->config.sky_top_rgb[1], engine->config.sky_top_rgb[2]);
	uint16_t background_bottom = rgb555_pack(engine->config.sky_bottom_rgb[0], engine->config.sky_bottom_rgb[1], engine->config.sky_bottom_rgb[2]);
	framebuffer_clear_gradient(&engine->framebuffer, background_top, background_bottom);

	for (int i = 0; i < scene->mesh_renderer_count; i++) {
		entity_id_t id = scene->mesh_renderer_entities[i];
		mesh_renderer_t* renderer_component = &scene->mesh_renderers[i];
		node_t* node = scene_get_node(scene, id);
		mesh_t* mesh = resource_get_mesh(&engine->resources, renderer_component->mesh);
		material_t* material = resource_get_material(&engine->resources, renderer_component->material);
		texture_t* texture = NULL;

		if (!node || !(node->components & NB_NODE_COMPONENT_TRANSFORM) || !mesh || !material) {
			continue;
		}

		texture = resource_get_texture(&engine->resources, material->texture);
		if (!texture) {
			continue;
		}

		mat4_t model;
		for (int j = 0; j < 16; j++) {
			model.m[j] = node->previous_world_matrix.m[j] + (node->world_matrix.m[j] - node->previous_world_matrix.m[j]) * alpha;
		}

		if (node->tint_enabled) {
			draw_list_push_ex(list, mesh, texture, material, &model, node->tint);
		}

		else {
			draw_list_push(list, mesh, texture, material, &model);
		}
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
	for (int i = 0; i < scene->node_count; i++) {
		node_t* node = &scene->nodes[i];
		if (!(node->components & NB_NODE_COMPONENT_TRANSFORM)) {
			continue;
		}

		node->transform.rotation = node->transform.initial_rotation;
		node->transform.previous_rotation = node->transform.initial_rotation;
	}

	scene_update_world_transforms(scene);
	scene_rebuild_cache(scene);
	wren_vm_init_instances(&engine->script_vm);
	demo_configure_light(engine, scene);
}

static void demo_scene_shutdown(engine_t* engine, void* context) {
	(void)engine;

	demo_scene_t* runtime = (demo_scene_t*)context;
	if (!runtime) {
		return;
	}

	scene_shutdown(&runtime->scene);
	draw_list_free(&runtime->draw_list);
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
