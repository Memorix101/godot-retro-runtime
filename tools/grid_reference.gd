extends SceneTree
## Runs scenes/tests/grid_test.tscn in Godot itself, with Godot's physics on the GridMap:
## the walker falls onto the floor, walks along +X into the wall, and the place where it
## stands after eight seconds is printed in the form the runtime's GridTest prints it.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/grid_reference.gd

const SCENE := "res://scenes/tests/grid_test.tscn"
const FRAMES := 480

const MOVER := """
extends CharacterBody3D
var fall := 0.0
func _physics_process(delta: float) -> void:
	fall -= 9.8 * delta
	velocity = Vector3(3.0, fall, 0.0)
	move_and_slide()
	if is_on_floor():
		fall = 0.0
"""

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	_scene.set_script(null)
	var mover := GDScript.new()
	mover.source_code = MOVER
	mover.reload()
	_scene.get_node("Walker").set_script(mover)
	root.add_child(_scene)
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	# physics_frame comes before the nodes are processed: on call n + 1, n frames are done.
	if _frame == FRAMES:
		var at := (_scene.get_node("Walker") as Node3D).position
		print("grid: walker at %.1f %.1f %.1f" % [at.x, at.y, at.z])
		quit(0)
	_frame += 1
