#ifndef NB_CORE_PLATFORM_H
#define NB_CORE_PLATFORM_H

#include <stdint.h>

#define NB_PLATFORM_KEY_QUEUE_CAPACITY 64
#define NB_PLATFORM_SCANCODE_COUNT 512

typedef int platform_scancode_t;
typedef int (*platform_event_fn)(void* user, const void* event);

typedef struct {
	const char* title;
	int width;
	int height;
	int window_width;
	int window_height;
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
	platform_scancode_t keys[NB_PLATFORM_KEY_QUEUE_CAPACITY];
	int key_count;
	unsigned char down[NB_PLATFORM_SCANCODE_COUNT];
	int mouse_dx;
	int mouse_dy;
	int mouse_x;
	int mouse_y;
	int mouse_captured;
	int mouse_capture_blocked;
	unsigned int mouse_buttons;
} platform_t;

int platform_init(platform_t* platform, const platform_desc_t* desc);
void platform_shutdown(platform_t* platform);
void platform_pump_events(platform_t* platform, int* should_quit, platform_event_fn event_handler, void* event_user);
int platform_pop_key(platform_t* platform, platform_scancode_t* key);
int platform_key_down(const platform_t* platform, platform_scancode_t key);
void platform_mouse_delta(platform_t* platform, int* dx, int* dy);
void platform_set_mouse_capture(platform_t* platform, int captured);
int platform_mouse_capture(const platform_t* platform);
void platform_set_mouse_capture_blocked(platform_t* platform, int blocked);
float platform_delta_time(platform_t* platform);
void platform_present_framebuffer(platform_t* platform);
void platform_present_framebuffer_rect(platform_t* platform, int x, int y, int w, int h);
void platform_present(platform_t* platform);
uint32_t* platform_pixels(platform_t* platform);
void platform_output_size(const platform_t* platform, int* width, int* height);
int platform_mouse_buttons(const platform_t* platform);
void* platform_window(const platform_t* platform);
void* platform_renderer(const platform_t* platform);
int platform_scancode_from_name(const char* name);
const char* platform_scancode_name(platform_scancode_t scancode);
int platform_open_directory(const char* path);

#endif
