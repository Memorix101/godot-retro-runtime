extends SceneTree
## Command line entry point of the runner (see runner.gd), for use without the editor GUI:
##
##   godot --headless --path . --script res://addons/retro_runtime/run_cli.gd -- \
##       --platform pc [res://scenes/island.tscn]
##
## Without a scene the whole game is exported and started in the main scene. Exit code 0
## once the game was started, 1 if not (the reasons are printed to stderr). The game gets
## Godot's output as its own, so a shell that waits for that output (a pipe, Godot's
## console executable on Windows) only gets the call back when the game was closed.

const Platforms := preload("platforms.gd")
const Runner := preload("runner.gd")


func _initialize() -> void:
	var platform := ""
	var scene := ""
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if args[i] == "--platform" and i + 1 < args.size():
			platform = args[i + 1]
			i += 1
		else:
			scene = args[i]
		i += 1
	if platform == "":
		printerr("usage: -- --platform <%s> [res://scene.tscn]" % "|".join(Platforms.names()))
		quit(2)
		return

	# The root window joins the tree with the first frame; the export needs it there.
	await process_frame

	var errors: PackedStringArray = await Runner.run(root, platform, scene)
	for message in errors:
		printerr("RUN FAILED: " + message)
	# Let the freed viewports go before the engine shuts down.
	await process_frame
	quit(0 if errors.is_empty() else 1)
