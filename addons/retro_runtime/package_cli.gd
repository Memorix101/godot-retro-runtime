extends SceneTree
## Command line entry point of the PC packager (the same as the editor's
## "Package PC Build as Zip"):
##
##   godot --headless --path . --script res://addons/retro_runtime/package_cli.gd
##
## Exit code 0 on success, 1 on failure (the reasons are printed to stderr).

const Packager := preload("packager.gd")


func _initialize() -> void:
	# The root window joins the tree with the first frame; the export needs it there.
	await process_frame
	var result: Dictionary = await Packager.package_pc(root)
	if result.errors.is_empty():
		print("packaged %d files -> %s" % [result.files.size(), result.path])
	else:
		printerr("PACKAGING FAILED")
		for message in result.errors:
			printerr("  " + message)
	# Let the freed viewports go before the engine shuts down.
	await process_frame
	quit(0 if result.errors.is_empty() else 1)
