#ifndef NODEBENCH_RENDER_RENDERER_H
#define NODEBENCH_RENDER_RENDERER_H

#include "core/config.h"
#include "core/math.h"
#include "render/draw_list.h"
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
	draw_command_t commands[NB_DRAW_LIST_MAX_COMMANDS];
	int command_count;
	int projected_capacity;
	int triangle_capacity;
	int draw_calls;
	int drawn_triangles;
	int culled_triangles;
	int near_culled_triangles;
} renderer_t;

int renderer_init(renderer_t* renderer, framebuffer_t* framebuffer);
void renderer_shutdown(renderer_t* renderer);
void renderer_submit(renderer_t* renderer, const draw_list_t* list);
void renderer_flush(renderer_t* renderer);

#endif
