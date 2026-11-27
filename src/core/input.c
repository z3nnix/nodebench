#include <string.h>
#include <stdio.h>

#include "core/input.h"
#include "core/config.h"

static uint64_t input_bit(int action) {
	if (action < 0 || action >= NB_INPUT_ACTION_MAX) { return 0; }
	return (uint64_t)1 << (uint64_t)action;
}

static int input_find_action(const input_state_t* state, const char* name) {
	if (!state || !name) { return -1; }
	for (int i = 0; i < state->count; i++) {
		if (strcmp(state->names[i], name) == 0) { return i; }
	}
	return -1;
}

static int input_get_or_add_action(input_state_t* state, const char* name) {
	int idx = input_find_action(state, name);
	if (idx >= 0) { return idx; }
	if (!state || !name || !*name || state->count >= NB_INPUT_ACTION_MAX) { return -1; }
	idx = state->count++;
	snprintf(state->names[idx], sizeof(state->names[idx]), "%s", name);
	return idx;
}

void input_init(input_state_t* state, const render_config_t* config) {
	if (!state) { return; }
	state->pressed = 0;
	state->held = 0;
	state->count = 0;
	state->mouse_dx = 0;
	state->mouse_dy = 0;
	state->quit = 0;
	for (int i = 0; i < NB_PLATFORM_SCANCODE_COUNT; i++) { state->key_to_action[i] = -1; }
	for (int i = 0; i < NB_INPUT_ACTION_MAX; i++) { state->names[i][0] = '\0'; }
	if (!config) { return; }

	for (int i = 0; i < config->key_binding_count; i++) {
		const nb_key_binding_t* b = &config->key_bindings[i];
		int action = input_get_or_add_action(state, b->action);
		if (action < 0) { continue; }
		char buf[NB_CONFIG_BINDING_KEYS_MAX];
		snprintf(buf, sizeof(buf), "%s", b->keys);
		char* tok = strtok(buf, ",| ");
		while (tok) {
			int sc = platform_scancode_from_name(tok);
			if (sc > 0 && sc < NB_PLATFORM_SCANCODE_COUNT) {
				state->key_to_action[sc] = action;
			}
			tok = strtok(NULL, ",| ");
		}
	}
}

void input_update(input_state_t* state, platform_t* platform) {
	if (!state) { return; }

	state->pressed = 0;
	state->held = 0;
	state->mouse_dx = 0;
	state->mouse_dy = 0;

	if (!platform) { return; }

	platform_scancode_t key;
	while (platform_pop_key(platform, &key)) {
		if (key > 0 && key < NB_PLATFORM_SCANCODE_COUNT) {
			int action = state->key_to_action[key];
			if (action >= 0) { state->pressed |= input_bit(action); }
		}
	}

	for (int i = 1; i < NB_PLATFORM_SCANCODE_COUNT; i++) {
		int action = state->key_to_action[i];
		if (action >= 0 && platform_key_down(platform, i)) {
			state->held |= input_bit(action);
		}
	}

	platform_mouse_delta(platform, &state->mouse_dx, &state->mouse_dy);
}

void input_clear_transient(input_state_t* state) {
	if (!state) { return; }
	state->pressed = 0;
	state->mouse_dx = 0;
	state->mouse_dy = 0;
}

int input_was_pressed(const input_state_t* state, const char* action) {
	if (!state || !action) { return 0; }
	int idx = input_find_action(state, action);
	if (idx < 0) { return 0; }
	return (state->pressed & input_bit(idx)) != 0;
}

int input_is_held(const input_state_t* state, const char* action) {
	if (!state || !action) { return 0; }
	int idx = input_find_action(state, action);
	if (idx < 0) { return 0; }
	return (state->held & input_bit(idx)) != 0;
}

const char* input_action_at(const input_state_t* state, int index) {
	if (!state || index < 0 || index >= state->count) { return ""; }
	return state->names[index];
}

int input_action_count(const input_state_t* state) {
	if (!state) { return 0; }
	return state->count;
}
