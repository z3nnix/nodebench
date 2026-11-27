#ifndef NB_EDITOR_EDITOR_FS_H
#define NB_EDITOR_EDITOR_FS_H

#include <stddef.h>

#define NB_EDITOR_FS_PATH_MAX 512
#define NB_EDITOR_FS_NAME_MAX 128
#define NB_EDITOR_FS_MAX_ENTRIES 512

typedef enum {
	NB_EDITOR_FS_KIND_DIR = 0,
	NB_EDITOR_FS_KIND_FILE = 1,
	NB_EDITOR_FS_KIND_OTHER = 2
} editor_fs_kind_t;

typedef struct {
	char name[NB_EDITOR_FS_NAME_MAX];
	char path[NB_EDITOR_FS_PATH_MAX];
	editor_fs_kind_t kind;
	long size;
} editor_fs_entry_t;

int editor_fs_normalize(const char* path, char* out, size_t size);
int editor_fs_is_inside(const char* root, const char* path);
int editor_fs_exists(const char* path);
int editor_fs_is_dir(const char* path);
int editor_fs_list_dir(const char* dir, editor_fs_entry_t* out, int cap, int show_hidden);
int editor_fs_make_dir(const char* path);
int editor_fs_copy_file(const char* src, const char* dst);
int editor_fs_copy_path(const char* src, const char* dst);
int editor_fs_move_path(const char* src, const char* dst);
int editor_fs_delete_path(const char* path);
int editor_fs_read_text(const char* path, char** out, size_t* out_size);
int editor_fs_write_text(const char* path, const char* data, size_t size);
int editor_fs_rewrite_refs(const char* old_path, const char* new_path);
const char* editor_fs_ext(const char* path);
void editor_fs_basename(const char* path, char* out, size_t size);
void editor_fs_dirname(const char* path, char* out, size_t size);

#endif
