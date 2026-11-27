#ifndef NB_CORE_INPUT_H
#define NB_CORE_INPUT_H

#include <stdint.h>

#include "core/platform.h"
#include "core/config.h"

#define NB_INPUT_ACTION_MAX 64
#define NB_INPUT_ACTION_NAME_MAX 32

typedef struct {
	char names[NB_INPUT_ACTION_MAX][NB_INPUT_ACTION_NAME_MAX];
	uint64_t pressed;
	uint64_t held;
	int count;
	int mouse_dx;
	int mouse_dy;
	int mouse_x;
	int mouse_y;
	int quit;
	int key_to_action[NB_PLATFORM_SCANCODE_COUNT];
} input_state_t;

void input_init(input_state_t* state, const render_config_t* config);
void input_update(input_state_t* state, platform_t* platform);
void input_clear_transient(input_state_t* state);
int input_was_pressed(const input_state_t* state, const char* action);
int input_is_held(const input_state_t* state, const char* action);
const char* input_action_at(const input_state_t* state, int index);
int input_action_count(const input_state_t* state);

#endif
