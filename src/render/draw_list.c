#include "render/draw_list.h"

#include <stdlib.h>

static int draw_command_compare(const void* left, const void* right) {
	const draw_command_t* a = (const draw_command_t*)left;
	const draw_command_t* b = (const draw_command_t*)right;

	if (a->depth > b->depth) {
		return -1;
	}

	if (a->depth < b->depth) {
		return 1;
	}

	return 0;
}

void draw_list_init(draw_list_t* list) {
	if (!list) {
		return;
	}

	mat4_identity(&list->view);
	mat4_identity(&list->view_projection);
	list->count = 0;
}

void draw_list_clear(draw_list_t* list) {
	if (!list) {
		return;
	}

	list->count = 0;
}

void draw_list_set_camera(draw_list_t* list, const mat4_t* view, const mat4_t* projection) {
	if (!list || !view || !projection) {
		return;
	}

	list->view = *view;
	mat4_multiply(&list->view_projection, projection, view);
}

int draw_list_push(draw_list_t* list, const mesh_t* mesh, const texture_t* texture, const material_t* material, const mat4_t* model) {
	if (!material) {
		return 0;
	}

	return draw_list_push_ex(list, mesh, texture, material, model, material->vertex_color_tint);
}

int draw_list_push_ex(draw_list_t* list, const mesh_t* mesh, const texture_t* texture, const material_t* material, const mat4_t* model, vec3_t tint) {
	if (!list || !mesh || !material || list->count >= NB_DRAW_LIST_MAX_COMMANDS) {
		return 0;
	}

	draw_command_t* command = &list->commands[list->count];
	command->mesh = mesh;
	command->texture = texture;
	command->material = material;
	command->tint = tint;

	if (model) {
		command->model = *model;
	}

	else {
		mat4_identity(&command->model);
	}

	vec4_t position = mat4_transform(&list->view, (vec4_t){ command->model.m[12], command->model.m[13], command->model.m[14], 1.0f });
	command->depth = -position.z;
	list->count++;

	return 1;
}

void draw_list_sort(draw_list_t* list) {
	if (!list || list->count < 2) {
		return;
	}

	qsort(list->commands, (size_t)list->count, sizeof(draw_command_t), draw_command_compare);
}
