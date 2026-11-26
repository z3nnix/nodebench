#ifndef NB_CORE_INPUT_H
#define NB_CORE_INPUT_H

#include <stdint.h>

#include "core/platform.h"

typedef enum {
	NB_ACTION_QUIT,
	NB_ACTION_PAUSE,
	NB_ACTION_DEBUG_CYCLE,
	NB_ACTION_DEBUG_WIREFRAME,
	NB_ACTION_DEBUG_XRAY,
	NB_ACTION_RESET,
	NB_ACTION_TOGGLE_AFFINE,
	NB_ACTION_TOGGLE_DITHER,
	NB_ACTION_TOGGLE_FOG,
	NB_ACTION_TOGGLE_CULL,
	NB_ACTION_TOGGLE_CLIP,
	NB_ACTION_CAMERA_MODE,
	NB_ACTION_CAMERA_FORWARD,
	NB_ACTION_CAMERA_BACKWARD,
	NB_ACTION_CAMERA_LEFT,
	NB_ACTION_CAMERA_RIGHT,
	NB_ACTION_CAMERA_UP,
	NB_ACTION_CAMERA_DOWN,
	NB_ACTION_CAMERA_LOOK_LEFT,
	NB_ACTION_CAMERA_LOOK_RIGHT,
	NB_ACTION_CAMERA_LOOK_UP,
	NB_ACTION_CAMERA_LOOK_DOWN,
	NB_ACTION_HUD,
	NB_ACTION_HUD_DEBUG,
	NB_ACTION_COUNT
} input_action_t;

typedef struct {
	uint32_t pressed;
	uint32_t held;
	int mouse_dx;
	int mouse_dy;
	int quit;
} input_state_t;

void input_update(input_state_t* state, platform_t* platform);
void input_clear_transient(input_state_t* state);
int input_was_pressed(const input_state_t* state, input_action_t action);
int input_is_held(const input_state_t* state, input_action_t action);

#endif
