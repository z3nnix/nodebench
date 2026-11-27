#ifndef NB_RESOURCE_RESOURCE_H
#define NB_RESOURCE_RESOURCE_H

#include "audio/audio.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "resource/resource_id.h"

#define NB_RESOURCE_MAX_SLOTS 65535
#define NB_RESOURCE_INITIAL_MESHES 8
#define NB_RESOURCE_INITIAL_TEXTURES 8
#define NB_RESOURCE_INITIAL_MATERIALS 8
#define NB_RESOURCE_INITIAL_SCRIPTS 8
#define NB_RESOURCE_INITIAL_AUDIO 8

typedef struct {
	uint32_t generation;
	mesh_t* mesh;
} mesh_slot_t;

typedef struct {
	uint32_t generation;
	texture_t* texture;
} texture_slot_t;

typedef struct {
	uint32_t generation;
	material_t* material;
} material_slot_t;

typedef struct {
	uint32_t generation;
	char* source;
} script_slot_t;

typedef struct {
	uint32_t generation;
	audio_stream_t* stream;
	char name[NB_AUDIO_MAX_NAME];
} audio_slot_t;

typedef struct resource_store_t {
	mesh_slot_t* meshes;
	int mesh_count;
	int mesh_capacity;

	texture_slot_t* textures;
	int texture_count;
	int texture_capacity;

	material_slot_t* materials;
	int material_count;
	int material_capacity;

	script_slot_t* scripts;
	int script_count;
	int script_capacity;

	audio_slot_t* audio;
	int audio_count;
	int audio_capacity;

	int vram_limit;
	int vram_used;
} resource_store_t;

void resource_store_init(resource_store_t* store, int vram_limit);
void resource_store_shutdown(resource_store_t* store);
int resource_vram_used(const resource_store_t* store);

mesh_id resource_add_mesh(resource_store_t* store, const mesh_t* mesh);
texture_id resource_add_texture(resource_store_t* store, const texture_t* texture);
material_id resource_add_material(resource_store_t* store, const material_t* material);
script_id resource_add_script(resource_store_t* store, const char* source);
audio_id resource_add_audio(resource_store_t* store, const audio_stream_t* stream, const char* name);

mesh_t* resource_get_mesh(resource_store_t* store, mesh_id id);
texture_t* resource_get_texture(resource_store_t* store, texture_id id);
material_t* resource_get_material(resource_store_t* store, material_id id);
const char* resource_get_script(resource_store_t* store, script_id id);
audio_stream_t* resource_get_audio(resource_store_t* store, audio_id id);
audio_id resource_find_audio(resource_store_t* store, const char* name);

void resource_remove_mesh(resource_store_t* store, mesh_id id);
void resource_remove_texture(resource_store_t* store, texture_id id);
void resource_remove_material(resource_store_t* store, material_id id);
void resource_remove_script(resource_store_t* store, script_id id);
void resource_remove_audio(resource_store_t* store, audio_id id);

#endif
