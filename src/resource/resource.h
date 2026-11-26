#ifndef NB_RESOURCE_RESOURCE_H
#define NB_RESOURCE_RESOURCE_H

#include "audio/audio.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "resource/resource_id.h"

#define NB_RESOURCE_MAX_MESHES 64
#define NB_RESOURCE_MAX_TEXTURES 64
#define NB_RESOURCE_MAX_MATERIALS 64
#define NB_RESOURCE_MAX_SCRIPTS 64
#define NB_RESOURCE_MAX_AUDIO 32

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
	mesh_slot_t meshes[NB_RESOURCE_MAX_MESHES];
	texture_slot_t textures[NB_RESOURCE_MAX_TEXTURES];
	material_slot_t materials[NB_RESOURCE_MAX_MATERIALS];
	script_slot_t scripts[NB_RESOURCE_MAX_SCRIPTS];
	audio_slot_t audio[NB_RESOURCE_MAX_AUDIO];
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
