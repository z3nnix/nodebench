#ifndef NB_RENDER_MESH_H
#define NB_RENDER_MESH_H

#include <stdint.h>

#include "core/math.h"

typedef struct {
	vec3_t position;
	float u;
	float v;
	uint16_t color;
} vertex_t;

typedef struct {
	int a;
	int b;
	int c;
} triangle_t;

typedef struct {
	vertex_t* vertices;
	int vertex_count;
	triangle_t* triangles;
	int triangle_count;
} mesh_t;

void mesh_init(mesh_t* mesh, vertex_t* vertices, int vertex_count, triangle_t* triangles, int triangle_count);
void mesh_create_cube(mesh_t* mesh, vertex_t* vertices, int vertex_capacity, triangle_t* triangles, int triangle_capacity, float size);

#endif
