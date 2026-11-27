#include "scene/scene.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static node_t* scene_get_node_internal(scene_t* scene, entity_id_t id) {
	if (!scene || id == NB_ENTITY_INVALID) {
		return NULL;
	}

	for (int i = 0; i < scene->node_count; i++) {
		if (scene->nodes[i].id == id) {
			return &scene->nodes[i];
		}
	}

	return NULL;
}

static int scene_find_component(const entity_id_t* ids, int count, entity_id_t id) {
	for (int i = 0; i < count; i++) {
		if (ids[i] == id) {
			return i;
		}
	}

	return -1;
}

static void scene_node_local_matrix(const node_t* node, mat4_t* out) {
	mat4_t translation;
	mat4_t rotate_y;
	mat4_t rotate_x;
	mat4_t rotate_z;
	mat4_t scale;

	mat4_translate(&translation, node->transform.position.x, node->transform.position.y, node->transform.position.z);
	mat4_rotate_y(&rotate_y, node->transform.rotation.y);
	mat4_rotate_x(&rotate_x, node->transform.rotation.x);
	mat4_rotate_z(&rotate_z, node->transform.rotation.z);
	mat4_identity(&scale);
	scale.m[0] = node->transform.scale.x;
	scale.m[5] = node->transform.scale.y;
	scale.m[10] = node->transform.scale.z;

	mat4_multiply(out, &translation, &rotate_y);
	mat4_multiply(out, out, &rotate_x);
	mat4_multiply(out, out, &rotate_z);
	mat4_multiply(out, out, &scale);
}

static void scene_detach_parent(scene_t* scene, node_t* node) {
	node_t* parent = scene_get_node_internal(scene, node->parent);
	if (!parent) {
		return;
	}

	for (int i = 0; i < parent->child_count; i++) {
		if (parent->children[i] != node->id) {
			continue;
		}

		for (int j = i + 1; j < parent->child_count; j++) {
			parent->children[j - 1] = parent->children[j];
		}

		parent->child_count--;
		break;
	}
}

void scene_init(scene_t* scene) {
	if (!scene) {
		return;
	}

	memset(scene, 0, sizeof(*scene));
}

void scene_shutdown(scene_t* scene) {
	if (!scene) {
		return;
	}

	memset(scene, 0, sizeof(*scene));
}

entity_id_t scene_create_entity(scene_t* scene) {
	if (!scene || scene->node_count >= NB_SCENE_MAX_ENTITIES || scene->entity_count >= NB_SCENE_MAX_ENTITIES) {
		return NB_ENTITY_INVALID;
	}

	entity_id_t id = (entity_id_t)(scene->entity_count + 1);
	node_t* node = &scene->nodes[scene->node_count];
	memset(node, 0, sizeof(*node));

	node->id = id;
	node->parent = NB_ENTITY_INVALID;
	node->transform.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
	node->visible = 1;
	node->world_visible = 1;
	node->tint_enabled = 0;
	node->tint = (vec3_t){ 1.0f, 1.0f, 1.0f };
	snprintf(node->name, sizeof(node->name), "Node%d", id);
	mat4_identity(&node->world_matrix);
	mat4_identity(&node->previous_world_matrix);
	scene->node_count++;

	scene->ids[scene->entity_count] = id;
	scene->entity_count++;

	return id;
}

node_t* scene_get_node(scene_t* scene, entity_id_t id) {
	return scene_get_node_internal(scene, id);
}

entity_id_t scene_find_node(scene_t* scene, const char* name) {
	if (!scene || !name || !*name) {
		return NB_ENTITY_INVALID;
	}

	for (int i = 0; i < scene->node_count; i++) {
		if (strcmp(scene->nodes[i].name, name) == 0) {
			return scene->nodes[i].id;
		}
	}

	return NB_ENTITY_INVALID;
}

entity_id_t scene_find_node_path(scene_t* scene, const char* path) {
	if (!scene || !path || !*path) {
		return NB_ENTITY_INVALID;
	}

	char buffer[256];
	if (strlen(path) >= sizeof(buffer)) {
		return NB_ENTITY_INVALID;
	}

	strcpy(buffer, path);
	char* token = strtok(buffer, "/");
	if (!token) {
		return NB_ENTITY_INVALID;
	}

	entity_id_t id = scene_find_node(scene, token);
	if (id == NB_ENTITY_INVALID) {
		return NB_ENTITY_INVALID;
	}

	token = strtok(NULL, "/");
	while (token) {
		node_t* node = scene_get_node_internal(scene, id);
		if (!node) {
			return NB_ENTITY_INVALID;
		}

		entity_id_t child = NB_ENTITY_INVALID;
		for (int i = 0; i < node->child_count; i++) {
			node_t* candidate = scene_get_node_internal(scene, node->children[i]);
			if (candidate && strcmp(candidate->name, token) == 0) {
				child = candidate->id;
				break;
			}
		}

		if (child == NB_ENTITY_INVALID) {
			return NB_ENTITY_INVALID;
		}

		id = child;
		token = strtok(NULL, "/");
	}

	return id;
}

int scene_set_node_name(scene_t* scene, entity_id_t id, const char* name) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || !name || !*name || strlen(name) >= sizeof(node->name)) {
		return 0;
	}

	if (scene_find_node(scene, name) != NB_ENTITY_INVALID) {
		return 0;
	}

	snprintf(node->name, sizeof(node->name), "%s", name);
	return 1;
}

int scene_set_node_parent(scene_t* scene, entity_id_t id, entity_id_t parent) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || parent == id) {
		return 0;
	}

	entity_id_t current = parent;
	while (current != NB_ENTITY_INVALID) {
		if (current == id) {
			return 0;
		}

		node_t* ancestor = scene_get_node_internal(scene, current);
		if (!ancestor) {
			return 0;
		}

		current = ancestor->parent;
	}

	scene_detach_parent(scene, node);
	node->parent = NB_ENTITY_INVALID;

	if (parent == NB_ENTITY_INVALID) {
		return 1;
	}

	node_t* parent_node = scene_get_node_internal(scene, parent);
	if (!parent_node || parent_node->child_count >= NB_NODE_MAX_CHILDREN) {
		return 0;
	}

	parent_node->children[parent_node->child_count++] = id;
	node->parent = parent;

	return 1;
}

int scene_is_ancestor(scene_t* scene, entity_id_t ancestor, entity_id_t id) {
	if (!scene || ancestor == NB_ENTITY_INVALID || id == NB_ENTITY_INVALID || ancestor == id) {
		return 0;
	}

	node_t* node = scene_get_node_internal(scene, id);
	for (int depth = 0; node && depth < NB_SCENE_MAX_ENTITIES; depth++) {
		if (node->parent == ancestor) {
			return 1;
		}
		node = scene_get_node_internal(scene, node->parent);
	}

	return 0;
}

int scene_move_node(scene_t* scene, entity_id_t id, entity_id_t parent, int index) {
	if (!scene) { return 0; }
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || id == parent) { return 0; }

	node_t* parent_node = NULL;
	if (parent != NB_ENTITY_INVALID) {
		parent_node = scene_get_node_internal(scene, parent);
		if (!parent_node) { return 0; }
	}
	if (scene_is_ancestor(scene, id, parent)) { return 0; }

	entity_id_t old_parent = node->parent;
	int old_index = -1;
	node_t* old_parent_node = scene_get_node_internal(scene, old_parent);
	if (old_parent_node) {
		for (int i = 0; i < old_parent_node->child_count; i++) {
			if (old_parent_node->children[i] == id) {
				old_index = i;
				break;
			}
		}
	}

	int target_count = parent_node ? parent_node->child_count : 0;
	if (old_parent == parent && old_index >= 0) { target_count--; }
	if (target_count >= NB_NODE_MAX_CHILDREN) { return 0; }

	scene_detach_parent(scene, node);
	node->parent = NB_ENTITY_INVALID;
	if (!parent_node) { return 1; }

	if (index < 0 || index > target_count) { index = target_count; }
	for (int i = target_count; i > index; i--) {
		parent_node->children[i] = parent_node->children[i - 1];
	}
	parent_node->children[index] = id;
	parent_node->child_count++;
	node->parent = parent;

	return 1;
}

int scene_set_node_visible(scene_t* scene, entity_id_t id, int visible) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	node->visible = visible ? 1 : 0;

	return 1;
}

int scene_node_visible(scene_t* scene, entity_id_t id) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	return node->world_visible;
}

int scene_set_node_tint(scene_t* scene, entity_id_t id, vec3_t tint) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	node->tint = tint;
	node->tint_enabled = 1;

	return 1;
}

void scene_update_world_transforms(scene_t* scene) {
	if (!scene) {
		return;
	}

	unsigned char updated[NB_SCENE_MAX_ENTITIES];
	memset(updated, 0, sizeof(updated));

	for (int pass = 0; pass < scene->node_count; pass++) {
		for (int i = 0; i < scene->node_count; i++) {
			if (updated[i]) {
				continue;
			}

			node_t* node = &scene->nodes[i];
			int parent_ready = node->parent == NB_ENTITY_INVALID;

			if (!parent_ready) {
				for (int j = 0; j < scene->node_count; j++) {
					if (scene->nodes[j].id == node->parent && updated[j]) {
						parent_ready = 1;
						break;
					}
				}
			}

			if (!parent_ready) {
				continue;
			}

			mat4_t local;
			scene_node_local_matrix(node, &local);
			node->previous_world_matrix = node->world_matrix;
			node->world_visible = node->visible;

			if (node->parent == NB_ENTITY_INVALID) {
				node->world_matrix = local;
			}

			else {
				node_t* parent = scene_get_node_internal(scene, node->parent);
				if (!parent) {
					node->world_matrix = local;
				}

				else {
					mat4_multiply(&node->world_matrix, &parent->world_matrix, &local);
					if (!parent->world_visible) {
						node->world_visible = 0;
					}
				}
			}

			node->world_position = (vec3_t){ node->world_matrix.m[12], node->world_matrix.m[13], node->world_matrix.m[14] };
			if ((node->components & NB_NODE_COMPONENT_CAMERA) && (node->components & NB_NODE_COMPONENT_TRANSFORM)) {
				node->camera.eye = node->world_position;
			}
			updated[i] = 1;
		}
	}
}

void scene_rebuild_cache(scene_t* scene) {
	if (!scene) {
		return;
	}

	scene->transform_count = 0;
	scene->mesh_renderer_count = 0;
	scene->camera_count = 0;
	scene->light_count = 0;
	scene->collider_count = 0;

	for (int i = 0; i < scene->node_count; i++) {
		node_t* node = &scene->nodes[i];

		if (node->components & NB_NODE_COMPONENT_TRANSFORM) {
			scene->transforms[scene->transform_count] = node->transform;
			scene->transform_entities[scene->transform_count] = node->id;
			scene->transform_count++;
		}

		if ((node->components & NB_NODE_COMPONENT_MESH_RENDERER) && node->world_visible) {
			scene->mesh_renderers[scene->mesh_renderer_count] = node->mesh_renderer;
			scene->mesh_renderer_entities[scene->mesh_renderer_count] = node->id;
			scene->mesh_renderer_count++;
		}

		if (node->components & NB_NODE_COMPONENT_CAMERA) {
			camera_t camera = node->camera;
			if (node->components & NB_NODE_COMPONENT_TRANSFORM) {
				camera.eye = node->world_position;
			}

			scene->cameras[scene->camera_count] = camera;
			scene->camera_entities[scene->camera_count] = node->id;
			scene->camera_count++;
		}

		if (node->components & NB_NODE_COMPONENT_LIGHT) {
			scene->lights[scene->light_count] = node->light;
			scene->light_entities[scene->light_count] = node->id;
			scene->light_count++;
		}

		if ((node->components & NB_NODE_COMPONENT_COLLIDER) && node->world_visible) {
			scene->colliders[scene->collider_count] = node->collider;
			scene->collider_entities[scene->collider_count] = node->id;
			scene->collider_count++;
		}
	}

	if (scene->camera_count == 0) {
		scene->active_camera = NB_ENTITY_INVALID;
	}

	else if (scene_find_component(scene->camera_entities, scene->camera_count, scene->active_camera) < 0) {
		scene->active_camera = scene->camera_entities[0];
	}
}

int scene_add_transform(scene_t* scene, entity_id_t id, transform_t transform) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	transform.previous_rotation = transform.rotation;
	transform.initial_rotation = transform.rotation;
	node->transform = transform;
	node->components |= NB_NODE_COMPONENT_TRANSFORM;
	scene_rebuild_cache(scene);

	return 1;
}

int scene_add_mesh_renderer(scene_t* scene, entity_id_t id, mesh_renderer_t renderer) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	node->mesh_renderer = renderer;
	node->components |= NB_NODE_COMPONENT_MESH_RENDERER;
	scene_rebuild_cache(scene);

	return 1;
}

int scene_add_camera(scene_t* scene, entity_id_t id, camera_t camera) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	node->camera = camera;
	node->components |= NB_NODE_COMPONENT_CAMERA;
	scene_rebuild_cache(scene);

	return 1;
}

int scene_add_light(scene_t* scene, entity_id_t id, light_t light) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) {
		return 0;
	}

	node->light = light;
	node->components |= NB_NODE_COMPONENT_LIGHT;
	scene_rebuild_cache(scene);

	return 1;
}

int scene_add_collider(scene_t* scene, entity_id_t id, collider_t collider) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || collider.shape >= NB_COLLIDER_SHAPE_COUNT) {
		return 0;
	}

	if (collider.shape == NB_COLLIDER_SPHERE && collider.radius <= 0.0f) {
		return 0;
	}

	if (collider.shape == NB_COLLIDER_AABB && (collider.extents.x <= 0.0f || collider.extents.y <= 0.0f || collider.extents.z <= 0.0f)) {
		return 0;
	}

	node->collider = collider;
	node->components |= NB_NODE_COMPONENT_COLLIDER;
	scene_rebuild_cache(scene);

	return 1;
}

int scene_add_script(scene_t* scene, entity_id_t id, script_id script, const char* module) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || script == NB_RESOURCE_INVALID_ID || !module || !*module) {
		return 0;
	}

	node->script = script;
	snprintf(node->script_module, sizeof(node->script_module), "%s", module);
	node->components |= NB_NODE_COMPONENT_SCRIPT;

	return 1;
}

int scene_remove_entity(scene_t* scene, entity_id_t id) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node) { return 0; }

	while (node->child_count > 0) {
		entity_id_t child = node->children[0];
		if (!scene_remove_entity(scene, child)) { return 0; }
		node = scene_get_node_internal(scene, id);
		if (!node) { return 1; }
	}

	scene_detach_parent(scene, node);

	int idx = -1;
	for (int i = 0; i < scene->node_count; i++) {
		if (scene->nodes[i].id == id) {
			idx = i;
			break;
		}
	}
	if (idx < 0) { return 0; }

	for (int i = idx + 1; i < scene->node_count; i++) {
		scene->nodes[i - 1] = scene->nodes[i];
	}
	scene->node_count--;

	for (int i = 0; i < scene->entity_count; i++) {
		if (scene->ids[i] == id) {
			for (int j = i + 1; j < scene->entity_count; j++) {
				scene->ids[j - 1] = scene->ids[j];
			}
			scene->entity_count--;
			break;
		}
	}

	if (scene->active_camera == id) { scene->active_camera = NB_ENTITY_INVALID; }
	scene_rebuild_cache(scene);
	return 1;
}

int scene_duplicate_entity(scene_t* scene, entity_id_t id) {
	node_t* src = scene_get_node_internal(scene, id);
	if (!src) { return 0; }

	entity_id_t nid = scene_create_entity(scene);
	if (nid == NB_ENTITY_INVALID) { return 0; }
	node_t* dst = scene_get_node_internal(scene, nid);
	if (!dst) { return 0; }

	entity_id_t new_id = dst->id;
	entity_id_t new_parent = src->parent;
	int new_child_count = 0;
	memcpy(dst, src, sizeof(*dst));
	dst->id = new_id;
	dst->parent = NB_ENTITY_INVALID;
	dst->child_count = 0;
	for (int i = 0; i < NB_NODE_MAX_CHILDREN; i++) { dst->children[i] = NB_ENTITY_INVALID; }
	dst->script_instance = NULL;
	snprintf(dst->name, sizeof(dst->name), "%s_copy", src->name);
	for (int i = 0; i < scene->node_count; i++) {
		if (scene->nodes[i].id != nid && strcmp(scene->nodes[i].name, dst->name) == 0) {
			snprintf(dst->name, sizeof(dst->name), "%s_%u", src->name, (unsigned int)nid);
			break;
		}
	}

	if (new_parent != NB_ENTITY_INVALID) {
		scene_set_node_parent(scene, nid, new_parent);
	}

	scene_rebuild_cache(scene);
	(void)new_child_count;
	return 1;
}

int scene_set_active_camera(scene_t* scene, entity_id_t id) {
	node_t* node = scene_get_node_internal(scene, id);
	if (!node || !(node->components & NB_NODE_COMPONENT_CAMERA)) {
		return 0;
	}

	scene->active_camera = id;

	return 1;
}

transform_t* scene_get_transform(scene_t* scene, entity_id_t id) {
	if (!scene) {
		return NULL;
	}

	int index = scene_find_component(scene->transform_entities, scene->transform_count, id);
	if (index < 0) {
		return NULL;
	}

	return &scene->transforms[index];
}

mesh_renderer_t* scene_get_mesh_renderer(scene_t* scene, entity_id_t id) {
	if (!scene) {
		return NULL;
	}

	int index = scene_find_component(scene->mesh_renderer_entities, scene->mesh_renderer_count, id);
	if (index < 0) {
		return NULL;
	}

	return &scene->mesh_renderers[index];
}

camera_t* scene_get_camera(scene_t* scene, entity_id_t id) {
	if (!scene) {
		return NULL;
	}

	int index = scene_find_component(scene->camera_entities, scene->camera_count, id);
	if (index < 0) {
		return NULL;
	}

	return &scene->cameras[index];
}

camera_t* scene_get_active_camera(scene_t* scene) {
	if (!scene || scene->active_camera == NB_ENTITY_INVALID) {
		return NULL;
	}

	int index = scene_find_component(scene->camera_entities, scene->camera_count, scene->active_camera);
	if (index < 0) {
		return NULL;
	}

	return &scene->cameras[index];
}

light_t* scene_get_light(scene_t* scene, entity_id_t id) {
	if (!scene) {
		return NULL;
	}

	int index = scene_find_component(scene->light_entities, scene->light_count, id);
	if (index < 0) {
		return NULL;
	}

	return &scene->lights[index];
}

collider_t* scene_get_collider(scene_t* scene, entity_id_t id) {
	if (!scene) {
		return NULL;
	}

	int index = scene_find_component(scene->collider_entities, scene->collider_count, id);
	if (index < 0) {
		return NULL;
	}

	return &scene->colliders[index];
}
