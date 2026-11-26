#ifndef NODEBENCH_RENDER_MATERIAL_H
#define NODEBENCH_RENDER_MATERIAL_H

#include "core/config.h"
#include "core/math.h"
#include "resource/resource_id.h"

typedef struct {
	texture_id texture;
	nb_blend_mode_t blend_mode;
	nb_shade_mode_t shade_mode;
	int fog_enabled;
	vec3_t vertex_color_tint;
} material_t;

void material_init(material_t* material, texture_id texture, nb_blend_mode_t blend_mode, nb_shade_mode_t shade_mode, int fog_enabled, vec3_t vertex_color_tint);

#endif
