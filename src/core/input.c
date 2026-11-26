#include <string.h>

#include "core/input.h"

static const char* input_action_names[NB_ACTION_COUNT] = {
	"quit",
	"pause",
	"debug_cycle",
	"debug_wireframe",
	"debug_xray",
	"reset",
	"toggle_affine",
	"toggle_dither",
	"toggle_fog",
	"toggle_cull",
	"toggle_clip",
	"camera_mode",
	"camera_forward",
	"camera_backward",
	"camera_left",
	"camera_right",
	"camera_up",
	"camera_down",
	"camera_look_left",
	"camera_look_right",
	"camera_look_up",
	"camera_look_down",
	"hud",
	"hud_debug"
};

static uint32_t input_bit(input_action_t action) {
	if (action < 0 || action >= NB_ACTION_COUNT) { return 0; }
	return (uint32_t)1 << (uint32_t)action;
}

static input_action_t input_action_for_key(platform_key_t key) {
	if (key == NB_KEY_ESCAPE) { return NB_ACTION_QUIT; }

	else if (key == NB_KEY_P) { return NB_ACTION_PAUSE; }

	else if (key == NB_KEY_F1) { return NB_ACTION_DEBUG_CYCLE; }

	else if (key == NB_KEY_F2) { return NB_ACTION_DEBUG_WIREFRAME; }

	else if (key == NB_KEY_F3) { return NB_ACTION_DEBUG_XRAY; }

	else if (key == NB_KEY_F4) { return NB_ACTION_HUD_DEBUG; }

	else if (key == NB_KEY_H) { return NB_ACTION_HUD; }

	else if (key == NB_KEY_R) { return NB_ACTION_RESET; }

	else if (key == NB_KEY_1) { return NB_ACTION_TOGGLE_AFFINE; }

	else if (key == NB_KEY_2) { return NB_ACTION_TOGGLE_DITHER; }

	else if (key == NB_KEY_3) { return NB_ACTION_TOGGLE_FOG; }

	else if (key == NB_KEY_4) { return NB_ACTION_TOGGLE_CULL; }

	else if (key == NB_KEY_5) { return NB_ACTION_TOGGLE_CLIP; }

	else if (key == NB_KEY_TAB) { return NB_ACTION_CAMERA_MODE; }

	else if (key == NB_KEY_W) { return NB_ACTION_CAMERA_FORWARD; }

	else if (key == NB_KEY_S) { return NB_ACTION_CAMERA_BACKWARD; }

	else if (key == NB_KEY_A) { return NB_ACTION_CAMERA_LEFT; }

	else if (key == NB_KEY_D) { return NB_ACTION_CAMERA_RIGHT; }

	else if (key == NB_KEY_E) { return NB_ACTION_CAMERA_UP; }

	else if (key == NB_KEY_Q) { return NB_ACTION_CAMERA_DOWN; }

	else if (key == NB_KEY_UP) { return NB_ACTION_CAMERA_LOOK_UP; }

	else if (key == NB_KEY_DOWN) { return NB_ACTION_CAMERA_LOOK_DOWN; }

	else if (key == NB_KEY_LEFT) { return NB_ACTION_CAMERA_LOOK_LEFT; }

	else if (key == NB_KEY_RIGHT) { return NB_ACTION_CAMERA_LOOK_RIGHT; }

	return NB_ACTION_COUNT;
}

void input_update(input_state_t* state, platform_t* platform) {
	if (!state) { return; }

	state->pressed = 0;
	state->held = 0;
	state->mouse_dx = 0;
	state->mouse_dy = 0;

	if (!platform) { return; }

	platform_key_t key;
	while (platform_pop_key(platform, &key)) {
		input_action_t action = input_action_for_key(key);
		state->pressed |= input_bit(action);
	}

	for (int i = 0; i < (int)NB_KEY_COUNT; i++) {
		platform_key_t current = (platform_key_t)i;
		input_action_t action = input_action_for_key(current);
		if (action != NB_ACTION_COUNT && platform_key_down(platform, current)) { state->held |= input_bit(action); }
	}

	platform_mouse_delta(platform, &state->mouse_dx, &state->mouse_dy);
}

void input_clear_transient(input_state_t* state) {
	if (!state) { return; }
	state->pressed = 0;
	state->mouse_dx = 0;
	state->mouse_dy = 0;
}

int input_was_pressed(const input_state_t* state, input_action_t action) {
	if (!state) { return 0; }
	return (state->pressed & input_bit(action)) != 0;
}

int input_is_held(const input_state_t* state, input_action_t action) {
	if (!state) { return 0; }
	return (state->held & input_bit(action)) != 0;
}

int input_action_from_name(const char* name, input_action_t* action) {
	if (!name || !action) {
		return 0;
	}

	for (int i = 0; i < (int)NB_ACTION_COUNT; i++) {
		if (strcmp(name, input_action_names[i]) == 0) {
			*action = (input_action_t)i;
			return 1;
		}
	}

	return 0;
}

const char* input_action_name(input_action_t action) {
	if (action < 0 || action >= NB_ACTION_COUNT) {
		return "unknown";
	}

	return input_action_names[action];
}
