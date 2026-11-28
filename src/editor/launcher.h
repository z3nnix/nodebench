#ifndef NB_EDITOR_LAUNCHER_H
#define NB_EDITOR_LAUNCHER_H

#include <stddef.h>

#define NB_LAUNCHER_RECENT_MAX 8
#define NB_LAUNCHER_PATH_MAX 512
#define NB_LAUNCHER_NAME_MAX 128
#define NB_LAUNCHER_STATUS_MAX 192
#define NB_LAUNCHER_SEARCH_MAX 128

typedef struct engine_t engine_t;

typedef enum {
	NB_LAUNCHER_SORT_RECENT = 0,
	NB_LAUNCHER_SORT_NAME,
	NB_LAUNCHER_SORT_PATH
} launcher_sort_mode_t;

typedef struct {
	char name[NB_LAUNCHER_NAME_MAX];
	char root[NB_LAUNCHER_PATH_MAX];
	char scene[NB_LAUNCHER_PATH_MAX];
} launcher_recent_t;

typedef struct {
	char exe[NB_LAUNCHER_PATH_MAX];
	char mode[64];
	char project[NB_LAUNCHER_PATH_MAX + 32];
	char scene[NB_LAUNCHER_PATH_MAX + 32];
	char* argv[5];
} launcher_spawn_args_t;

typedef struct {
	engine_t* engine;
	int active;
	int ready;
	int selected;
	int sort_mode;
	int create_dialog_open;
	int import_dialog_open;
	int settings_open;
	int remove_pending;
	int exit_requested;
	int filtered[NB_LAUNCHER_RECENT_MAX];
	int filtered_count;
	int click_row;
	unsigned int click_tick;
	char search[NB_LAUNCHER_SEARCH_MAX];
	char new_name[NB_LAUNCHER_NAME_MAX];
	char new_parent[NB_LAUNCHER_PATH_MAX];
	char import_path[NB_LAUNCHER_PATH_MAX];
	char status[NB_LAUNCHER_STATUS_MAX];
	char settings_path[NB_LAUNCHER_PATH_MAX];
	launcher_recent_t recent[NB_LAUNCHER_RECENT_MAX];
	int recent_count;
	void* nk_ctx;
	void* font_base;
	void* font_semibold;
	void* font_heading;
} launcher_t;

int launcher_init(launcher_t* launcher, engine_t* engine);
void launcher_shutdown(launcher_t* launcher);
void launcher_begin_frame(launcher_t* launcher);
int launcher_handle_event(launcher_t* launcher, const void* sdl_event);
void launcher_update(launcher_t* launcher, engine_t* engine);
void launcher_render(launcher_t* launcher, engine_t* engine);

int launcher_recent_load(launcher_t* launcher, const char* path);
int launcher_recent_save(const launcher_t* launcher, const char* path);
int launcher_recent_add(launcher_t* launcher, const launcher_recent_t* entry);
int launcher_recent_remove(launcher_t* launcher, int index);
int launcher_project_info(const char* root, launcher_recent_t* out);
void launcher_apply_filter(launcher_t* launcher);
long long launcher_project_modified(const launcher_recent_t* entry);
int launcher_import_project(launcher_t* launcher, const char* path);
int launcher_create_project(const char* parent, const char* name, launcher_recent_t* out);
void launcher_make_spawn_args(launcher_spawn_args_t* args, const launcher_recent_t* entry, int editor);
int launcher_open_project(launcher_t* launcher, engine_t* engine, int editor);

#endif
