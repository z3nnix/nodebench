#include "resource/loader_tres.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resource/loader.h"

#define NB_TRES_MAX_EXT_RESOURCES 64
#define NB_TRES_LINE_CAPACITY 1024

typedef enum {
	TRES_SECTION_OTHER = 0,
	TRES_SECTION_EXT,
	TRES_SECTION_RESOURCE
} tres_section_t;

typedef struct {
	char id[64];
	char type[64];
	char path[256];
	image_t image;
	palette_t palette;
	int has_image;
	int has_palette;
} tres_ext_resource_t;

static char* tres_trim(char* text) {
	char* start = text;
	while (*start && isspace((unsigned char)*start)) {
		start++;
	}

	char* end = start + strlen(start);
	while (end > start && isspace((unsigned char)end[-1])) {
		end--;
	}

	*end = '\0';

	return start;
}

static void tres_parse_value(const char* value, char* out, size_t size) {
	if (!value || size == 0) {
		return;
	}

	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	out[0] = '\0';

	if (*value == '"' || *value == '\'') {
		char quote = *value++;
		size_t index = 0;
		while (*value && *value != quote && index + 1 < size) {
			out[index++] = *value++;
		}
		out[index] = '\0';

		return;
	}

	size_t index = 0;
	while (*value && !isspace((unsigned char)*value) && index + 1 < size) {
		out[index++] = *value++;
	}
	out[index] = '\0';
}

static int tres_header_value(const char* header, const char* key, char* out, size_t size) {
	size_t key_length = strlen(key);
	const char* position = header;

	while ((position = strstr(position, key)) != NULL) {
		int boundary = position == header || position[-1] == ' ' || position[-1] == '[';
		const char* after = position + key_length;

		if (boundary && *after == '=') {
			tres_parse_value(after + 1, out, size);

			return 1;
		}

		position += key_length;
	}

	out[0] = '\0';

	return 0;
}

static void tres_parse_reference(const char* value, char* out, size_t size) {
	const char* open = strchr(value, '(');
	if (!open) {
		out[0] = '\0';

		return;
	}

	tres_parse_value(open + 1, out, size);
}

static int tres_split_property(char* line, char** key, char** value) {
	char* equal = strchr(line, '=');
	if (!equal) {
		return 0;
	}

	*equal = '\0';
	*key = tres_trim(line);
	*value = tres_trim(equal + 1);

	return *key[0] && *value[0];
}

static int tres_parse_floats(const char* value, const char* prefix, float* out, int capacity) {
	while (*value && isspace((unsigned char)*value)) {
		value++;
	}

	size_t prefix_length = strlen(prefix);
	if (strncmp(value, prefix, prefix_length) != 0 || value[prefix_length] != '(') {
		return 0;
	}

	const char* position = value + prefix_length + 1;
	int count = 0;

	while (count < capacity) {
		while (*position && (isspace((unsigned char)*position) || *position == ',')) {
			position++;
		}

		if (*position == '\0' || *position == ')') {
			break;
		}

		char* end = NULL;
		float number = strtof(position, &end);
		if (end == position) {
			return 0;
		}

		out[count++] = number;
		position = end;
	}

	return count;
}

static const char* tres_resolve_path(const char* path) {
	static char buffer[512];
	FILE* file = NULL;

	if (strncmp(path, "res://", 6) == 0) {
		const char* relative = path + 6;

		snprintf(buffer, sizeof(buffer), "%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);

			return buffer;
		}

		snprintf(buffer, sizeof(buffer), "assets/%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);

			return buffer;
		}

		snprintf(buffer, sizeof(buffer), "examples/%s", relative);
		file = fopen(buffer, "r");
		if (file) {
			fclose(file);

			return buffer;
		}

		buffer[0] = '\0';

		return buffer;
	}

	snprintf(buffer, sizeof(buffer), "assets/%s", path);
	file = fopen(buffer, "r");
	if (file) {
		fclose(file);

		return buffer;
	}

	snprintf(buffer, sizeof(buffer), "examples/%s", path);
	file = fopen(buffer, "r");
	if (file) {
		fclose(file);

		return buffer;
	}

	buffer[0] = '\0';

	return buffer;
}

static nb_blend_mode_t tres_parse_blend_mode(const char* value) {
	if (strcmp(value, "alpha_50") == 0) {
		return NB_BLEND_ALPHA_50;
	}

	if (strcmp(value, "add") == 0) {
		return NB_BLEND_ADD;
	}

	if (strcmp(value, "subtract") == 0) {
		return NB_BLEND_SUBTRACT;
	}

	if (strcmp(value, "add_quarter") == 0) {
		return NB_BLEND_ADD_QUARTER;
	}

	return NB_BLEND_NONE;
}

static nb_shade_mode_t tres_parse_shade_mode(const char* value) {
	if (strcmp(value, "flat") == 0) {
		return NB_SHADE_FLAT;
	}

	return NB_SHADE_GOURAUD;
}

static int tres_parse_bool(const char* value, int fallback) {
	if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
		return 1;
	}

	if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
		return 0;
	}

	return fallback;
}

static tres_ext_resource_t* tres_find_ext(tres_ext_resource_t* resources, int count, const char* id) {
	if (!id[0]) {
		return NULL;
	}

	for (int i = 0; i < count; i++) {
		if (strcmp(resources[i].id, id) == 0) {
			return &resources[i];
		}
	}

	return NULL;
}

static int tres_load_ext_resource(tres_ext_resource_t* resource) {
	const char* path = tres_resolve_path(resource->path);
	if (!path[0]) {
		return 0;
	}

	if (strcmp(resource->type, "Texture2D") == 0) {
		if (!image_load(path, &resource->image)) {
			return 0;
		}

		resource->has_image = 1;

		return 1;
	}

	if (strcmp(resource->type, "Palette") == 0 || strcmp(resource->type, "CLUT") == 0) {
		if (!clut_load(path, &resource->palette)) {
			return 0;
		}

		resource->has_palette = 1;

		return 1;
	}

	return 1;
}

static void tres_free_ext_resources(tres_ext_resource_t* resources, int count) {
	for (int i = 0; i < count; i++) {
		if (resources[i].has_image) {
			image_free(&resources[i].image);
		}

		if (resources[i].has_palette) {
			clut_free(&resources[i].palette);
		}
	}
}

material_id material_load_tres(engine_t* engine, const char* path) {
	if (!engine || !path) {
		return NB_RESOURCE_INVALID_ID;
	}

	FILE* file = fopen(path, "r");
	if (!file) {
		return NB_RESOURCE_INVALID_ID;
	}

	tres_ext_resource_t ext_resources[NB_TRES_MAX_EXT_RESOURCES];
	int ext_count = 0;
	char texture_ref[64] = "";
	char palette_ref[64] = "";
	vec3_t tint = { 1.0f, 1.0f, 1.0f };
	nb_blend_mode_t blend_mode = engine->config.blend_mode;
	nb_shade_mode_t shade_mode = engine->config.shade_mode;
	int fog_enabled = engine->config.fog_enabled;
	int transparent_index = NB_TEXTURE_TRANSPARENT;
	material_id result = NB_RESOURCE_INVALID_ID;
	tres_section_t section = TRES_SECTION_OTHER;
	char line[NB_TRES_LINE_CAPACITY];

	memset(ext_resources, 0, sizeof(ext_resources));

	while (fgets(line, sizeof(line), file)) {
		char* text = tres_trim(line);
		char* comment = strchr(text, ';');
		if (comment) {
			*comment = '\0';
			text = tres_trim(text);
		}

		if (!text[0]) {
			continue;
		}

		if (text[0] == '[') {
			if (strncmp(text, "[ext_resource", 13) == 0) {
				section = TRES_SECTION_EXT;

				if (ext_count >= NB_TRES_MAX_EXT_RESOURCES) {
					goto cleanup;
				}

				tres_ext_resource_t* resource = &ext_resources[ext_count++];
				memset(resource, 0, sizeof(*resource));
				tres_header_value(text, "id", resource->id, sizeof(resource->id));
				tres_header_value(text, "type", resource->type, sizeof(resource->type));
				tres_header_value(text, "path", resource->path, sizeof(resource->path));

				tres_load_ext_resource(resource);
			}

			else if (strncmp(text, "[resource", 9) == 0) {
				section = TRES_SECTION_RESOURCE;
			}

			else {
				section = TRES_SECTION_OTHER;
			}

			continue;
		}

		if (section != TRES_SECTION_RESOURCE) {
			continue;
		}

		char* key = NULL;
		char* value = NULL;
		if (!tres_split_property(text, &key, &value)) {
			continue;
		}

		if (strcmp(key, "texture") == 0) {
			tres_parse_value(value, texture_ref, sizeof(texture_ref));
		}

		else if (strcmp(key, "palette") == 0) {
			tres_parse_value(value, palette_ref, sizeof(palette_ref));
		}

		else if (strcmp(key, "blend_mode") == 0) {
			char mode[32];
			tres_parse_value(value, mode, sizeof(mode));
			blend_mode = tres_parse_blend_mode(mode);
		}

		else if (strcmp(key, "shade_mode") == 0) {
			char mode[32];
			tres_parse_value(value, mode, sizeof(mode));
			shade_mode = tres_parse_shade_mode(mode);
		}

		else if (strcmp(key, "fog_enabled") == 0) {
			char enabled[16];
			tres_parse_value(value, enabled, sizeof(enabled));
			fog_enabled = tres_parse_bool(enabled, fog_enabled);
		}

		else if (strcmp(key, "vertex_color_tint") == 0) {
			float colors[4];
			if (tres_parse_floats(value, "Color", colors, 4) >= 3) {
				tint.x = colors[0];
				tint.y = colors[1];
				tint.z = colors[2];
			}
		}

		else if (strcmp(key, "transparent_index") == 0) {
			char index[32];
			tres_parse_value(value, index, sizeof(index));
			transparent_index = atoi(index);
		}
	}

	fclose(file);
	file = NULL;

	char texture_id_ref[64] = "";
	char palette_id_ref[64] = "";

	tres_parse_reference(texture_ref, texture_id_ref, sizeof(texture_id_ref));
	tres_parse_reference(palette_ref, palette_id_ref, sizeof(palette_id_ref));

	tres_ext_resource_t* image_resource = tres_find_ext(ext_resources, ext_count, texture_id_ref);
	tres_ext_resource_t* palette_resource = tres_find_ext(ext_resources, ext_count, palette_id_ref);
	uint8_t fallback_pixels[2048];
	uint16_t fallback_palette[16];
	uint8_t* pixels = NULL;
	texture_t texture;
	int use_fallback = 1;

	if (image_resource && image_resource->has_image && palette_resource && palette_resource->has_palette) {
		texture_format_t format = palette_resource->palette.count > 16 ? NB_TEXTURE_8BIT : NB_TEXTURE_4BIT;
		if (image_quantize(&image_resource->image, &palette_resource->palette, format, &pixels) && pixels) {
			texture_init(&texture, image_resource->image.width, image_resource->image.height, format, pixels, palette_resource->palette.colors, transparent_index);
			use_fallback = 0;
		}
	}

	if (use_fallback) {
		texture_generate_fallback(fallback_pixels, fallback_palette, 64, 64);
		texture_init(&texture, 64, 64, NB_TEXTURE_4BIT, fallback_pixels, fallback_palette, NB_TEXTURE_TRANSPARENT);
	}

	texture_id texture_id = resource_add_texture(&engine->resources, &texture);
	free(pixels);

	if (texture_id == NB_RESOURCE_INVALID_ID) {
		goto cleanup;
	}

	material_t material;
	material_init(&material, texture_id, blend_mode, shade_mode, fog_enabled, tint);
	result = resource_add_material(&engine->resources, &material);

cleanup:
	if (file) {
		fclose(file);
	}

	tres_free_ext_resources(ext_resources, ext_count);

	return result;
}
