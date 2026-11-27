#include <stdio.h>
#include <string.h>

#include "physics/collision.h"
#include "scene/scene.h"

static entity_id_t test_add_node(scene_t* scene, vec3_t position, collider_t collider) {
	entity_id_t id = scene_create_entity(scene);
	if (id == NB_ENTITY_INVALID) {
		return NB_ENTITY_INVALID;
	}

	transform_t transform;
	memset(&transform, 0, sizeof(transform));
	transform.position = position;
	transform.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
	if (!scene_add_transform(scene, id, transform) || !scene_add_collider(scene, id, collider)) {
		return NB_ENTITY_INVALID;
	}

	return id;
}

int main(void) {
	scene_t scene;
	scene_init(&scene);

	collider_t sphere;
	sphere.shape = NB_COLLIDER_SPHERE;
	sphere.extents = (vec3_t){ 0.0f, 0.0f, 0.0f };
	sphere.radius = 0.5f;

	collider_t box;
	box.shape = NB_COLLIDER_AABB;
	box.extents = (vec3_t){ 0.5f, 0.5f, 0.5f };
	box.radius = 0.0f;

	entity_id_t left = test_add_node(&scene, (vec3_t){ 0.0f, 0.0f, 0.0f }, sphere);
	entity_id_t right = test_add_node(&scene, (vec3_t){ 0.75f, 0.0f, 0.0f }, box);
	entity_id_t far = test_add_node(&scene, (vec3_t){ 20.0f, 0.0f, 0.0f }, sphere);
	if (left == NB_ENTITY_INVALID || right == NB_ENTITY_INVALID || far == NB_ENTITY_INVALID) {
		scene_shutdown(&scene);
		return 1;
	}

	scene_update_world_transforms(&scene);

	collision_state_t state;
	collision_state_init(&state);
	collision_events_t events;
	if (!collision_update(&scene, &state, &events) || events.count != 1 || events.events[0].left != left || events.events[0].right != right) {
		scene_shutdown(&scene);
		return 1;
	}

	if (collision_update(&scene, &state, &events) || events.count != 0) {
		scene_shutdown(&scene);
		return 1;
	}

	node_t* node = scene_get_node(&scene, right);
	node->transform.position.x = 5.0f;
	scene_update_world_transforms(&scene);
	if (collision_update(&scene, &state, &events) || events.count != 0) {
		scene_shutdown(&scene);
		return 1;
	}

	node->transform.position.x = 0.75f;
	scene_update_world_transforms(&scene);
	if (!collision_update(&scene, &state, &events) || events.count != 1) {
		scene_shutdown(&scene);
		return 1;
	}

	vec3_t moved = collision_move_sphere(&scene, left, (vec3_t){ 0.0f, 0.0f, 0.0f }, (vec3_t){ 1.0f, 0.0f, 0.0f }, 0.2f);
	if (moved.x != 0.0f || moved.z != 0.0f) {
		scene_shutdown(&scene);
		return 1;
	}

	moved = collision_move_sphere(&scene, left, (vec3_t){ 0.0f, 0.0f, 0.0f }, (vec3_t){ -1.0f, 0.0f, 0.5f }, 0.2f);
	if (moved.x != -1.0f || moved.z != 0.5f) {
		scene_shutdown(&scene);
		return 1;
	}

	scene_shutdown(&scene);
	printf("collision test ok\n");

	return 0;
}
