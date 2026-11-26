#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ui/hud.h"

static const unsigned char hud_font[45][5] = {
	{ 0, 0, 0, 0, 0 },
	{ 7, 5, 5, 5, 7 },
	{ 2, 6, 2, 2, 7 },
	{ 7, 1, 7, 4, 7 },
	{ 7, 1, 7, 1, 7 },
	{ 5, 5, 7, 1, 1 },
	{ 7, 4, 7, 1, 7 },
	{ 7, 4, 7, 5, 7 },
	{ 7, 1, 2, 2, 2 },
	{ 7, 5, 7, 5, 7 },
	{ 7, 5, 7, 1, 7 },
	{ 2, 5, 7, 5, 5 },
	{ 6, 5, 6, 5, 6 },
	{ 3, 4, 4, 4, 3 },
	{ 6, 5, 5, 5, 6 },
	{ 7, 4, 6, 4, 7 },
	{ 7, 4, 6, 4, 4 },
	{ 3, 4, 5, 5, 3 },
	{ 5, 5, 7, 5, 5 },
	{ 7, 2, 2, 2, 7 },
	{ 1, 1, 1, 5, 2 },
	{ 5, 5, 6, 5, 5 },
	{ 4, 4, 4, 4, 7 },
	{ 5, 7, 7, 5, 5 },
	{ 5, 7, 7, 7, 5 },
	{ 2, 5, 5, 5, 2 },
	{ 6, 5, 6, 4, 4 },
	{ 2, 5, 5, 7, 3 },
	{ 6, 5, 6, 5, 5 },
	{ 3, 4, 2, 1, 6 },
	{ 7, 2, 2, 2, 2 },
	{ 5, 5, 5, 5, 7 },
	{ 5, 5, 5, 5, 2 },
	{ 5, 5, 7, 7, 5 },
	{ 5, 5, 2, 5, 5 },
	{ 5, 5, 2, 2, 2 },
	{ 7, 1, 2, 4, 7 },
	{ 0, 7, 0, 7, 0 },
	{ 0, 0, 0, 0, 2 },
	{ 0, 2, 0, 2, 0 },
	{ 0, 0, 7, 0, 0 },
	{ 1, 1, 2, 4, 4 },
	{ 5, 1, 2, 4, 5 },
	{ 0, 2, 7, 2, 0 },
	{ 0, 0, 0, 0, 7 }
};

static int hud_glyph_index(char character) {
	unsigned char value = (unsigned char)toupper((unsigned char)character);

	if (value == ' ') { return 0; }
	if (value >= '0' && value <= '9') { return 1 + (int)(value - '0'); }
	if (value >= 'A' && value <= 'Z') { return 11 + (int)(value - 'A'); }
	if (value == '=') { return 37; }
	if (value == '.') { return 38; }
	if (value == ':') { return 39; }
	if (value == '-') { return 40; }
	if (value == '/') { return 41; }
	if (value == '%') { return 42; }
	if (value == '+') { return 43; }
	if (value == '_') { return 44; }
	return 0;
}

static void hud_fill_rect(framebuffer_t* framebuffer, int x, int y, int width, int height, float r, float g, float b) {
	for (int row = 0; row < height; row++) {
		for (int column = 0; column < width; column++) {
			framebuffer_write(framebuffer, x + column, y + row, r, g, b, NB_BLEND_NONE);
		}
	}
}

static void hud_draw_text(framebuffer_t* framebuffer, int x, int y, int scale, float r, float g, float b, const char* text) {
	if (!text) { return; }

	int cursor = x;

	for (int i = 0; text[i]; i++) {
		const unsigned char* glyph = hud_font[hud_glyph_index(text[i])];

		for (int row = 0; row < 5; row++) {
			for (int column = 0; column < 3; column++) {
				if (glyph[row] & (1u << (2u - (unsigned int)column))) { hud_fill_rect(framebuffer, cursor + column * scale, y + row * scale, scale, scale, r, g, b); }
			}
		}

		cursor += 4 * scale;
	}
}

void hud_init(hud_t* hud) {
	if (!hud) { return; }
	memset(hud, 0, sizeof(*hud));
}

void hud_update(hud_t* hud, float dt, int draw_calls, int triangles, int culled_triangles, int near_culled_triangles, int vram_used) {
	if (!hud) { return; }

	if (dt > 0.0001f) {
		float instant = 1.0f / dt;
		hud->fps = hud->fps > 0.0f ? hud->fps * 0.9f + instant * 0.1f : instant;
	}

	hud->draw_calls = draw_calls;
	hud->triangles = triangles;
	hud->culled_triangles = culled_triangles;
	hud->near_culled_triangles = near_culled_triangles;
	hud->vram_used = vram_used;
}

void hud_draw(hud_t* hud, framebuffer_t* framebuffer, const render_config_t* config, int paused) {
	if (!hud || !framebuffer || !config || !config->hud_enabled) { return; }

	char line[64];

	hud_fill_rect(framebuffer, 4, 4, 196, 58, 0, 0, 0);

	snprintf(line, sizeof(line), "FPS %5.1f", hud->fps);
	hud_draw_text(framebuffer, 6, 6, 1, 90, 220, 180, line);

	snprintf(line, sizeof(line), "DRAW %d TRI %d", hud->draw_calls, hud->triangles);
	hud_draw_text(framebuffer, 6, 12, 1, 90, 220, 180, line);

	snprintf(line, sizeof(line), "CULL %d NEAR %d VRAM %d", hud->culled_triangles, hud->near_culled_triangles, hud->vram_used);
	hud_draw_text(framebuffer, 6, 18, 1, 90, 220, 180, line);

	snprintf(line, sizeof(line), "CFG A%.2f D%d F%d C%d N%d", config->affine_strength, config->dither_enabled, config->fog_enabled, config->cull_backfaces, config->reject_near_triangles);
	hud_draw_text(framebuffer, 6, 24, 1, 90, 220, 180, line);

	snprintf(line, sizeof(line), "W%d X%d B%d S%d PAUSE%d", config->wireframe_enabled, config->xray_enabled, config->blend_mode, config->shade_mode, paused);
	hud_draw_text(framebuffer, 6, 30, 1, 90, 220, 180, line);

	snprintf(line, sizeof(line), "HUD 1 OVERLAY %d LOG %d", config->hud_debug_enabled, config->log_level);
	hud_draw_text(framebuffer, 6, 36, 1, 90, 220, 180, line);

	if (!config->hud_debug_enabled) { return; }

	hud_fill_rect(framebuffer, 4, 66, 196, 48, 0, 0, 0);

	snprintf(line, sizeof(line), "NEAR %.2f FAR %.2f", config->near_plane, config->far_plane);
	hud_draw_text(framebuffer, 6, 68, 1, 120, 200, 255, line);

	snprintf(line, sizeof(line), "FOG %.1f %.1f %s", config->fog_near, config->fog_far, config->fog_enabled ? "INC" : "EXCL");
	hud_draw_text(framebuffer, 6, 74, 1, 120, 200, 255, line);

	snprintf(line, sizeof(line), "CULL %d NEAR %d", hud->culled_triangles, hud->near_culled_triangles);
	hud_draw_text(framebuffer, 6, 80, 1, 120, 200, 255, line);

	hud_draw_text(framebuffer, 6, 86, 1, 120, 200, 255, "DITHER MAP");

	int margin = 10 + (int)(config->near_plane * 20.0f);
	int right = framebuffer->width - margin - 1;
	int bottom = framebuffer->height - margin - 1;
	framebuffer_draw_line(framebuffer, margin, margin, right, margin, 40, 160, 220, NB_BLEND_NONE);
	framebuffer_draw_line(framebuffer, right, margin, right, bottom, 40, 160, 220, NB_BLEND_NONE);
	framebuffer_draw_line(framebuffer, right, bottom, margin, bottom, 40, 160, 220, NB_BLEND_NONE);
	framebuffer_draw_line(framebuffer, margin, bottom, margin, margin, 40, 160, 220, NB_BLEND_NONE);

	int map_x = framebuffer->width - 38;
	int map_y = framebuffer->height - 38;
	hud_fill_rect(framebuffer, map_x - 2, map_y - 2, 38, 38, 0, 0, 0);

	if (config->dither_matrix) {
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				int threshold = config->dither_matrix[y * 4 + x];
				float brightness = 20.0f + (float)threshold * 14.0f;
				hud_fill_rect(framebuffer, map_x + x * 9, map_y + y * 9, 7, 7, brightness, brightness, brightness);
			}
		}
	}
}
