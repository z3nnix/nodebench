#include "editor/editor_dnd.h"

#include <math.h>
#include <string.h>

#define NB_EDITOR_DND_FOV 60.0f
#define NB_EDITOR_DND_FALLBACK_DISTANCE 5.0f

nb_resource_kind_t editor_dnd_kind_for_path(const char* path) {
	const char* ext = editor_fs_ext(path);
	if (!ext) { return NB_RESOURCE_KIND_NONE; }
	if (strcmp(ext, "tscn") == 0) { return NB_RESOURCE_KIND_PACKED_SCENE; }
	if (strcmp(ext, "obj") == 0) { return NB_RESOURCE_KIND_MESH; }
	if (strcmp(ext, "tres") == 0) { return NB_RESOURCE_KIND_MATERIAL; }
	if (strcmp(ext, "wren") == 0) { return NB_RESOURCE_KIND_SCRIPT; }
	if (strcmp(ext, "ppm") == 0 || strcmp(ext, "bmp") == 0) { return NB_RESOURCE_KIND_TEXTURE; }
	if (strcmp(ext, "clut") == 0) { return NB_RESOURCE_KIND_PALETTE; }
	if (strcmp(ext, "wav") == 0) { return NB_RESOURCE_KIND_AUDIO; }
	return NB_RESOURCE_KIND_NONE;
}

int editor_dnd_accepts(nb_resource_kind_t kind, nb_resource_kind_t target) {
	if (kind == NB_RESOURCE_KIND_NONE || target == NB_RESOURCE_KIND_NONE) { return 0; }
	return kind == target;
}

int editor_dnd_point_in_rect(const float rect[4], int x, int y) {
	if (!rect || rect[2] <= 0.0f || rect[3] <= 0.0f) { return 0; }
	return (float)x >= rect[0] && (float)x < rect[0] + rect[2] && (float)y >= rect[1] && (float)y < rect[1] + rect[3];
}

int editor_dnd_screen_ray(const editor_dnd_view_t* view, int mx, int my, vec3_t* out) {
	if (!view || !out || view->viewport_w <= 0 || view->viewport_h <= 0) { return 0; }
	float aspect = (float)view->viewport_w / (float)view->viewport_h;
	float ndc_x = ((float)(mx - view->viewport_x) / (float)view->viewport_w) * 2.0f - 1.0f;
	float ndc_y = 1.0f - ((float)(my - view->viewport_y) / (float)view->viewport_h) * 2.0f;
	float cy = cosf(view->yaw);
	float sy = sinf(view->yaw);
	float cp = cosf(view->pitch);
	float sp = sinf(view->pitch);
	vec3_t eye = { view->pos[0], view->pos[1], view->pos[2] };
	vec3_t target = { eye.x + cy * cp, eye.y + sp, eye.z + sy * cp };
	vec3_t forward = vec3_normalize(vec3_sub(target, eye));
	vec3_t right = vec3_normalize(vec3_cross(forward, (vec3_t){ 0.0f, 1.0f, 0.0f }));
	vec3_t up = vec3_cross(right, forward);
	float tan_half = tanf(NB_EDITOR_DND_FOV * 3.14159265f / 360.0f);
	vec3_t dir = vec3_add(vec3_add(vec3_scale(right, ndc_x * tan_half * aspect), vec3_scale(up, ndc_y * tan_half)), forward);
	*out = vec3_normalize(dir);
	return 1;
}

int editor_dnd_ground_point(const editor_dnd_view_t* view, int mx, int my, vec3_t* out) {
	if (!view || !out) { return 0; }
	vec3_t eye = { view->pos[0], view->pos[1], view->pos[2] };
	vec3_t dir;
	if (!editor_dnd_screen_ray(view, mx, my, &dir)) { return 0; }
	if (fabsf(dir.y) < 0.0001f) {
		*out = vec3_add(eye, vec3_scale(dir, NB_EDITOR_DND_FALLBACK_DISTANCE));
		return 1;
	}
	float t = -eye.y / dir.y;
	if (t <= 0.0f) { t = NB_EDITOR_DND_FALLBACK_DISTANCE; }
	*out = vec3_add(eye, vec3_scale(dir, t));
	out->y = 0.0f;
	return 1;
}

void editor_dnd_reset(editor_dnd_state_t* dnd) {
	if (!dnd) { return; }
	dnd->path[0] = '\0';
	dnd->pressed = 0;
	dnd->active = 0;
	dnd->start_x = 0;
	dnd->start_y = 0;
	dnd->target = NB_EDITOR_DND_NONE;
	dnd->target_path[0] = '\0';
	dnd->hierarchy_parent = NB_ENTITY_INVALID;
	dnd->tres_ext_index = -1;
}
