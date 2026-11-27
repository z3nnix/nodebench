#include "physics/collision.h"

#include <string.h>

static uint64_t collision_pair_key(entity_id_t left, entity_id_t right) {
	if (left > right) {
		entity_id_t swap = left;
		left = right;
		right = swap;
	}

	return ((uint64_t)left << 32) | (uint64_t)right;
}

static int collision_pair_contains(const uint64_t* pairs, int count, uint64_t key) {
	for (int i = 0; i < count; i++) {
		if (pairs[i] == key) {
			return 1;
		}
	}

	return 0;
}

static float collision_abs(float value) {
	return value < 0.0f ? -value : value;
}

static vec3_t collision_collider_extents(const collider_t* collider, const node_t* node) {
	return (vec3_t){
		collider->extents.x * collision_abs(node->transform.scale.x),
		collider->extents.y * collision_abs(node->transform.scale.y),
		collider->extents.z * collision_abs(node->transform.scale.z)
	};
}

static float collision_collider_radius(const collider_t* collider, const node_t* node) {
	float scale = collision_abs(node->transform.scale.x);
	if (collision_abs(node->transform.scale.y) > scale) {
		scale = collision_abs(node->transform.scale.y);
	}

	if (collision_abs(node->transform.scale.z) > scale) {
		scale = collision_abs(node->transform.scale.z);
	}

	return collider->radius * scale;
}

static float collision_clamp(float value, float low, float high) {
	if (value < low) {
		return low;
	}

	if (value > high) {
		return high;
	}

	return value;
}

static int collision_aabb_overlap(const vec3_t* left_center, const vec3_t* left_extents, const vec3_t* right_center, const vec3_t* right_extents) {
	if (collision_abs(left_center->x - right_center->x) > left_extents->x + right_extents->x) {
		return 0;
	}

	if (collision_abs(left_center->y - right_center->y) > left_extents->y + right_extents->y) {
		return 0;
	}

	if (collision_abs(left_center->z - right_center->z) > left_extents->z + right_extents->z) {
		return 0;
	}

	return 1;
}

static int collision_sphere_overlap(const vec3_t* left_center, float left_radius, const vec3_t* right_center, float right_radius) {
	float dx = left_center->x - right_center->x;
	float dy = left_center->y - right_center->y;
	float dz = left_center->z - right_center->z;
	float radius = left_radius + right_radius;

	return dx * dx + dy * dy + dz * dz <= radius * radius;
}

static int collision_sphere_aabb_overlap(const vec3_t* sphere_center, float sphere_radius, const vec3_t* box_center, const vec3_t* box_extents) {
	vec3_t nearest;
	nearest.x = collision_clamp(sphere_center->x, box_center->x - box_extents->x, box_center->x + box_extents->x);
	nearest.y = collision_clamp(sphere_center->y, box_center->y - box_extents->y, box_center->y + box_extents->y);
	nearest.z = collision_clamp(sphere_center->z, box_center->z - box_extents->z, box_center->z + box_extents->z);

	float dx = sphere_center->x - nearest.x;
	float dy = sphere_center->y - nearest.y;
	float dz = sphere_center->z - nearest.z;

	return dx * dx + dy * dy + dz * dz <= sphere_radius * sphere_radius;
}

void collision_state_init(collision_state_t* state) {
	if (!state) {
		return;
	}

	memset(state, 0, sizeof(*state));
}

int collision_overlap(const collider_t* left, const node_t* left_node, const collider_t* right, const node_t* right_node) {
	if (!left || !left_node || !right || !right_node) {
		return 0;
	}

	const vec3_t* left_center = &left_node->world_position;
	const vec3_t* right_center = &right_node->world_position;

	if (left->shape == NB_COLLIDER_AABB && right->shape == NB_COLLIDER_AABB) {
		vec3_t left_extents = collision_collider_extents(left, left_node);
		vec3_t right_extents = collision_collider_extents(right, right_node);

		return collision_aabb_overlap(left_center, &left_extents, right_center, &right_extents);
	}

	if (left->shape == NB_COLLIDER_SPHERE && right->shape == NB_COLLIDER_SPHERE) {
		return collision_sphere_overlap(left_center, collision_collider_radius(left, left_node), right_center, collision_collider_radius(right, right_node));
	}

	if (left->shape == NB_COLLIDER_SPHERE) {
		vec3_t right_extents = collision_collider_extents(right, right_node);

		return collision_sphere_aabb_overlap(left_center, collision_collider_radius(left, left_node), right_center, &right_extents);
	}

	vec3_t left_extents = collision_collider_extents(left, left_node);

	return collision_sphere_aabb_overlap(right_center, collision_collider_radius(right, right_node), left_center, &left_extents);
}

int collision_sphere_blocked(const scene_t* scene, entity_id_t self, vec3_t position, float radius) {
	if (!scene || radius <= 0.0f) {
		return 0;
	}

	for (int i = 0; i < scene->collider_count; i++) {
		entity_id_t id = scene->collider_entities[i];
		if (id == self) {
			continue;
		}

		node_t* node = scene_get_node((scene_t*)scene, id);
		if (!node || !node->world_visible) {
			continue;
		}

		const collider_t* collider = &scene->colliders[i];

		if (collider->shape == NB_COLLIDER_AABB) {
			vec3_t extents = collision_collider_extents(collider, node);
			float nearest_x = collision_clamp(position.x, node->world_position.x - extents.x, node->world_position.x + extents.x);
			float nearest_z = collision_clamp(position.z, node->world_position.z - extents.z, node->world_position.z + extents.z);
			float dx = position.x - nearest_x;
			float dz = position.z - nearest_z;

			if (dx * dx + dz * dz < radius * radius) {
				return 1;
			}
		}

		else if (collider->shape == NB_COLLIDER_SPHERE) {
			float dx = position.x - node->world_position.x;
			float dz = position.z - node->world_position.z;
			float combined = radius + collision_collider_radius(collider, node);

			if (dx * dx + dz * dz < combined * combined) {
				return 1;
			}
		}
	}

	return 0;
}

vec3_t collision_move_sphere(const scene_t* scene, entity_id_t self, vec3_t position, vec3_t delta, float radius) {
	vec3_t result = position;

	if (delta.y != 0.0f) {
		vec3_t candidate = (vec3_t){ result.x, result.y + delta.y, result.z };
		if (!collision_sphere_blocked(scene, self, candidate, radius)) {
			result.y = candidate.y;
		}
	}

	if (delta.x != 0.0f) {
		vec3_t candidate = (vec3_t){ result.x + delta.x, result.y, result.z };
		if (!collision_sphere_blocked(scene, self, candidate, radius)) {
			result.x = candidate.x;
		}
	}

	if (delta.z != 0.0f) {
		vec3_t candidate = (vec3_t){ result.x, result.y, result.z + delta.z };
		if (!collision_sphere_blocked(scene, self, candidate, radius)) {
			result.z = candidate.z;
		}
	}

	return result;
}

int collision_update(const scene_t* scene, collision_state_t* state, collision_events_t* events) {
	if (!scene || !state || !events) {
		return 0;
	}

	uint64_t current[NB_COLLISION_MAX_PAIRS];
	int current_count = 0;
	events->count = 0;

	for (int i = 0; i < scene->collider_count && current_count < NB_COLLISION_MAX_PAIRS; i++) {
		entity_id_t left_id = scene->collider_entities[i];
		const collider_t* left = &scene->colliders[i];
		node_t* left_node = scene_get_node((scene_t*)scene, left_id);
		if (!left_node) {
			continue;
		}

		for (int j = i + 1; j < scene->collider_count && current_count < NB_COLLISION_MAX_PAIRS; j++) {
			entity_id_t right_id = scene->collider_entities[j];
			const collider_t* right = &scene->colliders[j];
			node_t* right_node = scene_get_node((scene_t*)scene, right_id);
			if (!right_node) {
				continue;
			}

			if (!collision_overlap(left, left_node, right, right_node)) {
				continue;
			}

			uint64_t key = collision_pair_key(left_id, right_id);
			if (collision_pair_contains(current, current_count, key)) {
				continue;
			}

			current[current_count++] = key;
			if (collision_pair_contains(state->pairs, state->count, key)) {
				continue;
			}

			if (events->count >= NB_COLLISION_MAX_EVENTS) {
				continue;
			}

			entity_id_t first = (entity_id_t)(key >> 32);
			entity_id_t second = (entity_id_t)(key & 0xffffffffu);
			events->events[events->count].left = first;
			events->events[events->count].right = second;
			events->count++;
		}
	}

	memcpy(state->pairs, current, (size_t)current_count * sizeof(uint64_t));
	state->count = current_count;

	return events->count;
}
