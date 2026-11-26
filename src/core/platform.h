#ifndef NB_CORE_PLATFORM_H
#define NB_CORE_PLATFORM_H

#include <stdint.h>

#define NB_PLATFORM_KEY_QUEUE_CAPACITY 64

typedef enum {
	NB_KEY_NONE,
	NB_KEY_ESCAPE,
	NB_KEY_R,
	NB_KEY_P,
	NB_KEY_TAB,
	NB_KEY_F1,
	NB_KEY_F2,
	NB_KEY_F3,
	NB_KEY_F4,
	NB_KEY_H,
	NB_KEY_1,
	NB_KEY_2,
	NB_KEY_3,
	NB_KEY_4,
	NB_KEY_5,
	NB_KEY_W,
	NB_KEY_A,
	NB_KEY_S,
	NB_KEY_D,
	NB_KEY_Q,
	NB_KEY_E,
	NB_KEY_UP,
	NB_KEY_DOWN,
	NB_KEY_LEFT,
	NB_KEY_RIGHT,
	NB_KEY_COUNT
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
	platform_key_t keys[NB_PLATFORM_KEY_QUEUE_CAPACITY];
	int key_count;
	unsigned char down[NB_KEY_COUNT];
	int mouse_dx;
	int mouse_dy;
} platform_t;

int platform_init(platform_t* platform, const platform_desc_t* desc);
void platform_shutdown(platform_t* platform);
void platform_pump_events(platform_t* platform, int* should_quit);
int platform_pop_key(platform_t* platform, platform_key_t* key);
int platform_key_down(const platform_t* platform, platform_key_t key);
void platform_mouse_delta(platform_t* platform, int* dx, int* dy);
float platform_delta_time(platform_t* platform);
void platform_present(platform_t* platform);
uint32_t* platform_pixels(platform_t* platform);

#endif
