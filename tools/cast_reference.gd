extends SceneTree
## Runs scenes/tests/cast_test.tscn in Godot itself and prints what its ShapeCast nodes
## report with Godot's own physics, in the form the runtime's CastTest component prints it.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/cast_reference.gd
##
## One line per cast: what it hit, how far along its line (safe and unsafe fraction) and
## the normal there.

const SCENE := "res://scenes/tests/cast_test.tscn"
const CASTS: Array[String] = ["SphereCast", "BoxCast", "CapsuleCast", "MissCast", "Layer2D/CircleCast"]

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	_scene.set_script(null)
	root.add_child(_scene)
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	_frame += 1
	if _frame < 4:
		return
	for path in CASTS:
		var cast := _scene.get_node(path)
		cast.force_shapecast_update()
		var hit := "nothing"
		var normal := Vector3.ZERO
		if cast.is_colliding():
			hit = cast.get_collider(0).name
			var found = cast.get_collision_normal(0)
			normal = Vector3(found.x, found.y, found.z if found is Vector3 else 0.0)
		print("cast: %s hits %s at %.1f to %.1f normal %.1f %.1f %.1f" % [cast.name, hit,
				cast.get_closest_collision_safe_fraction(), cast.get_closest_collision_unsafe_fraction(),
				normal.x, normal.y, normal.z])
	quit(0)
