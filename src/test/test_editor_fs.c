#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "editor/editor_fs.h"

int main(void) {
	char root[] = "nb_fs_test_XXXXXX";
	if (!mkdtemp(root)) { return 1; }

	char alpha[NB_EDITOR_FS_PATH_MAX];
	char zeta[NB_EDITOR_FS_PATH_MAX];
	snprintf(alpha, sizeof(alpha), "%s/alpha", root);
	snprintf(zeta, sizeof(zeta), "%s/zeta", root);
	if (!editor_fs_make_dir(alpha) || !editor_fs_make_dir(zeta)) { return 1; }

	char a[NB_EDITOR_FS_PATH_MAX];
	char b[NB_EDITOR_FS_PATH_MAX];
	snprintf(a, sizeof(a), "%s/a.txt", root);
	snprintf(b, sizeof(b), "%s/b.txt", root);
	if (!editor_fs_write_text(a, "a", 1) || !editor_fs_write_text(b, "b", 1)) { return 1; }

	char meta[NB_EDITOR_FS_PATH_MAX];
	snprintf(meta, sizeof(meta), "%s/a.txt.meta", root);
	if (!editor_fs_write_text(meta, "uuid=x", 6)) { return 1; }

	editor_fs_entry_t entries[8];
	int count = editor_fs_list_dir(root, entries, 8, 0);
	if (count != 4) { return 1; }
	if (strcmp(entries[0].name, "alpha") != 0 || strcmp(entries[1].name, "zeta") != 0) { return 1; }
	if (strcmp(entries[2].name, "a.txt") != 0 || strcmp(entries[3].name, "b.txt") != 0) { return 1; }

	char script[NB_EDITOR_FS_PATH_MAX];
	char config[NB_EDITOR_FS_PATH_MAX];
	char binary[NB_EDITOR_FS_PATH_MAX];
	char resource[NB_EDITOR_FS_PATH_MAX];
	snprintf(script, sizeof(script), "%s/script.wren", root);
	snprintf(config, sizeof(config), "%s/project.ini", root);
	snprintf(binary, sizeof(binary), "%s/blob.bin", root);
	snprintf(resource, sizeof(resource), "%s/material.tres", root);
	if (!editor_fs_write_text(script, "class A {}\n", 11) || !editor_fs_write_text(config, "mode=editor\n", 12)) { return 1; }
	if (!editor_fs_write_text(resource, "[resource]\n", 11)) { return 1; }
	char raw[] = { 'a', '\0', 'b', 'c' };
	if (!editor_fs_write_text(binary, raw, sizeof(raw))) { return 1; }
	if (!editor_fs_is_external_text(a) || !editor_fs_is_external_text(script) || !editor_fs_is_external_text(config)) { return 1; }
	if (editor_fs_is_external_text(binary) || editor_fs_is_external_text(resource) || editor_fs_is_external_text(meta)) { return 1; }

	char copy[NB_EDITOR_FS_PATH_MAX];
	char moved[NB_EDITOR_FS_PATH_MAX];
	snprintf(copy, sizeof(copy), "%.*s/copy.txt", (int)sizeof(alpha) - 11, alpha);
	snprintf(moved, sizeof(moved), "%.*s/copy.txt", (int)sizeof(zeta) - 11, zeta);
	if (!editor_fs_copy_file(a, copy) || !editor_fs_move_path(copy, moved)) { return 1; }
	if (editor_fs_exists(copy) || !editor_fs_exists(moved)) { return 1; }

	char scene[NB_EDITOR_FS_PATH_MAX];
	snprintf(scene, sizeof(scene), "%s/scene.tscn", root);
	char old_ref[NB_EDITOR_FS_PATH_MAX];
	char new_ref[NB_EDITOR_FS_PATH_MAX];
	snprintf(old_ref, sizeof(old_ref), "%s/old.txt", root);
	snprintf(new_ref, sizeof(new_ref), "%s/new.txt", root);
	char content[512];
	snprintf(content, sizeof(content), "[ext]\npath = \"res://%.*s\"\n", (int)sizeof(content) - 23, old_ref);
	if (!editor_fs_write_text(scene, content, strlen(content))) { return 1; }
	if (!editor_fs_rewrite_refs(old_ref, new_ref)) { return 1; }

	char* updated = NULL;
	if (!editor_fs_read_text(scene, &updated, NULL)) { return 1; }
	int ok = strstr(updated, new_ref) != NULL && strstr(updated, old_ref) == NULL;
	free(updated);
	if (!ok) { return 1; }

	char normalized[NB_EDITOR_FS_PATH_MAX];
	if (!editor_fs_normalize("./nb_fs_test/../nb_fs_test/file", normalized, sizeof(normalized))) { return 1; }
	if (!editor_fs_is_inside(".", normalized) || editor_fs_is_inside(zeta, normalized)) { return 1; }

	char old_path[4096];
	const char* env_path = getenv("PATH");
	snprintf(old_path, sizeof(old_path), "%.*s", (int)sizeof(old_path) - 1, env_path ? env_path : "");
	char fake_bin[NB_EDITOR_FS_PATH_MAX];
	char fake_nvim[NB_EDITOR_FS_PATH_MAX];
	char fake_code[NB_EDITOR_FS_PATH_MAX];
	snprintf(fake_bin, sizeof(fake_bin), "%s/bin", root);
	snprintf(fake_nvim, sizeof(fake_nvim), "%.*s/nvim", NB_EDITOR_FS_PATH_MAX - 6, fake_bin);
	snprintf(fake_code, sizeof(fake_code), "%.*s/code-oss", NB_EDITOR_FS_PATH_MAX - 10, fake_bin);
	if (!editor_fs_make_dir(fake_bin)) { return 1; }
	if (!editor_fs_write_text(fake_nvim, "#!/bin/sh\n", 10) || !editor_fs_write_text(fake_code, "#!/bin/sh\n", 10)) { return 1; }
	if (chmod(fake_nvim, 0755) != 0 || chmod(fake_code, 0755) != 0) { return 1; }
	if (setenv("PATH", fake_bin, 1) != 0) { return 1; }
	const char* found = editor_fs_find_text_editor();
	int priority_ok = found && strcmp(found, "code-oss") == 0;
	if (unlink(fake_code) != 0) { priority_ok = 0; }
	found = editor_fs_find_text_editor();
	priority_ok = priority_ok && found && strcmp(found, "nvim") == 0;
	setenv("PATH", old_path, 1);
	if (!priority_ok) { return 1; }

	if (!editor_fs_delete_path(root) || editor_fs_exists(root)) { return 1; }
	printf("editor fs test ok\n");
	return 0;
}
