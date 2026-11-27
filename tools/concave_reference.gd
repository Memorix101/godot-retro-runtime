extends SceneTree
## Runs scenes/tests/concave_test.tscn in Godot itself, with Godot's own physics, and
## prints what the runtime's ConcaveTest component prints: where the rays hit the
## ConcavePolygonShape3D and with which normal, and where the walker and the ball are
## after eight seconds.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/concave_reference.gd

const SCENE := "res://scenes/tests/concave_test.tscn"
const RAYS: Array[String] = ["Down0", "Down1", "Down2", "Down3", "WallFront", "WallBack", "WallBackHit",
		"FenceBack", "FenceBackHit"]
const FRAMES := 480

## The walker goes from the middle of the bowl towards its wall, as in the component.
const MOVER := """
extends CharacterBody3D
var fall := 0.0
func _physics_process(delta: float) -> void:
	fall -= 9.8 * delta
	var out := Vector3(0.8660254, 0.0, -0.5) * 2.0
	velocity = Vector3(out.x, fall, out.z)
	move_and_slide()
	if is_on_floor():
		fall = 0.0
"""

## The scene's root, which hears the ball's body_entered.
## Only the first body: the ball bounces when it lands, as often as the engine makes it.
const ROOT := """
extends Node3D
var landed := false
func _on_ball_body_entered(body: Node) -> void:
	if not landed:
		landed = true
		print("concave: Ball entered %s" % body.name)
"""

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	var listener := GDScript.new()
	listener.source_code = ROOT
	listener.reload()
	_scene.set_script(listener)
	var mover := GDScript.new()
	mover.source_code = MOVER
	mover.reload()
	_scene.get_node("Walker").set_script(mover)
	root.add_child(_scene)
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	# physics_frame comes before the nodes are processed: on call n + 1, n frames are done.
	if _frame == 4:
		for path in RAYS:
			var ray := _scene.get_node(path) as RayCast3D
			ray.force_raycast_update()
			if ray.is_colliding():
				var p := ray.get_collision_point()
				var n := ray.get_collision_normal()
				print("concave: %s hits at %.2f %.2f %.2f normal %.2f %.2f %.2f" % [ray.name, p.x, p.y, p.z, n.x, n.y, n.z])
			else:
				print("concave: %s hits nothing" % ray.name)
	if _frame == FRAMES:
		var w := (_scene.get_node("Walker") as Node3D).position
		var b := (_scene.get_node("Ball") as Node3D).position
		print("concave: walker at %.1f %.1f %.1f" % [w.x, w.y, w.z])
		print("concave: ball at %.1f %.1f %.1f" % [b.x, b.y, b.z])
		quit(0)
	_frame += 1
