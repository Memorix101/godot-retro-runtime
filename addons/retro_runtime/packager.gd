extends RefCounted
## Builds a zip of the PC game that runs after unpacking: the runtime executable and the
## packages of all game scenes.
##
##   <name>/retro(.exe)           the runtime of the system this runs on
##   <name>/data/<scene>.rpk
##   <name>/data/boot.txt        name of the first scene
##
## The runtime itself is not built here. Build it first (`make`). It is linked statically
## and needs no DLLs; if a build of another kind has put DLLs next to the executable,
## they are packed too.

const Platforms := preload("platforms.gd")

const RUNTIME_DIR := "res://build/pc"
const PACKAGE_DIR := "res://build/pkg/pc"
const OUTPUT_DIR := "res://build/dist"

## The file name of the PC runtime as `make` builds it on this system. A function, not a
## static var: the editor does not run static initializers of scripts that are no @tool.
static func runtime_exe() -> String:
	return "retro.exe" if OS.has_feature("windows") else "retro"


## Exports every scene directly inside res://scenes for the PC and writes the zip.
## Returns {"errors": PackedStringArray, "path": String, "files": PackedStringArray}; an
## empty error list means the zip at `path` is complete. `host` is any node inside the
## tree (see platforms.gd). This is a coroutine: call it with `await`.
static func package_pc(host: Node) -> Dictionary:
	var errors := PackedStringArray()
	var result := {"errors": errors, "path": "", "files": PackedStringArray()}

	if not FileAccess.file_exists(RUNTIME_DIR.path_join(runtime_exe())):
		errors.append("the PC runtime is not built: %s is missing. Run `make` in the project folder first."
				% RUNTIME_DIR.path_join(runtime_exe()))
		return result

	var scenes := Platforms.game_scenes()
	if scenes.is_empty():
		errors.append("there are no scenes in %s" % Platforms.SCENES_DIR)
		return result

	var main_scene := Platforms.main_scene()
	if not scenes.has(main_scene):
		errors.append("the main scene (Project Settings > Application > Run) must be one of the scenes in %s; it is '%s'"
				% [Platforms.SCENES_DIR, main_scene])
		return result

	# Export into the usual package directory, so a later `make run` sees the same files.
	var package_dir := ProjectSettings.globalize_path(PACKAGE_DIR)
	for scene in scenes:
		var scene_errors: PackedStringArray = await Platforms.export_scene_file(host, scene, "pc",
				package_dir.path_join(scene.get_file().get_basename() + ".rpk"))
		for message in scene_errors:
			errors.append("%s: %s" % [scene.get_file(), message])
	if not errors.is_empty():
		return result

	var game_name := String(ProjectSettings.get_setting("application/config/name", "game")).to_snake_case()
	var zip_path := ProjectSettings.globalize_path(OUTPUT_DIR).path_join(game_name + "-pc.zip")
	DirAccess.make_dir_recursive_absolute(zip_path.get_base_dir())
	var zip := ZIPPacker.new()
	var status := zip.open(zip_path)
	if status != OK:
		errors.append("cannot write '%s' (%s)" % [zip_path, error_string(status)])
		return result

	var files: PackedStringArray = result.files
	for file in DirAccess.get_files_at(RUNTIME_DIR):
		if file == runtime_exe() or file.get_extension().to_lower() in ["dll", "so"]:
			_add(zip, game_name.path_join(file), FileAccess.get_file_as_bytes(RUNTIME_DIR.path_join(file)), files)
	var data_files: PackedStringArray = []
	for scene in scenes:
		data_files.append(scene.get_file().get_basename() + ".rpk")
	# The music the scenes stream while they run.
	for file in DirAccess.get_files_at(package_dir):
		if file.get_extension().to_lower() == "wav":
			data_files.append(file)
	for file in data_files:
		var bytes: PackedByteArray = await _read(host, package_dir.path_join(file))
		if bytes.is_empty():
			# Never pack a file that could not be read: the game would only fail later.
			errors.append("cannot read '%s' (%s)" % [package_dir.path_join(file),
					error_string(FileAccess.get_open_error())])
		_add(zip, game_name.path_join("data").path_join(file), bytes, files)
	_add(zip, game_name.path_join("data/boot.txt"),
			(main_scene.get_file().get_basename() + "\n").to_utf8_buffer(), files)
	zip.close()
	if errors.is_empty() and not OS.has_feature("windows"):
		_mark_executable(zip_path, game_name.path_join(runtime_exe()), errors)

	if not errors.is_empty():
		# A zip with a file missing is worse than none.
		DirAccess.remove_absolute(zip_path)
		return result
	result.path = zip_path
	return result


## Reads a file that was written a moment ago. A virus scanner may hold such a file for
## a few seconds, and inside the editor Godot then refuses to open it, so this tries
## again for a while before it gives up and returns nothing.
static func _read(host: Node, path: String) -> PackedByteArray:
	for attempt in 50:
		var bytes := FileAccess.get_file_as_bytes(path)
		if not bytes.is_empty():
			return bytes
		await host.get_tree().create_timer(0.2).timeout
	return PackedByteArray()


static func _add(zip: ZIPPacker, path: String, data: PackedByteArray, files: PackedStringArray) -> void:
	zip.start_file(path)
	zip.write_file(data)
	zip.close_file()
	files.append(path)


## ZIPPacker writes no file modes, so unzip on Linux would unpack the runtime without its
## executable bit. This marks the entry `entry` in the central directory of the closed zip
## as made on Unix with mode 0755.
static func _mark_executable(zip_path: String, entry: String, errors: PackedStringArray) -> void:
	var bytes := FileAccess.get_file_as_bytes(zip_path)
	var name := entry.to_utf8_buffer()
	var found := false
	var i := 0
	while i + 46 <= bytes.size():
		# Central directory file header: signature PK\1\2.
		if bytes.decode_u32(i) != 0x02014b50:
			i += 1
			continue
		var name_length := bytes.decode_u16(i + 28)
		var record_length := 46 + name_length + bytes.decode_u16(i + 30) + bytes.decode_u16(i + 32)
		if bytes.slice(i + 46, i + 46 + name_length) == name:
			bytes[i + 5] = 3  # version made by: Unix
			bytes.encode_u32(i + 38, 0x81ED << 16)  # external attributes: regular file, mode 0100755
			found = true
		i += record_length
	var file := FileAccess.open(zip_path, FileAccess.WRITE) if found else null
	if file == null:
		errors.append("cannot mark '%s' in '%s' as executable" % [entry, zip_path])
		return
	file.store_buffer(bytes)
	file.close()
