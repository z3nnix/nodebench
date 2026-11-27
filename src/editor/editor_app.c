#include "editor/editor_app.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/math.h"
#include "editor/editor.h"
#include "render/color.h"
#include "render/draw_list.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "resource/resource.h"
#include "scene/scene_doc.h"
#include "script/wren_vm.h"

static const float pi = 3.14159265f;

typedef struct {
	scene_t scene;
	draw_list_t draw_list;
	int logged;
} editor_app_t;

static void editor_app_get_camera(editor_t* editor, camera_t* camera) {
	camera->eye.x = editor->camera_pos[0];
	camera->eye.y = editor->camera_pos[1];
	camera->eye.z = editor->camera_pos[2];
	float cy = cosf(editor->camera_yaw);
	float sy = sinf(editor->camera_yaw);
	float cp = cosf(editor->camera_pitch);
	float sp = sinf(editor->camera_pitch);
	camera->target.x = camera->eye.x + cy * cp;
	camera->target.y = camera->eye.y + sp;
	camera->target.z = camera->eye.z + sy * cp;
	camera->up = (vec3_t){ 0.0f, 1.0f, 0.0f };
	camera->fov = 60.0f;
	camera->near_plane = 0.05f;
	camera->far_plane = 200.0f;
}

static void editor_app_configure_camera(engine_t* engine, draw_list_t* list, const camera_t* camera) {
	float aspect = (float)engine->config.width / (float)engine->config.height;
	mat4_t projection;
	mat4_t view;
	mat4_perspective(&projection, camera->fov * pi / 180.0f, aspect, camera->near_plane, camera->far_plane);
	mat4_look_at(&view, camera->eye, camera->target, camera->up);
	draw_list_set_camera(list, &view, &projection);
}

static nb_result_t editor_app_init(engine_t* engine, void** context) {
	if (!engine || !context) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	editor_app_t* app = (editor_app_t*)malloc(sizeof(editor_app_t));
	if (!app) { return NB_RESULT_ERROR_SCENE; }

	memset(app, 0, sizeof(*app));
	scene_init(&app->scene);
	draw_list_init(&app->draw_list);
	scene_doc_clear(&engine->scene_doc);

	nb_result_t result = scene_doc_load(&engine->scene_doc, engine->config.scene_path, engine, &app->scene);
	if (result != NB_RESULT_OK) {
		engine_log(engine, "editor failed to load scene: %s: %s", engine->config.scene_path, nb_error_string(result));
		if (!engine->scene_doc.path[0] && engine->config.scene_path[0]) {
			snprintf(engine->scene_doc.path, sizeof(engine->scene_doc.path), "%s", engine->config.scene_path);
		}
		scene_shutdown(&app->scene);
		free(app);
		return result;
	}

	engine->scene_graph = &app->scene;
	if (engine->scene_doc.path[0]) {
		snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%s", engine->scene_doc.path);
	}
	*context = app;
	engine_log(engine, "editor scene loaded: %s entities=%d", engine->scene_doc.path, app->scene.entity_count);
	return NB_RESULT_OK;
}

static void editor_app_update(engine_t* engine, void* context, float dt) {
	(void)dt;
	editor_app_t* app = (editor_app_t*)context;
	if (!engine || !app) { return; }

	scene_update_world_transforms(&app->scene);
	scene_rebuild_cache(&app->scene);
}

static void editor_app_render(engine_t* engine, void* context, float alpha) {
	editor_app_t* app = (editor_app_t*)context;
	if (!engine || !app) { return; }
	if (alpha < 0.0f) { alpha = 0.0f; }
	if (alpha > 1.0f) { alpha = 1.0f; }

	draw_list_clear(&app->draw_list);
	camera_t editor_cam;
	editor_app_get_camera(&engine->editor, &editor_cam);
	editor_cam.near_plane = engine->config.near_plane;
	editor_cam.far_plane = engine->config.far_plane;
	editor_app_configure_camera(engine, &app->draw_list, &editor_cam);
	uint16_t bg_top = rgb555_pack(engine->config.sky_top_rgb[0], engine->config.sky_top_rgb[1], engine->config.sky_top_rgb[2]);
	uint16_t bg_bottom = rgb555_pack(engine->config.sky_bottom_rgb[0], engine->config.sky_bottom_rgb[1], engine->config.sky_bottom_rgb[2]);
	framebuffer_clear_gradient(&engine->framebuffer, bg_top, bg_bottom);

	for (int i = 0; i < app->scene.mesh_renderer_count; i++) {
		entity_id_t id = app->scene.mesh_renderer_entities[i];
		mesh_renderer_t* mr = &app->scene.mesh_renderers[i];
		node_t* node = scene_get_node(&app->scene, id);
		mesh_t* mesh = resource_get_mesh(&engine->resources, mr->mesh);
		material_t* material = resource_get_material(&engine->resources, mr->material);
		texture_t* texture = NULL;
		if (!node || !(node->components & NB_NODE_COMPONENT_TRANSFORM) || !mesh || !material) { continue; }
		texture = resource_get_texture(&engine->resources, material->texture);
		if (!texture) { continue; }

		mat4_t model;
		for (int j = 0; j < 16; j++) {
			model.m[j] = node->previous_world_matrix.m[j] + (node->world_matrix.m[j] - node->previous_world_matrix.m[j]) * alpha;
		}
		if (node->tint_enabled) { draw_list_push_ex(&app->draw_list, mesh, texture, material, &model, node->tint); }
		else { draw_list_push(&app->draw_list, mesh, texture, material, &model); }
	}

	draw_list_sort(&app->draw_list);
	renderer_submit(&engine->renderer, &app->draw_list);
	renderer_flush(&engine->renderer);

	if (!app->logged) {
		engine_log(engine, "editor draw commands=%d", app->draw_list.count);
		app->logged = 1;
	}
}

static void editor_app_reset(engine_t* engine, void* context) {
	editor_app_t* app = (editor_app_t*)context;
	if (!engine || !app) { return; }

	char path[NB_SCENE_DOC_STR];
	snprintf(path, sizeof(path), "%s", engine->scene_doc.path[0] ? engine->scene_doc.path : engine->config.scene_path);
	scene_shutdown(&app->scene);
	scene_init(&app->scene);
	if (scene_doc_load(&engine->scene_doc, path, engine, &app->scene) == NB_RESULT_OK) {
		engine->scene_graph = &app->scene;
		if (engine->scene_doc.path[0]) {
			snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%s", engine->scene_doc.path);
		}
		engine_log(engine, "editor scene reloaded: %s", engine->scene_doc.path);
	}
	else {
		engine_log(engine, "editor scene reload failed: %s", path);
		engine->scene_graph = &app->scene;
	}
}

static void editor_app_shutdown(engine_t* engine, void* context) {
	editor_app_t* app = (editor_app_t*)context;
	if (!app) { return; }
	if (engine) { engine->scene_graph = NULL; }
	scene_shutdown(&app->scene);
	free(app);
}

nb_result_t editor_app_open_scene(engine_t* engine, const char* path) {
	if (!engine || !path || !*path || !engine->scene.context) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}
	editor_app_t* app = (editor_app_t*)engine->scene.context;

	scene_shutdown(&app->scene);
	scene_init(&app->scene);
	scene_doc_clear(&engine->scene_doc);
	if (engine->script_vm_ready) {
		wren_vm_shutdown(&engine->script_vm);
		engine->script_vm_ready = 0;
	}
	if (engine->resources_ready) {
		resource_store_shutdown(&engine->resources);
		engine->resources_ready = 0;
	}
	resource_store_init(&engine->resources, engine->config.texture_vram_limit);
	engine->resources_ready = 1;
	if (!wren_vm_init(&engine->script_vm, engine)) {
		engine->script_vm_ready = 0;
	}
	else {
		engine->script_vm_ready = 1;
		wren_vm_bind_scene(&engine->script_vm, &app->scene);
	}

	snprintf(engine->scene_doc.path, sizeof(engine->scene_doc.path), "%s", path);
	snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%s", path);
	engine->scene_graph = &app->scene;
	nb_result_t result = scene_doc_load(&engine->scene_doc, path, engine, &app->scene);
	if (result != NB_RESULT_OK) {
		engine_log(engine, "editor scene open failed: %s: %s", path, nb_error_string(result));
		return result;
	}
	engine_log(engine, "editor scene opened: %s entities=%d", engine->scene_doc.path, app->scene.entity_count);
	return NB_RESULT_OK;
}

void editor_app_register(engine_t* engine) {
	if (!engine) { return; }
	engine->scene.context = NULL;
	engine->scene.init = editor_app_init;
	engine->scene.update = editor_app_update;
	engine->scene.render = editor_app_render;
	engine->scene.reset = editor_app_reset;
	engine->scene.shutdown = editor_app_shutdown;
}
