extends SceneTree
## Runs scenes/tests/anim_test.tscn in Godot itself: starts clip "b" while clip "a" plays
## and prints where the node is before, during and after the change, in the form the
## runtime's AnimTest component prints it.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/anim_reference.gd
##
## Each line has the node's position and the direction its Z axis points in.

const SCENE := "res://scenes/tests/anim_test.tscn"
## Frames that were processed when clip "b" is started, and when the lines are printed.
const SWITCH_FRAME := 20
const PRINT_FRAMES: Array[int] = [10, 26, 36, 70]

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	_scene.set_script(null)
	root.add_child(_scene)
	process_frame.connect(_on_frame)


func _on_frame() -> void:
	# process_frame comes before the nodes are processed: on call n + 1, n frames are done.
	var done := _frame
	_frame += 1
	var mover := _scene.get_node("Mover") as Node3D
	if PRINT_FRAMES.has(done):
		var z := mover.transform.basis.z
		print("anim: frame %d at %.2f %.2f %.2f facing %.2f %.2f %.2f" % [done, mover.position.x,
				mover.position.y, mover.position.z, z.x, z.y, z.z])
	if done == SWITCH_FRAME:
		(_scene.get_node("Anim") as AnimationPlayer).play("b")
	if done == PRINT_FRAMES[-1]:
		quit(0)
