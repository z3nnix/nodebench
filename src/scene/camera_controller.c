#include <math.h>

#include "scene/camera_controller.h"

static float clampf(float value, float minimum, float maximum) {
	if (value < minimum) { return minimum; }
	if (value > maximum) { return maximum; }
	return value;
}

static float controller_axis(const input_state_t* input, input_action_t negative, input_action_t positive) {
	float result = 0.0f;
	if (input_is_held(input, negative)) { result -= 1.0f; }
	if (input_is_held(input, positive)) { result += 1.0f; }
	return result;
}

static vec3_t controller_forward(const camera_controller_t* controller) {
	float horizontal = cosf(controller->pitch);
	vec3_t forward;
	forward.x = sinf(controller->yaw) * horizontal;
	forward.y = sinf(controller->pitch);
	forward.z = -cosf(controller->yaw) * horizontal;
	return forward;
}

void camera_controller_init(camera_controller_t* controller, const camera_t* camera) {
	if (!controller || !camera) { return; }

	vec3_t delta = vec3_sub(camera->target, camera->eye);
	float distance = sqrtf(vec3_dot(delta, delta));
	vec3_t forward;

	if (distance < 0.0001f) {
		forward = (vec3_t){ 0.0f, 0.0f, -1.0f };
		distance = 1.0f;
		controller->target = camera->eye;
	}

	else {
		forward = vec3_scale(delta, 1.0f / distance);
		controller->target = camera->target;
	}

	controller->mode = NB_CAMERA_MODE_FREE;
	controller->yaw = atan2f(forward.x, -forward.z);
	controller->pitch = asinf(clampf(forward.y, -1.0f, 1.0f));
	controller->distance = distance;
	controller->move_speed = 3.0f;
	controller->look_speed = 2.5f;
	controller->mouse_sensitivity = 0.003f;
}

void camera_controller_reset(camera_controller_t* controller, const camera_t* camera) {
	if (!controller || !camera) { return; }

	camera_mode_t mode = controller->mode;
	camera_controller_init(controller, camera);
	controller->mode = mode;
}

void camera_controller_set_mode(camera_controller_t* controller, camera_mode_t mode) {
	if (!controller) { return; }
	controller->mode = mode;
}

camera_mode_t camera_controller_get_mode(const camera_controller_t* controller) {
	if (!controller) { return NB_CAMERA_MODE_FREE; }
	return controller->mode;
}

void camera_controller_update(camera_controller_t* controller, camera_t* camera, const input_state_t* input, float dt) {
	if (!controller || !camera || !input) { return; }

	if (dt < 0.0f) { dt = 0.0f; }

	float look_horizontal = controller_axis(input, NB_ACTION_CAMERA_LOOK_LEFT, NB_ACTION_CAMERA_LOOK_RIGHT);
	float look_vertical = controller_axis(input, NB_ACTION_CAMERA_LOOK_DOWN, NB_ACTION_CAMERA_LOOK_UP);

	controller->yaw += look_horizontal * controller->look_speed * dt + (float)input->mouse_dx * controller->mouse_sensitivity;
	controller->pitch += look_vertical * controller->look_speed * dt - (float)input->mouse_dy * controller->mouse_sensitivity;
	controller->pitch = clampf(controller->pitch, -1.5f, 1.5f);

	vec3_t world_up = { 0.0f, 1.0f, 0.0f };
	vec3_t forward = controller_forward(controller);
	vec3_t right = vec3_normalize(vec3_cross(forward, world_up));

	float move_forward = controller_axis(input, NB_ACTION_CAMERA_BACKWARD, NB_ACTION_CAMERA_FORWARD);
	float move_strafe = controller_axis(input, NB_ACTION_CAMERA_LEFT, NB_ACTION_CAMERA_RIGHT);
	float move_vertical = controller_axis(input, NB_ACTION_CAMERA_DOWN, NB_ACTION_CAMERA_UP);

	vec3_t move = { 0.0f, 0.0f, 0.0f };
	move = vec3_add(move, vec3_scale(forward, move_forward));
	move = vec3_add(move, vec3_scale(right, move_strafe));
	move = vec3_add(move, vec3_scale(world_up, move_vertical));

	float move_length = sqrtf(vec3_dot(move, move));
	if (move_length > 1.0f) { move = vec3_scale(move, 1.0f / move_length); }

	if (controller->mode == NB_CAMERA_MODE_ORBIT) {
		vec3_t pan;
		pan = vec3_add(vec3_scale(right, move_strafe), vec3_scale(world_up, move_vertical));
		controller->target = vec3_add(controller->target, vec3_scale(pan, controller->move_speed * dt));
		controller->distance = clampf(controller->distance - move_forward * controller->move_speed * dt, 0.5f, 50.0f);
		camera->target = controller->target;
		camera->eye = vec3_sub(controller->target, vec3_scale(forward, controller->distance));
	}

	else {
		camera->eye = vec3_add(camera->eye, vec3_scale(move, controller->move_speed * dt));
		camera->target = vec3_add(camera->eye, forward);
	}
}
