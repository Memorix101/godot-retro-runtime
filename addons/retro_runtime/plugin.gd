@tool
extends EditorPlugin
## Adds three entries to "Project > Tools":
##
## - "Export Scene as Retro Package" exports the saved file of the scene that is open in
##   the editor, once per platform, into build/pkg/<platform>/.
## - "Package PC Build as Zip" exports all game scenes for the PC and zips them together
##   with the runtime executable into build/dist/ (see packager.gd).
## - "Generate Missing Placeholder Scripts" writes a script into res://components for
##   every C component of the built PC runtime that the project has no class for (see
##   placeholders.gd).
##
## And its own play buttons to the toolbar above the viewport, which stand in for Godot's:
## one runs the whole game, one the open scene, one stops what was started. The list next
## to them picks the platform. They export and start the game in the runtime or an
## emulator (see runner.gd).

const Platforms := preload("platforms.gd")
const Packager := preload("packager.gd")
const Runner := preload("runner.gd")
const Placeholders := preload("placeholders.gd")

const EXPORT_ITEM := "Export Scene as Retro Package"
const PACKAGE_ITEM := "Package PC Build as Zip"
const PLACEHOLDER_ITEM := "Generate Missing Placeholder Scripts"
const OUTPUT_DIR := "res://build/pkg"

## Where the editor remembers the platform that was picked, per project and user.
const METADATA_SECTION := "retro_runtime"
const METADATA_PLATFORM := "run_platform"

var _run_bar: HBoxContainer
var _platform_list: OptionButton
var _running := false


func _enter_tree() -> void:
	add_tool_menu_item(EXPORT_ITEM, _export_current_scene)
	add_tool_menu_item(PACKAGE_ITEM, _package_pc)
	add_tool_menu_item(PLACEHOLDER_ITEM, _generate_placeholders)

	_run_bar = HBoxContainer.new()
	_run_bar.add_child(VSeparator.new())
	_add_run_button(&"MainPlay", "Retro Runtime: run the game", _run.bind(false))
	_add_run_button(&"PlayScene", "Retro Runtime: run the open scene", _run.bind(true))
	_add_run_button(&"Stop", "Retro Runtime: stop the game that was started", Runner.stop)
	_platform_list = OptionButton.new()
	_platform_list.tooltip_text = "Retro Runtime: the platform the game is run on"
	_platform_list.flat = true
	var picked: String = EditorInterface.get_editor_settings().get_project_metadata(
			METADATA_SECTION, METADATA_PLATFORM, "pc")
	for platform in Platforms.names():
		_platform_list.add_item(platform)
		if platform == picked:
			_platform_list.select(_platform_list.item_count - 1)
	_platform_list.item_selected.connect(func(index: int) -> void:
		EditorInterface.get_editor_settings().set_project_metadata(
				METADATA_SECTION, METADATA_PLATFORM, _platform_list.get_item_text(index)))
	_run_bar.add_child(_platform_list)
	add_control_to_container(CONTAINER_TOOLBAR, _run_bar)


func _exit_tree() -> void:
	remove_tool_menu_item(EXPORT_ITEM)
	remove_tool_menu_item(PACKAGE_ITEM)
	remove_tool_menu_item(PLACEHOLDER_ITEM)
	remove_control_from_container(CONTAINER_TOOLBAR, _run_bar)
	_run_bar.queue_free()


func _add_run_button(icon: StringName, tooltip: String, action: Callable) -> void:
	var button := Button.new()
	button.flat = true
	button.tooltip_text = tooltip
	button.icon = EditorInterface.get_editor_theme().get_icon(icon, &"EditorIcons")
	button.pressed.connect(action)
	_run_bar.add_child(button)


func _run(current_scene: bool) -> void:
	if _running:
		return
	var platform := _platform_list.get_item_text(_platform_list.selected)
	var scene_path := ""
	if current_scene:
		var root := EditorInterface.get_edited_scene_root()
		if root == null or root.scene_file_path == "":
			_report("Nothing to run", "Open a scene and save it first.")
			return
		scene_path = root.scene_file_path
	# The export reads the scene files, so unsaved changes would be missing.
	EditorInterface.save_all_scenes()

	# No second run while one is being exported and built. Stop stays usable.
	_running = true
	for i in [1, 2]:
		(_run_bar.get_child(i) as Button).disabled = true
	var errors: PackedStringArray = await Runner.run(self, platform, scene_path)
	for i in [1, 2]:
		(_run_bar.get_child(i) as Button).disabled = false
	_running = false
	if not errors.is_empty():
		for message in errors:
			push_error("Retro run (%s): %s" % [platform, message])
		_report("Could not run the game", "\n".join(errors))


func _export_current_scene() -> void:
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.scene_file_path == "":
		_report("Nothing exported", "Open a scene and save it first.")
		return
	# The export reads the file, so unsaved changes would be missing.
	EditorInterface.save_scene()

	var scene_path := root.scene_file_path
	var lines := PackedStringArray()
	var failed := false
	for platform in Platforms.names():
		var out_path := "%s/%s/%s.rpk" % [OUTPUT_DIR, platform, scene_path.get_file().get_basename()]
		var errors: PackedStringArray = await Platforms.export_scene_file(self, scene_path, platform,
				ProjectSettings.globalize_path(out_path))
		if errors.is_empty():
			lines.append("%s: wrote %s" % [platform, out_path])
		else:
			failed = true
			lines.append("%s: FAILED" % platform)
			for message in errors:
				lines.append("    " + message)
				push_error("Retro export (%s): %s" % [platform, message])
	print("Retro export of %s\n%s" % [scene_path, "\n".join(lines)])
	_report("Export failed" if failed else "Export finished", "\n".join(lines))


func _package_pc() -> void:
	# The packager reads the scene files, so unsaved changes would be missing.
	EditorInterface.save_all_scenes()

	var result: Dictionary = await Packager.package_pc(self)
	if not result.errors.is_empty():
		for message in result.errors:
			push_error("Retro packaging: " + message)
		_report("Packaging failed", "\n".join(result.errors))
		return
	print("Retro packaging: %d files -> %s" % [result.files.size(), result.path])
	_report("Packaging finished", "Wrote %s\n\n%s" % [result.path, "\n".join(result.files)])
	OS.shell_show_in_file_manager(result.path)


func _report(title: String, text: String) -> void:
	var dialog := AcceptDialog.new()
	dialog.title = title
	dialog.dialog_text = text
	dialog.confirmed.connect(dialog.queue_free)
	dialog.canceled.connect(dialog.queue_free)
	EditorInterface.popup_dialog_centered(dialog)


func _generate_placeholders() -> void:
	var result: Dictionary = Placeholders.generate()
	if not result.errors.is_empty():
		for message in result.errors:
			push_error("Retro placeholders: " + message)
		_report("Placeholders failed", "\n".join(result.errors))
		return
	# The new scripts become classes once the editor has seen them.
	EditorInterface.get_resource_filesystem().scan()
	if result.written.is_empty():
		_report("Placeholders", "Every one of the %d components has its script already." % result.known)
	else:
		_report("Placeholders written", "\n".join(result.written))
