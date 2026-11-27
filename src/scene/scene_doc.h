#ifndef NB_SCENE_SCENE_DOC_H
#define NB_SCENE_SCENE_DOC_H

#include <stdint.h>

#include "core/result.h"
#include "scene/scene.h"

typedef struct engine_t engine_t;

#define NB_SCENE_DOC_MAX_EXT 128
#define NB_SCENE_DOC_MAX_NODES 256
#define NB_SCENE_DOC_STR 256
#define NB_SCENE_DOC_ID 64

typedef struct {
	char id[NB_SCENE_DOC_ID];
	char type[64];
	char path[NB_SCENE_DOC_STR];
	uint32_t handle;
} scene_doc_ext_t;

typedef struct {
	char path[NB_SCENE_DOC_STR];
	scene_doc_ext_t ext[NB_SCENE_DOC_MAX_EXT];
	int ext_count;
	int dirty;
} scene_doc_t;

void scene_doc_init(scene_doc_t* doc);
void scene_doc_clear(scene_doc_t* doc);
nb_result_t scene_doc_load(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene);
nb_result_t scene_doc_save(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene);
nb_result_t scene_doc_save_to_buffer(scene_doc_t* doc, scene_t* scene, char** out_data, size_t* out_size);
nb_result_t scene_doc_load_from_buffer(scene_doc_t* doc, const char* path, const char* data, size_t size, engine_t* engine, scene_t* scene);
const scene_doc_ext_t* scene_doc_find_ext_by_handle(const scene_doc_t* doc, uint32_t handle);
const scene_doc_ext_t* scene_doc_find_ext_by_id(const scene_doc_t* doc, const char* id);

#endif
