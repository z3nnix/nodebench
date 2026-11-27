#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/engine.h"
#include "scene/scene_doc.h"

static const char* bad_ref_scene =
	"[gd_scene load_steps=3 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://models/cube.obj\" id=\"m\"]\n"
	"[ext_resource type=\"NBPS1Material\" path=\"res://assets/tile.tres\" id=\"mat\"]\n"
	"[node name=\"Bad\" type=\"MeshInstance3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"mesh = ExtResource(\"m\")\n"
	"material = ExtResource(\"m\")\n";

static node_t* scene_doc_test_node(scene_t* scene, const char* name) {
	entity_id_t id = scene_find_node(scene, name);
	if (id == NB_ENTITY_INVALID) {
		return NULL;
	}

	return scene_get_node(scene, id);
}

static int scene_doc_test_close(float a, float b) {
	return fabsf(a - b) < 0.0001f;
}

int main(void) {
	if (chdir("examples/kula") != 0) {
		return 1;
	}

	engine_t engine;
	memset(&engine, 0, sizeof(engine));
	if (!log_init(&engine.log, NULL, NB_LOG_INFO)) {
		return 1;
	}

	render_config_init(&engine.config);
	resource_store_init(&engine.resources, 1048576);
	if (!asset_db_init(&engine.assets, ".")) {
		return 1;
	}

	engine.assets_ready = 1;

	scene_doc_t doc;
	scene_t scene;
	scene_doc_init(&doc);
	if (scene_doc_load(&doc, "scenes/level01.tscn", &engine, &scene) != NB_RESULT_OK) {
		printf("fail load\n");
		return 1;
	}

	node_t* player = scene_doc_test_node(&scene, "Player");
	node_t* level = scene_doc_test_node(&scene, "Level");
	if (!player || !level) {
		printf("fail find node\n");
		return 1;
	}

	const scene_doc_ext_t* mat = scene_doc_find_ext(&doc, NB_RESOURCE_KIND_MATERIAL, player->mesh_renderer.material);
	const scene_doc_ext_t* script = scene_doc_find_ext(&doc, NB_RESOURCE_KIND_SCRIPT, player->script);
	const scene_doc_ext_t* level_script = scene_doc_find_ext(&doc, NB_RESOURCE_KIND_SCRIPT, level->script);
	if (!mat || strcmp(mat->id, "mat_ball") != 0 || !script || strcmp(script->id, "script_player") != 0 || !level_script || strcmp(level_script->id, "script_game") != 0) {
		printf("fail typed refs\n");
		return 1;
	}

	char* data = NULL;
	size_t size = 0;
	if (scene_doc_save_to_buffer(&doc, &scene, &data, &size) != NB_RESULT_OK || !data || size == 0) {
		printf("fail save buffer\n");
		return 1;
	}

	scene_doc_t doc2;
	scene_t scene2;
	scene_doc_init(&doc2);
	if (scene_doc_load_from_buffer(&doc2, "scenes/level01.tscn", data, size, &engine, &scene2) != NB_RESULT_OK) {
		printf("fail load buffer\n");
		free(data);
		return 1;
	}

	free(data);

	node_t* player2 = scene_doc_test_node(&scene2, "Player");
	if (!player2 || player2->components != player->components || player2->mesh_renderer.mesh == NB_RESOURCE_INVALID_ID || player2->mesh_renderer.material == NB_RESOURCE_INVALID_ID || player2->script == NB_RESOURCE_INVALID_ID) {
		printf("fail roundtrip components\n");
		return 1;
	}

	if (!scene_doc_test_close(player2->transform.position.x, player->transform.position.x) || !scene_doc_test_close(player2->transform.position.y, player->transform.position.y) || !scene_doc_test_close(player2->transform.position.z, player->transform.position.z)) {
		printf("fail roundtrip transform\n");
		return 1;
	}

	const scene_doc_ext_t* mat2 = scene_doc_find_ext(&doc2, NB_RESOURCE_KIND_MATERIAL, player2->mesh_renderer.material);
	const scene_doc_ext_t* script2 = scene_doc_find_ext(&doc2, NB_RESOURCE_KIND_SCRIPT, player2->script);
	if (!mat2 || strcmp(mat2->id, "mat_ball") != 0 || !script2 || strcmp(script2->id, "script_player") != 0) {
		printf("fail roundtrip refs\n");
		return 1;
	}

	const char* mesh_uuid = asset_db_uuid(&engine.assets, "res://models/cube.obj");
	const char* material_uuid = asset_db_uuid(&engine.assets, "res://assets/tile.tres");
	if (!mesh_uuid || !material_uuid) {
		printf("fail uuid lookup\n");
		return 1;
	}

	char uuid_scene[1024];
	int written = snprintf(uuid_scene, sizeof(uuid_scene),
		"[gd_scene load_steps=3 format=3]\n"
		"[ext_resource type=\"Mesh\" uuid=\"%s\" id=\"m\"]\n"
		"[ext_resource type=\"NBPS1Material\" uuid=\"%s\" id=\"mat\"]\n"
		"[node name=\"Uuid\" type=\"MeshInstance3D\" parent=\".\"]\n"
		"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
		"mesh = ExtResource(\"m\")\n"
		"material = ExtResource(\"mat\")\n",
		mesh_uuid, material_uuid);
	if (written < 0 || (size_t)written >= sizeof(uuid_scene)) {
		return 1;
	}

	scene_doc_t uuid_doc;
	scene_t uuid_scene_data;
	scene_doc_init(&uuid_doc);
	if (scene_doc_load_from_buffer(&uuid_doc, "uuid.tscn", uuid_scene, strlen(uuid_scene), &engine, &uuid_scene_data) != NB_RESULT_OK) {
		printf("fail uuid scene load\n");
		return 1;
	}

	node_t* uuid_node = scene_doc_test_node(&uuid_scene_data, "Uuid");
	if (!uuid_node || uuid_node->mesh_renderer.mesh == NB_RESOURCE_INVALID_ID || uuid_node->mesh_renderer.material == NB_RESOURCE_INVALID_ID) {
		printf("fail uuid scene refs\n");
		return 1;
	}

	const scene_doc_ext_t* uuid_mesh = scene_doc_find_ext(&uuid_doc, NB_RESOURCE_KIND_MESH, uuid_node->mesh_renderer.mesh);
	if (!uuid_mesh || strcmp(uuid_mesh->uuid, mesh_uuid) != 0) {
		printf("fail uuid copy\n");
		return 1;
	}

	char* uuid_data = NULL;
	size_t uuid_size = 0;
	if (scene_doc_save_to_buffer(&uuid_doc, &uuid_scene_data, &uuid_data, &uuid_size) != NB_RESULT_OK || !uuid_data) {
		printf("fail uuid save buffer\n");
		return 1;
	}
	if (!strstr(uuid_data, mesh_uuid) || !strstr(uuid_data, material_uuid)) {
		printf("fail uuid roundtrip\n");
		free(uuid_data);
		return 1;
	}
	free(uuid_data);

	node_t* camera = scene_doc_test_node(&scene, "Camera");
	node_t* sun = scene_doc_test_node(&scene, "Sun");
	node_t* player_ext = scene_doc_test_node(&scene, "Player");
	if (!camera || !sun || !player_ext) {
		printf("fail component source nodes\n");
		return 1;
	}

	camera->camera.fov = 70.0f;
	camera->camera.near_plane = 0.25f;
	camera->camera.far_plane = 250.0f;
	camera->camera.target = (vec3_t){ 1.0f, 2.0f, 3.0f };
	camera->camera.up = (vec3_t){ 0.0f, 1.0f, 0.0f };
	sun->light.direction = (vec3_t){ 0.0f, 1.0f, 0.0f };
	sun->light.intensity = 2.5f;
	scene_set_node_visible(&scene, player_ext->id, 0);
	scene_set_node_tint(&scene, player_ext->id, (vec3_t){ 0.5f, 0.25f, 0.75f });
	collider_t sphere;
	memset(&sphere, 0, sizeof(sphere));
	sphere.shape = NB_COLLIDER_SPHERE;
	sphere.radius = 0.75f;
	if (!scene_add_collider(&scene, player_ext->id, sphere)) {
		printf("fail component collider setup\n");
		return 1;
	}
	transform_t spin = level->transform;
	spin.spin_speed = 0.5f;
	if (!scene_add_transform(&scene, level->id, spin)) {
		printf("fail component spin setup\n");
		return 1;
	}

	char* component_data = NULL;
	size_t component_size = 0;
	if (scene_doc_save_to_buffer(&doc, &scene, &component_data, &component_size) != NB_RESULT_OK || !component_data) {
		printf("fail component save buffer\n");
		return 1;
	}
	scene_doc_t component_doc;
	scene_t component_scene;
	scene_doc_init(&component_doc);
	if (scene_doc_load_from_buffer(&component_doc, "components.tscn", component_data, component_size, &engine, &component_scene) != NB_RESULT_OK) {
		printf("fail component load buffer\n");
		free(component_data);
		return 1;
	}
	free(component_data);

	node_t* camera2 = scene_doc_test_node(&component_scene, "Camera");
	node_t* sun2 = scene_doc_test_node(&component_scene, "Sun");
	node_t* player3 = scene_doc_test_node(&component_scene, "Player");
	node_t* level2 = scene_doc_test_node(&component_scene, "Level");
	int component_ok = camera2 && sun2 && player3 && level2;
	if (component_ok) {
		component_ok = scene_doc_test_close(camera2->camera.fov, 70.0f) && scene_doc_test_close(camera2->camera.near_plane, 0.25f) && scene_doc_test_close(camera2->camera.far_plane, 250.0f);
		component_ok = component_ok && scene_doc_test_close(camera2->camera.target.x, 1.0f) && scene_doc_test_close(camera2->camera.target.y, 2.0f) && scene_doc_test_close(camera2->camera.target.z, 3.0f);
		component_ok = component_ok && scene_doc_test_close(sun2->light.intensity, 2.5f) && scene_doc_test_close(sun2->light.direction.y, 1.0f);
		component_ok = component_ok && !player3->visible && player3->tint_enabled;
		component_ok = component_ok && scene_doc_test_close(player3->tint.x, 0.5f) && scene_doc_test_close(player3->tint.y, 0.25f) && scene_doc_test_close(player3->tint.z, 0.75f);
		component_ok = component_ok && (player3->components & NB_NODE_COMPONENT_COLLIDER) && player3->collider.shape == NB_COLLIDER_SPHERE && scene_doc_test_close(player3->collider.radius, 0.75f);
		component_ok = component_ok && scene_doc_test_close(level2->transform.spin_speed, 0.5f);
	}
	if (!component_ok) {
		printf("fail component roundtrip\n");
		scene_shutdown(&component_scene);
		return 1;
	}
	scene_shutdown(&component_scene);

	if (scene_doc_save(&doc, "test_atomic.tscn", &engine, &scene) != NB_RESULT_OK) {
		printf("fail atomic save\n");
		return 1;
	}
	FILE* leftover = fopen("test_atomic.tscn.tmp", "r");
	if (leftover) {
		printf("fail atomic temp cleanup\n");
		fclose(leftover);
		return 1;
	}
	remove("test_atomic.tscn");

	scene_doc_t bad_doc;
	scene_t bad_scene;
	scene_doc_init(&bad_doc);
	if (scene_doc_load_from_buffer(&bad_doc, "bad.tscn", bad_ref_scene, strlen(bad_ref_scene), &engine, &bad_scene) == NB_RESULT_OK) {
		printf("fail typed ref rejection\n");
		return 1;
	}

	scene_shutdown(&bad_scene);
	scene_shutdown(&uuid_scene_data);
	scene_shutdown(&scene2);
	scene_shutdown(&scene);
	asset_db_shutdown(&engine.assets);
	resource_store_shutdown(&engine.resources);
	log_shutdown(&engine.log);

	printf("scene doc test ok\n");

	return 0;
}
