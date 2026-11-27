#ifndef NODEBENCH_SCENE_TSCN_H
#define NODEBENCH_SCENE_TSCN_H

#include <stddef.h>
#include <stdint.h>

#include "asset/asset_db.h"
#include "core/result.h"
#include "resource/loader.h"
#include "scene/scene.h"

typedef struct engine_t engine_t;

#define NB_TSCN_LINE_CAPACITY 1024

typedef struct {
	char id[64];
	char type[64];
	char uuid[NB_ASSET_DB_UUID_SIZE];
	char path[256];
	nb_resource_kind_t kind;
	uint32_t handle;
	image_t image;
	palette_t palette;
	int has_image;
	int has_palette;
} tscn_ext_t;

typedef struct {
	char name[64];
	char type[64];
	char parent[128];
	transform_t transform;
	camera_t camera;
	light_t light;
	collider_t collider;
	int has_transform;
	int has_collider;
	int has_mesh_ref;
	int has_material_ref;
	int has_script_ref;
	int visible;
	int tint_enabled;
	vec3_t tint;
	char mesh_ref[64];
	char material_ref[64];
	char script_ref[64];
	nb_resource_ref_t mesh;
	nb_resource_ref_t material;
	nb_resource_ref_t script;
} tscn_node_t;

typedef struct {
	tscn_ext_t* ext;
	int ext_count;
	int ext_capacity;
	tscn_node_t* nodes;
	int node_count;
	int node_capacity;
} tscn_file_t;

void tscn_file_init(tscn_file_t* file);
void tscn_file_free(tscn_file_t* file);
nb_result_t tscn_file_load(tscn_file_t* file, const char* path, engine_t* engine);
nb_result_t tscn_file_load_from_buffer(tscn_file_t* file, const char* path, const char* data, size_t size, engine_t* engine);
nb_result_t tscn_file_build_scene(const tscn_file_t* file, engine_t* engine, scene_t* scene);
const tscn_ext_t* tscn_file_find_ext_by_id(const tscn_file_t* file, const char* id);
const tscn_ext_t* tscn_file_find_ext(const tscn_file_t* file, nb_resource_kind_t kind, uint32_t handle);

#endif
