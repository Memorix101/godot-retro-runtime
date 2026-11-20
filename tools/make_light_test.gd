extends SceneTree
## Builds scenes/tests/light_test.tscn: a floor of many small squares under an OmniLight3D
## and a SpotLight3D, to look at what the light bake makes of them.
##
##   godot --headless --path . --script res://tools/make_light_test.gd
##
## The light is baked per vertex, so a surface needs enough vertices for a light to show:
## the floor is divided into 40 x 40 squares. There is no DirectionalLight3D; a dim one is
## not needed either, the ambient light of the WorldEnvironment keeps the rest visible.

const OUTPUT := "res://scenes/tests/light_test.tscn"

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "LightTest"

	var world := _add(WorldEnvironment.new(), "World") as WorldEnvironment
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.02, 0.05)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.15, 0.15, 0.2)
	world.environment = environment

	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(20, 20)
	floor_mesh.subdivide_width = 39
	floor_mesh.subdivide_depth = 39
	var ground := _add(MeshInstance3D.new(), "Floor") as MeshInstance3D
	ground.mesh = floor_mesh

	var box_mesh := BoxMesh.new()
	box_mesh.size = Vector3(1.5, 1.5, 1.5)
	var box := _add(MeshInstance3D.new(), "Box") as MeshInstance3D
	box.mesh = box_mesh
	box.position = Vector3(0, 0.75, 0)

	var lamp := _add(OmniLight3D.new(), "Lamp") as OmniLight3D
	lamp.position = Vector3(-4, 2, 0)
	lamp.light_color = Color(1.0, 0.45, 0.2)
	lamp.light_energy = 3.0
	lamp.omni_range = 7.0

	var spot := _add(SpotLight3D.new(), "Spot") as SpotLight3D
	spot.position = Vector3(5, 5, 0)
	spot.rotation_degrees = Vector3(-90, 0, 0)
	spot.light_color = Color(0.3, 0.6, 1.0)
	spot.light_energy = 12.0
	spot.spot_range = 8.0
	spot.spot_angle = 30.0

	var camera := _add(Camera3D.new(), "Camera") as Camera3D
	camera.position = Vector3(0, 9, 11)
	camera.rotation_degrees = Vector3(-40, 0, 0)
	camera.current = true

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _add(node: Node, node_name: String) -> Node:
	node.name = node_name
	_root.add_child(node)
	node.owner = _root
	return node
