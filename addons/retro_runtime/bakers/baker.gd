extends RefCounted
## Base class of the per-platform bakers. A baker knows everything about a package that
## depends on the target: platform id, byte order, vertex layout, texture and sound
## formats and the codes of keys and buttons. The exporter itself is platform independent.
##
## The layouts are documented in docs/package-format.md.

const BIND_KEY := 1
const BIND_PAD_BUTTON := 2
const BIND_PAD_AXIS := 3

## Return value of the *_code functions for an input that does not exist on the platform.
const NOT_AVAILABLE := -1
## Return value for an input that exists on the platform but is missing from the table.
const UNKNOWN := -2


## Platform id as stored in the package header.
func platform_id() -> int:
	return 0


## Short name used on the command line and in file names.
func platform_name() -> String:
	return ""


func big_endian() -> bool:
	return false


## Screen size the UI rectangles are baked for.
func screen_size() -> Vector2i:
	return Vector2i(640, 480)


## Size of one baked vertex in bytes.
func vertex_size() -> int:
	return 0


## Appends one vertex to `out`, which already has the byte order of the platform.
func write_vertex(_out: StreamPeerBuffer, _position: Vector3, _uv: Vector2, _color: Color) -> void:
	pass


## Size a texture is stored with on this platform, for an image of the given size.
func texture_size(content: Vector2i) -> Vector2i:
	return content


## Returns a texture asset (header plus pixels) for an RGBA8 image, or an empty array
## with a message appended to `errors`. `filter` is 0 for linear, 1 for nearest.
func bake_texture(_image: Image, _filter: int, _errors: PackedStringArray) -> PackedByteArray:
	return PackedByteArray()


## Side of one face of the sky cube in pixels, at most. Six of them are in video memory.
func sky_face_size() -> int:
	return 512


## Sample rate of music files, which are streamed while they play.
func music_rate() -> int:
	return 44100


## Returns a sound asset (header plus samples) for 16-bit samples: one channel, or two
## with the samples of the left and the right one in turns.
func bake_sound(_samples: PackedInt32Array, _sample_rate: int, _channels: int) -> PackedByteArray:
	return PackedByteArray()


## Binding code for a Godot key, joypad button or joypad axis.
func key_code(_keycode: int) -> int:
	return NOT_AVAILABLE


func pad_button_code(_button: int) -> int:
	return NOT_AVAILABLE


func pad_axis_code(_axis: int) -> int:
	return NOT_AVAILABLE


## Bindings for a built-in ui_* action whose Godot defaults have nothing that exists on
## the platform. Each entry is [kind, code, sign].
func default_ui_bindings(_action: String) -> Array:
	return []


func _new_buffer() -> StreamPeerBuffer:
	var buffer := StreamPeerBuffer.new()
	buffer.big_endian = big_endian()
	return buffer
