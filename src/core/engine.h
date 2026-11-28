#ifndef NB_CORE_ENGINE_H
#define NB_CORE_ENGINE_H

#include "asset/asset_db.h"
#include "audio/audio.h"
#include "core/config.h"
#include "core/input.h"
#include "core/log.h"
#include "core/platform.h"
#include "editor/editor.h"
#include "editor/launcher.h"
#include "physics/collision.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "resource/resource.h"
#include "script/wren_vm.h"
#include "scene/scene.h"
#include "scene/scene_doc.h"
#include "ui/hud.h"
#include "core/result.h"

#define NB_ENGINE_FIXED_STEP 0.016666667f

typedef struct engine_t engine_t;

typedef nb_result_t (*engine_scene_init_fn)(engine_t* engine, void** context);
typedef void (*engine_scene_update_fn)(engine_t* engine, void* context, float dt);
typedef void (*engine_scene_render_fn)(engine_t* engine, void* context, float alpha);
typedef void (*engine_scene_reset_fn)(engine_t* engine, void* context);
typedef void (*engine_scene_shutdown_fn)(engine_t* engine, void* context);

typedef struct {
	void* context;
	engine_scene_init_fn init;
	engine_scene_update_fn update;
	engine_scene_render_fn render;
	engine_scene_reset_fn reset;
	engine_scene_shutdown_fn shutdown;
} engine_scene_t;

typedef struct {
	float accumulator;
	float time;
	float elapsed;
	float alpha;
} engine_clock_t;

typedef struct engine_t {
	render_config_t config;
	asset_db_t assets;
	platform_t platform;
	framebuffer_t framebuffer;
	renderer_t renderer;
	resource_store_t resources;
	wren_vm_t script_vm;
	audio_system_t audio;
	collision_state_t collisions;
	engine_scene_t scene;
	scene_t* scene_graph;
	scene_t owned_scene;
	scene_doc_t scene_doc;
	input_state_t input;
	engine_clock_t clock;
	hud_t hud;
	editor_t editor;
	launcher_t launcher;
	log_t log;
	int assets_ready;
	int script_vm_ready;
	int audio_ready;
	int paused;
	int platform_ready;
	int framebuffer_ready;
	int renderer_ready;
	int resources_ready;
	int scene_ready;
	int editor_ready;
} engine_t;

const char* nb_error_string(nb_result_t result);
void engine_log(engine_t* engine, const char* format, ...);
nb_result_t engine_init(engine_t* engine, int argc, char** argv);
void engine_update(engine_t* engine, float dt);
void engine_render(engine_t* engine, float alpha);
void engine_shutdown(engine_t* engine);
nb_result_t engine_run(int argc, char** argv);

#endif
