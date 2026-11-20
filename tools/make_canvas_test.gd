extends SceneTree
## Builds scenes/tests/canvas_test.tscn: a Camera2D with drag margins, smoothing and
## limits that follows a moving node, Parallax2D nodes, sprites with a z_index and a HUD
## in a CanvasLayer that comes first in the tree.
##
##   godot --headless --path . --script res://tools/make_canvas_test.gd
##
## The component (CanvasTest in runtime/game/test_features.c) moves the target and prints
## where the view and the parallax nodes are. tools/canvas_reference.gd runs the same
## scene in Godot and prints the same line, so the two can be compared.

const OUTPUT := "res://scenes/tests/canvas_test.tscn"
const COIN := "res://assets/sprites/coin.png"
const SHIP := "res://assets/sprites/ship.png"
const LOGO := "res://assets/sprites/logo.png"

var _root: Node2D


func _initialize() -> void:
	_root = Node2D.new()
	_root.name = "CanvasTest"
	_root.set_script(load("res://components/tests/canvas_test.gd"))

	# The HUD is first in the tree and still has to be drawn over everything else.
	var hud := _add(_root, CanvasLayer.new(), "Hud") as CanvasLayer
	var title := _add(hud, Label.new(), "Title") as Label
	title.position = Vector2(16, 12)
	title.size = Vector2(400, 30)
	title.text = "HUD: on top, does not scroll"

	# Far away: moves at half the speed of the view.
	var far := _add(_root, Parallax2D.new(), "Far") as Parallax2D
	far.scroll_scale = Vector2(0.5, 0.5)
	far.scroll_offset = Vector2(40, 20)
	_sprite(far, "Logo", LOGO, Vector2(400, 200), 0)

	# A strip that repeats and drifts to the left by itself.
	var strip := _add(_root, Parallax2D.new(), "Strip") as Parallax2D
	strip.scroll_scale = Vector2(0.25, 1.0)
	strip.repeat_size = Vector2(160, 0)
	strip.repeat_times = 6
	strip.autoscroll = Vector2(-30, 0)
	_sprite(strip, "Coin", COIN, Vector2(40, 420), 0)

	# Scrolls only inside its limits.
	var bound := _add(_root, Parallax2D.new(), "Bound") as Parallax2D
	bound.scroll_scale = Vector2(0.8, 0.8)
	bound.limit_begin = Vector2(0, 0)
	bound.limit_end = Vector2(800, 600)
	_sprite(bound, "Ship", SHIP, Vector2(200, 300), 0)

	# Three sprites on top of each other: the first in the tree is drawn last.
	_sprite(_root, "Front", SHIP, Vector2(330, 250), 2)
	_sprite(_root, "Middle", COIN, Vector2(340, 250), 0).scale = Vector2(3, 3)
	_sprite(_root, "Back", LOGO, Vector2(340, 250), -1)

	var target := _add(_root, Node2D.new(), "Target") as Node2D
	target.position = Vector2(320, 240)
	_sprite(target, "Look", SHIP, Vector2.ZERO, 5)
	var camera := _add(target, Camera2D.new(), "Camera") as Camera2D
	camera.position_smoothing_enabled = true
	camera.position_smoothing_speed = 4.0
	camera.drag_horizontal_enabled = true
	camera.drag_left_margin = 0.3
	camera.drag_right_margin = 0.1
	camera.limit_left = 0
	camera.limit_top = 0
	camera.limit_right = 2000
	camera.limit_bottom = 520
	camera.offset = Vector2(0, -10)

	_root.target = target
	_root.far = far
	_root.strip = strip
	_root.bound = bound

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _sprite(parent: Node, sprite_name: String, texture: String, at: Vector2, z: int) -> Sprite2D:
	var sprite := _add(parent, Sprite2D.new(), sprite_name) as Sprite2D
	sprite.texture = load(texture)
	sprite.position = at
	sprite.z_index = z
	return sprite


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
