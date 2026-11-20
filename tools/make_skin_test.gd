extends SceneTree
## Builds scenes/tests/skin_test.tscn: a tube whose vertices follow two bones, with the
## weights going over from one to the other along its middle, and an animation that bends
## the upper bone by a quarter turn.
##
##   godot --headless --path . --script res://tools/make_skin_test.gd
##
## The component (SkinTest in runtime/game/test_features.c) prints where two vertices of
## the tube are once it is bent. tools/skin_reference.gd works the same out from the pose
## Godot's Skeleton3D has then, so the lines can be compared.

const OUTPUT := "res://scenes/tests/skin_test.tscn"
const RINGS := 9
const SIDES := 8
const RADIUS := 0.3
const HEIGHT := 2.0

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "SkinTest"
	_root.set_script(load("res://components/tests/skin_test.gd"))

	var skeleton := _add(_root, Skeleton3D.new(), "Skeleton3D") as Skeleton3D
	skeleton.add_bone("Root")
	skeleton.add_bone("Tip")
	skeleton.set_bone_parent(1, 0)
	skeleton.set_bone_rest(1, Transform3D(Basis(), Vector3(0, 1, 0)))
	skeleton.reset_bone_poses()

	# The tube stands on the origin. Below y = 0.5 it follows Root, above 1.5 Tip, and in
	# between both.
	var positions := PackedVector3Array()
	var normals := PackedVector3Array()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	for ring in RINGS:
		var y := HEIGHT * ring / (RINGS - 1)
		var upper := clampf(y - 0.5, 0.0, 1.0)
		for side in SIDES:
			var angle := TAU * side / SIDES
			var out := Vector3(cos(angle), 0, sin(angle))
			positions.append(out * RADIUS + Vector3(0, y, 0))
			normals.append(out)
			bones.append_array([0, 1, 0, 0])
			weights.append_array([1.0 - upper, upper, 0.0, 0.0])
	for ring in RINGS - 1:
		for side in SIDES:
			var a := ring * SIDES + side
			var b := ring * SIDES + (side + 1) % SIDES
			indices.append_array([a, a + SIDES, b, b, a + SIDES, b + SIDES])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = positions
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_BONES] = bones
	arrays[Mesh.ARRAY_WEIGHTS] = weights
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.9, 0.6, 0.3)
	mesh.surface_set_material(0, material)

	# The bind poses take a vertex from the mesh into the space of each bone at rest.
	var skin := Skin.new()
	skin.add_bind(0, Transform3D())
	skin.add_bind(1, Transform3D(Basis(), Vector3(0, -1, 0)))

	var tube := _add(skeleton, MeshInstance3D.new(), "Tube") as MeshInstance3D
	tube.mesh = mesh
	tube.skin = skin
	tube.skeleton = NodePath("..")

	var bend := Animation.new()
	bend.length = 1.0
	var track := bend.add_track(Animation.TYPE_ROTATION_3D)
	bend.track_set_path(track, "Skeleton3D:Tip")
	bend.rotation_track_insert_key(track, 0.0, Quaternion.IDENTITY)
	bend.rotation_track_insert_key(track, 1.0, Quaternion(Vector3(0, 0, 1), PI / 2))
	var library := AnimationLibrary.new()
	library.add_animation("bend", bend)
	var anim := _add(_root, AnimationPlayer.new(), "Anim") as AnimationPlayer
	anim.add_animation_library("", library)
	anim.autoplay = "bend"

	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.position = Vector3(-0.3, 1.1, 4.5)
	camera.current = true

	_root.tube = tube

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
