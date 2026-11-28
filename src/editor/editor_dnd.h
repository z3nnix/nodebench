#ifndef NB_EDITOR_DND_H
#define NB_EDITOR_DND_H

#include "core/math.h"
#include "editor/editor_fs.h"
#include "resource/resource_id.h"
#include "scene/scene.h"

typedef enum {
	NB_EDITOR_DND_NONE = 0,
	NB_EDITOR_DND_HIERARCHY = 1,
	NB_EDITOR_DND_VIEWPORT = 2,
	NB_EDITOR_DND_INSPECTOR_MESH = 3,
	NB_EDITOR_DND_INSPECTOR_MATERIAL = 4,
	NB_EDITOR_DND_INSPECTOR_SCRIPT = 5,
	NB_EDITOR_DND_INSPECTOR_TEXTURE = 6,
	NB_EDITOR_DND_INSPECTOR_PALETTE = 7,
	NB_EDITOR_DND_EXPLORER_DIR = 8
} editor_dnd_target_t;

typedef struct {
	char path[NB_EDITOR_FS_PATH_MAX];
	int pressed;
	int active;
	int start_x;
	int start_y;
	editor_dnd_target_t target;
	char target_path[NB_EDITOR_FS_PATH_MAX];
	unsigned int hierarchy_parent;
	int tres_ext_index;
} editor_dnd_state_t;

typedef struct {
	float pos[3];
	float yaw;
	float pitch;
	int viewport_x;
	int viewport_y;
	int viewport_w;
	int viewport_h;
} editor_dnd_view_t;

nb_resource_kind_t editor_dnd_kind_for_path(const char* path);
int editor_dnd_accepts(nb_resource_kind_t kind, nb_resource_kind_t target);
int editor_dnd_point_in_rect(const float rect[4], int x, int y);
int editor_dnd_screen_ray(const editor_dnd_view_t* view, int mx, int my, vec3_t* out);
int editor_dnd_ground_point(const editor_dnd_view_t* view, int mx, int my, vec3_t* out);
void editor_dnd_reset(editor_dnd_state_t* dnd);

#endif
