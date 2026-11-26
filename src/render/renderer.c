#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "render/renderer.h"

static int renderer_reserve(renderer_t* renderer, int vertex_count, int triangle_count) {
	if (!renderer) { return 0; }
	if (vertex_count <= renderer->projected_capacity && triangle_count <= renderer->triangle_capacity) { return 1; }

	int projected_capacity = renderer->projected_capacity;
	int triangle_capacity = renderer->triangle_capacity;

	if (vertex_count > projected_capacity) {
		projected_capacity = projected_capacity > 0 ? projected_capacity : 64;
		while (projected_capacity < vertex_count) { projected_capacity *= 2; }
	}

	if (triangle_count > triangle_capacity) {
		triangle_capacity = triangle_capacity > 0 ? triangle_capacity : 64;
		while (triangle_capacity < triangle_count) { triangle_capacity *= 2; }
	}

	projected_vertex_t* projected = renderer->projected;
	vec3_t* world = renderer->world;
	projected_triangle_t* triangles = renderer->triangles;

	if (projected_capacity != renderer->projected_capacity) {
		projected = (projected_vertex_t*)realloc(projected, (size_t)projected_capacity * sizeof(projected_vertex_t));
		if (!projected) { return 0; }

		world = (vec3_t*)realloc(world, (size_t)projected_capacity * sizeof(vec3_t));
		if (!world) {
			if (projected != renderer->projected) { free(projected); }
			return 0;
		}
	}

	if (triangle_capacity != renderer->triangle_capacity) {
		triangles = (projected_triangle_t*)realloc(triangles, (size_t)triangle_capacity * sizeof(projected_triangle_t));
		if (!triangles) {
			if (projected != renderer->projected) { free(projected); }
			if (world != renderer->world) { free(world); }
			return 0;
		}
	}

	renderer->projected = projected;
	renderer->world = world;
	renderer->triangles = triangles;
	renderer->projected_capacity = projected_capacity;
	renderer->triangle_capacity = triangle_capacity;

	return 1;
}

static float clamp01(float value) {
	if (value < 0.0f) { return 0.0f; }
	if (value > 1.0f) { return 1.0f; }
	return value;
}

static float edge2(float ax, float ay, float bx, float by, float px, float py) {
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static float min3f(float a, float b, float c) {
	float result = a;
	if (b < result) { result = b; }
	if (c < result) { result = c; }
	return result;
}

static float max3f(float a, float b, float c) {
	float result = a;
	if (b > result) { result = b; }
	if (c > result) { result = c; }
	return result;
}

static int triangle_compare(const void* a, const void* b) {
	const projected_triangle_t* left = (const projected_triangle_t*)a;
	const projected_triangle_t* right = (const projected_triangle_t*)b;

	if (left->depth > right->depth) { return -1; }
	if (left->depth < right->depth) { return 1; }
	return 0;
}

static void rasterize_triangle(renderer_t* renderer, const projected_triangle_t* triangle, const texture_t* texture, nb_blend_mode_t blend_mode) {
	const render_config_t* config = renderer->config;
	framebuffer_t* framebuffer = renderer->framebuffer;
	const projected_vertex_t* v0 = &triangle->v[0];
	const projected_vertex_t* v1 = &triangle->v[1];
	const projected_vertex_t* v2 = &triangle->v[2];

	float area = edge2(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y);
	if (fabsf(area) < 0.0001f) { return; }

	float inverse_area = 1.0f / area;
	float strength = clamp01(config->affine_strength);

	int min_x = (int)floorf(min3f(v0->x, v1->x, v2->x));
	int max_x = (int)ceilf(max3f(v0->x, v1->x, v2->x));
	int min_y = (int)floorf(min3f(v0->y, v1->y, v2->y));
	int max_y = (int)ceilf(max3f(v0->y, v1->y, v2->y));

	if (min_x < 0) { min_x = 0; }
	if (min_y < 0) { min_y = 0; }
	if (max_x >= framebuffer->width) { max_x = framebuffer->width - 1; }
	if (max_y >= framebuffer->height) { max_y = framebuffer->height - 1; }

	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			float px = (float)x + 0.5f;
			float py = (float)y + 0.5f;

			float w0 = edge2(v1->x, v1->y, v2->x, v2->y, px, py) * inverse_area;
			float w1 = edge2(v2->x, v2->y, v0->x, v0->y, px, py) * inverse_area;
			float w2 = edge2(v0->x, v0->y, v1->x, v1->y, px, py) * inverse_area;

			if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) { continue; }

			float u_affine = w0 * v0->u + w1 * v1->u + w2 * v2->u;
			float v_affine = w0 * v0->v + w1 * v1->v + w2 * v2->v;
			float u = u_affine;
			float v = v_affine;

			if (strength < 1.0f) {
				float inverse_w = w0 * v0->inv_w + w1 * v1->inv_w + w2 * v2->inv_w;

				if (inverse_w > 0.000001f) {
					float u_perspective = (w0 * v0->u * v0->inv_w + w1 * v1->u * v1->inv_w + w2 * v2->u * v2->inv_w) / inverse_w;
					float v_perspective = (w0 * v0->v * v0->inv_w + w1 * v1->v * v1->inv_w + w2 * v2->v * v2->inv_w) / inverse_w;

					u = u_perspective + (u_affine - u_perspective) * strength;
					v = v_perspective + (v_affine - v_perspective) * strength;
				}
			}

			float fog = clamp01(w0 * v0->fog + w1 * v1->fog + w2 * v2->fog);
			float r = w0 * v0->r + w1 * v1->r + w2 * v2->r;
			float g = w0 * v0->g + w1 * v1->g + w2 * v2->g;
			float b = w0 * v0->b + w1 * v1->b + w2 * v2->b;

			r = r * (1.0f - fog) + (float)config->fog_color_rgb[0] * fog;
			g = g * (1.0f - fog) + (float)config->fog_color_rgb[1] * fog;
			b = b * (1.0f - fog) + (float)config->fog_color_rgb[2] * fog;

			if (texture) {
				uint16_t texel = texture_sample(texture, u, v);
				if (texel == NB_TEXTURE_TRANSPARENT) { continue; }

				int tr = 0;
				int tg = 0;
				int tb = 0;

				rgb555_unpack(texel, &tr, &tg, &tb);
				r = r * (float)tr / 255.0f;
				g = g * (float)tg / 255.0f;
				b = b * (float)tb / 255.0f;
			}

			framebuffer_write(framebuffer, x, y, r, g, b, blend_mode);
		}
	}
}

int renderer_init(renderer_t* renderer, framebuffer_t* framebuffer) {
	if (!renderer || !framebuffer || !framebuffer->config) { return 0; }

	renderer->framebuffer = framebuffer;
	renderer->config = framebuffer->config;
	renderer->projected = NULL;
	renderer->world = NULL;
	renderer->triangles = NULL;
	renderer->projected_capacity = 0;
	renderer->triangle_capacity = 0;
	renderer->command_count = 0;

	mat4_identity(&renderer->view);
	mat4_identity(&renderer->view_projection);

	if (!renderer_reserve(renderer, 64, 128)) { return 0; }

	return 1;
}

void renderer_shutdown(renderer_t* renderer) {
	if (!renderer) { return; }

	free(renderer->projected);
	free(renderer->world);
	free(renderer->triangles);

	renderer->projected = NULL;
	renderer->world = NULL;
	renderer->triangles = NULL;
	renderer->projected_capacity = 0;
	renderer->triangle_capacity = 0;
	renderer->command_count = 0;
}

void renderer_submit(renderer_t* renderer, const draw_list_t* list) {
	if (!renderer || !list) { return; }

	renderer->view = list->view;
	renderer->view_projection = list->view_projection;
	renderer->command_count = list->count;

	if (renderer->command_count > NB_DRAW_LIST_MAX_COMMANDS) {
		renderer->command_count = NB_DRAW_LIST_MAX_COMMANDS;
	}

	memcpy(renderer->commands, list->commands, (size_t)renderer->command_count * sizeof(draw_command_t));
}

static void renderer_draw_command(renderer_t* renderer, const draw_command_t* command) {
	if (!renderer || !renderer->framebuffer || !command) { return; }

	const mesh_t* mesh = command->mesh;
	const material_t* material = command->material;
	if (!mesh || !mesh->vertices || !mesh->triangles || !material) { return; }
	if (!renderer_reserve(renderer, mesh->vertex_count, mesh->triangle_count)) { return; }

	const render_config_t* config = renderer->config;
	int width = renderer->framebuffer->width;
	int height = renderer->framebuffer->height;
	float snap_step_x = config->snap_grid_width > 0 ? (float)width / (float)config->snap_grid_width : 1.0f;
	float snap_step_y = config->snap_grid_height > 0 ? (float)height / (float)config->snap_grid_height : 1.0f;

	mat4_t local_model = command->model;

	vec3_t light = vec3_normalize((vec3_t){ config->light_direction[0], config->light_direction[1], config->light_direction[2] });

	for (int i = 0; i < mesh->vertex_count; i++) {
		const vertex_t* vertex = &mesh->vertices[i];
		vec4_t world4 = mat4_transform(&local_model, (vec4_t){ vertex->position.x, vertex->position.y, vertex->position.z, 1.0f });
		vec4_t view4 = mat4_transform(&renderer->view, world4);
		vec4_t clip = mat4_transform(&renderer->view_projection, world4);

		projected_vertex_t* projected = &renderer->projected[i];
		projected->clip = clip;
		projected->x = 0.0f;
		projected->y = 0.0f;
		projected->inv_w = 0.0f;
		projected->u = vertex->u;
		projected->v = vertex->v;

		int color_r = 0;
		int color_g = 0;
		int color_b = 0;

		rgb555_unpack(vertex->color, &color_r, &color_g, &color_b);
		projected->r = (float)color_r * material->vertex_color_tint.x;
		projected->g = (float)color_g * material->vertex_color_tint.y;
		projected->b = (float)color_b * material->vertex_color_tint.z;

		float distance = -view4.z;
		float fog = 0.0f;

		if (config->fog_enabled && material->fog_enabled && config->fog_far != config->fog_near) { fog = (distance - config->fog_near) / (config->fog_far - config->fog_near); }
		projected->fog = clamp01(fog);

		renderer->world[i] = (vec3_t){ world4.x, world4.y, world4.z };

		if (clip.w <= config->near_plane) { continue; }

		projected->inv_w = 1.0f / clip.w;

		float ndc_x = clip.x * projected->inv_w;
		float ndc_y = clip.y * projected->inv_w;
		float screen_x = (ndc_x * 0.5f + 0.5f) * (float)width;
		float screen_y = (0.5f - ndc_y * 0.5f) * (float)height;

		projected->x = floorf(screen_x / snap_step_x + 0.5f) * snap_step_x;
		projected->y = floorf(screen_y / snap_step_y + 0.5f) * snap_step_y;
	}

	int triangle_count = 0;

	for (int i = 0; i < mesh->triangle_count; i++) {
		const triangle_t* source = &mesh->triangles[i];

		if (source->a < 0 || source->b < 0 || source->c < 0) { continue; }
		if (source->a >= mesh->vertex_count || source->b >= mesh->vertex_count || source->c >= mesh->vertex_count) { continue; }

		const projected_vertex_t* base_a = &renderer->projected[source->a];
		const projected_vertex_t* base_b = &renderer->projected[source->b];
		const projected_vertex_t* base_c = &renderer->projected[source->c];

		if (config->reject_near_triangles) {
			if (base_a->clip.w <= config->near_plane || base_b->clip.w <= config->near_plane || base_c->clip.w <= config->near_plane) { continue; }
		}

		projected_vertex_t v0 = *base_a;
		projected_vertex_t v1 = *base_b;
		projected_vertex_t v2 = *base_c;

		float area = edge2(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
		if (fabsf(area) < 0.0001f) { continue; }
		if (config->cull_backfaces && area > 0.0f) { continue; }

		vec3_t edge_a = vec3_sub(renderer->world[source->b], renderer->world[source->a]);
		vec3_t edge_b = vec3_sub(renderer->world[source->c], renderer->world[source->a]);
		vec3_t normal = vec3_normalize(vec3_cross(edge_a, edge_b));
		float facing = vec3_dot(normal, light);
		if (facing < 0.0f) { facing = 0.0f; }
		float intensity = 0.35f + 0.65f * facing;

		if (material->shade_mode == NB_SHADE_FLAT) {
			v1.r = v0.r;
			v1.g = v0.g;
			v1.b = v0.b;
			v2.r = v0.r;
			v2.g = v0.g;
			v2.b = v0.b;
		}

		v0.r *= intensity;
		v0.g *= intensity;
		v0.b *= intensity;
		v1.r *= intensity;
		v1.g *= intensity;
		v1.b *= intensity;
		v2.r *= intensity;
		v2.g *= intensity;
		v2.b *= intensity;

		projected_triangle_t* target = &renderer->triangles[triangle_count];
		target->v[0] = v0;
		target->v[1] = v1;
		target->v[2] = v2;
		target->depth = (v0.clip.w + v1.clip.w + v2.clip.w) / 3.0f;
		triangle_count++;
	}

	if (triangle_count > 1) { qsort(renderer->triangles, (size_t)triangle_count, sizeof(projected_triangle_t), triangle_compare); }

	for (int i = 0; i < triangle_count; i++) { rasterize_triangle(renderer, &renderer->triangles[i], command->texture, material->blend_mode); }
}

void renderer_flush(renderer_t* renderer) {
	if (!renderer) {
		return;
	}

	for (int i = 0; i < renderer->command_count; i++) {
		renderer_draw_command(renderer, &renderer->commands[i]);
	}
}
