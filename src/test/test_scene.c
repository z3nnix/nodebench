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

	if (scene.transform_count != 2) {
		return 1;
	}

	scene_shutdown(&scene);
	printf("scene test ok\n");

	return 0;
}
