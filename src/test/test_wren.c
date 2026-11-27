#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/engine.h"
#include "core/log.h"
#include "scene/scene.h"
#include "script/wren_vm.h"

static const char* wren_test_source =
	"import \"nodebench\" for Node3D, Vector3\n"
	"class TestScript is Node3D {\n"
	"  construct new(id) {\n"
	"    super(id)\n"
	"  }\n"
	"  init() {\n"
	"    position = Vector3.new(1.0, 2.0, 3.0)\n"
	"    if (name == \"TestNode\" && parent != null && parent.name == \"Root\") {\n"
	"      rotation.x = 1.0\n"
	"    }\n"
	"    var kids = children\n"
	"    if (kids.count == 1 && kids[0].name == \"Child\") {\n"
	"      rotation.x = 2.0\n"
	"    }\n"
	"    var child = find_child(\"Child\")\n"
	"    if (child != null && child.name == \"Child\") {\n"
	"      rotation.z = 1.0\n"
	"    }\n"
	"    var path = find_node(\"Root/TestNode/Child\")\n"
	"    if (path != null && path.name == \"Child\") {\n"
	"      scale = Vector3.new(2.0, 2.0, 2.0)\n"
	"    }\n"
	"    if (find_parent(\"Root\") != null) {\n"
	"      position.x = 1.0\n"
	"    }\n"
	"    var other = find_node(\"Root/Other\")\n"
	"    if (other != null && add_child(other)) {\n"
	"      rotation.x = 3.0\n"
	"    }\n"
	"    if (other != null && remove_child(other)) {\n"
	"      rotation.z = 3.0\n"
	"    }\n"
	"  }\n"
	"  update(dt) {\n"
	"    rotation.y = rotation.y + dt * 16.0\n"
	"  }\n"
	"  on_input(action) {\n"
	"    if (action == \"reset\") {\n"
	"      position = Vector3.new(4.0, 5.0, 6.0)\n"
	"    }\n"
	"  }\n"
	"  on_collision(other) {\n"
	"    if (other.name == \"Other\") {\n"
	"      position = Vector3.new(other.id, 0.0, 0.0)\n"
	"    }\n"
	"  }\n"
	"}\n";

static const char* wren_camera_source =
	"import \"nodebench\" for Node3D, Vector3, Camera, Input\n"
	"class TestCamera is Node3D {\n"
	"  construct new(id) {\n"
	"    super(id)\n"
	"  }\n"
	"  init() {\n"
	"    Camera.use(id)\n"
	"    Input.set_mouse_captured(true)\n"
	"    Camera.set_target(id, Vector3.new(1.0, 2.0, 3.0))\n"
	"    Camera.set_up(id, Vector3.new(0.0, 0.0, 1.0))\n"
	"    Camera.set_fov(id, 80.0)\n"
	"    Camera.set_near(id, 0.5)\n"
	"    Camera.set_far(id, 200.0)\n"
	"  }\n"
	"  update(dt) {\n"
	"    return\n"
	"  }\n"
	"  on_input(action) {\n"
	"    return\n"
	"  }\n"
	"  on_collision(other) {\n"
	"    return\n"
	"  }\n"
	"}\n";

static const char* wren_no_callback_source =
	"import \"nodebench\" for Node3D\n"
	"class TestNoCallbacks is Node3D {\n"
	"  construct new(id) {\n"
	"    super(id)\n"
	"  }\n"
	"}\n";

static const char* wren_world_source =
	"import \"nodebench\" for Node3D, Vector3, World, Physics\n"
	"class TestWorld is Node3D {\n"
	"  construct new(id) {\n"
	"    super(id)\n"
	"  }\n"
	"  init() {\n"
	"    World.set_fog(2.0, 9.0, 11, 22, 33)\n"
	"    World.set_sky(1, 2, 3, 4, 5, 6)\n"
	"    World.set_light(0.1, 0.2, 0.3, 0.4, 0.5, 0.6)\n"
	"    World.set_affine(0.75)\n"
	"    World.set_dither(false)\n"
	"    World.set_fog_enabled(false)\n"
	"    visible = false\n"
	"    tint = Vector3.new(0.7, 0.8, 0.9)\n"
	"    look_at(4.0, 0.0, 0.0)\n"
	"    Physics.move(id, 1.0, 0.0, 0.0, 0.2)\n"
	"  }\n"
	"  update(dt) { null }\n"
	"  on_input(action) { null }\n"
	"  on_collision(other) { null }\n"
	"}\n";

int input_was_pressed(const input_state_t* state, const char* action) {
	(void)state;
	(void)action;
	return 0;
}

int input_is_held(const input_state_t* state, const char* action) {
	(void)state;
	(void)action;
	return 0;
}

const char* input_action_at(const input_state_t* state, int index) {
	(void)state;
	(void)index;
	return "";
}

int input_action_count(const input_state_t* state) {
	(void)state;
	return 0;
}

static int wren_test_close(float a, float b) {
	return fabsf(a - b) < 0.0001f;
}

static entity_id_t wren_test_create_node(scene_t* scene, const char* name, entity_id_t parent) {
	transform_t transform;
	memset(&transform, 0, sizeof(transform));
	transform.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };

	entity_id_t id = scene_create_entity(scene);
	if (id == NB_ENTITY_INVALID || !scene_add_transform(scene, id, transform) || !scene_set_node_name(scene, id, name)) {
		return NB_ENTITY_INVALID;
	}

	if (!scene_set_node_parent(scene, id, parent)) {
		return NB_ENTITY_INVALID;
	}

	return id;
}

int main(void) {
	engine_t engine;
	memset(&engine, 0, sizeof(engine));
	if (!log_init(&engine.log, NULL, NB_LOG_ERROR)) {
		return 1;
	}

	scene_t scene;
	scene_init(&scene);

	entity_id_t root = wren_test_create_node(&scene, "Root", NB_ENTITY_INVALID);
	entity_id_t id = wren_test_create_node(&scene, "TestNode", root);
	entity_id_t child = wren_test_create_node(&scene, "Child", id);
	entity_id_t other = wren_test_create_node(&scene, "Other", root);
	entity_id_t camera_id = wren_test_create_node(&scene, "TestCamera", root);
	entity_id_t no_callbacks_id = wren_test_create_node(&scene, "NoCallbacks", root);
	entity_id_t world_id = wren_test_create_node(&scene, "TestWorld", root);
	if (root == NB_ENTITY_INVALID || id == NB_ENTITY_INVALID || child == NB_ENTITY_INVALID || other == NB_ENTITY_INVALID || camera_id == NB_ENTITY_INVALID || no_callbacks_id == NB_ENTITY_INVALID || world_id == NB_ENTITY_INVALID) {
		return 1;
	}

	node_t* other_node = scene_get_node(&scene, other);
	if (!other_node) {
		return 1;
	}

	other_node->transform.position.x = 1.0f;
	collider_t box;
	box.shape = NB_COLLIDER_AABB;
	box.extents = (vec3_t){ 0.5f, 0.5f, 0.5f };
	box.radius = 0.0f;
	if (!scene_add_collider(&scene, other, box)) {
		return 1;
	}

	scene_update_world_transforms(&scene);
	scene_rebuild_cache(&scene);

	camera_t camera;
	memset(&camera, 0, sizeof(camera));
	camera.eye = (vec3_t){ 0.0f, 0.0f, 0.0f };
	camera.target = (vec3_t){ 0.0f, 0.0f, -1.0f };
	camera.up = (vec3_t){ 0.0f, 1.0f, 0.0f };
	camera.fov = 60.0f;
	camera.near_plane = 0.1f;
	camera.far_plane = 100.0f;
	if (!scene_add_camera(&scene, camera_id, camera)) {
		return 1;
	}

	if (!wren_vm_init(&engine.script_vm, &engine)) {
		return 1;
	}

	wren_vm_bind_scene(&engine.script_vm, &scene);
	if (!wren_vm_attach(&engine.script_vm, id, "test_spin", wren_test_source)) {
		return 1;
	}

	if (!wren_vm_attach(&engine.script_vm, camera_id, "test_camera", wren_camera_source)) {
		return 1;
	}

	if (!wren_vm_attach(&engine.script_vm, no_callbacks_id, "test_no_callbacks", wren_no_callback_source)) {
		return 1;
	}

	if (!wren_vm_attach(&engine.script_vm, world_id, "test_world", wren_world_source)) {
		return 1;
	}

	wren_vm_init_instances(&engine.script_vm);
	node_t* node = scene_get_node(&scene, id);
	node_t* world_node = scene_get_node(&scene, world_id);
	if (!node || !world_node || !other_node) {
		return 1;
	}

	if (!wren_test_close(node->transform.position.x, 1.0f) || !wren_test_close(node->transform.position.y, 2.0f) || !wren_test_close(node->transform.position.z, 3.0f)) {
		return 1;
	}

	if (!wren_test_close(node->transform.rotation.x, 3.0f) || !wren_test_close(node->transform.rotation.z, 3.0f) || !wren_test_close(node->transform.scale.x, 2.0f)) {
		return 1;
	}

	if (other_node->parent != NB_ENTITY_INVALID) {
		return 1;
	}

	scene_rebuild_cache(&scene);
	camera_t* active_camera = scene_get_active_camera(&scene);
	if (!active_camera || scene.active_camera != camera_id) {
		return 1;
	}

	if (!wren_test_close(active_camera->target.x, 1.0f) || !wren_test_close(active_camera->target.y, 2.0f) || !wren_test_close(active_camera->target.z, 3.0f)) {
		return 1;
	}

	if (!wren_test_close(active_camera->up.z, 1.0f) || !wren_test_close(active_camera->fov, 80.0f) || !wren_test_close(active_camera->near_plane, 0.5f) || !wren_test_close(active_camera->far_plane, 200.0f)) {
		return 1;
	}

	if (!engine.platform.mouse_captured) {
		return 1;
	}

	wren_vm_update(&engine.script_vm, 0.25f);
	if (!wren_test_close(node->transform.rotation.y, 4.0f)) {
		return 1;
	}

	wren_vm_on_input(&engine.script_vm, "reset");
	if (!wren_test_close(node->transform.position.x, 4.0f) || !wren_test_close(node->transform.position.y, 5.0f) || !wren_test_close(node->transform.position.z, 6.0f)) {
		return 1;
	}

	wren_vm_on_collision(&engine.script_vm, id, other);
	if (!wren_test_close(node->transform.position.x, (float)other) || !wren_test_close(node->transform.position.y, 0.0f) || !wren_test_close(node->transform.position.z, 0.0f)) {
		return 1;
	}

	node_t* no_callbacks_node = scene_get_node(&scene, no_callbacks_id);
	if (!no_callbacks_node || !wren_test_close(no_callbacks_node->transform.rotation.y, 0.0f)) {
		return 1;
	}

	scene_update_world_transforms(&scene);
	if (world_node->visible || world_node->world_visible || !world_node->tint_enabled) {
		return 1;
	}

	if (!wren_test_close(world_node->tint.x, 0.7f) || !wren_test_close(world_node->tint.y, 0.8f) || !wren_test_close(world_node->tint.z, 0.9f)) {
		return 1;
	}

	if (!wren_test_close(world_node->transform.rotation.y, 1.5707963267948966f) || !wren_test_close(world_node->transform.position.x, 0.0f)) {
		return 1;
	}

	if (!wren_test_close(engine.config.fog_near, 2.0f) || !wren_test_close(engine.config.fog_far, 9.0f) || engine.config.fog_color_rgb[0] != 11 || engine.config.sky_top_rgb[2] != 3 || engine.config.sky_bottom_rgb[0] != 4) {
		return 1;
	}

	if (!wren_test_close(engine.config.affine_strength, 0.75f) || engine.config.dither_enabled || engine.config.fog_enabled || !wren_test_close(engine.config.light_color[1], 0.5f)) {
		return 1;
	}

	wren_vm_shutdown(&engine.script_vm);
	scene_shutdown(&scene);
	log_shutdown(&engine.log);
	printf("wren test ok\n");

	return 0;
}
