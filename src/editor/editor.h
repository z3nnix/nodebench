#ifndef NB_EDITOR_EDITOR_H
#define NB_EDITOR_EDITOR_H

#include "core/config.h"
#include "scene/scene.h"

typedef struct engine_t engine_t;

typedef enum {
	NB_EDITOR_TOOL_SELECT = 0,
	NB_EDITOR_TOOL_MOVE = 1,
	NB_EDITOR_TOOL_ROTATE = 2,
	NB_EDITOR_TOOL_SCALE = 3
} editor_tool_t;

#define NB_EDITOR_UNDO_MAX 64

typedef struct {
	char* data;
	size_t size;
	unsigned int selected_entity;
} editor_snapshot_t;

typedef struct {
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
	int toolbar_h;
	int tool;
	int gizmo_axis;
	int gizmo_dragging;
	float gizmo_drag_start[3];
	int gizmo_drag_mouse_x;
	int gizmo_drag_mouse_y;
	void* tool_textures[4];
	editor_snapshot_t undo[NB_EDITOR_UNDO_MAX];
	editor_snapshot_t redo[NB_EDITOR_UNDO_MAX];
	int undo_count;
	int redo_count;
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
	unsigned char hierarchy_expanded[NB_SCENE_MAX_ENTITIES];
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
