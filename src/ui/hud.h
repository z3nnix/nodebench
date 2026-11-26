#ifndef NB_UI_HUD_H
#define NB_UI_HUD_H

#include "core/config.h"
#include "render/framebuffer.h"

typedef struct {
	float fps;
	int draw_calls;
	int triangles;
	int culled_triangles;
	int near_culled_triangles;
	int vram_used;
} hud_t;

void hud_init(hud_t* hud);
void hud_update(hud_t* hud, float dt, int draw_calls, int triangles, int culled_triangles, int near_culled_triangles, int vram_used);
void hud_draw(hud_t* hud, framebuffer_t* framebuffer, const render_config_t* config, int paused);

#endif
