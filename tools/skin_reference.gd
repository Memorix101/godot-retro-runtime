extends SceneTree
## Runs scenes/tests/skin_test.tscn in Godot itself until its animation has bent the tube,
## and prints where two of its vertices are then, in the form the runtime's SkinTest
## component prints it.
##
##   godot --headless --fixed-fps 60 --path . --script res://tools/skin_reference.gd
##
## Godot skins on the graphics card, where the result cannot be read back. So the places
## are worked out here from what Godot does provide: the pose its Skeleton3D has after the
## animation, the bind poses of the Skin and the weights in the mesh.

const SCENE := "res://scenes/tests/skin_test.tscn"
const FRAMES := 80
## The vertices that are printed, by where they are in the mesh at rest: one at the top
## (it follows the upper bone alone) and one in the middle (it follows both).
const PROBES: Array[Vector3] = [Vector3(0.3, 2.0, 0.0), Vector3(0.3, 1.0, 0.0)]

var _scene: Node
var _frame := 0


func _initialize() -> void:
	_scene = (load(SCENE) as PackedScene).instantiate()
	_scene.set_script(null)
	root.add_child(_scene)
	process_frame.connect(_on_frame)


func _on_frame() -> void:
	_frame += 1
	if _frame != FRAMES + 1:
		return
	var skeleton := _scene.get_node("Skeleton3D") as Skeleton3D
	var tube := skeleton.get_node("Tube") as MeshInstance3D
	var arrays := tube.mesh.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var bones: PackedInt32Array = arrays[Mesh.ARRAY_BONES]
	var weights: PackedFloat32Array = arrays[Mesh.ARRAY_WEIGHTS]
	var per_vertex := bones.size() / positions.size()
	var line := "skin:"
	for probe in PROBES:
		var nearest := 0
		for v in positions.size():
			if positions[v].distance_to(probe) < positions[nearest].distance_to(probe):
				nearest = v
		var at := Vector3.ZERO
		for k in per_vertex:
			var bind := bones[nearest * per_vertex + k]
			var weight := weights[nearest * per_vertex + k]
			if weight > 0.0:
				at += weight * (skeleton.get_bone_global_pose(tube.skin.get_bind_bone(bind))
						* tube.skin.get_bind_pose(bind) * positions[nearest])
		line += " %.2f %.2f %.2f" % [at.x, at.y, at.z]
	print(line)
	quit(0)
