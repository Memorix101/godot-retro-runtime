extends SceneTree
## Runs scenes/tests/rigid2d_test.tscn in Godot itself, with Godot's 2D physics, and
## prints where the bodies are after five seconds in the form the runtime's Rigid2DTest
## prints it, so the two lines can be compared. The runtime's bodies are moved by Box2D;
## what is printed is where the bodies rest, which both engines agree on to the pixel.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/rigid2d_reference.gd

const SCENE := "res://scenes/tests/rigid2d_test.tscn"
const FRAMES := 300
const PUSH_FRAME := 30

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	_scene.set_script(null)
	root.add_child(_scene)
	physics_frame.connect(_on_frame)


func _body(body_name: String) -> RigidBody2D:
	return _scene.get_node(body_name) as RigidBody2D


func _on_frame() -> void:
	if _frame == PUSH_FRAME:
		_body("Slider").apply_impulse(Vector2(-40, 0))
	if _frame == FRAMES:
		var box := _body("Box")
		print("rigid2d: box %.0f turned %.0f stack %.0f %.0f %.0f ball %.0f ghost %s riser %.0f slider %.0f tiles %.0f %.0f" % [
			box.position.y, fmod(absf(rad_to_deg(box.rotation)) + 45.0, 90.0) - 45.0,
			_body("StackA").position.y, _body("StackB").position.y, _body("StackC").position.y,
			_body("Ball").position.y, "fell" if _body("Ghost").position.y > 600.0 else "held",
			_body("Riser").position.y, _body("Slider").position.x,
			_body("TileBox").position.y, _body("TileRiser").position.y])
		quit(0)
	_frame += 1
