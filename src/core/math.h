#ifndef NB_CORE_MATH_H
#define NB_CORE_MATH_H

typedef struct {
	float x;
	float y;
	float z;
} vec3_t;

typedef struct {
	float x;
	float y;
	float z;
	float w;
} vec4_t;

typedef struct {
	float m[16];
} mat4_t;

void mat4_identity(mat4_t* out);
void mat4_multiply(mat4_t* out, const mat4_t* a, const mat4_t* b);
void mat4_perspective(mat4_t* out, float fov_y, float aspect, float near, float far);
void mat4_look_at(mat4_t* out, vec3_t eye, vec3_t center, vec3_t up);
void mat4_translate(mat4_t* out, float x, float y, float z);
void mat4_rotate_x(mat4_t* out, float angle);
void mat4_rotate_y(mat4_t* out, float angle);
vec4_t mat4_transform(const mat4_t* m, vec4_t v);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t v, float s);
float vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_cross(vec3_t a, vec3_t b);
vec3_t vec3_normalize(vec3_t v);

#endif
