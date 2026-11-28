#include "test/fixture_project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/engine.h"
#include "render/primitives.h"
#include "scene/scene_doc.h"

static const char* bad_ref_scene =
	"[gd_scene load_steps=3 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://models/cube.obj\" id=\"m\"]\n"
	"[ext_resource type=\"NBPS1Material\" path=\"res://assets/tile.tres\" id=\"mat\"]\n"
	"[node name=\"Bad\" type=\"MeshInstance3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"mesh = ExtResource(\"m\")\n"
	"material = ExtResource(\"m\")\n";

static const char* unused_broken_ext_scene =
	"[gd_scene load_steps=2 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://primitive/triangle\" id=\"old_triangle\"]\n"
	"[node name=\"Unused\" type=\"Node3D\" parent=\".\"]\n";

static const char* used_broken_ext_scene =
	"[gd_scene load_steps=2 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://primitive/triangle\" id=\"old_triangle\"]\n"
	"[node name=\"Used\" type=\"MeshInstance3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"mesh = ExtResource(\"old_triangle\")\n";

static const char* primitive_scene =
	"[gd_scene load_steps=3 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://primitive/square\" id=\"mesh_prim_square\"]\n"
	"[ext_resource type=\"NBPS1Material\" path=\"res://primitive/default.tres\" id=\"mat_prim_default\"]\n"
	"[node name=\"Primitive\" type=\"MeshInstance3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"mesh = ExtResource(\"mesh_prim_square\")\n"
	"material = ExtResource(\"mat_prim_default\")\n";

static const char* helpers_scene =
	"[gd_scene load_steps=1 format=3]\n"
	"[node name=\"Cam\" type=\"Camera3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 5, 6, 7)\n"
	"fov = 70.000000\n"
	"target = Vector3(1.000000, 2.000000, 3.000000)\n"
	"[node name=\"Sun\" type=\"DirectionalLight3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 2, 3)\n"
	"direction = Vector3(0.000000, -1.000000, 0.000000)\n";

static const char* missing_material_scene =
	"[gd_scene load_steps=2 format=3]\n"
	"[ext_resource type=\"Mesh\" path=\"res://primitive/square\" id=\"mesh_prim_square\"]\n"
	"[node name=\"NoMat\" type=\"MeshInstance3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"mesh = ExtResource(\"mesh_prim_square\")\n";

static const char* instance_scene =
	"[gd_scene load_steps=2 format=3]\n"
	"[ext_resource type=\"PackedScene\" path=\"res://scenes/level01.tscn\" id=\"packed_level\"]\n"
	"[node name=\"LevelInstance\" type=\"Node3D\" parent=\".\"]\n"
	"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)\n"
	"instance = ExtResource(\"packed_level\")\n";

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
	char cwd[PATH_MAX];
	char root[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd)) || !nb_fixture_create(root, sizeof(root)) || chdir(root) != 0) {
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

	scene_doc_t unused_doc;
	scene_t unused_scene;
	scene_doc_init(&unused_doc);
	if (scene_doc_load_from_buffer(&unused_doc, "unused.tscn", unused_broken_ext_scene, strlen(unused_broken_ext_scene), &engine, &unused_scene) != NB_RESULT_OK) {
		printf("fail unused broken ext tolerance\n");
		return 1;
	}
	scene_shutdown(&unused_scene);

	scene_doc_t used_doc;
	scene_t used_scene;
	scene_doc_init(&used_doc);
	if (scene_doc_load_from_buffer(&used_doc, "used.tscn", used_broken_ext_scene, strlen(used_broken_ext_scene), &engine, &used_scene) == NB_RESULT_OK) {
		printf("fail used broken ext rejection\n");
		scene_shutdown(&used_scene);
		return 1;
	}
	scene_shutdown(&used_scene);

	for (int i = 0; i < mesh_primitive_count(); i++) {
		mesh_t primitive_mesh;
		if (!mesh_create_primitive(mesh_primitive_name(i), &primitive_mesh) || primitive_mesh.vertex_count < 3 || primitive_mesh.triangle_count < 1) {
			printf("fail primitive mesh %s\n", mesh_primitive_name(i));
			return 1;
		}
		mesh_destroy(&primitive_mesh);
	}

	scene_doc_t primitive_doc;
	scene_t primitive_scene_data;
	scene_doc_init(&primitive_doc);
	if (scene_doc_load_from_buffer(&primitive_doc, "primitive.tscn", primitive_scene, strlen(primitive_scene), &engine, &primitive_scene_data) != NB_RESULT_OK) {
		printf("fail primitive scene load\n");
		return 1;
	}
	node_t* primitive = scene_doc_test_node(&primitive_scene_data, "Primitive");
	if (!primitive || primitive->mesh_renderer.mesh == NB_RESOURCE_INVALID_ID || primitive->mesh_renderer.material == NB_RESOURCE_INVALID_ID) {
		printf("fail primitive refs\n");
		scene_shutdown(&primitive_scene_data);
		return 1;
	}
	const scene_doc_ext_t* primitive_mesh = scene_doc_find_ext(&primitive_doc, NB_RESOURCE_KIND_MESH, primitive->mesh_renderer.mesh);
	const scene_doc_ext_t* primitive_material = scene_doc_find_ext(&primitive_doc, NB_RESOURCE_KIND_MATERIAL, primitive->mesh_renderer.material);
	if (!primitive_mesh || strcmp(primitive_mesh->path, "res://primitive/square") != 0 || !primitive_material || strcmp(primitive_material->path, "res://primitive/default.tres") != 0) {
		printf("fail primitive ext paths\n");
		scene_shutdown(&primitive_scene_data);
		return 1;
	}
	const material_t* primitive_material_res = resource_get_material(&engine.resources, primitive->mesh_renderer.material);
	const texture_t* primitive_texture = primitive_material_res ? resource_get_texture(&engine.resources, primitive_material_res->texture) : NULL;
	if (!primitive_texture || primitive_texture->width != 64 || primitive_texture->height != 64 || primitive_texture->format != NB_TEXTURE_4BIT) {
		printf("fail primitive default texture\n");
		scene_shutdown(&primitive_scene_data);
		return 1;
	}
	scene_doc_ext_t extra;
	memset(&extra, 0, sizeof(extra));
	snprintf(extra.id, sizeof(extra.id), "%s", "mesh_prim_sphere");
	snprintf(extra.type, sizeof(extra.type), "%s", "Mesh");
	snprintf(extra.path, sizeof(extra.path), "%s", "res://primitive/sphere");
	extra.kind = NB_RESOURCE_KIND_MESH;
	extra.handle = primitive->mesh_renderer.mesh;
	if (!scene_doc_add_ext(&primitive_doc, &extra) || scene_doc_add_ext(&primitive_doc, &extra) || !scene_doc_find_ext_by_id(&primitive_doc, "mesh_prim_sphere")) {
		printf("fail add ext\n");
		scene_shutdown(&primitive_scene_data);
		return 1;
	}
	char* primitive_data = NULL;
	size_t primitive_size = 0;
	if (scene_doc_save_to_buffer(&primitive_doc, &primitive_scene_data, &primitive_data, &primitive_size) != NB_RESULT_OK || !primitive_data ||
		!strstr(primitive_data, "res://primitive/square") || !strstr(primitive_data, "res://primitive/default.tres") || strstr(primitive_data, "res://primitive/sphere")) {
		printf("fail primitive roundtrip\n");
		free(primitive_data);
		scene_shutdown(&primitive_scene_data);
		return 1;
	}
	free(primitive_data);
	scene_shutdown(&primitive_scene_data);

	scene_doc_t fallback_doc;
	scene_t fallback_scene;
	scene_doc_init(&fallback_doc);
	if (scene_doc_load_from_buffer(&fallback_doc, "missing_material.tscn", missing_material_scene, strlen(missing_material_scene), &engine, &fallback_scene) != NB_RESULT_OK) {
		printf("fail missing material scene load\n");
		return 1;
	}
	node_t* fallback_node = scene_doc_test_node(&fallback_scene, "NoMat");
	const scene_doc_ext_t* fallback_material = fallback_node ? scene_doc_find_ext(&fallback_doc, NB_RESOURCE_KIND_MATERIAL, fallback_node->mesh_renderer.material) : NULL;
	if (!fallback_node || fallback_node->mesh_renderer.material == NB_RESOURCE_INVALID_ID || !fallback_material || strcmp(fallback_material->path, "res://primitive/default.tres") != 0) {
		printf("fail missing material fallback\n");
		scene_shutdown(&fallback_scene);
		return 1;
	}
	char* fallback_data = NULL;
	size_t fallback_size = 0;
	if (scene_doc_save_to_buffer(&fallback_doc, &fallback_scene, &fallback_data, &fallback_size) != NB_RESULT_OK || !fallback_data || !strstr(fallback_data, "material = ExtResource(")) {
		printf("fail missing material save\n");
		free(fallback_data);
		scene_shutdown(&fallback_scene);
		return 1;
	}
	free(fallback_data);
	scene_shutdown(&fallback_scene);

	scene_doc_t instance_doc;
	scene_t instance_scene_data;
	scene_doc_init(&instance_doc);
	if (scene_doc_load_from_buffer(&instance_doc, "instance.tscn", instance_scene, strlen(instance_scene), &engine, &instance_scene_data) != NB_RESULT_OK) {
		printf("fail instance scene load\n");
		return 1;
	}
	node_t* instance_node = scene_doc_test_node(&instance_scene_data, "LevelInstance");
	node_t* instance_child = scene_doc_test_node(&instance_scene_data, "Player");
	if (!instance_node || !(instance_node->components & NB_NODE_COMPONENT_SCENE_INSTANCE) || !instance_child || !(instance_child->components & NB_NODE_COMPONENT_INSTANCE_CHILD) || instance_child->instance_owner != instance_node->id) {
		printf("fail instance nodes\n");
		scene_shutdown(&instance_scene_data);
		return 1;
	}
	char* instance_data = NULL;
	size_t instance_size = 0;
	if (scene_doc_save_to_buffer(&instance_doc, &instance_scene_data, &instance_data, &instance_size) != NB_RESULT_OK || !instance_data ||
		!strstr(instance_data, "type=\"PackedScene\"") || !strstr(instance_data, "instance = ExtResource(") || strstr(instance_data, "[node name=\"Player\"")) {
		printf("fail instance save\n");
		free(instance_data);
		scene_shutdown(&instance_scene_data);
		return 1;
	}
	free(instance_data);
	scene_shutdown(&instance_scene_data);

	scene_doc_t helpers_doc;
	scene_t helpers_scene_data;
	scene_doc_init(&helpers_doc);
	if (scene_doc_load_from_buffer(&helpers_doc, "helpers.tscn", helpers_scene, strlen(helpers_scene), &engine, &helpers_scene_data) != NB_RESULT_OK) {
		printf("fail helpers scene load\n");
		return 1;
	}
	scene_update_world_transforms(&helpers_scene_data);
	scene_rebuild_cache(&helpers_scene_data);
	node_t* helper_camera = scene_doc_test_node(&helpers_scene_data, "Cam");
	node_t* helper_light = scene_doc_test_node(&helpers_scene_data, "Sun");
	if (!helper_camera || !(helper_camera->components & NB_NODE_COMPONENT_CAMERA) || !helper_light ||
		!(helper_light->components & NB_NODE_COMPONENT_LIGHT) || !(helper_light->components & NB_NODE_COMPONENT_TRANSFORM)) {
		printf("fail helper components\n");
		scene_shutdown(&helpers_scene_data);
		return 1;
	}
	if (fabsf(helper_camera->world_position.x - 5.0f) > 0.001f || fabsf(helper_camera->camera.fov - 70.0f) > 0.001f ||
		fabsf(helper_light->world_position.z - 3.0f) > 0.001f || fabsf(helper_light->light.direction.y + 1.0f) > 0.001f) {
		printf("fail helper data\n");
		scene_shutdown(&helpers_scene_data);
		return 1;
	}
	scene_shutdown(&helpers_scene_data);

	scene_shutdown(&bad_scene);
	scene_shutdown(&uuid_scene_data);
	scene_shutdown(&scene2);
	scene_shutdown(&scene);
	asset_db_shutdown(&engine.assets);
	resource_store_shutdown(&engine.resources);
	log_shutdown(&engine.log);

	if (chdir(cwd) != 0) {
		return 1;
	}
	nb_fixture_destroy(root);
	printf("scene doc test ok\n");

	return 0;
}
