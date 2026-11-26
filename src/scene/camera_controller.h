#ifndef NB_SCENE_CAMERA_CONTROLLER_H
#define NB_SCENE_CAMERA_CONTROLLER_H

#include "core/input.h"
#include "scene/scene.h"

typedef enum {
	NB_CAMERA_MODE_FREE,
	NB_CAMERA_MODE_ORBIT
} camera_mode_t;

typedef struct {
	camera_mode_t mode;
	float yaw;
	float pitch;
	float distance;
	float move_speed;
	float look_speed;
	float mouse_sensitivity;
	vec3_t target;
} camera_controller_t;

void camera_controller_init(camera_controller_t* controller, const camera_t* camera);
void camera_controller_reset(camera_controller_t* controller, const camera_t* camera);
void camera_controller_set_mode(camera_controller_t* controller, camera_mode_t mode);
camera_mode_t camera_controller_get_mode(const camera_controller_t* controller);
void camera_controller_update(camera_controller_t* controller, camera_t* camera, const input_state_t* input, float dt);

#endif
