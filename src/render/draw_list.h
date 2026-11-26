#ifndef NODEBENCH_RENDER_DRAW_LIST_H
#define NODEBENCH_RENDER_DRAW_LIST_H

#include "core/math.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/texture.h"

#define NB_DRAW_LIST_MAX_COMMANDS 256

typedef struct {
	const mesh_t* mesh;
	const texture_t* texture;
	const material_t* material;
	mat4_t model;
	float depth;
} draw_command_t;

typedef struct {
	mat4_t view;
	mat4_t view_projection;
	draw_command_t commands[NB_DRAW_LIST_MAX_COMMANDS];
	int count;
} draw_list_t;

void draw_list_init(draw_list_t* list);
void draw_list_clear(draw_list_t* list);
void draw_list_set_camera(draw_list_t* list, const mat4_t* view, const mat4_t* projection);
int draw_list_push(draw_list_t* list, const mesh_t* mesh, const texture_t* texture, const material_t* material, const mat4_t* model);
void draw_list_sort(draw_list_t* list);

#endif
