#include <SDL2/SDL.h>
#include <stdlib.h>

#include "core/platform.h"

static platform_key_t map_key(SDL_Keycode sym) {
	if (sym == SDLK_ESCAPE) { return NB_KEY_ESCAPE; }

	else if (sym == SDLK_r) { return NB_KEY_R; }

	else if (sym == SDLK_1) { return NB_KEY_1; }

	else if (sym == SDLK_2) { return NB_KEY_2; }

	else if (sym == SDLK_3) { return NB_KEY_3; }

	else if (sym == SDLK_4) { return NB_KEY_4; }

	else if (sym == SDLK_5) { return NB_KEY_5; }

	return NB_KEY_NONE;
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

	if (platform->width <= 0 || platform->height <= 0) { return 0; }

	platform->pixels = (uint32_t*)malloc((size_t)platform->width * (size_t)platform->height * sizeof(uint32_t));
	if (!platform->pixels) { return 0; }

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { goto fail; }

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

	int window_scale = desc->window_scale > 0 ? desc->window_scale : 1;
	int window_width = platform->width * window_scale;
	int window_height = platform->height * window_scale;

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

	return 1;

fail:
	platform_shutdown(platform);
	return 0;
}

void platform_shutdown(platform_t* platform) {
	if (!platform) { return; }

	if (platform->texture) { SDL_DestroyTexture((SDL_Texture*)platform->texture); }
	if (platform->renderer) { SDL_DestroyRenderer((SDL_Renderer*)platform->renderer); }
	if (platform->window) { SDL_DestroyWindow((SDL_Window*)platform->window); }

	free(platform->pixels);

	platform->texture = NULL;
	platform->renderer = NULL;
	platform->window = NULL;
	platform->pixels = NULL;
	platform->key_count = 0;

	SDL_Quit();
}

void platform_pump_events(platform_t* platform, int* should_quit) {
	if (!platform || !should_quit) { return; }

	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) { *should_quit = 1; }

		if (event.type == SDL_KEYDOWN) {
			platform_key_t key = map_key(event.key.keysym.sym);

			if (key != NB_KEY_NONE && platform->key_count < 16) { platform->keys[platform->key_count++] = key; }

			if (key == NB_KEY_ESCAPE) { *should_quit = 1; }
		}
	}
}

int platform_pop_key(platform_t* platform, platform_key_t* key) {
	if (!platform || !key || platform->key_count <= 0) { return 0; }

	*key = platform->keys[0];

	for (int i = 1; i < platform->key_count; i++) { platform->keys[i - 1] = platform->keys[i]; }

	platform->key_count--;

	return 1;
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

void platform_present(platform_t* platform) {
	if (!platform || !platform->renderer || !platform->texture || !platform->pixels) { return; }

	int output_width = 0;
	int output_height = 0;

	SDL_GetRendererOutputSize((SDL_Renderer*)platform->renderer, &output_width, &output_height);
	if (output_width <= 0 || output_height <= 0) { return; }

	float scale_x = (float)output_width / (float)platform->width;
	float scale_y = (float)output_height / (float)platform->height;
	float scale = scale_x < scale_y ? scale_x : scale_y;

	SDL_Rect destination;
	destination.w = (int)((float)platform->width * scale);
	destination.h = (int)((float)platform->height * scale);
	destination.x = (output_width - destination.w) / 2;
	destination.y = (output_height - destination.h) / 2;

	SDL_UpdateTexture((SDL_Texture*)platform->texture, NULL, platform->pixels, (int)((size_t)platform->width * sizeof(uint32_t)));
	SDL_RenderClear((SDL_Renderer*)platform->renderer);
	SDL_RenderCopy((SDL_Renderer*)platform->renderer, (SDL_Texture*)platform->texture, NULL, &destination);
	SDL_RenderPresent((SDL_Renderer*)platform->renderer);
}

uint32_t* platform_pixels(platform_t* platform) {
	if (!platform) { return NULL; }
	return platform->pixels;
}
