#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/engine.h"
#include "scene/demo.h"

const char* nb_error_string(nb_result_t result) {
	if (result == NB_RESULT_OK) { return "ok"; }

	else if (result == NB_RESULT_HELP) { return "help"; }

	else if (result == NB_RESULT_ERROR_INVALID_ARGUMENT) { return "invalid argument"; }

	else if (result == NB_RESULT_ERROR_CONFIG) { return "configuration error"; }

	else if (result == NB_RESULT_ERROR_PLATFORM) { return "platform error"; }

	else if (result == NB_RESULT_ERROR_RENDERER) { return "renderer error"; }

	else if (result == NB_RESULT_ERROR_RESOURCE) { return "resource error"; }

	else if (result == NB_RESULT_ERROR_SCENE) { return "scene error"; }

	return "unknown error";
}

void engine_log(engine_t* engine, const char* format, ...) {
	if (!engine || !format) { return; }

	va_list args;
	va_start(args, format);
	vsnprintf(engine->log.last_message, sizeof(engine->log.last_message), format, args);
	va_end(args);

	fprintf(stderr, "%s\n", engine->log.last_message);
	engine->log.message_count++;
}

nb_result_t engine_init(engine_t* engine, int argc, char** argv) {
	if (!engine) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	memset(engine, 0, sizeof(*engine));

	render_config_init(&engine->config);
	if (!config_load_file(&engine->config, "config.ini")) { return NB_RESULT_ERROR_CONFIG; }

	int cli_status = config_apply_cli(&engine->config, argc, argv);
	if (cli_status == 2) { return NB_RESULT_HELP; }
	if (cli_status == 0) { return NB_RESULT_ERROR_CONFIG; }

	platform_desc_t platform_desc = { "NodeBench", engine->config.width, engine->config.height, 3, 0 };
	if (!platform_init(&engine->platform, &platform_desc)) { return NB_RESULT_ERROR_PLATFORM; }
	engine->platform_ready = 1;

	if (!framebuffer_init(&engine->framebuffer, &engine->config)) { return NB_RESULT_ERROR_RENDERER; }
	engine->framebuffer_ready = 1;

	if (!renderer_init(&engine->renderer, &engine->framebuffer)) { return NB_RESULT_ERROR_RENDERER; }
	engine->renderer_ready = 1;

	resource_store_init(&engine->resources, engine->config.texture_vram_limit);
	engine->resources_ready = 1;

	return NB_RESULT_OK;
}

static void engine_poll_events(engine_t* engine) {
	if (!engine) { return; }

	platform_pump_events(&engine->platform, &engine->input.quit);

	platform_key_t key;
	while (platform_pop_key(&engine->platform, &key)) {
		if (engine->input.key_count < NB_ENGINE_INPUT_CAPACITY) {
			engine->input.keys[engine->input.key_count++] = key;
		}
	}
}

static void engine_apply_key(engine_t* engine, platform_key_t key) {
	if (!engine) { return; }

	if (key == NB_KEY_1) {
		engine->config.affine_strength = engine->config.affine_strength > 0.5f ? 0.0f : 1.0f;
	}

	else if (key == NB_KEY_2) {
		engine->config.dither_enabled = !engine->config.dither_enabled;
	}

	else if (key == NB_KEY_3) {
		engine->config.fog_enabled = !engine->config.fog_enabled;
	}

	else if (key == NB_KEY_4) {
		engine->config.cull_backfaces = !engine->config.cull_backfaces;
	}

	else if (key == NB_KEY_5) {
		engine->config.reject_near_triangles = !engine->config.reject_near_triangles;
	}

	else if (key == NB_KEY_R && engine->scene.reset) {
		engine->scene.reset(engine, engine->scene.context);
	}

	engine_log(engine, "affine=%.2f dither=%d fog=%d cull=%d clip=%d", engine->config.affine_strength, engine->config.dither_enabled, engine->config.fog_enabled, engine->config.cull_backfaces, engine->config.reject_near_triangles);
}

void engine_update(engine_t* engine, float dt) {
	if (!engine) { return; }

	for (int i = 0; i < engine->input.key_count; i++) {
		engine_apply_key(engine, engine->input.keys[i]);
	}
	engine->input.key_count = 0;

	if (engine->scene.update) { engine->scene.update(engine, engine->scene.context, dt); }
}

void engine_render(engine_t* engine, float alpha) {
	if (!engine || !engine->platform_ready || !engine->framebuffer_ready || !engine->renderer_ready) { return; }

	if (engine->scene.render) { engine->scene.render(engine, engine->scene.context, alpha); }

	uint32_t* output = platform_pixels(&engine->platform);
	framebuffer_resolve(&engine->framebuffer, output);
	platform_present(&engine->platform);
}

void engine_shutdown(engine_t* engine) {
	if (!engine) { return; }

	if (engine->scene_ready && engine->scene.shutdown) { engine->scene.shutdown(engine, engine->scene.context); }

	if (engine->renderer_ready) { renderer_shutdown(&engine->renderer); }
	if (engine->framebuffer_ready) { framebuffer_shutdown(&engine->framebuffer); }
	if (engine->platform_ready) { platform_shutdown(&engine->platform); }
	if (engine->resources_ready) { resource_store_shutdown(&engine->resources); }

	engine->scene_ready = 0;
	engine->resources_ready = 0;
	engine->renderer_ready = 0;
	engine->framebuffer_ready = 0;
	engine->platform_ready = 0;
}

nb_result_t engine_run(int argc, char** argv) {
	engine_t engine;
	nb_result_t result = engine_init(&engine, argc, argv);
	if (result == NB_RESULT_HELP) { return NB_RESULT_OK; }
	if (result != NB_RESULT_OK) {
		engine_shutdown(&engine);
		return result;
	}

	demo_scene_register(&engine);
	if (!engine.scene.init) {
		engine_log(&engine, "scene is not registered");
		engine_shutdown(&engine);
		return NB_RESULT_ERROR_SCENE;
	}

	result = engine.scene.init(&engine, &engine.scene.context);
	if (result != NB_RESULT_OK) {
		engine_log(&engine, "scene init failed: %s", nb_error_string(result));
		engine_shutdown(&engine);
		return result;
	}
	engine.scene_ready = 1;

	engine_log(&engine, "affine=%.2f dither=%d fog=%d cull=%d clip=%d", engine.config.affine_strength, engine.config.dither_enabled, engine.config.fog_enabled, engine.config.cull_backfaces, engine.config.reject_near_triangles);

	while (!engine.input.quit) {
		float dt = platform_delta_time(&engine.platform);
		if (dt > 0.25f) { dt = 0.25f; }

		engine_poll_events(&engine);
		if (engine.input.quit) { break; }

		engine.clock.elapsed += dt;
		engine.clock.accumulator += dt;

		while (engine.clock.accumulator >= NB_ENGINE_FIXED_STEP) {
			engine_update(&engine, NB_ENGINE_FIXED_STEP);
			engine.clock.time += NB_ENGINE_FIXED_STEP;
			engine.clock.accumulator -= NB_ENGINE_FIXED_STEP;
		}

		engine.clock.alpha = engine.clock.accumulator / NB_ENGINE_FIXED_STEP;
		engine_render(&engine, engine.clock.alpha);
	}

	engine_shutdown(&engine);

	return NB_RESULT_OK;
}
