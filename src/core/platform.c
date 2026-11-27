#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "core/platform.h"

static platform_scancode_t map_scancode(SDL_Scancode scancode) {
	if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= NB_PLATFORM_SCANCODE_COUNT) { return 0; }
	return (platform_scancode_t)scancode;
}

int platform_init(platform_t* platform, const platform_desc_t* desc) {
	if (!platform || !desc) { return 0; }

	platform->window = NULL;
	platform->renderer = NULL;
	platform->texture = NULL;
	platform->pixels = NULL;
	platform->width = desc->width;
	platform->height = desc->height;
	platform->last_time = 0;
	platform->key_count = 0;
	platform->mouse_dx = 0;
	platform->mouse_dy = 0;
	platform->mouse_x = 0;
	platform->mouse_y = 0;
	platform->mouse_captured = 0;
	platform->mouse_capture_blocked = 0;
	platform->mouse_buttons = 0;
	memset(platform->down, 0, sizeof(platform->down));

	if (platform->width <= 0 || platform->height <= 0) { return 0; }

	platform->pixels = (uint32_t*)malloc((size_t)platform->width * (size_t)platform->height * sizeof(uint32_t));
	if (!platform->pixels) { return 0; }

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { goto fail; }

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

	int window_width = desc->window_width > 0 ? desc->window_width : platform->width;
	int window_height = desc->window_height > 0 ? desc->window_height : platform->height;

	Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
	if (desc->fullscreen) { window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP; }

	const char* title = desc->title ? desc->title : "NodeBench";

	platform->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, window_flags);
	if (!platform->window) { goto fail; }

	platform->renderer = SDL_CreateRenderer((SDL_Window*)platform->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!platform->renderer) {
		platform->renderer = SDL_CreateRenderer((SDL_Window*)platform->window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!platform->renderer) { goto fail; }

	SDL_RenderSetIntegerScale((SDL_Renderer*)platform->renderer, SDL_TRUE);

	platform->texture = SDL_CreateTexture((SDL_Renderer*)platform->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, platform->width, platform->height);
	if (!platform->texture) { goto fail; }

	SDL_SetTextureBlendMode((SDL_Texture*)platform->texture, SDL_BLENDMODE_NONE);
	platform->last_time = SDL_GetTicks();
	platform_set_mouse_capture(platform, 1);

	return 1;

fail:
	platform_shutdown(platform);
	return 0;
}

void platform_shutdown(platform_t* platform) {
	if (!platform) { return; }

	SDL_SetRelativeMouseMode(SDL_FALSE);
	SDL_ShowCursor(SDL_ENABLE);
	platform->mouse_captured = 0;

	if (platform->texture) { SDL_DestroyTexture((SDL_Texture*)platform->texture); }
	if (platform->renderer) { SDL_DestroyRenderer((SDL_Renderer*)platform->renderer); }
	if (platform->window) { SDL_DestroyWindow((SDL_Window*)platform->window); }

	free(platform->pixels);

	platform->texture = NULL;
	platform->renderer = NULL;
	platform->window = NULL;
	platform->pixels = NULL;
	platform->key_count = 0;
	platform->mouse_dx = 0;
	platform->mouse_dy = 0;
	platform->mouse_x = 0;
	platform->mouse_y = 0;
	platform->mouse_capture_blocked = 0;
	memset(platform->down, 0, sizeof(platform->down));

	SDL_Quit();
}

void platform_pump_events(platform_t* platform, int* should_quit, platform_event_fn event_handler, void* event_user) {
	if (!platform || !should_quit) { return; }

	platform->mouse_dx = 0;
	platform->mouse_dy = 0;

	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event_handler) {
			event_handler(event_user, &event);
		}

		if (event.type == SDL_QUIT) { *should_quit = 1; }

		if (event.type == SDL_KEYDOWN) {
			platform_scancode_t key = map_scancode(event.key.keysym.scancode);

			if (key > 0) {
				if (!event.key.repeat && platform->key_count < NB_PLATFORM_KEY_QUEUE_CAPACITY) { platform->keys[platform->key_count++] = key; }
				platform->down[key] = 1;
			}
		}

		if (event.type == SDL_KEYUP) {
			platform_scancode_t key = map_scancode(event.key.keysym.scancode);
			if (key > 0) { platform->down[key] = 0; }
		}

		if (event.type == SDL_MOUSEMOTION) {
			platform->mouse_dx += event.motion.xrel;
			platform->mouse_dy += event.motion.yrel;
			platform->mouse_x = event.motion.x;
			platform->mouse_y = event.motion.y;
		}

		if (event.type == SDL_MOUSEBUTTONDOWN) {
			platform->mouse_buttons |= SDL_BUTTON(event.button.button);
			if (event.button.button == SDL_BUTTON_LEFT) {
				if (!platform->mouse_captured && !platform->mouse_capture_blocked) { platform_set_mouse_capture(platform, 1); }
			}
		}

		if (event.type == SDL_MOUSEBUTTONUP) {
			platform->mouse_buttons &= ~SDL_BUTTON(event.button.button);
		}

		if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
			if (platform->mouse_captured && !platform->mouse_capture_blocked) { platform_set_mouse_capture(platform, 1); }
		}
	}
}

int platform_pop_key(platform_t* platform, platform_scancode_t* key) {
	if (!platform || !key || platform->key_count <= 0) { return 0; }

	*key = platform->keys[0];

	for (int i = 1; i < platform->key_count; i++) { platform->keys[i - 1] = platform->keys[i]; }

	platform->key_count--;

	return 1;
}

int platform_key_down(const platform_t* platform, platform_scancode_t key) {
	if (!platform || key <= 0 || key >= NB_PLATFORM_SCANCODE_COUNT) { return 0; }
	return platform->down[key];
}

void platform_mouse_delta(platform_t* platform, int* dx, int* dy) {
	if (!platform) { return; }

	if (dx) { *dx = platform->mouse_dx; }
	if (dy) { *dy = platform->mouse_dy; }

	platform->mouse_dx = 0;
	platform->mouse_dy = 0;
}

void platform_set_mouse_capture(platform_t* platform, int captured) {
	if (!platform) { return; }

	if (captured && platform->mouse_capture_blocked) { return; }
	platform->mouse_captured = captured ? 1 : 0;
	if (!platform->window) { return; }

	SDL_SetRelativeMouseMode(platform->mouse_captured ? SDL_TRUE : SDL_FALSE);
	SDL_ShowCursor(platform->mouse_captured ? SDL_DISABLE : SDL_ENABLE);
}

int platform_mouse_capture(const platform_t* platform) {
	return platform && platform->mouse_captured;
}

void platform_set_mouse_capture_blocked(platform_t* platform, int blocked) {
	if (!platform) { return; }

	platform->mouse_capture_blocked = blocked ? 1 : 0;
	if (platform->mouse_capture_blocked && platform->mouse_captured) {
		platform_set_mouse_capture(platform, 0);
	}
}

float platform_delta_time(platform_t* platform) {
	if (!platform) { return 0.0f; }

	uint32_t now = SDL_GetTicks();
	float dt = (float)(now - platform->last_time) / 1000.0f;

	platform->last_time = now;

	if (dt < 0.0f) { dt = 0.0f; }
	if (dt > 0.1f) { dt = 0.1f; }

	return dt;
}

void platform_present_framebuffer(platform_t* platform) {
	if (!platform) { return; }

	int output_width = 0;
	int output_height = 0;
	platform_output_size(platform, &output_width, &output_height);
	if (output_width <= 0 || output_height <= 0) { return; }

	float scale_x = (float)output_width / (float)platform->width;
	float scale_y = (float)output_height / (float)platform->height;
	float scale = scale_x < scale_y ? scale_x : scale_y;
	int w = (int)((float)platform->width * scale);
	int h = (int)((float)platform->height * scale);
	int x = (output_width - w) / 2;
	int y = (output_height - h) / 2;
	platform_present_framebuffer_rect(platform, x, y, w, h);
}

void platform_present_framebuffer_rect(platform_t* platform, int x, int y, int w, int h) {
	if (!platform || !platform->renderer || !platform->texture || !platform->pixels) { return; }
	if (w <= 0 || h <= 0) { return; }

	SDL_Rect destination;
	destination.x = x;
	destination.y = y;
	destination.w = w;
	destination.h = h;

	SDL_UpdateTexture((SDL_Texture*)platform->texture, NULL, platform->pixels, (int)((size_t)platform->width * sizeof(uint32_t)));
	SDL_RenderCopy((SDL_Renderer*)platform->renderer, (SDL_Texture*)platform->texture, NULL, &destination);
}

void platform_present(platform_t* platform) {
	if (!platform || !platform->renderer) { return; }

	platform_present_framebuffer(platform);
	SDL_RenderPresent((SDL_Renderer*)platform->renderer);
}

uint32_t* platform_pixels(platform_t* platform) {
	if (!platform) { return NULL; }
	return platform->pixels;
}

void platform_output_size(const platform_t* platform, int* width, int* height) {
	if (width) { *width = 0; }
	if (height) { *height = 0; }
	if (!platform || !platform->renderer) { return; }
	SDL_GetRendererOutputSize((SDL_Renderer*)platform->renderer, width, height);
}

int platform_mouse_buttons(const platform_t* platform) {
	if (!platform) { return 0; }
	return (int)platform->mouse_buttons;
}

void* platform_window(const platform_t* platform) {
	if (!platform) { return NULL; }
	return platform->window;
}

void* platform_renderer(const platform_t* platform) {
	if (!platform) { return NULL; }
	return platform->renderer;
}

int platform_scancode_from_name(const char* name) {
	if (!name || !*name) { return 0; }
	SDL_Scancode sc = SDL_GetScancodeFromName(name);
	if (sc <= SDL_SCANCODE_UNKNOWN || sc >= NB_PLATFORM_SCANCODE_COUNT) { return 0; }
	return (int)sc;
}

const char* platform_scancode_name(platform_scancode_t scancode) {
	if (scancode <= 0 || scancode >= NB_PLATFORM_SCANCODE_COUNT) { return ""; }
	const char* name = SDL_GetScancodeName((SDL_Scancode)scancode);
	return name ? name : "";
}
