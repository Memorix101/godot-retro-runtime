extends SceneTree
## Builds scenes/tests/grid_test.tscn: a room made with a GridMap, grass made with a
## MultiMeshInstance3D, and a CharacterBody3D that walks across the floor into a wall.
##
##   godot --headless --path . --script res://tools/make_grid_test.gd
##
## The component (GridTest in runtime/game/test_features.c) moves the walker and prints
## where it stops. tools/grid_reference.gd does the same with Godot's own physics on the
## GridMap, so the two lines can be compared.

const OUTPUT := "res://scenes/tests/grid_test.tscn"
const SIDE := 10  # the floor is SIDE x SIDE cells of 2 m

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "GridTest"
	_root.set_script(load("res://components/tests/grid_test.gd"))

	# Two items: a floor tile at the bottom of its cell, and a wall along one side of it.
	# The walls are in the layer of cells above the floor and reach down to stand on it.
	var library := MeshLibrary.new()
	_item(library, 0, "Floor", Vector3(2, 0.2, 2), Vector3(0, -0.9, 0), Color(0.45, 0.6, 0.4))
	_item(library, 1, "Wall", Vector3(0.4, 2, 2), Vector3(0.8, -1.8, 0), Color(0.7, 0.55, 0.4))

	var grid := _add(GridMap.new(), "Room") as GridMap
	grid.mesh_library = library
	grid.cell_size = Vector3(2, 2, 2)
	for x in SIDE:
		for z in SIDE:
			grid.set_cell_item(Vector3i(x, 0, z), 0)
	# The walls stand on the cells at the edge, turned so that they face inwards. Item 1
	# is at the +X side of its cell; the orientations turn it to the other sides.
	var east := 0
	var west := grid.get_orthogonal_index_from_basis(Basis(Vector3.UP, PI))
	var north := grid.get_orthogonal_index_from_basis(Basis(Vector3.UP, PI / 2))
	var south := grid.get_orthogonal_index_from_basis(Basis(Vector3.UP, -PI / 2))
	for i in SIDE:
		grid.set_cell_item(Vector3i(SIDE - 1, 1, i), 1, east)
		grid.set_cell_item(Vector3i(0, 1, i), 1, west)
		grid.set_cell_item(Vector3i(i, 1, 0), 1, north)
		grid.set_cell_item(Vector3i(i, 1, SIDE - 1), 1, south)

	# Grass: many copies of one small mesh, turned and scaled differently.
	var blade := PrismMesh.new()
	blade.size = Vector3(0.15, 0.5, 0.05)
	var green := StandardMaterial3D.new()
	green.albedo_color = Color(0.3, 0.8, 0.3)
	blade.material = green
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = blade
	multimesh.instance_count = 300
	# Written as the buffer the scene file holds (the rows of a 3 x 4 matrix per copy): a
	# Godot without a screen forgets transforms that are set one by one.
	var buffer := PackedFloat32Array()
	var random := RandomNumberGenerator.new()
	random.seed = 7
	for i in multimesh.instance_count:
		var at := Vector3(random.randf_range(1.5, 18.5), 0.45, random.randf_range(1.5, 18.5))
		var turn := Basis(Vector3.UP, random.randf_range(0, TAU)).scaled(Vector3.ONE * random.randf_range(0.7, 1.4))
		buffer.append_array([turn.x.x, turn.y.x, turn.z.x, at.x, turn.x.y, turn.y.y, turn.z.y, at.y,
				turn.x.z, turn.y.z, turn.z.z, at.z])
	multimesh.buffer = buffer
	var grass := _add(MultiMeshInstance3D.new(), "Grass") as MultiMeshInstance3D
	grass.multimesh = multimesh

	var walker := _add(CharacterBody3D.new(), "Walker") as CharacterBody3D
	walker.position = Vector3(4, 1.5, 9)
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.3
	capsule.height = 1.2
	var shape := CollisionShape3D.new()
	shape.name = "Shape"
	walker.add_child(shape)
	shape.owner = _root
	shape.shape = capsule
	var look := MeshInstance3D.new()
	look.name = "Look"
	walker.add_child(look)
	look.owner = _root
	var look_mesh := CapsuleMesh.new()
	look_mesh.radius = 0.3
	look_mesh.height = 1.2
	look.mesh = look_mesh

	var sun := _add(DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-50, 30, 0)
	var camera := _add(Camera3D.new(), "Camera") as Camera3D
	camera.position = Vector3(10, 14, 26)
	camera.rotation_degrees = Vector3(-38, 0, 0)
	camera.current = true

	_root.walker = walker

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


## One item of the mesh library: a box of a size and colour, moved inside its cell, with
## a collision shape of the same size in the same place.
func _item(library: MeshLibrary, id: int, item_name: String, size: Vector3, offset: Vector3, color: Color) -> void:
	var mesh := BoxMesh.new()
	mesh.size = size
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	mesh.material = material
	var shape := BoxShape3D.new()
	shape.size = size
	library.create_item(id)
	library.set_item_name(id, item_name)
	library.set_item_mesh(id, mesh)
	library.set_item_mesh_transform(id, Transform3D(Basis(), offset))
	library.set_item_shapes(id, [shape, Transform3D(Basis(), offset)])


func _add(node: Node, node_name: String) -> Node:
	node.name = node_name
	_root.add_child(node)
	node.owner = _root
	return node
