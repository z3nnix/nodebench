#define _POSIX_C_SOURCE 200809L
#include "scene/scene_doc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "resource/loader.h"
#include "resource/loader_tres.h"
#include "script/script.h"

#define NB_TSCN_MAX_EXT_RESOURCES 128
#define NB_TSCN_MAX_NODES 256
#define NB_TSCN_LINE_CAPACITY 1024

typedef enum {
	TSCN_SECTION_HEADER = 0,
	TSCN_SECTION_EXT,
	TSCN_SECTION_NODE,
	TSCN_SECTION_OTHER
} tscn_section_t;

typedef struct {
	char id[64];
	char type[64];
	char path[256];
	uint32_t handle;
	image_t image;
	palette_t palette;
	int has_image;
	int has_palette;
} tscn_ext_resource_t;

typedef struct {
	char name[64];
	char type[64];
	char parent[128];
	transform_t transform;
	int has_transform;
	int mesh_ref_kind;
	char mesh_ref[64];
	int material_ref_kind;
	char material_ref[64];
	int script_ref_kind;
	char script_ref[64];
	camera_t camera;
	light_t light;
	collider_t collider;
	int has_collider;
	int visible;
} tscn_node_t;

static void tscn_init_collider(collider_t* collider) {
	collider->shape = NB_COLLIDER_AABB;
	collider->extents = (vec3_t){ 0.5f, 0.5f, 0.5f };
	collider->radius = 0.5f;
}

static char* tscn_trim(char* text) {
	char* start = text;
	while (*start && isspace((unsigned char)*start)) {
		start++;
	}

	char* end = start + strlen(start);
	while (end > start && isspace((unsigned char)end[-1])) {
		end--;
	}

	*end = '\0';

	return start;
}

static void tscn_parse_value(const char* value, char* out, size_t size) {
	if (!value || size == 0) {
		return;
	}

	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	out[0] = '\0';

	if (*value == '"' || *value == '\'') {
		char quote = *value++;
		size_t index = 0;
		while (*value && *value != quote && index + 1 < size) {
			out[index++] = *value++;
		}
		out[index] = '\0';
		return;
	}

	size_t index = 0;
	while (*value && !isspace((unsigned char)*value) && index + 1 < size) {
		out[index++] = *value++;
	}
	out[index] = '\0';
}

static int tscn_header_value(const char* header, const char* key, char* out, size_t size) {
	size_t key_length = strlen(key);
	const char* position = header;

	while ((position = strstr(position, key)) != NULL) {
		int boundary = position == header || position[-1] == ' ' || position[-1] == '[';
		const char* after = position + key_length;

		if (boundary && *after == '=') {
			tscn_parse_value(after + 1, out, size);
			return 1;
		}

		position += key_length;
	}

	out[0] = '\0';
	return 0;
}

static int tscn_split_property(char* line, char** key, char** value) {
	char* equal = strchr(line, '=');
	if (!equal) {
		return 0;
	}

	*equal = '\0';
	*key = tscn_trim(line);
	*value = tscn_trim(equal + 1);
	return *key[0] && *value[0];
}

static int tscn_parse_floats(const char* value, const char* prefix, float* out, int capacity) {
	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	size_t prefix_length = strlen(prefix);
	if (strncmp(value, prefix, prefix_length) != 0 || value[prefix_length] != '(') {
		return 0;
	}

	const char* position = value + prefix_length + 1;
	int count = 0;

	while (count < capacity) {
		while (*position && (isspace((unsigned char)*position) || *position == ',')) {
			position++;
		}

		if (*position == '\0' || *position == ')') {
			break;
		}

		char* end = NULL;
		float number = strtof(position, &end);
		if (end == position) {
			return 0;
		}

		out[count++] = number;
		position = end;
	}

	return count;
}

static int tscn_parse_vector3(const char* value, vec3_t* out) {
	float numbers[3];
	if (tscn_parse_floats(value, "Vector3", numbers, 3) != 3) {
		return 0;
	}

	out->x = numbers[0];
	out->y = numbers[1];
	out->z = numbers[2];
	return 1;
}

static int tscn_parse_transform(const char* value, transform_t* transform) {
	float numbers[12];
	if (tscn_parse_floats(value, "Transform3D", numbers, 12) != 12) {
		return 0;
	}

	transform->position.x = numbers[9];
	transform->position.y = numbers[10];
	transform->position.z = numbers[11];
	transform->scale.x = sqrtf(numbers[0] * numbers[0] + numbers[1] * numbers[1] + numbers[2] * numbers[2]);
	transform->scale.y = sqrtf(numbers[3] * numbers[3] + numbers[4] * numbers[4] + numbers[5] * numbers[5]);
	transform->scale.z = sqrtf(numbers[6] * numbers[6] + numbers[7] * numbers[7] + numbers[8] * numbers[8]);
	return 1;
}

static int tscn_parse_reference(const char* value, char* id, size_t size) {
	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	int kind = 0;
	if (strncmp(value, "ExtResource", 11) == 0) {
		kind = 1;
	}

	else if (strncmp(value, "SubResource", 11) == 0) {
		kind = 2;
	}

	else {
		return 0;
	}

	const char* open = strchr(value, '(');
	if (!open) {
		return 0;
	}

	tscn_parse_value(open + 1, id, size);
	if (!id[0]) {
		return 0;
	}

	return kind;
}

static const char* tscn_resolve_path(const char* path) {
	static char buffer[512];
	FILE* file = NULL;

	if (strncmp(path, "res://", 6) == 0) {
		const char* relative = path + 6;

		snprintf(buffer, sizeof(buffer), "%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);
			return buffer;
		}

		snprintf(buffer, sizeof(buffer), "assets/%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);
			return buffer;
		}

		snprintf(buffer, sizeof(buffer), "examples/%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);
			return buffer;
		}

		buffer[0] = '\0';
		return buffer;
	}

	snprintf(buffer, sizeof(buffer), "assets/%s", path);
	file = fopen(buffer, "r");
	if (file) {
		fclose(file);
		return buffer;
	}

	snprintf(buffer, sizeof(buffer), "examples/%s", path);
	file = fopen(buffer, "r");
	if (file) {
		fclose(file);
		return buffer;
	}

	buffer[0] = '\0';
	return buffer;
}

static tscn_ext_resource_t* tscn_find_ext(tscn_ext_resource_t* resources, int count, const char* id) {
	if (!id[0]) {
		return NULL;
	}

	for (int i = 0; i < count; i++) {
		if (strcmp(resources[i].id, id) == 0) {
			return &resources[i];
		}
	}

	return NULL;
}

static int tscn_load_ext_resource(engine_t* engine, tscn_ext_resource_t* resource) {
	const char* path = tscn_resolve_path(resource->path);
	if (!path[0]) {
		return 0;
	}

	if (strcmp(resource->type, "Mesh") == 0) {
		mesh_t mesh;
		if (!obj_load(path, &mesh)) {
			return 0;
		}

		resource->handle = resource_add_mesh(&engine->resources, &mesh);
		mesh_destroy(&mesh);
		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (strcmp(resource->type, "Texture2D") == 0) {
		if (!image_load(path, &resource->image)) {
			return 0;
		}
		resource->has_image = 1;
		return 1;
	}

	if (strcmp(resource->type, "Palette") == 0 || strcmp(resource->type, "CLUT") == 0) {
		if (!clut_load(path, &resource->palette)) {
			return 0;
		}
		resource->has_palette = 1;
		return 1;
	}

	if (strcmp(resource->type, "NBPS1Material") == 0) {
		resource->handle = material_load_tres(engine, path);
		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (strcmp(resource->type, "WrenScript") == 0 || strcmp(resource->type, "Script") == 0) {
		char* source = script_load_file(path);
		if (!source) {
			return 0;
		}

		resource->handle = resource_add_script(&engine->resources, source);
		free(source);
		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (strcmp(resource->type, "AudioStreamWAV") == 0 || strcmp(resource->type, "AudioStream") == 0) {
		audio_stream_t stream;
		const char* name = resource->id;
		if (!wav_load(path, &stream)) {
			return 0;
		}

		if (strncmp(name, "audio_", 6) == 0 && name[6] != '\0') {
			name = name + 6;
		}

		resource->handle = resource_add_audio(&engine->resources, &stream, name);
		audio_stream_free(&stream);
		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	return 1;
}

static void tscn_free_ext_resources(tscn_ext_resource_t* resources, int count) {
	for (int i = 0; i < count; i++) {
		if (resources[i].has_image) {
			image_free(&resources[i].image);
		}
		if (resources[i].has_palette) {
			clut_free(&resources[i].palette);
		}
	}
}

static void scene_doc_copy_ext(scene_doc_t* doc, tscn_ext_resource_t* ext, int count) {
	doc->ext_count = 0;
	for (int i = 0; i < count && doc->ext_count < NB_SCENE_DOC_MAX_EXT; i++) {
		scene_doc_ext_t* out = &doc->ext[doc->ext_count++];
		memset(out, 0, sizeof(*out));
		snprintf(out->id, sizeof(out->id), "%s", ext[i].id);
		snprintf(out->type, sizeof(out->type), "%s", ext[i].type);
		snprintf(out->path, sizeof(out->path), "%s", ext[i].path);
		out->handle = ext[i].handle;
	}
}

static int tscn_create_entities(engine_t* engine, scene_t* scene, tscn_node_t* nodes, int count, tscn_ext_resource_t* ext_resources, int ext_count) {
	entity_id_t ids[NB_TSCN_MAX_NODES];

	for (int i = 0; i < count; i++) {
		tscn_node_t* node = &nodes[i];
		entity_id_t id = scene_create_entity(scene);
		if (id == NB_ENTITY_INVALID) {
			engine_log(engine, "tscn entity overflow at node=%s", node->name);
			return 0;
		}

		ids[i] = id;
		if (!scene_set_node_name(scene, id, node->name)) {
			engine_log(engine, "tscn name failed at node=%s", node->name);
			return 0;
		}

		scene_set_node_visible(scene, id, node->visible);

		if (node->script_ref_kind == 1) {
			tscn_ext_resource_t* script_resource = tscn_find_ext(ext_resources, ext_count, node->script_ref);
			if (!script_resource || script_resource->handle == NB_RESOURCE_INVALID_ID || !scene_add_script(scene, id, script_resource->handle, node->name)) {
				engine_log(engine, "tscn script failed at node=%s", node->name);
				return 0;
			}
		}

		if (node->has_collider && !scene_add_collider(scene, id, node->collider)) {
			engine_log(engine, "tscn collider failed at node=%s", node->name);
			return 0;
		}

		if (strcmp(node->type, "Camera3D") == 0) {
			if (node->has_transform && !scene_add_transform(scene, id, node->transform)) {
				engine_log(engine, "tscn transform failed at node=%s", node->name);
				return 0;
			}

			camera_t camera = node->camera;
			if (node->has_transform) {
				camera.eye = node->transform.position;
			}

			if (!scene_add_camera(scene, id, camera)) {
				engine_log(engine, "tscn camera failed at node=%s", node->name);
				return 0;
			}
			continue;
		}

		if (strcmp(node->type, "MeshInstance3D") == 0) {
			tscn_ext_resource_t* mesh_resource = NULL;
			if (node->mesh_ref_kind == 1) {
				mesh_resource = tscn_find_ext(ext_resources, ext_count, node->mesh_ref);
			}

			tscn_ext_resource_t* material_resource = NULL;
			if (node->material_ref_kind == 1) {
				material_resource = tscn_find_ext(ext_resources, ext_count, node->material_ref);
			}

			if (!mesh_resource || !material_resource || mesh_resource->handle == NB_RESOURCE_INVALID_ID || material_resource->handle == NB_RESOURCE_INVALID_ID) {
				engine_log(engine, "tscn mesh resources failed at node=%s", node->name);
				return 0;
			}

			mesh_renderer_t renderer;
			renderer.mesh = mesh_resource->handle;
			renderer.material = material_resource->handle;

			if (!scene_add_transform(scene, id, node->transform) || !scene_add_mesh_renderer(scene, id, renderer)) {
				engine_log(engine, "tscn mesh component failed at node=%s", node->name);
				return 0;
			}
			continue;
		}

		if (strcmp(node->type, "DirectionalLight3D") == 0) {
			if (!scene_add_light(scene, id, node->light)) {
				engine_log(engine, "tscn light failed at node=%s", node->name);
				return 0;
			}
			continue;
		}

		if (node->has_transform && strcmp(node->type, "Node3D") == 0 && !scene_add_transform(scene, id, node->transform)) {
			engine_log(engine, "tscn transform failed at node=%s", node->name);
			return 0;
		}
	}

	for (int i = 0; i < count; i++) {
		tscn_node_t* node = &nodes[i];
		if (!node->parent[0] || strcmp(node->parent, ".") == 0) {
			continue;
		}

		entity_id_t parent = scene_find_node(scene, node->parent);
		if (parent == NB_ENTITY_INVALID || !scene_set_node_parent(scene, ids[i], parent)) {
			engine_log(engine, "tscn parent failed at node=%s parent=%s", node->name, node->parent);
			return 0;
		}
	}

	scene_update_world_transforms(scene);
	scene_rebuild_cache(scene);
	return 1;
}

void scene_doc_init(scene_doc_t* doc) {
	if (!doc) { return; }
	memset(doc, 0, sizeof(*doc));
}

void scene_doc_clear(scene_doc_t* doc) {
	if (!doc) { return; }
	memset(doc, 0, sizeof(*doc));
}

const scene_doc_ext_t* scene_doc_find_ext_by_handle(const scene_doc_t* doc, uint32_t handle) {
	if (!doc || handle == NB_RESOURCE_INVALID_ID) { return NULL; }
	for (int i = 0; i < doc->ext_count; i++) {
		if (doc->ext[i].handle == handle) { return &doc->ext[i]; }
	}
	return NULL;
}

const scene_doc_ext_t* scene_doc_find_ext_by_id(const scene_doc_t* doc, const char* id) {
	if (!doc || !id) { return NULL; }
	for (int i = 0; i < doc->ext_count; i++) {
		if (strcmp(doc->ext[i].id, id) == 0) { return &doc->ext[i]; }
	}
	return NULL;
}

nb_result_t scene_doc_load(scene_doc_t* doc, const char* path, engine_t* engine, scene_t* scene) {
	if (!doc || !path || !engine || !scene) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	FILE* file = fopen(path, "r");
	if (!file) { return NB_RESULT_ERROR_RESOURCE; }

	scene_init(scene);
	scene_doc_init(doc);
	snprintf(doc->path, sizeof(doc->path), "%s", path);

	tscn_ext_resource_t ext_resources[NB_TSCN_MAX_EXT_RESOURCES];
	tscn_node_t nodes[NB_TSCN_MAX_NODES];
	int ext_count = 0;
	int node_count = 0;
	nb_result_t result = NB_RESULT_OK;
	tscn_section_t section = TSCN_SECTION_HEADER;
	char line[NB_TSCN_LINE_CAPACITY];

	memset(ext_resources, 0, sizeof(ext_resources));
	memset(nodes, 0, sizeof(nodes));

	while (fgets(line, sizeof(line), file)) {
		char* text = tscn_trim(line);
		char* comment = strchr(text, ';');
		if (comment) {
			*comment = '\0';
			text = tscn_trim(text);
		}

		if (!text[0]) { continue; }

		if (text[0] == '[') {
			if (strncmp(text, "[gd_scene", 9) == 0) {
				section = TSCN_SECTION_HEADER;
			}

			else if (strncmp(text, "[ext_resource", 13) == 0) {
				section = TSCN_SECTION_EXT;
				if (ext_count >= NB_TSCN_MAX_EXT_RESOURCES) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}

				tscn_ext_resource_t* resource = &ext_resources[ext_count++];
				memset(resource, 0, sizeof(*resource));
				tscn_header_value(text, "id", resource->id, sizeof(resource->id));
				tscn_header_value(text, "type", resource->type, sizeof(resource->type));
				tscn_header_value(text, "path", resource->path, sizeof(resource->path));
				if (!tscn_load_ext_resource(engine, resource)) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}
			}

			else if (strncmp(text, "[sub_resource", 13) == 0) {
				section = TSCN_SECTION_OTHER;
			}

			else if (strncmp(text, "[node", 5) == 0) {
				section = TSCN_SECTION_NODE;
				if (node_count >= NB_TSCN_MAX_NODES) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}

				tscn_node_t* node = &nodes[node_count++];
				memset(node, 0, sizeof(*node));
				tscn_init_collider(&node->collider);
				node->transform.scale.x = 1.0f;
				node->transform.scale.y = 1.0f;
				node->transform.scale.z = 1.0f;
				node->camera.fov = 60.0f;
				node->camera.near_plane = 0.1f;
				node->camera.far_plane = 100.0f;
				node->camera.up.y = 1.0f;
				node->light.intensity = 1.0f;
				node->light.direction.z = 1.0f;
				node->visible = 1;
				tscn_header_value(text, "name", node->name, sizeof(node->name));
				tscn_header_value(text, "type", node->type, sizeof(node->type));
				tscn_header_value(text, "parent", node->parent, sizeof(node->parent));
			}

			else {
				section = TSCN_SECTION_OTHER;
			}

			continue;
		}

		char* key = NULL;
		char* value = NULL;
		if (!tscn_split_property(text, &key, &value)) { continue; }
		if (section == TSCN_SECTION_EXT) { continue; }

		if (section == TSCN_SECTION_NODE && node_count > 0) {
			tscn_node_t* node = &nodes[node_count - 1];
			if (strcmp(key, "transform") == 0) {
				node->has_transform = tscn_parse_transform(value, &node->transform);
			}
			else if (strcmp(key, "rotation_deg") == 0) {
				vec3_t rotation;
				if (tscn_parse_vector3(value, &rotation)) {
					node->transform.rotation = vec3_scale(rotation, 3.141592654f / 180.0f);
					node->has_transform = 1;
				}
			}
			else if (strcmp(key, "mesh") == 0) {
				node->mesh_ref_kind = tscn_parse_reference(value, node->mesh_ref, sizeof(node->mesh_ref));
			}
			else if (strcmp(key, "material") == 0) {
				node->material_ref_kind = tscn_parse_reference(value, node->material_ref, sizeof(node->material_ref));
			}
			else if (strcmp(key, "script") == 0) {
				node->script_ref_kind = tscn_parse_reference(value, node->script_ref, sizeof(node->script_ref));
			}
			else if (strcmp(key, "collider_shape") == 0) {
				char shape[32];
				tscn_parse_value(value, shape, sizeof(shape));
				if (strcmp(shape, "sphere") == 0) { node->collider.shape = NB_COLLIDER_SPHERE; }
				else if (strcmp(shape, "aabb") == 0) { node->collider.shape = NB_COLLIDER_AABB; }
				node->has_collider = node->collider.shape == NB_COLLIDER_SPHERE || node->collider.shape == NB_COLLIDER_AABB;
			}
			else if (strcmp(key, "collider_extents") == 0) {
				if (tscn_parse_vector3(value, &node->collider.extents)) { node->has_collider = 1; }
			}
			else if (strcmp(key, "collider_radius") == 0) {
				node->collider.radius = strtof(value, NULL);
				node->has_collider = node->collider.radius > 0.0f;
			}
			else if (strcmp(key, "fov") == 0) { node->camera.fov = strtof(value, NULL); }
			else if (strcmp(key, "near") == 0) { node->camera.near_plane = strtof(value, NULL); }
			else if (strcmp(key, "far") == 0) { node->camera.far_plane = strtof(value, NULL); }
			else if (strcmp(key, "target") == 0) { tscn_parse_vector3(value, &node->camera.target); }
			else if (strcmp(key, "up") == 0) { tscn_parse_vector3(value, &node->camera.up); }
			else if (strcmp(key, "direction") == 0) {
				vec3_t direction;
				if (tscn_parse_vector3(value, &direction)) { node->light.direction = vec3_normalize(direction); }
			}
			else if (strcmp(key, "intensity") == 0) { node->light.intensity = strtof(value, NULL); }
			else if (strcmp(key, "spin_speed") == 0) {
				node->transform.spin_speed = strtof(value, NULL);
				node->has_transform = 1;
			}
			else if (strcmp(key, "visible") == 0) {
				node->visible = strcmp(value, "false") != 0 && strcmp(value, "0") != 0;
			}
		}
	}

	fclose(file);

	if (result == NB_RESULT_OK && !tscn_create_entities(engine, scene, nodes, node_count, ext_resources, ext_count)) {
		result = NB_RESULT_ERROR_SCENE;
	}

	if (result == NB_RESULT_OK) {
		scene_doc_copy_ext(doc, ext_resources, ext_count);
		doc->dirty = 0;
	}

	tscn_free_ext_resources(ext_resources, ext_count);

	if (result != NB_RESULT_OK) {
		char failed_path[NB_SCENE_DOC_STR];
		snprintf(failed_path, sizeof(failed_path), "%s", path);
		scene_shutdown(scene);
		scene_doc_clear(doc);
		snprintf(doc->path, sizeof(doc->path), "%s", failed_path);
	}

	return result;
}

static const char* scene_doc_node_type(const node_t* node) {
	if (!node) { return "Node3D"; }
	if (node->components & NB_NODE_COMPONENT_CAMERA) { return "Camera3D"; }
	if (node->components & NB_NODE_COMPONENT_MESH_RENDERER) { return "MeshInstance3D"; }
	if (node->components & NB_NODE_COMPONENT_LIGHT) { return "DirectionalLight3D"; }
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

static void scene_doc_write(scene_doc_t* doc, FILE* file, scene_t* scene) {
	fprintf(file, "[gd_scene load_steps=%d format=3]\n\n", doc->ext_count + 1);
	for (int i = 0; i < doc->ext_count; i++) {
		scene_doc_ext_t* ext = &doc->ext[i];
		fprintf(file, "[ext_resource type=\"%s\" path=\"%s\" id=\"%s\"]\n", ext->type, ext->path, ext->id);
	}
	if (doc->ext_count > 0) { fprintf(file, "\n"); }

	for (int i = 0; i < scene->node_count; i++) {
		node_t* node = &scene->nodes[i];
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

		if ((node->components & NB_NODE_COMPONENT_MESH_RENDERER)) {
			const scene_doc_ext_t* mesh_ext = scene_doc_find_ext_by_handle(doc, node->mesh_renderer.mesh);
			const scene_doc_ext_t* mat_ext = scene_doc_find_ext_by_handle(doc, node->mesh_renderer.material);
			if (mesh_ext) { fprintf(file, "mesh = ExtResource(\"%s\")\n", mesh_ext->id); }
			if (mat_ext) { fprintf(file, "material = ExtResource(\"%s\")\n", mat_ext->id); }
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
			const scene_doc_ext_t* script_ext = scene_doc_find_ext_by_handle(doc, node->script);
			if (script_ext) { fprintf(file, "script = ExtResource(\"%s\")\n", script_ext->id); }
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
	if (!doc || !path || !engine || !scene) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	char save_path[NB_SCENE_DOC_STR];
	snprintf(save_path, sizeof(save_path), "%s", path);
	if (!save_path[0]) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	FILE* file = fopen(save_path, "w");
	if (!file) { return NB_RESULT_ERROR_RESOURCE; }

	scene_doc_write(doc, file, scene);
	fclose(file);
	snprintf(doc->path, sizeof(doc->path), "%s", save_path);
	doc->dirty = 0;
	engine_log(engine, "scene saved: %s", save_path);
	return NB_RESULT_OK;
}

nb_result_t scene_doc_save_to_buffer(scene_doc_t* doc, scene_t* scene, char** out_data, size_t* out_size) {
	if (!doc || !scene || !out_data || !out_size) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }
	*out_data = NULL;
	*out_size = 0;
	char* data = NULL;
	size_t size = 0;
	FILE* file = open_memstream(&data, &size);
	if (!file) { return NB_RESULT_ERROR_RESOURCE; }
	scene_doc_write(doc, file, scene);
	fclose(file);
	*out_data = data;
	*out_size = size;
	return NB_RESULT_OK;
}

nb_result_t scene_doc_load_from_buffer(scene_doc_t* doc, const char* path, const char* data, size_t size, engine_t* engine, scene_t* scene) {
	if (!doc || !path || !data || !engine || !scene) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }
	FILE* file = fmemopen((void*)data, size, "r");
	if (!file) { return NB_RESULT_ERROR_RESOURCE; }

	scene_init(scene);
	scene_doc_init(doc);
	snprintf(doc->path, sizeof(doc->path), "%s", path);

	tscn_ext_resource_t ext_resources[NB_TSCN_MAX_EXT_RESOURCES];
	tscn_node_t nodes[NB_TSCN_MAX_NODES];
	int ext_count = 0;
	int node_count = 0;
	nb_result_t result = NB_RESULT_OK;
	tscn_section_t section = TSCN_SECTION_HEADER;
	char line[NB_TSCN_LINE_CAPACITY];

	memset(ext_resources, 0, sizeof(ext_resources));
	memset(nodes, 0, sizeof(nodes));

	while (fgets(line, sizeof(line), file)) {
		char* text = tscn_trim(line);
		char* comment = strchr(text, ';');
		if (comment) {
			*comment = '\0';
			text = tscn_trim(text);
		}

		if (!text[0]) { continue; }

		if (text[0] == '[') {
			if (strncmp(text, "[gd_scene", 9) == 0) {
				section = TSCN_SECTION_HEADER;
			}

			else if (strncmp(text, "[ext_resource", 13) == 0) {
				section = TSCN_SECTION_EXT;
				if (ext_count >= NB_TSCN_MAX_EXT_RESOURCES) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}

				tscn_ext_resource_t* resource = &ext_resources[ext_count++];
				memset(resource, 0, sizeof(*resource));
				tscn_header_value(text, "id", resource->id, sizeof(resource->id));
				tscn_header_value(text, "type", resource->type, sizeof(resource->type));
				tscn_header_value(text, "path", resource->path, sizeof(resource->path));
				if (!tscn_load_ext_resource(engine, resource)) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}
			}

			else if (strncmp(text, "[sub_resource", 13) == 0) {
				section = TSCN_SECTION_OTHER;
			}

			else if (strncmp(text, "[node", 5) == 0) {
				section = TSCN_SECTION_NODE;
				if (node_count >= NB_TSCN_MAX_NODES) {
					result = NB_RESULT_ERROR_RESOURCE;
					break;
				}

				tscn_node_t* node = &nodes[node_count++];
				memset(node, 0, sizeof(*node));
				tscn_init_collider(&node->collider);
				node->transform.scale.x = 1.0f;
				node->transform.scale.y = 1.0f;
				node->transform.scale.z = 1.0f;
				node->camera.fov = 60.0f;
				node->camera.near_plane = 0.1f;
				node->camera.far_plane = 100.0f;
				node->camera.up.y = 1.0f;
				node->light.intensity = 1.0f;
				node->light.direction.z = 1.0f;
				node->visible = 1;
				tscn_header_value(text, "name", node->name, sizeof(node->name));
				tscn_header_value(text, "type", node->type, sizeof(node->type));
				tscn_header_value(text, "parent", node->parent, sizeof(node->parent));
			}

			else {
				section = TSCN_SECTION_OTHER;
			}

			continue;
		}

		char* key = NULL;
		char* value = NULL;
		if (!tscn_split_property(text, &key, &value)) { continue; }
		if (section == TSCN_SECTION_EXT) { continue; }

		if (section == TSCN_SECTION_NODE && node_count > 0) {
			tscn_node_t* node = &nodes[node_count - 1];
			if (strcmp(key, "transform") == 0) {
				node->has_transform = tscn_parse_transform(value, &node->transform);
			}
			else if (strcmp(key, "rotation_deg") == 0) {
				vec3_t rotation;
				if (tscn_parse_vector3(value, &rotation)) {
					node->transform.rotation = vec3_scale(rotation, 3.141592654f / 180.0f);
					node->has_transform = 1;
				}
			}
			else if (strcmp(key, "mesh") == 0) {
				node->mesh_ref_kind = tscn_parse_reference(value, node->mesh_ref, sizeof(node->mesh_ref));
			}
			else if (strcmp(key, "material") == 0) {
				node->material_ref_kind = tscn_parse_reference(value, node->material_ref, sizeof(node->material_ref));
			}
			else if (strcmp(key, "script") == 0) {
				node->script_ref_kind = tscn_parse_reference(value, node->script_ref, sizeof(node->script_ref));
			}
			else if (strcmp(key, "collider_shape") == 0) {
				char shape[32];
				tscn_parse_value(value, shape, sizeof(shape));
				if (strcmp(shape, "sphere") == 0) { node->collider.shape = NB_COLLIDER_SPHERE; }
				else if (strcmp(shape, "aabb") == 0) { node->collider.shape = NB_COLLIDER_AABB; }
				node->has_collider = node->collider.shape == NB_COLLIDER_SPHERE || node->collider.shape == NB_COLLIDER_AABB;
			}
			else if (strcmp(key, "collider_extents") == 0) {
				if (tscn_parse_vector3(value, &node->collider.extents)) { node->has_collider = 1; }
			}
			else if (strcmp(key, "collider_radius") == 0) {
				node->collider.radius = strtof(value, NULL);
				node->has_collider = node->collider.radius > 0.0f;
			}
			else if (strcmp(key, "fov") == 0) { node->camera.fov = strtof(value, NULL); }
			else if (strcmp(key, "near") == 0) { node->camera.near_plane = strtof(value, NULL); }
			else if (strcmp(key, "far") == 0) { node->camera.far_plane = strtof(value, NULL); }
			else if (strcmp(key, "target") == 0) { tscn_parse_vector3(value, &node->camera.target); }
			else if (strcmp(key, "up") == 0) { tscn_parse_vector3(value, &node->camera.up); }
			else if (strcmp(key, "direction") == 0) {
				vec3_t direction;
				if (tscn_parse_vector3(value, &direction)) { node->light.direction = vec3_normalize(direction); }
			}
			else if (strcmp(key, "intensity") == 0) { node->light.intensity = strtof(value, NULL); }
			else if (strcmp(key, "spin_speed") == 0) {
				node->transform.spin_speed = strtof(value, NULL);
				node->has_transform = 1;
			}
			else if (strcmp(key, "visible") == 0) {
				node->visible = strcmp(value, "false") != 0 && strcmp(value, "0") != 0;
			}
		}
	}

	fclose(file);

	if (result == NB_RESULT_OK && !tscn_create_entities(engine, scene, nodes, node_count, ext_resources, ext_count)) {
		result = NB_RESULT_ERROR_SCENE;
	}

	if (result == NB_RESULT_OK) {
		scene_doc_copy_ext(doc, ext_resources, ext_count);
		doc->dirty = 0;
	}

	tscn_free_ext_resources(ext_resources, ext_count);

	if (result != NB_RESULT_OK) {
		char failed_path[NB_SCENE_DOC_STR];
		snprintf(failed_path, sizeof(failed_path), "%s", path);
		scene_shutdown(scene);
		scene_doc_clear(doc);
		snprintf(doc->path, sizeof(doc->path), "%s", failed_path);
	}

	return result;
}
