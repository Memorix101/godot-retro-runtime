extends SceneTree
## Runs scenes/tests/canvas_test.tscn in Godot itself and prints where its Camera2D puts
## the view and where the Parallax2D nodes are, in the form the runtime's CanvasTest
## component prints them. `make test` compares the two lines.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/canvas_reference.gd
##
## The target moves as in the component: (240, 60) pixels per second for 90 frames, then
## it stands still; the line is printed after 150 frames.

const SCENE := "res://scenes/tests/canvas_test.tscn"
const MOVE_FRAMES := 90
const PRINT_FRAME := 150

const MOVER := """
extends Node2D
var frame := 0
func _process(delta: float) -> void:
	frame += 1
	if frame <= %d:
		position += Vector2(240, 60) * delta
"""

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	# The placeholder has no logic; what the C component does is done here instead.
	_scene.set_script(null)
	var mover := GDScript.new()
	mover.source_code = MOVER % MOVE_FRAMES
	mover.reload()
	_scene.get_node("Target").set_script(mover)
	# A viewport of the size of the target's screen, as the exporter uses: the window of a
	# headless Godot has another size, and the camera counts in screens.
	var viewport := SubViewport.new()
	viewport.size = Vector2i(640, 480)
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	root.add_child(viewport)
	viewport.add_child(_scene)
	process_frame.connect(_on_frame)


func _on_frame() -> void:
	# process_frame comes before the nodes are processed: after PRINT_FRAME frames have
	# been processed, this is call number PRINT_FRAME + 1.
	_frame += 1
	if _frame != PRINT_FRAME + 1:
		return
	var camera := _scene.get_node("Target/Camera") as Camera2D
	var center := camera.get_screen_center_position()
	var far := _scene.get_node("Far") as Node2D
	var strip := _scene.get_node("Strip") as Node2D
	var bound := _scene.get_node("Bound") as Node2D
	print("canvas: center %.1f %.1f far %.1f %.1f strip %.1f %.1f bound %.1f %.1f" % [center.x, center.y,
			far.position.x, far.position.y, strip.position.x, strip.position.y,
			bound.position.x, bound.position.y])
	quit(0)
