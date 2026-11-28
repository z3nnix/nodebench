#define _POSIX_C_SOURCE 200809L
#include "scene/scene_doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset/asset_db.h"
#include "core/dynamic_array.h"
#include "core/engine.h"
#include "scene/tscn.h"

void scene_doc_init(scene_doc_t* doc) {
	if (!doc) {
		return;
	}

	memset(doc, 0, sizeof(*doc));
}

void scene_doc_clear(scene_doc_t* doc) {
	if (!doc) {
		return;
	}

	nb_array_free(doc->ext);
	memset(doc, 0, sizeof(*doc));
}

const scene_doc_ext_t* scene_doc_find_ext(const scene_doc_t* doc, nb_resource_kind_t kind, uint32_t handle) {
	if (!doc || kind == NB_RESOURCE_KIND_NONE || handle == NB_RESOURCE_INVALID_ID) {
		return NULL;
	}

	for (int i = 0; i < doc->ext_count; i++) {
		if (doc->ext[i].kind == kind && doc->ext[i].handle == handle) {
			return &doc->ext[i];
		}
	}

	return NULL;
}

const scene_doc_ext_t* scene_doc_find_ext_by_id(const scene_doc_t* doc, const char* id) {
	if (!doc || !id || !id[0]) {
		return NULL;
	}

	for (int i = 0; i < doc->ext_count; i++) {
		if (strcmp(doc->ext[i].id, id) == 0) {
			return &doc->ext[i];
		}
	}

	return NULL;
}

int scene_doc_add_ext(scene_doc_t* doc, const scene_doc_ext_t* ext) {
	if (!doc || !ext || !ext->id[0] || !ext->path[0]) {
		return 0;
	}
	if (scene_doc_find_ext_by_id(doc, ext->id)) {
		return 0;
	}
	void* grown = nb_array_grow(doc->ext, &doc->ext_capacity, doc->ext_count + 1, sizeof(*doc->ext));
	if (!grown) {
		return 0;
	}
	doc->ext = grown;
	doc->ext[doc->ext_count++] = *ext;
	return 1;
}

static int scene_doc_copy_ext(scene_doc_t* doc, const tscn_file_t* file) {
	doc->ext_count = 0;

	for (int i = 0; i < file->ext_count; i++) {
		void* ext = nb_array_grow(doc->ext, &doc->ext_capacity, doc->ext_count + 1, sizeof(*doc->ext));
		if (!ext) {
			return 0;
		}

		doc->ext = ext;
		scene_doc_ext_t* out = &doc->ext[doc->ext_count++];
		memset(out, 0, sizeof(*out));
		snprintf(out->id, sizeof(out->id), "%s", file->ext[i].id);
		snprintf(out->type, sizeof(out->type), "%s", file->ext[i].type);
		snprintf(out->path, sizeof(out->path), "%s", file->ext[i].path);
		snprintf(out->uuid, sizeof(out->uuid), "%s", file->ext[i].uuid);
		out->kind = file->ext[i].kind;
		out->handle = file->ext[i].handle;
	}

	return 1;
}

static nb_result_t scene_doc_load_file(scene_doc_t* doc, tscn_file_t* file, const char* path, engine_t* engine, scene_t* scene) {
	char failed_path[NB_SCENE_DOC_STR];
	snprintf(failed_path, sizeof(failed_path), "%s", path);

	nb_result_t result = tscn_file_build_scene(file, engine, scene);
	if (result == NB_RESULT_OK && !scene_doc_copy_ext(doc, file)) {
		result = NB_RESULT_ERROR_RESOURCE;
	}

	if (result == NB_RESULT_OK) {
		doc->dirty = 0;
	}

	tscn_file_free(file);

	if (result != NB_RESULT_OK) {
		scene_shutdown(scene);
		scene_doc_clear(doc);
		snprintf(doc->path, sizeof(doc->path), "%s", failed_path);
	}

	return result;
}

nb_result_t scene_doc_load(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene) {
	if (!doc || !path || !engine || !scene) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	scene_doc_init(doc);
	snprintf(doc->path, sizeof(doc->path), "%s", path);
	scene_init(scene);

	tscn_file_t file;
	nb_result_t result = tscn_file_load(&file, path, engine);
	if (result != NB_RESULT_OK) {
		scene_init(scene);
		scene_doc_clear(doc);
		snprintf(doc->path, sizeof(doc->path), "%s", path);

		return result;
	}

	return scene_doc_load_file(doc, &file, path, engine, scene);
}

nb_result_t scene_doc_load_from_buffer(scene_doc_t* doc, const char* path, const char* data, size_t size, engine_t* engine, scene_t* scene) {
	if (!doc || !path || !data || !engine || !scene) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	scene_doc_init(doc);
	snprintf(doc->path, sizeof(doc->path), "%s", path);
	scene_init(scene);

	tscn_file_t file;
	nb_result_t result = tscn_file_load_from_buffer(&file, path, data, size, engine);
	if (result != NB_RESULT_OK) {
		scene_init(scene);
		scene_doc_clear(doc);
		snprintf(doc->path, sizeof(doc->path), "%s", path);

		return result;
	}

	return scene_doc_load_file(doc, &file, path, engine, scene);
}

static const char* scene_doc_node_type(const node_t* node) {
	if (!node) {
		return "Node3D";
	}

	if (node->components & NB_NODE_COMPONENT_CAMERA) {
		return "Camera3D";
	}

	if (node->components & NB_NODE_COMPONENT_MESH_RENDERER) {
		return "MeshInstance3D";
	}

	if (node->components & NB_NODE_COMPONENT_LIGHT) {
		return "DirectionalLight3D";
	}

	return "Node3D";
}

static void scene_doc_write_transform(FILE* file, const node_t* node) {
	mat4_t local;
	mat4_t t;
	mat4_t rx;
	mat4_t ry;
	mat4_t rz;
	mat4_t s;
	mat4_translate(&t, node->transform.position.x, node->transform.position.y, node->transform.position.z);
	mat4_rotate_x(&rx, node->transform.rotation.x);
	mat4_rotate_y(&ry, node->transform.rotation.y);
	mat4_rotate_z(&rz, node->transform.rotation.z);
	mat4_identity(&s);
	s.m[0] = node->transform.scale.x;
	s.m[5] = node->transform.scale.y;
	s.m[10] = node->transform.scale.z;
	mat4_multiply(&local, &t, &ry);
	mat4_multiply(&local, &local, &rx);
	mat4_multiply(&local, &local, &rz);
	mat4_multiply(&local, &local, &s);
	fprintf(file, "transform = Transform3D(%.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f)\n", local.m[0], local.m[1], local.m[2], local.m[4], local.m[5], local.m[6], local.m[8], local.m[9], local.m[10], local.m[12], local.m[13], local.m[14]);
}

static void scene_doc_mark_ext(scene_doc_t* doc, unsigned char* used, nb_resource_kind_t kind, uint32_t handle) {
	if (!doc || !used || kind == NB_RESOURCE_KIND_NONE || handle == NB_RESOURCE_INVALID_ID) {
		return;
	}

	for (int i = 0; i < doc->ext_count; i++) {
		if (doc->ext[i].kind == kind && doc->ext[i].handle == handle) {
			used[i] = 1;
			return;
		}
	}
}

static void scene_doc_mark_ext_path(scene_doc_t* doc, unsigned char* used, nb_resource_kind_t kind, const char* path) {
	if (!doc || !used || !path || !*path) {
		return;
	}

	for (int i = 0; i < doc->ext_count; i++) {
		if (doc->ext[i].kind == kind && strcmp(doc->ext[i].path, path) == 0) {
			used[i] = 1;
			return;
		}
	}
}

static void scene_doc_prune_ext(scene_doc_t* doc, const scene_t* scene) {
	if (!doc || !scene || doc->ext_count <= 0) {
		return;
	}

	unsigned char* used = (unsigned char*)calloc((size_t)doc->ext_count, 1);
	if (!used) {
		return;
	}

	for (int i = 0; i < scene->node_count; i++) {
		const node_t* node = &scene->nodes[i];
		if (node->components & NB_NODE_COMPONENT_INSTANCE_CHILD) {
			continue;
		}

		if (node->components & NB_NODE_COMPONENT_MESH_RENDERER) {
			scene_doc_mark_ext(doc, used, NB_RESOURCE_KIND_MESH, node->mesh_renderer.mesh);
			scene_doc_mark_ext(doc, used, NB_RESOURCE_KIND_MATERIAL, node->mesh_renderer.material);
		}
		if (node->components & NB_NODE_COMPONENT_SCRIPT) {
			scene_doc_mark_ext(doc, used, NB_RESOURCE_KIND_SCRIPT, node->script);
		}
		if (node->components & NB_NODE_COMPONENT_SCENE_INSTANCE) {
			scene_doc_mark_ext_path(doc, used, NB_RESOURCE_KIND_PACKED_SCENE, node->scene_instance_path);
		}
	}

	int write = 0;
	for (int i = 0; i < doc->ext_count; i++) {
		if (!used[i]) {
			continue;
		}

		if (write != i) {
			doc->ext[write] = doc->ext[i];
		}
		write++;
	}

	doc->ext_count = write;
	free(used);
}

static void scene_doc_write(scene_doc_t* doc, FILE* file, scene_t* scene, engine_t* engine) {
	scene_doc_prune_ext(doc, scene);
	fprintf(file, "[gd_scene load_steps=%d format=3]\n\n", doc->ext_count + 1);

	for (int i = 0; i < doc->ext_count; i++) {
		scene_doc_ext_t* ext = &doc->ext[i];
		const char* uuid = ext->uuid[0] ? ext->uuid : NULL;
		if (!uuid && engine && engine->assets_ready) {
			uuid = asset_db_uuid(&engine->assets, ext->path);
		}

		fprintf(file, "[ext_resource type=\"%s\" path=\"%s\"", ext->type, ext->path);
		if (uuid && uuid[0]) {
			fprintf(file, " uuid=\"%s\"", uuid);
		}

		fprintf(file, " id=\"%s\"]\n", ext->id);
	}

	if (doc->ext_count > 0) {
		fprintf(file, "\n");
	}

	for (int i = 0; i < scene->node_count; i++) {
		node_t* node = &scene->nodes[i];
		if (node->components & NB_NODE_COMPONENT_INSTANCE_CHILD) {
			continue;
		}

		const char* type = scene_doc_node_type(node);

		if (node->parent == NB_ENTITY_INVALID) {
			fprintf(file, "[node name=\"%s\" type=\"%s\" parent=\".\"]\n", node->name, type);
		}

		else {
			node_t* parent = scene_get_node(scene, node->parent);
			fprintf(file, "[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", node->name, type, parent ? parent->name : ".");
		}

		if (node->components & NB_NODE_COMPONENT_TRANSFORM) {
			scene_doc_write_transform(file, node);
		}

		if (node->components & NB_NODE_COMPONENT_MESH_RENDERER) {
			const scene_doc_ext_t* mesh_ext = scene_doc_find_ext(doc, NB_RESOURCE_KIND_MESH, node->mesh_renderer.mesh);
			const scene_doc_ext_t* mat_ext = scene_doc_find_ext(doc, NB_RESOURCE_KIND_MATERIAL, node->mesh_renderer.material);
			if (mesh_ext) {
				fprintf(file, "mesh = ExtResource(\"%s\")\n", mesh_ext->id);
			}
			if (mat_ext) {
				fprintf(file, "material = ExtResource(\"%s\")\n", mat_ext->id);
			}
		}

		if (node->components & NB_NODE_COMPONENT_SCENE_INSTANCE) {
			const scene_doc_ext_t* scene_ext = NULL;
			for (int j = 0; j < doc->ext_count; j++) {
				if (doc->ext[j].kind == NB_RESOURCE_KIND_PACKED_SCENE && strcmp(doc->ext[j].path, node->scene_instance_path) == 0) {
					scene_ext = &doc->ext[j];
					break;
				}
			}
			if (scene_ext) {
				fprintf(file, "instance = ExtResource(\"%s\")\n", scene_ext->id);
			}
		}

		if (node->components & NB_NODE_COMPONENT_CAMERA) {
			fprintf(file, "fov = %.6f\n", node->camera.fov);
			fprintf(file, "near = %.6f\n", node->camera.near_plane);
			fprintf(file, "far = %.6f\n", node->camera.far_plane);
			fprintf(file, "target = Vector3(%.6f, %.6f, %.6f)\n", node->camera.target.x, node->camera.target.y, node->camera.target.z);
			fprintf(file, "up = Vector3(%.6f, %.6f, %.6f)\n", node->camera.up.x, node->camera.up.y, node->camera.up.z);
		}

		if (node->components & NB_NODE_COMPONENT_LIGHT) {
			fprintf(file, "direction = Vector3(%.6f, %.6f, %.6f)\n", node->light.direction.x, node->light.direction.y, node->light.direction.z);
			fprintf(file, "intensity = %.6f\n", node->light.intensity);
		}

		if (node->components & NB_NODE_COMPONENT_COLLIDER) {
			fprintf(file, "collider_shape = \"%s\"\n", node->collider.shape == NB_COLLIDER_SPHERE ? "sphere" : "aabb");
			if (node->collider.shape == NB_COLLIDER_SPHERE) {
				fprintf(file, "collider_radius = %.6f\n", node->collider.radius);
			}

			else {
				fprintf(file, "collider_extents = Vector3(%.6f, %.6f, %.6f)\n", node->collider.extents.x, node->collider.extents.y, node->collider.extents.z);
			}
		}

		if (node->components & NB_NODE_COMPONENT_SCRIPT) {
			const scene_doc_ext_t* script_ext = scene_doc_find_ext(doc, NB_RESOURCE_KIND_SCRIPT, node->script);
			if (script_ext) {
				fprintf(file, "script = ExtResource(\"%s\")\n", script_ext->id);
			}
		}

		if (node->tint_enabled) {
			fprintf(file, "tint_enabled = true\n");
			fprintf(file, "tint = Vector3(%.6f, %.6f, %.6f)\n", node->tint.x, node->tint.y, node->tint.z);
		}

		if (!node->visible) {
			fprintf(file, "visible = false\n");
		}

		if (node->transform.spin_speed != 0.0f) {
			fprintf(file, "spin_speed = %.6f\n", node->transform.spin_speed);
		}

		fprintf(file, "\n");
	}
}

nb_result_t scene_doc_save(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene) {
	if (!doc || !path || !engine || !scene) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	char save_path[NB_SCENE_DOC_STR];
	snprintf(save_path, sizeof(save_path), "%s", path);
	if (!save_path[0] || strlen(save_path) + 4 >= sizeof(save_path)) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	char temp_path[NB_SCENE_DOC_STR];
	snprintf(temp_path, sizeof(temp_path), "%.*s.tmp", NB_SCENE_DOC_STR - 5, save_path);
	FILE* file = fopen(temp_path, "w");
	if (!file) {
		return NB_RESULT_ERROR_RESOURCE;
	}

	scene_doc_write(doc, file, scene, engine);
	if (fclose(file) != 0 || rename(temp_path, save_path) != 0) {
		remove(temp_path);
		return NB_RESULT_ERROR_RESOURCE;
	}

	snprintf(doc->path, sizeof(doc->path), "%s", save_path);
	doc->dirty = 0;
	engine_log(engine, "scene saved: %s", save_path);

	return NB_RESULT_OK;
}

nb_result_t scene_doc_save_to_buffer(scene_doc_t* doc, scene_t* scene, char** out_data, size_t* out_size) {
	if (!doc || !scene || !out_data || !out_size) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	*out_data = NULL;
	*out_size = 0;

	char* data = NULL;
	size_t size = 0;
	FILE* file = open_memstream(&data, &size);
	if (!file) {
		return NB_RESULT_ERROR_RESOURCE;
	}

	scene_doc_write(doc, file, scene, NULL);
	fclose(file);
	*out_data = data;
	*out_size = size;

	return NB_RESULT_OK;
}
