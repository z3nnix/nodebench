#include <math.h>
#include <stdio.h>
#include <string.h>

#include "editor/editor_dnd.h"

int main(void) {
	if (editor_dnd_kind_for_path("scene.tscn") != NB_RESOURCE_KIND_PACKED_SCENE) { return 1; }
	if (editor_dnd_kind_for_path("mesh.obj") != NB_RESOURCE_KIND_MESH) { return 1; }
	if (editor_dnd_kind_for_path("mat.tres") != NB_RESOURCE_KIND_MATERIAL) { return 1; }
	if (editor_dnd_kind_for_path("script.wren") != NB_RESOURCE_KIND_SCRIPT) { return 1; }
	if (editor_dnd_kind_for_path("tile.ppm") != NB_RESOURCE_KIND_TEXTURE) { return 1; }
	if (editor_dnd_kind_for_path("tile.bmp") != NB_RESOURCE_KIND_TEXTURE) { return 1; }
	if (editor_dnd_kind_for_path("colors.clut") != NB_RESOURCE_KIND_PALETTE) { return 1; }
	if (editor_dnd_kind_for_path("sound.wav") != NB_RESOURCE_KIND_AUDIO) { return 1; }
	if (editor_dnd_kind_for_path("blob.bin") != NB_RESOURCE_KIND_NONE) { return 1; }
	if (editor_dnd_kind_for_path(NULL) != NB_RESOURCE_KIND_NONE) { return 1; }

	if (!editor_dnd_accepts(NB_RESOURCE_KIND_MESH, NB_RESOURCE_KIND_MESH)) { return 1; }
	if (editor_dnd_accepts(NB_RESOURCE_KIND_MESH, NB_RESOURCE_KIND_MATERIAL)) { return 1; }
	if (editor_dnd_accepts(NB_RESOURCE_KIND_NONE, NB_RESOURCE_KIND_NONE)) { return 1; }

	float rect[4] = { 10.0f, 20.0f, 30.0f, 40.0f };
	if (!editor_dnd_point_in_rect(rect, 10, 20)) { return 1; }
	if (!editor_dnd_point_in_rect(rect, 39, 59)) { return 1; }
	if (editor_dnd_point_in_rect(rect, 40, 30)) { return 1; }
	if (editor_dnd_point_in_rect(rect, 20, 60)) { return 1; }
	if (editor_dnd_point_in_rect(NULL, 20, 30)) { return 1; }

	editor_dnd_view_t view;
	memset(&view, 0, sizeof(view));
	view.pos[1] = 5.0f;
	view.yaw = -1.5707963f;
	view.pitch = -0.35f;
	view.viewport_x = 100;
	view.viewport_y = 100;
	view.viewport_w = 800;
	view.viewport_h = 600;
	vec3_t center;
	if (!editor_dnd_screen_ray(&view, 500, 400, &center)) { return 1; }
	if (fabsf(center.x) > 0.001f || center.y >= 0.0f || center.z >= 0.0f) { return 1; }
	vec3_t right;
	if (!editor_dnd_screen_ray(&view, 700, 400, &right)) { return 1; }
	if (right.x <= center.x) { return 1; }
	vec3_t pos;
	if (!editor_dnd_ground_point(&view, 500, 400, &pos)) { return 1; }
	if (fabsf(pos.y) > 0.0001f || pos.z >= 0.0f) { return 1; }
	if (fabsf(pos.x) > 0.01f) { return 1; }
	view.viewport_w = 0;
	if (editor_dnd_screen_ray(&view, 500, 400, &pos)) { return 1; }

	editor_dnd_state_t dnd;
	memset(&dnd, 0, sizeof(dnd));
	snprintf(dnd.path, sizeof(dnd.path), "mesh.obj");
	snprintf(dnd.target_path, sizeof(dnd.target_path), "assets");
	dnd.pressed = 1;
	dnd.active = 1;
	dnd.start_x = 12;
	dnd.start_y = 24;
	dnd.target = NB_EDITOR_DND_VIEWPORT;
	dnd.hierarchy_parent = 42;
	dnd.tres_ext_index = 3;
	editor_dnd_reset(&dnd);
	if (dnd.path[0] || dnd.target_path[0] || dnd.pressed || dnd.active || dnd.start_x || dnd.start_y) { return 1; }
	if (dnd.target != NB_EDITOR_DND_NONE || dnd.hierarchy_parent != NB_ENTITY_INVALID || dnd.tres_ext_index != -1) { return 1; }

	printf("editor dnd test ok\n");
	return 0;
}
