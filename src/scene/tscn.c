#define _POSIX_C_SOURCE 200809L
#include "scene/tscn.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset/asset_db.h"
#include "audio/audio.h"
#include "core/dynamic_array.h"
#include "resource/loader.h"
#include "resource/loader_tres.h"
#include "script/script.h"

static nb_resource_kind_t tscn_kind_from_type(const char* type) {
	if (!type || !*type) {
		return NB_RESOURCE_KIND_NONE;
	}

	if (strcmp(type, "Mesh") == 0) {
		return NB_RESOURCE_KIND_MESH;
	}

	if (strcmp(type, "Texture2D") == 0) {
		return NB_RESOURCE_KIND_TEXTURE;
	}

	if (strcmp(type, "Palette") == 0 || strcmp(type, "CLUT") == 0) {
		return NB_RESOURCE_KIND_PALETTE;
	}

	if (strcmp(type, "NBPS1Material") == 0) {
		return NB_RESOURCE_KIND_MATERIAL;
	}

	if (strcmp(type, "WrenScript") == 0 || strcmp(type, "Script") == 0) {
		return NB_RESOURCE_KIND_SCRIPT;
	}

	if (strcmp(type, "AudioStreamWAV") == 0 || strcmp(type, "AudioStream") == 0) {
		return NB_RESOURCE_KIND_AUDIO;
	}

	return NB_RESOURCE_KIND_NONE;
}

static void tscn_init_node(tscn_node_t* node) {
	memset(node, 0, sizeof(*node));
	node->collider.shape = NB_COLLIDER_AABB;
	node->collider.extents = (vec3_t){ 0.5f, 0.5f, 0.5f };
	node->collider.radius = 0.5f;
	node->transform.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
	node->camera.fov = 60.0f;
	node->camera.near_plane = 0.1f;
	node->camera.far_plane = 100.0f;
	node->camera.up.y = 1.0f;
	node->light.intensity = 1.0f;
	node->light.direction.z = 1.0f;
	node->visible = 1;
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

	float scale_x = sqrtf(numbers[0] * numbers[0] + numbers[1] * numbers[1] + numbers[2] * numbers[2]);
	float scale_y = sqrtf(numbers[3] * numbers[3] + numbers[4] * numbers[4] + numbers[5] * numbers[5]);
	float scale_z = sqrtf(numbers[6] * numbers[6] + numbers[7] * numbers[7] + numbers[8] * numbers[8]);
	if (scale_x <= 0.0f || scale_y <= 0.0f || scale_z <= 0.0f) {
		return 0;
	}

	transform->position.x = numbers[9];
	transform->position.y = numbers[10];
	transform->position.z = numbers[11];
	transform->scale.x = scale_x;
	transform->scale.y = scale_y;
	transform->scale.z = scale_z;

	float sin_x = -numbers[7] / scale_z;
	if (sin_x > 1.0f) {
		sin_x = 1.0f;
	}
	if (sin_x < -1.0f) {
		sin_x = -1.0f;
	}

	float cos_x = sqrtf(1.0f - sin_x * sin_x);
	transform->rotation.x = asinf(sin_x);

	if (cos_x > 0.0001f) {
		transform->rotation.y = atan2f(numbers[6] / scale_z, numbers[8] / scale_z);
		transform->rotation.z = atan2f(numbers[1] / scale_x, numbers[4] / scale_x);
	}

	else {
		transform->rotation.y = atan2f(-numbers[2] / scale_x, numbers[0] / scale_x);
		transform->rotation.z = 0.0f;
	}

	return 1;
}

static int tscn_parse_reference(const char* value, char* id, size_t size) {
	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	if (strncmp(value, "ExtResource", 11) != 0) {
		return 0;
	}

	const char* open = strchr(value, '(');
	if (!open) {
		return 0;
	}

	tscn_parse_value(open + 1, id, size);

	return id[0] != '\0';
}

static int tscn_set_resource_path(char* out, size_t size, const char* relative) {
	size_t length = strlen(relative);
	if (size < length + 7) {
		return 0;
	}

	memcpy(out, "res://", 6);
	memcpy(out + 6, relative, length + 1);

	return 1;
}

static const char* tscn_resolve_path(engine_t* engine, const char* path) {
	static char buffer[512];
	FILE* file = NULL;

	if (engine && engine->assets_ready && asset_db_resolve(&engine->assets, path, buffer, sizeof(buffer))) {
		return buffer;
	}

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

const tscn_ext_t* tscn_file_find_ext_by_id(const tscn_file_t* file, const char* id) {
	if (!file || !id || !id[0]) {
		return NULL;
	}

	for (int i = 0; i < file->ext_count; i++) {
		if (strcmp(file->ext[i].id, id) == 0) {
			return &file->ext[i];
		}
	}

	return NULL;
}

const tscn_ext_t* tscn_file_find_ext(const tscn_file_t* file, nb_resource_kind_t kind, uint32_t handle) {
	if (!file || kind == NB_RESOURCE_KIND_NONE || handle == NB_RESOURCE_INVALID_ID) {
		return NULL;
	}

	for (int i = 0; i < file->ext_count; i++) {
		if (file->ext[i].kind == kind && file->ext[i].handle == handle) {
			return &file->ext[i];
		}
	}

	return NULL;
}

static int tscn_load_ext_resource(engine_t* engine, tscn_ext_t* resource) {
	const char* path = tscn_resolve_path(engine, resource->path);
	if (!path[0]) {
		return 0;
	}

	if (engine && engine->assets_ready) {
		asset_db_import(&engine->assets, resource->path);
	}

	if (resource->kind == NB_RESOURCE_KIND_MESH) {
		mesh_t mesh;
		if (!obj_load(path, &mesh)) {
			return 0;
		}

		resource->handle = resource_add_mesh(&engine->resources, &mesh);
		mesh_destroy(&mesh);

		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (resource->kind == NB_RESOURCE_KIND_TEXTURE) {
		if (!image_load(path, &resource->image)) {
			return 0;
		}

		resource->has_image = 1;

		return 1;
	}

	if (resource->kind == NB_RESOURCE_KIND_PALETTE) {
		if (!clut_load(path, &resource->palette)) {
			return 0;
		}

		resource->has_palette = 1;

		return 1;
	}

	if (resource->kind == NB_RESOURCE_KIND_MATERIAL) {
		resource->handle = material_load_tres(engine, path);

		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (resource->kind == NB_RESOURCE_KIND_SCRIPT) {
		char* source = script_load_file(path);
		if (!source) {
			return 0;
		}

		resource->handle = resource_add_script(&engine->resources, source);
		free(source);

		return resource->handle != NB_RESOURCE_INVALID_ID;
	}

	if (resource->kind == NB_RESOURCE_KIND_AUDIO) {
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

	return 0;
}

static int tscn_grow_ext(tscn_file_t* file) {
	void* ext = nb_array_grow(file->ext, &file->ext_capacity, file->ext_count + 1, sizeof(*file->ext));
	if (!ext) {
		return 0;
	}

	file->ext = ext;
	memset(&file->ext[file->ext_count], 0, sizeof(file->ext[file->ext_count]));

	return 1;
}

static int tscn_grow_node(tscn_file_t* file) {
	void* nodes = nb_array_grow(file->nodes, &file->node_capacity, file->node_count + 1, sizeof(*file->nodes));
	if (!nodes) {
		return 0;
	}

	file->nodes = nodes;

	return 1;
}

void tscn_file_init(tscn_file_t* file) {
	if (!file) {
		return;
	}

	memset(file, 0, sizeof(*file));
}

void tscn_file_free(tscn_file_t* file) {
	if (!file) {
		return;
	}

	for (int i = 0; i < file->ext_count; i++) {
		if (file->ext[i].has_image) {
			image_free(&file->ext[i].image);
		}

		if (file->ext[i].has_palette) {
			clut_free(&file->ext[i].palette);
		}
	}

	nb_array_free(file->ext);
	nb_array_free(file->nodes);

	memset(file, 0, sizeof(*file));
}

static nb_result_t tscn_parse_stream(FILE* stream, engine_t* engine, tscn_file_t* file) {
	tscn_file_init(file);

	int section_ext = 0;
	int section_node = 0;
	int scene_header = 0;
	char line[NB_TSCN_LINE_CAPACITY];

	while (fgets(line, sizeof(line), stream)) {
		char* text = tscn_trim(line);
		char* comment = strchr(text, ';');
		if (comment) {
			*comment = '\0';
			text = tscn_trim(text);
		}

		if (!text[0]) {
			continue;
		}

		if (text[0] == '[') {
			section_ext = 0;
			section_node = 0;

			if (strncmp(text, "[gd_scene", 9) == 0) {
				char format[16];
				tscn_header_value(text, "format", format, sizeof(format));
				char* format_end = NULL;
				long version = strtol(format, &format_end, 10);
				if (format_end == format || version != 3) {
					return NB_RESULT_ERROR_RESOURCE;
				}

				scene_header = 1;

				continue;
			}

			if (strncmp(text, "[ext_resource", 13) == 0) {
				if (!tscn_grow_ext(file)) {
					return NB_RESULT_ERROR_RESOURCE;
				}

				tscn_ext_t* resource = &file->ext[file->ext_count];
				tscn_header_value(text, "id", resource->id, sizeof(resource->id));
				tscn_header_value(text, "type", resource->type, sizeof(resource->type));
				tscn_header_value(text, "uuid", resource->uuid, sizeof(resource->uuid));
				tscn_header_value(text, "path", resource->path, sizeof(resource->path));
				resource->kind = tscn_kind_from_type(resource->type);

				if (!resource->path[0] && resource->uuid[0] && engine && engine->assets_ready) {
					char relative[NB_ASSET_DB_PATH_MAX];
					if (asset_db_path_for_uuid(&engine->assets, resource->uuid, relative, sizeof(relative))) {
						tscn_set_resource_path(resource->path, sizeof(resource->path), relative);
					}
				}

				if (resource->path[0] && resource->uuid[0] && engine && engine->assets_ready) {
					const char* resolved = tscn_resolve_path(engine, resource->path);
					if (!resolved[0]) {
						char relative[NB_ASSET_DB_PATH_MAX];
						if (asset_db_path_for_uuid(&engine->assets, resource->uuid, relative, sizeof(relative))) {
							tscn_set_resource_path(resource->path, sizeof(resource->path), relative);
						}
					}
				}

				if (!tscn_load_ext_resource(engine, resource)) {
					engine_log(engine, "tscn ext load failed type=%s path=%s", resource->type, resource->path);
					return NB_RESULT_ERROR_RESOURCE;
				}

				file->ext_count++;
				section_ext = 1;

				continue;
			}

			if (strncmp(text, "[node", 5) == 0) {
				if (!tscn_grow_node(file)) {
					return NB_RESULT_ERROR_RESOURCE;
				}

				tscn_node_t* node = &file->nodes[file->node_count];
				tscn_init_node(node);
				tscn_header_value(text, "name", node->name, sizeof(node->name));
				tscn_header_value(text, "type", node->type, sizeof(node->type));
				tscn_header_value(text, "parent", node->parent, sizeof(node->parent));
				file->node_count++;
				section_node = 1;
			}

			continue;
		}

		if (!section_node) {
			continue;
		}

		char* key = NULL;
		char* value = NULL;
		if (!tscn_split_property(text, &key, &value)) {
			continue;
		}

		tscn_node_t* node = &file->nodes[file->node_count - 1];

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
			node->has_mesh_ref = 1;
			tscn_parse_reference(value, node->mesh_ref, sizeof(node->mesh_ref));
		}

		else if (strcmp(key, "material") == 0) {
			node->has_material_ref = 1;
			tscn_parse_reference(value, node->material_ref, sizeof(node->material_ref));
		}

		else if (strcmp(key, "script") == 0) {
			node->has_script_ref = 1;
			tscn_parse_reference(value, node->script_ref, sizeof(node->script_ref));
		}

		else if (strcmp(key, "collider_shape") == 0) {
			char shape[32];
			tscn_parse_value(value, shape, sizeof(shape));
			if (strcmp(shape, "sphere") == 0) {
				node->collider.shape = NB_COLLIDER_SPHERE;
			}

			else if (strcmp(shape, "aabb") == 0) {
				node->collider.shape = NB_COLLIDER_AABB;
			}

			node->has_collider = 1;
		}

		else if (strcmp(key, "collider_extents") == 0) {
			if (tscn_parse_vector3(value, &node->collider.extents)) {
				node->has_collider = 1;
			}
		}

		else if (strcmp(key, "collider_radius") == 0) {
			node->collider.radius = strtof(value, NULL);
			node->has_collider = node->collider.radius > 0.0f;
		}

		else if (strcmp(key, "fov") == 0) {
			node->camera.fov = strtof(value, NULL);
		}

		else if (strcmp(key, "near") == 0) {
			node->camera.near_plane = strtof(value, NULL);
		}

		else if (strcmp(key, "far") == 0) {
			node->camera.far_plane = strtof(value, NULL);
		}

		else if (strcmp(key, "target") == 0) {
			tscn_parse_vector3(value, &node->camera.target);
		}

		else if (strcmp(key, "up") == 0) {
			tscn_parse_vector3(value, &node->camera.up);
		}

		else if (strcmp(key, "direction") == 0) {
			vec3_t direction;
			if (tscn_parse_vector3(value, &direction)) {
				node->light.direction = vec3_normalize(direction);
			}
		}

		else if (strcmp(key, "intensity") == 0) {
			node->light.intensity = strtof(value, NULL);
		}

		else if (strcmp(key, "spin_speed") == 0) {
			node->transform.spin_speed = strtof(value, NULL);
			node->has_transform = 1;
		}

		else if (strcmp(key, "visible") == 0) {
			node->visible = strcmp(value, "false") != 0 && strcmp(value, "0") != 0;
		}

		else if (strcmp(key, "tint_enabled") == 0) {
			node->tint_enabled = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
		}

		else if (strcmp(key, "tint") == 0) {
			if (tscn_parse_vector3(value, &node->tint)) {
				node->tint_enabled = 1;
			}
		}

		(void)section_ext;
	}

	if (!scene_header) {
		return NB_RESULT_ERROR_RESOURCE;
	}

	return NB_RESULT_OK;
}

static int tscn_resolve_ref(tscn_file_t* file, const char* id, nb_resource_ref_t* ref, nb_resource_kind_t kind) {
	if (!ref || kind == NB_RESOURCE_KIND_NONE) {
		return 0;
	}

	memset(ref, 0, sizeof(*ref));

	if (!id[0]) {
		return 0;
	}

	const tscn_ext_t* ext = tscn_file_find_ext_by_id(file, id);
	if (!ext || ext->kind != kind || ext->handle == NB_RESOURCE_INVALID_ID) {
		return 0;
	}

	ref->kind = kind;
	ref->handle = ext->handle;

	return 1;
}

static nb_result_t tscn_file_resolve_refs(tscn_file_t* file, engine_t* engine) {
	for (int i = 0; i < file->node_count; i++) {
		tscn_node_t* node = &file->nodes[i];

		if (node->has_mesh_ref && !tscn_resolve_ref(file, node->mesh_ref, &node->mesh, NB_RESOURCE_KIND_MESH)) {
			engine_log(engine, "tscn mesh ref failed at node=%s ref=%s", node->name, node->mesh_ref);
			return NB_RESULT_ERROR_RESOURCE;
		}

		if (node->has_material_ref && !tscn_resolve_ref(file, node->material_ref, &node->material, NB_RESOURCE_KIND_MATERIAL)) {
			engine_log(engine, "tscn material ref failed at node=%s ref=%s", node->name, node->material_ref);
			return NB_RESULT_ERROR_RESOURCE;
		}

		if (node->has_script_ref && !tscn_resolve_ref(file, node->script_ref, &node->script, NB_RESOURCE_KIND_SCRIPT)) {
			engine_log(engine, "tscn script ref failed at node=%s ref=%s", node->name, node->script_ref);
			return NB_RESULT_ERROR_RESOURCE;
		}
	}

	return NB_RESULT_OK;
}

nb_result_t tscn_file_load(tscn_file_t* file, const char* path, engine_t* engine) {
	if (!file || !path || !engine) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	FILE* stream = fopen(path, "r");
	if (!stream) {
		return NB_RESULT_ERROR_RESOURCE;
	}

	nb_result_t result = tscn_parse_stream(stream, engine, file);
	fclose(stream);

	if (result == NB_RESULT_OK) {
		result = tscn_file_resolve_refs(file, engine);
	}

	if (result != NB_RESULT_OK) {
		tscn_file_free(file);
	}

	return result;
}

nb_result_t tscn_file_load_from_buffer(tscn_file_t* file, const char* path, const char* data, size_t size, engine_t* engine) {
	if (!file || !path || !data || !engine) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	FILE* stream = fmemopen((void*)data, size, "r");
	if (!stream) {
		return NB_RESULT_ERROR_RESOURCE;
	}

	nb_result_t result = tscn_parse_stream(stream, engine, file);
	fclose(stream);

	if (result == NB_RESULT_OK) {
		result = tscn_file_resolve_refs(file, engine);
	}

	if (result != NB_RESULT_OK) {
		tscn_file_free(file);
	}

	return result;
}

nb_result_t tscn_file_build_scene(const tscn_file_t* file, engine_t* engine, scene_t* scene) {
	if (!file || !engine || !scene) {
		return NB_RESULT_ERROR_INVALID_ARGUMENT;
	}

	scene_init(scene);

	for (int i = 0; i < file->node_count; i++) {
		const tscn_node_t* node = &file->nodes[i];
		entity_id_t id = scene_create_entity(scene);
		if (id == NB_ENTITY_INVALID) {
			engine_log(engine, "tscn entity overflow at node=%s", node->name);
			return NB_RESULT_ERROR_SCENE;
		}

		if (!scene_set_node_name(scene, id, node->name)) {
			engine_log(engine, "tscn name failed at node=%s", node->name);
			return NB_RESULT_ERROR_SCENE;
		}

		scene_set_node_visible(scene, id, node->visible);

		if (node->has_script_ref) {
			if (node->script.kind != NB_RESOURCE_KIND_SCRIPT || node->script.handle == NB_RESOURCE_INVALID_ID || !scene_add_script(scene, id, node->script.handle, node->name)) {
				engine_log(engine, "tscn script failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}
		}

		if (node->has_collider && !scene_add_collider(scene, id, node->collider)) {
			engine_log(engine, "tscn collider failed at node=%s", node->name);
			return NB_RESULT_ERROR_SCENE;
		}

		if (node->tint_enabled) {
			scene_set_node_tint(scene, id, node->tint);
		}

		if (strcmp(node->type, "Camera3D") == 0) {
			if (node->has_transform && !scene_add_transform(scene, id, node->transform)) {
				engine_log(engine, "tscn transform failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}

			camera_t camera = node->camera;
			if (node->has_transform) {
				camera.eye = node->transform.position;
			}

			if (!scene_add_camera(scene, id, camera)) {
				engine_log(engine, "tscn camera failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}

			continue;
		}

		if (strcmp(node->type, "MeshInstance3D") == 0) {
			if (!node->has_mesh_ref || !node->has_material_ref || node->mesh.kind != NB_RESOURCE_KIND_MESH || node->material.kind != NB_RESOURCE_KIND_MATERIAL || node->mesh.handle == NB_RESOURCE_INVALID_ID || node->material.handle == NB_RESOURCE_INVALID_ID) {
				engine_log(engine, "tscn mesh resources failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}

			mesh_renderer_t renderer;
			renderer.mesh = node->mesh.handle;
			renderer.material = node->material.handle;

			if (!scene_add_transform(scene, id, node->transform) || !scene_add_mesh_renderer(scene, id, renderer)) {
				engine_log(engine, "tscn mesh component failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}

			continue;
		}

		if (strcmp(node->type, "DirectionalLight3D") == 0) {
			if (!scene_add_light(scene, id, node->light)) {
				engine_log(engine, "tscn light failed at node=%s", node->name);
				return NB_RESULT_ERROR_SCENE;
			}

			continue;
		}

		if (node->has_transform && strcmp(node->type, "Node3D") == 0 && !scene_add_transform(scene, id, node->transform)) {
			engine_log(engine, "tscn transform failed at node=%s", node->name);
			return NB_RESULT_ERROR_SCENE;
		}
	}

	for (int i = 0; i < file->node_count; i++) {
		const tscn_node_t* node = &file->nodes[i];
		if (!node->parent[0] || strcmp(node->parent, ".") == 0) {
			continue;
		}

		entity_id_t child = scene_find_node(scene, node->name);
		entity_id_t parent = scene_find_node(scene, node->parent);
		if (child == NB_ENTITY_INVALID || parent == NB_ENTITY_INVALID || !scene_set_node_parent(scene, child, parent)) {
			engine_log(engine, "tscn parent failed at node=%s parent=%s", node->name, node->parent);
			return NB_RESULT_ERROR_SCENE;
		}
	}

	scene_update_world_transforms(scene);
	scene_rebuild_cache(scene);

	return NB_RESULT_OK;
}
