#include "render/color.h"
#include "render/mesh.h"

void mesh_init(mesh_t* mesh, vertex_t* vertices, int vertex_count, triangle_t* triangles, int triangle_count) {
	if (!mesh) { return; }

	mesh->vertices = vertices;
	mesh->vertex_count = vertex_count;
	mesh->triangles = triangles;
	mesh->triangle_count = triangle_count;
}

static void set_face(vertex_t* vertices, int base, const vec3_t corners[4], uint16_t face_color) {
	int r = 0;
	int g = 0;
	int b = 0;

	rgb555_unpack(face_color, &r, &g, &b);

	for (int i = 0; i < 4; i++) {
		float shade = (i < 2) ? 0.62f : 1.0f;

		vertices[base + i].position = corners[i];
		vertices[base + i].u = (i == 1 || i == 2) ? 1.0f : 0.0f;
		vertices[base + i].v = (i >= 2) ? 1.0f : 0.0f;
		vertices[base + i].color = rgb555_pack((int)((float)r * shade), (int)((float)g * shade), (int)((float)b * shade));
	}
}

void mesh_create_cube(mesh_t* mesh, vertex_t* vertices, int vertex_capacity, triangle_t* triangles, int triangle_capacity, float size) {
	if (!mesh || !vertices || !triangles) { return; }
	if (vertex_capacity < 24 || triangle_capacity < 12) { return; }

	float s = size;

	vec3_t front[4] = {
		{ -s, -s, s }, { s, -s, s }, { s, s, s }, { -s, s, s }
	};
	vec3_t back[4] = {
		{ s, -s, -s }, { -s, -s, -s }, { -s, s, -s }, { s, s, -s }
	};
	vec3_t right[4] = {
		{ s, -s, s }, { s, -s, -s }, { s, s, -s }, { s, s, s }
	};
	vec3_t left[4] = {
		{ -s, -s, -s }, { -s, -s, s }, { -s, s, s }, { -s, s, -s }
	};
	vec3_t top[4] = {
		{ -s, s, s }, { s, s, s }, { s, s, -s }, { -s, s, -s }
	};
	vec3_t bottom[4] = {
		{ -s, -s, -s }, { s, -s, -s }, { s, -s, s }, { -s, -s, s }
	};

	uint16_t face_colors[6] = {
		rgb555_pack(224, 232, 255),
		rgb555_pack(168, 184, 220),
		rgb555_pack(255, 208, 160),
		rgb555_pack(164, 208, 236),
		rgb555_pack(255, 244, 200),
		rgb555_pack(140, 152, 184)
	};

	const vec3_t* faces[6] = { front, back, right, left, top, bottom };

	for (int face = 0; face < 6; face++) {
		int base = face * 4;

		set_face(vertices, base, faces[face], face_colors[face]);

		triangles[face * 2].a = base;
		triangles[face * 2].b = base + 1;
		triangles[face * 2].c = base + 2;
		triangles[face * 2 + 1].a = base;
		triangles[face * 2 + 1].b = base + 2;
		triangles[face * 2 + 1].c = base + 3;
	}

	mesh_init(mesh, vertices, 24, triangles, 12);
}
