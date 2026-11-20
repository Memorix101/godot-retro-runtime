extends SceneTree
## Runs scenes/tests/scroll_test.tscn in Godot itself, moves the focus through the list the
## way the scripted input of `make test` does in the runtime, and prints how far the
## ScrollContainer is scrolled at the two points the runtime's ScrollTest prints it.
##
##   godot --headless --path . --script res://tools/scroll_reference.gd
##
## The focus goes down from the first item to the last, then up again by five.

const SCENE := "res://scenes/tests/scroll_test.tscn"
const ITEMS := 9
const BACK := 5


func _initialize() -> void:
	var scene: Node = (load(SCENE) as PackedScene).instantiate()
	scene.set_script(null)
	# A viewport of the size of the target's screen, as the exporter uses.
	var viewport := SubViewport.new()
	viewport.size = Vector2i(640, 480)
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	root.add_child(viewport)
	viewport.add_child(scene)
	await process_frame
	await process_frame

	var list := scene.get_node("Frame/List") as ScrollContainer
	var line := "scroll:"
	for i in range(1, ITEMS + 1):
		(list.get_node("Column/Item%d" % i) as Button).grab_focus()
		await process_frame
	line += " bottom %d" % list.scroll_vertical
	for i in range(ITEMS - 1, ITEMS - 1 - BACK, -1):
		(list.get_node("Column/Item%d" % i) as Button).grab_focus()
		await process_frame
	line += " back %d" % list.scroll_vertical
	print(line)
	quit(0)
