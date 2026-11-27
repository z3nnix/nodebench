import "nodebench" for Node3D, Vector3, Camera, Input, Audio, Log, World

class KulaGame is Node3D {
	construct new(id) {
		super(id)
		_player = null
		_camera = null
		_tiles = {}
		_pickups = {}
		_gx = 0
		_gz = 0
		_tx = 0
		_tz = 0
		_move_t = 1.0
		_move_dur = 0.16
		_jump_t = 1.0
		_jump_dur = 0.24
		_dir = 0
		_turn_t = 1.0
		_turn_dur = 0.12
		_turn_from = 0.0
		_turn_to = 0.0
		_score = 0
		_keys = 0
		_keys_needed = 0
		_dead = false
		_won = false
		_time = 90.0
		_ambient = false
		_cam_yaw = 0.0
		_roll_x = 0.0
		_roll_z = 0.0
	}

	key(x, z) { "%(x.floor),%(z.floor)" }

	init() {
		_player = find_node("Player")
		_camera = find_node("Camera")
		if (_camera != null) {
			Camera.use(_camera.id)
		}

		World.set_fog(6.0, 35.0, 20, 30, 55)
		World.set_sky(30, 50, 90, 80, 130, 180)
		World.set_light(0.4, 0.8, 0.5, 1.0, 0.95, 0.9)
		World.set_affine(1.0)
		World.set_dither(true)
		World.set_fog_enabled(true)
		rebuild()

		if (_player != null) {
			var p = _player.position
			_gx = (p.x + 0.5).floor
			_gz = (p.z + 0.5).floor
			_tx = _gx
			_tz = _gz
			sync_player()
		}

		_cam_yaw = yaw_for_dir(_dir)
		_turn_from = _cam_yaw
		_turn_to = _cam_yaw

		if (!_ambient) {
			Audio.loop("ambient", 0.18)
			_ambient = true
		}

		Log.info("kula: ready keys=%(_keys_needed)")
	}

	rebuild() {
		_tiles = {}
		_pickups = {}
		_keys_needed = 0
		for (child in children) {
			var n = child.name
			if (n.startsWith("T_")) {
				var p = child.position
				var gx = (p.x + 0.5).floor
				var gz = (p.z + 0.5).floor
				var kind = "tile"

				if (n.indexOf("_spike") >= 0) {
					kind = "spike"
				}

				if (n.indexOf("_exit") >= 0) {
					kind = "exit"
				}

				_tiles[key(gx, gz)] = kind
			}
			if (n.startsWith("K_") || n.startsWith("C_")) {
				if (child.visible) {
					var p = child.position
					var gx = (p.x + 0.5).floor
					var gz = (p.z + 0.5).floor

					if (n.startsWith("K_")) {
						_keys_needed = _keys_needed + 1
					}

					_pickups[key(gx, gz)] = child
				}
			}
		}
	}

	can_enter(x, z) {
		return _tiles.containsKey(key(x, z))
	}

	dir_vec(dir) {
		if (dir == 0) {
			return [0, -1]
		}

		if (dir == 1) {
			return [1, 0]
		}

		if (dir == 2) {
			return [0, 1]
		}

		return [-1, 0]
	}

	yaw_for_dir(dir) {
		return dir * 1.5707963267948966
	}

	lerp_angle(a, b, t) {
		var d = b - a
		while (d > 3.141592653589793) { d = d - 6.283185307179586 }
		while (d < -3.141592653589793) { d = d + 6.283185307179586 }
		return a + d * t
	}

	sync_player() {
		if (_player == null) {
			return
		}

		var p = _player.position
		var t = _move_t
		
		if (t > 1.0) {
			t = 1.0
		}

		p.x = _gx + (_tx - _gx) * t
		p.z = _gz + (_tz - _gz) * t
		
		if (_jump_t < 1.0) {
			var jt = _jump_t
			p.y = 0.75 + (1.0 - (jt * 2 - 1) * (jt * 2 - 1)) * 0.9
		} else {
			p.y = 0.75
		}

		_player.position = p

		var r = _player.rotation
		r.x = _roll_x
		r.z = _roll_z
		_player.rotation = r
		
		check_pickups_at(p.x, p.z)
	}

	is_busy() {
		return _move_t < 1.0 || _jump_t < 1.0 || _turn_t < 1.0 || _dead || _won
	}

	start_turn(ndir) {
		if (ndir == _dir) {
			return
		}

		_turn_from = _cam_yaw
		_dir = ndir
		_turn_to = lerp_angle(_turn_from, yaw_for_dir(_dir), 1.0)
		_turn_t = 0.0
	}

	turn_left() {
		if (is_busy()) {
			return
		}

		start_turn((_dir + 3) % 4)
	}

	turn_right() {
		if (is_busy()) {
			return
		}

		start_turn((_dir + 1) % 4)
	}

	try_move(forward_sign, jump) {
		if (is_busy()) {
			return
		}

		var v = dir_vec(_dir)
		var dx = v[0] * forward_sign
		var dz = v[1] * forward_sign
		var steps = 1
		
		if (jump) {
			steps = 2
		}

		var nx = _tx + dx * steps
		var nz = _tz + dz * steps
		if (!can_enter(nx, nz)) {
			if (jump) {
				Audio.play("jump")
			}

			return
		}

		_gx = _tx
		_gz = _tz
		_tx = nx
		_tz = nz
		_move_t = 0.0
		
		if (jump) {
			_jump_t = 0.0
			Audio.play("jump")
		} else {
			_jump_t = 1.0
		}
	}

	check_pickups_at(x, z) {
		var gx = (x + 0.5).floor
		var gz = (z + 0.5).floor
		var k = key(gx, gz)
		
		if (!_pickups.containsKey(k)) {
			return
		}
		
		var node = _pickups[k]
		if (node == null || !node.visible) {
			_pickups.remove(k)
			return
		}
		
		node.visible = false
		if (node.name.startsWith("K_")) {
			_keys = _keys + 1
			_score = _score + 1000
			Audio.play("pickup")
			Log.info("kula: key %(_keys)/%(_keys_needed)")
		} else {
			_score = _score + 250
			Audio.play("pickup")
		}
		_pickups.remove(k)
	}

	on_player_step(x, z) {
		check_pickups_at(x, z)
		var k = key(x, z)
		var kind = _tiles.containsKey(k) ? _tiles[k] : "void"
		if (kind == "spike") {
			_dead = true
			Audio.play("death")
			Log.warn("kula: died")
			return
		}
		
		if (kind == "exit") {
			if (_keys >= _keys_needed) {
				_won = true
				Audio.play("win")
				Log.info("kula: complete score=%(_score)")
			} else {
				Log.info("kula: exit locked %(_keys)/%(_keys_needed)")
			}
		}
	}

	update_camera() {
		if (_camera == null || _player == null) {
			return
		}

		var p = _player.position
		var yaw = _cam_yaw
		var dist = 3.2
		var height = 2.1
		var look_ahead = 1.4
		var fx = yaw.sin
		var fz = -yaw.cos
		var cam = _camera.position
		
		cam.x = p.x - fx * dist
		cam.y = p.y + height
		cam.z = p.z - fz * dist
		
		_camera.position = cam
		Camera.set_target(_camera.id, Vector3.new(p.x + fx * look_ahead, p.y + 0.35, p.z + fz * look_ahead))
	}

	update(dt) {
		if (_dead || _won) {
			update_camera()
			if (_won) {
				World.set_light(0.3, 0.9, 0.4, 0.8, 1.0, 0.8)
			}
			return
		}
		
		_time = _time - dt
		if (_time <= 0) {
			_time = 0
			_dead = true
			Log.warn("kula: time out")
			Audio.play("death")
			return
		}

		if (Input.was_pressed("left")) {
			turn_left()
		}
		
		if (Input.was_pressed("right")) {
			turn_right()
		}

		if (_move_t >= 1.0 && _jump_t >= 1.0 && _turn_t >= 1.0) {
			var jump = Input.is_pressed("jump")
			if (Input.is_pressed("forward")) {
				try_move(1, jump)
			} else if (Input.is_pressed("backward")) {
				try_move(-1, jump)
			}
		}

		if (_turn_t < 1.0) {
			_turn_t = _turn_t + dt / _turn_dur
			
			if (_turn_t >= 1.0) {
				_turn_t = 1.0
				_cam_yaw = yaw_for_dir(_dir)
			} else {
				_cam_yaw = lerp_angle(_turn_from, _turn_to, _turn_t)
			}
		} else {
			_cam_yaw = yaw_for_dir(_dir)
		}

		if (_move_t < 1.0) {
			_move_t = _move_t + dt / _move_dur
			var step = dt / _move_dur
			var dx = _tx - _gx
			var dz = _tz - _gz
			_roll_x = _roll_x + dz * step * 1.5707963267948966
			_roll_z = _roll_z - dx * step * 1.5707963267948966
			if (_move_t >= 1.0) {
				_move_t = 1.0
				_gx = _tx
				_gz = _tz
				on_player_step(_gx, _gz)
			}
		}

		if (_jump_t < 1.0) {
			_jump_t = _jump_t + dt / _jump_dur
			if (_jump_t >= 1.0) {
				_jump_t = 1.0
			}
		}
		
		sync_player()
		update_camera()
	}
}
