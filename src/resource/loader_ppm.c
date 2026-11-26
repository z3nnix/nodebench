#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/color.h"
#include "resource/loader.h"

static int read_token(FILE* file, char* token, size_t size) {
	char word[1024];
	char c = 0;
	int next = 0;

	while (fscanf(file, " %c", &c) == 1) {
		if (c == '#') {
			while ((next = fgetc(file)) != EOF && next != '\n') {}
			if (next == EOF) { return 0; }
			continue;
		}

		ungetc(c, file);
		if (fscanf(file, "%1023s", word) != 1) { return 0; }
		if (strlen(word) >= size) { return 0; }

		strcpy(token, word);
		return 1;
	}

	return 0;
}

static uint8_t scale_channel(int value, int max_value) {
	if (value < 0) { value = 0; }
	if (value > max_value) { value = max_value; }
	return (uint8_t)((value * 255) / max_value);
}

static int image_load_ppm(FILE* file, image_t* image, int binary) {
	char token[64];

	if (!read_token(file, token, sizeof(token))) { return 0; }
	int width = atoi(token);
	if (!read_token(file, token, sizeof(token))) { return 0; }
	int height = atoi(token);
	if (!read_token(file, token, sizeof(token))) { return 0; }
	int max_value = atoi(token);

	if (width <= 0 || height <= 0 || max_value <= 0 || max_value > 65535) { return 0; }
	if ((size_t)width * (size_t)height > INT_MAX / 3) { return 0; }

	image->rgb = (uint8_t*)malloc((size_t)width * (size_t)height * 3);
	if (!image->rgb) { return 0; }

	if (binary) {
		if (max_value > 255) { return 0; }

		int separator = fgetc(file);
		if (separator == '\r') { separator = fgetc(file); }
		if (separator != '\n' && separator != ' ' && separator != '\t') { return 0; }

		size_t count = (size_t)width * (size_t)height * 3;
		if (fread(image->rgb, 1, count, file) != count) { return 0; }

		if (max_value != 255) {
			for (size_t i = 0; i < count; i++) { image->rgb[i] = scale_channel(image->rgb[i], max_value); }
		}
	}

	else {
		for (size_t i = 0; i < (size_t)width * (size_t)height; i++) {
			int r = 0;
			int g = 0;
			int b = 0;
			if (fscanf(file, "%d %d %d", &r, &g, &b) != 3) { return 0; }

			image->rgb[i * 3] = scale_channel(r, max_value);
			image->rgb[i * 3 + 1] = scale_channel(g, max_value);
			image->rgb[i * 3 + 2] = scale_channel(b, max_value);
		}
	}

	image->width = width;
	image->height = height;

	return 1;
}

static uint32_t bmp_u32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t bmp_i32(const uint8_t* p) {
	uint32_t value = bmp_u32(p);
	int32_t result = 0;
	memcpy(&result, &value, sizeof(result));
	return result;
}

static int image_load_bmp(FILE* file, image_t* image) {
	uint8_t header[54];
	if (fread(header, 1, sizeof(header), file) != sizeof(header)) { return 0; }
	if (header[0] != 'B' || header[1] != 'M') { return 0; }
	if (bmp_u32(header + 14) < 40) { return 0; }

	int32_t width = bmp_i32(header + 18);
	int32_t height = bmp_i32(header + 22);
	int bpp = (int)header[28] | ((int)header[29] << 8);
	uint32_t compression = bmp_u32(header + 30);

	if (width <= 0 || height == INT_MIN || height == 0 || bpp != 24 || compression != 0) { return 0; }

	int abs_height = height < 0 ? -height : height;
	size_t stride = (((size_t)width * 3 + 3) / 4) * 4;
	if ((size_t)width * (size_t)abs_height > INT_MAX / 3) { return 0; }

	if (fseek(file, (long)bmp_u32(header + 10), SEEK_SET) != 0) { return 0; }

	image->rgb = (uint8_t*)malloc((size_t)width * (size_t)abs_height * 3);
	uint8_t* row = (uint8_t*)malloc(stride);
	if (!image->rgb || !row) {
		free(image->rgb);
		free(row);
		image->rgb = NULL;
		return 0;
	}

	for (int y = 0; y < abs_height; y++) {
		if (fread(row, 1, stride, file) != stride) {
			free(image->rgb);
			free(row);
			image->rgb = NULL;
			return 0;
		}

		int source_y = height > 0 ? abs_height - 1 - y : y;
		uint8_t* destination = image->rgb + (size_t)source_y * (size_t)width * 3;

		for (int x = 0; x < width; x++) {
			destination[x * 3] = row[x * 3 + 2];
			destination[x * 3 + 1] = row[x * 3 + 1];
			destination[x * 3 + 2] = row[x * 3];
		}
	}

	free(row);
	image->width = width;
	image->height = abs_height;

	return 1;
}

int image_load(const char* path, image_t* image) {
	if (!path || !image) { return 0; }

	image->width = 0;
	image->height = 0;
	image->rgb = NULL;

	FILE* file = fopen(path, "rb");
	if (!file) { return 0; }

	uint8_t magic[2] = { 0, 0 };
	if (fread(magic, 1, 2, file) != 2) {
		fclose(file);
		return 0;
	}

	int ok = 0;

	if (magic[0] == 'P' && (magic[1] == '3' || magic[1] == '6')) {
		ok = image_load_ppm(file, image, magic[1] == '6');
	}

	else if (magic[0] == 'B' && magic[1] == 'M') {
		ok = image_load_bmp(file, image);
	}

	fclose(file);

	if (!ok) {
		image_free(image);
		return 0;
	}

	return 1;
}

void image_free(image_t* image) {
	if (!image) { return; }

	free(image->rgb);
	image->rgb = NULL;
	image->width = 0;
	image->height = 0;
}

static char* skip_spaces(char* text) {
	while (*text == ' ' || *text == '\t') { text++; }
	return text;
}

static int parse_color_line(char* line, int* color) {
	char* cursor = skip_spaces(line);
	if (*cursor == '\0' || *cursor == '#') { return 0; }

	for (int i = 0; i < 3; i++) {
		char* end = NULL;
		long value = strtol(cursor, &end, 10);
		if (end == cursor) { return 0; }

		if (value < 0) { value = 0; }
		if (value > 255) { value = 255; }
		color[i] = (int)value;
		cursor = end;
	}

	return 1;
}

int clut_load(const char* path, palette_t* palette) {
	if (!path || !palette) { return 0; }

	palette->colors = NULL;
	palette->count = 0;

	FILE* file = fopen(path, "r");
	if (!file) { return 0; }

	char line[1024];
	int count = 0;

	while (fgets(line, sizeof(line), file)) {
		char* cursor = skip_spaces(line);
		if (*cursor == '\0' || *cursor == '#') { continue; }

		char* end = NULL;
		long value = strtol(cursor, &end, 10);
		if (end == cursor) { goto fail; }
		count = (int)value;
		break;
	}

	if (count <= 0 || count > 256) { goto fail; }

	palette->colors = (uint16_t*)malloc((size_t)count * sizeof(uint16_t));
	if (!palette->colors) { goto fail; }

	for (int i = 0; i < count; i++) {
		int color[3] = { 0, 0, 0 };
		int found = 0;

		while (fgets(line, sizeof(line), file)) {
			if (parse_color_line(line, color)) {
				found = 1;
				break;
			}
		}

		if (!found) { goto fail; }
		palette->colors[i] = rgb555_pack(color[0], color[1], color[2]);
	}

	palette->count = count;
	fclose(file);

	return 1;

fail:
	clut_free(palette);
	fclose(file);
	return 0;
}

void clut_free(palette_t* palette) {
	if (!palette) { return; }

	free(palette->colors);
	palette->colors = NULL;
	palette->count = 0;
}

static int is_pot(int value) {
	return value > 0 && (value & (value - 1)) == 0;
}

int image_quantize(const image_t* image, const palette_t* palette, texture_format_t format, uint8_t** pixels) {
	if (!image || !palette || !pixels || !image->rgb || !palette->colors) { return 0; }
	if (!is_pot(image->width) || !is_pot(image->height)) { return 0; }
	if (palette->count <= 0) { return 0; }

	int bits = 0;
	int max_entries = 0;

	if (format == NB_TEXTURE_4BIT) {
		bits = 4;
		max_entries = 16;
	}

	else if (format == NB_TEXTURE_8BIT) {
		bits = 8;
		max_entries = 256;
	}

	else {
		return 0;
	}

	if (palette->count > max_entries) { return 0; }
	if (bits == 4 && image->width < 2) { return 0; }

	size_t pixel_bytes = (size_t)image->width * (size_t)image->height * (size_t)bits / 8;
	uint8_t* output = (uint8_t*)calloc(pixel_bytes, 1);
	if (!output) { return 0; }

	for (int y = 0; y < image->height; y++) {
		for (int x = 0; x < image->width; x++) {
			size_t source = ((size_t)y * (size_t)image->width + (size_t)x) * 3;
			int r = image->rgb[source];
			int g = image->rgb[source + 1];
			int b = image->rgb[source + 2];
			int best_index = 0;
			long best_distance = LONG_MAX;

			for (int i = 0; i < palette->count; i++) {
				int pr = 0;
				int pg = 0;
				int pb = 0;
				rgb555_unpack(palette->colors[i], &pr, &pg, &pb);

				long dr = r - pr;
				long dg = g - pg;
				long db = b - pb;
				long distance = dr * dr + dg * dg + db * db;

				if (distance < best_distance) {
					best_distance = distance;
					best_index = i;
				}
			}

			if (bits == 4) {
				size_t destination = (size_t)y * (size_t)(image->width / 2) + (size_t)(x / 2);
				if (x & 1) { output[destination] = (uint8_t)((output[destination] & 0xF0u) | (uint8_t)best_index); }

				else { output[destination] = (uint8_t)((output[destination] & 0x0Fu) | (uint8_t)(best_index << 4)); }
			}

			else {
				output[(size_t)y * (size_t)image->width + (size_t)x] = (uint8_t)best_index;
			}
		}
	}

	*pixels = output;

	return 1;
}
