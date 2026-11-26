#ifndef NB_CORE_PLATFORM_H
#define NB_CORE_PLATFORM_H

#include <stdint.h>

typedef enum {
	NB_KEY_NONE,
	NB_KEY_ESCAPE,
	NB_KEY_R,
	NB_KEY_1,
	NB_KEY_2,
	NB_KEY_3,
	NB_KEY_4,
	NB_KEY_5
} platform_key_t;

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
	platform_key_t keys[16];
	int key_count;
} platform_t;

int platform_init(platform_t* platform, const platform_desc_t* desc);
void platform_shutdown(platform_t* platform);
void platform_pump_events(platform_t* platform, int* should_quit);
int platform_pop_key(platform_t* platform, platform_key_t* key);
float platform_delta_time(platform_t* platform);
void platform_present(platform_t* platform);
uint32_t* platform_pixels(platform_t* platform);

#endif
