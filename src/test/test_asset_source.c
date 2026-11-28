#include "test/fixture_project.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asset/asset_db.h"
#include "audio/audio.h"
#include "core/config.h"
#include "core/engine.h"
#include "core/file_io.h"
#include "resource/loader.h"
#include "resource/loader_tres.h"
#include "resource/resource.h"
#include "scene/tscn.h"
#include "script/script.h"

static int read_via_db(const asset_db_t* db, const char* path, unsigned char** data, size_t* size) {
	return asset_db_read_all(db, path, data, size);
}

static int mesh_equal(const mesh_t* a, const mesh_t* b) {
	if (a->vertex_count != b->vertex_count || a->triangle_count != b->triangle_count) {
		return 0;
	}

	for (int i = 0; i < a->vertex_count; i++) {
		if (fabs(a->vertices[i].position.x - b->vertices[i].position.x) > 0.0001f) {
			return 0;
		}

		if (fabs(a->vertices[i].position.y - b->vertices[i].position.y) > 0.0001f) {
			return 0;
		}

		if (fabs(a->vertices[i].position.z - b->vertices[i].position.z) > 0.0001f) {
			return 0;
		}
	}

	return 1;
}

int main(void) {
	char cwd[PATH_MAX];
	char root[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd)) || !nb_fixture_create(root, sizeof(root)) || chdir(root) != 0) {
		return 1;
	}

	asset_db_t db;
	if (!asset_db_init(&db, ".") || db.count <= 0) {
		return 1;
	}

	const char* paths[] = {
		"res://models/cube.obj",
		"res://assets/tile.tres",
		"res://scripts/player.wren",
		"res://assets/blip.wav",
		"res://scenes/level01.tscn"
	};

	for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		char direct[PATH_MAX];
		unsigned char* from_db = NULL;
		unsigned char* from_disk = NULL;
		size_t db_size = 0;
		size_t disk_size = 0;
		if (!asset_db_resolve(&db, paths[i], direct, sizeof(direct))) {
			return 1;
		}

		if (!read_via_db(&db, paths[i], &from_db, &db_size) || !nb_file_read_all(direct, &from_disk, &disk_size)) {
			free(from_db);
			free(from_disk);
			return 1;
		}

		int same = db_size == disk_size && memcmp(from_db, from_disk, db_size) == 0;
		free(from_db);
		free(from_disk);
		if (!same) {
			return 1;
		}
	}

	mesh_t mesh_disk;
	mesh_t mesh_mem;
	unsigned char* bytes = NULL;
	size_t size = 0;
	if (!obj_load("models/cube.obj", &mesh_disk) || !read_via_db(&db, "res://models/cube.obj", &bytes, &size) || !obj_load_mem(bytes, size, &mesh_mem)) {
		free(bytes);
		return 1;
	}

	int mesh_ok = mesh_equal(&mesh_disk, &mesh_mem);
	mesh_destroy(&mesh_disk);
	mesh_destroy(&mesh_mem);
	free(bytes);
	if (!mesh_ok) {
		return 1;
	}

	char* script_disk = script_load_file("scripts/player.wren");
	bytes = NULL;
	if (!read_via_db(&db, "res://scripts/player.wren", &bytes, &size)) {
		free(script_disk);
		return 1;
	}

	char* script_mem = script_load_mem(bytes, size);
	free(bytes);
	int script_ok = script_disk && script_mem && strcmp(script_disk, script_mem) == 0;
	free(script_disk);
	free(script_mem);
	if (!script_ok) {
		return 1;
	}

	audio_stream_t wav_disk;
	audio_stream_t wav_mem;
	bytes = NULL;
	if (!wav_load("assets/blip.wav", &wav_disk) || !read_via_db(&db, "res://assets/blip.wav", &bytes, &size) || !wav_load_mem(bytes, size, &wav_mem)) {
		free(bytes);
		return 1;
	}

	int wav_ok = wav_disk.sample_rate == wav_mem.sample_rate && wav_disk.channels == wav_mem.channels && wav_disk.frame_count == wav_mem.frame_count && wav_disk.samples[1] == wav_mem.samples[1];
	audio_stream_free(&wav_disk);
	audio_stream_free(&wav_mem);
	free(bytes);
	if (!wav_ok) {
		return 1;
	}

	render_config_t config_disk;
	render_config_t config_mem;
	render_config_init(&config_disk);
	render_config_init(&config_mem);
	bytes = NULL;
	if (!config_load_file(&config_disk, "project.ini") || !nb_file_read_all("project.ini", &bytes, &size) || !config_load_buffer(&config_mem, (const char*)bytes, size)) {
		free(bytes);
		return 1;
	}

	int config_ok = strcmp(config_disk.scene_path, config_mem.scene_path) == 0 && strcmp(config_disk.mode, config_mem.mode) == 0;
	free(bytes);
	if (!config_ok) {
		return 1;
	}

	engine_t engine;
	memset(&engine, 0, sizeof(engine));
	render_config_init(&engine.config);
	if (!asset_db_init(&engine.assets, ".")) {
		return 1;
	}
	engine.assets_ready = 1;
	resource_store_init(&engine.resources, engine.config.texture_vram_limit);
	engine.resources_ready = 1;

	bytes = NULL;
	if (!read_via_db(&db, "res://assets/tile.tres", &bytes, &size)) {
		return 1;
	}

	material_id material_disk = material_load_tres(&engine, "assets/tile.tres");
	material_id material_mem = material_load_tres_mem(&engine, bytes, size);
	free(bytes);
	if (material_disk == NB_RESOURCE_INVALID_ID || material_mem == NB_RESOURCE_INVALID_ID) {
		return 1;
	}

	tscn_file_t scene_disk;
	tscn_file_t scene_mem;
	tscn_file_init(&scene_disk);
	tscn_file_init(&scene_mem);
	bytes = NULL;
	nb_result_t result_disk = tscn_file_load(&scene_disk, "scenes/level01.tscn", &engine);
	if (result_disk == NB_RESULT_OK) {
		result_disk = read_via_db(&db, "res://scenes/level01.tscn", &bytes, &size) ? NB_RESULT_OK : NB_RESULT_ERROR_RESOURCE;
	}

	nb_result_t result_mem = result_disk == NB_RESULT_OK ? tscn_file_load_from_buffer(&scene_mem, "scenes/level01.tscn", (const char*)bytes, size, &engine) : result_disk;
	free(bytes);

	int scene_ok = result_disk == NB_RESULT_OK && result_mem == NB_RESULT_OK && scene_disk.ext_count == scene_mem.ext_count && scene_disk.node_count == scene_mem.node_count;
	if (scene_ok) {
		for (int i = 0; i < scene_disk.node_count; i++) {
			if (strcmp(scene_disk.nodes[i].name, scene_mem.nodes[i].name) != 0 || strcmp(scene_disk.nodes[i].type, scene_mem.nodes[i].type) != 0) {
				scene_ok = 0;
				break;
			}
		}
	}

	tscn_file_free(&scene_disk);
	tscn_file_free(&scene_mem);
	resource_store_shutdown(&engine.resources);
	asset_db_shutdown(&engine.assets);
	asset_db_shutdown(&db);
	if (!scene_ok || chdir(cwd) != 0) {
		return 1;
	}

	nb_fixture_destroy(root);
	printf("asset source test ok\n");

	return 0;
}
