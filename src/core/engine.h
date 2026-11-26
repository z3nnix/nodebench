#ifndef NB_CORE_ENGINE_H
#define NB_CORE_ENGINE_H

#include "core/config.h"
#include "core/input.h"
#include "core/log.h"
#include "core/platform.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "resource/resource.h"
#include "ui/hud.h"

#define NB_ENGINE_FIXED_STEP 0.016666667f

typedef enum {
	NB_RESULT_OK = 0,
	NB_RESULT_HELP,
	NB_RESULT_ERROR_INVALID_ARGUMENT,
	NB_RESULT_ERROR_CONFIG,
	NB_RESULT_ERROR_PLATFORM,
	NB_RESULT_ERROR_RENDERER,
	NB_RESULT_ERROR_RESOURCE,
	NB_RESULT_ERROR_SCENE
} nb_result_t;

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
	platform_t platform;
	framebuffer_t framebuffer;
	renderer_t renderer;
	resource_store_t resources;
	engine_scene_t scene;
	input_state_t input;
	engine_clock_t clock;
	hud_t hud;
	log_t log;
	int paused;
	int platform_ready;
	int framebuffer_ready;
	int renderer_ready;
	int resources_ready;
	int scene_ready;
} engine_t;

const char* nb_error_string(nb_result_t result);
void engine_log(engine_t* engine, const char* format, ...);
nb_result_t engine_init(engine_t* engine, int argc, char** argv);
void engine_update(engine_t* engine, float dt);
void engine_render(engine_t* engine, float alpha);
void engine_shutdown(engine_t* engine);
nb_result_t engine_run(int argc, char** argv);

#endif
