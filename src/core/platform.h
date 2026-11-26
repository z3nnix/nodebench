#ifndef NB_CORE_PLATFORM_H
#define NB_CORE_PLATFORM_H

#include <stdint.h>

typedef struct {
	const char* title;
	int width;
	int height;
	int window_scale;
	int fullscreen;
} platform_desc_t;

typedef struct {
	void* window;
	void* renderer;
	void* texture;
	uint32_t* pixels;
	int width;
	int height;
	uint32_t last_time;
} platform_t;

int platform_init(platform_t* platform, const platform_desc_t* desc);
void platform_shutdown(platform_t* platform);
void platform_pump_events(platform_t* platform, int* should_quit);
float platform_delta_time(platform_t* platform);
void platform_present(platform_t* platform);
uint32_t* platform_pixels(platform_t* platform);

#endif
