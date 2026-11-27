#include <SDL2/SDL.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "editor/editor.h"
#include "core/engine.h"
#include "scene/scene_doc.h"

extern char** environ;

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION

#include "../../third_party/nuklear/nuklear.h"
#include "../../third_party/nuklear/nuklear_sdl_renderer.h"

static void editor_set_status(editor_t* editor, const char* status) {
	if (!editor || !status) { return; }
	snprintf(editor->status, sizeof(editor->status), "%s", status);
}

static double editor_now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void editor_snapshot_free(editor_snapshot_t* s) {
	if (!s) { return; }
	free(s->data);
	s->data = NULL;
	s->size = 0;
	s->selected_entity = NB_ENTITY_INVALID;
}

static void editor_history_clear(editor_t* editor) {
	if (!editor) { return; }
	for (int i = 0; i < editor->undo_count; i++) { editor_snapshot_free(&editor->undo[i]); }
	for (int i = 0; i < editor->redo_count; i++) { editor_snapshot_free(&editor->redo[i]); }
	editor->undo_count = 0;
	editor->redo_count = 0;
}

static nb_result_t editor_snapshot_capture(editor_t* editor, engine_t* engine, editor_snapshot_t* out) {
	if (!editor || !engine || !out) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }
	out->data = NULL;
	out->size = 0;
	out->selected_entity = editor->selected_entity;
	if (!engine->scene_graph) { return NB_RESULT_ERROR_SCENE; }
	return scene_doc_save_to_buffer(&engine->scene_doc, engine->scene_graph, &out->data, &out->size);
}

static void editor_history_push(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || editor->history_suspended) { return; }
	editor_snapshot_t snap;
	if (editor_snapshot_capture(editor, engine, &snap) != NB_RESULT_OK) { return; }
	if (editor->undo_count >= NB_EDITOR_UNDO_MAX) {
		editor_snapshot_free(&editor->undo[0]);
		memmove(&editor->undo[0], &editor->undo[1], sizeof(editor_snapshot_t) * (size_t)(NB_EDITOR_UNDO_MAX - 1));
		editor->undo_count = NB_EDITOR_UNDO_MAX - 1;
	}
	editor->undo[editor->undo_count++] = snap;
	for (int i = 0; i < editor->redo_count; i++) { editor_snapshot_free(&editor->redo[i]); }
	editor->redo_count = 0;
}

static void editor_hierarchy_reset_drag(editor_t* editor) {
	if (!editor) { return; }
	editor->hierarchy_drag_candidate = NB_ENTITY_INVALID;
	editor->hierarchy_drag_entity = NB_ENTITY_INVALID;
	editor->hierarchy_drag_start_x = 0;
	editor->hierarchy_drag_start_y = 0;
	editor->hierarchy_drag_active = 0;
	editor->hierarchy_drop_entity = NB_ENTITY_INVALID;
	editor->hierarchy_drop_row_entity = NB_ENTITY_INVALID;
	editor->hierarchy_drop_index = 0;
	editor->hierarchy_drop_zone = 0;
}

static void editor_hierarchy_clear_state(editor_t* editor) {
	if (!editor) { return; }
	memset(editor->hierarchy_expanded, 0, sizeof(editor->hierarchy_expanded));
	editor->hierarchy_filter[0] = '\0';
	editor->hierarchy_context_entity = NB_ENTITY_INVALID;
	editor->rename_entity = NB_ENTITY_INVALID;
	editor->rename_focus = 0;
	editor->reparent_source = NB_ENTITY_INVALID;
	editor->reparent_open = 0;
	editor_hierarchy_reset_drag(editor);
	editor->inspector_name_entity = NB_ENTITY_INVALID;
}

static void editor_fix_selection(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || !engine->scene_graph) { return; }
	if (editor->selected_entity == NB_ENTITY_INVALID) { return; }
	if (scene_get_node(engine->scene_graph, editor->selected_entity) == NULL) { editor->selected_entity = NB_ENTITY_INVALID; }
}

static int editor_rename_node(editor_t* editor, engine_t* engine, unsigned int id, const char* name) {
	if (!editor || !engine || !engine->scene_graph || !name || !*name) { return 0; }
	node_t* node = scene_get_node(engine->scene_graph, id);
	if (!node) { return 0; }
	if (strcmp(node->name, name) == 0) { return 1; }
	if (scene_find_node(engine->scene_graph, name) != NB_ENTITY_INVALID) {
		editor_set_status(editor, "rename failed: name exists");
		return 0;
	}
	editor_history_push(editor, engine);
	if (!scene_set_node_name(engine->scene_graph, id, name)) {
		editor_set_status(editor, "rename failed");
		return 0;
	}
	engine->scene_doc.dirty = 1;
	editor_set_status(editor, "node renamed");
	return 1;
}

static nb_result_t editor_restore_snapshot(editor_t* editor, engine_t* engine, const editor_snapshot_t* snap) {
	if (!editor || !engine || !snap || !snap->data) { return NB_RESULT_ERROR_INVALID_ARGUMENT; }
	scene_t next_scene;
	scene_doc_t next_doc;
	scene_init(&next_scene);
	scene_doc_init(&next_doc);
	nb_result_t result = scene_doc_load_from_buffer(&next_doc, engine->scene_doc.path[0] ? engine->scene_doc.path : engine->config.scene_path, snap->data, snap->size, engine, &next_scene);
	if (result != NB_RESULT_OK) { return result; }
	if (engine->scene_graph) {
		scene_shutdown(engine->scene_graph);
		*engine->scene_graph = next_scene;
	}
	else {
		return NB_RESULT_ERROR_SCENE;
	}
	engine->scene_doc = next_doc;
	engine->scene_doc.dirty = 1;
	editor->selected_entity = snap->selected_entity;
	editor_hierarchy_clear_state(editor);
	editor_fix_selection(editor, engine);
	return NB_RESULT_OK;
}

static void editor_undo(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || editor->undo_count <= 0) { return; }
	editor_snapshot_t cur;
	if (editor_snapshot_capture(editor, engine, &cur) != NB_RESULT_OK) { return; }
	editor_snapshot_t prev = editor->undo[--editor->undo_count];
	editor->history_suspended = 1;
	nb_result_t result = editor_restore_snapshot(editor, engine, &prev);
	editor->history_suspended = 0;
	editor_snapshot_free(&prev);
	if (result != NB_RESULT_OK) {
		editor_snapshot_free(&cur);
		editor_set_status(editor, "undo failed");
		return;
	}
	if (editor->redo_count < NB_EDITOR_UNDO_MAX) {
		editor->redo[editor->redo_count++] = cur;
	}
	else {
		editor_snapshot_free(&editor->redo[0]);
		memmove(&editor->redo[0], &editor->redo[1], sizeof(editor_snapshot_t) * (size_t)(NB_EDITOR_UNDO_MAX - 1));
		editor->redo[NB_EDITOR_UNDO_MAX - 1] = cur;
	}
	editor_set_status(editor, "undo");
}

static void editor_redo(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || editor->redo_count <= 0) { return; }
	editor_snapshot_t cur;
	if (editor_snapshot_capture(editor, engine, &cur) != NB_RESULT_OK) { return; }
	editor_snapshot_t next = editor->redo[--editor->redo_count];
	editor->history_suspended = 1;
	nb_result_t result = editor_restore_snapshot(editor, engine, &next);
	editor->history_suspended = 0;
	editor_snapshot_free(&next);
	if (result != NB_RESULT_OK) {
		editor_snapshot_free(&cur);
		editor_set_status(editor, "redo failed");
		return;
	}
	if (editor->undo_count < NB_EDITOR_UNDO_MAX) {
		editor->undo[editor->undo_count++] = cur;
	}
	else {
		editor_snapshot_free(&editor->undo[0]);
		memmove(&editor->undo[0], &editor->undo[1], sizeof(editor_snapshot_t) * (size_t)(NB_EDITOR_UNDO_MAX - 1));
		editor->undo[NB_EDITOR_UNDO_MAX - 1] = cur;
	}
	editor_set_status(editor, "redo");
}

static const char* editor_scene_path(editor_t* editor, engine_t* engine) {
	if (engine && engine->scene_doc.path[0]) { return engine->scene_doc.path; }
	if (engine && engine->config.scene_path[0]) { return engine->config.scene_path; }
	if (editor && editor->scene_path[0]) { return editor->scene_path; }
	return "examples/kula/scenes/level01.tscn";
}

static int editor_save_scene(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || !engine->scene_graph) { return 0; }
	const char* path = editor_scene_path(editor, engine);
	if (!path || !path[0]) {
		editor_set_status(editor, "scene save failed: no path");
		engine_log(engine, "save failed: doc='%s' cfg='%s' ed='%s'", engine->scene_doc.path, engine->config.scene_path, editor->scene_path);
		return 0;
	}
	if (!engine->scene_doc.path[0]) {
		snprintf(engine->scene_doc.path, sizeof(engine->scene_doc.path), "%s", path);
	}
	nb_result_t result = scene_doc_save(&engine->scene_doc, path, engine, engine->scene_graph);
	if (result == NB_RESULT_OK) {
		const char* saved = engine->scene_doc.path[0] ? engine->scene_doc.path : path;
		snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%s", saved);
		snprintf(editor->scene_path, sizeof(editor->scene_path), "%s", saved);
		editor->last_save_time = editor_now_seconds();
		editor_set_status(editor, "scene saved");
		return 1;
	}
	char msg[128];
	snprintf(msg, sizeof(msg), "scene save failed: %s (%s)", nb_error_string(result), path);
	editor_set_status(editor, msg);
	return 0;
}

static void editor_run_scene(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	if (!editor_save_scene(editor, engine)) { return; }
	const char* scene = editor_scene_path(editor, engine);
	if (!scene || !scene[0]) {
		editor_set_status(editor, "run failed: no scene path");
		engine_log(engine, "run failed: doc='%s' cfg='%s' ed='%s'", engine->scene_doc.path, engine->config.scene_path, editor->scene_path);
		return;
	}
	char scene_arg[512];
	snprintf(scene_arg, sizeof(scene_arg), "--scene=%s", scene);
	char* argv[] = { (char*)"./build/nodebench", (char*)"--mode=game", scene_arg, NULL };
	pid_t pid = -1;
	int rc = posix_spawn(&pid, "./build/nodebench", NULL, NULL, argv, environ);
	if (rc == 0) {
		editor->play_pid = (int)pid;
		editor_set_status(editor, "game launched");
	}
	else {
		editor_set_status(editor, "game launch failed");
	}
}

int editor_init(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return 0; }

	memset(editor, 0, sizeof(*editor));
	editor_hierarchy_clear_state(editor);
	editor->enabled = engine->config.editor_enabled ? 1 : 0;
	editor->selected_entity = NB_ENTITY_INVALID;
	snprintf(editor->scene_path, sizeof(editor->scene_path), "%s", engine->config.scene_path);
	snprintf(editor->new_node_name, sizeof(editor->new_node_name), "%s", "NewNode");
	editor->camera_pos[0] = 0.0f;
	editor->camera_pos[1] = 3.0f;
	editor->camera_pos[2] = 6.0f;
	editor->camera_yaw = -1.5707963f;
	editor->camera_pitch = -0.35f;
	editor->camera_speed = 6.0f;
	editor->viewport_x = 0;
	editor->viewport_y = 0;
	editor->viewport_w = engine->config.width;
	editor->viewport_h = engine->config.height;
	editor->toolbar_h = 34;
	editor->tool = NB_EDITOR_TOOL_SELECT;
	editor->gizmo_axis = -1;
	editor->gizmo_dragging = 0;
	editor->undo_count = 0;
	editor->redo_count = 0;
	editor->history_suspended = 0;
	editor->last_save_time = editor_now_seconds();
	editor->autosave_timer = 0.0;
	editor->play_pid = -1;
	editor->request_save = 0;
	editor->request_undo = 0;
	editor->request_redo = 0;
	editor->inspector_editing = 0;
	editor->has_pre_edit = 0;
	memset(&editor->pre_edit, 0, sizeof(editor->pre_edit));
	editor_set_status(editor, "editor ready");

	SDL_Window* window = (SDL_Window*)platform_window(&engine->platform);
	SDL_Renderer* renderer = (SDL_Renderer*)platform_renderer(&engine->platform);
	if (!window || !renderer) {
		editor_set_status(editor, "editor init failed: no SDL renderer");
		return 0;
	}

	struct nk_context* ctx = nk_sdl_init(window, renderer);
	if (!ctx) {
		editor_set_status(editor, "editor init failed: nuklear");
		return 0;
	}

	struct nk_font_atlas* atlas = NULL;
	nk_sdl_font_stash_begin(&atlas);
	nk_sdl_font_stash_end();

	{
		static const char* icons[4][16] = {
			{
				"................",
				"..X.............",
				"..XX............",
				"..XXX...........",
				"..XXXX..........",
				"..XXXXX.........",
				"..XXXXXX........",
				"..XXXXXXX.......",
				"..XXXXXXXX......",
				"..XXXXX.........",
				"..XX.XX.........",
				"..X..XX.........",
				".....XX.........",
				".....XX.........",
				"......X.........",
				"................"
			},
			{
				".......XX.......",
				"......XXXX......",
				".......XX.......",
				".......XX.......",
				".......XX.......",
				".......XX.......",
				".XX....XX....XX.",
				"XXXX..XXXX..XXXX",
				".XX....XX....XX.",
				".......XX.......",
				".......XX.......",
				".......XX.......",
				".......XX.......",
				"......XXXX......",
				".......XX.......",
				"................"
			},
			{
				"................",
				"....XXXXXXX.....",
				"...XX.....XX....",
				"..XX.......X....",
				"..X.............",
				".XX.............",
				".XX.............",
				".XX.............",
				"..X.............",
				"..XX.......X....",
				"...XX.....XX....",
				"....XXXXXXX.....",
				"...........XX...",
				"..........XXXX..",
				"...........XX...",
				"................"
			},
			{
				"................",
				".......XX.......",
				"......XXXX......",
				".......XX.......",
				"...XXXXXXXXXX...",
				"...X........X...",
				"...X........X...",
				"...X........X...",
				".XXXX......XXXX.",
				"...X........X...",
				"...X........X...",
				"...X........X...",
				"...XXXXXXXXXX...",
				".......XX.......",
				"......XXXX......",
				".......XX......."
			}
		};
		for (int t = 0; t < 4; t++) {
			unsigned char pixels[16 * 16 * 4];
			for (int y = 0; y < 16; y++) {
				for (int x = 0; x < 16; x++) {
					int i = (y * 16 + x) * 4;
					int on = icons[t][y][x] == 'X';
					pixels[i + 0] = on ? 235 : 0;
					pixels[i + 1] = on ? 235 : 0;
					pixels[i + 2] = on ? 235 : 0;
					pixels[i + 3] = on ? 255 : 0;
				}
			}
			SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
			if (tex) {
				SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
				SDL_UpdateTexture(tex, NULL, pixels, 16 * 4);
			}
			editor->tool_textures[t] = tex;
		}
	}

	editor->nk_ctx = ctx;
	editor->ready = 1;
	platform_set_mouse_capture_blocked(&engine->platform, editor->enabled);
	if (editor->enabled) {
		platform_set_mouse_capture(&engine->platform, 0);
	}
	return 1;
}

void editor_shutdown(editor_t* editor, engine_t* engine) {
	(void)engine;
	if (!editor) { return; }

	if (editor->ready) {
		for (int i = 0; i < 4; i++) {
			if (editor->tool_textures[i]) {
				SDL_DestroyTexture((SDL_Texture*)editor->tool_textures[i]);
				editor->tool_textures[i] = NULL;
			}
		}
		if (editor->has_pre_edit) { editor_snapshot_free(&editor->pre_edit); editor->has_pre_edit = 0; }
		editor_history_clear(editor);
		nk_sdl_shutdown();
	}
	editor->nk_ctx = NULL;
	editor->ready = 0;
	editor->enabled = 0;
}

int editor_handle_event(editor_t* editor, const void* sdl_event) {
	if (!editor || !editor->ready || !editor->enabled || !sdl_event) { return 0; }
	SDL_Event* e = (SDL_Event*)sdl_event;
	if (e->type == SDL_KEYDOWN) {
		SDL_Keymod mods = SDL_GetModState();
		int ctrl = (mods & KMOD_CTRL) != 0;
		if (ctrl && e->key.keysym.sym == SDLK_s) { editor->request_save = 1; return 1; }
		if (ctrl && e->key.keysym.sym == SDLK_z) { editor->request_undo = 1; return 1; }
		if (ctrl && e->key.keysym.sym == SDLK_y) { editor->request_redo = 1; return 1; }
	}
	return nk_sdl_handle_event(e);
}

int editor_enabled(const editor_t* editor) {
	return editor && editor->enabled && editor->ready;
}

int editor_wants_mouse(const editor_t* editor) {
	return editor_enabled(editor) && editor->want_capture_mouse;
}

int editor_wants_keyboard(const editor_t* editor) {
	return editor_enabled(editor) && editor->want_capture_keyboard;
}

static void editor_draw_scene_panel(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	(void)win_h;
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int panel_w = 340;
	int panel_h = 240;
	if (panel_w > win_w - 20) { panel_w = win_w - 20; }
	if (nk_begin(ctx, "Scene", nk_rect(10, 10, (float)panel_w, (float)panel_h), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)) {
		const char* path = editor_scene_path(editor, engine);
		const char* base = strrchr(path, '/');
		base = base ? base + 1 : path;
		nk_layout_row_dynamic(ctx, 22, 2);
		if (nk_button_label(ctx, "Run")) { editor_run_scene(editor, engine); }
		if (nk_button_label(ctx, "Save")) { editor_save_scene(editor, engine); }

		nk_layout_row_dynamic(ctx, 18, 1);
		nk_labelf(ctx, NK_TEXT_LEFT, "file: %s%s", base, engine->scene_doc.dirty ? " *" : "");
		nk_labelf(ctx, NK_TEXT_LEFT, "path: %s", path);
		nk_labelf(ctx, NK_TEXT_LEFT, "entities: %d  meshes: %d", engine->scene_graph ? engine->scene_graph->entity_count : 0, engine->scene_graph ? engine->scene_graph->mesh_renderer_count : 0);
		nk_labelf(ctx, NK_TEXT_LEFT, "cams: %d  lights: %d  col: %d", engine->scene_graph ? engine->scene_graph->camera_count : 0, engine->scene_graph ? engine->scene_graph->light_count : 0, engine->scene_graph ? engine->scene_graph->collider_count : 0);
		nk_labelf(ctx, NK_TEXT_LEFT, "undo: %d  redo: %d", editor->undo_count, editor->redo_count);

		if (editor->selected_entity != NB_ENTITY_INVALID && engine->scene_graph) {
			node_t* node = scene_get_node(engine->scene_graph, editor->selected_entity);
			if (node) {
				nk_labelf(ctx, NK_TEXT_LEFT, "sel: %s (#%u)", node->name, (unsigned int)node->id);
			}
		}
		else {
			nk_label(ctx, "sel: <none>", NK_TEXT_LEFT);
		}

		double since_save = editor_now_seconds() - editor->last_save_time;
		if (since_save < 0.0) { since_save = 0.0; }
		nk_labelf(ctx, NK_TEXT_LEFT, "saved: %.0fs ago  %s", since_save, editor->status);
		nk_label(ctx, "Ctrl+S save  Ctrl+Z undo  Ctrl+Y redo", NK_TEXT_LEFT);
	}
	nk_end(ctx);
}

static void editor_draw_render_config(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int panel_w = 340;
	int panel_x = win_w - panel_w - 10;
	int panel_h = 270;
	int panel_y = win_h - panel_h - 10;
	if (panel_x < 360) { panel_x = 360; panel_w = win_w - panel_x - 10; }
	if (panel_w < 240) { panel_w = 240; }
	if (panel_y < 200) { panel_y = 200; panel_h = win_h - panel_y - 10; }
	if (nk_begin(ctx, "Render", nk_rect((float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
		nk_layout_row_dynamic(ctx, 18, 1);
		nk_checkbox_label(ctx, "HUD", &engine->config.hud_enabled);
		nk_checkbox_label(ctx, "HUD Debug", &engine->config.hud_debug_enabled);
		nk_checkbox_label(ctx, "Wireframe", &engine->config.wireframe_enabled);
		nk_checkbox_label(ctx, "XRay", &engine->config.xray_enabled);
		nk_checkbox_label(ctx, "Fog", &engine->config.fog_enabled);
		nk_checkbox_label(ctx, "Dither", &engine->config.dither_enabled);
		nk_checkbox_label(ctx, "Cull Backfaces", &engine->config.cull_backfaces);
		nk_checkbox_label(ctx, "Reject Near", &engine->config.reject_near_triangles);

		nk_property_float(ctx, "Affine", 0.0f, &engine->config.affine_strength, 1.0f, 0.01f, 0.01f);
		nk_property_float(ctx, "Fog Near", 0.0f, &engine->config.fog_near, engine->config.fog_far - 0.001f, 0.1f, 0.1f);
		nk_property_float(ctx, "Fog Far", engine->config.fog_near + 0.001f, &engine->config.fog_far, 500.0f, 0.1f, 0.1f);
		nk_property_float(ctx, "Near Plane", 0.001f, &engine->config.near_plane, engine->config.far_plane - 0.001f, 0.01f, 0.01f);
		nk_property_float(ctx, "Far Plane", engine->config.near_plane + 0.001f, &engine->config.far_plane, 1000.0f, 0.1f, 0.1f);
	}
	nk_end(ctx);
}

static const char* editor_node_name(engine_t* engine, entity_id_t id) {
	node_t* node = scene_get_node(engine->scene_graph, id);
	if (!node) { return "<invalid>"; }
	return node->name;
}

static void editor_get_camera(editor_t* editor, camera_t* camera) {
	camera->eye.x = editor->camera_pos[0];
	camera->eye.y = editor->camera_pos[1];
	camera->eye.z = editor->camera_pos[2];
	float cy = cosf(editor->camera_yaw);
	float sy = sinf(editor->camera_yaw);
	float cp = cosf(editor->camera_pitch);
	float sp = sinf(editor->camera_pitch);
	camera->target.x = camera->eye.x + cy * cp;
	camera->target.y = camera->eye.y + sp;
	camera->target.z = camera->eye.z + sy * cp;
	camera->up = (vec3_t){ 0.0f, 1.0f, 0.0f };
	camera->fov = 60.0f;
	camera->near_plane = 0.05f;
	camera->far_plane = 200.0f;
}

static vec3_t editor_screen_ray(editor_t* editor, engine_t* engine, int mx, int my) {
	camera_t cam;
	editor_get_camera(editor, &cam);
	float aspect = (float)editor->viewport_w / (float)(editor->viewport_h > 0 ? editor->viewport_h : 1);
	float ndc_x = ((float)(mx - editor->viewport_x) / (float)editor->viewport_w) * 2.0f - 1.0f;
	float ndc_y = 1.0f - ((float)(my - editor->viewport_y) / (float)editor->viewport_h) * 2.0f;
	float tan_half = tanf(cam.fov * 3.14159265f / 360.0f);
	vec3_t forward = vec3_normalize(vec3_sub(cam.target, cam.eye));
	vec3_t right = vec3_normalize(vec3_cross(forward, cam.up));
	vec3_t up = vec3_cross(right, forward);
	vec3_t dir = vec3_add(vec3_add(vec3_scale(right, ndc_x * tan_half * aspect), vec3_scale(up, ndc_y * tan_half)), forward);
	(void)engine;
	return vec3_normalize(dir);
}

static int editor_project_point(editor_t* editor, vec3_t world, float* sx, float* sy) {
	camera_t cam;
	editor_get_camera(editor, &cam);
	vec3_t forward = vec3_normalize(vec3_sub(cam.target, cam.eye));
	vec3_t right = vec3_normalize(vec3_cross(forward, cam.up));
	vec3_t up = vec3_cross(right, forward);
	vec3_t rel = vec3_sub(world, cam.eye);
	float z = vec3_dot(rel, forward);
	if (z < 0.05f) { return 0; }
	float x = vec3_dot(rel, right);
	float y = vec3_dot(rel, up);
	float aspect = (float)editor->viewport_w / (float)(editor->viewport_h > 0 ? editor->viewport_h : 1);
	float tan_half = tanf(cam.fov * 3.14159265f / 360.0f);
	float ndc_x = x / (z * tan_half * aspect);
	float ndc_y = y / (z * tan_half);
	*sx = (float)editor->viewport_x + (ndc_x * 0.5f + 0.5f) * (float)editor->viewport_w;
	*sy = (float)editor->viewport_y + (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)editor->viewport_h;
	return 1;
}

static float editor_point_segment_dist2(float px, float py, float ax, float ay, float bx, float by) {
	float abx = bx - ax;
	float aby = by - ay;
	float apx = px - ax;
	float apy = py - ay;
	float ab2 = abx * abx + aby * aby;
	float t = ab2 > 0.0001f ? (apx * abx + apy * aby) / ab2 : 0.0f;
	if (t < 0.0f) { t = 0.0f; }
	if (t > 1.0f) { t = 1.0f; }
	float cx = ax + abx * t;
	float cy = ay + aby * t;
	float dx = px - cx;
	float dy = py - cy;
	return dx * dx + dy * dy;
}

static int editor_pick_gizmo_axis(editor_t* editor, engine_t* engine, int mx, int my) {
	if (!editor || !engine || !engine->scene_graph) { return -1; }
	node_t* node = scene_get_node(engine->scene_graph, editor->selected_entity);
	if (!node || !(node->components & NB_NODE_COMPONENT_TRANSFORM)) { return -1; }
	vec3_t origin = node->world_position;
	float ox, oy;
	if (!editor_project_point(editor, origin, &ox, &oy)) { return -1; }
	float axis_len = 1.0f;
	vec3_t axes[3] = { { axis_len, 0, 0 }, { 0, axis_len, 0 }, { 0, 0, axis_len } };
	int best = -1;
	float best_d2 = 100.0f;
	for (int i = 0; i < 3; i++) {
		vec3_t tip = vec3_add(origin, axes[i]);
		float tx, ty;
		if (!editor_project_point(editor, tip, &tx, &ty)) { continue; }
		float d2 = editor_point_segment_dist2((float)mx, (float)my, ox, oy, tx, ty);
		if (d2 < best_d2) { best_d2 = d2; best = i; }
	}
	return best;
}

static int editor_ray_aabb(vec3_t ro, vec3_t rd, vec3_t mn, vec3_t mx, float* t_out) {
	float tmin = 0.0f;
	float tmax = 100000.0f;
	float mins[3] = { mn.x, mn.y, mn.z };
	float maxs[3] = { mx.x, mx.y, mx.z };
	float ros[3] = { ro.x, ro.y, ro.z };
	float rds[3] = { rd.x, rd.y, rd.z };
	for (int i = 0; i < 3; i++) {
		if (fabsf(rds[i]) < 0.0001f) {
			if (ros[i] < mins[i] || ros[i] > maxs[i]) { return 0; }
		}
		else {
			float inv = 1.0f / rds[i];
			float t1 = (mins[i] - ros[i]) * inv;
			float t2 = (maxs[i] - ros[i]) * inv;
			if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
			if (t1 > tmin) { tmin = t1; }
			if (t2 < tmax) { tmax = t2; }
			if (tmin > tmax) { return 0; }
		}
	}
	if (t_out) { *t_out = tmin >= 0.0f ? tmin : tmax; }
	return tmax >= 0.0f;
}

static int editor_pick_node(editor_t* editor, engine_t* engine, int mx, int my, entity_id_t* out_id) {
	if (!editor || !engine || !engine->scene_graph || !out_id) { return 0; }
	vec3_t ro = (vec3_t){ editor->camera_pos[0], editor->camera_pos[1], editor->camera_pos[2] };
	vec3_t rd = editor_screen_ray(editor, engine, mx, my);
	float best_t = 100000.0f;
	entity_id_t best = NB_ENTITY_INVALID;

	for (int i = 0; i < engine->scene_graph->node_count; i++) {
		node_t* node = &engine->scene_graph->nodes[i];
		if (!(node->components & NB_NODE_COMPONENT_TRANSFORM)) { continue; }
		if (!(node->components & (NB_NODE_COMPONENT_MESH_RENDERER | NB_NODE_COMPONENT_CAMERA | NB_NODE_COMPONENT_LIGHT | NB_NODE_COMPONENT_COLLIDER))) { continue; }
		vec3_t c = node->world_position;
		vec3_t e = { 0.5f * node->transform.scale.x, 0.5f * node->transform.scale.y, 0.5f * node->transform.scale.z };
		if (node->components & NB_NODE_COMPONENT_COLLIDER) {
			if (node->collider.shape == NB_COLLIDER_SPHERE) {
				e = (vec3_t){ node->collider.radius, node->collider.radius, node->collider.radius };
			}
			else {
				e = node->collider.extents;
			}
		}
		if (e.x < 0.15f) { e.x = 0.15f; }
		if (e.y < 0.15f) { e.y = 0.15f; }
		if (e.z < 0.15f) { e.z = 0.15f; }
		vec3_t mn = { c.x - e.x, c.y - e.y, c.z - e.z };
		vec3_t mxv = { c.x + e.x, c.y + e.y, c.z + e.z };
		float t = 0.0f;
		if (editor_ray_aabb(ro, rd, mn, mxv, &t) && t < best_t) {
			best_t = t;
			best = node->id;
		}
	}

	if (best != NB_ENTITY_INVALID) {
		*out_id = best;
		return 1;
	}
	return 0;
}

static const char* editor_node_type_label(const node_t* node) {
	if (!node) { return "Node"; }
	if (node->components & NB_NODE_COMPONENT_CAMERA) { return "Camera"; }
	if (node->components & NB_NODE_COMPONENT_LIGHT) { return "Light"; }
	if (node->components & NB_NODE_COMPONENT_MESH_RENDERER) { return "Mesh"; }
	if (node->components & NB_NODE_COMPONENT_COLLIDER) { return "Collider"; }
	return "Node";
}

typedef struct {
	entity_id_t id;
	struct nk_rect rect;
} editor_hierarchy_row_t;

typedef struct {
	editor_hierarchy_row_t rows[NB_SCENE_MAX_ENTITIES];
	int count;
} editor_hierarchy_rows_t;

static float editor_hierarchy_row_height(struct nk_context* ctx) {
	if (!ctx) { return 20.0f; }
	return ctx->style.font->height + 2.0f * ctx->style.tab.padding.y;
}

static unsigned char* editor_hierarchy_state_ptr(editor_t* editor, unsigned int id) {
	if (!editor || id == NB_ENTITY_INVALID || id > NB_SCENE_MAX_ENTITIES) { return NULL; }
	return &editor->hierarchy_expanded[id - 1];
}

static char editor_hierarchy_lower(char c) {
	if (c >= 'A' && c <= 'Z') { return (char)(c - 'A' + 'a'); }
	return c;
}

static int editor_hierarchy_name_matches(const node_t* node, const char* filter) {
	if (!node || !filter || !*filter) { return 1; }
	const char* text = node->name;
	char first = editor_hierarchy_lower(filter[0]);
	while (*text) {
		if (editor_hierarchy_lower(*text) == first) {
			const char* a = text;
			const char* b = filter;
			while (*a && *b && editor_hierarchy_lower(*a) == editor_hierarchy_lower(*b)) {
				a++;
				b++;
			}
			if (!*b) { return 1; }
		}
		text++;
	}
	return 0;
}

static int editor_hierarchy_subtree_matches(scene_t* scene, node_t* node, const char* filter, int depth) {
	if (!scene || !node || depth > 64) { return 0; }
	if (!filter || !*filter) { return 1; }
	if (editor_hierarchy_name_matches(node, filter)) { return 1; }
	for (int i = 0; i < node->child_count; i++) {
		node_t* child = scene_get_node(scene, node->children[i]);
		if (child && editor_hierarchy_subtree_matches(scene, child, filter, depth + 1)) { return 1; }
	}
	return 0;
}

static void editor_hierarchy_make_label(const node_t* node, int depth, int manual_indent, char* out, size_t size) {
	const char* name = node && node->name[0] ? node->name : "<unnamed>";
	if (manual_indent) {
		int indent = depth * 2;
		if (indent > 20) { indent = 20; }
		snprintf(out, size, "%*s[%s] %s", indent, "", editor_node_type_label(node), name);
	}
	else {
		snprintf(out, size, "[%s] %s", editor_node_type_label(node), name);
	}
}

static void editor_hierarchy_store_row(editor_hierarchy_rows_t* rows, struct nk_context* ctx, unsigned int id, float row_h) {
	if (!rows || !ctx || !ctx->current || !ctx->current->layout) { return; }
	if (rows->count >= NB_SCENE_MAX_ENTITIES) { return; }
	struct nk_panel* layout = ctx->current->layout;
	struct nk_rect rect = nk_rect(layout->bounds.x, layout->at_y - (float)*layout->offset_y, layout->bounds.w, row_h - ctx->style.window.spacing.y);
	if (rect.h < 4.0f) { rect.h = row_h; }
	rows->rows[rows->count].id = id;
	rows->rows[rows->count].rect = rect;
	rows->count++;
}

static int editor_child_index(scene_t* scene, unsigned int parent, unsigned int id) {
	if (!scene) { return -1; }
	if (parent == NB_ENTITY_INVALID) {
		int index = 0;
		for (int i = 0; i < scene->node_count; i++) {
			node_t* node = &scene->nodes[i];
			if (node->parent != NB_ENTITY_INVALID) { continue; }
			if (node->id == id) { return index; }
			index++;
		}
		return -1;
	}
	node_t* parent_node = scene_get_node(scene, parent);
	if (!parent_node) { return -1; }
	for (int i = 0; i < parent_node->child_count; i++) {
		if (parent_node->children[i] == id) { return i; }
	}
	return -1;
}

static void editor_hierarchy_set_drop(editor_t* editor, engine_t* engine, unsigned int parent, int index, int zone) {
	if (!editor || !engine || !engine->scene_graph) { return; }
	node_t* source = scene_get_node(engine->scene_graph, editor->hierarchy_drag_entity);
	if (source && source->parent == parent) {
		int source_index = editor_child_index(engine->scene_graph, parent, source->id);
		if (source_index >= 0 && index >= 0 && source_index < index) { index--; }
	}
	if (index < 0) { index = 0; }
	editor->hierarchy_drop_entity = parent;
	editor->hierarchy_drop_index = index;
	editor->hierarchy_drop_zone = zone;
}

static void editor_hierarchy_update_drop(editor_t* editor, engine_t* engine, editor_hierarchy_rows_t* rows) {
	if (!editor || !engine || !engine->scene_graph || !rows) { return; }
	editor->hierarchy_drop_entity = NB_ENTITY_INVALID;
	editor->hierarchy_drop_row_entity = NB_ENTITY_INVALID;
	editor->hierarchy_drop_index = 0;
	editor->hierarchy_drop_zone = 0;
	if (!editor->hierarchy_drag_active) { return; }

	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int mx = ctx->input.mouse.pos.x;
	int my = ctx->input.mouse.pos.y;
	int hovered_row = 0;
	for (int i = 0; i < rows->count; i++) {
		struct nk_rect rect = rows->rows[i].rect;
		if (mx < rect.x || mx >= rect.x + rect.w || my < rect.y || my >= rect.y + rect.h) { continue; }
		hovered_row = 1;
		unsigned int target = rows->rows[i].id;
		if (target == editor->hierarchy_drag_entity || scene_is_ancestor(engine->scene_graph, editor->hierarchy_drag_entity, target)) { return; }
		node_t* target_node = scene_get_node(engine->scene_graph, target);
		if (!target_node) { return; }

		float rel = (float)(my - rect.y) / (rect.h > 0.0f ? rect.h : 1.0f);
		editor->hierarchy_drop_row_entity = target;
		if (rel < 0.25f) {
			editor_hierarchy_set_drop(editor, engine, target_node->parent, editor_child_index(engine->scene_graph, target_node->parent, target), -1);
		}
		else if (rel > 0.75f) {
			editor_hierarchy_set_drop(editor, engine, target_node->parent, editor_child_index(engine->scene_graph, target_node->parent, target) + 1, 1);
		}
		else {
			editor_hierarchy_set_drop(editor, engine, target, target_node->child_count, 2);
		}
		return;
	}

	if (!hovered_row && rows->count > 0) {
		struct nk_rect first = rows->rows[0].rect;
		if (mx >= first.x && mx < first.x + first.w) {
			editor_hierarchy_set_drop(editor, engine, NB_ENTITY_INVALID, -1, 3);
		}
	}
}

static void editor_hierarchy_draw_drop_feedback(editor_t* editor, editor_hierarchy_rows_t* rows) {
	if (!editor->hierarchy_drag_active || editor->hierarchy_drop_zone == 3 || editor->hierarchy_drop_row_entity == NB_ENTITY_INVALID) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (!ctx || !ctx->current) { return; }
	for (int i = 0; i < rows->count; i++) {
		if (rows->rows[i].id != editor->hierarchy_drop_row_entity) { continue; }
		struct nk_rect rect = rows->rows[i].rect;
		if (editor->hierarchy_drop_zone == 2) {
			nk_fill_rect(&ctx->current->buffer, rect, 0.0f, nk_rgba(80, 130, 190, 90));
		}
		else {
			float y = editor->hierarchy_drop_zone < 0 ? rect.y : rect.y + rect.h;
			nk_stroke_line(&ctx->current->buffer, rect.x, y, rect.x + rect.w, y, 2.0f, nk_rgb(90, 170, 255));
		}
		break;
	}
}

static void editor_hierarchy_commit_drag(editor_t* editor, engine_t* engine) {
	unsigned int id = editor->hierarchy_drag_entity;
	if (id != NB_ENTITY_INVALID && engine->scene_graph && scene_get_node(engine->scene_graph, id)) {
		unsigned int parent = editor->hierarchy_drop_entity;
		node_t* source = scene_get_node(engine->scene_graph, id);
		node_t* parent_node = parent == NB_ENTITY_INVALID ? NULL : scene_get_node(engine->scene_graph, parent);
		if (parent_node && parent_node->child_count >= NB_NODE_MAX_CHILDREN && source->parent != parent) {
			editor_set_status(editor, "move failed: parent full");
		}
		else {
			int index = editor->hierarchy_drop_zone == 3 ? -1 : editor->hierarchy_drop_index;
			editor_history_push(editor, engine);
			if (scene_move_node(engine->scene_graph, id, parent, index)) {
				unsigned char* state = editor_hierarchy_state_ptr(editor, parent);
				if (state) { *state = 1; }
				editor->selected_entity = id;
				engine->scene_doc.dirty = 1;
				editor_set_status(editor, parent == NB_ENTITY_INVALID ? "node moved to root" : "node moved");
			}
			else {
				editor_set_status(editor, "move failed");
			}
		}
	}
	editor_hierarchy_reset_drag(editor);
}

static void editor_hierarchy_handle_drag(editor_t* editor, engine_t* engine, editor_hierarchy_rows_t* rows) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (editor->hierarchy_context_entity != NB_ENTITY_INVALID || editor->rename_entity != NB_ENTITY_INVALID || editor->reparent_open) {
		editor_hierarchy_reset_drag(editor);
		return;
	}
	int mx = ctx->input.mouse.pos.x;
	int my = ctx->input.mouse.pos.y;
	if (!editor->hierarchy_drag_active && editor->hierarchy_drag_candidate == NB_ENTITY_INVALID && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT)) {
		for (int i = 0; i < rows->count; i++) {
			if (!nk_input_is_mouse_hovering_rect(&ctx->input, rows->rows[i].rect)) { continue; }
			editor->hierarchy_drag_candidate = rows->rows[i].id;
			editor->hierarchy_drag_start_x = mx;
			editor->hierarchy_drag_start_y = my;
			break;
		}
	}
	if (editor->hierarchy_drag_candidate != NB_ENTITY_INVALID && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
		int dx = mx - editor->hierarchy_drag_start_x;
		int dy = my - editor->hierarchy_drag_start_y;
		if (dx * dx + dy * dy > 36) {
			editor->hierarchy_drag_entity = editor->hierarchy_drag_candidate;
			editor->hierarchy_drag_candidate = NB_ENTITY_INVALID;
			editor->hierarchy_drag_active = 1;
		}
	}
	if (editor->hierarchy_drag_active) {
		editor_hierarchy_update_drop(editor, engine, rows);
		editor_hierarchy_draw_drop_feedback(editor, rows);
		if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT)) {
			editor_hierarchy_commit_drag(editor, engine);
		}
	}
	else if (!nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
		editor->hierarchy_drag_candidate = NB_ENTITY_INVALID;
	}
}

static void editor_hierarchy_finish_rename(editor_t* editor, engine_t* engine, unsigned int id, int commit) {
	if (commit) {
		if (!editor_rename_node(editor, engine, id, editor->rename_buffer)) {
			node_t* node = scene_get_node(engine->scene_graph, id);
			if (node) { snprintf(editor->rename_buffer, sizeof(editor->rename_buffer), "%s", node->name); }
		}
	}
	editor->rename_entity = NB_ENTITY_INVALID;
	editor->rename_focus = 0;
}

static int editor_hierarchy_draw_rename_row(editor_t* editor, engine_t* engine, node_t* node, float row_h, int force_open, editor_hierarchy_rows_t* rows) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	struct nk_panel* layout = ctx->current->layout;
	float avail = layout->bounds.w;
	if (avail < 60.0f) { avail = 60.0f; }
	float ratio[2];
	int cols = node->child_count > 0 ? 2 : 1;
	int opened = 0;
	if (cols == 2) {
		unsigned char* state = editor_hierarchy_state_ptr(editor, node->id);
		opened = force_open || (state && *state);
		ratio[0] = 20.0f;
		ratio[1] = avail - 20.0f;
	}
	else {
		ratio[0] = avail;
	}
	nk_layout_row(ctx, NK_STATIC, row_h, cols, ratio);
	if (cols == 2 && nk_button_symbol(ctx, opened ? NK_SYMBOL_TRIANGLE_DOWN : NK_SYMBOL_TRIANGLE_RIGHT)) {
		unsigned char* state = editor_hierarchy_state_ptr(editor, node->id);
		opened = !opened;
		if (state && !force_open) { *state = opened ? 1 : 0; }
	}
	if (editor->rename_focus) {
		nk_edit_focus(ctx, NK_EDIT_AUTO_SELECT | NK_EDIT_GOTO_END_ON_ACTIVATE);
		editor->rename_focus = 0;
	}
	nk_flags edit_flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->rename_buffer, (int)sizeof(editor->rename_buffer), nk_filter_default);
	int cancel = nk_input_is_key_pressed(&ctx->input, NK_KEY_TEXT_RESET_MODE);
	if (cancel) { editor_hierarchy_finish_rename(editor, engine, node->id, 0); }
	else if ((edit_flags & NK_EDIT_COMMITTED) || (edit_flags & NK_EDIT_DEACTIVATED)) { editor_hierarchy_finish_rename(editor, engine, node->id, 1); }
	editor_hierarchy_store_row(rows, ctx, node->id, row_h);
	return opened;
}

static int editor_node_is_descendant(scene_t* scene, unsigned int ancestor, unsigned int id) {
	if (!scene || ancestor == NB_ENTITY_INVALID || id == NB_ENTITY_INVALID || ancestor == id) { return 0; }
	node_t* node = scene_get_node(scene, id);
	for (int depth = 0; node && depth < NB_SCENE_MAX_ENTITIES; depth++) {
		if (node->parent == ancestor) { return 1; }
		node = scene_get_node(scene, node->parent);
	}
	return 0;
}

static void editor_create_node(editor_t* editor, engine_t* engine, unsigned int parent) {
	if (!editor || !engine || !engine->scene_graph) { return; }
	editor_history_push(editor, engine);
	entity_id_t id = scene_create_entity(engine->scene_graph);
	if (id == NB_ENTITY_INVALID) { return; }
	transform_t t;
	memset(&t, 0, sizeof(t));
	t.scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
	scene_add_transform(engine->scene_graph, id, t);
	if (editor->new_node_name[0]) { scene_set_node_name(engine->scene_graph, id, editor->new_node_name); }
	if (parent != NB_ENTITY_INVALID && scene_get_node(engine->scene_graph, parent)) { scene_set_node_parent(engine->scene_graph, id, parent); }
	editor->selected_entity = id;
	unsigned char* state = editor_hierarchy_state_ptr(editor, id);
	if (state) { *state = 0; }
	state = editor_hierarchy_state_ptr(editor, parent);
	if (state) { *state = 1; }
	engine->scene_doc.dirty = 1;
	editor_set_status(editor, "node created");
}

static void editor_duplicate_node(editor_t* editor, engine_t* engine, unsigned int id) {
	if (!editor || !engine || !engine->scene_graph || id == NB_ENTITY_INVALID) { return; }
	node_t* source = scene_get_node(engine->scene_graph, id);
	if (!source) { return; }
	editor_history_push(editor, engine);
	if (!scene_duplicate_entity(engine->scene_graph, id)) { return; }
	unsigned int copy = (unsigned int)engine->scene_graph->entity_count;
	editor->selected_entity = copy;
	unsigned char* state = editor_hierarchy_state_ptr(editor, copy);
	if (state) { *state = 0; }
	state = editor_hierarchy_state_ptr(editor, source->parent);
	if (state) { *state = 1; }
	engine->scene_doc.dirty = 1;
	editor_set_status(editor, "node duplicated");
}

static void editor_delete_node(editor_t* editor, engine_t* engine, unsigned int id) {
	if (!editor || !engine || !engine->scene_graph || id == NB_ENTITY_INVALID) { return; }
	node_t* node = scene_get_node(engine->scene_graph, id);
	if (!node) { return; }
	unsigned int parent = node->parent;
	editor_history_push(editor, engine);
	if (!scene_remove_entity(engine->scene_graph, id)) { return; }
	unsigned char* state = editor_hierarchy_state_ptr(editor, id);
	if (state) { *state = 0; }
	editor->selected_entity = scene_get_node(engine->scene_graph, parent) ? parent : NB_ENTITY_INVALID;
	editor->hierarchy_context_entity = NB_ENTITY_INVALID;
	if (editor->rename_entity == id) { editor->rename_entity = NB_ENTITY_INVALID; editor->rename_focus = 0; }
	if (editor->reparent_source == id) { editor->reparent_source = NB_ENTITY_INVALID; editor->reparent_open = 0; }
	engine->scene_doc.dirty = 1;
	editor_set_status(editor, "node deleted");
}

static void editor_reparent_node(editor_t* editor, engine_t* engine, unsigned int id, unsigned int parent) {
	if (!editor || !engine || !engine->scene_graph || id == NB_ENTITY_INVALID || id == parent) { return; }
	if (parent != NB_ENTITY_INVALID && !scene_get_node(engine->scene_graph, parent)) { return; }
	if (editor_node_is_descendant(engine->scene_graph, id, parent)) {
		editor_set_status(editor, "reparent failed: cycle");
		return;
	}
	editor_history_push(editor, engine);
	if (!scene_set_node_parent(engine->scene_graph, id, parent)) {
		editor_set_status(editor, "reparent failed");
		return;
	}
	unsigned char* state = editor_hierarchy_state_ptr(editor, parent);
	if (state) { *state = 1; }
	engine->scene_doc.dirty = 1;
	editor_set_status(editor, parent == NB_ENTITY_INVALID ? "node moved to root" : "node reparented");
}

static void editor_start_rename(editor_t* editor, engine_t* engine, unsigned int id) {
	if (!editor || !engine || !engine->scene_graph || id == NB_ENTITY_INVALID) { return; }
	node_t* node = scene_get_node(engine->scene_graph, id);
	if (!node) { return; }
	editor->rename_entity = id;
	snprintf(editor->rename_buffer, sizeof(editor->rename_buffer), "%s", node->name);
	editor->rename_focus = 1;
	editor->hierarchy_context_entity = NB_ENTITY_INVALID;
}

static void editor_draw_hierarchy_node(editor_t* editor, engine_t* engine, scene_t* scene, node_t* node, int depth, int manual_indent, editor_hierarchy_rows_t* rows) {
	if (!editor || !engine || !scene || !node) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	float row_h = editor_hierarchy_row_height(ctx);
	char label[192];
	editor_hierarchy_make_label(node, depth, manual_indent, label, sizeof(label));
	int force_open = 0;
	if (editor->hierarchy_filter[0] && node->child_count > 0) {
		for (int i = 0; i < node->child_count; i++) {
			node_t* child = scene_get_node(scene, node->children[i]);
			if (child && editor_hierarchy_subtree_matches(scene, child, editor->hierarchy_filter, depth + 1)) {
				force_open = 1;
				break;
			}
		}
	}

	if (editor->rename_entity == node->id) {
		int opened = editor_hierarchy_draw_rename_row(editor, engine, node, row_h, force_open, rows);
		if (opened) {
			for (int i = 0; i < node->child_count; i++) {
				node_t* child = scene_get_node(scene, node->children[i]);
				if (child) { editor_draw_hierarchy_node(editor, engine, scene, child, depth + 1, 1, rows); }
			}
		}
		return;
	}

	if (node->child_count > 0) {
		unsigned char* state = editor_hierarchy_state_ptr(editor, node->id);
		enum nk_collapse_states collapse = force_open || (state && *state) ? NK_MAXIMIZED : NK_MINIMIZED;
		nk_bool selected = editor->selected_entity == node->id ? nk_true : nk_false;
		nk_bool previous = selected;
		nk_bool opened = nk_tree_element_image_push_hashed_base(ctx, NK_TREE_NODE, NULL, label, (int)strlen(label), &collapse, &selected);
		if (!force_open && state) { *state = collapse == NK_MAXIMIZED ? 1 : 0; }
		if (selected != previous) { editor->selected_entity = node->id; }
		editor_hierarchy_store_row(rows, ctx, node->id, row_h);
		if (opened) {
			for (int i = 0; i < node->child_count; i++) {
				node_t* child = scene_get_node(scene, node->children[i]);
				if (child) { editor_draw_hierarchy_node(editor, engine, scene, child, depth + 1, manual_indent, rows); }
			}
			nk_tree_element_pop(ctx);
		}
	}
	else {
		nk_bool selected = editor->selected_entity == node->id ? nk_true : nk_false;
		if (nk_selectable_label(ctx, label, NK_TEXT_LEFT, &selected)) { editor->selected_entity = node->id; }
		editor_hierarchy_store_row(rows, ctx, node->id, row_h);
	}
}

static void editor_node_path(editor_t* editor, engine_t* engine, unsigned int id, char* out, size_t size) {
	if (!editor || !engine || !engine->scene_graph || !out || size < 2) { return; }
	const char* names[NB_SCENE_MAX_ENTITIES];
	int count = 0;
	node_t* node = scene_get_node(engine->scene_graph, id);
	while (node && count < NB_SCENE_MAX_ENTITIES) {
		names[count++] = node->name;
		node = scene_get_node(engine->scene_graph, node->parent);
	}
	size_t pos = 0;
	for (int i = count - 1; i >= 0; i--) {
		if (pos && pos + 1 < size) { out[pos++] = '/'; }
		size_t len = strlen(names[i]);
		if (len > size - pos - 1) { len = size - pos - 1; }
		memcpy(out + pos, names[i], len);
		pos += len;
	}
	out[pos] = '\0';
}

static void editor_copy_path(editor_t* editor, engine_t* engine, unsigned int id) {
	char path[512];
	editor_node_path(editor, engine, id, path, sizeof(path));
	if (!path[0]) { return; }
	SDL_SetClipboardText(path);
	editor_set_status(editor, "path copied");
}

static void editor_draw_hierarchy_context_menu(editor_t* editor, engine_t* engine) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (editor->hierarchy_context_entity == NB_ENTITY_INVALID || !engine->scene_graph) { return; }
	unsigned int id = editor->hierarchy_context_entity;
	struct nk_rect trigger = nk_rect(editor->hierarchy_context_rect[0], editor->hierarchy_context_rect[1], editor->hierarchy_context_rect[2], editor->hierarchy_context_rect[3]);
	if (nk_contextual_begin(ctx, NK_WINDOW_BORDER | NK_WINDOW_TITLE, nk_vec2(190, 190), trigger)) {
		int action_taken = 0;
		if (!scene_get_node(engine->scene_graph, id)) {
			editor->hierarchy_context_entity = NB_ENTITY_INVALID;
			nk_contextual_close(ctx);
		}
		else {
			editor->selected_entity = id;
			nk_layout_row_dynamic(ctx, 22, 1);
			if (nk_contextual_item_label(ctx, "Rename", NK_TEXT_LEFT)) {
				editor_start_rename(editor, engine, id);
				action_taken = 1;
			}
			if (nk_contextual_item_label(ctx, "Duplicate", NK_TEXT_LEFT)) {
				editor_duplicate_node(editor, engine, id);
				action_taken = 1;
			}
			if (nk_contextual_item_label(ctx, "Delete", NK_TEXT_LEFT)) {
				editor_delete_node(editor, engine, id);
				action_taken = 1;
			}
			if (nk_contextual_item_label(ctx, "Create Child", NK_TEXT_LEFT)) {
				editor_create_node(editor, engine, id);
				action_taken = 1;
			}
			if (nk_contextual_item_label(ctx, "Reparent...", NK_TEXT_LEFT)) {
				editor->reparent_source = id;
				editor->reparent_open = 1;
				action_taken = 1;
			}
			if (nk_contextual_item_label(ctx, "Copy Path", NK_TEXT_LEFT)) {
				editor_copy_path(editor, engine, id);
				action_taken = 1;
			}
			if (action_taken) { editor->hierarchy_context_entity = NB_ENTITY_INVALID; }
		}
		nk_contextual_end(ctx);
	}
	else {
		editor->hierarchy_context_entity = NB_ENTITY_INVALID;
	}
}

static void editor_draw_hierarchy(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (!engine->scene_graph) { return; }

	editor_hierarchy_rows_t rows;
	rows.count = 0;
	int panel_w = 340;
	int panel_x = 10;
	int panel_y = 240;
	int panel_h = win_h - panel_y - 10;
	if (panel_w > win_w - 20) { panel_w = win_w - 20; }
	if (panel_h < 120) { panel_h = 120; }
	if (nk_begin(ctx, "Hierarchy", nk_rect((float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 22, 2);
		nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, editor->new_node_name, (int)sizeof(editor->new_node_name), nk_filter_default);
		if (nk_button_label(ctx, "Create")) { editor_create_node(editor, engine, editor->selected_entity); }

		nk_layout_row_dynamic(ctx, 22, 2);
		nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, editor->hierarchy_filter, (int)sizeof(editor->hierarchy_filter), nk_filter_default);
		if (nk_button_label(ctx, "Clear")) { editor->hierarchy_filter[0] = '\0'; }

		nk_layout_row_dynamic(ctx, 22, 3);
		if (nk_button_label(ctx, "Child") && editor->selected_entity != NB_ENTITY_INVALID) { editor_create_node(editor, engine, editor->selected_entity); }
		if (nk_button_label(ctx, "Duplicate") && editor->selected_entity != NB_ENTITY_INVALID) { editor_duplicate_node(editor, engine, editor->selected_entity); }
		if (nk_button_label(ctx, "Delete") && editor->selected_entity != NB_ENTITY_INVALID) { editor_delete_node(editor, engine, editor->selected_entity); }

		int list_h = panel_h - 122;
		if (list_h < 60) { list_h = 60; }
		nk_layout_row_dynamic(ctx, (float)list_h, 1);
		if (nk_group_scrolled_offset_begin(ctx, &editor->hierarchy_scroll_x, &editor->hierarchy_scroll_y, "HierarchyNodes", NK_WINDOW_BORDER)) {
			for (int i = 0; i < engine->scene_graph->node_count; i++) {
				node_t* node = &engine->scene_graph->nodes[i];
				if (node->parent != NB_ENTITY_INVALID) { continue; }
				if (!editor_hierarchy_subtree_matches(engine->scene_graph, node, editor->hierarchy_filter, 0)) { continue; }
				editor_draw_hierarchy_node(editor, engine, engine->scene_graph, node, 0, 0, &rows);
			}
			for (int i = 0; i < rows.count; i++) {
				if (editor->hierarchy_context_entity != NB_ENTITY_INVALID || editor->hierarchy_drag_active || editor->hierarchy_drag_candidate != NB_ENTITY_INVALID) { break; }
				if (!nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, rows.rows[i].rect)) { continue; }
				editor->hierarchy_context_entity = rows.rows[i].id;
				editor->hierarchy_context_rect[0] = rows.rows[i].rect.x;
				editor->hierarchy_context_rect[1] = rows.rows[i].rect.y;
				editor->hierarchy_context_rect[2] = rows.rows[i].rect.w;
				editor->hierarchy_context_rect[3] = rows.rows[i].rect.h;
				editor->selected_entity = rows.rows[i].id;
			}
			if (editor->hierarchy_context_entity == NB_ENTITY_INVALID && editor->rename_entity == NB_ENTITY_INVALID && !editor->reparent_open && !editor->hierarchy_drag_active && editor->hierarchy_drag_candidate == NB_ENTITY_INVALID && !editor->want_capture_keyboard && rows.count > 0) {
				int index = -1;
				for (int i = 0; i < rows.count; i++) {
					if (rows.rows[i].id == editor->selected_entity) { index = i; break; }
				}
				if (index < 0) { index = 0; editor->selected_entity = rows.rows[0].id; }
				node_t* current = scene_get_node(engine->scene_graph, rows.rows[index].id);
				if (nk_input_is_key_pressed(&ctx->input, NK_KEY_F2)) {
					editor_start_rename(editor, engine, rows.rows[index].id);
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DEL)) {
					editor_delete_node(editor, engine, rows.rows[index].id);
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP)) {
					if (index > 0) { editor->selected_entity = rows.rows[index - 1].id; }
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
					if (index + 1 < rows.count) { editor->selected_entity = rows.rows[index + 1].id; }
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT)) {
					if (current && current->child_count > 0) {
						unsigned char* state = editor_hierarchy_state_ptr(editor, current->id);
						if (state && *state) { *state = 0; }
						else if (current->parent != NB_ENTITY_INVALID) { editor->selected_entity = current->parent; }
					}
					else if (current && current->parent != NB_ENTITY_INVALID) {
						editor->selected_entity = current->parent;
					}
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT)) {
					if (current && current->child_count > 0) {
						unsigned char* state = editor_hierarchy_state_ptr(editor, current->id);
						if (state && !*state) { *state = 1; }
						else {
							node_t* child = scene_get_node(engine->scene_graph, current->children[0]);
							if (child) { editor->selected_entity = child->id; }
						}
					}
				}
			}
			editor_hierarchy_handle_drag(editor, engine, &rows);
			nk_group_scrolled_end(ctx);
		}
		editor_draw_hierarchy_context_menu(editor, engine);
	}
	nk_end(ctx);
}

static void editor_draw_reparent(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	if (!editor->reparent_open || editor->reparent_source == NB_ENTITY_INVALID || !engine->scene_graph) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	node_t* source = scene_get_node(engine->scene_graph, editor->reparent_source);
	if (!source) {
		editor->reparent_open = 0;
		editor->reparent_source = NB_ENTITY_INVALID;
		return;
	}
	int w = 300;
	int h = 420;
	int x = win_w > w ? (win_w - w) / 2 : 0;
	int y = win_h > h ? (win_h - h) / 2 : 0;
	if (nk_begin(ctx, "Reparent", nk_rect((float)x, (float)y, (float)w, (float)h), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
		nk_layout_row_dynamic(ctx, 22, 1);
		nk_labelf(ctx, NK_TEXT_LEFT, "move: %s", source->name);
		if (nk_button_label(ctx, "<root>")) {
			editor_reparent_node(editor, engine, editor->reparent_source, NB_ENTITY_INVALID);
			editor->reparent_open = 0;
			editor->reparent_source = NB_ENTITY_INVALID;
		}
		for (int i = 0; i < engine->scene_graph->node_count; i++) {
			node_t* node = &engine->scene_graph->nodes[i];
			if (node->id == editor->reparent_source) { continue; }
			if (editor_node_is_descendant(engine->scene_graph, editor->reparent_source, node->id)) { continue; }
			char label[160];
			editor_hierarchy_make_label(node, 0, 0, label, sizeof(label));
			if (nk_button_label(ctx, label)) {
				editor_reparent_node(editor, engine, editor->reparent_source, node->id);
				editor->reparent_open = 0;
				editor->reparent_source = NB_ENTITY_INVALID;
				break;
			}
		}
		if (nk_button_label(ctx, "Cancel")) {
			editor->reparent_open = 0;
			editor->reparent_source = NB_ENTITY_INVALID;
		}
	}
	nk_end(ctx);
}

static void editor_draw_inspector(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int panel_w = 340;
	int panel_x = win_w - panel_w - 10;
	int panel_y = 10;
	int panel_h = win_h - 290;
	if (panel_x < 360) { panel_x = 360; panel_w = win_w - panel_x - 10; }
	if (panel_w < 240) { panel_w = 240; }
	if (panel_h < 180) { panel_h = 180; }
	if (nk_begin(ctx, "Inspector", nk_rect((float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
		nk_layout_row_dynamic(ctx, 18, 1);
		if (!engine->scene_graph || editor->selected_entity == NB_ENTITY_INVALID) {
			nk_label(ctx, "No node selected", NK_TEXT_LEFT);
			nk_end(ctx);
			return;
		}

		node_t* node = scene_get_node(engine->scene_graph, editor->selected_entity);
		if (!node) {
			nk_label(ctx, "Selected node missing", NK_TEXT_LEFT);
			nk_end(ctx);
			return;
		}

		nk_labelf(ctx, NK_TEXT_LEFT, "id: %u", (unsigned int)node->id);
		if (editor->inspector_name_entity != node->id) {
			editor->inspector_name_entity = node->id;
			snprintf(editor->inspector_name, sizeof(editor->inspector_name), "%s", node->name);
		}
		{
			nk_flags name_flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, editor->inspector_name, (int)sizeof(editor->inspector_name), nk_filter_default);
			if ((name_flags & NK_EDIT_COMMITTED) || (name_flags & NK_EDIT_DEACTIVATED)) {
				if (!editor_rename_node(editor, engine, node->id, editor->inspector_name)) {
					snprintf(editor->inspector_name, sizeof(editor->inspector_name), "%s", node->name);
				}
			}
		}
		nk_labelf(ctx, NK_TEXT_LEFT, "parent: %s", node->parent == NB_ENTITY_INVALID ? "<none>" : editor_node_name(engine, node->parent));
		{
			int visible = node->visible;
			if (nk_checkbox_label(ctx, "Visible", &visible) && visible != node->visible) {
				editor_history_push(editor, engine);
				node->visible = visible;
				engine->scene_doc.dirty = 1;
			}
		}

		if (node->components & NB_NODE_COMPONENT_TRANSFORM) {
			transform_t before_t = node->transform;
			nk_label(ctx, "Transform", NK_TEXT_LEFT);
			nk_property_float(ctx, "Pos X", -1000.0f, &node->transform.position.x, 1000.0f, 0.1f, 0.1f);
			nk_property_float(ctx, "Pos Y", -1000.0f, &node->transform.position.y, 1000.0f, 0.1f, 0.1f);
			nk_property_float(ctx, "Pos Z", -1000.0f, &node->transform.position.z, 1000.0f, 0.1f, 0.1f);
			nk_property_float(ctx, "Rot X", -1000.0f, &node->transform.rotation.x, 1000.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Rot Y", -1000.0f, &node->transform.rotation.y, 1000.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Rot Z", -1000.0f, &node->transform.rotation.z, 1000.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Scale X", 0.001f, &node->transform.scale.x, 1000.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Scale Y", 0.001f, &node->transform.scale.y, 1000.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Scale Z", 0.001f, &node->transform.scale.z, 1000.0f, 0.01f, 0.01f);
			if (memcmp(&before_t, &node->transform, sizeof(transform_t)) != 0) {
				if (!editor->inspector_editing) {
					editor_snapshot_t snap;
					if (editor_snapshot_capture(editor, engine, &snap) == NB_RESULT_OK) {
						if (editor->has_pre_edit) { editor_snapshot_free(&editor->pre_edit); }
						editor->pre_edit = snap;
						editor->has_pre_edit = 1;
					}
					editor->inspector_editing = 1;
				}
				engine->scene_doc.dirty = 1;
			}
		}

		if (node->components & NB_NODE_COMPONENT_CAMERA) {
			nk_label(ctx, "Camera", NK_TEXT_LEFT);
			nk_property_float(ctx, "FOV", 1.0f, &node->camera.fov, 179.0f, 0.1f, 0.1f);
			nk_property_float(ctx, "Near", 0.001f, &node->camera.near_plane, node->camera.far_plane - 0.001f, 0.01f, 0.01f);
			nk_property_float(ctx, "Far", node->camera.near_plane + 0.001f, &node->camera.far_plane, 10000.0f, 0.1f, 0.1f);
			if (nk_button_label(ctx, "Set Active Camera")) {
				scene_set_active_camera(engine->scene_graph, node->id);
				editor_set_status(editor, "active camera updated");
			}
		}

		if (node->components & NB_NODE_COMPONENT_LIGHT) {
			nk_label(ctx, "Light", NK_TEXT_LEFT);
			nk_property_float(ctx, "Dir X", -1.0f, &node->light.direction.x, 1.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Dir Y", -1.0f, &node->light.direction.y, 1.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Dir Z", -1.0f, &node->light.direction.z, 1.0f, 0.01f, 0.01f);
			nk_property_float(ctx, "Intensity", 0.0f, &node->light.intensity, 10.0f, 0.01f, 0.01f);
		}

		if (node->components & NB_NODE_COMPONENT_COLLIDER) {
			nk_label(ctx, "Collider", NK_TEXT_LEFT);
			nk_labelf(ctx, NK_TEXT_LEFT, "shape: %s", node->collider.shape == NB_COLLIDER_SPHERE ? "sphere" : "aabb");
			if (node->collider.shape == NB_COLLIDER_SPHERE) {
				nk_property_float(ctx, "Radius", 0.001f, &node->collider.radius, 1000.0f, 0.01f, 0.01f);
			}
			else {
				nk_property_float(ctx, "Ext X", 0.001f, &node->collider.extents.x, 1000.0f, 0.01f, 0.01f);
				nk_property_float(ctx, "Ext Y", 0.001f, &node->collider.extents.y, 1000.0f, 0.01f, 0.01f);
				nk_property_float(ctx, "Ext Z", 0.001f, &node->collider.extents.z, 1000.0f, 0.01f, 0.01f);
			}
		}

		if (node->components & NB_NODE_COMPONENT_SCRIPT) {
			nk_labelf(ctx, NK_TEXT_LEFT, "script: %s", node->script_module);
		}

		if (nk_button_label(ctx, "Focus Selected")) {
			node_t* sn = scene_get_node(engine->scene_graph, editor->selected_entity);
			if (sn) {
				editor->camera_pos[0] = sn->world_position.x;
				editor->camera_pos[1] = sn->world_position.y + 2.0f;
				editor->camera_pos[2] = sn->world_position.z + 5.0f;
				editor->camera_yaw = -1.5707963f;
				editor->camera_pitch = -0.35f;
				editor_set_status(editor, "camera focused");
			}
		}
	}
	nk_end(ctx);
}

void editor_begin_frame(editor_t* editor, engine_t* engine) {
	(void)engine;
	if (!editor_enabled(editor)) { return; }
	nk_input_begin((struct nk_context*)editor->nk_ctx);
}

static void editor_apply_gizmo(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || !engine->scene_graph) { return; }
	if (editor->selected_entity == NB_ENTITY_INVALID) { return; }
	node_t* node = scene_get_node(engine->scene_graph, editor->selected_entity);
	if (!node || !(node->components & NB_NODE_COMPONENT_TRANSFORM)) { return; }

	int mx = engine->input.mouse_x;
	int my = engine->input.mouse_y;
	int buttons = platform_mouse_buttons(&engine->platform);
	int in_viewport = mx >= editor->viewport_x && mx < editor->viewport_x + editor->viewport_w && my >= editor->viewport_y && my < editor->viewport_y + editor->viewport_h;

	if (!(buttons & SDL_BUTTON_LMASK)) {
		if (editor->gizmo_dragging) {
			engine->scene_doc.dirty = 1;
			editor_history_push(editor, engine);
		}
		editor->gizmo_dragging = 0;
		editor->gizmo_axis = -1;
		return;
	}

	if (!in_viewport || editor->want_capture_mouse) { return; }

	if (!editor->gizmo_dragging) {
		entity_id_t picked = NB_ENTITY_INVALID;
		if (editor->tool == NB_EDITOR_TOOL_SELECT) {
			if (editor_pick_node(editor, engine, mx, my, &picked)) {
				editor->selected_entity = picked;
				editor_set_status(editor, "node selected");
			}
			return;
		}

		int axis = editor_pick_gizmo_axis(editor, engine, mx, my);
		if (axis < 0) {
			if (editor_pick_node(editor, engine, mx, my, &picked)) {
				editor->selected_entity = picked;
			}
			return;
		}

		editor_history_push(editor, engine);
		editor->gizmo_dragging = 1;
		editor->gizmo_axis = axis;
		editor->gizmo_drag_mouse_x = mx;
		editor->gizmo_drag_mouse_y = my;
		editor->gizmo_drag_start[0] = node->transform.position.x;
		editor->gizmo_drag_start[1] = node->transform.position.y;
		editor->gizmo_drag_start[2] = node->transform.position.z;
		if (editor->tool == NB_EDITOR_TOOL_ROTATE) {
			editor->gizmo_drag_start[0] = node->transform.rotation.x;
			editor->gizmo_drag_start[1] = node->transform.rotation.y;
			editor->gizmo_drag_start[2] = node->transform.rotation.z;
		}
		else if (editor->tool == NB_EDITOR_TOOL_SCALE) {
			editor->gizmo_drag_start[0] = node->transform.scale.x;
			editor->gizmo_drag_start[1] = node->transform.scale.y;
			editor->gizmo_drag_start[2] = node->transform.scale.z;
		}
		return;
	}

	int dx = mx - editor->gizmo_drag_mouse_x;
	int dy = my - editor->gizmo_drag_mouse_y;
	float delta = (float)(dx - dy) * 0.01f;
	int axis = editor->gizmo_axis;
	if (editor->tool == NB_EDITOR_TOOL_MOVE) {
		if (axis == 0) { node->transform.position.x = editor->gizmo_drag_start[0] + (float)dx * 0.01f; }
		else if (axis == 1) { node->transform.position.y = editor->gizmo_drag_start[1] - (float)dy * 0.01f; }
		else if (axis == 2) { node->transform.position.z = editor->gizmo_drag_start[2] + (float)dx * 0.01f; }
	}
	else if (editor->tool == NB_EDITOR_TOOL_ROTATE) {
		float rdelta = (float)(dx - dy) * 0.01f;
		if (axis == 0) { node->transform.rotation.x = editor->gizmo_drag_start[0] + rdelta; }
		else if (axis == 1) { node->transform.rotation.y = editor->gizmo_drag_start[1] + rdelta; }
		else if (axis == 2) { node->transform.rotation.z = editor->gizmo_drag_start[2] + rdelta; }
		node->transform.previous_rotation = node->transform.rotation;
	}
	else if (editor->tool == NB_EDITOR_TOOL_SCALE) {
		float s = 1.0f + delta * 0.5f;
		if (s < 0.01f) { s = 0.01f; }
		if (axis == 0) { node->transform.scale.x = editor->gizmo_drag_start[0] * s; }
		else if (axis == 1) { node->transform.scale.y = editor->gizmo_drag_start[1] * s; }
		else if (axis == 2) { node->transform.scale.z = editor->gizmo_drag_start[2] * s; }
	}
}

static void editor_update_camera(editor_t* editor, engine_t* engine, float dt) {
	if (!editor || !engine) { return; }
	if (editor->want_capture_keyboard) { return; }

	float speed = editor->camera_speed * dt;
	float yaw = editor->camera_yaw;
	float pitch = editor->camera_pitch;
	float forward[3] = { cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) };
	float right[3] = { -sinf(yaw), 0.0f, cosf(yaw) };

	int mouse_buttons = platform_mouse_buttons(&engine->platform);
	int in_viewport = engine->input.mouse_x >= editor->viewport_x && engine->input.mouse_x < editor->viewport_x + editor->viewport_w && engine->input.mouse_y >= editor->viewport_y && engine->input.mouse_y < editor->viewport_y + editor->viewport_h;
	if ((mouse_buttons & SDL_BUTTON_RMASK) && in_viewport && !editor->want_capture_mouse) {
		editor->camera_yaw += (float)engine->input.mouse_dx * 0.005f;
		editor->camera_pitch -= (float)engine->input.mouse_dy * 0.005f;
		if (editor->camera_pitch > 1.5f) { editor->camera_pitch = 1.5f; }
		if (editor->camera_pitch < -1.5f) { editor->camera_pitch = -1.5f; }
		yaw = editor->camera_yaw;
		pitch = editor->camera_pitch;
		forward[0] = cosf(pitch) * cosf(yaw);
		forward[1] = sinf(pitch);
		forward[2] = cosf(pitch) * sinf(yaw);
		right[0] = -sinf(yaw);
		right[2] = cosf(yaw);
	}

	if (input_is_held(&engine->input, "forward")) {
		editor->camera_pos[0] += forward[0] * speed;
		editor->camera_pos[1] += forward[1] * speed;
		editor->camera_pos[2] += forward[2] * speed;
	}
	if (input_is_held(&engine->input, "backward")) {
		editor->camera_pos[0] -= forward[0] * speed;
		editor->camera_pos[1] -= forward[1] * speed;
		editor->camera_pos[2] -= forward[2] * speed;
	}
	if (input_is_held(&engine->input, "left")) {
		editor->camera_pos[0] -= right[0] * speed;
		editor->camera_pos[1] -= right[1] * speed;
		editor->camera_pos[2] -= right[2] * speed;
	}
	if (input_is_held(&engine->input, "right")) {
		editor->camera_pos[0] += right[0] * speed;
		editor->camera_pos[1] += right[1] * speed;
		editor->camera_pos[2] += right[2] * speed;
	}
	if (input_is_held(&engine->input, "jump")) {
		editor->camera_pos[1] += speed;
	}
}

void editor_update(editor_t* editor, engine_t* engine) {
	if (!editor_enabled(editor) || !engine) { return; }

	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	nk_input_end(ctx);

	editor->want_capture_mouse = nk_item_is_any_active(ctx) || nk_window_is_any_hovered(ctx);
	editor->want_capture_keyboard = nk_item_is_any_active(ctx);

	{
		const char* scene_path = editor_scene_path(editor, engine);
		if (scene_path[0]) {
			snprintf(editor->scene_path, sizeof(editor->scene_path), "%s", scene_path);
			if (!engine->scene_doc.path[0]) {
				snprintf(engine->scene_doc.path, sizeof(engine->scene_doc.path), "%s", scene_path);
			}
			if (!engine->config.scene_path[0]) {
				snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%s", scene_path);
			}
		}
		else {
			engine_log(engine, "editor path missing: doc='%s' cfg='%s' ed='%s' mode=%s", engine->scene_doc.path, engine->config.scene_path, editor->scene_path, engine->config.mode);
		}
	}

	if (editor->request_save) { editor_save_scene(editor, engine); editor->request_save = 0; }
	if (editor->request_undo) { editor_undo(editor, engine); editor->request_undo = 0; }
	if (editor->request_redo) { editor_redo(editor, engine); editor->request_redo = 0; }
	editor_fix_selection(editor, engine);

	editor->autosave_timer += 1.0 / 60.0;
	if (editor->autosave_timer >= 5.0) {
		editor->autosave_timer = 0.0;
		if (engine->scene_doc.dirty && !editor->want_capture_keyboard) {
			editor_save_scene(editor, engine);
		}
	}

	if (editor->play_pid > 0) {
		int status = 0;
		pid_t r = waitpid((pid_t)editor->play_pid, &status, WNOHANG);
		if (r != 0) { editor->play_pid = -1; }
	}

	platform_set_mouse_capture_blocked(&engine->platform, 1);
	platform_set_mouse_capture(&engine->platform, 0);

	int win_w = 0;
	int win_h = 0;
	platform_output_size(&engine->platform, &win_w, &win_h);
	if (win_w <= 0) { win_w = 1280; }
	if (win_h <= 0) { win_h = 720; }

	editor_draw_scene_panel(editor, engine, win_w, win_h);
	editor_draw_render_config(editor, engine, win_w, win_h);
	editor_draw_hierarchy(editor, engine, win_w, win_h);
	editor_draw_inspector(editor, engine, win_w, win_h);
	editor_draw_reparent(editor, engine, win_w, win_h);

	editor->toolbar_h = 34;
	editor->viewport_x = 360;
	editor->viewport_y = 10 + editor->toolbar_h + 8;
	editor->viewport_w = win_w - 360 - 360 - 20;
	editor->viewport_h = win_h - editor->viewport_y - 10;
	if (editor->viewport_w < 160) { editor->viewport_w = 160; }
	if (editor->viewport_h < 120) { editor->viewport_h = 120; }

	if (nk_begin(ctx, "Tools", nk_rect((float)editor->viewport_x, 10, (float)editor->viewport_w, (float)editor->toolbar_h), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
		int tools[4] = { NB_EDITOR_TOOL_SELECT, NB_EDITOR_TOOL_MOVE, NB_EDITOR_TOOL_ROTATE, NB_EDITOR_TOOL_SCALE };
		nk_layout_row_static(ctx, 28, 32, 4);
		for (int i = 0; i < 4; i++) {
			int active = editor->tool == tools[i];
			struct nk_style_button style = ctx->style.button;
			if (active) {
				style.normal = style.active;
				style.hover = style.active;
			}
			if (editor->tool_textures[i]) {
				struct nk_image img = nk_image_ptr(editor->tool_textures[i]);
				if (nk_button_image_styled(ctx, &style, img)) { editor->tool = tools[i]; }
			}
			else {
				const char* labels[4] = { "S", "M", "R", "C" };
				if (nk_button_label_styled(ctx, &style, labels[i])) { editor->tool = tools[i]; }
			}
		}
	}
	nk_end(ctx);

	editor_update_camera(editor, engine, 1.0f / 60.0f);
	editor_apply_gizmo(editor, engine);



	if (editor->inspector_editing && !nk_item_is_any_active(ctx)) {
		if (editor->has_pre_edit) {
			if (editor->undo_count >= NB_EDITOR_UNDO_MAX) {
				editor_snapshot_free(&editor->undo[0]);
				memmove(&editor->undo[0], &editor->undo[1], sizeof(editor_snapshot_t) * (size_t)(NB_EDITOR_UNDO_MAX - 1));
				editor->undo_count = NB_EDITOR_UNDO_MAX - 1;
			}
			editor->undo[editor->undo_count++] = editor->pre_edit;
			editor->has_pre_edit = 0;
			memset(&editor->pre_edit, 0, sizeof(editor->pre_edit));
			for (int i = 0; i < editor->redo_count; i++) { editor_snapshot_free(&editor->redo[i]); }
			editor->redo_count = 0;
		}
		editor->inspector_editing = 0;
	}

	if (engine->scene_graph) {
		scene_update_world_transforms(engine->scene_graph);
		scene_rebuild_cache(engine->scene_graph);
	}
}

static void editor_draw_gizmo_sdl(editor_t* editor, engine_t* engine) {
	if (!editor || !engine || !engine->scene_graph) { return; }
	if (editor->selected_entity == NB_ENTITY_INVALID) { return; }
	node_t* node = scene_get_node(engine->scene_graph, editor->selected_entity);
	if (!node || !(node->components & NB_NODE_COMPONENT_TRANSFORM)) { return; }
	SDL_Renderer* r = (SDL_Renderer*)platform_renderer(&engine->platform);
	if (!r) { return; }
	vec3_t origin = node->world_position;
	float ox, oy;
	if (!editor_project_point(editor, origin, &ox, &oy)) { return; }
	vec3_t axes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
	SDL_Color cols[3] = { {220,60,60,255}, {80,220,80,255}, {80,120,255,255} };
	for (int i = 0; i < 3; i++) {
		vec3_t tip = vec3_add(origin, axes[i]);
		float tx, ty;
		if (!editor_project_point(editor, tip, &tx, &ty)) { continue; }
		SDL_Color col = (editor->gizmo_axis == i && editor->gizmo_dragging) ? (SDL_Color){255,255,100,255} : cols[i];
		SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
		SDL_RenderDrawLineF(r, ox, oy, tx, ty);
		float dx = tx - ox;
		float dy = ty - oy;
		float len = sqrtf(dx * dx + dy * dy);
		if (len > 0.001f) { dx /= len; dy /= len; }
		float pxv = -dy;
		float pyv = dx;
		SDL_RenderDrawLineF(r, tx + dx * 8.0f, ty + dy * 8.0f, tx - dx * 1.0f + pxv * 4.0f, ty - dy * 1.0f + pyv * 4.0f);
		SDL_RenderDrawLineF(r, tx + dx * 8.0f, ty + dy * 8.0f, tx - dx * 1.0f - pxv * 4.0f, ty - dy * 1.0f - pyv * 4.0f);
		SDL_RenderDrawLineF(r, tx - dx * 1.0f + pxv * 4.0f, ty - dy * 1.0f + pyv * 4.0f, tx - dx * 1.0f - pxv * 4.0f, ty - dy * 1.0f - pyv * 4.0f);
		float lx = tx + dx * 12.0f;
		float ly = ty + dy * 12.0f;
		if (i == 0) {
			SDL_RenderDrawLineF(r, lx - 3, ly - 3, lx + 3, ly + 3);
			SDL_RenderDrawLineF(r, lx + 3, ly - 3, lx - 3, ly + 3);
		}
		else if (i == 1) {
			SDL_RenderDrawLineF(r, lx - 3, ly - 3, lx, ly);
			SDL_RenderDrawLineF(r, lx + 3, ly - 3, lx, ly);
			SDL_RenderDrawLineF(r, lx, ly, lx, ly + 3);
		}
		else {
			SDL_RenderDrawLineF(r, lx - 3, ly - 3, lx + 3, ly - 3);
			SDL_RenderDrawLineF(r, lx + 3, ly - 3, lx - 3, ly + 3);
			SDL_RenderDrawLineF(r, lx - 3, ly + 3, lx + 3, ly + 3);
		}
	}
}

void editor_render(editor_t* editor, engine_t* engine) {
	if (!editor_enabled(editor)) { return; }
	nk_sdl_render(NK_ANTI_ALIASING_ON);
	editor_draw_gizmo_sdl(editor, engine);
}
