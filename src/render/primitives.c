#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "render/color.h"
#include "render/mesh.h"
#include "render/primitives.h"

static const char* const primitive_names[NB_MESH_PRIMITIVE_MAX] = {
	"cube",
	"square",
	"plane",
	"tetrahedron",
	"sphere"
};

static const char* const primitive_labels[NB_MESH_PRIMITIVE_MAX] = {
	"Cube",
	"Square",
	"Plane",
	"Tetrahedron",
	"Sphere"
};

const char* mesh_primitive_name(int index) {
	if (index < 0 || index >= NB_MESH_PRIMITIVE_MAX) { return NULL; }
	return primitive_names[index];
}

const char* mesh_primitive_label(int index) {
	if (index < 0 || index >= NB_MESH_PRIMITIVE_MAX) { return NULL; }
	return primitive_labels[index];
}

int mesh_primitive_count(void) {
	return NB_MESH_PRIMITIVE_MAX;
}

static int primitive_alloc(mesh_t* mesh, int vertex_count, int triangle_count) {
	if (!mesh) { return 0; }
	vertex_t* vertices = (vertex_t*)malloc((size_t)vertex_count * sizeof(*vertices));
	triangle_t* triangles = (triangle_t*)malloc((size_t)triangle_count * sizeof(*triangles));
	if (!vertices || !triangles) {
		free(vertices);
		free(triangles);
		return 0;
	}
	mesh_init(mesh, vertices, vertex_count, triangles, triangle_count);
	return 1;
}

static void primitive_set_vertex(vertex_t* vertex, vec3_t position, float u, float v, uint16_t color) {
	vertex->position = position;
	vertex->u = u;
	vertex->v = v;
	vertex->color = color;
}

static int primitive_make_quad(mesh_t* mesh, const vec3_t corners[4], uint16_t color) {
	if (!primitive_alloc(mesh, 4, 2)) { return 0; }
	primitive_set_vertex(&mesh->vertices[0], corners[0], 0.0f, 0.0f, color);
	primitive_set_vertex(&mesh->vertices[1], corners[1], 1.0f, 0.0f, color);
	primitive_set_vertex(&mesh->vertices[2], corners[2], 1.0f, 1.0f, color);
	primitive_set_vertex(&mesh->vertices[3], corners[3], 0.0f, 1.0f, color);
	mesh->triangles[0].a = 0;
	mesh->triangles[0].b = 1;
	mesh->triangles[0].c = 2;
	mesh->triangles[1].a = 0;
	mesh->triangles[1].b = 2;
	mesh->triangles[1].c = 3;
	return 1;
}

static int primitive_make_tetrahedron(mesh_t* mesh, uint16_t color) {
	if (!primitive_alloc(mesh, 12, 4)) { return 0; }
	vec3_t corners[4] = {
		{ 0.0f, 0.5f, 0.0f },
		{ -0.5f, -0.25f, 0.5f },
		{ 0.5f, -0.25f, 0.5f },
		{ 0.0f, -0.25f, -0.5f }
	};
	int faces[4][3] = {
		{ 0, 1, 2 },
		{ 0, 2, 3 },
		{ 0, 3, 1 },
		{ 1, 3, 2 }
	};
	uint16_t face_colors[4] = {
		color,
		rgb555_pack(196, 212, 232),
		rgb555_pack(240, 224, 200),
		rgb555_pack(180, 196, 220)
	};
	float us[3] = { 0.5f, 0.0f, 1.0f };
	float vs[3] = { 1.0f, 0.0f, 0.0f };
	for (int face = 0; face < 4; face++) {
		int base = face * 3;
		for (int j = 0; j < 3; j++) {
			primitive_set_vertex(&mesh->vertices[base + j], corners[faces[face][j]], us[j], vs[j], face_colors[face]);
		}
		mesh->triangles[face] = (triangle_t){ base, base + 1, base + 2 };
	}
	return 1;
}

static int primitive_make_sphere(mesh_t* mesh, uint16_t color) {
	int lat = 8;
	int lon = 12;
	int vertex_count = 2 + (lat - 1) * lon;
	int triangle_count = lon * 2 + (lat - 2) * lon * 2;
	if (!primitive_alloc(mesh, vertex_count, triangle_count)) { return 0; }
	primitive_set_vertex(&mesh->vertices[0], (vec3_t){ 0.0f, 0.5f, 0.0f }, 0.5f, 1.0f, color);
	for (int ring = 1; ring < lat; ring++) {
		float phi = 3.14159265f * (float)ring / (float)lat;
		float y = 0.5f * cosf(phi);
		float radial = 0.5f * sinf(phi);
		int base = 1 + (ring - 1) * lon;
		for (int i = 0; i < lon; i++) {
			float theta = 6.28318530718f * (float)i / (float)lon;
			float x = radial * cosf(theta);
			float z = radial * sinf(theta);
			primitive_set_vertex(&mesh->vertices[base + i], (vec3_t){ x, y, z }, (float)i / (float)lon, (float)ring / (float)lat, color);
		}
	}
	int bottom = 1 + (lat - 1) * lon;
	primitive_set_vertex(&mesh->vertices[bottom], (vec3_t){ 0.0f, -0.5f, 0.0f }, 0.5f, 0.0f, color);
	int t = 0;
	int first_ring = 1;
	for (int i = 0; i < lon; i++) {
		int next = (i + 1) % lon;
		mesh->triangles[t++] = (triangle_t){ 0, first_ring + next, first_ring + i };
	}
	int last_ring = 1 + (lat - 2) * lon;
	for (int i = 0; i < lon; i++) {
		int next = (i + 1) % lon;
		mesh->triangles[t++] = (triangle_t){ last_ring + i, last_ring + next, bottom };
	}
	for (int ring = 1; ring < lat - 1; ring++) {
		int upper = 1 + (ring - 1) * lon;
		int lower = upper + lon;
		for (int i = 0; i < lon; i++) {
			int next = (i + 1) % lon;
			mesh->triangles[t++] = (triangle_t){ upper + i, upper + next, lower + i };
			mesh->triangles[t++] = (triangle_t){ upper + next, lower + next, lower + i };
		}
	}
	return 1;
}

int mesh_create_primitive(const char* name, mesh_t* mesh) {
	if (!name || !mesh) { return 0; }
	mesh->vertices = NULL;
	mesh->vertex_count = 0;
	mesh->triangles = NULL;
	mesh->triangle_count = 0;
	uint16_t color = rgb555_pack(220, 232, 244);
	if (strcmp(name, "cube") == 0) {
		if (!primitive_alloc(mesh, 24, 12)) { return 0; }
		mesh_create_cube(mesh, mesh->vertices, 24, mesh->triangles, 12, 0.5f);
		return 1;
	}
	if (strcmp(name, "square") == 0) {
		vec3_t corners[4] = {
			{ -0.5f, -0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f }
		};
		return primitive_make_quad(mesh, corners, color);
	}
	if (strcmp(name, "plane") == 0) {
		vec3_t corners[4] = {
			{ -0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, -0.5f }, { -0.5f, 0.0f, -0.5f }
		};
		return primitive_make_quad(mesh, corners, color);
	}
	if (strcmp(name, "tetrahedron") == 0) { return primitive_make_tetrahedron(mesh, color); }
	if (strcmp(name, "sphere") == 0) { return primitive_make_sphere(mesh, color); }
	return 0;
}
