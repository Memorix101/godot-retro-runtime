extends "baker.gd"
## Dreamcast: little-endian. Vertex = position, uv, colour as one 0xAARRGGBB word, which
## is the packed colour the PVR takes directly. Textures are 16-bit PVR pixel formats,
## padded to power-of-two sizes. Sounds are 4-bit Yamaha ADPCM for the AICA.
##
## Textures are stored row by row (not twiddled) for now.

const TEX_RGB565 := 2
const TEX_ARGB1555 := 3
const TEX_ARGB4444 := 4
const SOUND_ADPCM := 2
const MAX_TEXTURE_SIZE := 1024
const MIN_TEXTURE_SIZE := 8

## Godot joypad button -> CONT_* mask of KallistiOS.
const PAD_BUTTONS := {
	JOY_BUTTON_A: 1 << 2, JOY_BUTTON_B: 1 << 1, JOY_BUTTON_X: 1 << 10, JOY_BUTTON_Y: 1 << 9,
	JOY_BUTTON_START: 1 << 3,
	JOY_BUTTON_DPAD_UP: 1 << 4, JOY_BUTTON_DPAD_DOWN: 1 << 5,
	JOY_BUTTON_DPAD_LEFT: 1 << 6, JOY_BUTTON_DPAD_RIGHT: 1 << 7,
}

## Godot joypad axis -> 0 stick X, 1 stick Y, 2 left trigger, 3 right trigger.
const PAD_AXES := {
	JOY_AXIS_LEFT_X: 0, JOY_AXIS_LEFT_Y: 1, JOY_AXIS_TRIGGER_LEFT: 2, JOY_AXIS_TRIGGER_RIGHT: 3,
}

const ADPCM_STEP_SCALE: Array[int] = [230, 230, 230, 230, 307, 409, 512, 614]


func platform_id() -> int:
	return 2


func platform_name() -> String:
	return "dreamcast"


func vertex_size() -> int:
	return 24


func write_vertex(out: StreamPeerBuffer, position: Vector3, uv: Vector2, color: Color) -> void:
	out.put_float(position.x)
	out.put_float(position.y)
	out.put_float(position.z)
	out.put_float(uv.x)
	out.put_float(uv.y)
	out.put_u32((color.a8 << 24) | (color.r8 << 16) | (color.g8 << 8) | color.b8)


func texture_size(content: Vector2i) -> Vector2i:
	return Vector2i(maxi(MIN_TEXTURE_SIZE, nearest_po2(content.x)), maxi(MIN_TEXTURE_SIZE, nearest_po2(content.y)))


func bake_texture(image: Image, filter: int, errors: PackedStringArray) -> PackedByteArray:
	var content_width := image.get_width()
	var content_height := image.get_height()
	if content_width > MAX_TEXTURE_SIZE or content_height > MAX_TEXTURE_SIZE:
		errors.append("texture is %dx%d, the Dreamcast limit is %dx%d"
				% [content_width, content_height, MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE])
		return PackedByteArray()
	var stored := texture_size(image.get_size())
	var width := stored.x
	var height := stored.y
	var source := image.get_data()

	# No alpha: RGB565. Only fully on or off: ARGB1555. Otherwise ARGB4444.
	var format := TEX_RGB565
	for i in range(3, source.size(), 4):
		var alpha := source[i]
		if alpha != 255:
			if alpha != 0:
				format = TEX_ARGB4444
				break
			format = TEX_ARGB1555

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
				TEX_RGB565:
					out.put_u16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
				TEX_ARGB1555:
					out.put_u16(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
				_:
					out.put_u16(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
	return out.data_array


## Six faces of 256 x 256 in 16 bits are 768 KB of the 8 MB of video memory.
func sky_face_size() -> int:
	return 256


## The rate of a CD: 176 KB per second of stereo music to read from the disc, which the
## drive does at a fraction of its speed. Half of it sounded dull and harsh at once: the
## halving let through what lies above the new limit, and the sound chip's linear
## interpolation back up added mirror images of the rest.
func music_rate() -> int:
	return 44100


func bake_sound(samples: PackedInt32Array, sample_rate: int, channels: int) -> PackedByteArray:
	# One stream per channel, the left one first: that is how KallistiOS takes a sound
	# with two channels (the AICA plays each on a voice of its own).
	var frames := samples.size() / channels
	var data := PackedByteArray()
	for channel in channels:
		data.append_array(_adpcm(samples, channel, channels, frames))

	var out := _new_buffer()
	out.put_u32(sample_rate)
	out.put_u32(frames)
	out.put_u8(SOUND_ADPCM)
	out.put_u8(channels)
	out.put_u16(0)
	out.put_u32(data.size())
	out.put_data(data)
	return out.data_array


## Yamaha 4-bit ADPCM as the AICA plays it, of one channel: two samples per byte, low
## nibble first, padded to whole 32-bit words.
func _adpcm(samples: PackedInt32Array, channel: int, channels: int, frames: int) -> PackedByteArray:
	var data := PackedByteArray()
	data.resize((frames + 1) / 2)
	var predicted := 0
	var step := 127
	for i in frames:
		var delta := samples[i * channels + channel] - predicted
		var nibble := mini(7, absi(delta) * 4 / step)
		var change := step * (nibble * 2 + 1) / 8
		if delta < 0:
			nibble |= 8
			change = -change
		predicted = clampi(predicted + change, -32768, 32767)
		step = clampi(step * ADPCM_STEP_SCALE[nibble & 7] >> 8, 127, 24576)
		data[i >> 1] |= nibble << ((i & 1) * 4)
	while data.size() % 4 != 0:
		data.append(0)
	return data


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
