#include "render/material.h"

void material_init(material_t* material, texture_id texture, nb_blend_mode_t blend_mode, nb_shade_mode_t shade_mode, int fog_enabled, vec3_t vertex_color_tint) {
	if (!material) {
		return;
	}

	material->texture = texture;
	material->blend_mode = blend_mode;
	material->shade_mode = shade_mode;
	material->fog_enabled = fog_enabled;
	material->vertex_color_tint = vertex_color_tint;
}
