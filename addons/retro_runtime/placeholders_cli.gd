extends SceneTree
## Command line entry point of the placeholder generator (see placeholders.gd):
##
##   godot --headless --path . --script res://addons/retro_runtime/placeholders_cli.gd [-- --check]
##
## Writes a placeholder script into res://components for every component of the runtime
## that the project has no class for. With --check nothing is written; the exit code is
## then 1 if a placeholder is missing or one the runtime prints is not valid GDScript.

const Placeholders := preload("placeholders.gd")


func _initialize() -> void:
	var check := OS.get_cmdline_user_args().has("--check")
	var result: Dictionary = Placeholders.generate(check)
	for message: String in result.errors:
		printerr("PLACEHOLDERS FAILED: " + message)
	for path: String in result.written:
		print("%s %s" % ["missing" if check else "wrote", path])
	print("placeholders: %d components have a script, %d %s" % [result.known, result.written.size(),
			"are missing" if check else "were written"])
	quit(1 if not result.errors.is_empty() or (check and not result.written.is_empty()) else 0)
