#ifndef NB_SCENE_SCENE_DOC_H
#define NB_SCENE_SCENE_DOC_H

#include <stdint.h>

#include "core/result.h"
#include "scene/scene.h"
#include "scene/tscn.h"

typedef struct engine_t engine_t;

#define NB_SCENE_DOC_STR 256
#define NB_SCENE_DOC_ID 64

typedef tscn_ext_t scene_doc_ext_t;

typedef struct {
	char path[NB_SCENE_DOC_STR];
	scene_doc_ext_t* ext;
	int ext_count;
	int ext_capacity;
	int dirty;
} scene_doc_t;

void scene_doc_init(scene_doc_t* doc);
void scene_doc_clear(scene_doc_t* doc);
nb_result_t scene_doc_load(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene);
nb_result_t scene_doc_save(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene);
nb_result_t scene_doc_save_to_buffer(scene_doc_t* doc, scene_t* scene, char** out_data, size_t* out_size);
nb_result_t scene_doc_load_from_buffer(scene_doc_t* doc, const char* path, const char* data, size_t size, engine_t* engine, scene_t* scene);
const scene_doc_ext_t* scene_doc_find_ext(const scene_doc_t* doc, nb_resource_kind_t kind, uint32_t handle);
const scene_doc_ext_t* scene_doc_find_ext_by_id(const scene_doc_t* doc, const char* id);
int scene_doc_add_ext(scene_doc_t* doc, const scene_doc_ext_t* ext);

#endif
