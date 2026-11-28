#ifndef NODEBENCH_RESOURCE_RESOURCE_ID_H
#define NODEBENCH_RESOURCE_RESOURCE_ID_H

#include <stdint.h>

#define NB_RESOURCE_INVALID_ID 0u

typedef enum {
	NB_RESOURCE_KIND_NONE = 0,
	NB_RESOURCE_KIND_MESH,
	NB_RESOURCE_KIND_TEXTURE,
	NB_RESOURCE_KIND_PALETTE,
	NB_RESOURCE_KIND_MATERIAL,
	NB_RESOURCE_KIND_SCRIPT,
	NB_RESOURCE_KIND_AUDIO,
	NB_RESOURCE_KIND_PACKED_SCENE,
	NB_RESOURCE_KIND_COUNT
} nb_resource_kind_t;

typedef struct {
	nb_resource_kind_t kind;
	uint32_t handle;
} nb_resource_ref_t;

typedef uint32_t mesh_id;
typedef uint32_t texture_id;
typedef uint32_t material_id;
typedef uint32_t script_id;
typedef uint32_t audio_id;
typedef uint32_t packed_scene_id;

#endif
