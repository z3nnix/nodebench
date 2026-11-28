#define _XOPEN_SOURCE 700

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "core/config.h"
#include "editor/launcher.h"

static void test_remove_tree(const char* path) {
	DIR* dir = opendir(path);
	if (!dir) { return; }
	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }
		char child[PATH_MAX];
		snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
		struct stat st;
		if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
			test_remove_tree(child);
		}
		else {
			unlink(child);
		}
	}
	closedir(dir);
	rmdir(path);
}

static int test_find_index(const launcher_t* launcher, const char* name) {
	for (int i = 0; i < launcher->recent_count; i++) {
		if (strcmp(launcher->recent[i].name, name) == 0) { return i; }
	}
	return -1;
}

static void test_cli_detection(void) {
	char arg0[] = "nodebench";
	char mode_game[] = "--mode=game";
	char mode_editor[] = "--mode=editor";
	char mode_launcher[] = "--mode=launcher";
	char scene[] = "--scene=demo.tscn";
	char project[] = "--project=/tmp/demo";

	char* none[] = { arg0, NULL };
	assert(config_cli_selects_session(1, none) == 0);
	assert(config_cli_requests_launcher(1, none) == 0);

	char* launcher[] = { arg0, mode_launcher, NULL };
	assert(config_cli_selects_session(2, launcher) == 0);
	assert(config_cli_requests_launcher(2, launcher) == 1);

	char* game[] = { arg0, mode_game, NULL };
	assert(config_cli_selects_session(2, game) == 1);
	assert(config_cli_requests_launcher(2, game) == 0);

	char* editor[] = { arg0, mode_editor, NULL };
	assert(config_cli_selects_session(2, editor) == 1);

	char* scene_only[] = { arg0, scene, NULL };
	assert(config_cli_selects_session(2, scene_only) == 1);

	char* project_only[] = { arg0, project, NULL };
	assert(config_cli_selects_session(2, project_only) == 1);

	const char* ini = "/tmp/nodebench_launcher_mode_test.ini";
	FILE* file = fopen(ini, "wb");
	assert(file != NULL);
	fprintf(file, "mode = launcher\n");
	fclose(file);
	render_config_t config;
	render_config_init(&config);
	assert(strcmp(config.mode, "game") == 0);
	assert(config_load_file(&config, ini) == 1);
	assert(strcmp(config.mode, "launcher") == 0);
	unlink(ini);
}

static void test_recent_and_spawn(void) {
	char root[] = "/tmp/nodebench_launcher_test_XXXXXX";
	char* parent = mkdtemp(root);
	assert(parent != NULL);
	launcher_recent_t entry;
	memset(&entry, 0, sizeof(entry));
	assert(launcher_create_project(parent, "demo", &entry) == 1);
	assert(entry.root[0] == '/');
	assert(entry.scene[0] == '/');
	assert(strstr(entry.scene, "main.tscn") != NULL);

	launcher_t launcher;
	memset(&launcher, 0, sizeof(launcher));
	assert(launcher_recent_add(&launcher, &entry) == 1);
	assert(launcher.recent_count == 1);
	assert(strcmp(launcher.recent[0].name, "demo") == 0);
	assert(launcher_recent_add(&launcher, &entry) == 1);
	assert(launcher.recent_count == 1);

	char ini[PATH_MAX];
	snprintf(ini, sizeof(ini), "%s/launcher.ini", parent);
	assert(launcher_recent_save(&launcher, ini) == 1);
	memset(&launcher, 0, sizeof(launcher));
	assert(launcher_recent_load(&launcher, ini) == 1);
	assert(launcher.recent_count == 1);
	assert(strcmp(launcher.recent[0].root, entry.root) == 0);

	launcher_spawn_args_t args;
	launcher_make_spawn_args(&args, &launcher.recent[0], 1);
	assert(strcmp(args.argv[1], "--mode=editor") == 0);
	assert(strncmp(args.argv[2], "--project=", 10) == 0);
	assert(strncmp(args.argv[3], "--scene=", 8) == 0);
	assert(args.argv[4] == NULL);

	launcher_make_spawn_args(&args, &launcher.recent[0], 0);
	assert(strcmp(args.argv[1], "--mode=game") == 0);

	assert(launcher_recent_remove(&launcher, 0) == 1);
	assert(launcher.recent_count == 0);
	test_remove_tree(parent);
}

static void test_filter_sort_and_import(void) {
	char root[] = "/tmp/nodebench_launcher_filter_XXXXXX";
	char* parent = mkdtemp(root);
	assert(parent != NULL);
	launcher_recent_t zeta;
	launcher_recent_t alpha;
	memset(&zeta, 0, sizeof(zeta));
	memset(&alpha, 0, sizeof(alpha));
	assert(launcher_create_project(parent, "zeta", &zeta) == 1);
	assert(launcher_create_project(parent, "alpha", &alpha) == 1);

	launcher_t launcher;
	memset(&launcher, 0, sizeof(launcher));
	snprintf(launcher.settings_path, sizeof(launcher.settings_path), "%s/launcher.ini", parent);
	assert(launcher_recent_add(&launcher, &zeta) == 1);
	assert(launcher_recent_add(&launcher, &alpha) == 1);
	assert(launcher.recent_count == 2);
	assert(launcher_project_modified(&launcher.recent[0]) > 0);
	assert(launcher_project_modified(&launcher.recent[1]) > 0);

	launcher.sort_mode = NB_LAUNCHER_SORT_NAME;
	launcher_apply_filter(&launcher);
	assert(launcher.filtered_count == 2);
	assert(strcmp(launcher.recent[launcher.filtered[0]].name, "alpha") == 0);
	assert(strcmp(launcher.recent[launcher.filtered[1]].name, "zeta") == 0);

	launcher.sort_mode = NB_LAUNCHER_SORT_PATH;
	launcher_apply_filter(&launcher);
	assert(launcher.filtered_count == 2);
	assert(strcmp(launcher.recent[launcher.filtered[0]].name, "alpha") == 0);

	snprintf(launcher.search, sizeof(launcher.search), "ZET");
	launcher_apply_filter(&launcher);
	assert(launcher.filtered_count == 1);
	assert(strcmp(launcher.recent[launcher.filtered[0]].name, "zeta") == 0);
	launcher.search[0] = '\0';

	int zeta_index = test_find_index(&launcher, "zeta");
	int alpha_index = test_find_index(&launcher, "alpha");
	assert(zeta_index >= 0);
	assert(alpha_index >= 0);
	struct utimbuf old_time;
	old_time.actime = 1000000000;
	old_time.modtime = 1000000000;
	struct utimbuf new_time;
	new_time.actime = 1000001000;
	new_time.modtime = 1000001000;
	assert(utime(launcher.recent[zeta_index].scene, &old_time) == 0);
	assert(utime(launcher.recent[alpha_index].scene, &new_time) == 0);
	launcher.sort_mode = NB_LAUNCHER_SORT_RECENT;
	launcher_apply_filter(&launcher);
	assert(launcher.filtered_count == 2);
	assert(strcmp(launcher.recent[launcher.filtered[0]].name, "alpha") == 0);

	memset(launcher.recent, 0, sizeof(launcher.recent));
	launcher.recent_count = 0;
	launcher.selected = -1;
	assert(launcher_import_project(&launcher, alpha.root) == 1);
	assert(launcher.recent_count == 1);
	assert(launcher.selected == 0);
	assert(strcmp(launcher.recent[0].name, "alpha") == 0);
	assert(access(launcher.settings_path, F_OK) == 0);
	assert(launcher_import_project(&launcher, parent) == 0);
	assert(launcher.recent_count == 1);
	test_remove_tree(parent);
}

int main(void) {
	test_cli_detection();
	test_recent_and_spawn();
	test_filter_sort_and_import();
	printf("test_launcher ok\n");
	return 0;
}
