#include "core/memstream.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/color.h"
#include "resource/loader.h"

typedef struct {
	float x;
	float y;
	float z;
} obj_position_t;

typedef struct {
	float u;
	float v;
} obj_uv_t;

typedef struct {
	int position;
	int uv;
	uint16_t color;
} obj_corner_t;

typedef struct {
	obj_corner_t corners[3];
} obj_face_t;

static char* obj_skip_spaces(char* text) {
	while (*text == ' ' || *text == '\t') { text++; }
	return text;
}

static int obj_reserve(void** items, int* capacity, int needed, size_t item_size) {
	if (needed <= *capacity) { return 1; }

	int next = *capacity > 0 ? *capacity : 1024;
	while (next < needed) {
		if (next > INT_MAX / 2) { return 0; }
		next *= 2;
	}

	void* grown = realloc(*items, (size_t)next * item_size);
	if (!grown) { return 0; }

	*items = grown;
	*capacity = next;

	return 1;
}

static int obj_parse_face_token(char* token, int* position, int* uv, int position_count, int uv_count) {
	char* first_slash = strchr(token, '/');
	if (first_slash) { *first_slash = '\0'; }

	char* position_token = token;
	char* uv_token = first_slash ? first_slash + 1 : NULL;
	if (uv_token) {
		char* second_slash = strchr(uv_token, '/');
		if (second_slash) { *second_slash = '\0'; }
	}

	if (!position_token || !*position_token) { return 0; }

	int p = atoi(position_token);
	if (p < 0) { p += position_count + 1; }

	else { p -= 1; }

	if (p < 0 || p >= position_count) { return 0; }

	int t = -1;
	if (uv_token && *uv_token) {
		t = atoi(uv_token);
		if (t < 0) { t += uv_count + 1; }

		else { t -= 1; }

		if (t < 0 || t >= uv_count) { return 0; }
	}

	*position = p;
	*uv = t;

	return 1;
}

static int obj_load_stream(FILE* file, mesh_t* mesh) {
	if (!file || !mesh) { return 0; }

	mesh->vertices = NULL;
	mesh->vertex_count = 0;
	mesh->triangles = NULL;
	mesh->triangle_count = 0;

	obj_position_t* positions = NULL;
	obj_uv_t* uvs = NULL;
	obj_face_t* faces = NULL;
	int position_capacity = 0;
	int uv_capacity = 0;
	int face_capacity = 0;
	int position_count = 0;
	int uv_count = 0;
	int face_count = 0;
	uint16_t current_color = rgb555_pack(255, 255, 255);
	char line[1024];

	while (fgets(line, sizeof(line), file)) {
		char* cursor = obj_skip_spaces(line);
		if (*cursor == '\0' || *cursor == '#') { continue; }

		if (strncmp(cursor, "vt", 2) == 0) {
			float u = 0.0f;
			float v = 0.0f;
			if (sscanf(cursor + 2, "%f %f", &u, &v) != 2) { goto fail; }
			if (!obj_reserve((void**)&uvs, &uv_capacity, uv_count + 1, sizeof(obj_uv_t))) { goto fail; }

			uvs[uv_count].u = u;
			uvs[uv_count].v = v;
			uv_count++;
			continue;
		}

		if (cursor[0] == 'v' && (cursor[1] == ' ' || cursor[1] == '\t')) {
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			if (sscanf(cursor + 1, "%f %f %f", &x, &y, &z) != 3) { goto fail; }
			if (!obj_reserve((void**)&positions, &position_capacity, position_count + 1, sizeof(obj_position_t))) { goto fail; }

			positions[position_count].x = x;
			positions[position_count].y = y;
			positions[position_count].z = z;
			position_count++;
			continue;
		}

		if (cursor[0] == 'c' && (cursor[1] == ' ' || cursor[1] == '\t')) {
			int r = 0;
			int g = 0;
			int b = 0;
			if (sscanf(cursor + 1, "%d %d %d", &r, &g, &b) != 3) { goto fail; }

			current_color = rgb555_pack(r, g, b);
			continue;
		}

		if (cursor[0] == 'f' && (cursor[1] == ' ' || cursor[1] == '\t')) {
			char copy[1024];
			strncpy(copy, cursor, sizeof(copy) - 1);
			copy[sizeof(copy) - 1] = '\0';

			obj_corner_t first = { 0, -1, current_color };
			obj_corner_t previous = { 0, -1, current_color };
			int corner_count = 0;
			char* token = strtok(copy, " \t\r\n");

			if (!token || strcmp(token, "f") != 0) { goto fail; }
			token = strtok(NULL, " \t\r\n");

			while (token) {
				obj_corner_t corner;
				corner.color = current_color;

				if (!obj_parse_face_token(token, &corner.position, &corner.uv, position_count, uv_count)) { goto fail; }

				if (corner_count == 0) { first = corner; }

				else if (corner_count >= 2) {
					if (!obj_reserve((void**)&faces, &face_capacity, face_count + 1, sizeof(obj_face_t))) { goto fail; }

					faces[face_count].corners[0] = first;
					faces[face_count].corners[1] = previous;
					faces[face_count].corners[2] = corner;
					face_count++;
				}

				previous = corner;
				corner_count++;
				token = strtok(NULL, " \t\r\n");
			}

			if (corner_count < 3) { goto fail; }
		}
	}

	if (position_count <= 0 || face_count <= 0) { goto fail; }
	if ((size_t)face_count * 3 > INT_MAX / sizeof(vertex_t)) { goto fail; }

	mesh->vertices = (vertex_t*)malloc((size_t)face_count * 3 * sizeof(vertex_t));
	mesh->triangles = (triangle_t*)malloc((size_t)face_count * sizeof(triangle_t));
	if (!mesh->vertices || !mesh->triangles) { goto fail; }

	for (int i = 0; i < face_count; i++) {
		obj_face_t* face = &faces[i];
		vertex_t* face_vertices = &mesh->vertices[i * 3];
		obj_uv_t default_uv = { 0.0f, 0.0f };

		for (int j = 0; j < 3; j++) {
			obj_position_t* position = &positions[face->corners[j].position];
			obj_uv_t* uv = &default_uv;
			if (face->corners[j].uv >= 0) { uv = &uvs[face->corners[j].uv]; }

			face_vertices[j].position.x = position->x;
			face_vertices[j].position.y = position->y;
			face_vertices[j].position.z = position->z;
			face_vertices[j].u = uv->u;
			face_vertices[j].v = uv->v;
			face_vertices[j].color = face->corners[j].color;
		}

		mesh->triangles[i].a = i * 3;
		mesh->triangles[i].b = i * 3 + 1;
		mesh->triangles[i].c = i * 3 + 2;
	}

	mesh_init(mesh, mesh->vertices, face_count * 3, mesh->triangles, face_count);

	free(positions);
	free(uvs);
	free(faces);

	return 1;

fail:
	free(positions);
	free(uvs);
	free(faces);
	mesh_destroy(mesh);
	return 0;
}

int obj_load(const char* path, mesh_t* mesh) {
	if (!path || !mesh) { return 0; }

	FILE* file = fopen(path, "r");
	if (!file) { return 0; }

	int ok = obj_load_stream(file, mesh);
	fclose(file);

	return ok;
}

int obj_load_mem(const void* data, size_t size, mesh_t* mesh) {
	if (!data || !mesh) { return 0; }

	FILE* file = nb_memstream_open(data, size);
	if (!file) { return 0; }

	int ok = obj_load_stream(file, mesh);
	fclose(file);

	return ok;
}
