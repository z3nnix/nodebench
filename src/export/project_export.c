#define _XOPEN_SOURCE 700

#include "export/project_export.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asset/asset_db.h"
#include "core/dynamic_array.h"
#include "core/engine.h"
#include "core/file_io.h"
#include "core/sha256.h"
#include "resource/resource.h"
#include "scene/tscn.h"

typedef struct {
	char path[PATH_MAX];
	size_t size;
	uint8_t digest[32];
} export_entry_t;

static void export_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0) { return; }
	size_t half = size / 2 - 1;
	snprintf(out, size, "%.*s/%.*s", (int)half, a ? a : "", (int)half, b ? b : "");
}

static int export_mkdir_p(const char* path) {
	if (!path || !*path) { return 0; }

	char temp[PATH_MAX];
	snprintf(temp, sizeof(temp), "%s", path);

	for (char* p = temp + 1; *p; p++) {
		if (*p != '/') { continue; }
		*p = '\0';
		if (mkdir(temp, 0700) != 0 && errno != EEXIST) { return 0; }
		*p = '/';
	}

	if (mkdir(temp, 0700) != 0 && errno != EEXIST) { return 0; }

	return 1;
}

static void export_remove_tree(const char* path) {
	DIR* dir = opendir(path);
	if (!dir) { return; }

	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }

		char child[PATH_MAX];
		export_join(child, sizeof(child), path, item->d_name);
		struct stat info;
		if (stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
			export_remove_tree(child);
		}

		else {
			unlink(child);
		}
	}

	closedir(dir);
	rmdir(path);
}

static int export_skipped(const char* relative, const char* prefix) {
	if (!prefix[0]) { return 0; }

	size_t length = strlen(prefix);
	if (strncmp(relative, prefix, length) != 0) { return 0; }

	return relative[length] == '\0' || relative[length] == '/';
}

static int export_collect(const char* base, const char* directory, const char* relative, const char* skip_prefix, export_entry_t** entries, int* count, int* capacity) {
	DIR* dir = opendir(directory);
	if (!dir) { return 0; }

	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }

		char child_relative[PATH_MAX];
		if (relative[0]) {
			snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, item->d_name);
		}

		else {
			snprintf(child_relative, sizeof(child_relative), "%s", item->d_name);
		}

		if (export_skipped(child_relative, skip_prefix)) { continue; }

		char child_path[PATH_MAX];
		export_join(child_path, sizeof(child_path), base, child_relative);
		struct stat info;
		if (stat(child_path, &info) != 0) { continue; }

		if (S_ISDIR(info.st_mode)) {
			if (!export_collect(base, child_path, child_relative, skip_prefix, entries, count, capacity)) {
				closedir(dir);
				return 0;
			}

			continue;
		}

		if (!S_ISREG(info.st_mode)) { continue; }

		unsigned char* data = NULL;
		size_t size = 0;
		if (!nb_file_read_all(child_path, &data, &size)) {
			closedir(dir);
			return 0;
		}

		void* grown = nb_array_grow(*entries, capacity, *count + 1, sizeof(**entries));
		if (!grown) {
			free(data);
			closedir(dir);
			return 0;
		}

		*entries = grown;
		export_entry_t* entry = &(*entries)[(*count)++];
		snprintf(entry->path, sizeof(entry->path), "%s", child_relative);
		entry->size = size;
		sha256_compute(data, size, entry->digest);
		free(data);
	}

	closedir(dir);

	return 1;
}

static int export_entry_compare(const void* a, const void* b) {
	const export_entry_t* left = (const export_entry_t*)a;
	const export_entry_t* right = (const export_entry_t*)b;

	return strcmp(left->path, right->path);
}

static int export_copy_file(const char* source, const char* destination) {
	char parent[PATH_MAX];
	snprintf(parent, sizeof(parent), "%s", destination);
	char* slash = strrchr(parent, '/');
	if (slash) {
		*slash = '\0';
		if (!export_mkdir_p(parent)) { return 0; }
	}

	unsigned char* data = NULL;
	size_t size = 0;
	if (!nb_file_read_all(source, &data, &size)) { return 0; }

	FILE* file = fopen(destination, "wb");
	if (!file) {
		free(data);
		return 0;
	}

	int ok = fwrite(data, 1, size, file) == size;
	if (fclose(file) != 0) { ok = 0; }
	free(data);

	return ok;
}

static int export_json_string(FILE* file, const char* text) {
	if (fputc('"', file) == EOF) { return 0; }

	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		if (*p == '"' || *p == '\\') {
			if (fputc('\\', file) == EOF || fputc(*p, file) == EOF) { return 0; }
		}

		else if (*p < 0x20) {
			if (fprintf(file, "\\u%04x", *p) < 0) { return 0; }
		}

		else if (fputc(*p, file) == EOF) { return 0; }
	}

	return fputc('"', file) != EOF;
}

static int export_write_manifest(const char* path, const render_config_t* config, const export_entry_t* entries, int count) {
	FILE* file = fopen(path, "wb");
	if (!file) { return 0; }

	char digest[65];
	int ok = 1;

	if (fprintf(file, "{\n  \"version\": 1,\n  \"project\": ") < 0) { ok = 0; }
	if (ok) { ok = export_json_string(file, config->project_name); }
	if (ok && fprintf(file, ",\n  \"scene\": ") < 0) { ok = 0; }
	if (ok) { ok = export_json_string(file, config->scene_path); }
	if (ok && fprintf(file, ",\n  \"files\": [\n") < 0) { ok = 0; }

	for (int i = 0; ok && i < count; i++) {
		sha256_hex(entries[i].digest, digest);
		if (fprintf(file, "    {\n      \"path\": ") < 0) { ok = 0; }
		if (ok) { ok = export_json_string(file, entries[i].path); }
		if (ok && fprintf(file, ",\n      \"size\": %llu,\n      \"sha256\": \"%s\"\n    }%s\n", (unsigned long long)entries[i].size, digest, i + 1 < count ? "," : "") < 0) { ok = 0; }
	}

	if (ok && fprintf(file, "  ]\n}\n") < 0) { ok = 0; }
	if (ferror(file)) { ok = 0; }
	if (fclose(file) != 0) { ok = 0; }

	return ok;
}

nb_result_t project_export(engine_t* engine) {
	if (!engine || !engine->assets_ready || !engine->config.scene_path[0]) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	const char* root = engine->config.project_root[0] ? engine->config.project_root : ".";
	char root_abs[PATH_MAX];
	if (!realpath(root, root_abs)) { return NB_RESULT_ERROR_CONFIG; }

	const char* output = engine->config.export_dir[0] ? engine->config.export_dir : "build/export";
	if (!export_mkdir_p(output)) { return NB_RESULT_ERROR_RESOURCE; }

	char output_abs[PATH_MAX];
	if (!realpath(output, output_abs)) { return NB_RESULT_ERROR_RESOURCE; }
	if (strcmp(root_abs, output_abs) == 0) { return NB_RESULT_ERROR_CONFIG; }

	char skip_prefix[PATH_MAX] = "";
	size_t root_length = strlen(root_abs);
	if (strncmp(output_abs, root_abs, root_length) == 0 && output_abs[root_length] == '/') {
		snprintf(skip_prefix, sizeof(skip_prefix), "%s", output_abs + root_length + 1);
	}

	export_remove_tree(output_abs);
	if (!export_mkdir_p(output_abs)) { return NB_RESULT_ERROR_RESOURCE; }

	char scene_path[PATH_MAX];
	if (!asset_db_resolve(&engine->assets, engine->config.scene_path, scene_path, sizeof(scene_path))) {
		return NB_RESULT_ERROR_SCENE;
	}

	if (!engine->resources_ready) {
		resource_store_init(&engine->resources, engine->config.texture_vram_limit);
		engine->resources_ready = 1;
	}

	tscn_file_t scene_file;
	tscn_file_init(&scene_file);
	nb_result_t result = tscn_file_load(&scene_file, scene_path, engine);
	tscn_file_free(&scene_file);
	if (result != NB_RESULT_OK) { return NB_RESULT_ERROR_SCENE; }

	export_entry_t* entries = NULL;
	int count = 0;
	int capacity = 0;
	if (!export_collect(root_abs, root_abs, "", skip_prefix, &entries, &count, &capacity)) {
		nb_array_free(entries);
		return NB_RESULT_ERROR_RESOURCE;
	}

	qsort(entries, (size_t)count, sizeof(*entries), export_entry_compare);

	for (int i = 0; i < count; i++) {
		char source[PATH_MAX];
		char destination[PATH_MAX];
		export_join(source, sizeof(source), root_abs, entries[i].path);
		export_join(destination, sizeof(destination), output_abs, entries[i].path);
		if (!export_copy_file(source, destination)) {
			nb_array_free(entries);
			return NB_RESULT_ERROR_RESOURCE;
		}
	}

	char manifest[PATH_MAX];
	export_join(manifest, sizeof(manifest), output_abs, "manifest.json");
	if (!export_write_manifest(manifest, &engine->config, entries, count)) {
		nb_array_free(entries);
		return NB_RESULT_ERROR_RESOURCE;
	}

	engine_log(engine, "export ok dir=%s files=%d", engine->config.export_dir, count);
	nb_array_free(entries);

	return NB_RESULT_OK;
}
