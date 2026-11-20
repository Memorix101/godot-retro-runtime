extends RefCounted
## Exports the game and starts it: in the PC runtime, or in whatever the commands of a
## platform start (an emulator, a loader for the console).
##
## Godot never runs the game itself, so this is what stands in for its play buttons. The
## steps are: export the scenes for the platform into build/pkg/<platform>/, run the
## platform's build command if it has one, start its run command.
##
## The commands are in res://retro_local.cfg, a file of the machine it is on (compilers
## and emulators are somewhere else on every machine). One section per platform:
##
##   [pc]
##   build=["C:/msys64/usr/bin/env.exe", "PATH=/mingw64/bin:/usr/bin", "mingw32-make", "-C", "{project}"]
##
##   [dreamcast]
##   build=["wsl", "--cd", "{project}", "--", "bash", "-c", "... make -C runtime/platform/dreamcast cdi BOOT={scene}"]
##   run=["C:/emulators/flycast.exe", "{project}/build/dc/retro.cdi"]
##
## A command is a program followed by its arguments; no shell reads it. `{project}` stands
## for the project folder and `{scene}` for the name of the scene to start with. `build`
## may be left out. Without a file the PC runs build/pc/retro.exe as it is; every other
## platform needs a `run`.

const Platforms := preload("platforms.gd")

const CONFIG_PATH := "res://retro_local.cfg"
const PACKAGE_DIR := "res://build/pkg"
const PC_RUN: Array = ["{project}/build/pc/retro.exe", "{scene}", "--data", "{project}/build/pkg/pc"]

## How much of a failed build's output goes into the error message.
const BUILD_OUTPUT_LINES := 40

## What the last run started, so that the next one can end it first. An emulator that is
## still open holds the disc image, and two games at once are never wanted.
static var _pid := -1


## Exports and starts the game on `platform`. With a `scene_path` only that scene is
## exported and the game starts in it (the packages of the other scenes stay as they
## are); without one all game scenes are exported and the game starts in the project's
## main scene. Returns the error messages; an empty array means the game was started.
## `host` is any node inside the tree (see platforms.gd). This is a coroutine: call it
## with `await`.
static func run(host: Node, platform: String, scene_path: String = "") -> PackedStringArray:
	var errors := PackedStringArray()
	if not Platforms.names().has(platform):
		errors.append("unknown platform '%s' (known: %s)" % [platform, ", ".join(Platforms.names())])
		return errors
	var commands := _commands(platform, errors)
	if not errors.is_empty():
		return errors

	var scenes := PackedStringArray([scene_path])
	var start := scene_path
	if scene_path == "":
		scenes = Platforms.game_scenes()
		start = Platforms.main_scene()
		if not scenes.has(start):
			errors.append("the main scene (Project Settings > Application > Run) must be one of the scenes in %s; it is '%s'"
					% [Platforms.SCENES_DIR, start])
			return errors

	stop()

	var package_dir := ProjectSettings.globalize_path(PACKAGE_DIR).path_join(platform)
	for scene in scenes:
		var scene_errors: PackedStringArray = await Platforms.export_scene_file(host, scene, platform,
				package_dir.path_join(scene.get_file().get_basename() + ".rpk"))
		for message in scene_errors:
			errors.append("%s: %s" % [scene.get_file(), message])
	if not errors.is_empty():
		return errors

	var values := {
		"project": ProjectSettings.globalize_path("res://").trim_suffix("/"),
		"scene": start.get_file().get_basename(),
	}
	var build := _fill(commands.build, values)
	if not build.is_empty():
		print("Retro run: ", " ".join(build))
		var result: Dictionary = await _execute(host, build)
		if result.code != 0:
			var lines: PackedStringArray = result.output.strip_edges().split("\n")
			errors.append("the build command of '%s' failed (exit code %d): %s\n%s" % [platform,
					result.code, " ".join(build), "\n".join(lines.slice(-BUILD_OUTPUT_LINES))])
			return errors

	var command := _fill(commands.run, values)
	if command[0].is_absolute_path() and not FileAccess.file_exists(command[0]):
		errors.append("'%s' does not exist. %s" % [command[0],
				"Build the PC runtime first (`make`), or set a build command in %s." % CONFIG_PATH
				if platform == "pc" and build.is_empty() else "Check the commands in %s." % CONFIG_PATH])
		return errors
	print("Retro run: ", " ".join(command))
	_pid = OS.create_process(command[0], command.slice(1))
	if _pid == -1:
		errors.append("cannot start '%s'" % " ".join(command))
	return errors


## Ends the game the last run() started, if it is still running.
static func stop() -> void:
	if _pid != -1 and OS.is_process_running(_pid):
		OS.kill(_pid)
	_pid = -1


## The build and run command of a platform from the file of this machine.
static func _commands(platform: String, errors: PackedStringArray) -> Dictionary:
	var commands := {"build": [], "run": PC_RUN if platform == "pc" else []}
	var config := ConfigFile.new()
	if FileAccess.file_exists(CONFIG_PATH):
		var status := config.load(CONFIG_PATH)
		if status != OK:
			errors.append("cannot read %s (%s)" % [CONFIG_PATH, error_string(status)])
			return commands
	for key in commands:
		var value: Variant = config.get_value(platform, key, commands[key])
		var valid := value is Array
		if valid:
			for part in value:
				valid = valid and part is String
		if not valid:
			errors.append("%s: [%s] %s must be a list of strings: the program and its arguments"
					% [CONFIG_PATH, platform, key])
			return commands
		commands[key] = value
	if commands.run.is_empty():
		errors.append(("there is no command that starts the game on '%s'. Add one to %s:\n\n"
				+ "[%s]\nrun=[\"<program>\", \"<argument>\", ...]\n\n(see addons/retro_runtime/runner.gd)")
				% [platform, CONFIG_PATH, platform])
	return commands


static func _fill(command: Array, values: Dictionary) -> PackedStringArray:
	var filled := PackedStringArray()
	for part: String in command:
		filled.append(part.format(values))
	return filled


## Runs a command to its end without holding up the editor. Returns its exit code (-1 if
## it could not be started) and what it printed.
static func _execute(host: Node, command: PackedStringArray) -> Dictionary:
	var result := {"code": -1, "output": ""}
	var thread := Thread.new()
	thread.start(func() -> void:
		var output := []
		result.code = OS.execute(command[0], command.slice(1), output, true)
		result.output = "\n".join(output))
	while thread.is_alive():
		await host.get_tree().process_frame
	thread.wait_to_finish()
	return result
