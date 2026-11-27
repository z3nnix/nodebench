#ifndef NODEBENCH_SCENE_SCENE_H
#define NODEBENCH_SCENE_SCENE_H

#include "core/math.h"
#include "resource/resource.h"

#define NB_ENTITY_INVALID 0
#define NB_SCENE_MAX_ENTITIES 256
#define NB_NODE_NAME_CAPACITY 64
#define NB_NODE_MAX_CHILDREN 128

#define NB_NODE_COMPONENT_TRANSFORM 0x00000001u
#define NB_NODE_COMPONENT_MESH_RENDERER 0x00000002u
#define NB_NODE_COMPONENT_CAMERA 0x00000004u
#define NB_NODE_COMPONENT_LIGHT 0x00000008u
#define NB_NODE_COMPONENT_SCRIPT 0x00000010u
#define NB_NODE_COMPONENT_COLLIDER 0x00000020u
#define NB_NODE_COMPONENT_AUDIO 0x00000040u

typedef uint32_t entity_id_t;

typedef struct {
	vec3_t position;
	vec3_t rotation;
	vec3_t previous_rotation;
	vec3_t initial_rotation;
	vec3_t scale;
	float spin_speed;
} transform_t;

typedef struct {
	mesh_id mesh;
	material_id material;
} mesh_renderer_t;

typedef struct {
	vec3_t eye;
	vec3_t target;
	vec3_t up;
	float fov;
	float near_plane;
	float far_plane;
} camera_t;

typedef struct {
	vec3_t direction;
	float intensity;
} light_t;

typedef enum {
	NB_COLLIDER_AABB = 0,
	NB_COLLIDER_SPHERE = 1,
	NB_COLLIDER_SHAPE_COUNT = 2
} collider_shape_t;

typedef struct {
	collider_shape_t shape;
	vec3_t extents;
	float radius;
} collider_t;

typedef struct {
	entity_id_t id;
	entity_id_t parent;
	entity_id_t children[NB_NODE_MAX_CHILDREN];
	int child_count;
	char name[NB_NODE_NAME_CAPACITY];
	unsigned int components;
	transform_t transform;
	mesh_renderer_t mesh_renderer;
	camera_t camera;
	light_t light;
	collider_t collider;
	script_id script;
	void* script_instance;
	char script_module[NB_NODE_NAME_CAPACITY];
	int visible;
	int world_visible;
	int tint_enabled;
	vec3_t tint;
	vec3_t world_position;
	mat4_t world_matrix;
	mat4_t previous_world_matrix;
} node_t;

typedef struct {
	node_t nodes[NB_SCENE_MAX_ENTITIES];
	int node_count;

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
	entity_id_t active_camera;

	light_t lights[NB_SCENE_MAX_ENTITIES];
	entity_id_t light_entities[NB_SCENE_MAX_ENTITIES];
	int light_count;

	collider_t colliders[NB_SCENE_MAX_ENTITIES];
	entity_id_t collider_entities[NB_SCENE_MAX_ENTITIES];
	int collider_count;
} scene_t;

void scene_init(scene_t* scene);
void scene_shutdown(scene_t* scene);
entity_id_t scene_create_entity(scene_t* scene);
node_t* scene_get_node(scene_t* scene, entity_id_t id);
entity_id_t scene_find_node(scene_t* scene, const char* name);
entity_id_t scene_find_node_path(scene_t* scene, const char* path);
int scene_set_node_name(scene_t* scene, entity_id_t id, const char* name);
int scene_set_node_parent(scene_t* scene, entity_id_t id, entity_id_t parent);
int scene_is_ancestor(scene_t* scene, entity_id_t ancestor, entity_id_t id);
int scene_move_node(scene_t* scene, entity_id_t id, entity_id_t parent, int index);
int scene_set_node_visible(scene_t* scene, entity_id_t id, int visible);
int scene_node_visible(scene_t* scene, entity_id_t id);
int scene_set_node_tint(scene_t* scene, entity_id_t id, vec3_t tint);
void scene_update_world_transforms(scene_t* scene);
void scene_rebuild_cache(scene_t* scene);

int scene_add_transform(scene_t* scene, entity_id_t id, transform_t transform);
int scene_add_mesh_renderer(scene_t* scene, entity_id_t id, mesh_renderer_t renderer);
int scene_add_camera(scene_t* scene, entity_id_t id, camera_t camera);
int scene_add_light(scene_t* scene, entity_id_t id, light_t light);
int scene_add_collider(scene_t* scene, entity_id_t id, collider_t collider);
int scene_add_script(scene_t* scene, entity_id_t id, script_id script, const char* module);
int scene_remove_entity(scene_t* scene, entity_id_t id);
int scene_duplicate_entity(scene_t* scene, entity_id_t id);
int scene_set_active_camera(scene_t* scene, entity_id_t id);

transform_t* scene_get_transform(scene_t* scene, entity_id_t id);
mesh_renderer_t* scene_get_mesh_renderer(scene_t* scene, entity_id_t id);
camera_t* scene_get_camera(scene_t* scene, entity_id_t id);
camera_t* scene_get_active_camera(scene_t* scene);
light_t* scene_get_light(scene_t* scene, entity_id_t id);
collider_t* scene_get_collider(scene_t* scene, entity_id_t id);

#endif
