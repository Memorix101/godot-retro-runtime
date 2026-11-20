extends SceneTree
## Command line entry point of the exporter, for use without the editor GUI:
##
##   godot --headless --path . --script res://addons/retro_runtime/export_cli.gd -- \
##       --platform pc --out build/pkg/pc res://scenes/main_menu.tscn res://scenes/test_track.tscn
##
## Every scene is written to <out>/<scene name>.rpk. Exit code 0 on success, 1 if an
## export failed (the reasons are printed to stderr).

const Platforms := preload("platforms.gd")


func _initialize() -> void:
	var platform := ""
	var out_dir := ""
	var scenes: Array[String] = []
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if args[i] == "--platform" and i + 1 < args.size():
			platform = args[i + 1]
			i += 1
		elif args[i] == "--out" and i + 1 < args.size():
			out_dir = args[i + 1]
			i += 1
		else:
			scenes.append(args[i])
		i += 1
	if platform == "" or out_dir == "" or scenes.is_empty():
		printerr("usage: -- --platform <%s> --out <directory> <res://scene.tscn>..."
				% "|".join(Platforms.names()))
		quit(2)
		return

	# The root window joins the tree with the first frame; the export needs it there.
	await process_frame

	var failed := false
	for scene in scenes:
		var out_path := out_dir.path_join(scene.get_file().get_basename() + ".rpk")
		# A package from an earlier run must not survive a failed export.
		if FileAccess.file_exists(out_path):
			DirAccess.remove_absolute(out_path)
		var errors: PackedStringArray = await Platforms.export_scene_file(root, scene, platform, out_path)
		if errors.is_empty():
			print("exported %s -> %s (%s)" % [scene, out_path, platform])
		else:
			failed = true
			printerr("EXPORT FAILED: %s" % scene)
			for message in errors:
				printerr("  " + message)
	# Let the freed viewports go before the engine shuts down.
	await process_frame
	quit(1 if failed else 0)
