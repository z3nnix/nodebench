#include <math.h>
#include <stdio.h>
#include <string.h>

#include "scene/scene.h"

int main(void) {
	scene_t scene;
	scene_init(&scene);

	entity_id_t parent = scene_create_entity(&scene);
	entity_id_t child = scene_create_entity(&scene);
	if (parent == NB_ENTITY_INVALID || child == NB_ENTITY_INVALID) {
		return 1;
	}

	if (!scene_set_node_name(&scene, parent, "Parent") || !scene_set_node_name(&scene, child, "Child")) {
		return 1;
	}

	transform_t parent_transform;
	memset(&parent_transform, 0, sizeof(parent_transform));
	parent_transform.position = (vec3_t){ 1.0f, 2.0f, 3.0f };
	parent_transform.scale = (vec3_t){ 2.0f, 1.0f, 1.0f };

	transform_t child_transform;
	memset(&child_transform, 0, sizeof(child_transform));
	child_transform.position = (vec3_t){ 1.0f, 0.0f, 0.0f };

	if (!scene_add_transform(&scene, parent, parent_transform) || !scene_add_transform(&scene, child, child_transform)) {
		return 1;
	}

	if (!scene_set_node_parent(&scene, child, parent)) {
		return 1;
	}

	scene_update_world_transforms(&scene);
	scene_rebuild_cache(&scene);

	node_t* child_node = scene_get_node(&scene, child);
	if (!child_node) {
		return 1;
	}

	if (fabsf(child_node->world_position.x - 3.0f) > 0.0001f || fabsf(child_node->world_position.y - 2.0f) > 0.0001f || fabsf(child_node->world_position.z - 3.0f) > 0.0001f) {
		return 1;
	}

	if (scene_find_node_path(&scene, "Parent/Child") != child) {
		return 1;
	}

	if (scene_set_node_parent(&scene, child, child)) {
		return 1;
	}

	if (!scene_set_node_visible(&scene, parent, 0)) {
		return 1;
	}

	scene_update_world_transforms(&scene);
	if (scene_node_visible(&scene, child) || scene_node_visible(&scene, parent)) {
		return 1;
	}

	if (!scene_set_node_visible(&scene, parent, 1) || !scene_set_node_tint(&scene, child, (vec3_t){ 0.25f, 0.5f, 0.75f })) {
		return 1;
	}

	scene_update_world_transforms(&scene);
	node_t* tinted = scene_get_node(&scene, child);
	if (!tinted || !tinted->tint_enabled || fabsf(tinted->tint.x - 0.25f) > 0.0001f || fabsf(tinted->tint.y - 0.5f) > 0.0001f || fabsf(tinted->tint.z - 0.75f) > 0.0001f) {
		return 1;
	}

	if (scene.transform_count != 2) {
		return 1;
	}

	entity_id_t root2 = scene_create_entity(&scene);
	entity_id_t node_a = scene_create_entity(&scene);
	entity_id_t node_b = scene_create_entity(&scene);
	if (root2 == NB_ENTITY_INVALID || node_a == NB_ENTITY_INVALID || node_b == NB_ENTITY_INVALID) {
		return 1;
	}
	if (!scene_set_node_name(&scene, root2, "Root2") || !scene_set_node_name(&scene, node_a, "A") || !scene_set_node_name(&scene, node_b, "B")) {
		return 1;
	}
	if (!scene_set_node_parent(&scene, node_a, root2) || !scene_set_node_parent(&scene, node_b, root2)) {
		return 1;
	}
	node_t* root2_node = scene_get_node(&scene, root2);
	if (!root2_node || root2_node->child_count != 2 || root2_node->children[0] != node_a || root2_node->children[1] != node_b) {
		return 1;
	}

	if (!scene_move_node(&scene, node_a, root2, 1)) {
		return 1;
	}
	if (root2_node->child_count != 2 || root2_node->children[0] != node_b || root2_node->children[1] != node_a) {
		return 1;
	}

	if (!scene_move_node(&scene, node_a, NB_ENTITY_INVALID, -1)) {
		return 1;
	}
	if (scene_get_node(&scene, node_a)->parent != NB_ENTITY_INVALID || root2_node->child_count != 1 || root2_node->children[0] != node_b) {
		return 1;
	}

	if (!scene_is_ancestor(&scene, parent, child) || scene_is_ancestor(&scene, child, parent)) {
		return 1;
	}
	if (scene_move_node(&scene, parent, child, 0)) {
		return 1;
	}
	if (!scene_move_node(&scene, node_b, child, 0) || !scene_is_ancestor(&scene, child, node_b)) {
		return 1;
	}
	if (scene_find_node_path(&scene, "Parent/Child/B") != node_b) {
		return 1;
	}

	scene_shutdown(&scene);
	printf("scene test ok\n");

	return 0;
}
