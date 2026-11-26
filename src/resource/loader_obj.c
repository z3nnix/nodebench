#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/color.h"
#include "resource/loader.h"

#define NB_OBJ_MAX_POSITIONS 4096
#define NB_OBJ_MAX_UVS 4096
#define NB_OBJ_MAX_FACES 4096

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

static int obj_parse_face_token(char* token, int* position, int* uv, int position_count, int uv_count) {
	int p = 0;
	int t = 0;
	if (sscanf(token, "%d/%d", &p, &t) < 1) { return 0; }

	if (p < 0) { p += position_count + 1; }

	else { p -= 1; }

	if (t < 0) { t += uv_count + 1; }

	else { t -= 1; }

	if (p < 0 || p >= position_count || t < 0 || t >= uv_count) { return 0; }

	*position = p;
	*uv = t;

	return 1;
}

int obj_load(const char* path, mesh_t* mesh, vertex_t* vertices, int vertex_capacity, triangle_t* triangles, int triangle_capacity) {
	if (!path || !mesh || !vertices || !triangles) { return 0; }
	if (vertex_capacity < 3 || triangle_capacity < 1) { return 0; }

	FILE* file = fopen(path, "r");
	if (!file) { return 0; }

	obj_position_t* positions = (obj_position_t*)malloc(NB_OBJ_MAX_POSITIONS * sizeof(obj_position_t));
	obj_uv_t* uvs = (obj_uv_t*)malloc(NB_OBJ_MAX_UVS * sizeof(obj_uv_t));
	obj_face_t* faces = (obj_face_t*)malloc(NB_OBJ_MAX_FACES * sizeof(obj_face_t));
	if (!positions || !uvs || !faces) {
		free(positions);
		free(uvs);
		free(faces);
		fclose(file);
		return 0;
	}

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
			if (uv_count >= NB_OBJ_MAX_UVS) { goto fail; }

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
			if (position_count >= NB_OBJ_MAX_POSITIONS) { goto fail; }

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

			int face_positions[8];
			int face_uvs[8];
			int corner_count = 0;
			char* token = strtok(copy, " \t\r\n");

			if (!token || strcmp(token, "f") != 0) { goto fail; }
			token = strtok(NULL, " \t\r\n");

			while (token) {
				if (corner_count >= 8) { goto fail; }

				int position = 0;
				int uv = 0;
				if (!obj_parse_face_token(token, &position, &uv, position_count, uv_count)) { goto fail; }

				face_positions[corner_count] = position;
				face_uvs[corner_count] = uv;
				corner_count++;
				token = strtok(NULL, " \t\r\n");
			}

			if (corner_count < 3 || corner_count > 4) { goto fail; }
			if (face_count + corner_count - 2 > NB_OBJ_MAX_FACES) { goto fail; }

			for (int i = 1; i < corner_count - 1; i++) {
				obj_face_t* face = &faces[face_count];

				face->corners[0].position = face_positions[0];
				face->corners[0].uv = face_uvs[0];
				face->corners[0].color = current_color;
				face->corners[1].position = face_positions[i];
				face->corners[1].uv = face_uvs[i];
				face->corners[1].color = current_color;
				face->corners[2].position = face_positions[i + 1];
				face->corners[2].uv = face_uvs[i + 1];
				face->corners[2].color = current_color;
				face_count++;
			}
		}
	}

	if (position_count == 0 || uv_count == 0 || face_count == 0) { goto fail; }
	if (face_count > triangle_capacity || face_count * 3 > vertex_capacity) { goto fail; }

	for (int i = 0; i < face_count; i++) {
		obj_face_t* face = &faces[i];
		vertex_t* face_vertices = &vertices[i * 3];

		for (int j = 0; j < 3; j++) {
			obj_position_t* position = &positions[face->corners[j].position];
			obj_uv_t* uv = &uvs[face->corners[j].uv];

			face_vertices[j].position.x = position->x;
			face_vertices[j].position.y = position->y;
			face_vertices[j].position.z = position->z;
			face_vertices[j].u = uv->u;
			face_vertices[j].v = uv->v;
			face_vertices[j].color = face->corners[j].color;
		}

		triangles[i].a = i * 3;
		triangles[i].b = i * 3 + 1;
		triangles[i].c = i * 3 + 2;
	}

	mesh_init(mesh, vertices, face_count * 3, triangles, face_count);

	free(positions);
	free(uvs);
	free(faces);
	fclose(file);

	return 1;

fail:
	free(positions);
	free(uvs);
	free(faces);
	fclose(file);
	return 0;
}
