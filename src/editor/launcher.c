#define _XOPEN_SOURCE 700

#include <SDL2/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

#include "../../third_party/nuklear/nuklear.h"
#include "../../third_party/nuklear/nuklear_sdl_renderer.h"

#include "core/config.h"
#include "core/engine.h"
#include "core/platform.h"
#include "editor/editor.h"
#include "editor/editor_icons.h"
#include "editor/launcher.h"

extern char** environ;

static int launcher_logical_scene(const char* root, const char* scene, char* out, size_t size);
static void launcher_scene_abs(const launcher_recent_t* entry, char* out, size_t size);

typedef struct {
	int w;
	int h;
	int top_h;
	int tool_h;
	int footer_h;
	int rail_w;
	int content_y;
	int content_h;
	int list_w;
} launcher_metrics_t;

static char* launcher_trim(char* text) {
	if (!text) { return NULL; }
	while (*text && isspace((unsigned char)*text)) { text++; }
	char* end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1])) { end--; }
	*end = '\0';
	return text;
}

static void launcher_copy(char* dst, size_t size, const char* src) {
	if (!dst || size == 0) { return; }
	snprintf(dst, size, "%.*s", (int)size - 1, src ? src : "");
}

static void launcher_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0) { return; }
	if (b && b[0] == '/') {
		launcher_copy(out, size, b);
		return;
	}
	snprintf(out, size, "%.*s/%.*s", (int)size / 2 - 1, a ? a : "", (int)size / 2 - 1, b ? b : "");
}

static void launcher_dirname_into(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return; }
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path) {
		launcher_copy(out, size, "/");
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len >= size) { len = size - 1; }
	memcpy(out, path, len);
	out[len] = '\0';
}

static int launcher_is_dir(const char* path) {
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int launcher_absolute_path(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return 0; }
	char resolved[PATH_MAX];
	if (realpath(path, resolved)) {
		launcher_copy(out, size, resolved);
		return 1;
	}
	if (path[0] == '/') {
		launcher_copy(out, size, path);
		return 1;
	}
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) { return 0; }
	launcher_join(out, size, cwd, path);
	return 1;
}

static void launcher_basename(const char* path, char* out, size_t size) {
	const char* end = path ? path + strlen(path) : NULL;
	while (end && end > path && end[-1] == '/') { end--; }
	if (!end) {
		launcher_copy(out, size, "project");
		return;
	}
	const char* slash = strrchr(path, '/');
	const char* start = slash ? slash + 1 : path;
	size_t len = (size_t)(end - start);
	if (len >= size) { len = size - 1; }
	memcpy(out, start, len);
	out[len] = '\0';
}

static void launcher_ellipsize(const char* text, char* out, size_t size) {
	if (!text || !out || size == 0) { return; }
	size_t len = strlen(text);
	if (len + 1 <= size) {
		launcher_copy(out, size, text);
		return;
	}
	if (size < 6) {
		launcher_copy(out, size, text);
		return;
	}
	size_t tail = size - 5;
	snprintf(out, size, ".../%.*s", (int)tail, text + len - tail);
}

static int launcher_mkdir_p(const char* path) {
	if (!path || !*path || launcher_is_dir(path)) { return 1; }
	char tmp[PATH_MAX];
	launcher_copy(tmp, sizeof(tmp), path);
	for (char* p = tmp + 1; *p; p++) {
		if (*p != '/') { continue; }
		*p = '\0';
		mkdir(tmp, 0755);
		*p = '/';
	}
	mkdir(path, 0755);
	return launcher_is_dir(path);
}

static int launcher_write_text(const char* path, const char* text) {
	FILE* file = fopen(path, "wb");
	if (!file) { return 0; }
	size_t len = strlen(text);
	int ok = fwrite(text, 1, len, file) == len;
	if (fclose(file) != 0) { ok = 0; }
	return ok;
}

int launcher_recent_add(launcher_t* launcher, const launcher_recent_t* entry) {
	if (!launcher || !entry || !entry->root[0]) { return 0; }
	launcher_recent_t normalized;
	memset(&normalized, 0, sizeof(normalized));
	if (!launcher_absolute_path(entry->root, normalized.root, sizeof(normalized.root))) { return 0; }
	if (!launcher_is_dir(normalized.root)) { return 0; }
	launcher_recent_t info;
	if (launcher_project_info(normalized.root, &info)) {
		normalized = info;
	}
	else {
		launcher_copy(normalized.name, sizeof(normalized.name), entry->name);
		if (!launcher_logical_scene(normalized.root, entry->scene, normalized.scene, sizeof(normalized.scene))) {
			launcher_copy(normalized.scene, sizeof(normalized.scene), "res://scenes/main.tscn");
		}
		if (!normalized.name[0]) { launcher_basename(normalized.root, normalized.name, sizeof(normalized.name)); }
	}
	for (int i = 0; i < launcher->recent_count; i++) {
		if (strcmp(launcher->recent[i].root, normalized.root) == 0) {
			if (i > 0) {
				launcher_recent_t moved = launcher->recent[i];
				memmove(&launcher->recent[1], &launcher->recent[0], (size_t)i * sizeof(launcher_recent_t));
				launcher->recent[0] = moved;
			}
			launcher->recent[0] = normalized;
			launcher_apply_filter(launcher);
			return 1;
		}
	}
	if (launcher->recent_count >= NB_LAUNCHER_RECENT_MAX) {
		memmove(&launcher->recent[1], &launcher->recent[0], (size_t)(NB_LAUNCHER_RECENT_MAX - 1) * sizeof(launcher_recent_t));
		launcher->recent_count = NB_LAUNCHER_RECENT_MAX - 1;
	}
	launcher->recent[launcher->recent_count++] = normalized;
	launcher_apply_filter(launcher);
	return 1;
}

int launcher_recent_remove(launcher_t* launcher, int index) {
	if (!launcher || index < 0 || index >= launcher->recent_count) { return 0; }
	memmove(&launcher->recent[index], &launcher->recent[index + 1], (size_t)(launcher->recent_count - index - 1) * sizeof(launcher_recent_t));
	launcher->recent_count--;
	memset(&launcher->recent[launcher->recent_count], 0, sizeof(launcher_recent_t));
	launcher_apply_filter(launcher);
	return 1;
}

int launcher_recent_load(launcher_t* launcher, const char* path) {
	if (!launcher || !path || !*path) { return 0; }
	FILE* file = fopen(path, "rb");
	if (!file) { return 0; }
	char line[PATH_MAX * 2];
	int count = 0;
	while (fgets(line, sizeof(line), file)) {
		char* text = launcher_trim(line);
		if (strncmp(text, "recent=", 7) != 0) { continue; }
		char* name = text + 7;
		char* root = strchr(name, '|');
		if (!root) { continue; }
		*root++ = '\0';
		char* scene = strchr(root, '|');
		if (!scene) { continue; }
		*scene++ = '\0';
		launcher_recent_t entry;
		memset(&entry, 0, sizeof(entry));
		launcher_copy(entry.name, sizeof(entry.name), name);
		launcher_copy(entry.root, sizeof(entry.root), root);
		launcher_copy(entry.scene, sizeof(entry.scene), scene);
		if (launcher_recent_add(launcher, &entry)) { count++; }
	}
	fclose(file);
	launcher_apply_filter(launcher);
	return count;
}

int launcher_recent_save(const launcher_t* launcher, const char* path) {
	if (!launcher || !path || !*path) { return 0; }
	FILE* file = fopen(path, "wb");
	if (!file) { return 0; }
	for (int i = 0; i < launcher->recent_count; i++) {
		const launcher_recent_t* entry = &launcher->recent[i];
		fprintf(file, "recent=%s|%s|%s\n", entry->name, entry->root, entry->scene);
	}
	return fclose(file) == 0;
}

static void launcher_parse_ini(const char* path, char* name, size_t name_size, char* scene, size_t scene_size) {
	FILE* file = fopen(path, "rb");
	if (!file) { return; }
	char line[PATH_MAX];
	while (fgets(line, sizeof(line), file)) {
		char* text = launcher_trim(line);
		if (!*text || strchr(text, '=') == NULL) { continue; }
		char* eq = strchr(text, '=');
		*eq++ = '\0';
		char* key = launcher_trim(text);
		char* value = launcher_trim(eq);
		if (strcmp(key, "project_name") == 0) { launcher_copy(name, name_size, value); }
		else if (strcmp(key, "scene_path") == 0) { launcher_copy(scene, scene_size, value); }
	}
	fclose(file);
}

static int launcher_logical_scene(const char* root, const char* scene, char* out, size_t size) {
	if (!root || !scene || !out || !size) { return 0; }
	const char* logical = scene;
	if (strncmp(logical, "res://", 6) == 0) { logical += 6; }
	if (!*logical) { return 0; }
	if (logical[0] == '/') {
		size_t root_len = strlen(root);
		while (root_len > 0 && root[root_len - 1] == '/') { root_len--; }
		if (root_len == 0 || strncmp(logical, root, root_len) != 0 || (logical[root_len] != '/' && logical[root_len] != '\0')) { return 0; }
		logical += root_len;
		while (*logical == '/') { logical++; }
	}
	const char* part = logical;
	for (const char* p = logical;; p++) {
		if (*p != '/' && *p != '\0') { continue; }
		size_t len = (size_t)(p - part);
		if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) { return 0; }
		if (*p == '\0') { break; }
		part = p + 1;
	}
	snprintf(out, size, "res://%.*s", (int)size - 7, logical);
	return 1;
}

static void launcher_scene_abs(const launcher_recent_t* entry, char* out, size_t size) {
	if (!entry || !out || !size) { return; }
	const char* logical = entry->scene;
	if (strncmp(logical, "res://", 6) == 0) { logical += 6; }
	if (logical[0] == '/') {
		launcher_copy(out, size, logical);
		return;
	}
	launcher_join(out, size, entry->root, logical);
}

int launcher_project_info(const char* root, launcher_recent_t* out) {
	if (!root || !out) { return 0; }
	char abs[PATH_MAX];
	if (!launcher_absolute_path(root, abs, sizeof(abs)) || !launcher_is_dir(abs)) { return 0; }
	char ini[PATH_MAX];
	launcher_join(ini, sizeof(ini), abs, "project.ini");
	if (access(ini, F_OK) != 0) { return 0; }
	char scene[PATH_MAX];
	char name[NB_LAUNCHER_NAME_MAX];
	memset(name, 0, sizeof(name));
	memset(scene, 0, sizeof(scene));
	launcher_parse_ini(ini, name, sizeof(name), scene, sizeof(scene));
	if (!scene[0]) { launcher_copy(scene, sizeof(scene), "scenes/main.tscn"); }
	char logical[PATH_MAX];
	if (!launcher_logical_scene(abs, scene, logical, sizeof(logical))) {
		launcher_logical_scene(abs, "scenes/main.tscn", logical, sizeof(logical));
	}
	launcher_recent_t candidate;
	memset(&candidate, 0, sizeof(candidate));
	launcher_copy(candidate.root, sizeof(candidate.root), abs);
	launcher_copy(candidate.scene, sizeof(candidate.scene), logical);
	char scene_abs[PATH_MAX];
	launcher_scene_abs(&candidate, scene_abs, sizeof(scene_abs));
	if (access(scene_abs, F_OK) != 0) {
		launcher_copy(candidate.scene, sizeof(candidate.scene), "res://scenes/main.tscn");
		launcher_scene_abs(&candidate, scene_abs, sizeof(scene_abs));
	}
	memset(out, 0, sizeof(*out));
	launcher_copy(out->root, sizeof(out->root), abs);
	launcher_copy(out->name, sizeof(out->name), name[0] ? name : "project");
	if (access(scene_abs, F_OK) == 0) { launcher_copy(out->scene, sizeof(out->scene), candidate.scene); }
	return 1;
}

long long launcher_project_modified(const launcher_recent_t* entry) {
	if (!entry) { return 0; }
	char path[PATH_MAX];
	if (entry->scene[0]) { launcher_scene_abs(entry, path, sizeof(path)); }
	else { launcher_join(path, sizeof(path), entry->root, "project.ini"); }
	struct stat st;
	if (stat(path, &st) != 0) {
		launcher_join(path, sizeof(path), entry->root, "project.ini");
		if (stat(path, &st) != 0) { return 0; }
	}
	return (long long)st.st_mtime;
}

static int launcher_text_contains(const char* text, const char* needle) {
	if (!needle || !*needle) { return 1; }
	if (!text) { return 0; }
	char lower_text[PATH_MAX * 2];
	char lower_needle[NB_LAUNCHER_SEARCH_MAX];
	size_t n = 0;
	for (const unsigned char* p = (const unsigned char*)text; *p && n + 1 < sizeof(lower_text); p++) {
		lower_text[n++] = (char)tolower(*p);
	}
	lower_text[n] = '\0';
	n = 0;
	for (const unsigned char* p = (const unsigned char*)needle; *p && n + 1 < sizeof(lower_needle); p++) {
		lower_needle[n++] = (char)tolower(*p);
	}
	lower_needle[n] = '\0';
	return strstr(lower_text, lower_needle) != NULL;
}

static int launcher_compare_indices(const launcher_t* launcher, int a, int b) {
	const launcher_recent_t* lhs = &launcher->recent[a];
	const launcher_recent_t* rhs = &launcher->recent[b];
	if (launcher->sort_mode == NB_LAUNCHER_SORT_NAME) { return strcasecmp(lhs->name, rhs->name); }
	if (launcher->sort_mode == NB_LAUNCHER_SORT_PATH) { return strcasecmp(lhs->root, rhs->root); }
	long long left_time = launcher_project_modified(lhs);
	long long right_time = launcher_project_modified(rhs);
	if (left_time != right_time) { return left_time > right_time ? -1 : 1; }
	return strcasecmp(lhs->name, rhs->name);
}

void launcher_apply_filter(launcher_t* launcher) {
	if (!launcher) { return; }
	launcher->filtered_count = 0;
	for (int i = 0; i < launcher->recent_count; i++) {
		const launcher_recent_t* entry = &launcher->recent[i];
		if (launcher_text_contains(entry->name, launcher->search) || launcher_text_contains(entry->root, launcher->search)) {
			launcher->filtered[launcher->filtered_count++] = i;
		}
	}
	for (int i = 1; i < launcher->filtered_count; i++) {
		int key = launcher->filtered[i];
		int j = i - 1;
		while (j >= 0 && launcher_compare_indices(launcher, launcher->filtered[j], key) > 0) {
			launcher->filtered[j + 1] = launcher->filtered[j];
			j--;
		}
		launcher->filtered[j + 1] = key;
	}
	int selected_visible = 0;
	for (int i = 0; i < launcher->filtered_count; i++) {
		if (launcher->filtered[i] == launcher->selected) { selected_visible = 1; break; }
	}
	if (launcher->filtered_count == 0) {
		launcher->selected = -1;
	}
	else if (!selected_visible) {
		launcher->selected = launcher->filtered[0];
	}
}

int launcher_import_project(launcher_t* launcher, const char* path) {
	if (!launcher || !path || !*path) { return 0; }
	launcher_recent_t info;
	if (!launcher_project_info(path, &info)) { return 0; }
	if (!launcher_recent_add(launcher, &info)) { return 0; }
	for (int i = 0; i < launcher->recent_count; i++) {
		if (strcmp(launcher->recent[i].root, info.root) == 0) {
			launcher->selected = i;
			break;
		}
	}
	launcher_apply_filter(launcher);
	launcher_recent_save(launcher, launcher->settings_path);
	return 1;
}

static int launcher_skip_scan_dir(const char* name) {
	return strcmp(name, "build") == 0 || strcmp(name, "third_party") == 0 || strcmp(name, ".git") == 0 || strcmp(name, "node_modules") == 0;
}

static void launcher_scan_into(launcher_t* launcher, const char* start, int depth) {
	if (depth < 0 || launcher->recent_count >= NB_LAUNCHER_RECENT_MAX) { return; }
	DIR* dir = opendir(start);
	if (!dir) { return; }
	struct dirent* item = NULL;
	while ((item = readdir(dir)) != NULL) {
		if (item->d_name[0] == '.' || launcher_skip_scan_dir(item->d_name)) { continue; }
		char child[PATH_MAX];
		launcher_join(child, sizeof(child), start, item->d_name);
		if (!launcher_is_dir(child)) { continue; }
		launcher_recent_t info;
		if (launcher_project_info(child, &info)) {
			launcher_recent_add(launcher, &info);
		}
		else if (depth > 0) {
			launcher_scan_into(launcher, child, depth - 1);
		}
		if (launcher->recent_count >= NB_LAUNCHER_RECENT_MAX) { break; }
	}
	closedir(dir);
}

static void launcher_scan_fallback(launcher_t* launcher) {
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd))) {
		launcher_scan_into(launcher, cwd, 2);
	}
	char* base = SDL_GetBasePath();
	if (base) {
		launcher_scan_into(launcher, base, 2);
		SDL_free(base);
	}
	launcher_apply_filter(launcher);
}

int launcher_create_project(const char* parent, const char* name, launcher_recent_t* out) {
	if (!parent || !*parent || !name || !*name) { return 0; }
	for (const char* p = name; *p; p++) {
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') { return 0; }
	}
	char root[PATH_MAX];
	launcher_join(root, sizeof(root), parent, name);
	if (access(root, F_OK) == 0 || !launcher_mkdir_p(root)) { return 0; }
	char scenes[PATH_MAX];
	char assets[PATH_MAX];
	char scripts[PATH_MAX];
	char models[PATH_MAX];
	char scene[PATH_MAX];
	char ini[PATH_MAX];
	launcher_join(scenes, sizeof(scenes), root, "scenes");
	launcher_join(assets, sizeof(assets), root, "assets");
	launcher_join(scripts, sizeof(scripts), root, "scripts");
	launcher_join(models, sizeof(models), root, "models");
	launcher_join(scene, sizeof(scene), scenes, "main.tscn");
	launcher_join(ini, sizeof(ini), root, "project.ini");
	if (!launcher_mkdir_p(scenes) || !launcher_mkdir_p(assets) || !launcher_mkdir_p(scripts) || !launcher_mkdir_p(models)) { return 0; }
	char config[1024];
	snprintf(config, sizeof(config),
		"project_name = %s\nmode = editor\neditor_enabled = 1\nscene_path = scenes/main.tscn\n"
		"virtual_resolution = 320x180\nwindow_resolution = 1280x720\nlog_level = info\n",
		name);
	if (!launcher_write_text(ini, config)) { return 0; }
	if (!launcher_write_text(scene, "[gd_scene load_steps=1 format=3]\n\n[node name=\"Root\" type=\"Node\"]\n")) { return 0; }
	if (!launcher_project_info(root, out)) {
		memset(out, 0, sizeof(*out));
		launcher_copy(out->name, sizeof(out->name), name);
		launcher_copy(out->root, sizeof(out->root), root);
		launcher_copy(out->scene, sizeof(out->scene), "res://scenes/main.tscn");
	}
	return 1;
}

void launcher_make_spawn_args(launcher_spawn_args_t* args, const launcher_recent_t* entry, int editor) {
	if (!args || !entry) { return; }
	memset(args, 0, sizeof(*args));
	ssize_t len = readlink("/proc/self/exe", args->exe, sizeof(args->exe) - 1);
	if (len <= 0) { launcher_copy(args->exe, sizeof(args->exe), "nodebench"); }
	else { args->exe[len] = '\0'; }
	snprintf(args->mode, sizeof(args->mode), "--mode=%s", editor ? "editor" : "game");
	snprintf(args->project, sizeof(args->project), "--project=%s", entry->root);
	if (entry->scene[0]) { snprintf(args->scene, sizeof(args->scene), "--scene=%s", entry->scene); }
	args->argv[0] = args->exe;
	args->argv[1] = args->mode;
	args->argv[2] = args->project;
	if (args->scene[0]) { args->argv[3] = args->scene; }
}

int launcher_open_project(launcher_t* launcher, engine_t* engine, int editor) {
	if (!launcher || !engine || launcher->selected < 0 || launcher->selected >= launcher->recent_count) { return 0; }
	const launcher_recent_t* entry = &launcher->recent[launcher->selected];
	if (!launcher_is_dir(entry->root)) {
		char name[NB_LAUNCHER_NAME_MAX];
		launcher_basename(entry->root, name, sizeof(name));
		snprintf(launcher->status, sizeof(launcher->status), "project folder not found: %s", name);
		return 0;
	}
	launcher_recent_add(launcher, entry);
	launcher_recent_save(launcher, launcher->settings_path);
	launcher_spawn_args_t args;
	launcher_make_spawn_args(&args, &launcher->recent[launcher->selected], editor);
	pid_t pid = 0;
	if (posix_spawn(&pid, args.exe, NULL, NULL, args.argv, environ) != 0) {
		snprintf(launcher->status, sizeof(launcher->status), "could not start nodebench");
		return 0;
	}
	snprintf(launcher->status, sizeof(launcher->status), "starting %s in %s mode", entry->name, editor ? "editor" : "game");
	launcher->exit_requested = 1;
	return 1;
}

static struct nk_user_font* launcher_user_font(void* font) {
	if (!font) { return NULL; }
	return &((struct nk_font*)font)->handle;
}

static void launcher_label_aligned(launcher_t* launcher, const char* text, void* font, struct nk_color color, nk_flags align) {
	struct nk_context* ctx = (struct nk_context*)launcher->nk_ctx;
	if (!ctx || !text) { return; }
	struct nk_user_font* user_font = launcher_user_font(font);
	if (user_font) { nk_style_push_font(ctx, user_font); }
	nk_style_push_color(ctx, &ctx->style.text.color, color);
	nk_label(ctx, text, align);
	nk_style_pop_color(ctx);
	if (user_font) { nk_style_pop_font(ctx); }
}

static void launcher_label(launcher_t* launcher, const char* text, void* font, struct nk_color color) {
	launcher_label_aligned(launcher, text, font, color, NK_TEXT_LEFT);
}

static struct nk_style_button launcher_button_style(struct nk_context* ctx, struct nk_color color, struct nk_color text) {
	struct nk_style_button style = ctx->style.button;
	style.normal = nk_style_item_color(color);
	style.hover = nk_style_item_color(nk_rgb(color.r + 16, color.g + 16, color.b + 16));
	style.active = nk_style_item_color(nk_rgb(color.r - 12, color.g - 12, color.b - 12));
	style.text_normal = text;
	style.text_hover = text;
	style.text_active = text;
	style.border = 0.0f;
	style.rounding = 3.0f;
	style.padding = nk_vec2(10.0f, 6.0f);
	style.text_alignment = NK_TEXT_CENTERED;
	return style;
}

static void launcher_metrics(engine_t* engine, launcher_metrics_t* metrics) {
	memset(metrics, 0, sizeof(*metrics));
	int w = 0;
	int h = 0;
	platform_output_size(&engine->platform, &w, &h);
	metrics->w = w > 960 ? w : 960;
	metrics->h = h > 620 ? h : 620;
	metrics->top_h = 40;
	metrics->tool_h = 40;
	metrics->footer_h = 28;
	metrics->rail_w = 130;
	metrics->content_y = metrics->top_h + metrics->tool_h;
	metrics->content_h = metrics->h - metrics->content_y - metrics->footer_h;
	metrics->list_w = metrics->w - metrics->rail_w;
	if (metrics->content_h < 320) { metrics->content_h = 320; }
}

static void launcher_draw_rect(SDL_Renderer* renderer, int x, int y, int w, int h, struct nk_color color) {
	if (!renderer || w <= 0 || h <= 0) { return; }
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	SDL_Rect rect = { x, y, w, h };
	SDL_RenderFillRect(renderer, &rect);
}

static void launcher_draw_line(SDL_Renderer* renderer, int x1, int y1, int x2, int y2, struct nk_color color) {
	if (!renderer) { return; }
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

static void launcher_background(engine_t* engine) {
	SDL_Renderer* renderer = (SDL_Renderer*)platform_renderer(&engine->platform);
	launcher_metrics_t m;
	launcher_metrics(engine, &m);
	launcher_draw_rect(renderer, 0, 0, m.w, m.h, nk_rgb(30, 30, 30));
	launcher_draw_rect(renderer, 0, 0, m.w, m.top_h, nk_rgb(37, 37, 37));
	launcher_draw_rect(renderer, 0, m.top_h, m.w, m.tool_h, nk_rgb(42, 42, 42));
	launcher_draw_rect(renderer, 0, m.content_y, m.list_w, m.content_h, nk_rgb(38, 38, 38));
	launcher_draw_rect(renderer, m.list_w, m.content_y, m.rail_w, m.content_h, nk_rgb(44, 44, 44));
	launcher_draw_rect(renderer, 0, m.h - m.footer_h, m.w, m.footer_h, nk_rgb(37, 37, 37));
	launcher_draw_line(renderer, 0, m.top_h, m.w, m.top_h, nk_rgb(58, 58, 58));
	launcher_draw_line(renderer, 0, m.content_y, m.w, m.content_y, nk_rgb(58, 58, 58));
	launcher_draw_line(renderer, m.list_w, m.content_y, m.list_w, m.h - m.footer_h, nk_rgb(58, 58, 58));
	launcher_draw_line(renderer, 0, m.h - m.footer_h, m.w, m.h - m.footer_h, nk_rgb(58, 58, 58));
}

static void launcher_modified_text(const launcher_recent_t* entry, char* out, size_t size) {
	long long value = launcher_project_modified(entry);
	if (value <= 0) {
		launcher_copy(out, size, "-");
		return;
	}
	time_t raw = (time_t)value;
	struct tm local;
	if (!localtime_r(&raw, &local)) {
		launcher_copy(out, size, "-");
		return;
	}
	char text[32];
	if (strftime(text, sizeof(text), "%Y-%m-%d", &local) == 0) {
		launcher_copy(out, size, "-");
		return;
	}
	launcher_copy(out, size, text);
}

static void launcher_select_root(launcher_t* launcher, const char* root) {
	if (!launcher || !root) { return; }
	for (int i = 0; i < launcher->recent_count; i++) {
		if (strcmp(launcher->recent[i].root, root) == 0) {
			launcher->selected = i;
			launcher_apply_filter(launcher);
			return;
		}
	}
}

static void launcher_move_selection(launcher_t* launcher, int delta) {
	if (!launcher || launcher->filtered_count <= 0) { return; }
	int pos = 0;
	for (int i = 0; i < launcher->filtered_count; i++) {
		if (launcher->filtered[i] == launcher->selected) {
			pos = i;
			break;
		}
	}
	pos = (pos + delta + launcher->filtered_count) % launcher->filtered_count;
	launcher->selected = launcher->filtered[pos];
}

static void launcher_action_label(launcher_t* launcher, struct nk_context* ctx, float height, void* font, struct nk_color color, const char* text) {
	float ratios[2] = { 0.2f, 0.8f };
	nk_layout_row(ctx, NK_DYNAMIC, height, 2, ratios);
	launcher_label(launcher, " ", launcher->font_base, nk_rgb(44, 44, 44));
	launcher_label(launcher, text, font, color);
}

static int launcher_action_button(launcher_t* launcher, struct nk_context* ctx, const struct nk_style_button* style, const char* label) {
	struct nk_style_button action = *style;
	action.text_alignment = NK_TEXT_LEFT;
	action.padding = nk_vec2(12.0f, 6.0f);
	float ratios[3] = { 0.2f, 0.5f, 0.3f };
	nk_layout_row(ctx, NK_DYNAMIC, 30.0f, 3, ratios);
	launcher_label(launcher, " ", launcher->font_base, nk_rgb(44, 44, 44));
	int pressed = nk_button_label_styled(ctx, &action, label);
	launcher_label(launcher, " ", launcher->font_base, nk_rgb(44, 44, 44));
	return pressed;
}

static void launcher_draw_top_bar(launcher_t* launcher, struct nk_context* ctx, const launcher_metrics_t* m, struct nk_style_button flat) {
	int brand_w = 200;
	int settings_w = 100;
	int spacer = m->w - 32 - brand_w - settings_w;
	if (spacer < 12) { spacer = 12; }
	nk_layout_row_begin(ctx, NK_STATIC, (float)m->top_h, 3);
	nk_layout_row_push(ctx, (float)brand_w);
	launcher_label(launcher, "NODEBENCH", launcher->font_heading, nk_rgb(232, 232, 232));
	nk_layout_row_push(ctx, (float)spacer);
	launcher_label(launcher, " ", launcher->font_base, nk_rgb(37, 37, 37));
	nk_layout_row_push(ctx, (float)settings_w);
	if (nk_button_label_styled(ctx, &flat, "Settings")) { launcher->settings_open = 1; }
	nk_layout_row_end(ctx);
}

static void launcher_draw_toolbar(launcher_t* launcher, struct nk_context* ctx, launcher_metrics_t* m, struct nk_style_button flat, struct nk_style_button primary) {
	static const float ratios[4] = { 0.09f, 0.09f, 0.08f, 0.74f };
	nk_layout_row(ctx, NK_DYNAMIC, (float)m->tool_h, 4, ratios);
	if (nk_button_label_styled(ctx, &primary, "Create")) { launcher->create_dialog_open = 1; launcher_copy(launcher->new_name, sizeof(launcher->new_name), ""); }
	if (nk_button_label_styled(ctx, &flat, "Import")) { launcher->import_dialog_open = 1; }
	if (nk_button_label_styled(ctx, &flat, "Scan")) {
		launcher_scan_fallback(launcher);
		snprintf(launcher->status, sizeof(launcher->status), "%d recent project(s)", launcher->recent_count);
	}
	launcher_label(launcher, " ", launcher->font_base, nk_rgb(42, 42, 42));
}

static void launcher_draw_project_list(launcher_t* launcher, struct nk_context* ctx, engine_t* engine, const launcher_metrics_t* m, struct nk_style_button primary, struct nk_style_button flat) {
	struct nk_style_button row = launcher_button_style(ctx, nk_rgb(38, 38, 38), nk_rgb(232, 232, 232));
	row.text_alignment = NK_TEXT_LEFT;
	row.padding = nk_vec2(12.0f, 6.0f);
	row.rounding = 0.0f;
	struct nk_style_button selected_row = row;
	selected_row.normal = nk_style_item_color(nk_rgb(64, 64, 64));
	selected_row.hover = nk_style_item_color(nk_rgb(72, 72, 72));
	selected_row.active = nk_style_item_color(nk_rgb(56, 56, 56));
	selected_row.border = 1.0f;
	selected_row.border_color = nk_rgb(90, 90, 90);
	char line[NB_LAUNCHER_NAME_MAX + NB_LAUNCHER_PATH_MAX + 64];
	snprintf(line, sizeof(line), "%-26.26s  %-52.52s  %10s", "Name", "Path", "Modified");
	nk_layout_row_dynamic(ctx, 24.0f, 1);
	launcher_label(launcher, line, launcher->font_semibold, nk_rgb(120, 120, 120));

	if (launcher->filtered_count == 0) {
		nk_layout_row_dynamic(ctx, 24.0f, 1);
		launcher_label(launcher, launcher->recent_count == 0 ? "No projects yet" : "No projects match the filter", launcher->font_semibold, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 20.0f, 1);
		launcher_label(launcher, "Create a new project or import an existing one to get started.", launcher->font_base, nk_rgb(170, 170, 170));
		nk_layout_row_dynamic(ctx, 12.0f, 1);
		float actions[2] = { 0.5f, 0.5f };
		nk_layout_row(ctx, NK_DYNAMIC, 36.0f, 2, actions);
		if (nk_button_label_styled(ctx, &primary, "Create New Project")) { launcher->create_dialog_open = 1; }
		if (nk_button_label_styled(ctx, &flat, "Import Existing Project")) { launcher->import_dialog_open = 1; }
		return;
	}
	(void)m;
	for (int i = 0; i < launcher->filtered_count; i++) {
		int index = launcher->filtered[i];
		const launcher_recent_t* entry = &launcher->recent[index];
		char path[NB_LAUNCHER_PATH_MAX];
		char modified[32];
		launcher_ellipsize(entry->root, path, sizeof(path));
		launcher_modified_text(entry, modified, sizeof(modified));
		snprintf(line, sizeof(line), "%-26.26s  %-52.52s  %10s", entry->name, path, modified);
		nk_layout_row_dynamic(ctx, 40.0f, 1);
		if (nk_button_label_styled(ctx, index == launcher->selected ? &selected_row : &row, line)) {
			Uint32 now = SDL_GetTicks();
			if (launcher->selected == index && launcher->click_row == index && now - launcher->click_tick < 400) {
				launcher_open_project(launcher, engine, 1);
			}
			else {
				launcher->selected = index;
				launcher->click_row = index;
				launcher->click_tick = now;
			}
		}
	}
}

static void launcher_draw_action_rail(launcher_t* launcher, struct nk_context* ctx, engine_t* engine, struct nk_style_button flat, struct nk_style_button primary, struct nk_style_button success, struct nk_style_button danger) {
	struct nk_style_button disabled = launcher_button_style(ctx, nk_rgb(44, 44, 44), nk_rgb(110, 110, 110));
	launcher_action_label(launcher, ctx, 22.0f, launcher->font_semibold, nk_rgb(120, 120, 120), "PROJECT");
	launcher_action_label(launcher, ctx, 8.0f, launcher->font_base, nk_rgb(44, 44, 44), " ");
	if (launcher->selected < 0 || launcher->selected >= launcher->recent_count) {
		launcher_action_label(launcher, ctx, 22.0f, launcher->font_base, nk_rgb(170, 170, 170), "No project selected");
		launcher_action_label(launcher, ctx, 12.0f, launcher->font_base, nk_rgb(44, 44, 44), " ");
		launcher_action_button(launcher, ctx, &disabled, "Edit");
		launcher_action_button(launcher, ctx, &disabled, "Run");
		launcher_action_button(launcher, ctx, &disabled, "Open Folder");
		launcher_action_button(launcher, ctx, &disabled, "Remove");
		return;
	}
	const launcher_recent_t* entry = &launcher->recent[launcher->selected];
	launcher_action_label(launcher, ctx, 22.0f, launcher->font_semibold, nk_rgb(232, 232, 232), entry->name);
	launcher_action_label(launcher, ctx, 12.0f, launcher->font_base, nk_rgb(44, 44, 44), " ");
	if (launcher_action_button(launcher, ctx, &primary, "Edit")) {
		launcher_open_project(launcher, engine, 1);
	}
	if (launcher_action_button(launcher, ctx, &success, "Run")) {
		launcher_open_project(launcher, engine, 0);
	}
	if (launcher_action_button(launcher, ctx, &flat, "Open Folder")) {
		if (!platform_open_directory(entry->root)) {
			char name[NB_LAUNCHER_NAME_MAX];
			launcher_basename(entry->root, name, sizeof(name));
			snprintf(launcher->status, sizeof(launcher->status), "could not open %s", name);
		}
	}
	if (launcher_action_button(launcher, ctx, &danger, "Remove")) {
		launcher->remove_pending = 1;
	}
}

static void launcher_draw_create_modal(launcher_t* launcher, struct nk_context* ctx, engine_t* engine, const launcher_metrics_t* m, struct nk_style_button primary, struct nk_style_button flat) {
	if (!launcher->create_dialog_open) { return; }
	struct nk_style_item old_bg = ctx->style.window.fixed_background;
	ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(36, 36, 36));
	float x = (m->w - 460.0f) * 0.5f;
	float y = (m->h - 264.0f) * 0.5f;
	if (x < 0.0f) { x = 0.0f; }
	if (y < 0.0f) { y = 0.0f; }
	if (nk_begin(ctx, "Create Project", nk_rect(x, y, 460.0f, 264.0f), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 22.0f, 1);
		launcher_label(launcher, "Project name", launcher->font_semibold, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 30.0f, 1);
		nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, launcher->new_name, (int)sizeof(launcher->new_name), nk_filter_default);
		nk_layout_row_dynamic(ctx, 22.0f, 1);
		launcher_label(launcher, "Parent folder", launcher->font_semibold, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 30.0f, 1);
		nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, launcher->new_parent, (int)sizeof(launcher->new_parent), nk_filter_default);
		nk_layout_row_dynamic(ctx, 12.0f, 1);
		float actions[3] = { 0.34f, 0.33f, 0.33f };
		nk_layout_row(ctx, NK_DYNAMIC, 36.0f, 3, actions);
		if (nk_button_label_styled(ctx, &primary, "Create & Edit")) {
			launcher_recent_t entry;
			if (launcher_create_project(launcher->new_parent, launcher->new_name, &entry)) {
				launcher_recent_add(launcher, &entry);
				launcher_select_root(launcher, entry.root);
				snprintf(launcher->status, sizeof(launcher->status), "created %s", entry.name);
				launcher->create_dialog_open = 0;
				launcher_open_project(launcher, engine, 1);
			}
			else {
				launcher_copy(launcher->status, sizeof(launcher->status), "create failed: check name and parent folder");
			}
		}
		if (nk_button_label_styled(ctx, &flat, "Browse")) {
			if (!platform_open_directory(launcher->new_parent)) {
				launcher_copy(launcher->status, sizeof(launcher->status), "could not open parent folder");
			}
		}
		if (nk_button_label_styled(ctx, &flat, "Cancel")) { launcher->create_dialog_open = 0; }
	}
	nk_end(ctx);
	ctx->style.window.fixed_background = old_bg;
}

static void launcher_draw_import_modal(launcher_t* launcher, struct nk_context* ctx, const launcher_metrics_t* m, struct nk_style_button primary, struct nk_style_button flat) {
	if (!launcher->import_dialog_open) { return; }
	struct nk_style_item old_bg = ctx->style.window.fixed_background;
	ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(36, 36, 36));
	float x = (m->w - 460.0f) * 0.5f;
	float y = (m->h - 232.0f) * 0.5f;
	if (x < 0.0f) { x = 0.0f; }
	if (y < 0.0f) { y = 0.0f; }
	if (nk_begin(ctx, "Import Project", nk_rect(x, y, 460.0f, 232.0f), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 22.0f, 1);
		launcher_label(launcher, "Project folder", launcher->font_semibold, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 30.0f, 1);
		nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, launcher->import_path, (int)sizeof(launcher->import_path), nk_filter_default);
		nk_layout_row_dynamic(ctx, 12.0f, 1);
		float actions[3] = { 0.34f, 0.33f, 0.33f };
		nk_layout_row(ctx, NK_DYNAMIC, 36.0f, 3, actions);
		if (nk_button_label_styled(ctx, &primary, "Import")) {
			if (launcher_import_project(launcher, launcher->import_path)) {
				snprintf(launcher->status, sizeof(launcher->status), "imported %s", launcher->recent[launcher->selected].name);
				launcher->import_dialog_open = 0;
			}
			else {
				launcher_copy(launcher->status, sizeof(launcher->status), "import failed: project.ini not found");
			}
		}
		if (nk_button_label_styled(ctx, &flat, "Browse")) {
			const char* path = launcher_is_dir(launcher->import_path) ? launcher->import_path : launcher->new_parent;
			platform_open_directory(path);
		}
		if (nk_button_label_styled(ctx, &flat, "Cancel")) { launcher->import_dialog_open = 0; }
	}
	nk_end(ctx);
	ctx->style.window.fixed_background = old_bg;
}

static void launcher_draw_remove_modal(launcher_t* launcher, struct nk_context* ctx, const launcher_metrics_t* m, struct nk_style_button primary, struct nk_style_button flat) {
	if (!launcher->remove_pending || launcher->selected < 0 || launcher->selected >= launcher->recent_count) {
		launcher->remove_pending = 0;
		return;
	}
	struct nk_style_item old_bg = ctx->style.window.fixed_background;
	ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(36, 36, 36));
	float x = (m->w - 400.0f) * 0.5f;
	float y = (m->h - 168.0f) * 0.5f;
	if (x < 0.0f) { x = 0.0f; }
	if (y < 0.0f) { y = 0.0f; }
	if (nk_begin(ctx, "Remove Project", nk_rect(x, y, 400.0f, 168.0f), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		char text[NB_LAUNCHER_NAME_MAX + 64];
		snprintf(text, sizeof(text), "Remove %s from the recent list?", launcher->recent[launcher->selected].name);
		nk_layout_row_dynamic(ctx, 24.0f, 1);
		launcher_label(launcher, text, launcher->font_base, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 12.0f, 1);
		float actions[2] = { 0.5f, 0.5f };
		nk_layout_row(ctx, NK_DYNAMIC, 36.0f, 2, actions);
		if (nk_button_label_styled(ctx, &primary, "Remove")) {
			launcher_recent_remove(launcher, launcher->selected);
			launcher_recent_save(launcher, launcher->settings_path);
			launcher_copy(launcher->status, sizeof(launcher->status), "project removed");
			launcher->remove_pending = 0;
		}
		if (nk_button_label_styled(ctx, &flat, "Cancel")) { launcher->remove_pending = 0; }
	}
	nk_end(ctx);
	ctx->style.window.fixed_background = old_bg;
}

static void launcher_draw_settings_modal(launcher_t* launcher, struct nk_context* ctx, const launcher_metrics_t* m, struct nk_style_button flat) {
	if (!launcher->settings_open) { return; }
	struct nk_style_item old_bg = ctx->style.window.fixed_background;
	ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(36, 36, 36));
	float x = (m->w - 460.0f) * 0.5f;
	float y = (m->h - 196.0f) * 0.5f;
	if (x < 0.0f) { x = 0.0f; }
	if (y < 0.0f) { y = 0.0f; }
	if (nk_begin(ctx, "Settings", nk_rect(x, y, 460.0f, 196.0f), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_dynamic(ctx, 22.0f, 1);
		launcher_label(launcher, "Recent projects", launcher->font_semibold, nk_rgb(232, 232, 232));
		nk_layout_row_dynamic(ctx, 20.0f, 1);
		launcher_label(launcher, launcher->settings_path, launcher->font_base, nk_rgb(170, 170, 170));
		nk_layout_row_dynamic(ctx, 12.0f, 1);
		float actions[2] = { 0.5f, 0.5f };
		nk_layout_row(ctx, NK_DYNAMIC, 36.0f, 2, actions);
		if (nk_button_label_styled(ctx, &flat, "Open Folder")) {
			char folder[NB_LAUNCHER_PATH_MAX];
			launcher_dirname_into(launcher->settings_path, folder, sizeof(folder));
			if (!platform_open_directory(folder)) {
				launcher_copy(launcher->status, sizeof(launcher->status), "could not open settings folder");
			}
		}
		if (nk_button_label_styled(ctx, &flat, "Close")) { launcher->settings_open = 0; }
	}
	nk_end(ctx);
	ctx->style.window.fixed_background = old_bg;
}

int launcher_init(launcher_t* launcher, engine_t* engine) {
	if (!launcher || !engine || !engine->editor.nk_ctx) { return 0; }
	launcher->engine = engine;
	launcher->nk_ctx = engine->editor.nk_ctx;
	launcher->font_base = engine->editor.ui_font_base;
	launcher->font_semibold = engine->editor.ui_font_semibold;
	launcher->font_heading = engine->editor.ui_font_heading;
	launcher->selected = 0;
	launcher->sort_mode = NB_LAUNCHER_SORT_PATH;
	launcher->create_dialog_open = 0;
	launcher->import_dialog_open = 0;
	launcher->settings_open = 0;
	launcher->remove_pending = 0;
	launcher->exit_requested = 0;
	launcher->click_row = -1;
	launcher->click_tick = 0;
	char* pref = SDL_GetPrefPath("nodebench", "nodebench");
	if (pref) {
		launcher_join(launcher->settings_path, sizeof(launcher->settings_path), pref, "launcher.ini");
		SDL_free(pref);
	}
	else {
		launcher_copy(launcher->settings_path, sizeof(launcher->settings_path), "launcher.ini");
	}
	if (!getcwd(launcher->new_parent, sizeof(launcher->new_parent))) {
		launcher_copy(launcher->new_parent, sizeof(launcher->new_parent), ".");
	}
	launcher_copy(launcher->import_path, sizeof(launcher->import_path), launcher->new_parent);
	if (!launcher_recent_load(launcher, launcher->settings_path)) {
		launcher_scan_fallback(launcher);
	}
	launcher_apply_filter(launcher);
	snprintf(launcher->status, sizeof(launcher->status), "%d recent project(s)", launcher->recent_count);
	platform_set_mouse_capture_blocked(&engine->platform, 1);
	platform_set_mouse_capture(&engine->platform, 0);
	launcher->ready = 1;
	return 1;
}

void launcher_shutdown(launcher_t* launcher) {
	if (!launcher || !launcher->ready) { return; }
	launcher_recent_save(launcher, launcher->settings_path);
	if (launcher->engine) { platform_set_mouse_capture_blocked(&launcher->engine->platform, 0); }
	launcher->ready = 0;
	launcher->nk_ctx = NULL;
}

void launcher_begin_frame(launcher_t* launcher) {
	if (!launcher || !launcher->ready || !launcher->nk_ctx) { return; }
	nk_input_begin((struct nk_context*)launcher->nk_ctx);
}

int launcher_handle_event(launcher_t* launcher, const void* sdl_event) {
	if (!launcher || !launcher->ready || !launcher->nk_ctx || !sdl_event) { return 0; }
	struct nk_context* ctx = (struct nk_context*)launcher->nk_ctx;
	SDL_Event* event = (SDL_Event*)sdl_event;
	int consumed = nk_sdl_handle_event(event);
	if (event->type != SDL_KEYDOWN || event->key.repeat) { return consumed; }
	if (event->key.keysym.sym == SDLK_ESCAPE) {
		if (launcher->create_dialog_open) { launcher->create_dialog_open = 0; return 1; }
		if (launcher->import_dialog_open) { launcher->import_dialog_open = 0; return 1; }
		if (launcher->settings_open) { launcher->settings_open = 0; return 1; }
		if (launcher->remove_pending) { launcher->remove_pending = 0; return 1; }
		launcher->selected = -1;
		return 1;
	}
	if (launcher->create_dialog_open || launcher->import_dialog_open || launcher->settings_open || launcher->remove_pending) { return consumed; }
	if (event->key.keysym.sym == SDLK_UP) { launcher_move_selection(launcher, -1); return 1; }
	if (event->key.keysym.sym == SDLK_DOWN) { launcher_move_selection(launcher, 1); return 1; }
	if (event->key.keysym.sym == SDLK_RETURN && launcher->selected >= 0 && launcher->selected < launcher->recent_count) {
		launcher_open_project(launcher, launcher->engine, 1);
		return 1;
	}
	(void)ctx;
	return consumed;
}

void launcher_update(launcher_t* launcher, engine_t* engine) {
	struct nk_context* ctx = launcher && launcher->ready ? (struct nk_context*)launcher->nk_ctx : NULL;
	if (!ctx || !engine) {
		if (ctx) { nk_input_end(ctx); }
		return;
	}
	launcher_metrics_t m;
	launcher_metrics(engine, &m);
	struct nk_style_item old_bg = ctx->style.window.fixed_background;
	ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
	struct nk_user_font* base_font = launcher_user_font(launcher->font_base);
	if (base_font) { nk_style_push_font(ctx, base_font); }
	nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(16.0f, 0.0f));
	struct nk_style_button primary = launcher_button_style(ctx, nk_rgb(90, 90, 90), nk_rgb(240, 240, 240));
	struct nk_style_button flat = launcher_button_style(ctx, nk_rgb(48, 48, 48), nk_rgb(200, 200, 200));
	struct nk_style_button success = launcher_button_style(ctx, nk_rgb(70, 70, 70), nk_rgb(238, 238, 238));
	struct nk_style_button danger = launcher_button_style(ctx, nk_rgb(78, 78, 78), nk_rgb(245, 245, 245));

	if (nk_begin(ctx, "NodebenchLauncher", nk_rect(0.0f, 0.0f, (float)m.w, (float)m.h), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
		launcher_draw_top_bar(launcher, ctx, &m, flat);
		launcher_draw_toolbar(launcher, ctx, &m, flat, primary);
		float body[2] = { (float)m.list_w / (float)m.w, (float)m.rail_w / (float)m.w };
		nk_layout_row(ctx, NK_DYNAMIC, (float)m.content_h, 2, body);
		if (nk_group_begin(ctx, "Projects", 0)) {
			launcher_draw_project_list(launcher, ctx, engine, &m, primary, flat);
			nk_group_end(ctx);
		}
		if (nk_group_begin(ctx, "Actions", NK_WINDOW_NO_SCROLLBAR)) {
			launcher_draw_action_rail(launcher, ctx, engine, flat, primary, success, danger);
			nk_group_end(ctx);
		}
		float footer[2] = { 0.82f, 0.18f };
		nk_layout_row(ctx, NK_DYNAMIC, (float)m.footer_h, 2, footer);
		launcher_label(launcher, launcher->status, launcher->font_base, nk_rgb(170, 170, 170));
		launcher_label_aligned(launcher, "Offline mode", launcher->font_base, nk_rgb(120, 120, 120), NK_TEXT_RIGHT);
	}
	nk_end(ctx);
	nk_style_pop_vec2(ctx);
	ctx->style.window.fixed_background = old_bg;
	launcher_draw_create_modal(launcher, ctx, engine, &m, primary, flat);
	launcher_draw_import_modal(launcher, ctx, &m, primary, flat);
	launcher_draw_remove_modal(launcher, ctx, &m, primary, flat);
	launcher_draw_settings_modal(launcher, ctx, &m, flat);
	if (base_font) { nk_style_pop_font(ctx); }
	nk_input_end(ctx);
}

void launcher_render(launcher_t* launcher, engine_t* engine) {
	if (!launcher || !launcher->ready || !engine || !launcher->nk_ctx) { return; }
	launcher_background(engine);
	nk_sdl_render(NK_ANTI_ALIASING_ON);
}
