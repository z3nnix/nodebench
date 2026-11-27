#include <stdio.h>
#include <string.h>

#include "render/draw_list.h"
#include "render/material.h"
#include "render/mesh.h"
#include "resource/resource.h"
#include "scene/scene.h"

#define NB_STRESS_COUNT 1200

static int dynamic_test_resources(mesh_id* first_mesh, script_id* first_script) {
	resource_store_t store;
	resource_store_init(&store, 1048576);

	vertex_t vertices[3] = {
		{ { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0xffffu },
		{ { 1.0f, 0.0f, 0.0f }, 1.0f, 0.0f, 0xffffu },
		{ { 0.0f, 1.0f, 0.0f }, 0.0f, 1.0f, 0xffffu }
	};
	triangle_t triangles[1] = { { 0, 1, 2 } };
	mesh_t mesh;
	mesh_init(&mesh, vertices, 3, triangles, 1);

	*first_mesh = NB_RESOURCE_INVALID_ID;
	*first_script = NB_RESOURCE_INVALID_ID;

	for (int i = 0; i < NB_STRESS_COUNT; i++) {
		mesh_id mesh_ref = resource_add_mesh(&store, &mesh);
		script_id script_ref = resource_add_script(&store, "class Stress { update() {} }");
		if (mesh_ref == NB_RESOURCE_INVALID_ID || script_ref == NB_RESOURCE_INVALID_ID) {
			resource_store_shutdown(&store);
			return 0;
		}

		if (i == 0) {
			*first_mesh = mesh_ref;
			*first_script = script_ref;
		}
	}

	int ok = resource_get_mesh(&store, *first_mesh) != NULL && resource_get_script(&store, *first_script) != NULL;
	if (resource_get_mesh(&store, 0xffff0000u) != NULL) {
		ok = 0;
	}
	resource_store_shutdown(&store);

	return ok;
}

static int dynamic_test_entity_ids(void) {
	scene_t scene;
	scene_init(&scene);

	entity_id_t first = scene_create_entity(&scene);
	entity_id_t removed = scene_create_entity(&scene);
	entity_id_t last = scene_create_entity(&scene);
	int ok = first != NB_ENTITY_INVALID && removed != NB_ENTITY_INVALID && last != NB_ENTITY_INVALID;
	if (ok) {
		ok = scene_remove_entity(&scene, removed);
	}
	entity_id_t next = scene_create_entity(&scene);
	if (ok) {
		ok = next != NB_ENTITY_INVALID && next != removed && next != first && next != last;
		ok = ok && scene_get_node(&scene, next) != NULL && scene_get_node(&scene, removed) == NULL;
	}

	scene_shutdown(&scene);

	return ok;
}

static int dynamic_test_scene(mesh_id mesh) {
	scene_t scene;
	scene_init(&scene);

	entity_id_t first = NB_ENTITY_INVALID;
	entity_id_t first_camera = NB_ENTITY_INVALID;

	for (int i = 0; i < NB_STRESS_COUNT; i++) {
		entity_id_t id = scene_create_entity(&scene);
		if (id == NB_ENTITY_INVALID) {
			scene_shutdown(&scene);
			return 0;
		}

		if (i == 0) {
			first = id;
		}

		transform_t transform;
		memset(&transform, 0, sizeof(transform));
		transform.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
		transform.position = (vec3_t){ (float)i, 0.0f, 0.0f };
		if (!scene_add_transform(&scene, id, transform)) {
			scene_shutdown(&scene);
			return 0;
		}

		mesh_renderer_t renderer;
		renderer.mesh = mesh;
		renderer.material = NB_RESOURCE_INVALID_ID;
		if (!scene_add_mesh_renderer(&scene, id, renderer)) {
			scene_shutdown(&scene);
			return 0;
		}

		if (i % 100 == 0) {
			camera_t camera;
			memset(&camera, 0, sizeof(camera));
			camera.fov = 60.0f;
			camera.near_plane = 0.1f;
			camera.far_plane = 100.0f;
			if (!scene_add_camera(&scene, id, camera)) {
				scene_shutdown(&scene);
				return 0;
			}

			if (first_camera == NB_ENTITY_INVALID) {
				first_camera = id;
			}
		}
	}

	scene_update_world_transforms(&scene);
	if (!scene_rebuild_cache(&scene)) {
		scene_shutdown(&scene);
		return 0;
	}

	node_t* first_node = scene_get_node(&scene, first);
	int ok = first_node != NULL && scene.node_count == NB_STRESS_COUNT && scene.transform_count == NB_STRESS_COUNT && scene.mesh_renderer_count == NB_STRESS_COUNT;
	if (ok && first_camera != NB_ENTITY_INVALID && scene_set_active_camera(&scene, first_camera)) {
		ok = scene_get_active_camera(&scene) != NULL;
	}

	scene_t copy;
	scene_init(&copy);
	if (ok && !scene_copy(&copy, &scene)) {
		ok = 0;
	}

	if (ok && (copy.node_count != scene.node_count || !scene_get_node(&copy, first))) {
		ok = 0;
	}

	scene_shutdown(&copy);
	scene_shutdown(&scene);

	return ok;
}

static int dynamic_test_draw_list(void) {
	draw_list_t list;
	draw_list_init(&list);

	vertex_t vertices[3] = {
		{ { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, 0xffffu },
		{ { 1.0f, 0.0f, 0.0f }, 1.0f, 0.0f, 0xffffu },
		{ { 0.0f, 1.0f, 0.0f }, 0.0f, 1.0f, 0xffffu }
	};
	triangle_t triangles[1] = { { 0, 1, 2 } };
	mesh_t mesh;
	mesh_init(&mesh, vertices, 3, triangles, 1);

	material_t material;
	material_init(&material, NB_RESOURCE_INVALID_ID, NB_BLEND_NONE, NB_SHADE_FLAT, 0, (vec3_t){ 1.0f, 1.0f, 1.0f });

	mat4_t model;
	mat4_identity(&model);

	for (int i = 0; i < NB_STRESS_COUNT; i++) {
		model.m[12] = (float)i;
		if (!draw_list_push(&list, &mesh, NULL, &material, &model)) {
			draw_list_free(&list);
			return 0;
		}
	}

	draw_list_sort(&list);
	int ok = list.count == NB_STRESS_COUNT && list.capacity >= NB_STRESS_COUNT && list.commands != NULL;
	draw_list_free(&list);

	return ok;
}

int main(void) {
	mesh_id first_mesh = NB_RESOURCE_INVALID_ID;
	script_id first_script = NB_RESOURCE_INVALID_ID;

	if (!dynamic_test_resources(&first_mesh, &first_script)) {
		printf("fail stress resources\n");
		return 1;
	}

	if (!dynamic_test_scene(first_mesh)) {
		printf("fail stress scene\n");
		return 1;
	}

	if (!dynamic_test_draw_list()) {
		printf("fail stress draw list\n");
		return 1;
	}

	if (!dynamic_test_entity_ids()) {
		printf("fail entity id reuse\n");
		return 1;
	}

	printf("dynamic stress test ok: %d entities, %d resources\n", NB_STRESS_COUNT, NB_STRESS_COUNT * 2);
	return 0;
}
