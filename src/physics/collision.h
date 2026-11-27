#ifndef NODEBENCH_PHYSICS_COLLISION_H
#define NODEBENCH_PHYSICS_COLLISION_H

#include "scene/scene.h"

#define NB_COLLISION_MAX_PAIRS 512
#define NB_COLLISION_MAX_EVENTS 64

typedef struct {
	entity_id_t left;
	entity_id_t right;
} collision_event_t;

typedef struct {
	uint64_t pairs[NB_COLLISION_MAX_PAIRS];
	int count;
} collision_state_t;

typedef struct {
	collision_event_t events[NB_COLLISION_MAX_EVENTS];
	int count;
} collision_events_t;

void collision_state_init(collision_state_t* state);
int collision_overlap(const collider_t* left, const node_t* left_node, const collider_t* right, const node_t* right_node);
int collision_update(const scene_t* scene, collision_state_t* state, collision_events_t* events);
int collision_sphere_blocked(const scene_t* scene, entity_id_t self, vec3_t position, float radius);
vec3_t collision_move_sphere(const scene_t* scene, entity_id_t self, vec3_t position, vec3_t delta, float radius);

#endif
