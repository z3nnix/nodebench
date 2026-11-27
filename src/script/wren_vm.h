#ifndef NODEBENCH_SCRIPT_WREN_VM_H
#define NODEBENCH_SCRIPT_WREN_VM_H

#include "scene/scene.h"
#include "third_party/wren/wren.h"

typedef struct {
	WrenVM* vm;
	struct engine_t* engine;
	scene_t* scene;
	WrenHandle* node3d_class;
	WrenHandle* construct_instance;
	WrenHandle* call_init;
	WrenHandle* call_update;
	WrenHandle* call_on_input;
	WrenHandle* call_on_collision;
	WrenHandle** instances;
	int instance_capacity;
	entity_id_t* instance_ids;
	int instance_id_capacity;
	int instance_count;
	int ready;
} wren_vm_t;

int wren_vm_init(wren_vm_t* vm, struct engine_t* engine);
void wren_vm_shutdown(wren_vm_t* vm);
void wren_vm_bind_scene(wren_vm_t* vm, scene_t* scene);
int wren_vm_attach(wren_vm_t* vm, entity_id_t id, const char* module, const char* source);
void wren_vm_init_instances(wren_vm_t* vm);
void wren_vm_update(wren_vm_t* vm, float dt);
void wren_vm_on_input(wren_vm_t* vm, const char* action);
void wren_vm_on_collision(wren_vm_t* vm, entity_id_t left, entity_id_t right);

#endif
