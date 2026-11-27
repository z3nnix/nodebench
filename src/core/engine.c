#define _XOPEN_SOURCE 700

#include <SDL2/SDL.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/engine.h"
#include "editor/editor_app.h"
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

static void engine_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0) { return; }
	size_t half = size / 2 - 1;
	snprintf(out, size, "%.*s/%.*s", (int)half, a ? a : "", (int)half, b ? b : "");
}

static char* engine_absolute_cli_value(const char* cwd, const char* value) {
	if (!cwd || !value || !*value || value[0] == '/' || strncmp(value, "res://", 6) == 0) { return NULL; }
	char candidate[PATH_MAX];
	engine_join(candidate, sizeof(candidate), cwd, value);
	if (access(candidate, F_OK) != 0) { return NULL; }
	char resolved[PATH_MAX];
	if (!realpath(candidate, resolved)) { return NULL; }
	return strdup(resolved);
}

static int engine_is_path_key(const char* key) {
	return strcmp(key, "scene") == 0 || strcmp(key, "scene_path") == 0 || strcmp(key, "project") == 0 || strcmp(key, "project_root") == 0 || strcmp(key, "root") == 0;
}

static void engine_prepare_cli_paths(int argc, char** argv, const char* cwd, char** args, char** owned, int owned_cap, int* owned_count) {
	if (!argv || !args) { return; }
	for (int i = 0; i < argc; i++) { args[i] = argv[i]; }
	if (!owned || !owned_count) { return; }
	for (int i = 1; i < argc; i++) {
		if (!args[i]) { continue; }
		const char* key = NULL;
		const char* value = NULL;
		int value_index = -1;
		if (strncmp(args[i], "--scene=", 8) == 0) {
			key = "scene";
			value = args[i] + 8;
		}
		else if (strncmp(args[i], "--scene_path=", 13) == 0) {
			key = "scene_path";
			value = args[i] + 13;
		}
		else if (strncmp(args[i], "--project=", 10) == 0) {
			key = "project";
			value = args[i] + 10;
		}
		else if (strncmp(args[i], "--project_root=", 15) == 0) {
			key = "project_root";
			value = args[i] + 15;
		}
		else if (strncmp(args[i], "--root=", 7) == 0) {
			key = "root";
			value = args[i] + 7;
		}
		else if (i + 1 < argc && args[i + 1] && args[i + 1][0] != '-') {
			const char* exact = args[i];
			if (strcmp(exact, "--scene") == 0 || strcmp(exact, "--scene_path") == 0 || strcmp(exact, "--project") == 0 || strcmp(exact, "--project_root") == 0 || strcmp(exact, "--root") == 0) {
				key = exact + 2;
				value = args[i + 1];
				value_index = i + 1;
			}
		}
		if (!key || !engine_is_path_key(key) || !value) { continue; }
		char* absolute = engine_absolute_cli_value(cwd, value);
		if (!absolute || *owned_count >= owned_cap) {
			free(absolute);
			continue;
		}
		if (value_index >= 0) {
			args[value_index] = absolute;
		}
		else {
			size_t size = strlen(key) + strlen(absolute) + 4;
			char* combined = (char*)malloc(size);
			if (!combined) {
				free(absolute);
				continue;
			}
			snprintf(combined, size, "--%s=%s", key, absolute);
			free(absolute);
			args[i] = combined;
			absolute = combined;
		}
		owned[(*owned_count)++] = absolute;
	}
}

nb_result_t engine_init(engine_t* engine, int argc, char** argv) {
	if (!engine) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }

	memset(engine, 0, sizeof(*engine));

	render_config_init(&engine->config);

	char original_cwd[PATH_MAX];
	if (!getcwd(original_cwd, sizeof(original_cwd))) { snprintf(original_cwd, sizeof(original_cwd), "."); }
	char** args = (char**)malloc((size_t)(argc + 1) * sizeof(char*));
	char* owned[64];
	int owned_count = 0;
	if (args) {
		args[argc] = NULL;
		engine_prepare_cli_paths(argc, argv, original_cwd, args, owned, 64, &owned_count);
	}
	else { args = argv; }

	char root[PATH_MAX];
	root[0] = '\0';
	config_resolve_project_root(argc, args, root, sizeof(root));
	if (root[0] && chdir(root) != 0) {
		fprintf(stderr, "warning: cannot enter project root: %s\n", root);
		root[0] = '\0';
	}
	if (!root[0]) { getcwd(root, sizeof(root)); }
	snprintf(engine->config.project_root, sizeof(engine->config.project_root), "%.*s", (int)sizeof(engine->config.project_root) - 1, root);

	char config_path[PATH_MAX];
	engine_join(config_path, sizeof(config_path), root, "project.ini");
	if (access(config_path, F_OK) != 0) {
		fprintf(stderr, "warning: project.ini not found; using defaults\n");
	}
	if (!config_load_file(&engine->config, "project.ini")) {
		for (int i = 0; i < owned_count; i++) { free(owned[i]); }
		if (args != argv) { free(args); }
		return NB_RESULT_ERROR_CONFIG;
	}

	int cli_status = config_apply_cli(&engine->config, argc, args);
	for (int i = 0; i < owned_count; i++) { free(owned[i]); }
	if (args != argv) { free(args); }
	if (cli_status == 2) { return NB_RESULT_HELP; }
	if (cli_status == 0) { return NB_RESULT_ERROR_CONFIG; }

	if (!log_init(&engine->log, engine->config.log_file, engine->config.log_level)) { return NB_RESULT_ERROR_CONFIG; }
	hud_init(&engine->hud);
	input_init(&engine->input, &engine->config);

	platform_desc_t platform_desc = { "NodeBench", engine->config.width, engine->config.height, engine->config.window_width, engine->config.window_height, 0 };
	if (strcmp(engine->config.mode, "editor") == 0) {
		if (platform_desc.window_width < 1280) { platform_desc.window_width = 1280; }
		if (platform_desc.window_height < 720) { platform_desc.window_height = 720; }
	}
	else {
		if (platform_desc.window_width <= 0) { platform_desc.window_width = engine->config.width * 3; }
		if (platform_desc.window_height <= 0) { platform_desc.window_height = engine->config.height * 3; }
	}
	if (!platform_init(&engine->platform, &platform_desc)) { return NB_RESULT_ERROR_PLATFORM; }
	engine->platform_ready = 1;

	if (!framebuffer_init(&engine->framebuffer, &engine->config)) { return NB_RESULT_ERROR_RENDERER; }
	engine->framebuffer_ready = 1;

	if (!renderer_init(&engine->renderer, &engine->framebuffer)) { return NB_RESULT_ERROR_RENDERER; }
	engine->renderer_ready = 1;

	if (!editor_init(&engine->editor, engine)) {
		engine_log(engine, "editor unavailable");
	}

	else {
		engine->editor_ready = 1;
	}

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

	if (input_was_pressed(input, "editor_toggle") && engine->editor_ready) {
		engine->editor.enabled = !engine->editor.enabled;
		platform_set_mouse_capture_blocked(&engine->platform, engine->editor.enabled);
		if (engine->editor.enabled) {
			platform_set_mouse_capture(&engine->platform, 0);
		}
		else {
			platform_set_mouse_capture(&engine->platform, 1);
		}
		changed = 1;
	}

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
	SDL_Renderer* sdl_renderer = (SDL_Renderer*)platform_renderer(&engine->platform);
	SDL_SetRenderDrawColor(sdl_renderer, 24, 24, 28, 255);
	SDL_RenderClear(sdl_renderer);
	if (engine->editor_ready && editor_enabled(&engine->editor)) {
		platform_present_framebuffer_rect(&engine->platform, engine->editor.viewport_x, engine->editor.viewport_y, engine->editor.viewport_w, engine->editor.viewport_h);
		editor_render(&engine->editor, engine);
	}
	else {
		platform_present_framebuffer(&engine->platform);
	}
	SDL_RenderPresent(sdl_renderer);
}

void engine_shutdown(engine_t* engine) {
	if (!engine) { return; }

	if (engine->script_vm_ready) { wren_vm_shutdown(&engine->script_vm); }
	engine->script_vm_ready = 0;
	scene_doc_clear(&engine->scene_doc);

	if (engine->scene_ready && engine->scene.shutdown) { engine->scene.shutdown(engine, engine->scene.context); }
	engine->scene_graph = NULL;

	if (engine->audio_ready) { audio_shutdown(&engine->audio); }
	engine->audio_ready = 0;

	if (engine->editor_ready) { editor_shutdown(&engine->editor, engine); }
	if (engine->renderer_ready) { renderer_shutdown(&engine->renderer); }
	if (engine->framebuffer_ready) { framebuffer_shutdown(&engine->framebuffer); }
	if (engine->platform_ready) { platform_shutdown(&engine->platform); }
	if (engine->resources_ready) { resource_store_shutdown(&engine->resources); }

	engine->editor_ready = 0;
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

	engine_log(&engine, "mode=%s scene=%s", engine.config.mode, engine.config.scene_path);
	if (strcmp(engine.config.mode, "editor") == 0) {
		engine.config.editor_enabled = 1;
		editor_app_register(&engine);
	}
	else {
		demo_scene_register(&engine);
	}
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

		if (engine.editor_ready && editor_enabled(&engine.editor)) {
			editor_begin_frame(&engine.editor, &engine);
		}
		platform_pump_events(&engine.platform, &engine.input.quit, engine.editor_ready ? (platform_event_fn)editor_handle_event : NULL, &engine.editor);
		input_update(&engine.input, &engine.platform);
		if (engine.input.quit) { break; }
		if (engine.editor_ready && editor_enabled(&engine.editor)) {
			editor_update(&engine.editor, &engine);
		}

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
