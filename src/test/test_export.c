#include "test/fixture_project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/engine.h"
#include "core/file_io.h"
#include "core/result.h"
#include "core/sha256.h"

static int count_text(const char* text, const char* needle) {
	int count = 0;
	const char* position = text;

	while ((position = strstr(position, needle)) != NULL) {
		count++;
		position += strlen(needle);
	}

	return count;
}

static int files_equal(const char* a, const char* b) {
	unsigned char* left = NULL;
	unsigned char* right = NULL;
	size_t left_size = 0;
	size_t right_size = 0;
	if (!nb_file_read_all(a, &left, &left_size) || !nb_file_read_all(b, &right, &right_size)) {
		free(left);
		free(right);
		return 0;
	}

	int equal = left_size == right_size && memcmp(left, right, left_size) == 0;
	free(left);
	free(right);

	return equal;
}

int main(void) {
	uint8_t digest[32];
	char hex[65];
	sha256_compute("abc", 3, digest);
	sha256_hex(digest, hex);
	if (strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) {
		return 1;
	}

	char cwd[PATH_MAX];
	char root[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd)) || !nb_fixture_create(root, sizeof(root)) || chdir(root) != 0) {
		return 1;
	}

	char output[PATH_MAX];
	char manifest[PATH_MAX];
	char source_scene[PATH_MAX];
	char exported_scene[PATH_MAX];
	snprintf(output, sizeof(output), "%.*s/exported", (int)sizeof(output) - 10, root);
	snprintf(manifest, sizeof(manifest), "%.*s/manifest.json", (int)sizeof(manifest) - 15, output);
	snprintf(source_scene, sizeof(source_scene), "%.*s/scenes/level01.tscn", (int)sizeof(source_scene) - 21, root);
	snprintf(exported_scene, sizeof(exported_scene), "%.*s/scenes/level01.tscn", (int)sizeof(exported_scene) - 21, output);

	char* argv[] = { "nodebench", "export", output, NULL };
	if (engine_run(3, argv) != NB_RESULT_OK || !files_equal(source_scene, exported_scene)) {
		return 1;
	}

	unsigned char* first = NULL;
	size_t first_size = 0;
	if (!nb_file_read_all(manifest, &first, &first_size)) {
		return 1;
	}

	if (engine_run(3, argv) != NB_RESULT_OK) {
		free(first);
		return 1;
	}

	unsigned char* second = NULL;
	size_t second_size = 0;
	if (!nb_file_read_all(manifest, &second, &second_size)) {
		free(first);
		return 1;
	}

	int deterministic = first_size == second_size && memcmp(first, second, first_size) == 0;
	int count = count_text((const char*)second, "\"path\"");
	int skipped_output = strstr((const char*)second, "exported/") == NULL;
	free(first);
	free(second);

	if (!deterministic || count < 8 || !skipped_output || chdir(cwd) != 0) {
		return 1;
	}

	nb_fixture_destroy(root);
	printf("export test ok\n");

	return 0;
}
