#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/config.h"
#include "core/file_io.h"

static const unsigned char bayer_4x4[16] = {
	0, 8, 2, 10,
	12, 4, 14, 6,
	3, 11, 1, 9,
	15, 7, 13, 5
};

static char* trim(char* text) {
	if (!text) { return NULL; }

	while (*text && isspace((unsigned char)*text)) { text++; }

	char* end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1])) { end--; }
	*end = '\0';

	return text;
}

static void config_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0) { return; }
	size_t half = size / 2 - 1;
	snprintf(out, size, "%.*s/%.*s", (int)half, a ? a : "", (int)half, b ? b : "");
}

static int config_dir_exists(const char* path) {
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void config_dirname_into(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return; }
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path) {
		snprintf(out, size, "/");
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len >= size) { len = size - 1; }
	memmove(out, path, len);
	out[len] = '\0';
}

static int config_absolute_path(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return 0; }
	char resolved[PATH_MAX];
	if (realpath(path, resolved)) {
		snprintf(out, size, "%.*s", (int)size - 1, resolved);
		return 1;
	}
	if (path[0] == '/') {
		snprintf(out, size, "%.*s", (int)size - 1, path);
		return 1;
	}
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) { return 0; }
	config_join(out, size, cwd, path);
	return 1;
}

static int config_project_file(const char* dir, char* out, size_t size) {
	char candidate[PATH_MAX];
	config_join(candidate, sizeof(candidate), dir, "project.ini");
	if (access(candidate, F_OK) != 0) { return 0; }
	char abs[PATH_MAX];
	if (!config_absolute_path(candidate, abs, sizeof(abs))) { return 0; }
	config_dirname_into(abs, out, size);
	return 1;
}

static int config_walk_up(const char* start, char* out, size_t size) {
	char dir[PATH_MAX];
	if (!config_absolute_path(start, dir, sizeof(dir))) { return 0; }
	if (!config_dir_exists(dir)) { config_dirname_into(dir, dir, sizeof(dir)); }
	while (1) {
		if (config_project_file(dir, out, size)) { return 1; }
		if (strcmp(dir, "/") == 0) { break; }
		config_dirname_into(dir, dir, sizeof(dir));
	}
	return 0;
}

static int config_skip_scan_dir(const char* name) {
	return strcmp(name, "build") == 0 || strcmp(name, "third_party") == 0 || strcmp(name, ".git") == 0 || strcmp(name, "node_modules") == 0;
}

static int config_name_compare(const void* a, const void* b) {
	const char* lhs = *(const char* const*)a;
	const char* rhs = *(const char* const*)b;
	return strcmp(lhs, rhs);
}

static int config_scan_projects(const char* start, int depth, char* out, size_t size) {
	if (config_project_file(start, out, size)) { return 1; }
	if (depth <= 0) { return 0; }
	DIR* handle = opendir(start);
	if (!handle) { return 0; }
	char* names[128];
	int count = 0;
	struct dirent* item = NULL;
	while ((item = readdir(handle)) != NULL && count < 128) {
		if (item->d_name[0] == '.' || config_skip_scan_dir(item->d_name)) { continue; }
		char child[PATH_MAX];
		config_join(child, sizeof(child), start, item->d_name);
		if (!config_dir_exists(child)) { continue; }
		names[count] = strdup(item->d_name);
		if (names[count]) { count++; }
	}
	closedir(handle);
	qsort(names, (size_t)count, sizeof(names[0]), config_name_compare);
	int found_at = -1;
	for (int i = 0; i < count; i++) {
		char child[PATH_MAX];
		config_join(child, sizeof(child), start, names[i]);
		free(names[i]);
		if (config_scan_projects(child, depth - 1, out, size)) {
			found_at = i;
			break;
		}
	}
	int next = found_at >= 0 ? found_at + 1 : count;
	for (int i = next; i < count; i++) { free(names[i]); }
	return found_at >= 0;
}

static const char* config_find_arg_value(int argc, char** argv, const char* key) {
	if (!argv || !key) { return NULL; }
	char prefix[128];
	char exact[128];
	snprintf(prefix, sizeof(prefix), "--%s=", key);
	snprintf(exact, sizeof(exact), "--%s", key);
	size_t prefix_len = strlen(prefix);
	for (int i = 1; i < argc; i++) {
		if (!argv[i]) { continue; }
		if (strncmp(argv[i], prefix, prefix_len) == 0) { return argv[i] + prefix_len; }
		if (strcmp(argv[i], exact) == 0 && i + 1 < argc && argv[i + 1]) { return argv[i + 1]; }
	}
	return NULL;
}

int config_cli_requests_launcher(int argc, char** argv) {
	const char* mode = config_find_arg_value(argc, argv, "mode");
	return mode && strcmp(mode, "launcher") == 0;
}

int config_cli_selects_session(int argc, char** argv) {
	if (config_cli_requests_launcher(argc, argv)) { return 0; }
	if (config_find_arg_value(argc, argv, "mode")) { return 1; }
	const char* const keys[] = { "project", "project_root", "root", "scene", "scene_path" };
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const char* value = config_find_arg_value(argc, argv, keys[i]);
		if (value && *value) { return 1; }
	}
	return 0;
}

int config_resolve_project_root(int argc, char** argv, char* out, size_t size) {
	if (!out || size == 0) { return 0; }
	const char* explicit = config_find_arg_value(argc, argv, "project");
	if (!explicit) { explicit = config_find_arg_value(argc, argv, "project_root"); }
	if (!explicit) { explicit = config_find_arg_value(argc, argv, "root"); }
	if (explicit && *explicit) {
		char abs[PATH_MAX];
		if (config_absolute_path(explicit, abs, sizeof(abs))) {
			if (config_dir_exists(abs)) {
				snprintf(out, size, "%.*s", (int)size - 1, abs);
				return 1;
			}
			char parent[PATH_MAX];
			config_dirname_into(abs, parent, sizeof(parent));
			if (config_walk_up(parent, out, size)) { return 1; }
			snprintf(out, size, "%.*s", (int)size - 1, parent);
			return 1;
		}
	}

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd))) {
		if (config_walk_up(cwd, out, size)) { return 1; }
	}
	char exe[PATH_MAX];
	char exe_dir[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len > 0) {
		exe[len] = '\0';
		config_dirname_into(exe, exe_dir, sizeof(exe_dir));
		if (config_walk_up(exe_dir, out, size)) { return 1; }
	}
	if (getcwd(cwd, sizeof(cwd))) {
		if (config_scan_projects(cwd, 3, out, size)) { return 1; }
	}
	if (len > 0 && config_scan_projects(exe_dir, 2, out, size)) { return 1; }
	if (getcwd(cwd, sizeof(cwd))) {
		snprintf(out, size, "%.*s", (int)size - 1, cwd);
		return 1;
	}
	return 0;
}

static int parse_int(const char* text, int* out) {
	if (!text || !out || !*text) { return 0; }

	char* end = NULL;
	long value = strtol(text, &end, 10);

	if (end == text || *end != '\0') { return 0; }
	if (value < -2147483647L || value > 2147483647L) { return 0; }

	*out = (int)value;
	return 1;
}

static int parse_float(const char* text, float* out) {
	if (!text || !out || !*text) { return 0; }

	char* end = NULL;
	float value = strtof(text, &end);

	if (end == text || *end != '\0') { return 0; }
	if (!isfinite(value)) { return 0; }

	*out = value;
	return 1;
}

static int parse_bool(const char* text, int* out) {
	if (!text || !out) { return 0; }

	if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "on") == 0) {
		*out = 1;
		return 1;
	}

	if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0 || strcmp(text, "off") == 0) {
		*out = 0;
		return 1;
	}

	return parse_int(text, out);
}

static int parse_pair_ints(const char* text, char separator, int* first, int* second) {
	if (!text || !first || !second || strlen(text) >= 128) { return 0; }

	char buffer[128];
	strcpy(buffer, text);

	char* split = strchr(buffer, separator);
	if (!split) { return 0; }
	*split = '\0';

	return parse_int(trim(buffer), first) && parse_int(trim(split + 1), second);
}

static int parse_pair_floats(const char* text, char separator, float* first, float* second) {
	if (!text || !first || !second || strlen(text) >= 128) { return 0; }

	char buffer[128];
	strcpy(buffer, text);

	char* split = strchr(buffer, separator);
	if (!split) { return 0; }
	*split = '\0';

	return parse_float(trim(buffer), first) && parse_float(trim(split + 1), second);
}

static int parse_vec3_ints(const char* text, int* out) {
	if (!text || !out || strlen(text) >= 128) { return 0; }

	char buffer[128];
	strcpy(buffer, text);

	char* token = strtok(buffer, ",");

	for (int i = 0; i < 3; i++) {
		if (!token || !parse_int(trim(token), &out[i])) { return 0; }
		token = strtok(NULL, ",");
	}

	return token == NULL;
}

static int parse_vec3_floats(const char* text, float* out) {
	if (!text || !out || strlen(text) >= 128) { return 0; }

	char buffer[128];
	strcpy(buffer, text);

	char* token = strtok(buffer, ",");

	for (int i = 0; i < 3; i++) {
		if (!token || !parse_float(trim(token), &out[i])) { return 0; }
		token = strtok(NULL, ",");
	}

	return token == NULL;
}

static int parse_blend_mode(const char* text, nb_blend_mode_t* out) {
	if (!text || !out) { return 0; }

	if (strcmp(text, "none") == 0) {
		*out = NB_BLEND_NONE;
		return 1;
	}

	if (strcmp(text, "alpha50") == 0) {
		*out = NB_BLEND_ALPHA_50;
		return 1;
	}

	if (strcmp(text, "add") == 0) {
		*out = NB_BLEND_ADD;
		return 1;
	}

	if (strcmp(text, "subtract") == 0) {
		*out = NB_BLEND_SUBTRACT;
		return 1;
	}

	if (strcmp(text, "addquarter") == 0) {
		*out = NB_BLEND_ADD_QUARTER;
		return 1;
	}

	return 0;
}

static int parse_shade_mode(const char* text, nb_shade_mode_t* out) {
	if (!text || !out) { return 0; }

	if (strcmp(text, "flat") == 0) {
		*out = NB_SHADE_FLAT;
		return 1;
	}

	if (strcmp(text, "gouraud") == 0) {
		*out = NB_SHADE_GOURAUD;
		return 1;
	}

	return 0;
}

static int parse_log_level(const char* text, nb_log_level_t* out) {
	if (!text || !out) { return 0; }

	if (strcmp(text, "debug") == 0) {
		*out = NB_LOG_DEBUG;
		return 1;
	}

	if (strcmp(text, "info") == 0) {
		*out = NB_LOG_INFO;
		return 1;
	}

	if (strcmp(text, "warn") == 0) {
		*out = NB_LOG_WARN;
		return 1;
	}

	if (strcmp(text, "error") == 0) {
		*out = NB_LOG_ERROR;
		return 1;
	}

	return 0;
}

static void config_add_binding(render_config_t* config, const char* action, const char* keys) {
	if (!config || !action || !keys || config->key_binding_count >= NB_CONFIG_BINDINGS_MAX) { return; }
	for (int i = 0; i < config->key_binding_count; i++) {
		if (strcmp(config->key_bindings[i].action, action) == 0) {
			snprintf(config->key_bindings[i].keys, sizeof(config->key_bindings[i].keys), "%s", keys);
			return;
		}
	}
	nb_key_binding_t* b = &config->key_bindings[config->key_binding_count++];
	snprintf(b->action, sizeof(b->action), "%s", action);
	snprintf(b->keys, sizeof(b->keys), "%s", keys);
}

static int config_set_value(render_config_t* config, const char* key, const char* value) {
	if (!config || !key || !value) { return 0; }

	if (strcmp(key, "virtual_resolution") == 0) { return parse_pair_ints(value, 'x', &config->width, &config->height); }

	else if (strcmp(key, "window_resolution") == 0) { return parse_pair_ints(value, 'x', &config->window_width, &config->window_height); }

	else if (strcmp(key, "vertex_snap_grid") == 0) { return parse_pair_ints(value, 'x', &config->snap_grid_width, &config->snap_grid_height); }

	else if (strcmp(key, "affine_strength") == 0) { return parse_float(value, &config->affine_strength); }

	else if (strcmp(key, "color_depth") == 0) { return parse_int(value, &config->color_depth); }

	else if (strcmp(key, "dithering") == 0) { return parse_bool(value, &config->dither_enabled); }

	else if (strcmp(key, "fog_enabled") == 0) { return parse_bool(value, &config->fog_enabled); }

	else if (strcmp(key, "fog_range") == 0) { return parse_pair_floats(value, ',', &config->fog_near, &config->fog_far); }

	else if (strcmp(key, "fog_color") == 0) { return parse_vec3_ints(value, config->fog_color_rgb); }

	else if (strcmp(key, "sky_top") == 0) { return parse_vec3_ints(value, config->sky_top_rgb); }

	else if (strcmp(key, "sky_bottom") == 0) { return parse_vec3_ints(value, config->sky_bottom_rgb); }

	else if (strcmp(key, "near_plane") == 0) { return parse_float(value, &config->near_plane); }

	else if (strcmp(key, "far_plane") == 0) { return parse_float(value, &config->far_plane); }

	else if (strcmp(key, "cull_backfaces") == 0) { return parse_bool(value, &config->cull_backfaces); }

	else if (strcmp(key, "reject_near_triangles") == 0) { return parse_bool(value, &config->reject_near_triangles); }

	else if (strcmp(key, "subdivide_large_triangles") == 0) { return parse_bool(value, &config->subdivide_large_triangles); }

	else if (strcmp(key, "subdivide_area_threshold") == 0) { return parse_float(value, &config->subdivide_area_threshold); }

	else if (strcmp(key, "blend_mode") == 0) { return parse_blend_mode(value, &config->blend_mode); }

	else if (strcmp(key, "shade_mode") == 0) { return parse_shade_mode(value, &config->shade_mode); }

	else if (strcmp(key, "light_direction") == 0) { return parse_vec3_floats(value, config->light_direction); }

	else if (strcmp(key, "light_color") == 0) { return parse_vec3_floats(value, config->light_color); }

	else if (strcmp(key, "texture_vram_limit") == 0) { return parse_int(value, &config->texture_vram_limit); }

	else if (strcmp(key, "wireframe_enabled") == 0) { return parse_bool(value, &config->wireframe_enabled); }

	else if (strcmp(key, "xray_enabled") == 0) { return parse_bool(value, &config->xray_enabled); }

	else if (strcmp(key, "hud_enabled") == 0) { return parse_bool(value, &config->hud_enabled); }

	else if (strcmp(key, "hud_debug_enabled") == 0) { return parse_bool(value, &config->hud_debug_enabled); }

	else if (strcmp(key, "editor_enabled") == 0) {
		if (!parse_bool(value, &config->editor_enabled)) { return 0; }
		snprintf(config->mode, sizeof(config->mode), "%s", config->editor_enabled ? "editor" : "game");
		return 1;
	}

	else if (strcmp(key, "max_frames") == 0) { return parse_int(value, &config->max_frames); }

	else if (strcmp(key, "log_level") == 0) { return parse_log_level(value, &config->log_level); }

	else if (strcmp(key, "log_file") == 0) {
		if (strlen(value) >= sizeof(config->log_file)) { return 0; }
		snprintf(config->log_file, sizeof(config->log_file), "%s", value);
		return 1;
	}

	else if (strcmp(key, "scene_path") == 0 || strcmp(key, "scene") == 0) {
		if (!value[0] || strlen(value) >= sizeof(config->scene_path)) { return 0; }
		snprintf(config->scene_path, sizeof(config->scene_path), "%s", value);
		return 1;
	}

	else if (strcmp(key, "project_name") == 0) {
		if (!value[0] || strlen(value) >= sizeof(config->project_name)) { return 0; }
		snprintf(config->project_name, sizeof(config->project_name), "%s", value);
		return 1;
	}

	else if (strcmp(key, "project_root") == 0 || strcmp(key, "root") == 0 || strcmp(key, "project") == 0) {
		if (!value[0] || strlen(value) >= sizeof(config->project_root)) { return 0; }
		snprintf(config->project_root, sizeof(config->project_root), "%s", value);
		return 1;
	}

	else if (strcmp(key, "mode") == 0) {
		if (strcmp(value, "game") != 0 && strcmp(value, "editor") != 0 && strcmp(value, "launcher") != 0) { return 0; }
		snprintf(config->mode, sizeof(config->mode), "%s", value);
		config->editor_enabled = strcmp(value, "editor") == 0 ? 1 : 0;
		return 1;
	}

	return 0;
}

static int config_validate(const render_config_t* config) {
	if (!config) { return 0; }
	if (config->width <= 0 || config->height <= 0) { return 0; }
	if (config->window_width <= 0 || config->window_height <= 0) { return 0; }
	if (config->snap_grid_width <= 0 || config->snap_grid_height <= 0) { return 0; }
	if (config->near_plane <= 0.0f || config->far_plane <= config->near_plane) { return 0; }
	if (config->affine_strength < 0.0f || config->affine_strength > 1.0f) { return 0; }
	if (config->color_depth != 15) { return 0; }
	if (config->fog_near < 0.0f || config->fog_far <= config->fog_near) { return 0; }
	if (!config->dither_matrix) { return 0; }
	if (config->subdivide_area_threshold < 0.0f) { return 0; }
	if (config->texture_vram_limit < 0) { return 0; }
	if (config->max_frames < 0) { return 0; }
	if (strcmp(config->mode, "launcher") != 0 && !config->scene_path[0]) { return 0; }
	if (strcmp(config->mode, "game") != 0 && strcmp(config->mode, "editor") != 0 && strcmp(config->mode, "launcher") != 0) { return 0; }

	for (int i = 0; i < 3; i++) {
		if (config->fog_color_rgb[i] < 0 || config->fog_color_rgb[i] > 255) { return 0; }
		if (config->sky_top_rgb[i] < 0 || config->sky_top_rgb[i] > 255) { return 0; }
		if (config->sky_bottom_rgb[i] < 0 || config->sky_bottom_rgb[i] > 255) { return 0; }
		if (config->light_color[i] < 0.0f) { return 0; }
	}

	return 1;
}

void render_config_init(render_config_t* config) {
	if (!config) { return; }

	config->width = 320;
	config->height = 240;
	config->window_width = 1280;
	config->window_height = 720;
	config->snap_grid_width = 320;
	config->snap_grid_height = 240;
	config->near_plane = 0.1f;
	config->far_plane = 100.0f;
	config->affine_strength = 1.0f;
	config->color_depth = 15;
	config->fog_enabled = 1;
	config->fog_near = 8.0f;
	config->fog_far = 25.0f;
	config->fog_color_rgb[0] = 8;
	config->fog_color_rgb[1] = 14;
	config->fog_color_rgb[2] = 28;
	config->sky_top_rgb[0] = 6;
	config->sky_top_rgb[1] = 10;
	config->sky_top_rgb[2] = 22;
	config->sky_bottom_rgb[0] = 18;
	config->sky_bottom_rgb[1] = 26;
	config->sky_bottom_rgb[2] = 44;
	config->dither_enabled = 1;
	config->dither_matrix = bayer_4x4;
	config->cull_backfaces = 1;
	config->reject_near_triangles = 1;
	config->subdivide_large_triangles = 0;
	config->subdivide_area_threshold = 4096.0f;
	config->blend_mode = NB_BLEND_NONE;
	config->shade_mode = NB_SHADE_GOURAUD;
	config->light_direction[0] = 0.4f;
	config->light_direction[1] = 0.7f;
	config->light_direction[2] = 1.0f;
	config->light_color[0] = 1.0f;
	config->light_color[1] = 1.0f;
	config->light_color[2] = 1.0f;
	config->texture_vram_limit = 1024 * 1024;
	config->wireframe_enabled = 0;
	config->xray_enabled = 0;
	config->hud_enabled = 0;
	config->hud_debug_enabled = 0;
	config->editor_enabled = 0;
	config->max_frames = 0;
	config->log_level = NB_LOG_INFO;
	config->log_file[0] = '\0';
	config->project_root[0] = '\0';
	config->scene_path[0] = '\0';
	snprintf(config->project_name, sizeof(config->project_name), "%s", "Nodebench Project");
	snprintf(config->mode, sizeof(config->mode), "%s", "game");
	config->key_binding_count = 0;
}

static int config_load_stream(render_config_t* config, FILE* file, const char* label) {
	char line[256];
	int line_number = 0;
	int in_input = 0;

	while (fgets(line, (int)sizeof(line), file)) {
		line_number++;

		char* comment = strchr(line, '#');
		if (comment) { *comment = '\0'; }

		char* t = trim(line);
		if (!*t) { continue; }
		if (t[0] == '[') {
			in_input = (strcmp(t, "[input]") == 0);
			continue;
		}

		char* equals = strchr(t, '=');
		if (!equals) { continue; }
		*equals = '\0';

		char* key = trim(t);
		char* value = trim(equals + 1);

		if (!*key || !*value) { continue; }

		if (in_input) {
			if (strlen(key) >= NB_CONFIG_BINDING_NAME_MAX || strlen(value) >= NB_CONFIG_BINDING_KEYS_MAX) {
				fprintf(stderr, "%s:%d: invalid input binding: %s\n", label, line_number, key);
				return 0;
			}
			config_add_binding(config, key, value);
			continue;
		}

		if (!config_set_value(config, key, value)) {
			fprintf(stderr, "%s:%d: invalid key or value: %s\n", label, line_number, key);
			return 0;
		}
	}

	if (!config_validate(config)) {
		fprintf(stderr, "%s: invalid configuration\n", label);
		return 0;
	}

	return 1;
}

int config_load_file(render_config_t* config, const char* path) {
	if (!config || !path) { return 0; }

	unsigned char* data = NULL;
	size_t size = 0;
	if (!nb_file_read_all(path, &data, &size)) { return 1; }

	FILE* file = fmemopen((void*)data, size, "r");
	if (!file) {
		free(data);
		return 0;
	}

	int ok = config_load_stream(config, file, path);
	fclose(file);
	free(data);

	return ok;
}

int config_load_buffer(render_config_t* config, const char* data, size_t size) {
	if (!config || !data) { return 0; }

	FILE* file = fmemopen((void*)data, size, "r");
	if (!file) { return 0; }

	int ok = config_load_stream(config, file, "<buffer>");
	fclose(file);

	return ok;
}

int config_apply_cli(render_config_t* config, int argc, char** argv) {
	if (!config) { return 0; }

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			config_print_usage(argv[0] ? argv[0] : "nodebench");
			return 2;
		}
	}

	int skip_next = 0;

	for (int i = 1; i < argc; i++) {
		if (skip_next) {
			skip_next = 0;
			continue;
		}

		if (strcmp(argv[i], "--project") == 0 || strcmp(argv[i], "--project_root") == 0 || strcmp(argv[i], "--root") == 0) {
			skip_next = 1;
			continue;
		}

		if (strncmp(argv[i], "--project=", 10) == 0 || strncmp(argv[i], "--project_root=", 15) == 0 || strncmp(argv[i], "--root=", 7) == 0) { continue; }

		if (strncmp(argv[i], "--", 2) != 0 || strlen(argv[i]) >= 384) {
			fprintf(stderr, "unexpected argument: %s\n", argv[i]);
			return 0;
		}

		const char* equals = strchr(argv[i], '=');
		if (!equals) {
			if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
				char key[128];
				char value[256];
				snprintf(key, sizeof(key), "%s", argv[i] + 2);
				snprintf(value, sizeof(value), "%s", argv[i + 1]);
				char* clean_key = trim(key);
				char* clean_value = trim(value);
				if (strncmp(clean_key, "bind_", 5) == 0) {
					const char* action = clean_key + 5;
					if (!*action) { fprintf(stderr, "invalid option: %s\n", argv[i]); return 0; }
					config_add_binding(config, action, clean_value);
					i++;
					continue;
				}
				if (!config_set_value(config, clean_key, clean_value)) {
					fprintf(stderr, "invalid option: %s\n", argv[i]);
					return 0;
				}
				i++;
				continue;
			}
			fprintf(stderr, "expected --key=value, got: %s\n", argv[i]);
			return 0;
		}

		char key[128];
		char value[256];

		size_t key_length = (size_t)(equals - (argv[i] + 2));
		if (key_length == 0 || key_length >= sizeof(key)) {
			fprintf(stderr, "invalid option name: %s\n", argv[i]);
			return 0;
		}

		memcpy(key, argv[i] + 2, key_length);
		key[key_length] = '\0';
		strncpy(value, equals + 1, sizeof(value) - 1);
		value[sizeof(value) - 1] = '\0';

		char* clean_key = trim(key);
		char* clean_value = trim(value);

		if (strncmp(clean_key, "bind_", 5) == 0) {
			const char* action = clean_key + 5;
			if (!*action) { fprintf(stderr, "invalid option: %s\n", argv[i]); return 0; }
			config_add_binding(config, action, clean_value);
			continue;
		}

		if (!config_set_value(config, clean_key, clean_value)) {
			fprintf(stderr, "invalid option: %s\n", argv[i]);
			return 0;
		}
	}

	if (!config_validate(config)) {
		fprintf(stderr, "invalid configuration after CLI overrides\n");
		return 0;
	}

	return 1;
}

void config_print_usage(const char* program) {
	if (!program) { program = "nodebench"; }

	fprintf(stdout, "usage: %s [--project=path] [--key=value]\n", program);
	fprintf(stdout, "keys:\n");
	fprintf(stdout, "  virtual_resolution=320x240 window_resolution=1280x720 vertex_snap_grid=320x240\n");
	fprintf(stdout, "  affine_strength=0..1 color_depth=15 dithering=0|1\n");
	fprintf(stdout, "  fog_enabled=0|1 fog_range=8,25 fog_color=8,14,28\n");
	fprintf(stdout, "  sky_top=6,10,22 sky_bottom=18,26,44\n");
	fprintf(stdout, "  near_plane=0.1 far_plane=100 cull_backfaces=0|1 reject_near_triangles=0|1\n");
	fprintf(stdout, "  subdivide_large_triangles=0|1 subdivide_area_threshold=4096\n");
	fprintf(stdout, "  blend_mode=none|alpha50|add|subtract|addquarter shade_mode=flat|gouraud\n");
	fprintf(stdout, "  light_direction=0.4,0.7,1.0 light_color=1,1,1 texture_vram_limit=1048576\n");
	fprintf(stdout, "  wireframe_enabled=0|1 xray_enabled=0|1\n");
	fprintf(stdout, "  hud_enabled=0|1 hud_debug_enabled=0|1 editor_enabled=0|1 max_frames=0\n");
	fprintf(stdout, "  mode=launcher|game|editor project_name=name project_root=path scene|scene_path=path\n");
	fprintf(stdout, "  log_level=debug|info|warn|error log_file=path\n");
	fprintf(stdout, "hotkeys: 1 affine, 2 dither, 3 fog, 4 cull, 5 near-clip\n");
	fprintf(stdout, "  p pause, f1 debug, f2 wireframe, f3 xray, f4 overlay, h hud\n");
	fprintf(stdout, "  tab camera mode\n");
	fprintf(stdout, "  wasd/qe move, arrows look, right mouse look, r reset, esc quit\n");
}
