#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/dynamic_array.h"
#include "resource/resource.h"

static uint32_t resource_make_id(int index, uint32_t generation) {
	return (generation << 16) | (uint32_t)(index + 1);
}

static int resource_index(uint32_t id) {
	if ((id & 0xffffu) == 0) { return -1; }
	return (int)((id & 0xffffu) - 1u);
}

static uint32_t resource_generation(uint32_t id) {
	return id >> 16;
}

static void resource_free_mesh(mesh_t* mesh) {
	if (!mesh) { return; }

	free(mesh->vertices);
	free(mesh->triangles);
	free(mesh);
}

static void resource_free_texture(texture_t* texture) {
	if (!texture) { return; }

	free((void*)texture->pixels);
	free((void*)texture->palette);
	free(texture);
}

static void resource_free_audio(audio_stream_t* stream) {
	if (!stream) { return; }

	free(stream->samples);
	free(stream);
}

static int resource_is_pot(int value) {
	return value > 0 && (value & (value - 1)) == 0;
}

static int resource_texture_layout(const texture_t* texture, size_t* pixel_bytes, size_t* palette_bytes) {
	if (!texture || !pixel_bytes || !palette_bytes) { return 0; }

	assert(texture->width > 0 && texture->height > 0);
	assert(resource_is_pot(texture->width) && resource_is_pot(texture->height));

	if (!resource_is_pot(texture->width) || !resource_is_pot(texture->height)) { return 0; }
	if (!texture->pixels || !texture->palette) { return 0; }

	int bits = 0;
	int palette_entries = 0;

	if (texture->format == NB_TEXTURE_4BIT) {
		bits = 4;
		palette_entries = 16;
	}

	else if (texture->format == NB_TEXTURE_8BIT) {
		bits = 8;
		palette_entries = 256;
	}

	else {
		return 0;
	}

	*pixel_bytes = (size_t)texture->width * (size_t)texture->height * (size_t)bits / 8;
	*palette_bytes = (size_t)palette_entries * 2;

	return *pixel_bytes > INT_MAX || *palette_bytes > INT_MAX ? 0 : 1;
}

static void* resource_grow_slots(void* data, int* count, int* capacity, size_t element_size) {
	if (!count || !capacity || *count >= NB_RESOURCE_MAX_SLOTS) {
		return NULL;
	}

	void* grown = nb_array_grow(data, capacity, *count + 1, element_size);
	if (!grown) {
		return NULL;
	}

	memset((unsigned char*)grown + (size_t)*count * element_size, 0, element_size);
	(*count)++;

	return grown;
}

void resource_store_init(resource_store_t* store, int vram_limit) {
	if (!store) { return; }

	memset(store, 0, sizeof(*store));
	store->vram_limit = vram_limit > 0 ? vram_limit : 1048576;
}

void resource_store_shutdown(resource_store_t* store) {
	if (!store) { return; }

	for (int i = 0; i < store->mesh_count; i++) {
		mesh_slot_t* slot = &store->meshes[i];
		if (slot->mesh) {
			resource_free_mesh(slot->mesh);
		}
	}

	for (int i = 0; i < store->texture_count; i++) {
		texture_slot_t* slot = &store->textures[i];
		if (slot->texture) {
			resource_free_texture(slot->texture);
		}
	}

	for (int i = 0; i < store->material_count; i++) {
		material_slot_t* slot = &store->materials[i];
		if (slot->material) {
			free(slot->material);
		}
	}

	for (int i = 0; i < store->script_count; i++) {
		script_slot_t* slot = &store->scripts[i];
		if (slot->source) {
			free(slot->source);
		}
	}

	for (int i = 0; i < store->audio_count; i++) {
		audio_slot_t* slot = &store->audio[i];
		if (slot->stream) {
			resource_free_audio(slot->stream);
		}
	}

	nb_array_free(store->meshes);
	nb_array_free(store->textures);
	nb_array_free(store->materials);
	nb_array_free(store->scripts);
	nb_array_free(store->audio);

	memset(store, 0, sizeof(*store));
}

int resource_vram_used(const resource_store_t* store) {
	return store ? store->vram_used : 0;
}

mesh_id resource_add_mesh(resource_store_t* store, const mesh_t* mesh) {
	if (!store || !mesh || !mesh->vertices || !mesh->triangles) { return NB_RESOURCE_INVALID_ID; }
	if (mesh->vertex_count <= 0 || mesh->triangle_count <= 0) { return NB_RESOURCE_INVALID_ID; }

	mesh_slot_t* slot = NULL;
	for (int i = 0; i < store->mesh_count; i++) {
		if (!store->meshes[i].mesh) {
			slot = &store->meshes[i];
			break;
		}
	}

	if (!slot) {
		void* grown = resource_grow_slots(store->meshes, &store->mesh_count, &store->mesh_capacity, sizeof(*store->meshes));
		if (!grown) { return NB_RESOURCE_INVALID_ID; }
		store->meshes = grown;
		slot = &store->meshes[store->mesh_count - 1];
	}

	mesh_t* copy = (mesh_t*)malloc(sizeof(mesh_t));
	if (!copy) { return NB_RESOURCE_INVALID_ID; }

	copy->vertices = NULL;
	copy->triangles = NULL;
	copy->vertex_count = 0;
	copy->triangle_count = 0;

	copy->vertices = (vertex_t*)malloc((size_t)mesh->vertex_count * sizeof(vertex_t));
	copy->triangles = (triangle_t*)malloc((size_t)mesh->triangle_count * sizeof(triangle_t));
	if (!copy->vertices || !copy->triangles) {
		resource_free_mesh(copy);
		return NB_RESOURCE_INVALID_ID;
	}

	memcpy(copy->vertices, mesh->vertices, (size_t)mesh->vertex_count * sizeof(vertex_t));
	memcpy(copy->triangles, mesh->triangles, (size_t)mesh->triangle_count * sizeof(triangle_t));
	copy->vertex_count = mesh->vertex_count;
	copy->triangle_count = mesh->triangle_count;

	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
	slot->mesh = copy;

	int index = (int)(slot - store->meshes);

	return resource_make_id(index, slot->generation);
}

texture_id resource_add_texture(resource_store_t* store, const texture_t* texture) {
	if (!store || !texture) { return NB_RESOURCE_INVALID_ID; }

	size_t pixel_bytes = 0;
	size_t palette_bytes = 0;
	if (!resource_texture_layout(texture, &pixel_bytes, &palette_bytes)) { return NB_RESOURCE_INVALID_ID; }

	int total_bytes = (int)(pixel_bytes + palette_bytes);
	assert(store->vram_limit >= 0 && store->vram_used >= 0 && total_bytes >= 0);
	if (store->vram_used + total_bytes > store->vram_limit) { return NB_RESOURCE_INVALID_ID; }

	texture_slot_t* slot = NULL;
	for (int i = 0; i < store->texture_count; i++) {
		if (!store->textures[i].texture) {
			slot = &store->textures[i];
			break;
		}
	}

	if (!slot) {
		void* grown = resource_grow_slots(store->textures, &store->texture_count, &store->texture_capacity, sizeof(*store->textures));
		if (!grown) { return NB_RESOURCE_INVALID_ID; }
		store->textures = grown;
		slot = &store->textures[store->texture_count - 1];
	}

	uint8_t* pixels = (uint8_t*)malloc(pixel_bytes);
	uint16_t* palette = (uint16_t*)malloc(palette_bytes);
	texture_t* copy = (texture_t*)malloc(sizeof(texture_t));
	if (!pixels || !palette || !copy) {
		free(pixels);
		free(palette);
		free(copy);
		return NB_RESOURCE_INVALID_ID;
	}

	memcpy(pixels, texture->pixels, pixel_bytes);
	memcpy(palette, texture->palette, palette_bytes);

	copy->width = texture->width;
	copy->height = texture->height;
	copy->format = texture->format;
	copy->pixels = pixels;
	copy->palette = palette;
	copy->transparent_index = texture->transparent_index;

	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
	slot->texture = copy;
	store->vram_used += total_bytes;

	int index = (int)(slot - store->textures);

	return resource_make_id(index, slot->generation);
}

material_id resource_add_material(resource_store_t* store, const material_t* material) {
	if (!store || !material) { return NB_RESOURCE_INVALID_ID; }

	material_slot_t* slot = NULL;
	for (int i = 0; i < store->material_count; i++) {
		if (!store->materials[i].material) {
			slot = &store->materials[i];
			break;
		}
	}

	if (!slot) {
		void* grown = resource_grow_slots(store->materials, &store->material_count, &store->material_capacity, sizeof(*store->materials));
		if (!grown) { return NB_RESOURCE_INVALID_ID; }
		store->materials = grown;
		slot = &store->materials[store->material_count - 1];
	}

	material_t* copy = (material_t*)malloc(sizeof(material_t));
	if (!copy) { return NB_RESOURCE_INVALID_ID; }

	*copy = *material;

	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
	slot->material = copy;

	int index = (int)(slot - store->materials);

	return resource_make_id(index, slot->generation);
}

script_id resource_add_script(resource_store_t* store, const char* source) {
	if (!store || !source) { return NB_RESOURCE_INVALID_ID; }

	script_slot_t* slot = NULL;
	for (int i = 0; i < store->script_count; i++) {
		if (!store->scripts[i].source) {
			slot = &store->scripts[i];
			break;
		}
	}

	if (!slot) {
		void* grown = resource_grow_slots(store->scripts, &store->script_count, &store->script_capacity, sizeof(*store->scripts));
		if (!grown) { return NB_RESOURCE_INVALID_ID; }
		store->scripts = grown;
		slot = &store->scripts[store->script_count - 1];
	}

	size_t length = strlen(source) + 1;
	char* copy = (char*)malloc(length);
	if (!copy) { return NB_RESOURCE_INVALID_ID; }

	memcpy(copy, source, length);
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
	slot->source = copy;

	int index = (int)(slot - store->scripts);

	return resource_make_id(index, slot->generation);
}

audio_id resource_add_audio(resource_store_t* store, const audio_stream_t* stream, const char* name) {
	if (!store || !stream || !stream->samples || stream->frame_count <= 0 || stream->sample_rate == 0 || (stream->channels != 1 && stream->channels != 2) || !name || !*name) {
		return NB_RESOURCE_INVALID_ID;
	}

	if (strlen(name) >= NB_AUDIO_MAX_NAME) {
		return NB_RESOURCE_INVALID_ID;
	}

	audio_slot_t* slot = NULL;
	for (int i = 0; i < store->audio_count; i++) {
		if (!store->audio[i].stream) {
			slot = &store->audio[i];
			break;
		}
	}

	if (!slot) {
		void* grown = resource_grow_slots(store->audio, &store->audio_count, &store->audio_capacity, sizeof(*store->audio));
		if (!grown) { return NB_RESOURCE_INVALID_ID; }
		store->audio = grown;
		slot = &store->audio[store->audio_count - 1];
	}

	size_t bytes = (size_t)stream->frame_count * (size_t)stream->channels * sizeof(int16_t);
	audio_stream_t* copy = (audio_stream_t*)malloc(sizeof(audio_stream_t));
	if (!copy) { return NB_RESOURCE_INVALID_ID; }

	copy->samples = (int16_t*)malloc(bytes);
	if (!copy->samples) {
		free(copy);
		return NB_RESOURCE_INVALID_ID;
	}

	memcpy(copy->samples, stream->samples, bytes);
	copy->sample_rate = stream->sample_rate;
	copy->channels = stream->channels;
	copy->frame_count = stream->frame_count;

	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
	slot->stream = copy;
	snprintf(slot->name, sizeof(slot->name), "%s", name);

	int index = (int)(slot - store->audio);

	return resource_make_id(index, slot->generation);
}

mesh_t* resource_get_mesh(resource_store_t* store, mesh_id id) {
	if (!store || id == NB_RESOURCE_INVALID_ID) { return NULL; }

	int index = resource_index(id);

	if (index < 0 || index >= store->mesh_count) { return NULL; }

	mesh_slot_t* slot = &store->meshes[index];
	if (!slot->mesh || slot->generation != resource_generation(id)) { return NULL; }

	return slot->mesh;
}

texture_t* resource_get_texture(resource_store_t* store, texture_id id) {
	if (!store || id == NB_RESOURCE_INVALID_ID) { return NULL; }

	int index = resource_index(id);

	if (index < 0 || index >= store->texture_count) { return NULL; }

	texture_slot_t* slot = &store->textures[index];
	if (!slot->texture || slot->generation != resource_generation(id)) { return NULL; }

	return slot->texture;
}

material_t* resource_get_material(resource_store_t* store, material_id id) {
	if (!store || id == NB_RESOURCE_INVALID_ID) { return NULL; }

	int index = resource_index(id);

	if (index < 0 || index >= store->material_count) { return NULL; }

	material_slot_t* slot = &store->materials[index];
	if (!slot->material || slot->generation != resource_generation(id)) { return NULL; }

	return slot->material;
}

const char* resource_get_script(resource_store_t* store, script_id id) {
	if (!store || id == NB_RESOURCE_INVALID_ID) { return NULL; }

	int index = resource_index(id);

	if (index < 0 || index >= store->script_count) { return NULL; }

	script_slot_t* slot = &store->scripts[index];
	if (!slot->source || slot->generation != resource_generation(id)) { return NULL; }

	return slot->source;
}

audio_stream_t* resource_get_audio(resource_store_t* store, audio_id id) {
	if (!store || id == NB_RESOURCE_INVALID_ID) { return NULL; }

	int index = resource_index(id);

	if (index < 0 || index >= store->audio_count) { return NULL; }

	audio_slot_t* slot = &store->audio[index];
	if (!slot->stream || slot->generation != resource_generation(id)) { return NULL; }

	return slot->stream;
}

audio_id resource_find_audio(resource_store_t* store, const char* name) {
	if (!store || !name || !*name) { return NB_RESOURCE_INVALID_ID; }

	for (int i = 0; i < store->audio_count; i++) {
		audio_slot_t* slot = &store->audio[i];
		if (slot->stream && strcmp(slot->name, name) == 0) {
			return resource_make_id(i, slot->generation);
		}
	}

	return NB_RESOURCE_INVALID_ID;
}

void resource_remove_mesh(resource_store_t* store, mesh_id id) {
	mesh_t* mesh = resource_get_mesh(store, id);
	if (!mesh) { return; }

	int index = resource_index(id);
	mesh_slot_t* slot = &store->meshes[index];

	resource_free_mesh(mesh);
	slot->mesh = NULL;
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
}

void resource_remove_texture(resource_store_t* store, texture_id id) {
	texture_t* texture = resource_get_texture(store, id);
	if (!texture) { return; }

	size_t pixel_bytes = 0;
	size_t palette_bytes = 0;
	if (resource_texture_layout(texture, &pixel_bytes, &palette_bytes)) {
		store->vram_used -= (int)(pixel_bytes + palette_bytes);
	}

	int index = resource_index(id);
	texture_slot_t* slot = &store->textures[index];

	resource_free_texture(texture);
	slot->texture = NULL;
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
}

void resource_remove_material(resource_store_t* store, material_id id) {
	material_t* material = resource_get_material(store, id);
	if (!material) { return; }

	int index = resource_index(id);
	material_slot_t* slot = &store->materials[index];

	free(material);
	slot->material = NULL;
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
}

void resource_remove_script(resource_store_t* store, script_id id) {
	const char* source = resource_get_script(store, id);
	if (!source) { return; }

	int index = resource_index(id);
	script_slot_t* slot = &store->scripts[index];

	free((void*)source);
	slot->source = NULL;
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
}

void resource_remove_audio(resource_store_t* store, audio_id id) {
	audio_stream_t* stream = resource_get_audio(store, id);
	if (!stream) { return; }

	int index = resource_index(id);
	audio_slot_t* slot = &store->audio[index];

	resource_free_audio(stream);
	slot->stream = NULL;
	slot->generation++;
	if (slot->generation == 0) { slot->generation = 1; }
}
