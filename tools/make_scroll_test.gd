extends SceneTree
## Builds scenes/tests/scroll_test.tscn: a ScrollContainer with more buttons than fit,
## inside a PanelContainer, next to a TextureProgressBar, a separator and a GridContainer.
##
##   godot --headless --path . --script res://tools/make_scroll_test.gd
##
## The component (ScrollTest in runtime/game/test_features.c) prints how far the list is
## scrolled after the focus was moved through it. tools/scroll_reference.gd moves the
## focus the same way in Godot and prints the same line, so the two can be compared.

const OUTPUT := "res://scenes/tests/scroll_test.tscn"
const ITEMS := 9

var _root: Control


func _initialize() -> void:
	_root = Control.new()
	_root.name = "ScrollTest"
	_root.size = Vector2(640, 480)
	_root.set_script(load("res://components/tests/scroll_test.gd"))

	var back := _add(_root, ColorRect.new(), "Back") as ColorRect
	back.size = Vector2(640, 480)
	back.color = Color(0.12, 0.13, 0.18)

	# A panel with a border around the list.
	var frame := _add(_root, PanelContainer.new(), "Frame") as PanelContainer
	frame.position = Vector2(40, 40)
	frame.size = Vector2(260, 200)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.2, 0.22, 0.3)
	style.border_color = Color(0.9, 0.7, 0.2)
	style.set_border_width_all(3)
	style.set_content_margin_all(8)
	frame.add_theme_stylebox_override("panel", style)

	var list := _add(frame, ScrollContainer.new(), "List") as ScrollContainer
	list.follow_focus = true
	list.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	var column := _add(list, VBoxContainer.new(), "Column") as VBoxContainer
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for i in ITEMS:
		var item := _add(column, Button.new(), "Item%d" % (i + 1)) as Button
		item.text = "Item %d" % (i + 1)
		item.custom_minimum_size = Vector2(0, 36)

	# Next to it: a bar made of textures, a line, and a grid of labels.
	var bar := _add(_root, TextureProgressBar.new(), "Bar") as TextureProgressBar
	bar.position = Vector2(340, 50)
	bar.texture_under = load("res://assets/sprites/logo.png")
	bar.texture_progress = load("res://assets/sprites/logo.png")
	bar.tint_under = Color(0.3, 0.3, 0.3)
	bar.value = 60.0
	var line := _add(_root, HSeparator.new(), "Line") as HSeparator
	line.position = Vector2(340, 110)
	line.size = Vector2(240, 12)
	var grid := _add(_root, GridContainer.new(), "Grid") as GridContainer
	grid.position = Vector2(340, 130)
	grid.columns = 2
	for text: String in ["Name", "Score", "Ada", "1200", "Linus", "950"]:
		var cell := _add(grid, Label.new(), "Cell" + text) as Label
		cell.text = text
		cell.custom_minimum_size = Vector2(110, 0)

	# Text with tags: styles, colours and alignment, wrapped by the runtime.
	var rich := _add(_root, RichTextLabel.new(), "Rich") as RichTextLabel
	rich.position = Vector2(340, 250)
	rich.size = Vector2(260, 170)
	rich.bbcode_enabled = true
	rich.text = ("Plain, [b]bold[/b], [i]italic[/i], [color=red]red[/color] and "
			+ "[color=#40c0ff]blue [b]bold[/b][/color] text [lb]in brackets[rb] that goes on over more lines.\n"
			+ "[center]centred[/center]\n[right]right[/right]")

	_root.list = list

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
