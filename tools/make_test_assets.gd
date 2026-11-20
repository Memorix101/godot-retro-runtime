extends SceneTree
## Generates the small placeholder images and sounds used by the test scenes, so the
## repository needs no binary art to prove the pipeline. Run it once, then let Godot
## import the files:
##
##   godot --headless --path . --script res://tools/make_test_assets.gd
##   godot --headless --path . --import


func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute("res://assets/sprites")
	DirAccess.make_dir_recursive_absolute("res://assets/sounds")

	_logo().save_png("res://assets/sprites/logo.png")
	_ship().save_png("res://assets/sprites/ship.png")
	_coin().save_png("res://assets/sprites/coin.png")
	_checker().save_png("res://assets/sprites/backdrop.png")
	_panorama().save_png("res://assets/sprites/sky_panorama.png")
	_shadow().save_png("res://assets/sprites/shadow.png")
	_ground().save_png("res://assets/sprites/ground.png")
	_tiles().save_png("res://assets/sprites/tiles.png")
	_road().save_png("res://assets/sprites/road.png")

	_tone(880.0, 0.12, 22050, false).save_to_wav("res://assets/sounds/beep.wav")
	_tone(110.0, 0.4, 11025, true).save_to_wav("res://assets/sounds/engine.wav")
	_sweep(0.5, 22050).save_to_wav("res://assets/sounds/sweep.wav")
	print("test assets written to res://assets")
	quit(0)


## 96x40, opaque: deliberately not a power of two, to exercise the texture padding.
func _logo() -> Image:
	var image := Image.create_empty(96, 40, false, Image.FORMAT_RGBA8)
	for y in 40:
		for x in 96:
			var stripe := ((x + y) / 8) % 2 == 0
			image.set_pixel(x, y, Color(0.95, 0.55, 0.1) if stripe else Color(0.75, 0.25, 0.1))
	image.fill_rect(Rect2i(4, 4, 88, 4), Color.WHITE)
	image.fill_rect(Rect2i(4, 32, 88, 4), Color.WHITE)
	return image


## 24x24 with a cut-out background (alpha is fully on or off).
func _ship() -> Image:
	var image := Image.create_empty(24, 24, false, Image.FORMAT_RGBA8)
	for y in 24:
		var half := y / 2
		for x in range(12 - half, 12 + half):
			image.set_pixel(x, y, Color(0.3, 0.9, 1.0) if y < 18 else Color(0.2, 0.5, 0.9))
	return image


## 16x16 disc with a soft edge (alpha has in-between values).
func _coin() -> Image:
	var image := Image.create_empty(16, 16, false, Image.FORMAT_RGBA8)
	for y in 16:
		for x in 16:
			var distance := Vector2(x - 7.5, y - 7.5).length()
			image.set_pixel(x, y, Color(1.0, 0.85, 0.2, clampf(8.0 - distance, 0.0, 1.0)))
	return image


## 64x64 opaque checker board.
func _checker() -> Image:
	var image := Image.create_empty(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var dark := (x / 16 + y / 16) % 2 == 0
			image.set_pixel(x, y, Color(0.12, 0.14, 0.25) if dark else Color(0.16, 0.19, 0.33))
	return image


## 512x256 panorama for a sky: blue above, hazy at the horizon, green below, a sun in the
## middle (which is -Z) and a red and a yellow post at +X and -X to tell the directions.
func _panorama() -> Image:
	var image := Image.create_empty(512, 256, false, Image.FORMAT_RGBA8)
	for y in 256:
		for x in 512:
			var height := 1.0 - y / 128.0  # 1 straight up, 0 the horizon, -1 straight down
			var color := Color(0.75, 0.85, 0.95).lerp(Color(0.15, 0.35, 0.8), pow(maxf(height, 0.0), 0.6))
			if height < 0.0:
				color = Color(0.45, 0.6, 0.4).lerp(Color(0.15, 0.3, 0.15), -height)
			# Bands of cloud that follow a wave.
			var wave := sin(x * TAU / 512.0 * 3.0) * 6.0
			if height > 0.0 and absf(y + wave - 70.0) < 5.0 and (x / 64) % 2 == 0:
				color = color.lerp(Color.WHITE, 0.8)
			var sun := Vector2(x - 256, y - 60).length()
			if sun < 12.0:
				color = Color(1.0, 0.95, 0.6)
			if height > 0.0 and height < 0.25 and absi(x - 384) < 4:
				color = Color(0.9, 0.2, 0.2)
			if height > 0.0 and height < 0.25 and absi(x - 128) < 4:
				color = Color(0.95, 0.85, 0.2)
			image.set_pixel(x, y, color)
	return image


## 32x32 soft dark blob: the shadow below a jumping character.
func _shadow() -> Image:
	var image := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	for y in 32:
		for x in 32:
			var distance := Vector2(x - 15.5, y - 15.5).length() / 15.5
			image.set_pixel(x, y, Color(0, 0, 0, clampf((1.0 - distance) * 1.6, 0.0, 0.6)))
	return image


## 32x32, opaque: a light grain that repeats without a seam. The terrain demo tints it
## with vertex colours, so it is nearly white.
func _ground() -> Image:
	var image := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	var random := RandomNumberGenerator.new()
	random.seed = 4
	for y in 32:
		for x in 32:
			# Coarse patches of 4 pixels with a fine grain on top.
			var patch := 0.06 * sin(x * TAU / 16.0) * sin(y * TAU / 16.0)
			var level := clampf(0.86 + patch + random.randf_range(-0.07, 0.07), 0.0, 1.0)
			image.set_pixel(x, y, Color(level, level, level))
	return image


## Mono 16-bit tone. `buzz` gives a sawtooth (engine), otherwise a sine with a fade-out.
func _tone(frequency: float, seconds: float, rate: int, buzz: bool) -> AudioStreamWAV:
	var count := int(seconds * rate)
	# A whole number of periods, so the engine sound loops without a click.
	if buzz:
		count = int(roundf(count * frequency / rate) * rate / frequency)
	var data := PackedByteArray()
	data.resize(count * 2)
	for i in count:
		var phase := fmod(i * frequency / rate, 1.0)
		var value := (phase * 2.0 - 1.0) * 0.35 if buzz else sin(phase * TAU) * (1.0 - float(i) / count) * 0.6
		data.encode_s16(i * 2, int(value * 32767.0))
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = rate
	stream.stereo = false
	stream.data = data
	return stream


## A stereo sound: a tone that moves from the left speaker to the right one.
func _sweep(seconds: float, rate: int) -> AudioStreamWAV:
	var count := int(seconds * rate)
	var data := PackedByteArray()
	data.resize(count * 4)
	for i in count:
		var across := float(i) / count
		var value := sin(fmod(i * 660.0 / rate, 1.0) * TAU) * 0.5
		data.encode_s16(i * 4, int(value * (1.0 - across) * 32767.0))
		data.encode_s16(i * 4 + 2, int(value * across * 32767.0))
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = rate
	stream.stereo = true
	stream.data = data
	return stream


## 64x64 floor tiles for the ball demo: two by two squares in two greys with a dark joint,
## to be tinted by the mesh. A power of two, because the floors repeat it.
func _tiles() -> Image:
	var image := Image.create_empty(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var light := (x / 32 + y / 32) % 2 == 0
			var joint := x % 32 == 0 or y % 32 == 0
			var level := 0.55 if joint else (1.0 if light else 0.8)
			image.set_pixel(x, y, Color(level, level, level))
	return image


## 64x64 road for the stunt track, across the road from left to right: a white line at
## each edge and a dashed one in the middle on grainy asphalt. It repeats along the road.
func _road() -> Image:
	var image := Image.create_empty(64, 64, false, Image.FORMAT_RGBA8)
	var random := RandomNumberGenerator.new()
	random.seed = 9
	for y in 64:
		for x in 64:
			var level := 0.36 + random.randf_range(-0.03, 0.03)
			var color := Color(level, level, level * 1.06)
			if (x >= 2 and x < 5) or (x >= 59 and x < 62):
				color = Color(0.95, 0.95, 0.95)
			elif x >= 31 and x < 33 and y < 32:
				color = Color(0.95, 0.85, 0.3)
			image.set_pixel(x, y, color)
	return image
