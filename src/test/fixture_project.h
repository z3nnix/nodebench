#ifndef NB_TEST_FIXTURE_PROJECT_H
#define NB_TEST_FIXTURE_PROJECT_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NB_TEST_FIXTURE_MESH_UUID "11111111-1111-4111-8111-111111111111"
#define NB_TEST_FIXTURE_MATERIAL_UUID "22222222-2222-4222-8222-222222222222"
#define NB_TEST_FIXTURE_PLAYER_UUID "33333333-3333-4333-8333-333333333333"
#define NB_TEST_FIXTURE_LEVEL_UUID "44444444-4444-4444-8444-444444444444"
#define NB_TEST_FIXTURE_AUDIO_UUID "55555555-5555-4555-8555-555555555555"

static void nb_fixture_remove_tree(const char* path) {
	DIR* dir = opendir(path);
	if (!dir) {
		return;
	}

	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
			continue;
		}

		char child[PATH_MAX];
		snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
		struct stat info;
		if (stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
			nb_fixture_remove_tree(child);
		}
		else {
			unlink(child);
		}
	}

	closedir(dir);
	rmdir(path);
}

static int nb_fixture_write(const char* path, const char* text) {
	FILE* file = fopen(path, "wb");
	if (!file) {
		return 0;
	}

	size_t length = strlen(text);
	int ok = fwrite(text, 1, length, file) == length;
	if (fclose(file) != 0) {
		ok = 0;
	}

	return ok;
}

static int nb_fixture_write_bytes(const char* path, const void* data, size_t size) {
	FILE* file = fopen(path, "wb");
	if (!file) {
		return 0;
	}

	int ok = fwrite(data, 1, size, file) == size;
	if (fclose(file) != 0) {
		ok = 0;
	}

	return ok;
}

static int nb_fixture_create(char* out, size_t size) {
	char tmpl[] = "/tmp/nb_fixture_XXXXXX";
	char* root = mkdtemp(tmpl);
	if (!root || size == 0) {
		return 0;
	}

	int written = snprintf(out, size, "%s", root);
	if (written < 0 || (size_t)written >= size) {
		nb_fixture_remove_tree(root);
		return 0;
	}

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/models", root);
	if (mkdir(path, 0700) != 0) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/assets", root);
	if (mkdir(path, 0700) != 0) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scripts", root);
	if (mkdir(path, 0700) != 0) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scenes", root);
	if (mkdir(path, 0700) != 0) {
		nb_fixture_remove_tree(root);
		return 0;
	}

	const char* project_ini =
		"mode = editor\n"
		"scene_path = scenes/level01.tscn\n";
	const char* mesh =
		"v -1.0 -1.0 0.0\n"
		"v 1.0 -1.0 0.0\n"
		"v 1.0 1.0 0.0\n"
		"v -1.0 1.0 0.0\n"
		"vt 0.0 0.0\n"
		"vt 1.0 0.0\n"
		"vt 1.0 1.0\n"
		"vt 0.0 1.0\n"
		"f 1/1 2/2 3/3\n"
		"f 1/1 3/3 4/4\n";
	const char* material =
		"[gd_resource type=\"NBPS1Material\" load_steps=1 format=3]\n"
		"\n"
		"[resource]\n"
		"resource_name = \"tile.tres\"\n";
	const char* player_script =
		"import \"nodebench\" for Node3D\n"
		"\n"
		"class FixturePlayer is Node3D {\n"
		"  construct new(id) {\n"
		"    super(id)\n"
		"  }\n"
		"}\n";
	const char* level_script =
		"import \"nodebench\" for Node3D\n"
		"\n"
		"class FixtureLevel is Node3D {\n"
		"  construct new(id) {\n"
		"    super(id)\n"
		"  }\n"
		"}\n";
	const unsigned char wav[48] = {
		'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x22, 0x56, 0, 0, 0x44, 0xAC, 0, 0, 2, 0, 16, 0,
		'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 0, 0xE8, 0x03
	};
	const char* scene =
		"[gd_scene load_steps=5 format=3]\n"
		"\n"
		"[ext_resource type=\"Mesh\" path=\"res://models/cube.obj\" uuid=\"" NB_TEST_FIXTURE_MESH_UUID "\" id=\"mesh_cube\"]\n"
		"[ext_resource type=\"NBPS1Material\" path=\"res://assets/tile.tres\" uuid=\"" NB_TEST_FIXTURE_MATERIAL_UUID "\" id=\"mat_ball\"]\n"
		"[ext_resource type=\"WrenScript\" path=\"res://scripts/player.wren\" uuid=\"" NB_TEST_FIXTURE_PLAYER_UUID "\" id=\"script_player\"]\n"
		"[ext_resource type=\"WrenScript\" path=\"res://scripts/game.wren\" uuid=\"" NB_TEST_FIXTURE_LEVEL_UUID "\" id=\"script_game\"]\n"
		"\n"
		"[node name=\"Level\" type=\"Node3D\" parent=\".\"]\n"
		"script = ExtResource(\"script_game\")\n"
		"\n"
		"[node name=\"Camera\" type=\"Camera3D\" parent=\"Level\"]\n"
		"transform = Transform3D(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 3.0)\n"
		"fov = 60.0\n"
		"near = 0.05\n"
		"far = 100.0\n"
		"target = Vector3(0.0, 0.5, 0.0)\n"
		"up = Vector3(0.0, 1.0, 0.0)\n"
		"\n"
		"[node name=\"Sun\" type=\"DirectionalLight3D\" parent=\"Level\"]\n"
		"direction = Vector3(0.0, -1.0, 0.0)\n"
		"intensity = 1.0\n"
		"\n"
		"[node name=\"Player\" type=\"MeshInstance3D\" parent=\"Level\"]\n"
		"transform = Transform3D(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)\n"
		"mesh = ExtResource(\"mesh_cube\")\n"
		"material = ExtResource(\"mat_ball\")\n"
		"script = ExtResource(\"script_player\")\n";

	snprintf(path, sizeof(path), "%s/project.ini", root);
	if (!nb_fixture_write(path, project_ini)) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/models/cube.obj", root);
	if (!nb_fixture_write(path, mesh)) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/models/cube.obj.meta", root);
	if (!nb_fixture_write(path, "uuid=" NB_TEST_FIXTURE_MESH_UUID "\n")) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/assets/tile.tres", root);
	if (!nb_fixture_write(path, material)) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/assets/tile.tres.meta", root);
	if (!nb_fixture_write(path, "uuid=" NB_TEST_FIXTURE_MATERIAL_UUID "\n")) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scripts/player.wren", root);
	if (!nb_fixture_write(path, player_script)) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scripts/player.wren.meta", root);
	if (!nb_fixture_write(path, "uuid=" NB_TEST_FIXTURE_PLAYER_UUID "\n")) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scripts/game.wren", root);
	if (!nb_fixture_write(path, level_script)) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scripts/game.wren.meta", root);
	if (!nb_fixture_write(path, "uuid=" NB_TEST_FIXTURE_LEVEL_UUID "\n")) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/assets/blip.wav", root);
	if (!nb_fixture_write_bytes(path, wav, sizeof(wav))) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/assets/blip.wav.meta", root);
	if (!nb_fixture_write(path, "uuid=" NB_TEST_FIXTURE_AUDIO_UUID "\n")) {
		nb_fixture_remove_tree(root);
		return 0;
	}
	snprintf(path, sizeof(path), "%s/scenes/level01.tscn", root);
	if (!nb_fixture_write(path, scene)) {
		nb_fixture_remove_tree(root);
		return 0;
	}

	return 1;
}

static void nb_fixture_destroy(const char* root) {
	if (root && root[0]) {
		nb_fixture_remove_tree(root);
	}
}

#endif
