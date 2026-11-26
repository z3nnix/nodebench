import "nodebench" for Node3D, Log, Audio

class Spinner is Node3D {
	construct new(id) {
		super(id)
		_speed = 1.2
	}

	speed {
		return _speed
	}
	speed=(value) { _speed = value }

	init() {
		Log.info("script ready for node %(name)")
	}

	update(dt) {
		rotation.y = rotation.y + dt * speed
	}

	on_input(action) {
		if (action == "reset") {
			Log.info("script saw reset")
		}
	}

	on_collision(other) {
		Log.info("script collision with %(other.name)")
		Audio.play("hit")
	}
}
