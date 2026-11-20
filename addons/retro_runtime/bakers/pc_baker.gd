extends "baker.gd"
## PC: little-endian. Vertex = position, uv, colour as four bytes r g b a. Textures are
## RGBA8888 at their original size, sounds 16-bit PCM. Input codes are raylib's.

const TEX_RGBA8888 := 1
const SOUND_PCM16 := 1

## Godot key -> raylib key, for keys that differ. Letters, digits and space are the same
## in both.
const KEYS := {
	KEY_ESCAPE: 256, KEY_ENTER: 257, KEY_TAB: 258, KEY_BACKSPACE: 259,
	KEY_RIGHT: 262, KEY_LEFT: 263, KEY_DOWN: 264, KEY_UP: 265,
	KEY_KP_ENTER: 335, KEY_SHIFT: 340, KEY_CTRL: 341, KEY_ALT: 342,
}

## Godot joypad button -> raylib GamepadButton.
const PAD_BUTTONS := {
	JOY_BUTTON_A: 7, JOY_BUTTON_B: 6, JOY_BUTTON_X: 8, JOY_BUTTON_Y: 5,
	JOY_BUTTON_BACK: 13, JOY_BUTTON_GUIDE: 14, JOY_BUTTON_START: 15,
	JOY_BUTTON_LEFT_STICK: 16, JOY_BUTTON_RIGHT_STICK: 17,
	JOY_BUTTON_LEFT_SHOULDER: 9, JOY_BUTTON_RIGHT_SHOULDER: 11,
	JOY_BUTTON_DPAD_UP: 1, JOY_BUTTON_DPAD_RIGHT: 2, JOY_BUTTON_DPAD_DOWN: 3, JOY_BUTTON_DPAD_LEFT: 4,
}


func platform_id() -> int:
	return 1


func platform_name() -> String:
	return "pc"


func vertex_size() -> int:
	return 24


func write_vertex(out: StreamPeerBuffer, position: Vector3, uv: Vector2, color: Color) -> void:
	out.put_float(position.x)
	out.put_float(position.y)
	out.put_float(position.z)
	out.put_float(uv.x)
	out.put_float(uv.y)
	out.put_u8(color.r8)
	out.put_u8(color.g8)
	out.put_u8(color.b8)
	out.put_u8(color.a8)


func bake_texture(image: Image, filter: int, _errors: PackedStringArray) -> PackedByteArray:
	var out := _new_buffer()
	out.put_u16(image.get_width())
	out.put_u16(image.get_height())
	out.put_u16(image.get_width())
	out.put_u16(image.get_height())
	out.put_u8(TEX_RGBA8888)
	out.put_u8(filter)
	out.put_u16(0)
	out.put_data(image.get_data())
	return out.data_array


func bake_sound(samples: PackedInt32Array, sample_rate: int, channels: int) -> PackedByteArray:
	var out := _new_buffer()
	out.put_u32(sample_rate)
	out.put_u32(samples.size() / channels)
	out.put_u8(SOUND_PCM16)
	out.put_u8(channels)
	out.put_u16(0)
	out.put_u32(samples.size() * 2)
	for sample in samples:
		out.put_16(sample)
	return out.data_array


func key_code(keycode: int) -> int:
	if KEYS.has(keycode):
		return KEYS[keycode]
	if keycode == KEY_SPACE or (keycode >= KEY_0 and keycode <= KEY_9) or (keycode >= KEY_A and keycode <= KEY_Z):
		return keycode
	return UNKNOWN


func pad_button_code(button: int) -> int:
	return PAD_BUTTONS.get(button, UNKNOWN)


func pad_axis_code(axis: int) -> int:
	# Both use 0..5 for left X/Y, right X/Y, left trigger, right trigger.
	return axis if axis >= 0 and axis <= 5 else UNKNOWN
