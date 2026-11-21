extends "baker.gd"
## PSP: little-endian. Vertex = uv, colour, position, which is the order the PSP's graphics
## chip reads a vertex in, so meshes are drawn straight from the package. Textures are
## 16-bit with red in the low bits, padded to power-of-two sizes. Sounds are 16-bit PCM,
## mixed by the runtime.
##
## The screen has 480 x 272 pixels. The game counts in a screen of 848 x 480, which is
## the project's 480 lines with the width of the PSP's shape, and the runtime scales that
## down: Godot's stretch mode "canvas_items" with aspect "expand".

const TEX_BGR565 := 5
const TEX_ABGR1555 := 6
const TEX_ABGR4444 := 7
const SOUND_PCM16 := 1
const MAX_TEXTURE_SIZE := 512
const MIN_TEXTURE_SIZE := 8

## Godot joypad button -> PSP_CTRL_* mask of the PSP SDK. Cross is where Godot's A is.
const PAD_BUTTONS := {
	JOY_BUTTON_A: 0x4000, JOY_BUTTON_B: 0x2000, JOY_BUTTON_X: 0x8000, JOY_BUTTON_Y: 0x1000,
	JOY_BUTTON_BACK: 0x0001, JOY_BUTTON_START: 0x0008,
	JOY_BUTTON_LEFT_SHOULDER: 0x0100, JOY_BUTTON_RIGHT_SHOULDER: 0x0200,
	JOY_BUTTON_DPAD_UP: 0x0010, JOY_BUTTON_DPAD_RIGHT: 0x0020,
	JOY_BUTTON_DPAD_DOWN: 0x0040, JOY_BUTTON_DPAD_LEFT: 0x0080,
}

## Godot joypad axis -> 0 stick X, 1 stick Y, 2 L, 3 R. The PSP's shoulder buttons are
## switches: as triggers they are all the way down or not at all.
const PAD_AXES := {
	JOY_AXIS_LEFT_X: 0, JOY_AXIS_LEFT_Y: 1, JOY_AXIS_TRIGGER_LEFT: 2, JOY_AXIS_TRIGGER_RIGHT: 3,
}


func platform_id() -> int:
	return 3


func platform_name() -> String:
	return "psp"


func screen_size() -> Vector2i:
	return Vector2i(848, 480)


func vertex_size() -> int:
	return 24


func write_vertex(out: StreamPeerBuffer, position: Vector3, uv: Vector2, color: Color) -> void:
	out.put_float(uv.x)
	out.put_float(uv.y)
	out.put_u8(color.r8)
	out.put_u8(color.g8)
	out.put_u8(color.b8)
	out.put_u8(color.a8)
	out.put_float(position.x)
	out.put_float(position.y)
	out.put_float(position.z)


func texture_size(content: Vector2i) -> Vector2i:
	return Vector2i(maxi(MIN_TEXTURE_SIZE, nearest_po2(content.x)), maxi(MIN_TEXTURE_SIZE, nearest_po2(content.y)))


func bake_texture(image: Image, filter: int, errors: PackedStringArray) -> PackedByteArray:
	var content_width := image.get_width()
	var content_height := image.get_height()
	if content_width > MAX_TEXTURE_SIZE or content_height > MAX_TEXTURE_SIZE:
		errors.append("texture is %dx%d, the PSP limit is %dx%d"
				% [content_width, content_height, MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE])
		return PackedByteArray()
	var stored := texture_size(image.get_size())
	var width := stored.x
	var height := stored.y
	var source := image.get_data()

	# No alpha: BGR565. Only fully on or off: ABGR1555. Otherwise ABGR4444.
	var format := TEX_BGR565
	for i in range(3, source.size(), 4):
		var alpha := source[i]
		if alpha != 255:
			if alpha != 0:
				format = TEX_ABGR4444
				break
			format = TEX_ABGR1555

	var out := _new_buffer()
	out.put_u16(width)
	out.put_u16(height)
	out.put_u16(content_width)
	out.put_u16(content_height)
	out.put_u8(format)
	out.put_u8(filter)
	out.put_u16(0)
	for y in height:
		# The padding repeats the last row and column, so filtering at the edge of the
		# image does not pick up a foreign colour.
		var row := mini(y, content_height - 1) * content_width
		for x in width:
			var i := (row + mini(x, content_width - 1)) * 4
			var r := source[i]
			var g := source[i + 1]
			var b := source[i + 2]
			var a := source[i + 3]
			match format:
				TEX_BGR565:
					out.put_u16(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3))
				TEX_ABGR1555:
					out.put_u16(((a >> 7) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
				_:
					out.put_u16(((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4))
	return out.data_array


## Six faces of 256 x 256 in 16 bits are 768 KB.
func sky_face_size() -> int:
	return 256


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


func pad_button_code(button: int) -> int:
	return PAD_BUTTONS.get(button, NOT_AVAILABLE)


func pad_axis_code(axis: int) -> int:
	return PAD_AXES.get(axis, NOT_AVAILABLE)


func default_ui_bindings(action: String) -> Array:
	match action:
		"ui_accept":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_A], 0], [BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_START], 0]]
		"ui_cancel":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_B], 0]]
		"ui_left":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_DPAD_LEFT], 0]]
		"ui_right":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_DPAD_RIGHT], 0]]
		"ui_up":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_DPAD_UP], 0]]
		"ui_down":
			return [[BIND_PAD_BUTTON, PAD_BUTTONS[JOY_BUTTON_DPAD_DOWN], 0]]
	return []
