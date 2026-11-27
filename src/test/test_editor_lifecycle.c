#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asset/asset_db.h"
#include "core/engine.h"
#include "editor/editor_app.h"
#include "script/wren_vm.h"

static const char* editor_probe_source =
	"import \"nodebench\" for Node3D\n"
	"var Ticks = 0\n"
	"class EditorProbe is Node3D {\n"
	"  construct new(id) {\n"
	"    super(id)\n"
	"  }\n"
	"  init() {\n"
	"    Ticks = 0\n"
	"  }\n"
	"  update(dt) {\n"
	"    Ticks = Ticks + 1\n"
	"  }\n"
	"  on_input(action) {\n"
	"    return\n"
	"  }\n"
	"  on_collision(other) {\n"
	"    return\n"
	"  }\n"
	"}\n";

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
	snprintf(engine.config.mode, sizeof(engine.config.mode), "%s", "editor");
	resource_store_init(&engine.resources, 1048576);
	engine.resources_ready = 1;
	if (!asset_db_init(&engine.assets, ".")) {
		log_shutdown(&engine.log);
		return 1;
	}
	engine.assets_ready = 1;
	if (!wren_vm_init(&engine.script_vm, &engine)) {
		asset_db_shutdown(&engine.assets);
		log_shutdown(&engine.log);
		return 1;
	}
	engine.script_vm_ready = 1;
	scene_doc_init(&engine.scene_doc);
	snprintf(engine.config.scene_path, sizeof(engine.config.scene_path), "%s", "scenes/level01.tscn");
	editor_app_register(&engine);

	void* context = NULL;
	int ok = engine.scene.init && engine.scene.init(&engine, &context) == NB_RESULT_OK;
	if (ok) {
		ok = engine.scene_graph && engine.scene_graph->entity_count > 0 && engine.script_vm.instance_count == 0;
	}

	if (ok && engine.scene_graph->node_count > 0) {
		input_init(&engine.input, &engine.config);
		collision_state_init(&engine.collisions);
		wren_vm_bind_scene(&engine.script_vm, engine.scene_graph);
		ok = wren_vm_attach(&engine.script_vm, engine.scene_graph->nodes[0].id, "editor_probe", editor_probe_source);
		if (ok) {
			wren_vm_init_instances(&engine.script_vm);
			engine_update(&engine, 0.25f);
			wrenEnsureSlots(engine.script_vm.vm, 1);
			wrenGetVariable(engine.script_vm.vm, "editor_probe", "Ticks", 0);
			ok = wrenGetSlotDouble(engine.script_vm.vm, 0) == 0.0;
		}
	}

	const char* uuid_before = asset_db_uuid(&engine.assets, "res://scripts/player.wren");
	asset_db_shutdown(&engine.assets);
	engine.assets_ready = asset_db_init(&engine.assets, ".") ? 1 : 0;
	const char* uuid_after = asset_db_uuid(&engine.assets, "res://scripts/player.wren");
	if (!uuid_before || !uuid_after || strcmp(uuid_before, uuid_after) != 0) {
		ok = 0;
	}

	if (engine.scene.shutdown) {
		engine.scene.shutdown(&engine, context);
	}
	wren_vm_shutdown(&engine.script_vm);
	asset_db_shutdown(&engine.assets);
	resource_store_shutdown(&engine.resources);
	scene_doc_clear(&engine.scene_doc);
	log_shutdown(&engine.log);

	if (!ok) {
		printf("fail editor lifecycle\n");
		return 1;
	}

	printf("editor lifecycle test ok\n");

	return 0;
}
