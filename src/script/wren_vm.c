#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset/asset_db.h"
#include "core/dynamic_array.h"
#include "core/engine.h"
#include "core/input.h"
#include "core/log.h"
#include "physics/collision.h"
#include "script/script.h"
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
	"  foreign static world_axis(id, component)\n"
	"  foreign static visible(id)\n"
	"  foreign static set_visible(id, value)\n"
	"  foreign static look_at(id, x, y, z)\n"
	"}\n"
	"class CameraBridge {\n"
	"  foreign static set(id, kind, x, y, z)\n"
	"  foreign static set_scalar(id, kind, value)\n"
	"  foreign static use(id)\n"
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
	"  world_position {\n"
	"    return Vector3.new(NodeBridge.world_axis(_id, 0), NodeBridge.world_axis(_id, 1), NodeBridge.world_axis(_id, 2))\n"
	"  }\n"
	"  visible {\n"
	"    return NodeBridge.visible(_id)\n"
	"  }\n"
	"  visible=(value) { NodeBridge.set_visible(_id, value) }\n"
	"  tint {\n"
	"    return Vector3.from_node(_id, 5)\n"
	"  }\n"
	"  tint=(value) { NodeBridge.set(_id, 5, value.x, value.y, value.z) }\n"
	"  look_at(x, y, z) { NodeBridge.look_at(_id, x, y, z) }\n"
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
	"  init() { null }\n"
	"  update(dt) { null }\n"
	"  on_input(action) { null }\n"
	"  on_collision(other) { null }\n"
	"}\n"
	"class Camera {\n"
	"  static target(id) {\n"
	"    return Vector3.from_node(id, 3)\n"
	"  }\n"
	"  static set_target(id, value) { CameraBridge.set(id, 3, value.x, value.y, value.z) }\n"
	"  static set_up(id, value) { CameraBridge.set(id, 4, value.x, value.y, value.z) }\n"
	"  static set_fov(id, value) { CameraBridge.set_scalar(id, 0, value) }\n"
	"  static set_near(id, value) { CameraBridge.set_scalar(id, 1, value) }\n"
	"  static set_far(id, value) { CameraBridge.set_scalar(id, 2, value) }\n"
	"  static use(id) { CameraBridge.use(id) }\n"
	"}\n"
	"class Input {\n"
	"  foreign static is_pressed(name)\n"
	"  foreign static was_pressed(name)\n"
	"  foreign static mouse_dx()\n"
	"  foreign static mouse_dy()\n"
	"  foreign static mouse_captured()\n"
	"  foreign static set_mouse_captured(captured)\n"
	"}\n"
	"class Log {\n"
	"  foreign static info(message)\n"
	"  foreign static warn(message)\n"
	"  foreign static error(message)\n"
	"}\n"
	"class Audio {\n"
	"  foreign static play(name)\n"
	"  foreign static loop(name, volume)\n"
	"  foreign static stop(name)\n"
	"  foreign static set_volume(name, volume)\n"
	"  foreign static stop_all()\n"
	"}\n"
	"class World {\n"
	"  foreign static set_fog(near, far, r, g, b)\n"
	"  foreign static set_sky(top_r, top_g, top_b, bottom_r, bottom_g, bottom_b)\n"
	"  foreign static set_light(x, y, z, r, g, b)\n"
	"  foreign static set_affine(value)\n"
	"  foreign static set_dither(value)\n"
	"  foreign static set_fog_enabled(value)\n"
	"  foreign static time()\n"
	"}\n"
	"class Physics {\n"
	"  foreign static move(id, dx, dy, dz, radius)\n"
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

	if (kind == 3) {
		return &node->camera.target.x;
	}

	if (kind == 4) {
		return &node->camera.up.x;
	}

	if (kind == 5) {
		return &node->tint.x;
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
		if (kind == 5 && node) {
			node->tint_enabled = 1;
		}
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
	if (kind == 5 && node) {
		node->tint_enabled = 1;
	}
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

static void wren_vm_bridge_world_axis(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	int axis = (int)wrenGetSlotDouble(vm, 2);
	float* vector = node ? &node->world_position.x : NULL;
	float value = 0.0f;

	if (vector && axis >= 0 && axis <= 2) {
		value = vector[axis];
	}

	wrenSetSlotDouble(vm, 0, (double)value);
}

static void wren_vm_bridge_visible(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	wrenSetSlotBool(vm, 0, node ? node->visible != 0 : false);
}

static void wren_vm_bridge_set_visible(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	if (node) {
		node->visible = wrenGetSlotBool(vm, 2) ? 1 : 0;
	}
}

static void wren_vm_bridge_look_at(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	if (!node) {
		return;
	}

	float dx = (float)wrenGetSlotDouble(vm, 2) - node->world_position.x;
	float dz = (float)wrenGetSlotDouble(vm, 4) - node->world_position.z;
	node->transform.rotation.y = atan2f(dx, dz);
}

static void wren_vm_world_set_fog(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->engine) {
		return;
	}

	render_config_t* config = &state->engine->config;
	float near = (float)wrenGetSlotDouble(vm, 1);
	float far = (float)wrenGetSlotDouble(vm, 2);
	if (near < 0.0f) {
		near = 0.0f;
	}

	if (far <= near) {
		far = near + 0.001f;
	}

	config->fog_near = near;
	config->fog_far = far;

	for (int i = 0; i < 3; i++) {
		int value = (int)wrenGetSlotDouble(vm, 3 + i);
		if (value < 0) {
			value = 0;
		}

		if (value > 255) {
			value = 255;
		}

		config->fog_color_rgb[i] = value;
	}
}

static void wren_vm_world_set_sky(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->engine) {
		return;
	}

	render_config_t* config = &state->engine->config;
	for (int i = 0; i < 3; i++) {
		int top = (int)wrenGetSlotDouble(vm, 1 + i);
		int bottom = (int)wrenGetSlotDouble(vm, 4 + i);
		if (top < 0) {
			top = 0;
		}

		if (top > 255) {
			top = 255;
		}

		if (bottom < 0) {
			bottom = 0;
		}

		if (bottom > 255) {
			bottom = 255;
		}

		config->sky_top_rgb[i] = top;
		config->sky_bottom_rgb[i] = bottom;
	}
}

static void wren_vm_world_set_light(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->engine) {
		return;
	}

	render_config_t* config = &state->engine->config;
	for (int i = 0; i < 3; i++) {
		config->light_direction[i] = (float)wrenGetSlotDouble(vm, 1 + i);
		float color = (float)wrenGetSlotDouble(vm, 4 + i);
		config->light_color[i] = color < 0.0f ? 0.0f : color;
	}
}

static void wren_vm_world_set_affine(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (!state || !state->engine) {
		return;
	}

	float value = (float)wrenGetSlotDouble(vm, 1);
	if (value < 0.0f) {
		value = 0.0f;
	}

	if (value > 1.0f) {
		value = 1.0f;
	}

	state->engine->config.affine_strength = value;
}

static void wren_vm_world_set_dither(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine) {
		state->engine->config.dither_enabled = wrenGetSlotBool(vm, 1) ? 1 : 0;
	}
}

static void wren_vm_world_set_fog_enabled(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine) {
		state->engine->config.fog_enabled = wrenGetSlotBool(vm, 1) ? 1 : 0;
	}
}

static void wren_vm_world_time(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	wrenSetSlotDouble(vm, 0, state && state->engine ? (double)state->engine->clock.elapsed : 0.0);
}

static void wren_vm_physics_move(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	node_t* node = wren_vm_node_from_slot(vm, 1);
	if (!state || !state->scene || !node) {
		wrenSetSlotBool(vm, 0, false);

		return;
	}

	vec3_t delta = {
		(float)wrenGetSlotDouble(vm, 2),
		(float)wrenGetSlotDouble(vm, 3),
		(float)wrenGetSlotDouble(vm, 4)
	};
	float radius = (float)wrenGetSlotDouble(vm, 5);
	vec3_t resolved = collision_move_sphere(state->scene, node->id, node->world_position, delta, radius);

	node->transform.position.x += resolved.x - node->world_position.x;
	node->transform.position.y += resolved.y - node->world_position.y;
	node->transform.position.z += resolved.z - node->world_position.z;
	wrenSetSlotBool(vm, 0, true);
}

static camera_t* wren_vm_camera_from_slot(WrenVM* vm) {
	node_t* node = wren_vm_node_from_slot(vm, 1);
	if (!node || !(node->components & NB_NODE_COMPONENT_CAMERA)) {
		return NULL;
	}

	return &node->camera;
}

static void wren_vm_camera_set_scalar(WrenVM* vm) {
	camera_t* camera = wren_vm_camera_from_slot(vm);
	int kind = (int)wrenGetSlotDouble(vm, 2);
	float value = (float)wrenGetSlotDouble(vm, 3);
	if (camera) {
		if (kind == 0) {
			camera->fov = value;
		}

		else if (kind == 1) {
			camera->near_plane = value;
		}

		else if (kind == 2) {
			camera->far_plane = value;
		}
	}
}

static void wren_vm_camera_use(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	entity_id_t id = (entity_id_t)wrenGetSlotDouble(vm, 1);
	int used = state && state->scene && scene_set_active_camera(state->scene, id);
	wrenSetSlotBool(vm, 0, used ? true : false);
}

static void wren_vm_input_mouse_dx(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int value = state && state->engine ? state->engine->input.mouse_dx : 0;
	wrenSetSlotDouble(vm, 0, (double)value);
}

static void wren_vm_input_mouse_dy(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int value = state && state->engine ? state->engine->input.mouse_dy : 0;
	wrenSetSlotDouble(vm, 0, (double)value);
}

static void wren_vm_input_mouse_captured(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int captured = state && state->engine ? platform_mouse_capture(&state->engine->platform) : 0;
	wrenSetSlotBool(vm, 0, captured ? true : false);
}

static void wren_vm_input_set_mouse_captured(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int captured = wrenGetSlotBool(vm, 1) ? 1 : 0;
	if (state && state->engine) {
		platform_set_mouse_capture(&state->engine->platform, captured);
	}
}

static void wren_vm_input_is_pressed(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int pressed = 0;
	if (state && state->engine) {
		pressed = input_is_held(&state->engine->input, wrenGetSlotString(vm, 1));
	}
	wrenSetSlotBool(vm, 0, pressed ? true : false);
}

static void wren_vm_input_was_pressed(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int pressed = 0;
	if (state && state->engine) {
		pressed = input_was_pressed(&state->engine->input, wrenGetSlotString(vm, 1));
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

static void wren_vm_audio_loop(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int played = 0;
	if (state && state->engine && state->engine->audio_ready) {
		const char* name = wrenGetSlotString(vm, 1);
		float volume = (float)wrenGetSlotDouble(vm, 2);
		audio_id id = resource_find_audio(&state->engine->resources, name);
		if (id != NB_RESOURCE_INVALID_ID && audio_play_ex(&state->engine->audio, &state->engine->resources, id, volume, 1)) {
			played = 1;
		}
	}

	wrenSetSlotBool(vm, 0, played ? true : false);
}

static void wren_vm_audio_stop(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int stopped = 0;
	if (state && state->engine && state->engine->audio_ready) {
		stopped = audio_stop(&state->engine->audio, &state->engine->resources, wrenGetSlotString(vm, 1));
	}

	wrenSetSlotBool(vm, 0, stopped ? true : false);
}

static void wren_vm_audio_set_volume(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	int updated = 0;
	if (state && state->engine && state->engine->audio_ready) {
		updated = audio_set_volume(&state->engine->audio, &state->engine->resources, wrenGetSlotString(vm, 1), (float)wrenGetSlotDouble(vm, 2));
	}

	wrenSetSlotBool(vm, 0, updated ? true : false);
}

static void wren_vm_audio_stop_all(WrenVM* vm) {
	wren_vm_t* state = wren_vm_from_wren(vm);
	if (state && state->engine && state->engine->audio_ready) {
		audio_stop_all(&state->engine->audio);
	}
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

static void wren_vm_module_complete(WrenVM* vm, const char* name, WrenLoadModuleResult result) {
	(void)vm;
	(void)name;
	if (result.source) {
		free((void*)result.source);
	}
}

static char* wren_vm_read_source(const wren_vm_t* state, const char* path) {
	unsigned char* data = NULL;
	size_t size = 0;
	const asset_db_t* db = state && state->engine ? &state->engine->assets : NULL;
	if (!asset_db_read_all(db, path, &data, &size)) {
		return NULL;
	}

	char* source = script_load_mem(data, size);
	free(data);

	return source;
}

static void wren_vm_scene_dir(const wren_vm_t* state, char* out, size_t size) {
	const char* scene_path = "assets";
	const char* slash;

	if (!out || size == 0) {
		return;
	}

	out[0] = '\0';
	if (state && state->engine && state->engine->config.scene_path[0]) {
		scene_path = state->engine->config.scene_path;
	}

	slash = strrchr(scene_path, '/');
	if (!slash) {
		snprintf(out, size, "assets");

		return;
	}

	if ((size_t)(slash - scene_path) >= size) {
		snprintf(out, size, "assets");

		return;
	}

	memcpy(out, scene_path, (size_t)(slash - scene_path));
	out[slash - scene_path] = '\0';
}

static WrenLoadModuleResult wren_vm_load_module(WrenVM* vm, const char* name) {
	WrenLoadModuleResult result;
	wren_vm_t* state = wren_vm_from_wren(vm);
	memset(&result, 0, sizeof(result));
	if (!name) {
		return result;
	}

	if (strcmp(name, NB_WREN_MODULE) == 0) {
		result.source = wren_builtin_source;

		return result;
	}

	char path[512];
	char* source = NULL;
	if (strchr(name, '/') || strchr(name, '.')) {
		source = wren_vm_read_source(state, name);
		if (!source) {
			snprintf(path, sizeof(path), "%s.wren", name);
			source = wren_vm_read_source(state, path);
		}
	}

	else {
		char scene_dir[256];
		wren_vm_scene_dir(state, scene_dir, sizeof(scene_dir));
		snprintf(path, sizeof(path), "%s/%s.wren", scene_dir, name);
		source = wren_vm_read_source(state, path);

		if (!source) {
			snprintf(path, sizeof(path), "assets/%s.wren", name);
			source = wren_vm_read_source(state, path);
		}

		if (!source) {
			snprintf(path, sizeof(path), "%s.wren", name);
			source = wren_vm_read_source(state, path);
		}
	}

	if (!source) {
		return result;
	}

	result.source = source;
	result.onComplete = wren_vm_module_complete;

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

		if (strcmp(signature, "world_axis(_,_)") == 0) {
			return wren_vm_bridge_world_axis;
		}

		if (strcmp(signature, "visible(_)") == 0) {
			return wren_vm_bridge_visible;
		}

		if (strcmp(signature, "set_visible(_,_)") == 0) {
			return wren_vm_bridge_set_visible;
		}

		if (strcmp(signature, "look_at(_,_,_,_)") == 0) {
			return wren_vm_bridge_look_at;
		}
	}

	if (strcmp(class_name, "CameraBridge") == 0 && is_static) {
		if (strcmp(signature, "axis(_,_,_)") == 0) {
			return wren_vm_bridge_axis;
		}

		if (strcmp(signature, "set_axis(_,_,_,_)") == 0) {
			return wren_vm_bridge_set_axis;
		}

		if (strcmp(signature, "set(_,_,_,_,_)") == 0) {
			return wren_vm_bridge_set;
		}

		if (strcmp(signature, "set_scalar(_,_,_)") == 0) {
			return wren_vm_camera_set_scalar;
		}

		if (strcmp(signature, "use(_)") == 0) {
			return wren_vm_camera_use;
		}
	}

	if (strcmp(class_name, "Input") == 0 && is_static) {
		if (strcmp(signature, "mouse_dx()") == 0) {
			return wren_vm_input_mouse_dx;
		}

		if (strcmp(signature, "mouse_dy()") == 0) {
			return wren_vm_input_mouse_dy;
		}

		if (strcmp(signature, "mouse_captured()") == 0) {
			return wren_vm_input_mouse_captured;
		}

		if (strcmp(signature, "set_mouse_captured(_)") == 0) {
			return wren_vm_input_set_mouse_captured;
		}

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

	if (strcmp(class_name, "Audio") == 0 && is_static) {
		if (strcmp(signature, "play(_)") == 0) {
			return wren_vm_audio_play;
		}

		if (strcmp(signature, "loop(_,_)") == 0) {
			return wren_vm_audio_loop;
		}

		if (strcmp(signature, "stop(_)") == 0) {
			return wren_vm_audio_stop;
		}

		if (strcmp(signature, "set_volume(_,_)") == 0) {
			return wren_vm_audio_set_volume;
		}

		if (strcmp(signature, "stop_all()") == 0) {
			return wren_vm_audio_stop_all;
		}
	}

	if (strcmp(class_name, "World") == 0 && is_static) {
		if (strcmp(signature, "set_fog(_,_,_,_,_)") == 0) {
			return wren_vm_world_set_fog;
		}

		if (strcmp(signature, "set_sky(_,_,_,_,_,_)") == 0) {
			return wren_vm_world_set_sky;
		}

		if (strcmp(signature, "set_light(_,_,_,_,_,_)") == 0) {
			return wren_vm_world_set_light;
		}

		if (strcmp(signature, "set_affine(_)") == 0) {
			return wren_vm_world_set_affine;
		}

		if (strcmp(signature, "set_dither(_)") == 0) {
			return wren_vm_world_set_dither;
		}

		if (strcmp(signature, "set_fog_enabled(_)") == 0) {
			return wren_vm_world_set_fog_enabled;
		}

		if (strcmp(signature, "time()") == 0) {
			return wren_vm_world_time;
		}
	}

	if (strcmp(class_name, "Physics") == 0 && is_static && strcmp(signature, "move(_,_,_,_,_)") == 0) {
		return wren_vm_physics_move;
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
	nb_array_free(vm->instances);
	nb_array_free(vm->instance_ids);
	memset(vm, 0, sizeof(*vm));
}

void wren_vm_bind_scene(wren_vm_t* vm, scene_t* scene) {
	if (vm) {
		vm->scene = scene;
	}
}

int wren_vm_attach(wren_vm_t* vm, entity_id_t id, const char* module, const char* source) {
	if (!vm || !vm->ready || !module || !*module || !source || !*source) {
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

	void* instances = nb_array_grow(vm->instances, &vm->instance_capacity, vm->instance_count + 1, sizeof(*vm->instances));
	if (!instances) {
		return 0;
	}

	void* instance_ids = nb_array_grow(vm->instance_ids, &vm->instance_id_capacity, vm->instance_count + 1, sizeof(*vm->instance_ids));
	if (!instance_ids) {
		return 0;
	}

	vm->instances = instances;
	vm->instance_ids = instance_ids;

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
