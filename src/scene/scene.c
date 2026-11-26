#include "scene/scene.h"

#include <stddef.h>
#include <string.h>

static int scene_entity_exists(const scene_t* scene, entity_id_t id) {
	if (!scene || id == NB_ENTITY_INVALID) {
		return 0;
	}

	for (int i = 0; i < scene->entity_count; i++) {
		if (scene->ids[i] == id) {
			return 1;
		}
	}

	return 0;
}

static int scene_find_component(const entity_id_t* ids, int count, entity_id_t id) {
	for (int i = 0; i < count; i++) {
		if (ids[i] == id) {
			return i;
		}
	}

	return -1;
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
	if (!scene || scene->entity_count >= NB_SCENE_MAX_ENTITIES) {
		return NB_ENTITY_INVALID;
	}

	scene->ids[scene->entity_count] = (entity_id_t)(scene->entity_count + 1);
	scene->entity_count++;

	return scene->ids[scene->entity_count - 1];
}

int scene_add_transform(scene_t* scene, entity_id_t id, transform_t transform) {
	if (!scene_entity_exists(scene, id) || scene->transform_count >= NB_SCENE_MAX_ENTITIES) {
		return 0;
	}

	transform.previous_rotation = transform.rotation;
	transform.initial_rotation = transform.rotation;
	scene->transforms[scene->transform_count] = transform;
	scene->transform_entities[scene->transform_count] = id;
	scene->transform_count++;

	return 1;
}

int scene_add_mesh_renderer(scene_t* scene, entity_id_t id, mesh_renderer_t renderer) {
	if (!scene_entity_exists(scene, id) || scene->mesh_renderer_count >= NB_SCENE_MAX_ENTITIES) {
		return 0;
	}

	scene->mesh_renderers[scene->mesh_renderer_count] = renderer;
	scene->mesh_renderer_entities[scene->mesh_renderer_count] = id;
	scene->mesh_renderer_count++;

	return 1;
}

int scene_add_camera(scene_t* scene, entity_id_t id, camera_t camera) {
	if (!scene_entity_exists(scene, id) || scene->camera_count >= NB_SCENE_MAX_ENTITIES) {
		return 0;
	}

	scene->cameras[scene->camera_count] = camera;
	scene->camera_entities[scene->camera_count] = id;
	scene->camera_count++;

	return 1;
}

int scene_add_light(scene_t* scene, entity_id_t id, light_t light) {
	if (!scene_entity_exists(scene, id) || scene->light_count >= NB_SCENE_MAX_ENTITIES) {
		return 0;
	}

	scene->lights[scene->light_count] = light;
	scene->light_entities[scene->light_count] = id;
	scene->light_count++;

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
