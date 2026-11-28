#define _XOPEN_SOURCE 700

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asset/asset_db.h"
#include "core/engine.h"
#include "editor/editor.h"
#include "editor/editor_fs.h"
#include "resource/loader_tres.h"
#include "scene/scene.h"
#include "scene/scene_doc.h"

static int read_contains(const char* path, const char* needle) {
	char* data = NULL;
	size_t size = 0;
	if (!editor_fs_read_text(path, &data, &size)) { return 0; }
	int found = strstr(data, needle) != NULL;
	free(data);
	return found;
}

int main(void) {
	char tmpl[] = "/tmp/opencode/nb_resource_create_XXXXXX";
	char* root = mkdtemp(tmpl);
	if (!root) { return 1; }
	editor_t* editor = (editor_t*)calloc(1, sizeof(*editor));
	if (!editor) {
		editor_fs_delete_path(root);
		return 1;
	}
	engine_t engine;
	memset(&engine, 0, sizeof(engine));
	scene_t scene;
	memset(&scene, 0, sizeof(scene));
	scene_t script_scene;
	memset(&script_scene, 0, sizeof(script_scene));
	scene_doc_t script_doc;
	memset(&script_doc, 0, sizeof(script_doc));
	int ok = 1;
	char script_path[PATH_MAX];
	char material_path[PATH_MAX];
	char abs_script[PATH_MAX];
	char abs_material[PATH_MAX];
	char fallback_path[PATH_MAX];
	char scenes_dir[PATH_MAX];
	char script_scene_path[PATH_MAX];
	char logical[PATH_MAX];
	char cwd[PATH_MAX] = "";
	char* saved = NULL;
	entity_id_t entity = NB_ENTITY_INVALID;
	material_id old_material = NB_RESOURCE_INVALID_ID;

	if (!getcwd(cwd, sizeof(cwd))) { ok = 0; goto cleanup; }
	if (chdir(root) != 0) { ok = 0; goto cleanup; }
	if (!log_init(&engine.log, NULL, NB_LOG_INFO)) { ok = 0; goto cleanup; }
	render_config_init(&engine.config);
	snprintf(engine.config.mode, sizeof(engine.config.mode), "%s", "editor");
	snprintf(engine.config.project_root, sizeof(engine.config.project_root), "%.*s", (int)sizeof(engine.config.project_root) - 1, root);
	resource_store_init(&engine.resources, 1048576);
	engine.resources_ready = 1;
	if (!asset_db_init(&engine.assets, root)) { ok = 0; goto cleanup; }
	engine.assets_ready = 1;
	scene_doc_init(&engine.scene_doc);
	scene_init(&scene);
	engine.scene_graph = &scene;
	snprintf(editor->explorer.root, sizeof(editor->explorer.root), "%.*s", (int)sizeof(editor->explorer.root) - 1, root);
	snprintf(editor->explorer.current_dir, sizeof(editor->explorer.current_dir), "%.*s", (int)sizeof(editor->explorer.current_dir) - 1, root);
	editor->engine = &engine;

	if (!editor_create_script_asset(editor, &engine, "Probe", logical, sizeof(logical))) { ok = 0; goto cleanup; }
	if (strcmp(logical, "res://scripts/Probe.wren") != 0) { ok = 0; goto cleanup; }
	snprintf(script_path, sizeof(script_path), "%s/scripts/Probe.wren", root);
	snprintf(abs_script, sizeof(abs_script), "%.*s", (int)sizeof(abs_script) - 1, script_path);
	if (!logical[0] || !read_contains(abs_script, "class Probe is Node3D")) { ok = 0; goto cleanup; }
	if (editor_create_script_asset(editor, &engine, "../bad", logical, sizeof(logical))) { ok = 0; goto cleanup; }

	snprintf(scenes_dir, sizeof(scenes_dir), "%s/scenes", root);
	snprintf(script_scene_path, sizeof(script_scene_path), "%s/scenes/script_probe.tscn", root);
	if (!editor_fs_make_dir(scenes_dir)) { ok = 0; goto cleanup; }
	static const char* script_scene_data =
		"[gd_scene load_steps=2 format=3]\n"
		"[ext_resource type=\"WrenScript\" path=\"res://scripts/Probe.wren\" id=\"1_probe\"]\n"
		"[node name=\"Runner\" type=\"Node3D\" parent=\".\"]\n"
		"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
		"script = ExtResource(\"1_probe\")\n";
	if (!editor_fs_write_text(script_scene_path, script_scene_data, strlen(script_scene_data))) { ok = 0; goto cleanup; }
	if (scene_doc_load(&script_doc, "scenes/script_probe.tscn", &engine, &script_scene) != NB_RESULT_OK) { ok = 0; goto cleanup; }
	entity_id_t script_entity = scene_find_node(&script_scene, "Runner");
	node_t* script_node = scene_get_node(&script_scene, script_entity);
	if (!script_node || strcmp(script_node->script_module, "res://scripts/Probe.wren") != 0) { ok = 0; goto cleanup; }

	if (!editor_create_tres_asset(editor, &engine, "mat", logical, sizeof(logical))) { ok = 0; goto cleanup; }
	if (strcmp(logical, "res://assets/mat.tres") != 0) { ok = 0; goto cleanup; }
	snprintf(material_path, sizeof(material_path), "%s/assets/mat.tres", root);
	snprintf(abs_material, sizeof(abs_material), "%.*s", (int)sizeof(abs_material) - 1, material_path);
	if (!read_contains(abs_material, "type=\"Texture2D\"") || !read_contains(abs_material, "type=\"Palette\"")) { ok = 0; goto cleanup; }
	if (!editor_fs_exists(abs_script)) { ok = 0; goto cleanup; }

	snprintf(fallback_path, sizeof(fallback_path), "%s/assets/fallback.tres", root);
	static const char* fallback_data =
		"[gd_resource type=\"NBPS1Material\" load_steps=1 format=3]\n\n"
		"[resource]\n"
		"resource_name = \"fallback\"\n"
		"blend_mode = \"add\"\n";
	if (!editor_fs_write_text(fallback_path, fallback_data, strlen(fallback_data))) { ok = 0; goto cleanup; }
	material_id fallback_material = material_load_tres(&engine, fallback_path);
	const material_t* fallback_res = resource_get_material(&engine.resources, fallback_material);
	texture_t* fallback_texture = fallback_res ? resource_get_texture(&engine.resources, fallback_res->texture) : NULL;
	if (fallback_material == NB_RESOURCE_INVALID_ID || !fallback_texture) { ok = 0; goto cleanup; }
	if (fallback_texture->width != 64 || fallback_texture->height != 64 || fallback_texture->format != NB_TEXTURE_4BIT) { ok = 0; goto cleanup; }
	if (fallback_texture->palette[0] == fallback_texture->palette[2] || fallback_texture->palette[2] == fallback_texture->palette[3]) { ok = 0; goto cleanup; }
	if (((fallback_texture->pixels[0] >> 4) & 15) != 5) { ok = 0; goto cleanup; }
	if (((fallback_texture->pixels[4 * 32 + 4] >> 4) & 15) != 2) { ok = 0; goto cleanup; }

	old_material = material_load_tres(&engine, abs_material);
	if (old_material == NB_RESOURCE_INVALID_ID) { ok = 0; goto cleanup; }
	entity = scene_create_entity(&scene);
	mesh_renderer_t renderer;
	memset(&renderer, 0, sizeof(renderer));
	renderer.material = old_material;
	if (scene_add_mesh_renderer(&scene, entity, renderer) != 1) { ok = 0; goto cleanup; }
	scene_doc_ext_t ext;
	memset(&ext, 0, sizeof(ext));
	snprintf(ext.id, sizeof(ext.id), "%s", "1");
	snprintf(ext.type, sizeof(ext.type), "%s", "NBPS1Material");
	snprintf(ext.path, sizeof(ext.path), "%.*s", (int)sizeof(ext.path) - 1, "res://assets/mat.tres");
	ext.kind = NB_RESOURCE_KIND_MATERIAL;
	ext.handle = old_material;
	if (!scene_doc_add_ext(&engine.scene_doc, &ext)) { ok = 0; goto cleanup; }

	editor_tres_state_t* state = &editor->inspector_tres;
	state->ext_count = 2;
	snprintf(state->ext[0].id, sizeof(state->ext[0].id), "%s", "1_tex");
	snprintf(state->ext[0].type, sizeof(state->ext[0].type), "%s", "Texture2D");
	snprintf(state->ext[0].path, sizeof(state->ext[0].path), "%s", "res://assets/default_texture.ppm");
	snprintf(state->ext[1].id, sizeof(state->ext[1].id), "%s", "2_pal");
	snprintf(state->ext[1].type, sizeof(state->ext[1].type), "%s", "Palette");
	snprintf(state->ext[1].path, sizeof(state->ext[1].path), "%s", "res://assets/default_palette.clut");
	snprintf(state->name, sizeof(state->name), "%s", "mat");
	snprintf(state->texture, sizeof(state->texture), "%s", "1_tex");
	snprintf(state->palette, sizeof(state->palette), "%s", "2_pal");
	state->blend_mode = (int)NB_BLEND_ADD;
	state->shade_mode = (int)NB_SHADE_GOURAUD;
	state->fog_enabled = 1;
	state->tint[0] = 1.0f;
	state->tint[1] = 1.0f;
	state->tint[2] = 1.0f;
	state->tint[3] = 1.0f;
	state->transparent_index = 65535;
	state->runtime_material = old_material;
	editor->inspector_kind = NB_EDITOR_INSPECTOR_TRES;
	snprintf(editor->inspector_path, sizeof(editor->inspector_path), "%.*s", (int)sizeof(editor->inspector_path) - 1, abs_material);
	editor->inspector_dirty = 1;

	editor_shutdown(editor, &engine);
	if (!read_contains(abs_material, "blend_mode = \"add\"")) { ok = 0; goto cleanup; }
	node_t* node = scene_get_node(&scene, entity);
	if (!node || node->mesh_renderer.material == old_material || node->mesh_renderer.material == NB_RESOURCE_INVALID_ID) { ok = 0; goto cleanup; }
	material_id next_material = node->mesh_renderer.material;
	const material_t* material = resource_get_material(&engine.resources, next_material);
	if (!material || material->blend_mode != NB_BLEND_ADD) { ok = 0; goto cleanup; }
	if (resource_get_material(&engine.resources, old_material) != NULL) { ok = 0; goto cleanup; }
	if (engine.scene_doc.ext_count != 1 || engine.scene_doc.ext[0].handle != next_material) { ok = 0; goto cleanup; }

cleanup:
	if (cwd[0]) { chdir(cwd); }
	if (!ok) {
		printf("fail resource create\n");
	}
	free(saved);
	if (editor) {
		free(editor);
	}
	scene_shutdown(&scene);
	scene_shutdown(&script_scene);
	scene_doc_clear(&script_doc);
	engine.scene_graph = NULL;
	asset_db_shutdown(&engine.assets);
	resource_store_shutdown(&engine.resources);
	scene_doc_clear(&engine.scene_doc);
	log_shutdown(&engine.log);
	editor_fs_delete_path(root);
	if (!ok) { return 1; }
	printf("resource create test ok\n");
	return 0;
}
