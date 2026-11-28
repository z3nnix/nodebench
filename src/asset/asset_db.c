#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "asset/asset_db.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/dynamic_array.h"
#include "core/file_io.h"

static int asset_db_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0 || !a || !b) {
		return 0;
	}

	size_t a_length = strlen(a);
	int slash = a_length > 0 && a[a_length - 1] != '/';
	int written = snprintf(out, size, "%s%s%s", a, slash ? "/" : "", b);

	return written >= 0 && (size_t)written < size;
}

static int asset_db_skip_name(const char* name) {
	if (!name || !*name || name[0] == '.') {
		return 1;
	}

	if (strcmp(name, "build") == 0 || strcmp(name, "third_party") == 0 || strcmp(name, "node_modules") == 0) {
		return 1;
	}

	return 0;
}

static int asset_db_read_uuid(const char* meta_path, char* uuid, size_t size) {
	FILE* file = fopen(meta_path, "r");
	if (!file) {
		return 0;
	}

	char line[128];
	while (fgets(line, sizeof(line), file)) {
		if (strncmp(line, "uuid=", 5) != 0) {
			continue;
		}

		char* value = line + 5;
		while (*value == ' ' || *value == '\t') {
			value++;
		}

		char* end = value + strlen(value);
		while (end > value && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
			end--;
		}

		*end = '\0';
		fclose(file);

		if (strlen(value) != NB_ASSET_DB_UUID_SIZE - 1) {
			return 0;
		}

		snprintf(uuid, size, "%s", value);

		return 1;
	}

	fclose(file);

	return 0;
}

static void asset_db_generate_uuid(char* uuid, size_t size) {
	unsigned char bytes[16];
	FILE* random = fopen("/dev/urandom", "rb");

	if (random && fread(bytes, 1, sizeof(bytes), random) == sizeof(bytes)) {
		fclose(random);
	}

	else {
		if (random) {
			fclose(random);
		}

		unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)bytes ^ (unsigned int)rand();
		for (int i = 0; i < 16; i++) {
			seed = seed * 1103515245u + 12345u;
			bytes[i] = (unsigned char)(seed >> 16);
		}
	}

	bytes[6] = (unsigned char)((bytes[6] & 0x0fu) | 0x40u);
	bytes[8] = (unsigned char)((bytes[8] & 0x3fu) | 0x80u);

	snprintf(uuid, size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		bytes[0], bytes[1], bytes[2], bytes[3],
		bytes[4], bytes[5], bytes[6], bytes[7],
		bytes[8], bytes[9], bytes[10], bytes[11],
		bytes[12], bytes[13], bytes[14], bytes[15]);
}

int asset_db_ensure_meta(const asset_db_t* db, const char* path, char* uuid_out, size_t size) {
	if (!db || !path || !uuid_out || size < NB_ASSET_DB_UUID_SIZE) {
		return 0;
	}

	char meta_path[NB_ASSET_DB_PATH_MAX];
	int written = snprintf(meta_path, sizeof(meta_path), "%s.meta", path);
	if (written < 0 || (size_t)written >= sizeof(meta_path)) {
		return 0;
	}

	char uuid[NB_ASSET_DB_UUID_SIZE];
	if (asset_db_read_uuid(meta_path, uuid, sizeof(uuid))) {
		snprintf(uuid_out, size, "%s", uuid);

		return 1;
	}

	asset_db_generate_uuid(uuid, sizeof(uuid));

	FILE* file = fopen(meta_path, "w");
	if (!file) {
		return 0;
	}

	fprintf(file, "uuid=%s\n", uuid);
	fclose(file);
	snprintf(uuid_out, size, "%s", uuid);

	return 1;
}

static asset_db_entry_t* asset_db_find(asset_db_t* db, const char* relative) {
	if (!db || !relative) {
		return NULL;
	}

	for (int i = 0; i < db->count; i++) {
		if (strcmp(db->entries[i].path, relative) == 0) {
			return &db->entries[i];
		}
	}

	return NULL;
}

static void asset_db_update_stat(asset_db_entry_t* entry, const char* full) {
	struct stat status;
	if (stat(full, &status) != 0 || !S_ISREG(status.st_mode)) {
		return;
	}

	entry->mtime = (int64_t)status.st_mtime;
	entry->size = (int64_t)status.st_size;
}

static int asset_db_add(asset_db_t* db, const char* relative, const char* full) {
	if (!db || !relative || !full) {
		return 0;
	}

	asset_db_entry_t* entry = asset_db_find(db, relative);
	if (entry) {
		entry->seen = 1;
		asset_db_update_stat(entry, full);
		asset_db_ensure_meta(db, full, entry->uuid, sizeof(entry->uuid));

		return 1;
	}

	void* entries = nb_array_grow(db->entries, &db->capacity, db->count + 1, sizeof(*db->entries));
	if (!entries) {
		return 0;
	}

	db->entries = entries;
	entry = &db->entries[db->count];
	memset(entry, 0, sizeof(*entry));
	snprintf(entry->path, sizeof(entry->path), "%s", relative);
	entry->seen = 1;
	asset_db_update_stat(entry, full);
	asset_db_ensure_meta(db, full, entry->uuid, sizeof(entry->uuid));
	db->count++;

	return 1;
}

static int asset_db_scan(asset_db_t* db, const char* relative_dir) {
	char full_dir[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_join(full_dir, sizeof(full_dir), db->root, relative_dir)) {
		return 0;
	}

	DIR* dir = opendir(full_dir);
	if (!dir) {
		return 0;
	}

	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (asset_db_skip_name(item->d_name)) {
			continue;
		}

		size_t name_length = strlen(item->d_name);
		if (name_length >= 5 && strcmp(item->d_name + name_length - 5, ".meta") == 0) {
			continue;
		}

		char relative[NB_ASSET_DB_PATH_MAX];
		if (relative_dir[0]) {
			snprintf(relative, sizeof(relative), "%s/%s", relative_dir, item->d_name);
		}

		else {
			snprintf(relative, sizeof(relative), "%s", item->d_name);
		}

		char full[NB_ASSET_DB_PATH_MAX];
		if (!asset_db_join(full, sizeof(full), db->root, relative)) {
			continue;
		}

		struct stat status;
		if (stat(full, &status) != 0) {
			continue;
		}

		if (S_ISDIR(status.st_mode)) {
			asset_db_scan(db, relative);
		}

		else if (S_ISREG(status.st_mode)) {
			asset_db_add(db, relative, full);
		}
	}

	closedir(dir);

	return 1;
}

int asset_db_init(asset_db_t* db, const char* root) {
	if (!db || !root || !root[0]) {
		return 0;
	}

	memset(db, 0, sizeof(*db));

	char resolved[NB_ASSET_DB_PATH_MAX];
	if (!realpath(root, resolved)) {
		snprintf(resolved, sizeof(resolved), "%s", root);
	}

	snprintf(db->root, sizeof(db->root), "%s", resolved);
	asset_db_scan(db, "");
	db->ready = 1;

	return 1;
}

void asset_db_shutdown(asset_db_t* db) {
	if (!db) {
		return;
	}

	nb_array_free(db->entries);
	memset(db, 0, sizeof(*db));
}

int asset_db_refresh(asset_db_t* db) {
	if (!db || !db->ready) {
		return 0;
	}

	for (int i = 0; i < db->count; i++) {
		db->entries[i].seen = 0;
	}

	asset_db_scan(db, "");

	int kept = 0;
	for (int i = 0; i < db->count; i++) {
		if (!db->entries[i].seen) {
			continue;
		}

		if (kept != i) {
			db->entries[kept] = db->entries[i];
		}

		kept++;
	}

	for (int i = kept; i < db->count; i++) {
		memset(&db->entries[i], 0, sizeof(db->entries[i]));
	}

	db->count = kept;

	return 1;
}

static int asset_db_try_candidate(const asset_db_t* db, const char* candidate, char* out, size_t size) {
	if (!candidate[0]) {
		return 0;
	}

	for (int i = 0; i < db->count; i++) {
		if (strcmp(db->entries[i].path, candidate) == 0) {
			return asset_db_join(out, size, db->root, db->entries[i].path);
		}
	}

	char path[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_join(path, sizeof(path), db->root, candidate) || access(path, F_OK) != 0) {
		return 0;
	}

	snprintf(out, size, "%s", path);

	return 1;
}

const char* asset_db_resolve(const asset_db_t* db, const char* path, char* out, size_t size) {
	if (!db || !db->ready || !path || !out || size == 0) {
		return NULL;
	}

	if (path[0] == '/') {
		return NULL;
	}

	const char* logical = path;
	if (strncmp(logical, "res://", 6) == 0) {
		logical += 6;
	}

	char candidate[NB_ASSET_DB_PATH_MAX];

	if (asset_db_try_candidate(db, logical, out, size)) {
		return out;
	}

	snprintf(candidate, sizeof(candidate), "assets/%s", logical);
	if (asset_db_try_candidate(db, candidate, out, size)) {
		return out;
	}

	return NULL;
}

int asset_db_read_all(const asset_db_t* db, const char* path, unsigned char** out, size_t* size) {
	if (!path || !out || !size) {
		return 0;
	}

	char resolved[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_resolve(db, path, resolved, sizeof(resolved))) {
		return 0;
	}

	return nb_file_read_all(resolved, out, size);
}

int asset_db_import(asset_db_t* db, const char* path) {
	if (!db || !path) {
		return 0;
	}

	char full[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_resolve(db, path, full, sizeof(full))) {
		return 0;
	}

	asset_db_entry_t* entry = NULL;
	for (int i = 0; i < db->count; i++) {
		char candidate[NB_ASSET_DB_PATH_MAX];
		if (!asset_db_join(candidate, sizeof(candidate), db->root, db->entries[i].path)) {
			continue;
		}

		if (strcmp(candidate, full) == 0) {
			entry = &db->entries[i];
			break;
		}
	}

	if (!entry) {
		return 0;
	}

	struct stat status;
	if (stat(full, &status) != 0 || !S_ISREG(status.st_mode)) {
		return 0;
	}

	int64_t mtime = (int64_t)status.st_mtime;
	int64_t size = (int64_t)status.st_size;
	if (entry->imported && entry->mtime == mtime && entry->size == size) {
		return 1;
	}

	entry->mtime = mtime;
	entry->size = size;
	entry->imported = 1;

	return 1;
}

const char* asset_db_path_for_uuid(const asset_db_t* db, const char* uuid, char* out, size_t size) {
	if (!db || !uuid || !out || size == 0) {
		return NULL;
	}

	for (int i = 0; i < db->count; i++) {
		if (strcmp(db->entries[i].uuid, uuid) != 0) {
			continue;
		}

		snprintf(out, size, "%s", db->entries[i].path);

		return out;
	}

	return NULL;
}

const char* asset_db_uuid(const asset_db_t* db, const char* path) {
	if (!db || !path) {
		return NULL;
	}

	char resolved[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_resolve(db, path, resolved, sizeof(resolved))) {
		return NULL;
	}

	for (int i = 0; i < db->count; i++) {
		char full[NB_ASSET_DB_PATH_MAX];
		if (!asset_db_join(full, sizeof(full), db->root, db->entries[i].path)) {
			continue;
		}

		if (strcmp(full, resolved) == 0) {
			return db->entries[i].uuid;
		}
	}

	return NULL;
}
