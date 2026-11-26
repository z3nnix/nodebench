#ifndef NB_RENDER_RENDERER_H
#define NB_RENDER_RENDERER_H

#include "core/config.h"
#include "core/math.h"
#include "render/framebuffer.h"
#include "render/mesh.h"
#include "render/texture.h"

typedef struct {
	vec4_t clip;
	float x;
	float y;
	float inv_w;
	float u;
	float v;
	float r;
	float g;
	float b;
	float fog;
} projected_vertex_t;

typedef struct {
	projected_vertex_t v[3];
	float depth;
} projected_triangle_t;

typedef struct {
	framebuffer_t* framebuffer;
	const render_config_t* config;
	mat4_t view;
	mat4_t view_projection;
	projected_vertex_t* projected;
	vec3_t* world;
	projected_triangle_t* triangles;
	int projected_capacity;
	int triangle_capacity;
} renderer_t;

int renderer_init(renderer_t* renderer, framebuffer_t* framebuffer);
void renderer_shutdown(renderer_t* renderer);
void renderer_set_camera(renderer_t* renderer, const mat4_t* view, const mat4_t* projection);
void renderer_draw_mesh(renderer_t* renderer, const mesh_t* mesh, const texture_t* texture, const mat4_t* model, nb_blend_mode_t blend_mode);

#endif
