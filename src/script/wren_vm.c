#include <stdio.h>
#include <string.h>

#include "core/engine.h"
#include "core/input.h"
#include "core/log.h"
#include "script/wren_vm.h"
#include "scene/scene.h"

#define NB_WREN_MODULE "nodebench"
#define NB_WREN_CLASS_NAME_CAPACITY 64

static const char* wren_builtin_source =
	"class NodeBridge {\n"
	"  foreign static name(id)\n"
	"  foreign static axis(id, kind, component)\n"
	"  foreign static set_axis(id, kind, component, value)\n"
	"  foreign static set(id, kind, x, y, z)\n"
	"  foreign static parent(id)\n"
	"  foreign static children(id)\n"
	"  foreign static add_child(parent, child)\n"
	"  foreign static remove_child(parent, child)\n"
	"  foreign static find_child(parent, name)\n"
	"  foreign static find_node(path)\n"
	"}\n"
	"class Vector3 {\n"
	"  construct new(x, y, z) {\n"
	"    _owner = 0\n"
	"    _kind = -1\n"
	"    _x = x\n"
	"    _y = y\n"
	"    _z = z\n"
	"  }\n"
	"  construct bind(owner, kind, x, y, z) {\n"
	"    _owner = owner\n"
	"    _kind = kind\n"
	"    _x = x\n"
	"    _y = y\n"
	"    _z = z\n"
	"  }\n"
	"  static from_node(owner, kind) {\n"
	"    return Vector3.bind(owner, kind, NodeBridge.axis(owner, kind, 0), NodeBridge.axis(owner, kind, 1), NodeBridge.axis(owner, kind, 2))\n"
	"  }\n"
	"  x {\n"
	"    if (_owner == 0) {\n"
	"      return _x\n"
	"    }\n"
	"    return NodeBridge.axis(_owner, _kind, 0)\n"
	"  }\n"
	"  x=(value) {\n"
	"    if (_owner == 0) {\n"
	"      _x = value\n"
	"    } else {\n"
	"      NodeBridge.set_axis(_owner, _kind, 0, value)\n"
	"    }\n"
	"  }\n"
	"  y {\n"
	"    if (_owner == 0) {\n"
	"      return _y\n"
	"    }\n"
	"    return NodeBridge.axis(_owner, _kind, 1)\n"
	"  }\n"
	"  y=(value) {\n"
	"    if (_owner == 0) {\n"
	"      _y = value\n"
	"    } else {\n"
	"      NodeBridge.set_axis(_owner, _kind, 1, value)\n"
	"    }\n"
	"  }\n"
	"  z {\n"
	"    if (_owner == 0) {\n"
	"      return _z\n"
	"    }\n"
	"    return NodeBridge.axis(_owner, _kind, 2)\n"
	"  }\n"
	"  z=(value) {\n"
	"    if (_owner == 0) {\n"
	"      _z = value\n"
	"    } else {\n"
	"      NodeBridge.set_axis(_owner, _kind, 2, value)\n"
	"    }\n"
	"  }\n"
	"}\n"
	"class Node3D {\n"
	"  construct new(id) {\n"
	"    _id = id\n"
	"    _position = Vector3.from_node(_id, 0)\n"
	"    _rotation = Vector3.from_node(_id, 1)\n"
	"    _scale = Vector3.from_node(_id, 2)\n"
	"  }\n"
	"  id {\n"
	"    return _id\n"
	"  }\n"
	"  name {\n"
	"    return NodeBridge.name(_id)\n"
	"  }\n"
	"  position {\n"
	"    return _position\n"
	"  }\n"
	"  position=(value) { NodeBridge.set(_id, 0, value.x, value.y, value.z) }\n"
	"  rotation {\n"
	"    return _rotation\n"
	"  }\n"
	"  rotation=(value) { NodeBridge.set(_id, 1, value.x, value.y, value.z) }\n"
	"  scale {\n"
	"    return _scale\n"
	"  }\n"
	"  scale=(value) { NodeBridge.set(_id, 2, value.x, value.y, value.z) }\n"
	"  parent {\n"
	"    var parent_id = NodeBridge.parent(_id)\n"
	"    if (parent_id == 0) {\n"
	"      return null\n"
	"    }\n"
	"    return Node3D.new(parent_id)\n"
	"  }\n"
	"  children {\n"
	"    var result = []\n"
	"    for (child_id in NodeBridge.children(_id)) {\n"
	"      result.add(Node3D.new(child_id))\n"
	"    }\n"
	"    return result\n"
	"  }\n"
	"  add_child(child) {\n"
	"    return NodeBridge.add_child(_id, child.id)\n"
	"  }\n"
	"  remove_child(child) {\n"
	"    return NodeBridge.remove_child(_id, child.id)\n"
	"  }\n"
	"  find_child(name) {\n"
	"    var child_id = NodeBridge.find_child(_id, name)\n"
	"    if (child_id == 0) {\n"
	"      return null\n"
	"    }\n"
	"    return Node3D.new(child_id)\n"
	"  }\n"
	"  find_node(path) {\n"
	"    var node_id = NodeBridge.find_node(path)\n"
	"    if (node_id == 0) {\n"
	"      return null\n"
	"    }\n"
	"    return Node3D.new(node_id)\n"
	"  }\n"
	"  find_parent(name) {\n"
	"    var node = parent\n"
	"    while (node != null) {\n"
	"      if (node.name == name) {\n"
	"        return node\n"
	"      }\n"
	"      node = node.parent\n"
	"    }\n"
	"    return null\n"
	"  }\n"
	"}\n"
	"class Input {\n"
	"  foreign static is_pressed(name)\n"
	"  foreign static was_pressed(name)\n"
	"}\n"
	"class Log {\n"
	"  foreign static info(message)\n"
	"  foreign static warn(message)\n"
	"  foreign static error(message)\n"
	"}\n"
	"class Audio {\n"
	"  foreign static play(name)\n"
	"}\n";

static wren_vm_t* wren_vm_from_wren(WrenVM* vm) {
	return (wren_vm_t*)wrenGetUserData(vm);
}

static int wren_vm_is_ident_start(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int wren_vm_is_ident_char(char c) {
	return wren_vm_is_ident_start(c) || (c >= '0' && c <= '9');
}

static void wren_vm_skip_space(const char** cursor) {
	const char* p = *cursor;
	for (;;) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			p++;
		}

		if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n') {
				p++;
			}
			continue;
		}

		if (p[0] == '/' && p[1] == '*') {
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) {
				p++;
			}
			if (*p) {
				p += 2;
			}
			continue;
		}

		*cursor = p;
		return;
	}
}

static int wren_vm_find_class(const char* source, char* name, size_t capacity) {
	const char* p = source;
	if (!p || !name || capacity == 0) {
		return 0;
	}

	while (*p) {
		wren_vm_skip_space(&p);
		if (!*p) {
			break;
		}

		if (*p == '"') {
			p++;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1]) {
					p += 2;
				}

				else {
					p++;
				}
			}
			if (*p) {
				p++;
			}
			continue;
		}

		if (strncmp(p, "class", 5) == 0 && !wren_vm_is_ident_char(p[5])) {
			p += 5;
			wren_vm_skip_space(&p);
			const char* start = p;
			while (wren_vm_is_ident_char(*p)) {
				p++;
			}
			size_t length = (size_t)(p - start);
			if (!length || length >= capacity) {
				continue;
			}

			wren_vm_skip_space(&p);
			if (strncmp(p, "is", 2) != 0 || wren_vm_is_ident_char(p[2])) {
				continue;
			}

			p += 2;
			wren_vm_skip_space(&p);
			if (strncmp(p, "Node3D", 6) == 0 && !wren_vm_is_ident_char(p[6])) {
				memcpy(name, start, length);
				name[length] = '\0';

				return 1;
			}
			continue;
		}

		p++;
	}

	return 0;
}

static node_t* wren_vm_node_from_slot(WrenVM* vm, int slot) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->scene) {
		return NULL;
	}

	entity_id_t id = (entity_id_t)wrenGetSlotDouble(vm, slot);

	return scene_get_node(state->scene, id);
}

static float* wren_vm_vector_from_kind(node_t* node, int kind) {
	if (!node) {
		return NULL;
	}

	if (kind == 0) {
		return &node->transform.position.x;
	}

	if (kind == 1) {
		return &node->transform.rotation.x;
	}

	if (kind == 2) {
		return &node->transform.scale.x;
	}

	return NULL;
}

static float* wren_vm_axis_from_kind(node_t* node, int kind, int axis) {
	float* vector = wren_vm_vector_from_kind(node, kind);
	if (!vector || axis < 0 || axis > 2) {
		return NULL;
	}

	return &vector[axis];
}

static void wren_vm_bridge_name(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	wrenSetSlotString(vm, 0, node ? node->name : "");
}

static void wren_vm_bridge_axis(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	int kind = (int)wrenGetSlotDouble(vm, 2);
	int axis = (int)wrenGetSlotDouble(vm, 3);
	float* value = wren_vm_axis_from_kind(node, kind, axis);
	wrenSetSlotDouble(vm, 0, value ? (double)*value : 0.0);
}

static void wren_vm_bridge_set_axis(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	int kind = (int)wrenGetSlotDouble(vm, 2);
	int axis = (int)wrenGetSlotDouble(vm, 3);
	float* value = wren_vm_axis_from_kind(node, kind, axis);
	if (value) {
		*value = (float)wrenGetSlotDouble(vm, 4);
	}
}

static void wren_vm_bridge_set(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	int kind = (int)wrenGetSlotDouble(vm, 2);
	float* vector = wren_vm_vector_from_kind(node, kind);
	if (!vector) {
		wrenSetSlotBool(vm, 0, false);

		return;
	}

	vector[0] = (float)wrenGetSlotDouble(vm, 3);
	vector[1] = (float)wrenGetSlotDouble(vm, 4);
	vector[2] = (float)wrenGetSlotDouble(vm, 5);
	wrenSetSlotBool(vm, 0, true);
}

static void wren_vm_bridge_parent(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	entity_id_t parent = node ? node->parent : NB_ENTITY_INVALID;
	wrenSetSlotDouble(vm, 0, (double)parent);
}

static void wren_vm_bridge_children(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	wrenEnsureSlots(vm, 3);
	wrenSetSlotNewList(vm, 0);
	if (!node) {
		return;
	}

	int index = 0;
	for (int i = 0; i < node->child_count; i++) {
		wrenSetSlotDouble(vm, 2, (double)node->children[i]);
		wrenInsertInList(vm, 0, index, 2);
		index++;
	}
}

static void wren_vm_bridge_add_child(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	node_t* parent = wren_vm_node_from_slot(vm, 1);
	entity_id_t child_id = (entity_id_t)wrenGetSlotDouble(vm, 2);
	int added = 0;
	if (state && state->scene && parent && scene_get_node(state->scene, child_id)) {
		added = scene_set_node_parent(state->scene, child_id, parent->id);
		if (added) {
			scene_update_world_transforms(state->scene);
		}
	}

	wrenSetSlotBool(vm, 0, added ? true : false);
}

static void wren_vm_bridge_remove_child(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	node_t* parent = wren_vm_node_from_slot(vm, 1);
	entity_id_t child_id = (entity_id_t)wrenGetSlotDouble(vm, 2);
	node_t* child = state && state->scene ? scene_get_node(state->scene, child_id) : NULL;
	int removed = 0;
	if (parent && child && child->parent == parent->id) {
		removed = scene_set_node_parent(state->scene, child_id, NB_ENTITY_INVALID);
		if (removed) {
			scene_update_world_transforms(state->scene);
		}
	}

	wrenSetSlotBool(vm, 0, removed ? true : false);
}

static void wren_vm_bridge_find_child(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	node_t* parent = wren_vm_node_from_slot(vm, 1);
	entity_id_t id = NB_ENTITY_INVALID;
	if (state && state->scene && parent) {
		entity_id_t candidate = scene_find_node(state->scene, wrenGetSlotString(vm, 2));
		node_t* child = scene_get_node(state->scene, candidate);
		if (child && child->parent == parent->id) {
			id = candidate;
		}
	}

	wrenSetSlotDouble(vm, 0, (double)id);
}

static void wren_vm_bridge_find_node(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	entity_id_t id = state && state->scene ? scene_find_node_path(state->scene, wrenGetSlotString(vm, 1)) : NB_ENTITY_INVALID;
	wrenSetSlotDouble(vm, 0, (double)id);
}

static void wren_vm_input_is_pressed(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	input_action_t action = NB_ACTION_COUNT;
	int pressed = 0;
	if (state && state->engine && input_action_from_name(wrenGetSlotString(vm, 1), &action)) {
		pressed = input_is_held(&state->engine->input, action);
	}

	wrenSetSlotBool(vm, 0, pressed ? true : false);
}

static void wren_vm_input_was_pressed(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	input_action_t action = NB_ACTION_COUNT;
	int pressed = 0;
	if (state && state->engine && input_action_from_name(wrenGetSlotString(vm, 1), &action)) {
		pressed = input_was_pressed(&state->engine->input, action);
	}

	wrenSetSlotBool(vm, 0, pressed ? true : false);
}

static void wren_vm_log_info(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine) {
		log_write(&state->engine->log, NB_LOG_INFO, "%s", wrenGetSlotString(vm, 1));
	}
}

static void wren_vm_log_warn(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine) {
		log_write(&state->engine->log, NB_LOG_WARN, "%s", wrenGetSlotString(vm, 1));
	}
}

static void wren_vm_log_error(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine) {
		log_write(&state->engine->log, NB_LOG_ERROR, "%s", wrenGetSlotString(vm, 1));
	}
}

static void wren_vm_audio_play(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int played = 0;
	if (state && state->engine && state->engine->audio_ready) {
		const char* name = wrenGetSlotString(vm, 1);
		audio_id id = resource_find_audio(&state->engine->resources, name);
		if (id != NB_RESOURCE_INVALID_ID && audio_play(&state->engine->audio, &state->engine->resources, id, 1.0f)) {
			played = 1;
		}
	}

	wrenSetSlotBool(vm, 0, played ? true : false);
}

static void wren_vm_write(WrenVM* vm, const char* text) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine && text) {
		log_write(&state->engine->log, NB_LOG_INFO, "wren: %s", text);
	}
}

static void wren_vm_error(WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->engine || !message) {
		return;
	}

	const char* kind = "runtime";
	if (type == WREN_ERROR_COMPILE) {
		kind = "compile";
	}

	else if (type == WREN_ERROR_STACK_TRACE) {
		kind = "stack";
	}

	if (module) {
		log_write(&state->engine->log, NB_LOG_ERROR, "wren %s %s:%d: %s", kind, module, line, message);
	}

	else {
		log_write(&state->engine->log, NB_LOG_ERROR, "wren %s: %s", kind, message);
	}
}

static WrenLoadModuleResult wren_vm_load_module(WrenVM* vm, const char* name) {
	WrenLoadModuleResult result;
	memset(&result, 0, sizeof(result));
	(void)vm;
	if (name && strcmp(name, NB_WREN_MODULE) == 0) {
		result.source = wren_builtin_source;
	}

	return result;
}

static WrenForeignMethodFn wren_vm_bind_method(WrenVM* vm, const char* module, const char* class_name, bool is_static, const char* signature) {
	(void)vm;
	if (!module || strcmp(module, NB_WREN_MODULE) != 0 || !class_name || !signature) {
		return NULL;
	}

	if (strcmp(class_name, "NodeBridge") == 0 && is_static) {
		if (strcmp(signature, "name(_)") == 0) {
			return wren_vm_bridge_name;
		}

		if (strcmp(signature, "axis(_,_,_)") == 0) {
			return wren_vm_bridge_axis;
		}

		if (strcmp(signature, "set_axis(_,_,_,_)") == 0) {
			return wren_vm_bridge_set_axis;
		}

		if (strcmp(signature, "set(_,_,_,_,_)") == 0) {
			return wren_vm_bridge_set;
		}

		if (strcmp(signature, "parent(_)") == 0) {
			return wren_vm_bridge_parent;
		}

		if (strcmp(signature, "children(_)") == 0) {
			return wren_vm_bridge_children;
		}

		if (strcmp(signature, "add_child(_,_)") == 0) {
			return wren_vm_bridge_add_child;
		}

		if (strcmp(signature, "remove_child(_,_)") == 0) {
			return wren_vm_bridge_remove_child;
		}

		if (strcmp(signature, "find_child(_,_)") == 0) {
			return wren_vm_bridge_find_child;
		}

		if (strcmp(signature, "find_node(_)") == 0) {
			return wren_vm_bridge_find_node;
		}
	}

	if (strcmp(class_name, "Input") == 0 && is_static) {
		if (strcmp(signature, "is_pressed(_)") == 0) {
			return wren_vm_input_is_pressed;
		}

		if (strcmp(signature, "was_pressed(_)") == 0) {
			return wren_vm_input_was_pressed;
		}
	}

	if (strcmp(class_name, "Log") == 0 && is_static) {
		if (strcmp(signature, "info(_)") == 0) {
			return wren_vm_log_info;
		}

		if (strcmp(signature, "warn(_)") == 0) {
			return wren_vm_log_warn;
		}

		if (strcmp(signature, "error(_)") == 0) {
			return wren_vm_log_error;
		}
	}

	if (strcmp(class_name, "Audio") == 0 && is_static && strcmp(signature, "play(_)") == 0) {
		return wren_vm_audio_play;
	}

	return NULL;
}

static void wren_vm_check_result(wren_vm_t* state, WrenInterpretResult result, const char* context) {
	if (result == WREN_RESULT_SUCCESS || !state || !state->engine) {
		return;
	}

	const char* text = result == WREN_RESULT_COMPILE_ERROR ? "compile error" : "runtime error";
	log_write(&state->engine->log, NB_LOG_ERROR, "wren %s: %s", context, text);
}

int wren_vm_init(wren_vm_t* vm, struct engine_t* engine) {
	if (!vm || !engine) {
		return 0;
	}

	memset(vm, 0, sizeof(*vm));
	vm->engine = engine;

	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.loadModuleFn = wren_vm_load_module;
	config.bindForeignMethodFn = wren_vm_bind_method;
	config.writeFn = wren_vm_write;
	config.errorFn = wren_vm_error;

	vm->vm = wrenNewVM(&config);
	if (!vm->vm) {
		return 0;
	}

	wrenSetUserData(vm->vm, vm);
	WrenInterpretResult result = wrenInterpret(vm->vm, NB_WREN_MODULE, wren_builtin_source);
	wren_vm_check_result(vm, result, "builtins");
	if (result != WREN_RESULT_SUCCESS) {
		wren_vm_shutdown(vm);

		return 0;
	}

	wrenEnsureSlots(vm->vm, 1);
	wrenGetVariable(vm->vm, NB_WREN_MODULE, "Node3D", 0);
	vm->node3d_class = wrenGetSlotHandle(vm->vm, 0);
	vm->construct_instance = wrenMakeCallHandle(vm->vm, "new(_)");
	vm->call_init = wrenMakeCallHandle(vm->vm, "init()");
	vm->call_update = wrenMakeCallHandle(vm->vm, "update(_)");
	vm->call_on_input = wrenMakeCallHandle(vm->vm, "on_input(_)");
	vm->call_on_collision = wrenMakeCallHandle(vm->vm, "on_collision(_)");
	if (!vm->node3d_class || !vm->construct_instance || !vm->call_init || !vm->call_update || !vm->call_on_input || !vm->call_on_collision) {
		wren_vm_shutdown(vm);

		return 0;
	}

	vm->ready = 1;

	return 1;
}

void wren_vm_shutdown(wren_vm_t* vm) {
	if (!vm || !vm->vm) {
		return;
	}

	for (int i = 0; i < vm->instance_count; i++) {
		if (vm->instances[i]) {
			wrenReleaseHandle(vm->vm, vm->instances[i]);
			vm->instances[i] = NULL;
		}
	}

	if (vm->node3d_class) {
		wrenReleaseHandle(vm->vm, vm->node3d_class);
	}

	if (vm->construct_instance) {
		wrenReleaseHandle(vm->vm, vm->construct_instance);
	}

	if (vm->call_init) {
		wrenReleaseHandle(vm->vm, vm->call_init);
	}

	if (vm->call_update) {
		wrenReleaseHandle(vm->vm, vm->call_update);
	}

	if (vm->call_on_input) {
		wrenReleaseHandle(vm->vm, vm->call_on_input);
	}

	if (vm->call_on_collision) {
		wrenReleaseHandle(vm->vm, vm->call_on_collision);
	}

	wrenFreeVM(vm->vm);
	memset(vm, 0, sizeof(*vm));
}

void wren_vm_bind_scene(wren_vm_t* vm, scene_t* scene) {
	if (vm) {
		vm->scene = scene;
	}
}

int wren_vm_attach(wren_vm_t* vm, entity_id_t id, const char* module, const char* source) {
	if (!vm || !vm->ready || !module || !*module || !source || !*source || vm->instance_count >= NB_WREN_MAX_INSTANCES) {
		return 0;
	}

	char class_name[NB_WREN_CLASS_NAME_CAPACITY];
	if (!wren_vm_find_class(source, class_name, sizeof(class_name))) {
		log_write(&vm->engine->log, NB_LOG_ERROR, "wren %s: class inheriting Node3D not found", module);

		return 0;
	}

	WrenInterpretResult result = wrenInterpret(vm->vm, module, source);
	wren_vm_check_result(vm, result, module);
	if (result != WREN_RESULT_SUCCESS) {
		return 0;
	}

	if (!wrenHasVariable(vm->vm, module, class_name)) {
		log_write(&vm->engine->log, NB_LOG_ERROR, "wren %s: class %s not found", module, class_name);

		return 0;
	}

	wrenEnsureSlots(vm->vm, 3);
	wrenGetVariable(vm->vm, module, class_name, 0);
	wrenSetSlotDouble(vm->vm, 1, (double)id);
	result = wrenCall(vm->vm, vm->construct_instance);
	wren_vm_check_result(vm, result, module);
	if (result != WREN_RESULT_SUCCESS) {
		return 0;
	}

	WrenHandle* instance = wrenGetSlotHandle(vm->vm, 0);
	if (!instance) {
		return 0;
	}

	vm->instances[vm->instance_count] = instance;
	vm->instance_ids[vm->instance_count] = id;
	vm->instance_count++;

	return 1;
}

void wren_vm_init_instances(wren_vm_t* vm) {
	if (!vm || !vm->ready) {
		return;
	}

	for (int i = 0; i < vm->instance_count; i++) {
		wrenEnsureSlots(vm->vm, 1);
		wrenSetSlotHandle(vm->vm, 0, vm->instances[i]);
		wren_vm_check_result(vm, wrenCall(vm->vm, vm->call_init), "init");
	}
}

void wren_vm_update(wren_vm_t* vm, float dt) {
	if (!vm || !vm->ready) {
		return;
	}

	for (int i = 0; i < vm->instance_count; i++) {
		wrenEnsureSlots(vm->vm, 2);
		wrenSetSlotHandle(vm->vm, 0, vm->instances[i]);
		wrenSetSlotDouble(vm->vm, 1, (double)dt);
		wren_vm_check_result(vm, wrenCall(vm->vm, vm->call_update), "update");
	}
}

void wren_vm_on_input(wren_vm_t* vm, const char* action) {
	if (!vm || !vm->ready || !action) {
		return;
	}

	for (int i = 0; i < vm->instance_count; i++) {
		wrenEnsureSlots(vm->vm, 2);
		wrenSetSlotHandle(vm->vm, 0, vm->instances[i]);
		wrenSetSlotString(vm->vm, 1, action);
		wren_vm_check_result(vm, wrenCall(vm->vm, vm->call_on_input), "on_input");
	}
}

void wren_vm_on_collision(wren_vm_t* vm, entity_id_t left, entity_id_t right) {
	if (!vm || !vm->ready || !vm->node3d_class) {
		return;
	}

	for (int i = 0; i < vm->instance_count; i++) {
		entity_id_t self = vm->instance_ids[i];
		entity_id_t other = NB_ENTITY_INVALID;
		if (self == left) {
			other = right;
		}

		else if (self == right) {
			other = left;
		}

		else {
			continue;
		}

		wrenEnsureSlots(vm->vm, 3);
		wrenSetSlotHandle(vm->vm, 0, vm->node3d_class);
		wrenSetSlotDouble(vm->vm, 1, (double)other);
		WrenInterpretResult result = wrenCall(vm->vm, vm->construct_instance);
		wren_vm_check_result(vm, result, "wrap_node");
		if (result != WREN_RESULT_SUCCESS) {
			continue;
		}

		WrenHandle* other_node = wrenGetSlotHandle(vm->vm, 0);
		if (!other_node) {
			continue;
		}

		wrenEnsureSlots(vm->vm, 2);
		wrenSetSlotHandle(vm->vm, 0, vm->instances[i]);
		wrenSetSlotHandle(vm->vm, 1, other_node);
		wren_vm_check_result(vm, wrenCall(vm->vm, vm->call_on_collision), "on_collision");
		wrenReleaseHandle(vm->vm, other_node);
	}
}
