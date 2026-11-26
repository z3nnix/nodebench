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

int input_action_from_name(const char* name, input_action_t* action) {
	if (!name || !action || strcmp(name, "reset") != 0) {
		return 0;
	}

	*action = NB_ACTION_RESET;

	return 1;
}

int input_is_held(const input_state_t* state, input_action_t action) {
	(void)state;
	(void)action;

	return 0;
}

int input_was_pressed(const input_state_t* state, input_action_t action) {
	(void)state;
	(void)action;

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
	if (root == NB_ENTITY_INVALID || id == NB_ENTITY_INVALID || child == NB_ENTITY_INVALID || other == NB_ENTITY_INVALID) {
		return 1;
	}

	if (!wren_vm_init(&engine.script_vm, &engine)) {
		return 1;
	}

	wren_vm_bind_scene(&engine.script_vm, &scene);
	if (!wren_vm_attach(&engine.script_vm, id, "test_spin", wren_test_source)) {
		return 1;
	}

	wren_vm_init_instances(&engine.script_vm);
	node_t* node = scene_get_node(&scene, id);
	node_t* other_node = scene_get_node(&scene, other);
	if (!node || !other_node) {
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

	wren_vm_shutdown(&engine.script_vm);
	scene_shutdown(&scene);
	log_shutdown(&engine.log);
	printf("wren test ok\n");

	return 0;
}
