#define _XOPEN_SOURCE 700

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "core/config.h"

int main(void) {
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) { return 1; }
	char tmpl[] = "/tmp/opencode/nb_config_root_XXXXXX";
	char* root = mkdtemp(tmpl);
	if (!root) { return 1; }
	char examples[PATH_MAX];
	char sub[PATH_MAX];
	char config[PATH_MAX];
	snprintf(examples, sizeof(examples), "%s/examples", root);
	snprintf(sub, sizeof(sub), "%s/examples/kula", root);
	snprintf(config, sizeof(config), "%s/project.ini", root);
	if (mkdir(examples, 0700) != 0 || mkdir(sub, 0700) != 0) { return 1; }
	FILE* file = fopen(config, "w");
	if (!file) { return 1; }
	fprintf(file, "mode=editor\n");
	fclose(file);
	char expected[PATH_MAX];
	if (!realpath(root, expected)) { return 1; }
	if (chdir(sub) != 0) { return 1; }
	char found[PATH_MAX];
	if (!config_resolve_project_root(0, NULL, found, sizeof(found)) || strcmp(found, expected) != 0) {
		fprintf(stderr, "cwd root: got %s expected %s\n", found, expected);
		return 1;
	}
	char config_arg[PATH_MAX + 16];
	snprintf(config_arg, sizeof(config_arg), "--project=%s", config);
	char* argv[] = { "nodebench", config_arg, NULL };
	if (!config_resolve_project_root(2, argv, found, sizeof(found)) || strcmp(found, expected) != 0) {
		fprintf(stderr, "explicit project root: got %s expected %s\n", found, expected);
		return 1;
	}
	if (chdir(cwd) != 0) { return 1; }
	unlink(config);
	rmdir(sub);
	rmdir(examples);
	rmdir(root);
	printf("config root test ok\n");
	return 0;
}
