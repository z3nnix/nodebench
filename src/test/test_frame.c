#include <stdio.h>
#include <stdlib.h>

#include "core/config.h"
#include "core/math.h"
#include "render/color.h"
#include "render/draw_list.h"
#include "render/framebuffer.h"
#include "render/material.h"
#include "render/mesh.h"
#include "render/renderer.h"

static int write_ppm(const framebuffer_t* framebuffer, const uint32_t* output, const char* path) {
	if (!framebuffer || !output || !path) { return 0; }

	FILE* file = fopen(path, "wb");
	if (!file) { return 0; }

	fprintf(file, "P6\n%d %d\n255\n", framebuffer->width, framebuffer->height);

	for (int i = 0; i < framebuffer->width * framebuffer->height; i++) {
		int r = (int)((output[i] >> 24) & 0xffu);
		int g = (int)((output[i] >> 16) & 0xffu);
		int b = (int)((output[i] >> 8) & 0xffu);
		fputc(r, file);
		fputc(g, file);
		fputc(b, file);
	}

	fclose(file);
	return 1;
}

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: test_frame OUTPUT.ppm\n");
		return 2;
	}

	render_config_t config;
	render_config_init(&config);
	config.width = 160;
	config.height = 120;
	config.snap_grid_width = 160;
	config.snap_grid_height = 120;
	config.fog_enabled = 0;
	config.cull_backfaces = 0;
	config.reject_near_triangles = 0;
	config.affine_strength = 1.0f;
	config.blend_mode = NB_BLEND_NONE;
	config.shade_mode = NB_SHADE_GOURAUD;

	vertex_t vertices[4];
	vertices[0] = (vertex_t){ (vec3_t){ -1.0f, -1.0f, 0.0f }, 0.0f, 0.0f, rgb555_pack(255, 80, 80) };
	vertices[1] = (vertex_t){ (vec3_t){ 1.0f, -1.0f, 0.0f }, 1.0f, 0.0f, rgb555_pack(80, 255, 80) };
	vertices[2] = (vertex_t){ (vec3_t){ 1.0f, 1.0f, 0.0f }, 1.0f, 1.0f, rgb555_pack(80, 80, 255) };
	vertices[3] = (vertex_t){ (vec3_t){ -1.0f, 1.0f, 0.0f }, 0.0f, 1.0f, rgb555_pack(255, 255, 80) };

	triangle_t triangles[2];
	triangles[0] = (triangle_t){ 0, 1, 2 };
	triangles[1] = (triangle_t){ 0, 2, 3 };

	mesh_t mesh;
	mesh_init(&mesh, vertices, 4, triangles, 2);

	material_t material;
	material_init(&material, 0, NB_BLEND_NONE, NB_SHADE_GOURAUD, 1, (vec3_t){ 1.0f, 1.0f, 1.0f });

	framebuffer_t framebuffer;
	if (!framebuffer_init(&framebuffer, &config)) { return 1; }

	renderer_t renderer;
	if (!renderer_init(&renderer, &framebuffer)) {
		framebuffer_shutdown(&framebuffer);
		return 1;
	}

	draw_list_t list;
	draw_list_init(&list);

	mat4_t projection;
	mat4_t view;
	mat4_t model;
	mat4_perspective(&projection, 60.0f * 3.14159265f / 180.0f, (float)config.width / (float)config.height, config.near_plane, config.far_plane);
	mat4_look_at(&view, (vec3_t){ 0.0f, 0.0f, 5.0f }, (vec3_t){ 0.0f, 0.0f, 0.0f }, (vec3_t){ 0.0f, 1.0f, 0.0f });
	mat4_rotate_y(&model, 0.35f);

	framebuffer_clear_gradient(&framebuffer, rgb555_pack(6, 10, 22), rgb555_pack(18, 26, 44));
	draw_list_set_camera(&list, &view, &projection);
	draw_list_push(&list, &mesh, NULL, &material, &model);
	draw_list_sort(&list);
	renderer_submit(&renderer, &list);
	renderer_flush(&renderer);

	uint32_t* output = (uint32_t*)malloc((size_t)config.width * (size_t)config.height * sizeof(uint32_t));
	if (!output) {
		renderer_shutdown(&renderer);
		framebuffer_shutdown(&framebuffer);
		return 1;
	}

	framebuffer_resolve(&framebuffer, output);
	int ok = write_ppm(&framebuffer, output, argv[1]);

	free(output);
	renderer_shutdown(&renderer);
	framebuffer_shutdown(&framebuffer);

	return ok ? 0 : 1;
}
