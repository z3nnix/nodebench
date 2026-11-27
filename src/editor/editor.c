#define _XOPEN_SOURCE 700

#include <SDL2/SDL.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "editor/editor.h"
#include "audio/audio.h"
#include "core/engine.h"
#include "editor/editor_app.h"
#include "render/color.h"
#include "resource/loader.h"
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

static void editor_inspector_autosave(editor_t* editor, int force);

static void editor_inspector_mark_dirty(editor_t* editor) {
	editor->inspector_dirty = 1;
	editor->inspector_dirty_time = editor_now_seconds();
}

static void editor_inspector_show_node(editor_t* editor) {
	if (!editor) { return; }
	editor_inspector_autosave(editor, 1);
	editor->inspector_kind = NB_EDITOR_INSPECTOR_NODE;
	editor->inspector_path[0] = '\0';
	editor->inspector_dirty = 0;
	editor->inspector_color_index = -1;
	editor->inspector_scroll_x = 0;
	editor->inspector_scroll_y = 0;
}

static void editor_inspector_set_resource(editor_t* editor, editor_inspector_kind_t kind, const char* path) {
	editor_inspector_autosave(editor, 1);
	editor->inspector_kind = kind;
	snprintf(editor->inspector_path, sizeof(editor->inspector_path), "%.*s", (int)sizeof(editor->inspector_path) - 1, path);
	editor->inspector_node_entity = editor->selected_entity;
	editor->inspector_dirty = 0;
	editor->inspector_color_index = -1;
	editor->inspector_wav_end = 0.0;
	editor->inspector_scroll_x = 0;
	editor->inspector_scroll_y = 0;
}

static int editor_inspector_append(char* buf, size_t cap, int* off, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n = vsnprintf(buf + *off, cap - (size_t)*off, fmt, args);
	va_end(args);
	if (n < 0 || *off + n >= (int)cap) { return 0; }
	*off += n;
	return 1;
}

static void editor_inspector_trim(char* text) {
	char* start = text;
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') { start++; }
	char* end = start + strlen(start);
	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) { end--; }
	*end = '\0';
	if (start != text) { memmove(text, start, (size_t)(end - start) + 1); }
}

static void editor_inspector_strip_comment(char* line) {
	char* comment = strchr(line, ';');
	if (comment) { *comment = '\0'; }
	editor_inspector_trim(line);
}

static void editor_inspector_kv(const char* line, const char* key, char* out, size_t size) {
	out[0] = '\0';
	const char* eq = strchr(line, '=');
	if (!eq) { return; }
	char found[64];
	size_t len = (size_t)(eq - line);
	if (len >= sizeof(found)) { return; }
	memcpy(found, line, len);
	found[len] = '\0';
	editor_inspector_trim(found);
	if (strcmp(found, key) != 0) { return; }
	const char* value = eq + 1;
	while (*value == ' ' || *value == '\t') { value++; }
	len = strlen(value);
	if (len >= size) { len = size - 1; }
	memcpy(out, value, len);
	out[len] = '\0';
	editor_inspector_trim(out);
	len = strlen(out);
	if (len >= 2 && ((out[0] == '"' && out[len - 1] == '"') || (out[0] == '\'' && out[len - 1] == '\''))) {
		memmove(out, out + 1, len - 2);
		out[len - 2] = '\0';
	}
}

static void editor_inspector_parse_reference(const char* value, char* out, size_t size) {
	out[0] = '\0';
	const char* call = strstr(value, "ExtResource");
	if (!call) { return; }
	const char* open = strchr(call, '(');
	if (!open) { return; }
	const char* quote = strchr(open, '"');
	if (!quote) { return; }
	quote++;
	size_t i = 0;
	while (*quote && *quote != '"' && i + 1 < size) { out[i++] = *quote++; }
	out[i] = '\0';
}

static void editor_inspector_parse_color(const char* value, float* out, int count) {
	const char* open = strchr(value, '(');
	if (!open) { return; }
	float colors[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (sscanf(open + 1, "%f , %f , %f , %f", &colors[0], &colors[1], &colors[2], &colors[3]) < 3) { return; }
	for (int i = 0; i < count; i++) { out[i] = colors[i]; }
}

static int editor_inspector_blend_index(const char* name) {
	static const char* const names[] = { "none", "alpha_50", "add", "subtract", "add_quarter" };
	for (int i = 0; i < 5; i++) {
		if (strcmp(name, names[i]) == 0) { return i; }
	}
	return NB_BLEND_NONE;
}

static int editor_inspector_shade_index(const char* name) {
	static const char* const names[] = { "flat", "gouraud" };
	for (int i = 0; i < 2; i++) {
		if (strcmp(name, names[i]) == 0) { return i; }
	}
	return NB_SHADE_GOURAUD;
}

static const char* editor_inspector_blend_name(int index) {
	static const char* const names[] = { "none", "alpha_50", "add", "subtract", "add_quarter" };
	if (index < 0 || index > 4) { return names[0]; }
	return names[index];
}

static const char* editor_inspector_shade_name(int index) {
	static const char* const names[] = { "flat", "gouraud" };
	if (index < 0 || index > 1) { return names[0]; }
	return names[index];
}

static struct nk_color editor_inspector_hsv_color(float h, float s, float v) {
	float sector = h / 60.0f;
	float f = fmodf(sector, 2.0f);
	float x = v * s * (1.0f - fabsf(f - 1.0f));
	float c = v * s;
	float m = v - c;
	float r = m;
	float g = m;
	float b = m;
	if (sector < 1.0f) { r += c; g += x; }
	else if (sector < 2.0f) { r += x; g += c; }
	else if (sector < 3.0f) { g += c; b += x; }
	else if (sector < 4.0f) { g += x; b += c; }
	else if (sector < 5.0f) { r += x; b += c; }
	else { r += c; b += x; }
	return nk_rgb((int)(r * 255.0f), (int)(g * 255.0f), (int)(b * 255.0f));
}

static void editor_inspector_rgb_to_hsv(int r, int g, int b, float* out_h, float* out_s, float* out_v) {
	float rn = (float)r / 255.0f;
	float gn = (float)g / 255.0f;
	float bn = (float)b / 255.0f;
	float max = rn > gn ? rn : gn;
	if (bn > max) { max = bn; }
	float min = rn < gn ? rn : gn;
	if (bn < min) { min = bn; }
	float delta = max - min;
	*out_v = max;
	if (delta < 0.0001f) {
		*out_h = 0.0f;
		*out_s = 0.0f;
		return;
	}
	*out_s = max > 0.0f ? delta / max : 0.0f;
	if (max == rn) { *out_h = 60.0f * fmodf((gn - bn) / delta, 6.0f); }
	else if (max == gn) { *out_h = 60.0f * ((bn - rn) / delta + 2.0f); }
	else { *out_h = 60.0f * ((rn - gn) / delta + 4.0f); }
	if (*out_h < 0.0f) { *out_h += 360.0f; }
}

static void editor_inspector_update_hex(editor_t* editor, struct nk_color color) {
	int a = (int)(editor->inspector_color_a * 255.0f);
	if (editor->inspector_color_target == NB_EDITOR_COLOR_TARGET_TRES) {
		snprintf(editor->inspector_color_hex, sizeof(editor->inspector_color_hex), "#%02X%02X%02X%02X", color.r, color.g, color.b, a);
	}
	else {
		snprintf(editor->inspector_color_hex, sizeof(editor->inspector_color_hex), "#%02X%02X%02X", color.r, color.g, color.b);
	}
}

static void editor_inspector_sync_picker(editor_t* editor, int r, int g, int b, int a) {
	editor_inspector_rgb_to_hsv(r, g, b, &editor->inspector_color_h, &editor->inspector_color_s, &editor->inspector_color_v);
	editor->inspector_color_a = (float)a / 255.0f;
	editor_inspector_update_hex(editor, nk_rgb(r, g, b));
}

static void editor_inspector_apply_picker_color(editor_t* editor) {
	if (editor->inspector_color_h < 0.0f) { editor->inspector_color_h += 360.0f; }
	else if (editor->inspector_color_h >= 360.0f) { editor->inspector_color_h -= 360.0f; }
	if (editor->inspector_color_s < 0.0f) { editor->inspector_color_s = 0.0f; }
	else if (editor->inspector_color_s > 1.0f) { editor->inspector_color_s = 1.0f; }
	if (editor->inspector_color_v < 0.0f) { editor->inspector_color_v = 0.0f; }
	else if (editor->inspector_color_v > 1.0f) { editor->inspector_color_v = 1.0f; }
	if (editor->inspector_color_a < 0.0f) { editor->inspector_color_a = 0.0f; }
	else if (editor->inspector_color_a > 1.0f) { editor->inspector_color_a = 1.0f; }
	struct nk_color color = editor_inspector_hsv_color(editor->inspector_color_h, editor->inspector_color_s, editor->inspector_color_v);
	editor_inspector_update_hex(editor, color);
	if (editor->inspector_color_target == NB_EDITOR_COLOR_TARGET_CLUT) {
		editor_clut_state_t* state = &editor->inspector_clut;
		int index = editor->inspector_color_index;
		if (index < 0 || index >= state->count) { return; }
		state->r[index] = color.r;
		state->g[index] = color.g;
		state->b[index] = color.b;
	}
	else {
		editor_tres_state_t* state = &editor->inspector_tres;
		state->tint[0] = (float)color.r / 255.0f;
		state->tint[1] = (float)color.g / 255.0f;
		state->tint[2] = (float)color.b / 255.0f;
		state->tint[3] = editor->inspector_color_a;
	}
	editor_inspector_mark_dirty(editor);
}

static void editor_inspector_select_clut_color(editor_t* editor, int index) {
	editor_clut_state_t* state = &editor->inspector_clut;
	if (index < 0 || index >= state->count) { return; }
	editor->inspector_color_index = index;
	editor->inspector_color_target = NB_EDITOR_COLOR_TARGET_CLUT;
	editor_inspector_sync_picker(editor, (int)state->r[index], (int)state->g[index], (int)state->b[index], 255);
}

static int editor_inspector_hex_digit(char c) {
	if (c >= '0' && c <= '9') { return c - '0'; }
	if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
	if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
	return -1;
}

static int editor_inspector_parse_hex(editor_t* editor, const char* text) {
	if (!text) { return 0; }
	const char* p = text;
	if (*p == '#') { p++; }
	int values[8];
	int count = 0;
	for (; count < 8; count++) {
		int digit = editor_inspector_hex_digit(p[count]);
		if (digit < 0) { break; }
		values[count] = digit;
	}
	if (count != 6 && count != 8) { return 0; }
	while (p[count] == ' ' || p[count] == '\t') { count++; }
	if (p[count]) { return 0; }
	int r = values[0] * 16 + values[1];
	int g = values[2] * 16 + values[3];
	int b = values[4] * 16 + values[5];
	int a = count == 8 ? values[6] * 16 + values[7] : 255;
	editor_inspector_sync_picker(editor, r, g, b, a);
	editor_inspector_apply_picker_color(editor);
	return 1;
}

static void editor_inspector_load_clut(editor_t* editor, const char* path) {
	editor_clut_state_t* state = &editor->inspector_clut;
	memset(state, 0, sizeof(*state));
	palette_t palette;
	memset(&palette, 0, sizeof(palette));
	if (!clut_load(path, &palette)) {
		editor_set_status(editor, "failed to open clut");
		return;
	}
	state->count = palette.count > NB_EDITOR_INSPECTOR_CLUT_MAX ? NB_EDITOR_INSPECTOR_CLUT_MAX : palette.count;
	for (int i = 0; i < state->count; i++) {
		int r = 0;
		int g = 0;
		int b = 0;
		rgb555_unpack(palette.colors[i], &r, &g, &b);
		state->r[i] = (unsigned char)r;
		state->g[i] = (unsigned char)g;
		state->b[i] = (unsigned char)b;
	}
	clut_free(&palette);
	editor_inspector_set_resource(editor, NB_EDITOR_INSPECTOR_CLUT, path);
}

static void editor_inspector_load_tres(editor_t* editor, engine_t* engine, const char* path) {
	editor_tres_state_t* state = &editor->inspector_tres;
	memset(state, 0, sizeof(*state));
	state->blend_mode = engine ? (int)engine->config.blend_mode : NB_BLEND_NONE;
	state->shade_mode = engine ? (int)engine->config.shade_mode : NB_SHADE_FLAT;
	state->fog_enabled = engine ? engine->config.fog_enabled : 0;
	state->tint[0] = 1.0f;
	state->tint[1] = 1.0f;
	state->tint[2] = 1.0f;
	state->tint[3] = 1.0f;
	state->transparent_index = (int)NB_TEXTURE_TRANSPARENT;
	char* data = NULL;
	size_t size = 0;
	if (!editor_fs_read_text(path, &data, &size) || !data) {
		free(data);
		editor_set_status(editor, "failed to open tres");
		return;
	}
	int in_resource = 0;
	char* save = NULL;
	for (char* line = strtok_r(data, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
		editor_inspector_strip_comment(line);
		if (!line[0]) { continue; }
		if (strncmp(line, "[gd_resource", 12) == 0) {
			snprintf(state->header, sizeof(state->header), "%.*s", (int)sizeof(state->header) - 1, line);
			continue;
		}
		if (strncmp(line, "[ext_resource", 13) == 0) {
			if (state->ext_count < NB_EDITOR_INSPECTOR_TRES_EXT_MAX) {
				snprintf(state->ext[state->ext_count], NB_EDITOR_INSPECTOR_TRES_LINE_MAX, "%.*s", NB_EDITOR_INSPECTOR_TRES_LINE_MAX - 1, line);
				state->ext_count++;
			}
			continue;
		}
		if (strncmp(line, "[resource", 9) == 0) {
			in_resource = 1;
			continue;
		}
		if (!in_resource || line[0] == '[') { continue; }
		char value[256];
		editor_inspector_kv(line, "resource_name", value, sizeof(value));
		if (value[0]) { snprintf(state->name, sizeof(state->name), "%.*s", (int)sizeof(state->name) - 1, value); }
		editor_inspector_kv(line, "texture", value, sizeof(value));
		if (value[0]) { editor_inspector_parse_reference(value, state->texture, sizeof(state->texture)); }
		editor_inspector_kv(line, "palette", value, sizeof(value));
		if (value[0]) { editor_inspector_parse_reference(value, state->palette, sizeof(state->palette)); }
		editor_inspector_kv(line, "blend_mode", value, sizeof(value));
		if (value[0]) { state->blend_mode = editor_inspector_blend_index(value); }
		editor_inspector_kv(line, "shade_mode", value, sizeof(value));
		if (value[0]) { state->shade_mode = editor_inspector_shade_index(value); }
		editor_inspector_kv(line, "fog_enabled", value, sizeof(value));
		if (value[0]) { state->fog_enabled = strcmp(value, "true") == 0 || strcmp(value, "1") == 0; }
		editor_inspector_kv(line, "vertex_color_tint", value, sizeof(value));
		if (value[0]) { editor_inspector_parse_color(value, state->tint, 4); }
		editor_inspector_kv(line, "transparent_index", value, sizeof(value));
		if (value[0]) { state->transparent_index = atoi(value); }
	}
	free(data);
	editor->inspector_color_target = NB_EDITOR_COLOR_TARGET_TRES;
	editor_inspector_sync_picker(editor, (int)(state->tint[0] * 255.0f), (int)(state->tint[1] * 255.0f), (int)(state->tint[2] * 255.0f), (int)(state->tint[3] * 255.0f));
	editor_inspector_set_resource(editor, NB_EDITOR_INSPECTOR_TRES, path);
}

static void editor_inspector_load_wav(editor_t* editor, engine_t* engine, const char* path) {
	editor_wav_state_t* state = &editor->inspector_wav;
	memset(state, 0, sizeof(*state));
	if (editor->inspector_wav_voice >= 0) {
		audio_stop_voice(engine && engine->audio.ready ? &engine->audio : NULL, editor->inspector_wav_voice);
		editor->inspector_wav_voice = -1;
	}
	audio_stream_t stream;
	memset(&stream, 0, sizeof(stream));
	if (!wav_load(path, &stream)) {
		editor_set_status(editor, "failed to open wav");
		return;
	}
	state->sample_rate = (int)stream.sample_rate;
	state->channels = (int)stream.channels;
	state->frame_count = stream.frame_count;
	int total = stream.frame_count * (int)stream.channels;
	if (stream.samples && total > 0) {
		for (int i = 0; i < NB_EDITOR_INSPECTOR_WAV_PEAKS; i++) {
			int begin = total * i / NB_EDITOR_INSPECTOR_WAV_PEAKS;
			int end = total * (i + 1) / NB_EDITOR_INSPECTOR_WAV_PEAKS;
			int peak = 0;
			for (int j = begin; j < end; j++) {
				int sample = (int)stream.samples[j];
				if (sample < 0) { sample = -sample; }
				if (sample > peak) { peak = sample; }
			}
			state->peaks[i] = (float)peak / 32768.0f;
		}
	}
	audio_stream_free(&stream);
	editor_inspector_set_resource(editor, NB_EDITOR_INSPECTOR_WAV, path);
}

static void editor_inspector_open_resource(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !path || !*path) { return; }
	const char* ext = editor_fs_ext(path);
	if (strcmp(ext, "clut") == 0) { editor_inspector_load_clut(editor, path); }
	else if (strcmp(ext, "tres") == 0) { editor_inspector_load_tres(editor, engine, path); }
	else if (strcmp(ext, "wav") == 0) { editor_inspector_load_wav(editor, engine, path); }
}

static void editor_inspector_save_clut(editor_t* editor) {
	editor_clut_state_t* state = &editor->inspector_clut;
	char buf[8192];
	int off = 0;
	if (!editor_inspector_append(buf, sizeof(buf), &off, "%d\n", state->count)) { return; }
	for (int i = 0; i < state->count; i++) {
		if (!editor_inspector_append(buf, sizeof(buf), &off, "%d %d %d\n", (int)state->r[i], (int)state->g[i], (int)state->b[i])) { return; }
	}
	if (editor_fs_write_text(editor->inspector_path, buf, (size_t)off)) {
		editor->inspector_dirty = 0;
		editor_set_status(editor, "clut saved");
	}
	else {
		editor_set_status(editor, "failed to save clut");
	}
}

static void editor_inspector_save_tres(editor_t* editor) {
	editor_tres_state_t* state = &editor->inspector_tres;
	char buf[16384];
	int off = 0;
	if (state->header[0]) {
		if (!editor_inspector_append(buf, sizeof(buf), &off, "%s\n\n", state->header)) { return; }
	}
	else if (!editor_inspector_append(buf, sizeof(buf), &off, "[gd_resource type=\"NBPS1Material\" load_steps=%d format=3]\n\n", state->ext_count + 1)) { return; }
	for (int i = 0; i < state->ext_count; i++) {
		if (!editor_inspector_append(buf, sizeof(buf), &off, "%s\n", state->ext[i])) { return; }
	}
	if (!editor_inspector_append(buf, sizeof(buf), &off, "\n[resource]\n")) { return; }
	if (state->name[0] && !editor_inspector_append(buf, sizeof(buf), &off, "resource_name = \"%s\"\n", state->name)) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "texture = ExtResource(\"%s\")\n", state->texture)) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "palette = ExtResource(\"%s\")\n", state->palette)) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "blend_mode = \"%s\"\n", editor_inspector_blend_name(state->blend_mode))) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "shade_mode = \"%s\"\n", editor_inspector_shade_name(state->shade_mode))) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "fog_enabled = %s\n", state->fog_enabled ? "true" : "false")) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "vertex_color_tint = Color(%g, %g, %g, %g)\n", state->tint[0], state->tint[1], state->tint[2], state->tint[3])) { return; }
	if (!editor_inspector_append(buf, sizeof(buf), &off, "transparent_index = %d\n", state->transparent_index)) { return; }
	if (editor_fs_write_text(editor->inspector_path, buf, (size_t)off)) {
		editor->inspector_dirty = 0;
		editor_set_status(editor, "tres saved");
	}
	else {
		editor_set_status(editor, "failed to save tres");
	}
}

static void editor_inspector_autosave(editor_t* editor, int force) {
	if (!editor || !editor->inspector_dirty) { return; }
	if (!force && editor_now_seconds() - editor->inspector_dirty_time < 0.35) { return; }
	editor->inspector_dirty_time = editor_now_seconds();
	if (editor->inspector_kind == NB_EDITOR_INSPECTOR_CLUT) {
		editor_inspector_save_clut(editor);
	}
	else if (editor->inspector_kind == NB_EDITOR_INSPECTOR_TRES) {
		editor_inspector_save_tres(editor);
	}
	else {
		editor->inspector_dirty = 0;
	}
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
	memset(editor->hierarchy_expanded, 1, sizeof(editor->hierarchy_expanded));
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
	editor_inspector_show_node(editor);
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

static void editor_tab_store(editor_t* editor, engine_t* engine, editor_scene_tab_t* tab) {
	if (!editor || !tab) { return; }
	const char* path = editor_scene_path(editor, engine);
	snprintf(tab->path, sizeof(tab->path), "%s", path ? path : "");
	editor_fs_basename(tab->path, tab->name, sizeof(tab->name));
	for (int i = 0; i < 3; i++) { tab->camera_pos[i] = editor->camera_pos[i]; }
	tab->camera_yaw = editor->camera_yaw;
	tab->camera_pitch = editor->camera_pitch;
	tab->selected_entity = editor->selected_entity;
	tab->hierarchy_scroll_x = editor->hierarchy_scroll_x;
	tab->hierarchy_scroll_y = editor->hierarchy_scroll_y;
	memcpy(tab->hierarchy_expanded, editor->hierarchy_expanded, sizeof(tab->hierarchy_expanded));
	tab->dirty = engine && engine->scene_doc.dirty ? 1 : 0;
}

static void editor_tab_load(editor_t* editor, const editor_scene_tab_t* tab) {
	if (!editor || !tab) { return; }
	editor_hierarchy_clear_state(editor);
	for (int i = 0; i < 3; i++) { editor->camera_pos[i] = tab->camera_pos[i]; }
	editor->camera_yaw = tab->camera_yaw;
	editor->camera_pitch = tab->camera_pitch;
	editor->selected_entity = tab->selected_entity;
	editor_inspector_show_node(editor);
	editor->hierarchy_scroll_x = tab->hierarchy_scroll_x;
	editor->hierarchy_scroll_y = tab->hierarchy_scroll_y;
	memcpy(editor->hierarchy_expanded, tab->hierarchy_expanded, sizeof(editor->hierarchy_expanded));
	snprintf(editor->scene_path, sizeof(editor->scene_path), "%.*s", (int)sizeof(editor->scene_path) - 1, tab->path);
}

static void editor_tab_init(editor_scene_tab_t* tab, const char* path, const editor_t* editor) {
	if (!tab) { return; }
	memset(tab, 0, sizeof(*tab));
	memset(tab->hierarchy_expanded, 1, sizeof(tab->hierarchy_expanded));
	snprintf(tab->path, sizeof(tab->path), "%s", path ? path : "");
	editor_fs_basename(tab->path, tab->name, sizeof(tab->name));
	if (editor) {
		for (int i = 0; i < 3; i++) { tab->camera_pos[i] = editor->camera_pos[i]; }
		tab->camera_yaw = editor->camera_yaw;
		tab->camera_pitch = editor->camera_pitch;
	}
	else {
		tab->camera_pos[0] = 0.0f;
		tab->camera_pos[1] = 3.0f;
		tab->camera_pos[2] = 6.0f;
		tab->camera_yaw = -1.5707963f;
		tab->camera_pitch = -0.35f;
	}
	tab->selected_entity = NB_ENTITY_INVALID;
}

static int editor_tab_find(const editor_t* editor, const char* path) {
	if (!editor || !path) { return -1; }
	for (int i = 0; i < editor->tab_count; i++) {
		if (strcmp(editor->tabs[i].path, path) == 0) { return i; }
	}
	return -1;
}

static void editor_scene_state_reset(editor_t* editor) {
	if (!editor) { return; }
	editor_history_clear(editor);
	editor->has_pre_edit = 0;
	memset(&editor->pre_edit, 0, sizeof(editor->pre_edit));
	editor->inspector_editing = 0;
	editor->inspector_name_entity = NB_ENTITY_INVALID;
	editor->rename_entity = NB_ENTITY_INVALID;
	editor->reparent_source = NB_ENTITY_INVALID;
	editor->reparent_open = 0;
}

static void editor_open_scene_now(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !engine || !path || !*path) { return; }
	int existing = editor_tab_find(editor, path);
	int index = existing;
	if (index < 0) {
		index = editor->tab_count < NB_EDITOR_SCENE_TABS_MAX ? editor->tab_count : editor->active_tab;
	}
	if (editor->tab_count > 0 && editor->active_tab >= 0 && editor->active_tab < editor->tab_count) {
		editor_tab_store(editor, engine, &editor->tabs[editor->active_tab]);
	}
	if (existing == editor->active_tab && existing >= 0) { return; }
	nb_result_t result = editor_app_open_scene(engine, path);
	if (result != NB_RESULT_OK) {
		char msg[256];
		snprintf(msg, sizeof(msg), "open scene failed: %s", nb_error_string(result));
		editor_set_status(editor, msg);
		return;
	}
	if (index >= editor->tab_count) { editor->tab_count = index + 1; }
	editor_tab_init(&editor->tabs[index], path, editor);
	editor->active_tab = index;
	editor_scene_state_reset(editor);
	editor_tab_load(editor, &editor->tabs[index]);
	editor_fix_selection(editor, engine);
	snprintf(editor->scene_path, sizeof(editor->scene_path), "%s", path);
	editor_set_status(editor, "scene opened");
}

static void editor_switch_tab_now(editor_t* editor, engine_t* engine, int index) {
	if (!editor || !engine || index < 0 || index >= editor->tab_count || index == editor->active_tab) { return; }
	if (editor->active_tab >= 0 && editor->active_tab < editor->tab_count) {
		editor_tab_store(editor, engine, &editor->tabs[editor->active_tab]);
	}
	editor_scene_tab_t target = editor->tabs[index];
	nb_result_t result = editor_app_open_scene(engine, target.path);
	if (result != NB_RESULT_OK) {
		editor_set_status(editor, "scene switch failed");
		return;
	}
	editor->active_tab = index;
	editor->tabs[index] = target;
	editor_scene_state_reset(editor);
	editor_tab_load(editor, &editor->tabs[index]);
	editor_fix_selection(editor, engine);
	editor_set_status(editor, "scene switched");
}

static void editor_close_tab_now(editor_t* editor, engine_t* engine, int index) {
	if (!editor || !engine || index < 0 || index >= editor->tab_count) { return; }
	if (editor->tab_count <= 1) {
		editor_set_status(editor, "cannot close last scene tab");
		return;
	}
	int old_active = editor->active_tab;
	if (index == old_active) {
		editor_tab_store(editor, engine, &editor->tabs[index]);
	}
	for (int i = index; i < editor->tab_count - 1; i++) {
		editor->tabs[i] = editor->tabs[i + 1];
	}
	editor->tab_count--;
	if (index == old_active) {
		int next = index;
		if (next >= editor->tab_count) { next = editor->tab_count - 1; }
		editor_scene_tab_t target = editor->tabs[next];
		nb_result_t result = editor_app_open_scene(engine, target.path);
		if (result != NB_RESULT_OK) {
			editor_set_status(editor, "scene close failed");
			return;
		}
		editor->active_tab = next;
		editor_scene_state_reset(editor);
		editor_tab_load(editor, &editor->tabs[next]);
		editor_fix_selection(editor, engine);
	}
	else if (index < old_active) {
		editor->active_tab = old_active - 1;
	}
	editor_set_status(editor, "scene tab closed");
}

static void editor_dialog_open_discard(editor_t* editor, const char* path, int target, const char* text) {
	if (!editor) { return; }
	editor->dialog_kind = NB_EDITOR_DIALOG_DISCARD;
	editor->dialog_target = target;
	snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", path ? path : "");
	editor->dialog_dst[0] = '\0';
	snprintf(editor->dialog_title, sizeof(editor->dialog_title), "Unsaved Scene");
	snprintf(editor->dialog_text, sizeof(editor->dialog_text), "%s", text ? text : "Scene has unsaved changes.");
}

static void editor_request_open_scene(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !engine || !path || !*path) { return; }
	if (engine->scene_doc.dirty) {
		char text[256];
		snprintf(text, sizeof(text), "Save changes before opening '%s'?", path);
		editor_dialog_open_discard(editor, path, -3, text);
		return;
	}
	editor_open_scene_now(editor, engine, path);
}

static void editor_request_switch_tab(editor_t* editor, engine_t* engine, int index) {
	if (!editor || !engine || index < 0 || index >= editor->tab_count || index == editor->active_tab) { return; }
	if (engine->scene_doc.dirty) {
		char text[256];
		snprintf(text, sizeof(text), "Save changes before switching to '%s'?", editor->tabs[index].name);
		editor_dialog_open_discard(editor, editor->tabs[index].path, index, text);
		return;
	}
	editor_switch_tab_now(editor, engine, index);
}

static void editor_request_close_tab(editor_t* editor, engine_t* engine, int index) {
	if (!editor || !engine || index < 0 || index >= editor->tab_count) { return; }
	if (index == editor->active_tab && engine->scene_doc.dirty) {
		char text[256];
		snprintf(text, sizeof(text), "Save changes before closing '%s'?", editor->tabs[index].name);
		editor_dialog_open_discard(editor, editor->tabs[index].path, -2, text);
		return;
	}
	editor_close_tab_now(editor, engine, index);
}

static void editor_dialog_apply_discard(editor_t* editor, engine_t* engine, int save) {
	if (!editor || !engine) { return; }
	char path[NB_EDITOR_FS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", editor->dialog_path);
	int target = editor->dialog_target;
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	editor->dialog_target = 0;
	if (save) {
		if (!editor_save_scene(editor, engine)) { return; }
		if (editor->active_tab >= 0 && editor->active_tab < editor->tab_count) { editor->tabs[editor->active_tab].dirty = 0; }
	}
	engine->scene_doc.dirty = 0;
	if (target == -3) { editor_open_scene_now(editor, engine, path); }
	else if (target == -2) { editor_close_tab_now(editor, engine, editor->active_tab); }
	else if (target >= 0) { editor_switch_tab_now(editor, engine, target); }
}

static void editor_fs_join_into(char* out, size_t size, const char* dir, const char* name) {
	size_t half = size / 2 - 1;
	snprintf(out, size, "%.*s/%.*s", (int)half, dir ? dir : ".", (int)half, name ? name : "");
}

static void editor_explorer_refresh(editor_t* editor) {
	if (!editor) { return; }
	editor->explorer.file_count = editor_fs_list_dir(editor->explorer.current_dir, editor->explorer.files, NB_EDITOR_FS_MAX_ENTRIES, editor->explorer.show_hidden);
	editor->explorer.files_scroll_x = 0;
	editor->explorer.files_scroll_y = 0;
}

static void editor_explorer_history_reset(editor_t* editor) {
	if (!editor) { return; }
	editor->explorer.history_count = 0;
	editor->explorer.history_index = 0;
	snprintf(editor->explorer.history[0], NB_EDITOR_FS_PATH_MAX, "%.*s", NB_EDITOR_FS_PATH_MAX - 1, editor->explorer.current_dir);
	editor->explorer.history_count = 1;
}

static void editor_explorer_navigate(editor_t* editor, int delta) {
	if (!editor) { return; }
	int target = editor->explorer.history_index + delta;
	if (target < 0 || target >= editor->explorer.history_count) { return; }
	editor->explorer.history_index = target;
	snprintf(editor->explorer.current_dir, sizeof(editor->explorer.current_dir), "%s", editor->explorer.history[target]);
	editor->explorer.selected_path[0] = '\0';
	editor_explorer_refresh(editor);
}

static void editor_explorer_enter(editor_t* editor, const char* path) {
	if (!editor || !path || !*path || !editor_fs_is_dir(path)) { return; }
	if (strcmp(editor->explorer.current_dir, path) != 0) {
		if (editor->explorer.history_index + 1 < editor->explorer.history_count &&
			strcmp(editor->explorer.history[editor->explorer.history_index + 1], path) == 0) {
			editor->explorer.history_index++;
		}
		else {
			if (editor->explorer.history_index + 1 >= NB_EDITOR_EXPLORER_HISTORY_MAX) {
				memmove(&editor->explorer.history[0], &editor->explorer.history[1],
					sizeof(editor->explorer.history) - NB_EDITOR_FS_PATH_MAX);
				editor->explorer.history_count = NB_EDITOR_EXPLORER_HISTORY_MAX - 1;
				editor->explorer.history_index = editor->explorer.history_count - 1;
			}
			else {
				editor->explorer.history_count = editor->explorer.history_index + 1;
			}
			snprintf(editor->explorer.history[editor->explorer.history_count], NB_EDITOR_FS_PATH_MAX,
				"%.*s", NB_EDITOR_FS_PATH_MAX - 1, path);
			editor->explorer.history_count++;
			editor->explorer.history_index = editor->explorer.history_count - 1;
		}
	}
	snprintf(editor->explorer.current_dir, sizeof(editor->explorer.current_dir), "%.*s", (int)sizeof(editor->explorer.current_dir) - 1, path);
	editor->explorer.selected_path[0] = '\0';
	editor_explorer_refresh(editor);
}

static int editor_explorer_is_expanded(const editor_t* editor, const char* path) {
	if (!editor || !path) { return 0; }
	for (int i = 0; i < editor->explorer.expanded_count; i++) {
		if (strcmp(editor->explorer.expanded[i], path) == 0) { return 1; }
	}
	return 0;
}

static void editor_explorer_toggle_expanded(editor_t* editor, const char* path) {
	if (!editor || !path) { return; }
	for (int i = 0; i < editor->explorer.expanded_count; i++) {
		if (strcmp(editor->explorer.expanded[i], path) == 0) {
			for (int j = i; j < editor->explorer.expanded_count - 1; j++) {
				memmove(editor->explorer.expanded[j], editor->explorer.expanded[j + 1], NB_EDITOR_FS_PATH_MAX);
			}
			editor->explorer.expanded_count--;
			return;
		}
	}
	if (editor->explorer.expanded_count < NB_EDITOR_EXPLORER_TREE_MAX) {
		snprintf(editor->explorer.expanded[editor->explorer.expanded_count++], NB_EDITOR_FS_PATH_MAX, "%.*s", NB_EDITOR_FS_PATH_MAX - 1, path);
	}
}

static int editor_explorer_path_matches(const char* path, const char* prefix) {
	if (!path || !prefix) { return 0; }
	if (strcmp(path, prefix) == 0) { return 1; }
	size_t len = strlen(prefix);
	return strncmp(path, prefix, len) == 0 && path[len] == '/';
}

static void editor_explorer_after_path_change(editor_t* editor, engine_t* engine, const char* old_path, const char* new_path) {
	if (!editor || !engine || !old_path || !new_path) { return; }
	char old_prefix[NB_EDITOR_FS_PATH_MAX];
	char new_prefix[NB_EDITOR_FS_PATH_MAX];
	editor_fs_join_into(old_prefix, sizeof(old_prefix), old_path, "");
	editor_fs_join_into(new_prefix, sizeof(new_prefix), new_path, "");
	int active_changed = 0;
	for (int i = 0; i < editor->tab_count; i++) {
		editor_scene_tab_t* tab = &editor->tabs[i];
		int affected = 0;
		char updated[NB_EDITOR_FS_PATH_MAX];
		if (strcmp(tab->path, old_path) == 0) {
			snprintf(updated, sizeof(updated), "%.*s", (int)sizeof(updated) - 1, new_path);
			affected = 1;
		}
		else if (strncmp(tab->path, old_prefix, strlen(old_prefix)) == 0) {
			snprintf(updated, sizeof(updated), "%s%s", new_prefix, tab->path + strlen(old_prefix));
			affected = 1;
		}
		if (!affected) { continue; }
		snprintf(tab->path, sizeof(tab->path), "%.*s", (int)sizeof(tab->path) - 1, updated);
		editor_fs_basename(tab->path, tab->name, sizeof(tab->name));
		if (i == editor->active_tab) { active_changed = 1; }
	}
	if (active_changed && editor->active_tab >= 0 && editor->active_tab < editor->tab_count) {
		editor_scene_tab_t target = editor->tabs[editor->active_tab];
		if (editor_app_open_scene(engine, target.path) == NB_RESULT_OK) {
			editor_scene_state_reset(editor);
			editor_tab_load(editor, &target);
			editor_fix_selection(editor, engine);
			snprintf(engine->config.scene_path, sizeof(engine->config.scene_path), "%.*s", (int)sizeof(engine->config.scene_path) - 1, target.path);
		}
	}
}

static void editor_explorer_save_if_dirty(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	if (engine->scene_doc.dirty) { editor_save_scene(editor, engine); }
}

static void editor_explorer_activate(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !engine || !path || !*path) { return; }
	if (editor_fs_is_dir(path)) {
		editor_explorer_enter(editor, path);
		return;
	}
	if (strcmp(editor_fs_ext(path), "tscn") == 0) {
		editor_request_open_scene(editor, engine, path);
		return;
	}
	if (strcmp(editor_fs_ext(path), "clut") == 0 || strcmp(editor_fs_ext(path), "tres") == 0 || strcmp(editor_fs_ext(path), "wav") == 0) {
		editor_inspector_open_resource(editor, engine, path);
		return;
	}
	char msg[256];
	snprintf(msg, sizeof(msg), "selected: %s", path);
	editor_set_status(editor, msg);
}

static void editor_explorer_copy_path(const char* path) {
	if (!path || !*path) { return; }
	SDL_SetClipboardText(path);
}

static void editor_explorer_commit_rename(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	const char* name = editor->explorer.rename_buffer;
	int open = editor->explorer.rename_open;
	editor->explorer.rename_open = 0;
	if (!open || !*name) { return; }
	char src[NB_EDITOR_FS_PATH_MAX];
	char dir[NB_EDITOR_FS_PATH_MAX];
	char dst[NB_EDITOR_FS_PATH_MAX];
	snprintf(src, sizeof(src), "%s", editor->explorer.rename_path);
	editor_fs_dirname(src, dir, sizeof(dir));
	editor_fs_join_into(dst, sizeof(dst), dir, name);
	if (strcmp(src, dst) == 0) { return; }
	if (!editor_fs_is_inside(editor->explorer.root, dst)) {
		editor_set_status(editor, "rename failed: path outside project");
		return;
	}
	if (editor_fs_exists(dst)) {
		editor_set_status(editor, "rename failed: path exists");
		return;
	}
	editor_explorer_save_if_dirty(editor, engine);
	if (!editor_fs_move_path(src, dst)) {
		editor_set_status(editor, "rename failed");
		return;
	}
	editor_fs_rewrite_refs(src, dst);
	editor_explorer_after_path_change(editor, engine, src, dst);
	snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%s", dst);
	editor_explorer_refresh(editor);
	editor_set_status(editor, "renamed");
}

static void editor_explorer_duplicate(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !engine || !path || !*path) { return; }
	char dir[NB_EDITOR_FS_PATH_MAX];
	char base[NB_EDITOR_FS_NAME_MAX];
	char copy_name[NB_EDITOR_FS_NAME_MAX * 2];
	char dst[NB_EDITOR_FS_PATH_MAX];
	editor_fs_dirname(path, dir, sizeof(dir));
	editor_fs_basename(path, base, sizeof(base));
	snprintf(copy_name, sizeof(copy_name), "%s.copy", base);
	editor_fs_join_into(dst, sizeof(dst), dir, copy_name);
	if (!editor_fs_is_inside(editor->explorer.root, dst) || editor_fs_exists(dst)) {
		editor_set_status(editor, "duplicate failed: path exists or outside project");
		return;
	}
	if (!editor_fs_copy_path(path, dst)) {
		editor_set_status(editor, "duplicate failed");
		return;
	}
	editor_explorer_refresh(editor);
	editor_set_status(editor, "duplicated");
}

static void editor_explorer_perform_delete(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	char path[NB_EDITOR_FS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", editor->dialog_path);
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	if (!editor_fs_is_inside(editor->explorer.root, path)) {
		editor_set_status(editor, "delete failed: path outside project");
		return;
	}
	const char* active = editor_scene_path(editor, engine);
	if (editor_explorer_path_matches(active, path)) {
		editor_set_status(editor, "delete failed: scene is open");
		return;
	}
	editor_explorer_save_if_dirty(editor, engine);
	if (!editor_fs_delete_path(path)) {
		editor_set_status(editor, "delete failed");
		return;
	}
	editor_fs_rewrite_refs(path, "");
	if (editor_explorer_path_matches(editor->explorer.selected_path, path)) { editor->explorer.selected_path[0] = '\0'; }
	editor_explorer_refresh(editor);
	editor_set_status(editor, "deleted");
}

static void editor_explorer_perform_move(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	char src[NB_EDITOR_FS_PATH_MAX];
	char dst_dir[NB_EDITOR_FS_PATH_MAX];
	char base[NB_EDITOR_FS_NAME_MAX];
	char dst[NB_EDITOR_FS_PATH_MAX];
	snprintf(src, sizeof(src), "%s", editor->dialog_path);
	snprintf(dst_dir, sizeof(dst_dir), "%s", editor->dialog_dst);
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	editor_fs_basename(src, base, sizeof(base));
	editor_fs_join_into(dst, sizeof(dst), dst_dir, base);
	if (!editor_fs_is_inside(editor->explorer.root, src) || !editor_fs_is_inside(editor->explorer.root, dst) || strcmp(src, dst) == 0) {
		editor_set_status(editor, "move failed: invalid path");
		return;
	}
	if (!editor_fs_is_dir(dst_dir) || editor_fs_exists(dst)) {
		editor_set_status(editor, "move failed: path exists or folder missing");
		return;
	}
	const char* active = editor_scene_path(editor, engine);
	if (editor_explorer_path_matches(active, src)) {
		editor_explorer_save_if_dirty(editor, engine);
	}
	if (!editor_fs_move_path(src, dst)) {
		editor_set_status(editor, "move failed");
		return;
	}
	editor_fs_rewrite_refs(src, dst);
	editor_explorer_after_path_change(editor, engine, src, dst);
	if (editor_explorer_path_matches(editor->explorer.selected_path, src)) {
		snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%s", dst);
	}
	editor_explorer_refresh(editor);
	editor_set_status(editor, "moved");
}

static void editor_explorer_perform_import(editor_t* editor) {
	if (!editor) { return; }
	char src[NB_EDITOR_FS_PATH_MAX];
	char dir[NB_EDITOR_FS_PATH_MAX];
	char base[NB_EDITOR_FS_NAME_MAX];
	char dst[NB_EDITOR_FS_PATH_MAX];
	snprintf(src, sizeof(src), "%s", editor->dialog_path);
	snprintf(dir, sizeof(dir), "%s", editor->dialog_dst);
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	if (!editor_fs_exists(src) || !editor_fs_is_dir(dir)) {
		editor_set_status(editor, "import failed: invalid source or folder");
		return;
	}
	editor_fs_basename(src, base, sizeof(base));
	editor_fs_join_into(dst, sizeof(dst), dir, base);
	if (!editor_fs_is_inside(editor->explorer.root, dst) || editor_fs_exists(dst)) {
		editor_set_status(editor, "import failed: path exists or outside project");
		return;
	}
	if (!editor_fs_copy_file(src, dst)) {
		editor_set_status(editor, "import failed");
		return;
	}
	editor_explorer_refresh(editor);
	editor_set_status(editor, "imported");
}

static void editor_explorer_perform_new_folder(editor_t* editor) {
	if (!editor) { return; }
	char path[NB_EDITOR_FS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", editor->dialog_path);
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	if (!editor_fs_is_inside(editor->explorer.root, path) || editor_fs_exists(path)) {
		editor_set_status(editor, "new folder failed: path exists or outside project");
		return;
	}
	if (!editor_fs_make_dir(path)) {
		editor_set_status(editor, "new folder failed");
		return;
	}
	editor_explorer_enter(editor, path);
	editor_set_status(editor, "folder created");
}

static int editor_explorer_type_index(const char* path) {
	if (editor_fs_is_dir(path)) { return 1; }
	const char* ext = editor_fs_ext(path);
	if (strcmp(ext, "tscn") == 0) { return 2; }
	if (strcmp(ext, "tres") == 0) { return 3; }
	if (strcmp(ext, "obj") == 0) { return 4; }
	if (strcmp(ext, "ppm") == 0 || strcmp(ext, "clut") == 0) { return 5; }
	if (strcmp(ext, "wav") == 0) { return 6; }
	if (strcmp(ext, "wren") == 0) { return 7; }
	return 8;
}

static const char* editor_explorer_type_name(int index) {
	if (index == 1) { return "Folders"; }
	if (index == 2) { return "Scenes"; }
	if (index == 3) { return "Resources"; }
	if (index == 4) { return "Meshes"; }
	if (index == 5) { return "Textures"; }
	if (index == 6) { return "Audio"; }
	if (index == 7) { return "Scripts"; }
	if (index == 8) { return "Other"; }
	return "All";
}

static float editor_explorer_text_width(struct nk_context* ctx, const char* text, int len) {
	if (!ctx || !ctx->style.font || !text || len <= 0) { return 0.0f; }
	const struct nk_user_font* font = ctx->style.font;
	return font->width(font->userdata, font->height, text, len);
}

static void editor_explorer_draw_text(struct nk_context* ctx, struct nk_rect rect, const char* text, int align, struct nk_color color) {
	if (!ctx || !text || !*text) { return; }
	int len = (int)strlen(text);
	float width = editor_explorer_text_width(ctx, text, len);
	float x = rect.x;
	if (align == 1) { x += (rect.w - width) * 0.5f; }
	else if (align == 2) { x += rect.w - width; }
	if (x < rect.x) { x = rect.x; }
	nk_draw_text(nk_window_get_canvas(ctx), nk_rect(x, rect.y, rect.w - (x - rect.x), rect.h),
		text, len, ctx->style.font, nk_rgba(0, 0, 0, 0), color);
}

static void editor_explorer_draw_clipped_text(struct nk_context* ctx, struct nk_rect rect, const char* text, int align, struct nk_color color) {
	if (!ctx || !text) { return; }
	int len = (int)strlen(text);
	float dots = editor_explorer_text_width(ctx, "...", 3);
	if (editor_explorer_text_width(ctx, text, len) <= rect.w) {
		editor_explorer_draw_text(ctx, rect, text, align, color);
		return;
	}
	char buf[512];
	int cut = len;
	while (cut > 0 && editor_explorer_text_width(ctx, text, cut) + dots > rect.w) { cut--; }
	snprintf(buf, sizeof(buf), "%.*s...", cut, text);
	editor_explorer_draw_text(ctx, rect, buf, align, color);
}

static void editor_explorer_draw_name(struct nk_context* ctx, struct nk_rect rect, const char* name, struct nk_color color) {
	if (!ctx || !name) { return; }
	float line_h = ctx->style.font ? ctx->style.font->height : 14.0f;
	int len = (int)strlen(name);
	if (editor_explorer_text_width(ctx, name, len) <= rect.w) {
		editor_explorer_draw_text(ctx, nk_rect(rect.x, rect.y + (rect.h - line_h) * 0.5f, rect.w, line_h), name, 1, color);
		return;
	}
	int split = len / 2;
	for (int i = len / 2 - 2; i <= len / 2 + 2; i++) {
		if (i > 0 && i < len && (name[i] == '.' || name[i] == ' ' || name[i] == '_')) {
			split = i + (name[i] == '.' ? 0 : 1);
			break;
		}
	}
	while (split > 0 && editor_explorer_text_width(ctx, name, split) > rect.w) { split--; }
	if (split <= 0 || split >= len) {
		editor_explorer_draw_clipped_text(ctx, nk_rect(rect.x, rect.y + (rect.h - line_h) * 0.5f, rect.w, line_h), name, 1, color);
		return;
	}
	char first[512];
	snprintf(first, sizeof(first), "%.*s", split, name);
	editor_explorer_draw_clipped_text(ctx, nk_rect(rect.x, rect.y, rect.w, line_h), first, 1, color);
	editor_explorer_draw_clipped_text(ctx, nk_rect(rect.x, rect.y + line_h + 1.0f, rect.w, line_h), name + split, 1, color);
}

static void editor_explorer_draw_icon(struct nk_context* ctx, struct nk_rect r, int type, float line) {
	if (!ctx || r.w <= 0 || r.h <= 0) { return; }
	struct nk_command_buffer* out = nk_window_get_canvas(ctx);
	struct nk_color colors[9] = {
		nk_rgb(170, 170, 180),
		nk_rgb(230, 180, 70),
		nk_rgb(155, 115, 220),
		nk_rgb(90, 200, 190),
		nk_rgb(100, 160, 230),
		nk_rgb(120, 200, 120),
		nk_rgb(230, 140, 90),
		nk_rgb(230, 110, 130),
		nk_rgb(170, 170, 180)
	};
	struct nk_color c = colors[type >= 0 && type < 9 ? type : 8];
	struct nk_color dark = nk_rgb(30, 30, 30);
	float x = r.x;
	float y = r.y;
	float w = r.w;
	float h = r.h;
	if (line < 1.0f) { line = 1.0f; }
	if (type == 1) {
		nk_fill_rect(out, nk_rect(x, y + h * 0.30f, w, h * 0.64f), h * 0.06f, c);
		nk_fill_rect(out, nk_rect(x, y + h * 0.14f, w * 0.48f, h * 0.22f), h * 0.06f, c);
		nk_fill_rect(out, nk_rect(x + w * 0.08f, y + h * 0.40f, w * 0.84f, h * 0.06f), 0, dark);
	}
	else if (type == 2) {
		nk_fill_rect(out, nk_rect(x, y + h * 0.30f, w, h * 0.64f), h * 0.05f, c);
		nk_fill_rect(out, nk_rect(x, y + h * 0.14f, w, h * 0.22f), h * 0.05f, c);
		for (int i = 0; i < 4; i++) {
			nk_fill_rect(out, nk_rect(x + w * (0.08f + i * 0.24f), y + h * 0.18f, w * 0.10f, h * 0.12f), 0, dark);
		}
	}
	else if (type == 3) {
		nk_fill_triangle(out, x + w * 0.5f, y, x + w, y + h * 0.5f, x + w * 0.5f, y + h, c);
		nk_fill_triangle(out, x + w * 0.5f, y, x, y + h * 0.5f, x + w * 0.5f, y + h, c);
		nk_fill_triangle(out, x + w * 0.5f, y + h * 0.28f, x + w * 0.76f, y + h * 0.5f, x + w * 0.5f, y + h * 0.72f, dark);
	}
	else if (type == 4) {
		nk_stroke_rect(out, nk_rect(x + w * 0.16f, y + h * 0.22f, w * 0.68f, h * 0.62f), 0, line, c);
		nk_stroke_rect(out, nk_rect(x + w * 0.34f, y + h * 0.06f, w * 0.68f, h * 0.62f), 0, line, c);
		nk_stroke_line(out, x + w * 0.16f, y + h * 0.22f, x + w * 0.34f, y + h * 0.06f, line, c);
		nk_stroke_line(out, x + w * 0.84f, y + h * 0.22f, x + w, y + h * 0.06f, line, c);
		nk_stroke_line(out, x + w, y + h * 0.06f, x + w, y + h * 0.68f, line, c);
		nk_stroke_line(out, x + w * 0.84f, y + h * 0.84f, x + w, y + h * 0.68f, line, c);
	}
	else if (type == 5) {
		nk_stroke_rect(out, nk_rect(x + w * 0.06f, y + h * 0.12f, w * 0.88f, h * 0.76f), 0, line, c);
		nk_fill_triangle(out, x + w * 0.14f, y + h * 0.76f, x + w * 0.52f, y + h * 0.36f, x + w * 0.86f, y + h * 0.76f, c);
		nk_fill_circle(out, nk_rect(x + w * 0.62f, y + h * 0.22f, w * 0.16f, h * 0.16f), c);
	}
	else if (type == 6) {
		nk_fill_rect(out, nk_rect(x + w * 0.08f, y + h * 0.36f, w * 0.30f, h * 0.28f), 0, c);
		nk_fill_polygon(out, (const float[]){
			x + w * 0.36f, y + h * 0.36f,
			x + w * 0.36f, y + h * 0.64f,
			x + w * 0.62f, y + h * 0.82f,
			x + w * 0.62f, y + h * 0.18f
		}, 4, c);
		nk_stroke_line(out, x + w * 0.70f, y + h * 0.32f, x + w * 0.70f, y + h * 0.68f, line, c);
		nk_stroke_line(out, x + w * 0.86f, y + h * 0.20f, x + w * 0.86f, y + h * 0.80f, line, c);
	}
	else if (type == 7) {
		nk_stroke_rect(out, nk_rect(x + w * 0.14f, y + h * 0.08f, w * 0.72f, h * 0.84f), 0, line, c);
		nk_stroke_line(out, x + w * 0.32f, y + h * 0.30f, x + w * 0.68f, y + h * 0.30f, line, c);
		nk_stroke_line(out, x + w * 0.32f, y + h * 0.46f, x + w * 0.68f, y + h * 0.46f, line, c);
		nk_stroke_line(out, x + w * 0.32f, y + h * 0.62f, x + w * 0.54f, y + h * 0.62f, line, c);
	}
	else {
		nk_stroke_rect(out, nk_rect(x + w * 0.18f, y + h * 0.10f, w * 0.64f, h * 0.80f), 0, line, c);
		nk_stroke_line(out, x + w * 0.30f, y + h * 0.36f, x + w * 0.70f, y + h * 0.36f, line, c);
		nk_stroke_line(out, x + w * 0.30f, y + h * 0.52f, x + w * 0.70f, y + h * 0.52f, line, c);
		nk_stroke_line(out, x + w * 0.30f, y + h * 0.68f, x + w * 0.56f, y + h * 0.68f, line, c);
	}
}

static int editor_explorer_matches(const editor_t* editor, const editor_fs_entry_t* entry) {
	if (!editor || !entry) { return 0; }
	if (editor->explorer.filter[0] && strstr(entry->name, editor->explorer.filter) == NULL) { return 0; }
	if (editor->explorer.type_filter != 0 && editor_explorer_type_index(entry->path) != editor->explorer.type_filter) { return 0; }
	return 1;
}

static void editor_explorer_draw_tree_dir(editor_t* editor, engine_t* engine, struct nk_context* ctx, const char* path, const char* name, int depth, int tree_w, int* row_count) {
	if (!editor || !engine || !ctx || !path || depth > 8 || *row_count >= NB_EDITOR_EXPLORER_TREE_ROWS_MAX) { return; }
	int is_dir = editor_fs_is_dir(path);
	editor_fs_entry_t entries[128];
	int count = is_dir ? editor_fs_list_dir(path, entries, 128, editor->explorer.show_hidden) : 0;
	int dirs = 0;
	for (int i = 0; i < count; i++) {
		if (entries[i].kind == NB_EDITOR_FS_KIND_DIR) { dirs++; }
	}
	int has_dirs = is_dir && dirs > 0;
	int expanded = has_dirs && editor_explorer_is_expanded(editor, path);
	int indent = 8 + depth * 14;
	nk_layout_row_begin(ctx, NK_STATIC, 22.0f, 2);
	nk_layout_row_push(ctx, (float)indent);
	struct nk_rect spacer;
	nk_widget(&spacer, ctx);
	nk_layout_row_push(ctx, (float)(tree_w - indent - 6));
	struct nk_rect row;
	nk_widget(&row, ctx);
	struct nk_command_buffer* out = nk_window_get_canvas(ctx);
	int active = strcmp(editor->explorer.current_dir, path) == 0;
	int hovered = nk_input_is_mouse_hovering_rect(&ctx->input, row);
	if (active) {
		nk_fill_rect(out, nk_shrink_rect(row, 1.0f), 4.0f, nk_rgb(43, 82, 120));
	}
	else if (hovered) {
		nk_fill_rect(out, nk_shrink_rect(row, 1.0f), 4.0f, nk_rgb(48, 48, 48));
	}
	struct nk_rect toggle_rect = nk_rect(row.x + 3.0f, row.y + 4.0f, 13.0f, 14.0f);
	if (has_dirs) {
		float tx = row.x + 5.0f;
		float ty = row.y + 7.0f;
		if (expanded) {
			nk_fill_triangle(out, tx, ty, tx + 8.0f, ty, tx + 4.0f, ty + 7.0f, nk_rgb(190, 190, 190));
		}
		else {
			nk_fill_triangle(out, tx, ty, tx + 7.0f, ty + 4.0f, tx, ty + 8.0f, nk_rgb(190, 190, 190));
		}
	}
	editor_explorer_draw_icon(ctx, nk_rect(row.x + 18.0f, row.y + 3.0f, 14.0f, 14.0f), is_dir ? 1 : editor_explorer_type_index(path), 1.0f);
	editor_explorer_draw_clipped_text(ctx, nk_rect(row.x + 36.0f, row.y + 1.0f, row.w - 40.0f, 20.0f), name ? name : path, 0,
		active ? nk_rgb(240, 240, 240) : nk_rgb(200, 200, 200));
	if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, row)) {
		snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%.*s", (int)sizeof(editor->explorer.selected_path) - 1, path);
		editor->explorer.context_empty = 0;
		if (has_dirs && nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, toggle_rect)) {
			editor_explorer_toggle_expanded(editor, path);
		}
		else if (is_dir) {
			editor_explorer_enter(editor, path);
			if (has_dirs && !editor_explorer_is_expanded(editor, path)) { editor_explorer_toggle_expanded(editor, path); }
		}
		else {
			double now = editor_now_seconds();
			int double_clicked = nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_DOUBLE, row);
			int repeated_click = strcmp(editor->explorer.last_click_path, path) == 0 && now - editor->explorer.last_click_time < 0.35;
			if (double_clicked || repeated_click) {
				editor_explorer_activate(editor, engine, path);
				editor->explorer.last_click_path[0] = '\0';
			}
			else {
				editor->explorer.last_click_time = now;
				snprintf(editor->explorer.last_click_path, sizeof(editor->explorer.last_click_path), "%s", path);
			}
		}
	}
	if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, row)) {
		snprintf(editor->explorer.context_path, sizeof(editor->explorer.context_path), "%s", path);
		snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%s", path);
		editor->explorer.context_rect[0] = row.x;
		editor->explorer.context_rect[1] = row.y;
		editor->explorer.context_rect[2] = row.w;
		editor->explorer.context_rect[3] = row.h;
		editor->explorer.context_empty = 0;
	}
	if (hovered) { nk_tooltip(ctx, path); }
	if (*row_count < NB_EDITOR_EXPLORER_TREE_ROWS_MAX) {
		snprintf(editor->explorer.tree_row_paths[*row_count], NB_EDITOR_FS_PATH_MAX, "%.*s", NB_EDITOR_FS_PATH_MAX - 1, path);
		editor->explorer.tree_row_rects[*row_count][0] = row.x;
		editor->explorer.tree_row_rects[*row_count][1] = row.y;
		editor->explorer.tree_row_rects[*row_count][2] = row.w;
		editor->explorer.tree_row_rects[*row_count][3] = row.h;
		(*row_count)++;
	}
	nk_layout_row_end(ctx);
	if (!expanded) { return; }
	for (int i = 0; i < count; i++) {
		if (entries[i].kind != NB_EDITOR_FS_KIND_DIR) { continue; }
		editor_explorer_draw_tree_dir(editor, engine, ctx, entries[i].path, entries[i].name, depth + 1, tree_w, row_count);
	}
}

static void editor_explorer_handle_drag(editor_t* editor, engine_t* engine, struct nk_context* ctx) {
	if (!editor || !engine || !ctx) { return; }
	if (!editor->explorer.drag_path[0]) { return; }
	struct nk_vec2 mouse = ctx->input.mouse.pos;
	int dx = (int)mouse.x - editor->explorer.drag_start_x;
	int dy = (int)mouse.y - editor->explorer.drag_start_y;
	if (!editor->explorer.drag_active && dx * dx + dy * dy > 36) { editor->explorer.drag_active = 1; }
	if (!nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT)) { return; }
	if (editor->explorer.drag_active) {
		for (int i = 0; i < editor->explorer.tree_row_count; i++) {
			float* r = editor->explorer.tree_row_rects[i];
			struct nk_rect rect = nk_rect(r[0], r[1], r[2], r[3]);
			if (!nk_input_is_mouse_hovering_rect(&ctx->input, rect)) { continue; }
			const char* target = editor->explorer.tree_row_paths[i];
			if (editor_fs_is_dir(target)) {
				editor->dialog_kind = NB_EDITOR_DIALOG_MOVE;
				editor->dialog_target = 0;
				snprintf(editor->dialog_title, sizeof(editor->dialog_title), "Move File");
				snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", editor->explorer.drag_path);
				snprintf(editor->dialog_dst, sizeof(editor->dialog_dst), "%s", target);
				snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Move to folder:");
			}
			break;
		}
	}
	editor->explorer.drag_path[0] = '\0';
	editor->explorer.drag_active = 0;
}

static void editor_explorer_dialog_rename(editor_t* editor, const char* path) {
	if (!editor || !path || !*path) { return; }
	char base[NB_EDITOR_FS_NAME_MAX];
	editor_fs_basename(path, base, sizeof(base));
	snprintf(editor->explorer.rename_path, sizeof(editor->explorer.rename_path), "%s", path);
	snprintf(editor->explorer.rename_buffer, sizeof(editor->explorer.rename_buffer), "%s", base);
	editor->explorer.rename_open = 1;
}

static void editor_explorer_dialog_delete(editor_t* editor, const char* path) {
	if (!editor || !path || !*path) { return; }
	editor->dialog_kind = NB_EDITOR_DIALOG_DELETE;
	editor->dialog_target = 0;
	snprintf(editor->dialog_title, sizeof(editor->dialog_title), "Delete");
	snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", path);
	snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Delete this item?");
}

static void editor_explorer_dialog_new_folder(editor_t* editor, const char* dir) {
	if (!editor || !dir || !*dir) { return; }
	char child[NB_EDITOR_FS_PATH_MAX];
	editor_fs_join_into(child, sizeof(child), dir, "new_folder");
	editor->dialog_kind = NB_EDITOR_DIALOG_NEW_FOLDER;
	editor->dialog_target = 0;
	snprintf(editor->dialog_title, sizeof(editor->dialog_title), "New Folder");
	snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", child);
	snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Folder path:");
}

static void editor_explorer_dialog_new_scene(editor_t* editor, const char* dir) {
	if (!editor || !dir || !*dir) { return; }
	char child[NB_EDITOR_FS_PATH_MAX];
	editor_fs_join_into(child, sizeof(child), dir, "new_scene.tscn");
	editor->dialog_kind = NB_EDITOR_DIALOG_NEW_SCENE;
	editor->dialog_target = 0;
	snprintf(editor->dialog_title, sizeof(editor->dialog_title), "New Scene");
	snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", child);
	snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Create and open a new scene");
}

static void editor_explorer_dialog_import(editor_t* editor, const char* dir) {
	if (!editor || !dir || !*dir) { return; }
	editor->dialog_kind = NB_EDITOR_DIALOG_IMPORT;
	editor->dialog_target = 0;
	snprintf(editor->dialog_title, sizeof(editor->dialog_title), "Import File");
	editor->dialog_path[0] = '\0';
	snprintf(editor->dialog_dst, sizeof(editor->dialog_dst), "%s", dir);
	snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Source path:");
}

static void editor_run_scene(editor_t* editor, engine_t* engine);

static void editor_explorer_draw_item_context_menu(editor_t* editor, engine_t* engine) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (!editor->explorer.context_path[0]) { return; }
	char path[NB_EDITOR_FS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", editor->explorer.context_path);
	float* r = editor->explorer.context_rect;
	struct nk_rect trigger = nk_rect(r[0], r[1], r[2], r[3]);
	if (nk_contextual_begin(ctx, NK_WINDOW_BORDER | NK_WINDOW_TITLE, nk_vec2(190, 180), trigger)) {
		int action_taken = 0;
		nk_layout_row_dynamic(ctx, 22, 1);
		if (nk_contextual_item_label(ctx, "Open", NK_TEXT_LEFT)) {
			editor_explorer_activate(editor, engine, path);
			action_taken = 1;
		}
		if (strcmp(editor_fs_ext(path), "tscn") == 0 && nk_contextual_item_label(ctx, "Run Scene", NK_TEXT_LEFT)) {
			editor_run_scene(editor, engine);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Rename", NK_TEXT_LEFT)) {
			editor_explorer_dialog_rename(editor, path);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Duplicate", NK_TEXT_LEFT)) {
			editor_explorer_duplicate(editor, engine, path);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Delete", NK_TEXT_LEFT)) {
			editor_explorer_dialog_delete(editor, path);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Copy Path", NK_TEXT_LEFT)) {
			editor_explorer_copy_path(path);
			editor_set_status(editor, "path copied");
			action_taken = 1;
		}
		if (action_taken) { editor->explorer.context_path[0] = '\0'; }
		nk_contextual_end(ctx);
	}
	else {
		editor->explorer.context_path[0] = '\0';
	}
}

static void editor_explorer_draw_empty_context_menu(editor_t* editor) {
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	if (!editor->explorer.context_empty) { return; }
	float* r = editor->explorer.context_rect;
	struct nk_rect trigger = nk_rect(r[0], r[1], 2.0f, 2.0f);
	if (nk_contextual_begin(ctx, NK_WINDOW_BORDER | NK_WINDOW_TITLE, nk_vec2(190, 150), trigger)) {
		int action_taken = 0;
		nk_layout_row_dynamic(ctx, 22, 1);
		if (nk_contextual_item_label(ctx, "New Folder", NK_TEXT_LEFT)) {
			editor_explorer_dialog_new_folder(editor, editor->explorer.current_dir);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "New Scene", NK_TEXT_LEFT)) {
			editor_explorer_dialog_new_scene(editor, editor->explorer.current_dir);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Import...", NK_TEXT_LEFT)) {
			editor_explorer_dialog_import(editor, editor->explorer.current_dir);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Refresh", NK_TEXT_LEFT)) {
			editor_explorer_refresh(editor);
			action_taken = 1;
		}
		if (nk_contextual_item_label(ctx, "Show Hidden Files", NK_TEXT_LEFT)) {
			editor->explorer.show_hidden = !editor->explorer.show_hidden;
			editor_explorer_refresh(editor);
			action_taken = 1;
		}
		if (action_taken) { editor->explorer.context_empty = 0; }
		nk_contextual_end(ctx);
	}
	else {
		editor->explorer.context_empty = 0;
	}
}

static void editor_run_scene(editor_t* editor, engine_t* engine);
static void editor_open_project_from_dialog(editor_t* editor, engine_t* engine);
static void editor_create_project_from_dialog(editor_t* editor, engine_t* engine);

static void editor_explorer_item_click(editor_t* editor, engine_t* engine, struct nk_context* ctx, struct nk_rect rect, const editor_fs_entry_t* entry) {
	if (!editor || !engine || !ctx || !entry) { return; }
	snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%.*s", (int)sizeof(editor->explorer.selected_path) - 1, entry->path);
	snprintf(editor->explorer.drag_path, sizeof(editor->explorer.drag_path), "%s", entry->path);
	editor->explorer.drag_start_x = (int)ctx->input.mouse.pos.x;
	editor->explorer.drag_start_y = (int)ctx->input.mouse.pos.y;
	double now = editor_now_seconds();
	int double_clicked = nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_DOUBLE, rect);
	int repeated_click = strcmp(editor->explorer.last_click_path, entry->path) == 0 && now - editor->explorer.last_click_time < 0.35;
	if (double_clicked || repeated_click) {
		editor_explorer_activate(editor, engine, entry->path);
		editor->explorer.last_click_path[0] = '\0';
		editor->explorer.drag_path[0] = '\0';
	}
	else {
		editor->explorer.last_click_time = now;
		snprintf(editor->explorer.last_click_path, sizeof(editor->explorer.last_click_path), "%s", entry->path);
	}
}

static void editor_explorer_item_context(editor_t* editor, struct nk_context* ctx, struct nk_rect row, const editor_fs_entry_t* entry) {
	if (!editor || !ctx || !entry) { return; }
	if (!nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, row)) { return; }
	snprintf(editor->explorer.context_path, sizeof(editor->explorer.context_path), "%s", entry->path);
	editor->explorer.context_rect[0] = row.x;
	editor->explorer.context_rect[1] = row.y;
	editor->explorer.context_rect[2] = row.w;
	editor->explorer.context_rect[3] = row.h;
	snprintf(editor->explorer.selected_path, sizeof(editor->explorer.selected_path), "%s", entry->path);
}

struct editor_breadcrumb_t {
	char label[64];
	char path[NB_EDITOR_FS_PATH_MAX];
};

static void editor_draw_explorer_toolbar(struct nk_context* ctx, editor_t* editor, int w) {
	if (!ctx || !editor) { return; }
	struct editor_breadcrumb_t crumbs[8];
	int count = 0;
	char path[NB_EDITOR_FS_PATH_MAX];
	snprintf(path, sizeof(path), "%s", editor->explorer.current_dir);
	if (path[0] == '/') {
		snprintf(crumbs[0].label, sizeof(crumbs[0].label), "/");
		snprintf(crumbs[0].path, sizeof(crumbs[0].path), "/");
		count = 1;
	}
	char rest[NB_EDITOR_FS_PATH_MAX];
	snprintf(rest, sizeof(rest), "%s", path[0] == '/' ? path + 1 : path);
	char prefix[NB_EDITOR_FS_PATH_MAX];
	prefix[0] = '\0';
	char* save = NULL;
	for (char* part = strtok_r(rest, "/", &save); part && count < 8; part = strtok_r(NULL, "/", &save)) {
		if (prefix[0] && prefix[strlen(prefix) - 1] != '/') { strncat(prefix, "/", sizeof(prefix) - strlen(prefix) - 1); }
		strncat(prefix, part, sizeof(prefix) - strlen(prefix) - 1);
		if (count > 0) {
			snprintf(crumbs[count].label, sizeof(crumbs[count].label), "> %s", part);
		}
		else {
			snprintf(crumbs[count].label, sizeof(crumbs[count].label), "%s", part);
		}
		snprintf(crumbs[count].path, sizeof(crumbs[count].path), "%s", prefix);
		count++;
	}
	if (count == 0) {
		snprintf(crumbs[0].label, sizeof(crumbs[0].label), "%.*s", (int)sizeof(crumbs[0].label) - 1, path);
		snprintf(crumbs[0].path, sizeof(crumbs[0].path), "%s", path);
		count = 1;
	}
	if (count > 5) {
		struct editor_breadcrumb_t kept[5];
		snprintf(kept[0].label, sizeof(kept[0].label), "%s", crumbs[0].label);
		snprintf(kept[0].path, sizeof(kept[0].path), "%s", crumbs[0].path);
		snprintf(kept[1].label, sizeof(kept[1].label), "> ...");
		snprintf(kept[1].path, sizeof(kept[1].path), "%s", crumbs[count - 4].path);
		for (int i = 0; i < 3; i++) {
			snprintf(kept[2 + i].label, sizeof(kept[2 + i].label), "%s", crumbs[count - 3 + i].label);
			snprintf(kept[2 + i].path, sizeof(kept[2 + i].path), "%s", crumbs[count - 3 + i].path);
		}
		for (int i = 0; i < 5; i++) {
			snprintf(crumbs[i].label, sizeof(crumbs[i].label), "%s", kept[i].label);
			snprintf(crumbs[i].path, sizeof(crumbs[i].path), "%s", kept[i].path);
		}
		count = 5;
	}
	float search_w = 180.0f;
	if (w < 560) { search_w = 140.0f; }
	if (w < 460) { search_w = 100.0f; }
	float bc_avail = (float)w - 78.0f - search_w - 60.0f - 12.0f;
	if (bc_avail < 40.0f) { bc_avail = 40.0f; }
	float crumb_w[8];
	float crumb_total = 0.0f;
	for (int i = 0; i < count; i++) {
		float width = editor_explorer_text_width(ctx, crumbs[i].label, (int)strlen(crumbs[i].label)) + 12.0f;
		if (width < 26.0f) { width = 26.0f; }
		if (width > 100.0f) { width = 100.0f; }
		crumb_w[i] = width;
		crumb_total += width;
	}
	if (crumb_total > bc_avail && crumb_total > 0.0f) {
		float scale = bc_avail / crumb_total;
		for (int i = 0; i < count; i++) { crumb_w[i] *= scale; }
	}
	nk_layout_row_template_begin(ctx, 26.0f);
	nk_layout_row_template_push_static(ctx, 26.0f);
	nk_layout_row_template_push_static(ctx, 26.0f);
	nk_layout_row_template_push_static(ctx, 26.0f);
	for (int i = 0; i < count; i++) { nk_layout_row_template_push_static(ctx, crumb_w[i]); }
	nk_layout_row_template_push_variable(ctx, 20.0f);
	nk_layout_row_template_push_static(ctx, search_w);
	nk_layout_row_template_push_static(ctx, 60.0f);
	nk_layout_row_template_end(ctx);

	struct nk_rect bounds = nk_widget_bounds(ctx);
	if (nk_button_label(ctx, "<") && editor->explorer.history_index > 0) { editor_explorer_navigate(editor, -1); }
	if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) { nk_tooltip(ctx, "Back"); }
	bounds = nk_widget_bounds(ctx);
	if (nk_button_label(ctx, ">") && editor->explorer.history_index + 1 < editor->explorer.history_count) { editor_explorer_navigate(editor, 1); }
	if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) { nk_tooltip(ctx, "Forward"); }
	bounds = nk_widget_bounds(ctx);
	if (nk_button_label(ctx, "^")) {
		char parent[NB_EDITOR_FS_PATH_MAX];
		editor_fs_dirname(editor->explorer.current_dir, parent, sizeof(parent));
		editor_explorer_enter(editor, parent);
	}
	if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) { nk_tooltip(ctx, "Up"); }

	nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
	nk_style_push_style_item(ctx, &ctx->style.button.hover, nk_style_item_color(nk_rgb(45, 45, 45)));
	nk_style_push_style_item(ctx, &ctx->style.button.active, nk_style_item_color(nk_rgb(55, 55, 55)));
	nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
	nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(180, 195, 210));
	nk_style_push_color(ctx, &ctx->style.button.text_hover, nk_rgb(220, 230, 240));
	nk_style_push_color(ctx, &ctx->style.button.text_active, nk_rgb(220, 230, 240));
	for (int i = 0; i < count; i++) {
		if (nk_button_label(ctx, crumbs[i].label)) { editor_explorer_enter(editor, crumbs[i].path); }
	}
	nk_style_pop_color(ctx);
	nk_style_pop_color(ctx);
	nk_style_pop_color(ctx);
	nk_style_pop_float(ctx);
	nk_style_pop_style_item(ctx);
	nk_style_pop_style_item(ctx);
	nk_style_pop_style_item(ctx);

	nk_label(ctx, "", NK_TEXT_LEFT);
	struct nk_rect search_rect = nk_widget_bounds(ctx);
	nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, editor->explorer.filter, (int)sizeof(editor->explorer.filter), nk_filter_default);
	if (!editor->explorer.filter[0]) {
		editor_explorer_draw_text(ctx, nk_rect(search_rect.x + 6.0f, search_rect.y, search_rect.w - 12.0f, search_rect.h), "Search", 0, nk_rgb(110, 110, 110));
	}
	if (nk_button_label(ctx, editor->explorer.view_grid ? "List" : "Grid")) {
		editor->explorer.view_grid = !editor->explorer.view_grid;
	}
}

static void editor_draw_explorer(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	(void)win_w;
	if (!editor || !engine || !editor->nk_ctx) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int h = editor->explorer.height;
	if (h < 240) { h = 240; }
	if (h > win_h / 2) { h = win_h / 2; }
	int y = win_h - 10 - h;
	int x = editor->viewport_x;
	int w = editor->viewport_w;
	editor->explorer.height = h;
	if (w < 240) { return; }
	nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR;
	if (editor->dialog_kind != NB_EDITOR_DIALOG_NONE) { flags |= NK_WINDOW_ROM; }

	if (nk_begin(ctx, "Explorer", nk_rect((float)x, (float)y, (float)w, (float)h), flags)) {
		struct nk_rect content_region = nk_window_get_content_region(ctx);
		nk_fill_rect(nk_window_get_canvas(ctx), nk_rect(content_region.x, content_region.y, content_region.w, 26.0f), 0, nk_rgb(36, 36, 36));
		editor_draw_explorer_toolbar(ctx, editor, w);

		struct nk_rect clip = nk_window_get_content_region(ctx);
		int remaining = (int)(clip.y + clip.h - ctx->current->layout->at_y);
		if (remaining < 80) { remaining = 80; }
		int tree_w = editor->explorer.tree_width;
		if (tree_w <= 0) { tree_w = (int)((float)w * 0.22f); }
		if (w < 420) { tree_w = 0; }
		else {
			if (tree_w < 140) { tree_w = 140; }
			if (tree_w > w - 260) { tree_w = w - 260; }
			editor->explorer.tree_width = tree_w;
		}
		int files_w = tree_w > 0 ? w - tree_w - 8 : w;
		if (files_w < 120) { files_w = 120; }
		int body_cols = tree_w > 0 ? 2 : 1;
		float body_ratio[2] = { (float)tree_w, (float)files_w };
		nk_layout_row(ctx, NK_STATIC, (float)remaining, body_cols, body_ratio);

		if (tree_w > 0) {
			editor->explorer.tree_row_count = 0;
			if (nk_group_scrolled_offset_begin(ctx, &editor->explorer.tree_scroll_x, &editor->explorer.tree_scroll_y, "FSTree", NK_WINDOW_BORDER)) {
				struct nk_rect content = nk_window_get_content_region(ctx);
				editor->explorer.tree_rect[0] = content.x;
				editor->explorer.tree_rect[1] = content.y;
				editor->explorer.tree_rect[2] = content.w;
				editor->explorer.tree_rect[3] = content.h;
				char root_name[NB_EDITOR_FS_NAME_MAX];
				editor_fs_basename(editor->explorer.root, root_name, sizeof(root_name));
				if (!root_name[0]) { snprintf(root_name, sizeof(root_name), "%.*s", (int)sizeof(root_name) - 1, editor->explorer.root); }
				editor_explorer_draw_tree_dir(editor, engine, ctx, editor->explorer.root, root_name, 0, (int)content.w, &editor->explorer.tree_row_count);
				nk_group_scrolled_end(ctx);
			}
		}
		else {
			editor->explorer.tree_rect[0] = 0;
			editor->explorer.tree_rect[1] = 0;
			editor->explorer.tree_rect[2] = 0;
			editor->explorer.tree_rect[3] = 0;
		}

		if (nk_group_scrolled_offset_begin(ctx, &editor->explorer.files_scroll_x, &editor->explorer.files_scroll_y, "FSFiles", NK_WINDOW_BORDER)) {
			struct nk_rect content = nk_window_get_content_region(ctx);
			editor->explorer.files_rect[0] = content.x;
			editor->explorer.files_rect[1] = content.y;
			editor->explorer.files_rect[2] = content.w;
			editor->explorer.files_rect[3] = content.h;
			int pane_w = (int)content.w;
			if (pane_w < 120) { pane_w = files_w; }
			nk_fill_rect(nk_window_get_canvas(ctx), content, 0, nk_rgb(24, 24, 24));
			nk_style_push_float(ctx, &ctx->style.button.border, 0.0f);
			nk_style_push_float(ctx, &ctx->style.selectable.rounding, 4.0f);
			if (editor->explorer.rename_open) {
				nk_layout_row_static(ctx, 22.0f, (float)(pane_w - 120), 1);
				int flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->explorer.rename_buffer, (int)sizeof(editor->explorer.rename_buffer), nk_filter_default);
				if (flags & NK_EDIT_COMMITTED) { editor_explorer_commit_rename(editor, engine); }
				nk_layout_row_static(ctx, 22.0f, 55.0f, 2);
				if (nk_button_label(ctx, "OK")) { editor_explorer_commit_rename(editor, engine); }
				if (nk_button_label(ctx, "No")) { editor->explorer.rename_open = 0; }
			}
			int visible_count = 0;
			for (int i = 0; i < editor->explorer.file_count; i++) {
				if (editor_explorer_matches(editor, &editor->explorer.files[i])) { visible_count++; }
			}
			char selected_name[NB_EDITOR_FS_NAME_MAX];
			if (editor->explorer.selected_path[0]) {
				editor_fs_basename(editor->explorer.selected_path, selected_name, sizeof(selected_name));
			}
			else {
				snprintf(selected_name, sizeof(selected_name), "none");
			}
			snprintf(editor->explorer.status, sizeof(editor->explorer.status), "%d items | %d shown | %s", editor->explorer.file_count, visible_count, selected_name);
			if (visible_count == 0) {
				nk_layout_row_dynamic(ctx, 24.0f, 1);
				nk_label(ctx, "No files to show", NK_TEXT_LEFT);
			}
			else if (editor->explorer.view_grid) {
				int tile_w = editor->explorer.tile_width;
				if (tile_w < 84) { tile_w = 84; }
				if (tile_w > 96) { tile_w = 96; }
				int cols = pane_w / tile_w;
				if (cols < 1) { cols = 1; }
				float cell_w = (float)pane_w / (float)cols;
				struct nk_command_buffer* out = nk_window_get_canvas(ctx);
				int col = 0;
				for (int i = 0; i < editor->explorer.file_count; i++) {
					editor_fs_entry_t* entry = &editor->explorer.files[i];
					if (!editor_explorer_matches(editor, entry)) { continue; }
					if (col == 0) { nk_layout_row_begin(ctx, NK_STATIC, 84.0f, cols); }
					nk_layout_row_push(ctx, cell_w);
					struct nk_rect cell;
					nk_widget(&cell, ctx);
					struct nk_rect box = nk_shrink_rect(cell, 2.0f);
					int selected = strcmp(entry->path, editor->explorer.selected_path) == 0;
					int hovered = nk_input_is_mouse_hovering_rect(&ctx->input, cell);
					if (selected) {
						nk_fill_rect(out, box, 4.0f, nk_rgb(43, 82, 120));
						nk_stroke_rect(out, box, 4.0f, 1.0f, nk_rgb(90, 145, 190));
					}
					else {
						nk_fill_rect(out, box, 4.0f, hovered ? nk_rgb(44, 44, 44) : nk_rgb(32, 32, 32));
						if (hovered) { nk_stroke_rect(out, box, 4.0f, 1.0f, nk_rgb(75, 75, 75)); }
					}
					float icon_size = 48.0f;
					editor_explorer_draw_icon(ctx, nk_rect(box.x + (box.w - icon_size) * 0.5f, box.y + 4.0f, icon_size, icon_size),
						editor_explorer_type_index(entry->path), 2.0f);
					editor_explorer_draw_name(ctx, nk_rect(box.x + 2.0f, box.y + 56.0f, box.w - 4.0f, 26.0f), entry->name,
						selected ? nk_rgb(240, 240, 240) : nk_rgb(205, 205, 205));
					if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, cell)) {
						editor_explorer_item_click(editor, engine, ctx, cell, entry);
						editor->explorer.context_empty = 0;
					}
					if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, cell)) {
						editor_explorer_item_context(editor, ctx, cell, entry);
						editor->explorer.context_empty = 0;
					}
					if (hovered) { nk_tooltip(ctx, entry->path); }
					col++;
					if (col >= cols) {
						nk_layout_row_end(ctx);
						col = 0;
					}
				}
				if (col > 0) {
					for (; col < cols; col++) {
						nk_layout_row_push(ctx, cell_w);
						struct nk_rect empty;
						nk_widget(&empty, ctx);
					}
					nk_layout_row_end(ctx);
				}
			}
			else {
				int type_w = 72;
				int size_w = 64;
				int name_w = pane_w - type_w - size_w - 8;
				if (name_w < 80) {
					name_w = pane_w - 8;
					type_w = 0;
					size_w = 0;
				}
				nk_layout_row_begin(ctx, NK_STATIC, 20.0f, 3);
				nk_layout_row_push(ctx, (float)name_w);
				nk_label(ctx, "Name", NK_TEXT_LEFT);
				nk_layout_row_push(ctx, (float)type_w);
				nk_label(ctx, "Type", NK_TEXT_LEFT);
				nk_layout_row_push(ctx, (float)size_w);
				nk_label(ctx, "Size", NK_TEXT_RIGHT);
				nk_layout_row_end(ctx);
				for (int i = 0; i < editor->explorer.file_count; i++) {
					editor_fs_entry_t* entry = &editor->explorer.files[i];
					if (!editor_explorer_matches(editor, entry)) { continue; }
					nk_layout_row_begin(ctx, NK_STATIC, 22.0f, 3);
					nk_layout_row_push(ctx, (float)name_w);
					struct nk_rect row;
					nk_widget(&row, ctx);
					struct nk_command_buffer* out = nk_window_get_canvas(ctx);
					int selected = strcmp(entry->path, editor->explorer.selected_path) == 0;
					int hovered = nk_input_is_mouse_hovering_rect(&ctx->input, row);
					if (selected) {
						nk_fill_rect(out, nk_shrink_rect(row, 1.0f), 4.0f, nk_rgb(43, 82, 120));
					}
					else if (hovered) {
						nk_fill_rect(out, nk_shrink_rect(row, 1.0f), 4.0f, nk_rgb(44, 44, 44));
					}
					editor_explorer_draw_icon(ctx, nk_rect(row.x + 2.0f, row.y + 3.0f, 16.0f, 16.0f),
						editor_explorer_type_index(entry->path), 1.0f);
					editor_explorer_draw_clipped_text(ctx, nk_rect(row.x + 22.0f, row.y + 2.0f, row.w - 26.0f, 18.0f), entry->name, 0,
						selected ? nk_rgb(240, 240, 240) : nk_rgb(205, 205, 205));
					if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, row)) {
						editor_explorer_item_click(editor, engine, ctx, row, entry);
						editor->explorer.context_empty = 0;
					}
					if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, row)) {
						editor_explorer_item_context(editor, ctx, row, entry);
						editor->explorer.context_empty = 0;
					}
					if (hovered) { nk_tooltip(ctx, entry->path); }
					nk_layout_row_push(ctx, (float)type_w);
					struct nk_rect type_rect;
					nk_widget(&type_rect, ctx);
					editor_explorer_draw_clipped_text(ctx, type_rect, editor_explorer_type_name(editor_explorer_type_index(entry->path)), 0, nk_rgb(130, 130, 130));
					nk_layout_row_push(ctx, (float)size_w);
					struct nk_rect size_rect;
					nk_widget(&size_rect, ctx);
					char size_text[32];
					if (entry->kind == NB_EDITOR_FS_KIND_DIR) {
						snprintf(size_text, sizeof(size_text), "<DIR>");
					}
					else {
						snprintf(size_text, sizeof(size_text), "%ld", entry->size);
					}
					editor_explorer_draw_clipped_text(ctx, size_rect, size_text, 2, nk_rgb(130, 130, 130));
					nk_layout_row_end(ctx);
				}
			}
			if (!editor->explorer.context_path[0] && !editor->explorer.rename_open &&
				nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_RIGHT, content)) {
				editor->explorer.context_empty = 1;
				editor->explorer.context_rect[0] = ctx->input.mouse.pos.x;
				editor->explorer.context_rect[1] = ctx->input.mouse.pos.y;
				editor->explorer.context_rect[2] = 2.0f;
				editor->explorer.context_rect[3] = 2.0f;
			}
			nk_style_pop_float(ctx);
			nk_style_pop_float(ctx);
			nk_group_scrolled_end(ctx);
		}
		nk_layout_row_dynamic(ctx, 20.0f, 1);
		nk_label(ctx, editor->explorer.status, NK_TEXT_LEFT);
		editor_explorer_draw_item_context_menu(editor, engine);
		editor_explorer_draw_empty_context_menu(editor);
	}
	nk_end(ctx);
	editor_explorer_handle_drag(editor, engine, ctx);
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
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0) {
		editor_set_status(editor, "game launch failed: no executable path");
		return;
	}
	exe[len] = '\0';
	char scene_arg[512];
	snprintf(scene_arg, sizeof(scene_arg), "--scene=%s", scene);
	char* argv[] = { exe, (char*)"--mode=game", scene_arg, NULL };
	pid_t pid = -1;
	int rc = posix_spawn(&pid, exe, NULL, NULL, argv, environ);
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
	editor->engine = engine;
	editor_hierarchy_clear_state(editor);
	editor->enabled = engine->config.editor_enabled ? 1 : 0;
	editor->selected_entity = NB_ENTITY_INVALID;
	editor->inspector_color_index = -1;
	editor->inspector_wav_voice = -1;
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
	editor_tab_init(&editor->tabs[0], editor->scene_path, editor);
	editor->tab_count = 1;
	editor->active_tab = 0;
	editor->explorer.height = 320;
	editor->explorer.tree_width = editor->viewport_w * 22 / 100;
	if (editor->explorer.tree_width < 140) { editor->explorer.tree_width = 140; }
	editor->explorer.tile_width = 92;
	editor->explorer.view_grid = 1;
	snprintf(editor->explorer.root, sizeof(editor->explorer.root), "%s", engine->config.project_root[0] ? engine->config.project_root : ".");
	snprintf(editor->explorer.current_dir, sizeof(editor->explorer.current_dir), "%s", editor->explorer.root);
	editor_explorer_history_reset(editor);
	editor_explorer_refresh(editor);
	if (!editor_explorer_is_expanded(editor, editor->explorer.root)) { editor_explorer_toggle_expanded(editor, editor->explorer.root); }
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
	struct nk_font_config base_cfg = nk_font_config(14.0f);
	base_cfg.oversample_h = 2;
	base_cfg.oversample_v = 2;
	struct nk_font* base_font = nk_font_atlas_add_default(atlas, 14.0f, &base_cfg);
	if (base_font) { atlas->default_font = base_font; }
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
	editor_inspector_autosave(editor, 1);

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
	int resize_y = editor->win_h - 10 - editor->explorer.height;
	int split_x = (int)(editor->explorer.tree_rect[0] + editor->explorer.tree_rect[2]);
	int split_y = (int)editor->explorer.tree_rect[1];
	int split_h = (int)editor->explorer.tree_rect[3];
	if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
		if (e->button.x >= editor->viewport_x && e->button.x <= editor->viewport_x + editor->viewport_w && abs(e->button.y - resize_y) <= 5) {
			editor->explorer.resizing = 1;
			return 1;
		}
		if (split_h > 0 && abs(e->button.x - split_x) <= 4 && e->button.y >= split_y && e->button.y <= split_y + split_h) {
			editor->explorer.tree_resizing = 1;
			return 1;
		}
	}
	if (e->type == SDL_MOUSEMOTION && editor->explorer.resizing) {
		int h = editor->win_h - 10 - e->motion.y;
		if (h < 240) { h = 240; }
		if (h > editor->win_h / 2) { h = editor->win_h / 2; }
		editor->explorer.height = h;
		return 1;
	}
	if (e->type == SDL_MOUSEMOTION && editor->explorer.tree_resizing) {
		int tw = e->motion.x - editor->viewport_x;
		if (tw < 140) { tw = 140; }
		if (tw > editor->viewport_w - 260) { tw = editor->viewport_w - 260; }
		if (tw > 0) { editor->explorer.tree_width = tw; }
		return 1;
	}
	if (e->type == SDL_MOUSEBUTTONUP && editor->explorer.resizing) {
		editor->explorer.resizing = 0;
		return 1;
	}
	if (e->type == SDL_MOUSEBUTTONUP && editor->explorer.tree_resizing) {
		editor->explorer.tree_resizing = 0;
		return 1;
	}
	if (e->type == SDL_KEYDOWN) {
		SDL_Keymod mods = SDL_GetModState();
		int ctrl = (mods & KMOD_CTRL) != 0;
		if (ctrl && e->key.keysym.sym == SDLK_s) { editor->request_save = 1; return 1; }
		if (ctrl && e->key.keysym.sym == SDLK_z) { editor->request_undo = 1; return 1; }
		if (ctrl && e->key.keysym.sym == SDLK_y) { editor->request_redo = 1; return 1; }
		if (editor->explorer.selected_path[0] && editor->dialog_kind == NB_EDITOR_DIALOG_NONE && !editor->inspector_editing) {
			if (ctrl && e->key.keysym.sym == SDLK_d) {
				editor_explorer_duplicate(editor, editor->engine, editor->explorer.selected_path);
				return 1;
			}
			if (e->key.keysym.sym == SDLK_F2) {
				editor_explorer_dialog_rename(editor, editor->explorer.selected_path);
				return 1;
			}
			if (e->key.keysym.sym == SDLK_DELETE) {
				editor_explorer_dialog_delete(editor, editor->explorer.selected_path);
				return 1;
			}
		}
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
				editor_inspector_show_node(editor);
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
	editor_inspector_show_node(editor);
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
	editor_inspector_show_node(editor);
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
	editor_inspector_show_node(editor);
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
		if (selected != previous) {
			editor->selected_entity = node->id;
			editor_inspector_show_node(editor);
		}
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
		if (nk_selectable_label(ctx, label, NK_TEXT_LEFT, &selected)) {
			editor->selected_entity = node->id;
			editor_inspector_show_node(editor);
		}
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
			editor_inspector_show_node(editor);
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
				editor_inspector_show_node(editor);
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
					if (index > 0) {
						editor->selected_entity = rows.rows[index - 1].id;
						editor_inspector_show_node(editor);
					}
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
					if (index + 1 < rows.count) {
						editor->selected_entity = rows.rows[index + 1].id;
						editor_inspector_show_node(editor);
					}
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT)) {
					if (current && current->child_count > 0) {
						unsigned char* state = editor_hierarchy_state_ptr(editor, current->id);
						if (state && *state) { *state = 0; }
						else if (current->parent != NB_ENTITY_INVALID) {
							editor->selected_entity = current->parent;
							editor_inspector_show_node(editor);
						}
					}
					else if (current && current->parent != NB_ENTITY_INVALID) {
						editor->selected_entity = current->parent;
						editor_inspector_show_node(editor);
					}
				}
				else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT)) {
					if (current && current->child_count > 0) {
						unsigned char* state = editor_hierarchy_state_ptr(editor, current->id);
						if (state && !*state) { *state = 1; }
						else {
							node_t* child = scene_get_node(engine->scene_graph, current->children[0]);
							if (child) {
								editor->selected_entity = child->id;
								editor_inspector_show_node(editor);
							}
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

static int editor_inspector_labeled_edit(struct nk_context* ctx, const char* label, char* buf, int cap) {
	float widths[2];
	float avail = ctx->current->layout->bounds.w;
	if (avail < 140.0f) { avail = 140.0f; }
	widths[0] = 82.0f;
	widths[1] = avail - 82.0f;
	nk_layout_row(ctx, NK_STATIC, 22, 2, widths);
	nk_label(ctx, label, NK_TEXT_LEFT);
	nk_flags flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, buf, cap, nk_filter_default);
	return (flags & (NK_EDIT_COMMITTED | NK_EDIT_DEACTIVATED)) ? 1 : 0;
}

static void editor_inspector_draw_color_picker(editor_t* editor, struct nk_context* ctx) {
	struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
	nk_layout_row_dynamic(ctx, 112.0f, 1);
	struct nk_rect square;
	nk_widget(&square, ctx);
	float side = square.w < square.h ? square.w : square.h;
	if (side < 24.0f) { return; }
	square.x += (square.w - side) * 0.5f;
	square.y += (square.h - side) * 0.5f;
	square.w = side;
	square.h = side;
	if (nk_input_is_mouse_hovering_rect(&ctx->input, square) && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
		float mx = ((float)ctx->input.mouse.pos.x - square.x) / square.w;
		float my = ((float)ctx->input.mouse.pos.y - square.y) / square.h;
		editor->inspector_color_s = mx < 0.0f ? 0.0f : mx > 1.0f ? 1.0f : mx;
		editor->inspector_color_v = 1.0f - (my < 0.0f ? 0.0f : my > 1.0f ? 1.0f : my);
		editor_inspector_apply_picker_color(editor);
	}
	int cols = 28;
	int rows = 22;
	float cell_w = square.w / (float)cols;
	float cell_h = square.h / (float)rows;
	for (int y = 0; y < rows; y++) {
		float v = 1.0f - (float)y / (float)(rows - 1);
		for (int x = 0; x < cols; x++) {
			float s = (float)x / (float)(cols - 1);
			struct nk_rect cell = nk_rect(square.x + (float)x * cell_w, square.y + (float)y * cell_h, cell_w + 0.5f, cell_h + 0.5f);
			nk_fill_rect(canvas, cell, 0, editor_inspector_hsv_color(editor->inspector_color_h, s, v));
		}
	}
	nk_stroke_rect(canvas, square, 1.0f, 0, nk_rgb(80, 80, 80));
	float px = square.x + editor->inspector_color_s * square.w;
	float py = square.y + (1.0f - editor->inspector_color_v) * square.h;
	struct nk_rect marker = nk_rect(px - 5.0f, py - 5.0f, 10.0f, 10.0f);
	nk_fill_circle(canvas, marker, nk_rgb(255, 255, 255));
	nk_stroke_circle(canvas, marker, 1.0f, nk_rgb(20, 20, 20));
	nk_layout_row_dynamic(ctx, 16, 1);
	nk_labelf(ctx, NK_TEXT_LEFT, "Hue %d°", (int)editor->inspector_color_h);
	nk_layout_row_dynamic(ctx, 18, 1);
	struct nk_rect hue;
	nk_widget(&hue, ctx);
	if (nk_input_is_mouse_hovering_rect(&ctx->input, hue) && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
		float t = ((float)ctx->input.mouse.pos.x - hue.x) / hue.w;
		editor->inspector_color_h = (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t) * 360.0f;
		editor_inspector_apply_picker_color(editor);
	}
	float hue_w = hue.w / 180.0f;
	for (int i = 0; i < 180; i++) {
		struct nk_rect part = nk_rect(hue.x + (float)i * hue_w, hue.y, hue_w + 0.5f, hue.h);
		nk_fill_rect(canvas, part, 0, editor_inspector_hsv_color((float)i * 2.0f, 1.0f, 1.0f));
	}
	float hx = hue.x + editor->inspector_color_h / 360.0f * hue.w;
	nk_stroke_rect(canvas, nk_rect(hx - 3.0f, hue.y - 2.0f, 6.0f, hue.h + 4.0f), 2.0f, 0, nk_rgb(255, 255, 255));
	if (editor->inspector_color_target == NB_EDITOR_COLOR_TARGET_TRES) {
		nk_layout_row_dynamic(ctx, 16, 1);
		nk_labelf(ctx, NK_TEXT_LEFT, "Alpha %d%%", (int)(editor->inspector_color_a * 100.0f));
		nk_layout_row_dynamic(ctx, 18, 1);
		struct nk_rect alpha;
		nk_widget(&alpha, ctx);
		if (nk_input_is_mouse_hovering_rect(&ctx->input, alpha) && nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
			float t = ((float)ctx->input.mouse.pos.x - alpha.x) / alpha.w;
			editor->inspector_color_a = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
			editor_inspector_apply_picker_color(editor);
		}
		struct nk_color base = editor_inspector_hsv_color(editor->inspector_color_h, editor->inspector_color_s, editor->inspector_color_v);
		float alpha_w = alpha.w / 64.0f;
		for (int i = 0; i < 64; i++) {
			float t = (float)i / 63.0f;
			struct nk_color part_color = nk_rgb((int)(32.0f + ((float)base.r - 32.0f) * t), (int)(32.0f + ((float)base.g - 32.0f) * t), (int)(32.0f + ((float)base.b - 32.0f) * t));
			nk_fill_rect(canvas, nk_rect(alpha.x + (float)i * alpha_w, alpha.y, alpha_w + 0.5f, alpha.h), 0, part_color);
		}
		float ax = alpha.x + editor->inspector_color_a * alpha.w;
		nk_stroke_rect(canvas, nk_rect(ax - 3.0f, alpha.y - 2.0f, 6.0f, alpha.h + 4.0f), 2.0f, 0, nk_rgb(255, 255, 255));
	}
	nk_layout_row_dynamic(ctx, 16, 1);
	nk_label(ctx, "HEX", NK_TEXT_LEFT);
	float widths[2];
	float avail = ctx->current->layout->bounds.w;
	if (avail < 140.0f) { avail = 140.0f; }
	widths[0] = 56.0f;
	widths[1] = avail - 56.0f;
	nk_layout_row(ctx, NK_STATIC, 22, 2, widths);
	if (nk_button_label(ctx, "Copy")) {
		SDL_SetClipboardText(editor->inspector_color_hex);
		editor_set_status(editor, "hex copied");
	}
	nk_flags flags = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, editor->inspector_color_hex, (int)sizeof(editor->inspector_color_hex), nk_filter_default);
	if (flags & (NK_EDIT_COMMITTED | NK_EDIT_DEACTIVATED)) {
		if (!editor_inspector_parse_hex(editor, editor->inspector_color_hex)) {
			struct nk_color color = editor_inspector_hsv_color(editor->inspector_color_h, editor->inspector_color_s, editor->inspector_color_v);
			editor_inspector_update_hex(editor, color);
		}
	}
}

static void editor_inspector_draw_clut(editor_t* editor, struct nk_context* ctx, int panel_h) {
	editor_clut_state_t* state = &editor->inspector_clut;
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_labelf(ctx, NK_TEXT_LEFT, "clut: %s%s", editor->inspector_path, editor->inspector_dirty ? " *" : "");
	int count = state->count;
	if (nk_property_int(ctx, "Colors", 1, &count, NB_EDITOR_INSPECTOR_CLUT_MAX, 1, 1)) {
		for (int i = state->count; i < count; i++) {
			state->r[i] = 0;
			state->g[i] = 0;
			state->b[i] = 0;
		}
		state->count = count;
		editor_inspector_mark_dirty(editor);
	}
	if (editor->inspector_color_index >= count) { editor->inspector_color_index = -1; }
	float picker_h = 220.0f;
	float height = (float)panel_h - 96.0f - picker_h;
	if (height < 70.0f) { height = 70.0f; }
	nk_layout_row_dynamic(ctx, height, 1);
	if (nk_group_scrolled_offset_begin(ctx, &editor->inspector_scroll_x, &editor->inspector_scroll_y, "clut_colors", NK_WINDOW_BORDER)) {
		for (int i = 0; i < state->count; i++) {
			float widths[3];
			float avail = ctx->current->layout->bounds.w;
			if (avail < 180.0f) { avail = 180.0f; }
			widths[0] = 32.0f;
			widths[1] = 44.0f;
			widths[2] = avail - 76.0f;
			nk_layout_row(ctx, NK_STATIC, 22, 3, widths);
			nk_labelf(ctx, NK_TEXT_LEFT, "%d", i);
			struct nk_rect swatch;
			nk_widget(&swatch, ctx);
			struct nk_color color = nk_rgb((int)state->r[i], (int)state->g[i], (int)state->b[i]);
			nk_fill_rect(nk_window_get_canvas(ctx), swatch, 0, color);
			nk_stroke_rect(nk_window_get_canvas(ctx), swatch, 1, 0, i == editor->inspector_color_index ? nk_rgb(220, 220, 220) : nk_rgb(90, 90, 90));
			nk_labelf(ctx, NK_TEXT_LEFT, "#%02X%02X%02X", (int)state->r[i], (int)state->g[i], (int)state->b[i]);
			if (nk_input_is_mouse_click_in_rect(&ctx->input, NK_BUTTON_LEFT, swatch)) {
				editor_inspector_select_clut_color(editor, i);
			}
		}
		nk_group_scrolled_end(ctx);
	}
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_label(ctx, "Color", NK_TEXT_LEFT);
	if (editor->inspector_color_index < 0 || editor->inspector_color_index >= state->count) {
		nk_layout_row_dynamic(ctx, 18, 1);
		nk_label(ctx, "Select a color from the list", NK_TEXT_LEFT);
		return;
	}
	int index = editor->inspector_color_index;
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_labelf(ctx, NK_TEXT_LEFT, "color %d: #%02X%02X%02X", index, (int)state->r[index], (int)state->g[index], (int)state->b[index]);
	editor_inspector_draw_color_picker(editor, ctx);
}

static void editor_inspector_draw_tres(editor_t* editor, struct nk_context* ctx, int panel_h) {
	editor_tres_state_t* state = &editor->inspector_tres;
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_labelf(ctx, NK_TEXT_LEFT, "tres: %s%s", editor->inspector_path, editor->inspector_dirty ? " *" : "");
	editor->inspector_color_target = NB_EDITOR_COLOR_TARGET_TRES;
	float height = (float)panel_h - 320.0f;
	if (height < 100.0f) { height = 100.0f; }
	nk_layout_row_dynamic(ctx, height, 1);
	if (nk_group_scrolled_offset_begin(ctx, &editor->inspector_scroll_x, &editor->inspector_scroll_y, "tres_fields", NK_WINDOW_BORDER)) {
		nk_layout_row_dynamic(ctx, 20, 1);
		nk_label(ctx, "Resource", NK_TEXT_LEFT);
		if (editor_inspector_labeled_edit(ctx, "Name", state->name, (int)sizeof(state->name))) { editor_inspector_mark_dirty(editor); }
		if (editor_inspector_labeled_edit(ctx, "Texture", state->texture, (int)sizeof(state->texture))) { editor_inspector_mark_dirty(editor); }
		if (editor_inspector_labeled_edit(ctx, "Palette", state->palette, (int)sizeof(state->palette))) { editor_inspector_mark_dirty(editor); }
		static const char* const blend_items[] = { "none", "alpha_50", "add", "subtract", "add_quarter" };
		static const char* const shade_items[] = { "flat", "gouraud" };
		float widths[2];
		float avail = ctx->current->layout->bounds.w;
		if (avail < 140.0f) { avail = 140.0f; }
		widths[0] = 82.0f;
		widths[1] = avail - 82.0f;
		nk_layout_row(ctx, NK_STATIC, 22, 2, widths);
		nk_label(ctx, "Blend", NK_TEXT_LEFT);
		int blend = nk_combo(ctx, blend_items, 5, state->blend_mode, 18, nk_vec2(widths[1], 220));
		if (blend != state->blend_mode) { state->blend_mode = blend; editor_inspector_mark_dirty(editor); }
		nk_layout_row(ctx, NK_STATIC, 22, 2, widths);
		nk_label(ctx, "Shade", NK_TEXT_LEFT);
		int shade = nk_combo(ctx, shade_items, 2, state->shade_mode, 18, nk_vec2(widths[1], 120));
		if (shade != state->shade_mode) { state->shade_mode = shade; editor_inspector_mark_dirty(editor); }
		nk_layout_row_dynamic(ctx, 22, 1);
		if (nk_checkbox_label(ctx, "Fog", &state->fog_enabled)) { editor_inspector_mark_dirty(editor); }
		if (nk_property_int(ctx, "Transparent", 0, &state->transparent_index, 65535, 1, 1)) { editor_inspector_mark_dirty(editor); }
		nk_labelf(ctx, NK_TEXT_LEFT, "ext resources: %d", state->ext_count);
		nk_group_scrolled_end(ctx);
	}
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_label(ctx, "Vertex Color Tint", NK_TEXT_LEFT);
	editor_inspector_draw_color_picker(editor, ctx);
}

static void editor_inspector_draw_wav(editor_t* editor, engine_t* engine, struct nk_context* ctx, int panel_h) {
	(void)panel_h;
	editor_wav_state_t* state = &editor->inspector_wav;
	double duration = state->sample_rate > 0 ? (double)state->frame_count / (double)state->sample_rate : 0.0;
	double now = editor_now_seconds();
	int playing = editor->inspector_wav_voice >= 0 && now < editor->inspector_wav_end;
	if (editor->inspector_wav_voice >= 0 && now >= editor->inspector_wav_end) {
		audio_stop_voice(engine && engine->audio.ready ? &engine->audio : NULL, editor->inspector_wav_voice);
		editor->inspector_wav_voice = -1;
		playing = 0;
	}
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_labelf(ctx, NK_TEXT_LEFT, "wav: %s", editor->inspector_path);
	if (nk_button_label(ctx, playing ? "Stop" : "Play")) {
		if (playing) {
			audio_stop_voice(engine && engine->audio.ready ? &engine->audio : NULL, editor->inspector_wav_voice);
			editor->inspector_wav_voice = -1;
			editor_set_status(editor, "wav playback stopped");
		}
		else {
			audio_stream_t stream;
			memset(&stream, 0, sizeof(stream));
			if (!wav_load(editor->inspector_path, &stream)) {
				editor_set_status(editor, "failed to play wav");
			}
			else {
				int slot = audio_play_stream(engine && engine->audio.ready ? &engine->audio : NULL, &stream, 1.0f, 0);
				audio_stream_free(&stream);
				if (slot < 0) {
					editor_set_status(editor, "audio unavailable");
				}
				else {
					editor->inspector_wav_voice = slot;
					editor->inspector_wav_end = now + duration;
					editor_set_status(editor, "wav playing");
				}
			}
		}
	}
	nk_layout_row_dynamic(ctx, 18, 1);
	nk_label(ctx, "Read-only", NK_TEXT_LEFT);
	nk_labelf(ctx, NK_TEXT_LEFT, "sample rate: %d Hz", state->sample_rate);
	nk_labelf(ctx, NK_TEXT_LEFT, "channels: %d", state->channels);
	nk_labelf(ctx, NK_TEXT_LEFT, "frames: %d", state->frame_count);
	nk_labelf(ctx, NK_TEXT_LEFT, "duration: %.3f s", duration);
	nk_layout_row_dynamic(ctx, 64.0f, 1);
	struct nk_rect bounds;
	nk_widget(&bounds, ctx);
	nk_fill_rect(nk_window_get_canvas(ctx), bounds, 0, nk_rgb(20, 20, 20));
	nk_stroke_rect(nk_window_get_canvas(ctx), bounds, 1, 0, nk_rgb(70, 70, 70));
	float bar_w = bounds.w / (float)NB_EDITOR_INSPECTOR_WAV_PEAKS;
	for (int i = 0; i < NB_EDITOR_INSPECTOR_WAV_PEAKS; i++) {
		float bar_h = state->peaks[i] * (bounds.h - 4.0f);
		if (bar_h < 1.0f) { bar_h = 1.0f; }
		struct nk_rect bar = nk_rect(bounds.x + (float)i * bar_w, bounds.y + bounds.h - 2.0f - bar_h, bar_w - 1.0f, bar_h);
		nk_fill_rect(nk_window_get_canvas(ctx), bar, 0, nk_rgb(80, 150, 200));
	}
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
	if (editor->inspector_kind != NB_EDITOR_INSPECTOR_WAV && editor->inspector_wav_voice >= 0) {
		audio_stop_voice(engine && engine->audio.ready ? &engine->audio : NULL, editor->inspector_wav_voice);
		editor->inspector_wav_voice = -1;
	}
	node_t* node = NULL;
	if (nk_begin(ctx, "Inspector", nk_rect((float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h), NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
		if (editor->inspector_kind == NB_EDITOR_INSPECTOR_CLUT) {
			editor_inspector_draw_clut(editor, ctx, panel_h);
			goto inspector_done;
		}
		else if (editor->inspector_kind == NB_EDITOR_INSPECTOR_TRES) {
			editor_inspector_draw_tres(editor, ctx, panel_h);
			goto inspector_done;
		}
		else if (editor->inspector_kind == NB_EDITOR_INSPECTOR_WAV) {
			editor_inspector_draw_wav(editor, engine, ctx, panel_h);
			goto inspector_done;
		}
		if (!engine->scene_graph || editor->selected_entity == NB_ENTITY_INVALID) {
			nk_layout_row_dynamic(ctx, 18, 1);
			nk_label(ctx, "No node selected", NK_TEXT_LEFT);
			goto inspector_done;
		}
		node = scene_get_node(engine->scene_graph, editor->selected_entity);
		if (!node) {
			nk_layout_row_dynamic(ctx, 18, 1);
			nk_label(ctx, "Selected node missing", NK_TEXT_LEFT);
			goto inspector_done;
		}
		nk_layout_row_dynamic(ctx, 18, 1);
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
	inspector_done:
		if (nk_item_is_any_active(ctx)) { editor->inspector_editing = 1; }
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
				editor_inspector_show_node(editor);
				editor_set_status(editor, "node selected");
			}
			return;
		}

		int axis = editor_pick_gizmo_axis(editor, engine, mx, my);
		if (axis < 0) {
			if (editor_pick_node(editor, engine, mx, my, &picked)) {
				editor->selected_entity = picked;
				editor_inspector_show_node(editor);
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

static void editor_draw_scene_tabs(editor_t* editor, engine_t* engine, int win_w) {
	if (!editor || !editor->nk_ctx) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	int x = 360;
	int w = win_w - 720;
	if (w < 160) { w = 160; }
	int show_add = editor->tab_count < NB_EDITOR_SCENE_TABS_MAX;
	int columns = editor->tab_count * 2 + (show_add ? 1 : 0);
	if (columns <= 0) { return; }
	if (nk_begin(ctx, "SceneTabs", nk_rect((float)x, 10.0f, (float)w, 28.0f), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_begin(ctx, NK_STATIC, 24.0f, columns);
		for (int i = 0; i < editor->tab_count; i++) {
			editor_scene_tab_t* tab = &editor->tabs[i];
			nk_layout_row_push(ctx, 120.0f);
			struct nk_style_button style = ctx->style.button;
			if (i == editor->active_tab) {
				style.normal = style.active;
				style.hover = style.active;
			}
			char label[192];
			snprintf(label, sizeof(label), "%s%s", tab->name, tab->dirty ? "*" : "");
			if (nk_button_label_styled(ctx, &style, label)) {
				editor_request_switch_tab(editor, engine, i);
			}
			nk_layout_row_push(ctx, 26.0f);
			if (nk_button_label(ctx, "x")) {
				editor_request_close_tab(editor, engine, i);
			}
		}
		if (show_add) {
			nk_layout_row_push(ctx, 26.0f);
			if (nk_button_label(ctx, "+")) {
				editor->dialog_kind = NB_EDITOR_DIALOG_NEW_SCENE;
				editor->dialog_target = 0;
				snprintf(editor->dialog_title, sizeof(editor->dialog_title), "New Scene");
				snprintf(editor->dialog_path, sizeof(editor->dialog_path), "%s", "examples/kula/scenes/new_scene.tscn");
				snprintf(editor->dialog_text, sizeof(editor->dialog_text), "Create and open a new scene");
			}
		}
		nk_layout_row_end(ctx);
	}
	nk_end(ctx);
}

static void editor_create_scene_from_dialog(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	const char* path = editor->dialog_path;
	if (!editor_fs_is_inside(editor->explorer.root, path)) {
		editor_set_status(editor, "new scene failed: path outside project");
		return;
	}
	if (editor_fs_exists(path)) {
		editor_set_status(editor, "new scene failed: path exists");
		return;
	}
	const char* content = "[gd_scene load_steps=1 format=2]\n\n[node name=\"Root\" type=\"Node\"]\n";
	if (!editor_fs_write_text(path, content, strlen(content))) {
		editor_set_status(editor, "new scene failed: write error");
		return;
	}
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	editor_explorer_refresh(editor);
	editor_request_open_scene(editor, engine, path);
}

static int editor_project_valid(const char* path) {
	if (!path || !*path || !editor_fs_is_dir(path)) { return 0; }
	char marker[NB_EDITOR_FS_PATH_MAX];
	editor_fs_join_into(marker, sizeof(marker), path, "project.ini");
	return access(marker, F_OK) == 0;
}

static void editor_open_project_now(editor_t* editor, engine_t* engine, const char* path) {
	if (!editor || !engine || !path || !*path) { return; }
	char abs[PATH_MAX];
	if (!realpath(path, abs) || !editor_project_valid(abs)) {
		editor_set_status(editor, "open project failed: project.ini not found");
		return;
	}
	if (chdir(abs) != 0) {
		editor_set_status(editor, "open project failed: chdir");
		return;
	}
	render_config_init(&engine->config);
	if (!config_load_file(&engine->config, "project.ini")) {
		editor_set_status(editor, "open project failed: project.ini read error");
		return;
	}
	engine->config.editor_enabled = 1;
	snprintf(engine->config.mode, sizeof(engine->config.mode), "editor");
	snprintf(engine->config.project_root, sizeof(engine->config.project_root), "%.*s", (int)sizeof(engine->config.project_root) - 1, abs);
	editor->tab_count = 0;
	editor->active_tab = -1;
	snprintf(editor->scene_path, sizeof(editor->scene_path), "%s", engine->config.scene_path);
	snprintf(editor->explorer.root, sizeof(editor->explorer.root), "%.*s", (int)sizeof(editor->explorer.root) - 1, abs);
	snprintf(editor->explorer.current_dir, sizeof(editor->explorer.current_dir), "%s", editor->explorer.root);
	editor_explorer_history_reset(editor);
	editor->explorer.expanded_count = 0;
	editor->explorer.selected_path[0] = '\0';
	editor->explorer.context_path[0] = '\0';
	editor->explorer.context_empty = 0;
	editor->explorer.filter[0] = '\0';
	editor->explorer.type_filter = 0;
	if (engine->config.scene_path[0]) {
		editor_open_scene_now(editor, engine, engine->config.scene_path);
	}
	editor_explorer_refresh(editor);
	if (!editor_explorer_is_expanded(editor, editor->explorer.root)) { editor_explorer_toggle_expanded(editor, editor->explorer.root); }
	editor_set_status(editor, "project opened");
}

static void editor_open_project_from_dialog(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	editor_open_project_now(editor, engine, editor->dialog_path);
	if (editor->dialog_kind == NB_EDITOR_DIALOG_OPEN_PROJECT) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
}

static void editor_create_project_from_dialog(editor_t* editor, engine_t* engine) {
	if (!editor || !engine) { return; }
	char path[NB_EDITOR_FS_PATH_MAX];
	if (!editor_fs_normalize(editor->dialog_path, path, sizeof(path)) || !path[0]) {
		editor_set_status(editor, "new project failed: invalid path");
		return;
	}
	if (editor_fs_exists(path)) {
		editor_set_status(editor, "new project failed: path exists");
		return;
	}
	char scenes[NB_EDITOR_FS_PATH_MAX];
	char assets[NB_EDITOR_FS_PATH_MAX];
	char scripts[NB_EDITOR_FS_PATH_MAX];
	char models[NB_EDITOR_FS_PATH_MAX];
	char scene[NB_EDITOR_FS_PATH_MAX];
	char marker[NB_EDITOR_FS_PATH_MAX];
	editor_fs_join_into(scenes, sizeof(scenes), path, "scenes");
	editor_fs_join_into(assets, sizeof(assets), path, "assets");
	editor_fs_join_into(scripts, sizeof(scripts), path, "scripts");
	editor_fs_join_into(models, sizeof(models), path, "models");
	editor_fs_join_into(scene, sizeof(scene), scenes, "main.tscn");
	editor_fs_join_into(marker, sizeof(marker), path, "project.ini");
	if (!editor_fs_make_dir(scenes) || !editor_fs_make_dir(assets) || !editor_fs_make_dir(scripts) || !editor_fs_make_dir(models)) {
		editor_set_status(editor, "new project failed: cannot create folders");
		return;
	}
	char name[NB_EDITOR_FS_NAME_MAX];
	editor_fs_basename(path, name, sizeof(name));
	char content[4096];
	snprintf(content, sizeof(content),
		"virtual_resolution = %dx%d\nwindow_resolution = %dx%d\nvertex_snap_grid = %dx%d\n"
		"affine_strength = %.3f\ncolor_depth = %d\ndithering = %d\nfog_enabled = %d\n"
		"fog_range = %.3f,%.3f\nfog_color = %d,%d,%d\nsky_top = %d,%d,%d\nsky_bottom = %d,%d,%d\n"
		"near_plane = %.4f\nfar_plane = %.3f\ncull_backfaces = %d\nreject_near_triangles = %d\n"
		"subdivide_large_triangles = %d\nsubdivide_area_threshold = %.3f\n"
		"blend_mode = none\nshade_mode = gouraud\nlight_direction = %.3f,%.3f,%.3f\nlight_color = %.3f,%.3f,%.3f\n"
		"texture_vram_limit = %d\nwireframe_enabled = %d\nxray_enabled = %d\nhud_enabled = %d\n"
		"hud_debug_enabled = %d\neditor_enabled = 1\nlog_level = info\n"
		"project_name = %s\nmode = editor\nscene_path = scenes/main.tscn\n",
		engine->config.width, engine->config.height, engine->config.window_width, engine->config.window_height,
		engine->config.snap_grid_width, engine->config.snap_grid_height, engine->config.affine_strength,
		engine->config.color_depth, engine->config.dither_enabled, engine->config.fog_enabled,
		engine->config.fog_near, engine->config.fog_far,
		engine->config.fog_color_rgb[0], engine->config.fog_color_rgb[1], engine->config.fog_color_rgb[2],
		engine->config.sky_top_rgb[0], engine->config.sky_top_rgb[1], engine->config.sky_top_rgb[2],
		engine->config.sky_bottom_rgb[0], engine->config.sky_bottom_rgb[1], engine->config.sky_bottom_rgb[2],
		engine->config.near_plane, engine->config.far_plane, engine->config.cull_backfaces, engine->config.reject_near_triangles,
		engine->config.subdivide_large_triangles, engine->config.subdivide_area_threshold,
		engine->config.light_direction[0], engine->config.light_direction[1], engine->config.light_direction[2],
		engine->config.light_color[0], engine->config.light_color[1], engine->config.light_color[2],
		engine->config.texture_vram_limit, engine->config.wireframe_enabled, engine->config.xray_enabled,
		engine->config.hud_enabled, engine->config.hud_debug_enabled, name);
	const char* scene_data = "[gd_scene load_steps=1 format=2]\n\n[node name=\"Root\" type=\"Node\"]\n";
	if (!editor_fs_write_text(marker, content, strlen(content)) || !editor_fs_write_text(scene, scene_data, strlen(scene_data))) {
		editor_set_status(editor, "new project failed: write error");
		return;
	}
	editor->dialog_kind = NB_EDITOR_DIALOG_NONE;
	editor_open_project_now(editor, engine, path);
}

static void editor_draw_dialogs(editor_t* editor, engine_t* engine, int win_w, int win_h) {
	if (!editor || !editor->nk_ctx || editor->dialog_kind == NB_EDITOR_DIALOG_NONE) { return; }
	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	float w = 460.0f;
	float h = 176.0f;
	if (editor->dialog_kind == NB_EDITOR_DIALOG_MOVE || editor->dialog_kind == NB_EDITOR_DIALOG_IMPORT) { h = 210.0f; }
	float x = (float)(win_w - (int)w) * 0.5f;
	float y = (float)(win_h - (int)h) * 0.5f;
	if (nk_begin(ctx, "EditorDialog", nk_rect(x, y, w, h), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 24.0f, 1);
		nk_label(ctx, editor->dialog_text, NK_TEXT_LEFT);
		if (editor->dialog_kind == NB_EDITOR_DIALOG_NEW_SCENE) {
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_path, (int)sizeof(editor->dialog_path), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Create")) { editor_create_scene_from_dialog(editor, engine); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_NEW_FOLDER) {
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_path, (int)sizeof(editor->dialog_path), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Create")) { editor_explorer_perform_new_folder(editor); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_OPEN_PROJECT) {
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_path, (int)sizeof(editor->dialog_path), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Open")) { editor_open_project_from_dialog(editor, engine); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_NEW_PROJECT) {
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_path, (int)sizeof(editor->dialog_path), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Create")) { editor_create_project_from_dialog(editor, engine); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_DELETE) {
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_labelf(ctx, NK_TEXT_LEFT, "%s", editor->dialog_path);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Delete")) { editor_explorer_perform_delete(editor, engine); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_MOVE) {
			nk_layout_row_dynamic(ctx, 22.0f, 1);
			nk_labelf(ctx, NK_TEXT_LEFT, "from: %s", editor->dialog_path);
			nk_layout_row_dynamic(ctx, 22.0f, 1);
			nk_label(ctx, "folder:", NK_TEXT_LEFT);
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_dst, (int)sizeof(editor->dialog_dst), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Move")) { editor_explorer_perform_move(editor, engine); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else if (editor->dialog_kind == NB_EDITOR_DIALOG_IMPORT) {
			nk_layout_row_dynamic(ctx, 22.0f, 1);
			nk_label(ctx, "source:", NK_TEXT_LEFT);
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_path, (int)sizeof(editor->dialog_path), nk_filter_default);
			nk_layout_row_dynamic(ctx, 22.0f, 1);
			nk_label(ctx, "folder:", NK_TEXT_LEFT);
			nk_layout_row_dynamic(ctx, 24.0f, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_GOTO_END_ON_ACTIVATE, editor->dialog_dst, (int)sizeof(editor->dialog_dst), nk_filter_default);
			nk_layout_row_dynamic(ctx, 28.0f, 2);
			if (nk_button_label(ctx, "Import")) { editor_explorer_perform_import(editor); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; }
		}
		else {
			nk_layout_row_dynamic(ctx, 28.0f, 3);
			if (nk_button_label(ctx, "Save")) { editor_dialog_apply_discard(editor, engine, 1); }
			if (nk_button_label(ctx, "Discard")) { editor_dialog_apply_discard(editor, engine, 0); }
			if (nk_button_label(ctx, "Cancel")) { editor->dialog_kind = NB_EDITOR_DIALOG_NONE; editor->dialog_target = 0; }
		}
	}
	nk_end(ctx);
}

void editor_update(editor_t* editor, engine_t* engine) {
	if (!editor_enabled(editor) || !engine) { return; }

	struct nk_context* ctx = (struct nk_context*)editor->nk_ctx;
	nk_input_end(ctx);
	editor_inspector_autosave(editor, 0);

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
	if (editor->active_tab >= 0 && editor->active_tab < editor->tab_count) {
		editor->tabs[editor->active_tab].dirty = engine->scene_doc.dirty ? 1 : 0;
	}

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
	editor->win_w = win_w;
	editor->win_h = win_h;

	editor_draw_scene_panel(editor, engine, win_w, win_h);
	editor_draw_render_config(editor, engine, win_w, win_h);
	editor_draw_hierarchy(editor, engine, win_w, win_h);
	editor_draw_inspector(editor, engine, win_w, win_h);
	editor_draw_reparent(editor, engine, win_w, win_h);

	int tab_y = 10;
	int tab_h = 28;
	int toolbar_y = tab_y + tab_h + 6;
	editor->toolbar_h = 34;
	editor->viewport_x = 360;
	editor->viewport_y = toolbar_y + editor->toolbar_h + 8;
	editor->viewport_w = win_w - 360 - 360 - 20;
	int explorer_max_h = win_h - editor->viewport_y - 10 - 160;
	if (explorer_max_h < 240) { explorer_max_h = 240; }
	if (editor->explorer.height < 240) { editor->explorer.height = 240; }
	if (editor->explorer.height > explorer_max_h) { editor->explorer.height = explorer_max_h; }
	if (editor->explorer.height > win_h / 2) { editor->explorer.height = win_h / 2; }
	int explorer_y = win_h - 10 - editor->explorer.height;
	editor->viewport_h = explorer_y - editor->viewport_y - 8;
	if (editor->viewport_w < 160) { editor->viewport_w = 160; }
	if (editor->viewport_h < 120) { editor->viewport_h = 120; }

	editor_draw_scene_tabs(editor, engine, win_w);

	if (nk_begin(ctx, "Tools", nk_rect((float)editor->viewport_x, (float)toolbar_y, (float)editor->viewport_w, (float)editor->toolbar_h), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
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
	editor_draw_explorer(editor, engine, win_w, win_h);
	editor_draw_dialogs(editor, engine, win_w, win_h);

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
