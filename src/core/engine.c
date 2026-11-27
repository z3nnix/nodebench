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
	log_vwrite(&engine->log, NB_LOG_INFO, format, args);
	va_end(args);
}

nb_result_t engine_init(engine_t* engine, int argc, char** argv) {
	if (!engine) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	memset(engine, 0, sizeof(*engine));

	render_config_init(&engine->config);
	if (!config_load_file(&engine->config, "config.ini")) { return NB_RESULT_ERROR_CONFIG; }

	int cli_status = config_apply_cli(&engine->config, argc, argv);
	if (cli_status == 2) { return NB_RESULT_HELP; }
	if (cli_status == 0) { return NB_RESULT_ERROR_CONFIG; }

	if (!log_init(&engine->log, engine->config.log_file, engine->config.log_level)) { return NB_RESULT_ERROR_CONFIG; }
	hud_init(&engine->hud);
	input_init(&engine->input, &engine->config);

	platform_desc_t platform_desc = { "NodeBench", engine->config.width, engine->config.height, 3, 0 };
	if (!platform_init(&engine->platform, &platform_desc)) { return NB_RESULT_ERROR_PLATFORM; }
	engine->platform_ready = 1;

	if (!framebuffer_init(&engine->framebuffer, &engine->config)) { return NB_RESULT_ERROR_RENDERER; }
	engine->framebuffer_ready = 1;

	if (!renderer_init(&engine->renderer, &engine->framebuffer)) { return NB_RESULT_ERROR_RENDERER; }
	engine->renderer_ready = 1;

	resource_store_init(&engine->resources, engine->config.texture_vram_limit);
	engine->resources_ready = 1;

	if (!audio_init(&engine->audio)) {
		engine_log(engine, "audio unavailable");
	}

	else {
		engine->audio_ready = 1;
	}

	collision_state_init(&engine->collisions);

	if (!wren_vm_init(&engine->script_vm, engine)) { return NB_RESULT_ERROR_SCENE; }
	engine->script_vm_ready = 1;

	return NB_RESULT_OK;
}

static void engine_log_state(engine_t* engine) {
	if (!engine) { return; }

	engine_log(engine, "affine=%.2f dither=%d fog=%d cull=%d clip=%d wire=%d xray=%d hud=%d overlay=%d paused=%d", engine->config.affine_strength, engine->config.dither_enabled, engine->config.fog_enabled, engine->config.cull_backfaces, engine->config.reject_near_triangles, engine->config.wireframe_enabled, engine->config.xray_enabled, engine->config.hud_enabled, engine->config.hud_debug_enabled, engine->paused);
}

static void engine_cycle_debug(engine_t* engine) {
	if (!engine) { return; }

	if (!engine->config.wireframe_enabled && !engine->config.xray_enabled) {
		engine->config.wireframe_enabled = 1;
	}

	else if (engine->config.wireframe_enabled && !engine->config.xray_enabled) {
		engine->config.wireframe_enabled = 0;
		engine->config.xray_enabled = 1;
	}

	else {
		engine->config.wireframe_enabled = 0;
		engine->config.xray_enabled = 0;
	}
}

static void engine_apply_actions(engine_t* engine) {
	if (!engine) { return; }

	input_state_t* input = &engine->input;
	int changed = 0;

	if (input_was_pressed(input, "quit")) { input->quit = 1; }

	if (input_was_pressed(input, "pause")) {
		engine->paused = !engine->paused;
		changed = 1;
	}

	if (input_was_pressed(input, "debug_cycle")) {
		engine_cycle_debug(engine);
		changed = 1;
	}

	if (input_was_pressed(input, "debug_wireframe")) {
		engine->config.wireframe_enabled = !engine->config.wireframe_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "debug_xray")) {
		engine->config.xray_enabled = !engine->config.xray_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "hud")) {
		engine->config.hud_enabled = !engine->config.hud_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "hud_debug")) {
		engine->config.hud_debug_enabled = !engine->config.hud_debug_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "toggle_affine")) {
		engine->config.affine_strength = engine->config.affine_strength > 0.5f ? 0.0f : 1.0f;
		changed = 1;
	}

	if (input_was_pressed(input, "toggle_dither")) {
		engine->config.dither_enabled = !engine->config.dither_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "toggle_fog")) {
		engine->config.fog_enabled = !engine->config.fog_enabled;
		changed = 1;
	}

	if (input_was_pressed(input, "toggle_cull")) {
		engine->config.cull_backfaces = !engine->config.cull_backfaces;
		changed = 1;
	}

	if (input_was_pressed(input, "toggle_clip")) {
		engine->config.reject_near_triangles = !engine->config.reject_near_triangles;
		changed = 1;
	}

	if (input_was_pressed(input, "reset") && engine->scene.reset) {
		engine->scene.reset(engine, engine->scene.context);
		collision_state_init(&engine->collisions);
		audio_stop_all(&engine->audio);
		changed = 1;
	}

	if (changed) { engine_log_state(engine); }
}

void engine_update(engine_t* engine, float dt) {
	if (!engine) { return; }

	engine_apply_actions(engine);

	if (engine->script_vm_ready) {
		for (int i = 0; i < input_action_count(&engine->input); i++) {
			const char* action = input_action_at(&engine->input, i);
			if (input_was_pressed(&engine->input, action)) { wren_vm_on_input(&engine->script_vm, action); }
		}

		if (!engine->paused) {
			wren_vm_update(&engine->script_vm, dt);
		}
	}

	if (engine->scene.update) { engine->scene.update(engine, engine->scene.context, dt); }

	if (!engine->paused && engine->scene_graph && engine->script_vm_ready) {
		collision_events_t events;
		if (collision_update(engine->scene_graph, &engine->collisions, &events)) {
			for (int i = 0; i < events.count; i++) {
				wren_vm_on_collision(&engine->script_vm, events.events[i].left, events.events[i].right);
			}
		}
	}

	input_clear_transient(&engine->input);
}

void engine_render(engine_t* engine, float alpha) {
	if (!engine || !engine->platform_ready || !engine->framebuffer_ready || !engine->renderer_ready) { return; }

	if (engine->scene.render) { engine->scene.render(engine, engine->scene.context, alpha); }

	hud_draw(&engine->hud, &engine->framebuffer, &engine->config, engine->paused);

	uint32_t* output = platform_pixels(&engine->platform);
	framebuffer_resolve(&engine->framebuffer, output);
	platform_present(&engine->platform);
}

void engine_shutdown(engine_t* engine) {
	if (!engine) { return; }

	if (engine->script_vm_ready) { wren_vm_shutdown(&engine->script_vm); }
	engine->script_vm_ready = 0;

	if (engine->scene_ready && engine->scene.shutdown) { engine->scene.shutdown(engine, engine->scene.context); }
	engine->scene_graph = NULL;

	if (engine->audio_ready) { audio_shutdown(&engine->audio); }
	engine->audio_ready = 0;

	if (engine->renderer_ready) { renderer_shutdown(&engine->renderer); }
	if (engine->framebuffer_ready) { framebuffer_shutdown(&engine->framebuffer); }
	if (engine->platform_ready) { platform_shutdown(&engine->platform); }
	if (engine->resources_ready) { resource_store_shutdown(&engine->resources); }

	engine->scene_ready = 0;
	engine->resources_ready = 0;
	engine->renderer_ready = 0;
	engine->framebuffer_ready = 0;
	engine->platform_ready = 0;
	log_shutdown(&engine->log);
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

	engine_log_state(&engine);

	int frames = 0;
	while (!engine.input.quit) {
		float dt = platform_delta_time(&engine.platform);
		if (dt > 0.25f) { dt = 0.25f; }

		platform_pump_events(&engine.platform, &engine.input.quit);
		input_update(&engine.input, &engine.platform);
		if (engine.input.quit) { break; }

		engine.clock.elapsed += dt;
		engine.clock.accumulator += dt;

		while (engine.clock.accumulator >= NB_ENGINE_FIXED_STEP) {
			engine_update(&engine, NB_ENGINE_FIXED_STEP);
			engine.clock.time += NB_ENGINE_FIXED_STEP;
			engine.clock.accumulator -= NB_ENGINE_FIXED_STEP;
		}

		engine.clock.alpha = engine.clock.accumulator / NB_ENGINE_FIXED_STEP;
		hud_update(&engine.hud, dt, engine.renderer.draw_calls, engine.renderer.drawn_triangles, engine.renderer.culled_triangles, engine.renderer.near_culled_triangles, resource_vram_used(&engine.resources));
		engine_render(&engine, engine.clock.alpha);

		frames++;
		if (engine.config.max_frames > 0 && frames >= engine.config.max_frames) {
			break;
		}
	}

	engine_shutdown(&engine);

	return NB_RESULT_OK;
}
