#include <math.h>

#include "core/math.h"

void mat4_identity(mat4_t* out) {
	if (!out) { return; }

	for (int i = 0; i < 16; i++) { out->m[i] = 0.0f; }
	out->m[0] = 1.0f;
	out->m[5] = 1.0f;
	out->m[10] = 1.0f;
	out->m[15] = 1.0f;
}

void mat4_multiply(mat4_t* out, const mat4_t* a, const mat4_t* b) {
	if (!out || !a || !b) { return; }

	mat4_t result;

	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			float sum = 0.0f;

			for (int k = 0; k < 4; k++) { sum += a->m[k * 4 + row] * b->m[col * 4 + k]; }
			result.m[col * 4 + row] = sum;
		}
	}

	*out = result;
}

void mat4_perspective(mat4_t* out, float fov_y, float aspect, float near, float far) {
	if (!out || aspect == 0.0f || near == far) { return; }

	float f = 1.0f / tanf(fov_y * 0.5f);
	float nf = 1.0f / (near - far);

	mat4_identity(out);
	out->m[0] = f / aspect;
	out->m[5] = f;
	out->m[10] = (far + near) * nf;
	out->m[11] = -1.0f;
	out->m[14] = 2.0f * far * near * nf;
	out->m[15] = 0.0f;
}

void mat4_look_at(mat4_t* out, vec3_t eye, vec3_t center, vec3_t up) {
	if (!out) { return; }

	vec3_t forward = vec3_normalize(vec3_sub(center, eye));
	vec3_t right = vec3_normalize(vec3_cross(forward, up));
	vec3_t real_up = vec3_cross(right, forward);

	mat4_identity(out);
	out->m[0] = right.x;
	out->m[4] = right.y;
	out->m[8] = right.z;
	out->m[12] = -vec3_dot(right, eye);
	out->m[1] = real_up.x;
	out->m[5] = real_up.y;
	out->m[9] = real_up.z;
	out->m[13] = -vec3_dot(real_up, eye);
	out->m[2] = -forward.x;
	out->m[6] = -forward.y;
	out->m[10] = -forward.z;
	out->m[14] = vec3_dot(forward, eye);
}

void mat4_translate(mat4_t* out, float x, float y, float z) {
	if (!out) { return; }

	mat4_identity(out);
	out->m[12] = x;
	out->m[13] = y;
	out->m[14] = z;
}

void mat4_rotate_x(mat4_t* out, float angle) {
	if (!out) { return; }

	float c = cosf(angle);
	float s = sinf(angle);

	mat4_identity(out);
	out->m[5] = c;
	out->m[6] = s;
	out->m[9] = -s;
	out->m[10] = c;
}

void mat4_rotate_y(mat4_t* out, float angle) {
	if (!out) { return; }

	float c = cosf(angle);
	float s = sinf(angle);

	mat4_identity(out);
	out->m[0] = c;
	out->m[2] = -s;
	out->m[8] = s;
	out->m[10] = c;
}

vec4_t mat4_transform(const mat4_t* m, vec4_t v) {
	vec4_t result = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (!m) { return result; }

	result.x = m->m[0] * v.x + m->m[4] * v.y + m->m[8] * v.z + m->m[12] * v.w;
	result.y = m->m[1] * v.x + m->m[5] * v.y + m->m[9] * v.z + m->m[13] * v.w;
	result.z = m->m[2] * v.x + m->m[6] * v.y + m->m[10] * v.z + m->m[14] * v.w;
	result.w = m->m[3] * v.x + m->m[7] * v.y + m->m[11] * v.z + m->m[15] * v.w;

	return result;
}

vec3_t vec3_add(vec3_t a, vec3_t b) {
	vec3_t result = { a.x + b.x, a.y + b.y, a.z + b.z };
	return result;
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
	vec3_t result = { a.x - b.x, a.y - b.y, a.z - b.z };
	return result;
}

vec3_t vec3_scale(vec3_t v, float s) {
	vec3_t result = { v.x * s, v.y * s, v.z * s };
	return result;
}

float vec3_dot(vec3_t a, vec3_t b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
	vec3_t result = {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};

	return result;
}

vec3_t vec3_normalize(vec3_t v) {
	float length = sqrtf(vec3_dot(v, v));

	if (length < 0.000001f) { return (vec3_t){ 0.0f, 0.0f, 0.0f }; }

	return vec3_scale(v, 1.0f / length);
}
