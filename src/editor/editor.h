#ifndef NB_EDITOR_EDITOR_H
#define NB_EDITOR_EDITOR_H

#include "core/config.h"
#include "editor/editor_fs.h"
#include "scene/scene.h"

typedef struct engine_t engine_t;

typedef enum {
	NB_EDITOR_TOOL_SELECT = 0,
	NB_EDITOR_TOOL_MOVE = 1,
	NB_EDITOR_TOOL_ROTATE = 2,
	NB_EDITOR_TOOL_SCALE = 3
} editor_tool_t;

#define NB_EDITOR_UNDO_LIMIT 64
#define NB_EDITOR_INSPECTOR_CLUT_MAX 256
#define NB_EDITOR_INSPECTOR_TRES_EXT_MAX 64
#define NB_EDITOR_INSPECTOR_TRES_LINE_MAX 512
#define NB_EDITOR_INSPECTOR_WAV_PEAKS 128

typedef enum {
	NB_EDITOR_DIALOG_NONE = 0,
	NB_EDITOR_DIALOG_DELETE = 1,
	NB_EDITOR_DIALOG_MOVE = 2,
	NB_EDITOR_DIALOG_IMPORT = 3,
	NB_EDITOR_DIALOG_NEW_SCENE = 4,
	NB_EDITOR_DIALOG_DISCARD = 5,
	NB_EDITOR_DIALOG_NEW_FOLDER = 6,
	NB_EDITOR_DIALOG_OPEN_PROJECT = 7,
	NB_EDITOR_DIALOG_NEW_PROJECT = 8
} editor_dialog_kind_t;

typedef enum {
	NB_EDITOR_INSPECTOR_NODE = 0,
	NB_EDITOR_INSPECTOR_CLUT = 1,
	NB_EDITOR_INSPECTOR_TRES = 2,
	NB_EDITOR_INSPECTOR_WAV = 3
} editor_inspector_kind_t;

typedef enum {
	NB_EDITOR_COLOR_TARGET_CLUT = 0,
	NB_EDITOR_COLOR_TARGET_TRES = 1
} editor_color_target_t;

typedef struct {
	int count;
	unsigned char r[NB_EDITOR_INSPECTOR_CLUT_MAX];
	unsigned char g[NB_EDITOR_INSPECTOR_CLUT_MAX];
	unsigned char b[NB_EDITOR_INSPECTOR_CLUT_MAX];
} editor_clut_state_t;

typedef struct {
	char header[NB_EDITOR_INSPECTOR_TRES_LINE_MAX];
	char ext[NB_EDITOR_INSPECTOR_TRES_EXT_MAX][NB_EDITOR_INSPECTOR_TRES_LINE_MAX];
	int ext_count;
	char name[64];
	char texture[64];
	char palette[64];
	int blend_mode;
	int shade_mode;
	int fog_enabled;
	float tint[4];
	int transparent_index;
} editor_tres_state_t;

typedef struct {
	int sample_rate;
	int channels;
	int frame_count;
	float peaks[NB_EDITOR_INSPECTOR_WAV_PEAKS];
} editor_wav_state_t;

typedef struct {
	char* data;
	size_t size;
	unsigned int selected_entity;
} editor_snapshot_t;

typedef struct {
	char path[NB_EDITOR_FS_PATH_MAX];
	char name[NB_EDITOR_FS_NAME_MAX];
	float camera_pos[3];
	float camera_yaw;
	float camera_pitch;
	unsigned int selected_entity;
	unsigned int hierarchy_scroll_x;
	unsigned int hierarchy_scroll_y;
	unsigned char* hierarchy_expanded;
	int hierarchy_expanded_count;
	int hierarchy_expanded_capacity;
	int dirty;
} editor_scene_tab_t;

typedef struct {
	char root[NB_EDITOR_FS_PATH_MAX];
	char current_dir[NB_EDITOR_FS_PATH_MAX];
	char filter[64];
	char selected_path[NB_EDITOR_FS_PATH_MAX];
	char status[256];
	editor_fs_entry_t files[NB_EDITOR_FS_MAX_ENTRIES];
	int file_count;
	char (*expanded)[NB_EDITOR_FS_PATH_MAX];
	int expanded_count;
	int expanded_capacity;
	char rename_buffer[NB_EDITOR_FS_NAME_MAX];
	char rename_path[NB_EDITOR_FS_PATH_MAX];
	char drag_path[NB_EDITOR_FS_PATH_MAX];
	char drop_dir[NB_EDITOR_FS_PATH_MAX];
	char context_path[NB_EDITOR_FS_PATH_MAX];
	float context_rect[4];
	char (*history)[NB_EDITOR_FS_PATH_MAX];
	int history_count;
	int history_capacity;
	int history_index;
	int context_empty;
	int tile_width;
	int drag_start_x;
	int drag_start_y;
	int drag_active;
	int rename_open;
	int resizing;
	int tree_resizing;
	int tree_width;
	int show_hidden;
	int view_grid;
	int type_filter;
	int height;
	double last_click_time;
	char last_click_path[NB_EDITOR_FS_PATH_MAX];
	unsigned int files_scroll_x;
	unsigned int files_scroll_y;
	unsigned int tree_scroll_x;
	unsigned int tree_scroll_y;
	float files_rect[4];
	float tree_rect[4];
	char (*tree_row_paths)[NB_EDITOR_FS_PATH_MAX];
	float (*tree_row_rects)[4];
	int tree_row_count;
	int tree_row_capacity;
} editor_explorer_t;

typedef struct {
	engine_t* engine;
	int enabled;
	int ready;
	int want_capture_mouse;
	int want_capture_keyboard;
	unsigned int selected_entity;
	char status[128];
	char scene_path[256];
	char new_node_name[64];
	float camera_pos[3];
	float camera_yaw;
	float camera_pitch;
	float camera_speed;
	int viewport_x;
	int viewport_y;
	int viewport_w;
	int viewport_h;
	int win_w;
	int win_h;
	int toolbar_h;
	int tool;
	int gizmo_axis;
	int gizmo_dragging;
	float gizmo_drag_start[3];
	int gizmo_drag_mouse_x;
	int gizmo_drag_mouse_y;
	void* tool_textures[4];
	editor_snapshot_t* undo;
	int undo_count;
	int undo_capacity;
	editor_snapshot_t* redo;
	int redo_count;
	int redo_capacity;
	int history_suspended;
	double last_save_time;
	double autosave_timer;
	int play_pid;
	int request_save;
	int request_undo;
	int request_redo;
	int inspector_editing;
	editor_snapshot_t pre_edit;
	int has_pre_edit;
	unsigned int hierarchy_scroll_x;
	unsigned int hierarchy_scroll_y;
	unsigned char* hierarchy_expanded;
	int hierarchy_expanded_count;
	int hierarchy_expanded_capacity;
	char hierarchy_filter[64];
	unsigned int hierarchy_context_entity;
	float hierarchy_context_rect[4];
	unsigned int rename_entity;
	char rename_buffer[64];
	int rename_focus;
	unsigned int reparent_source;
	int reparent_open;
	unsigned int hierarchy_drag_candidate;
	unsigned int hierarchy_drag_entity;
	int hierarchy_drag_start_x;
	int hierarchy_drag_start_y;
	int hierarchy_drag_active;
	unsigned int hierarchy_drop_entity;
	unsigned int hierarchy_drop_row_entity;
	int hierarchy_drop_index;
	int hierarchy_drop_zone;
	char inspector_name[64];
	unsigned int inspector_name_entity;
	editor_inspector_kind_t inspector_kind;
	char inspector_path[NB_EDITOR_FS_PATH_MAX];
	unsigned int inspector_node_entity;
	int inspector_dirty;
	double inspector_dirty_time;
	editor_clut_state_t inspector_clut;
	editor_tres_state_t inspector_tres;
	editor_wav_state_t inspector_wav;
	int inspector_color_index;
	editor_color_target_t inspector_color_target;
	float inspector_color_h;
	float inspector_color_s;
	float inspector_color_v;
	float inspector_color_a;
	char inspector_color_hex[16];
	int inspector_wav_voice;
	double inspector_wav_end;
	unsigned int inspector_scroll_x;
	unsigned int inspector_scroll_y;
	editor_scene_tab_t* tabs;
	int tab_count;
	int tab_capacity;
	int active_tab;
	editor_explorer_t explorer;
	char dialog_path[NB_EDITOR_FS_PATH_MAX];
	char dialog_dst[NB_EDITOR_FS_PATH_MAX];
	char dialog_text[256];
	char dialog_title[64];
	int dialog_kind;
	int dialog_target;
	void* nk_ctx;
} editor_t;

int editor_init(editor_t* editor, engine_t* engine);
void editor_shutdown(editor_t* editor, engine_t* engine);
int editor_handle_event(editor_t* editor, const void* sdl_event);
void editor_begin_frame(editor_t* editor, engine_t* engine);
void editor_update(editor_t* editor, engine_t* engine);
void editor_render(editor_t* editor, engine_t* engine);
int editor_enabled(const editor_t* editor);
int editor_wants_mouse(const editor_t* editor);
int editor_wants_keyboard(const editor_t* editor);

#endif
