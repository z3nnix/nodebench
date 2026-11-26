#ifndef NODEBENCH_SCENE_SCENE_H
#define NODEBENCH_SCENE_SCENE_H

#include "core/math.h"
#include "resource/resource.h"

#define NB_ENTITY_INVALID 0
#define NB_SCENE_MAX_ENTITIES 256

typedef uint32_t entity_id_t;

typedef struct
{
	vec3_t position;
	vec3_t rotation;
	vec3_t previous_rotation;
	vec3_t initial_rotation;
	vec3_t scale;
	float spin_speed;
} transform_t;

typedef struct
{
	mesh_id mesh;
	material_id material;
} mesh_renderer_t;

typedef struct
{
	vec3_t eye;
	vec3_t target;
	vec3_t up;
	float fov;
	float near_plane;
	float far_plane;
} camera_t;

typedef struct
{
	vec3_t direction;
	float intensity;
} light_t;

typedef struct
{
	entity_id_t ids[NB_SCENE_MAX_ENTITIES];
	int entity_count;

	transform_t transforms[NB_SCENE_MAX_ENTITIES];
	entity_id_t transform_entities[NB_SCENE_MAX_ENTITIES];
	int transform_count;

	mesh_renderer_t mesh_renderers[NB_SCENE_MAX_ENTITIES];
	entity_id_t mesh_renderer_entities[NB_SCENE_MAX_ENTITIES];
	int mesh_renderer_count;

	camera_t cameras[NB_SCENE_MAX_ENTITIES];
	entity_id_t camera_entities[NB_SCENE_MAX_ENTITIES];
	int camera_count;

	light_t lights[NB_SCENE_MAX_ENTITIES];
	entity_id_t light_entities[NB_SCENE_MAX_ENTITIES];
	int light_count;
} scene_t;

void scene_init(scene_t* scene);
void scene_shutdown(scene_t* scene);
entity_id_t scene_create_entity(scene_t* scene);

int scene_add_transform(scene_t* scene, entity_id_t id, transform_t transform);
int scene_add_mesh_renderer(scene_t* scene, entity_id_t id, mesh_renderer_t renderer);
int scene_add_camera(scene_t* scene, entity_id_t id, camera_t camera);
int scene_add_light(scene_t* scene, entity_id_t id, light_t light);

transform_t* scene_get_transform(scene_t* scene, entity_id_t id);
mesh_renderer_t* scene_get_mesh_renderer(scene_t* scene, entity_id_t id);
camera_t* scene_get_camera(scene_t* scene, entity_id_t id);
light_t* scene_get_light(scene_t* scene, entity_id_t id);

#endif
