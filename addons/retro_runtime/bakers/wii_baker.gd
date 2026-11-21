extends "baker.gd"
## Wii and GameCube: big-endian. Vertex = position, colour as 0xRRGGBBAA, uv, which is what
## the graphics chip (GX) takes for a vertex, value by value. Textures are GX's own 16-bit
## formats, padded to power-of-two sizes and stored in blocks of 4 x 4 pixels. Sounds are
## 16-bit PCM.
##
## The screen is the project's 640 x 480.

const TEX_GX_RGB565 := 8
const TEX_GX_RGB5A3 := 9
const TEX_GX_IA8 := 10
const SOUND_PCM16 := 1
const MAX_TEXTURE_SIZE := 1024
const MIN_TEXTURE_SIZE := 8

## Godot joypad button -> PAD_BUTTON_* mask of libogc, the buttons of a GameCube pad. The
## runtime presses the same buttons for a Wii Remote and what is plugged into it.
const PAD_BUTTONS := {
	JOY_BUTTON_A: 0x0100, JOY_BUTTON_B: 0x0200, JOY_BUTTON_X: 0x0400, JOY_BUTTON_Y: 0x0800,
	JOY_BUTTON_START: 0x1000, JOY_BUTTON_BACK: 0x0010,
	JOY_BUTTON_LEFT_SHOULDER: 0x0040, JOY_BUTTON_RIGHT_SHOULDER: 0x0020,
	JOY_BUTTON_DPAD_LEFT: 0x0001, JOY_BUTTON_DPAD_RIGHT: 0x0002,
	JOY_BUTTON_DPAD_DOWN: 0x0004, JOY_BUTTON_DPAD_UP: 0x0008,
}

## Godot joypad axis -> 0 stick X, 1 stick Y, 2 left trigger, 3 right trigger.
const PAD_AXES := {
	JOY_AXIS_LEFT_X: 0, JOY_AXIS_LEFT_Y: 1, JOY_AXIS_TRIGGER_LEFT: 2, JOY_AXIS_TRIGGER_RIGHT: 3,
}


func platform_id() -> int:
	return 4


func platform_name() -> String:
	return "wii"


func big_endian() -> bool:
	return true


func vertex_size() -> int:
	return 24


func write_vertex(out: StreamPeerBuffer, position: Vector3, uv: Vector2, color: Color) -> void:
	out.put_float(position.x)
	out.put_float(position.y)
	out.put_float(position.z)
	out.put_u8(color.r8)
	out.put_u8(color.g8)
	out.put_u8(color.b8)
	out.put_u8(color.a8)
	out.put_float(uv.x)
	out.put_float(uv.y)


func texture_size(content: Vector2i) -> Vector2i:
	return Vector2i(maxi(MIN_TEXTURE_SIZE, nearest_po2(content.x)), maxi(MIN_TEXTURE_SIZE, nearest_po2(content.y)))


func bake_texture(image: Image, filter: int, errors: PackedStringArray) -> PackedByteArray:
	var content_width := image.get_width()
	var content_height := image.get_height()
	if content_width > MAX_TEXTURE_SIZE or content_height > MAX_TEXTURE_SIZE:
		errors.append("texture is %dx%d, the Wii limit is %dx%d"
				% [content_width, content_height, MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE])
		return PackedByteArray()
	var stored := texture_size(image.get_size())
	var width := stored.x
	var height := stored.y
	var source := image.get_data()

	# No alpha: RGB565. Alpha and nothing but greys (a font atlas, a soft shadow): IA8, eight
	# bits each for the grey and the alpha. Otherwise RGB5A3, where every pixel is either
	# solid with five bits a colour or has three bits of alpha and four bits a colour.
	var format := TEX_GX_RGB565
	var grey := true
	for i in range(0, source.size(), 4):
		if source[i + 3] != 255:
			format = TEX_GX_RGB5A3
		if source[i] != source[i + 1] or source[i] != source[i + 2]:
			grey = false
			if format == TEX_GX_RGB5A3:
				break
	if format == TEX_GX_RGB5A3 and grey:
		format = TEX_GX_IA8

	var out := _new_buffer()
	out.put_u16(width)
	out.put_u16(height)
	out.put_u16(content_width)
	out.put_u16(content_height)
	out.put_u8(format)
	out.put_u8(filter)
	out.put_u16(0)
	# Blocks of 4 x 4 pixels, row by row; the pixels of a block row by row too.
	for block_y in range(0, height, 4):
		for block_x in range(0, width, 4):
			for y in range(block_y, block_y + 4):
				# The padding repeats the last row and column, so filtering at the edge of
				# the image does not pick up a foreign colour.
				var row := mini(y, content_height - 1) * content_width
				for x in range(block_x, block_x + 4):
					var i := (row + mini(x, content_width - 1)) * 4
					var r := source[i]
					var g := source[i + 1]
					var b := source[i + 2]
					var a := source[i + 3]
					match format:
						TEX_GX_RGB565:
							out.put_u16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
						TEX_GX_IA8:
							out.put_u16((a << 8) | r)
						_:
							if a >= 224:
								out.put_u16(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
							else:
								out.put_u16(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
	return out.data_array


## Six faces of 256 x 256 in 16 bits are 768 KB, which a GameCube with its 24 MB still has.
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
