#include "test/fixture_project.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asset/asset_db.h"

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

	char path[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_resolve(&db, "res://models/cube.obj", path, sizeof(path)) || access(path, F_OK) != 0) {
		return 1;
	}

	char absolute[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_resolve(&db, "res://models/cube.obj", absolute, sizeof(absolute))) { return 1; }
	if (asset_db_resolve(&db, absolute, absolute, sizeof(absolute)) != NULL) { return 1; }
	unsigned char* denied = NULL;
	size_t denied_size = 0;
	if (asset_db_read_all(&db, absolute, &denied, &denied_size)) { return 1; }

	const char* uuid = asset_db_uuid(&db, "res://models/cube.obj");
	if (!uuid || strlen(uuid) != NB_ASSET_DB_UUID_SIZE - 1) {
		return 1;
	}

	char first_uuid[NB_ASSET_DB_UUID_SIZE];
	snprintf(first_uuid, sizeof(first_uuid), "%s", uuid);

	char relative[NB_ASSET_DB_PATH_MAX];
	if (!asset_db_path_for_uuid(&db, uuid, relative, sizeof(relative)) || strcmp(relative, "models/cube.obj") != 0) {
		return 1;
	}

	if (!asset_db_import(&db, "res://models/cube.obj")) {
		return 1;
	}

	if (!asset_db_import(&db, "res://models/cube.obj")) {
		return 1;
	}

	if (!asset_db_refresh(&db)) {
		return 1;
	}

	uuid = asset_db_uuid(&db, "res://models/cube.obj");
	if (!uuid || strcmp(uuid, first_uuid) != 0) {
		return 1;
	}

	if (!asset_db_import(&db, "res://models/cube.obj")) {
		return 1;
	}

	asset_db_shutdown(&db);
	if (chdir(cwd) != 0) {
		return 1;
	}
	nb_fixture_destroy(root);
	printf("asset db test ok\n");

	return 0;
}
