extends SceneTree
## Builds scenes/tests/concave_test.tscn: a StaticBody3D with one ConcavePolygonShape3D,
## a bowl and a wall that is solid only from the bowl's side, turned and scaled. Rays
## from above hit the bowl, two rays cross the wall from behind (one with
## hit_back_faces, one without), a CharacterBody3D walks up the bowl into the wall, and a
## RigidBody3D ball rolls down into the bowl and comes to rest at its bottom.
##
##   godot --headless --path . --script res://tools/make_concave_test.gd
##
## The component (ConcaveTest in runtime/game/test_features.c) prints what the rays hit
## and where the two bodies are after eight seconds. tools/concave_reference.gd prints the
## same from Godot's own physics, so the lines can be compared.

const OUTPUT := "res://scenes/tests/concave_test.tscn"
const CELLS := 8      # the bowl is CELLS x CELLS quads of one unit
const CURVE := 0.15   # its height: CURVE times the squared distance from the middle
const WALL_X := 3.0   # the wall stands across the bowl at this X, facing -X

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "ConcaveTest"
	_root.set_script(load("res://components/tests/concave_test.gd"))

	var faces := _bowl_faces()
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)

	var body := _add(StaticBody3D.new(), "Bowl", _root) as StaticBody3D
	body.position = Vector3(1, -2, 0)
	var collision := _add(CollisionShape3D.new(), "Shape", body) as CollisionShape3D
	collision.shape = shape
	# Turned and scaled: the test is also one of the shape's transform.
	collision.transform = Transform3D(Basis(Vector3.UP, deg_to_rad(30)).scaled(Vector3.ONE * 1.25), Vector3.ZERO)
	var look := _add(MeshInstance3D.new(), "Look", collision) as MeshInstance3D
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = faces
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.55, 0.6, 0.7)
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.surface_set_material(0, material)
	look.mesh = mesh

	# Rays straight down onto the bowl, in the world.
	var rays: Array[RayCast3D] = []
	for at in [Vector3(1, 6, 0), Vector3(3, 6, 1.5), Vector3(-1.5, 6, -2), Vector3(0.2, 6, 3.1)]:
		rays.append(_ray("Down%d" % rays.size(), at, Vector3(0, -12, 0), false))
	# Across the wall. Its front faces the bowl's middle: from the middle the ray meets it
	# from the front; from outside it meets it from behind.
	var out := Basis(Vector3.UP, deg_to_rad(30)) * Vector3(1, 0, 0)  # from the middle to the wall
	# High enough that the rays from outside pass over the rim of the bowl.
	var middle := body.position + Vector3(0, 3.75, 0)
	rays.append(_ray("WallFront", middle, out * 8.0, false))
	rays.append(_ray("WallBack", middle + out * 7.0, -out * 4.0, false))
	rays.append(_ray("WallBackHit", middle + out * 7.0, -out * 4.0, true))

	# A fence: one quad that faces +X and is solid from both sides (backface_collision).
	# Two rays meet it from behind, without and with hit_back_faces.
	var fence_shape := ConcavePolygonShape3D.new()
	fence_shape.backface_collision = true
	fence_shape.set_faces(PackedVector3Array([Vector3(0, -1, 1), Vector3(0, 2, 1), Vector3(0, 2, -1),
			Vector3(0, -1, 1), Vector3(0, 2, -1), Vector3(0, -1, -1)]))
	var fence := _add(StaticBody3D.new(), "Fence", _root) as StaticBody3D
	fence.position = Vector3(-7, 0, 0)
	(_add(CollisionShape3D.new(), "Shape", fence) as CollisionShape3D).shape = fence_shape
	rays.append(_ray("FenceBack", fence.position + Vector3(-3, 0.5, 0.2), Vector3(6, 0, 0), false))
	rays.append(_ray("FenceBackHit", fence.position + Vector3(-3, 0.5, 0.2), Vector3(6, 0, 0), true))

	var walker := _add(CharacterBody3D.new(), "Walker", _root) as CharacterBody3D
	walker.position = body.position + Vector3(0, 1.5, 0)
	walker.collision_layer = 2  # the rays look for layer 1 only, the bowl and the fence
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.3
	capsule.height = 1.2
	(_add(CollisionShape3D.new(), "Shape", walker) as CollisionShape3D).shape = capsule
	var walker_look := _add(MeshInstance3D.new(), "Look", walker) as MeshInstance3D
	var walker_mesh := CapsuleMesh.new()
	walker_mesh.radius = 0.3
	walker_mesh.height = 1.2
	walker_look.mesh = walker_mesh

	var ball := _add(RigidBody3D.new(), "Ball", _root) as RigidBody3D
	ball.position = body.position + Basis(Vector3.UP, deg_to_rad(30)) * Vector3(-3, 3, 2)
	ball.collision_layer = 2
	# It reports what it touches: body_entered must come once, when it lands in the bowl.
	ball.contact_monitor = true
	ball.max_contacts_reported = 4
	ball.linear_damp = 1.0
	ball.angular_damp = 1.0
	var sphere := SphereShape3D.new()
	sphere.radius = 0.4
	(_add(CollisionShape3D.new(), "Shape", ball) as CollisionShape3D).shape = sphere
	var ball_look := _add(MeshInstance3D.new(), "Look", ball) as MeshInstance3D
	var ball_mesh := SphereMesh.new()
	ball_mesh.radius = 0.4
	ball_mesh.height = 0.8
	ball_look.mesh = ball_mesh

	var sun := _add(DirectionalLight3D.new(), "Sun", _root) as DirectionalLight3D
	sun.rotation_degrees = Vector3(-50, 30, 0)
	var camera := _add(Camera3D.new(), "Camera", _root) as Camera3D
	camera.position = Vector3(1, 7, 11)
	camera.rotation_degrees = Vector3(-35, 0, 0)
	camera.current = true

	_root.down0 = rays[0]
	_root.down1 = rays[1]
	_root.down2 = rays[2]
	_root.down3 = rays[3]
	_root.wall_front = rays[4]
	_root.wall_back = rays[5]
	_root.wall_back_hit = rays[6]
	_root.fence_back = rays[7]
	_root.fence_back_hit = rays[8]
	_root.walker = walker
	_root.ball = ball
	ball.body_entered.connect(Callable(_root, "_on_ball_body_entered"), CONNECT_PERSIST)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


## The faces: the bowl's quads with their fronts up, two triangles each, and the wall as
## two triangles whose front faces -X. Godot's front side is the one the corners go round
## clockwise.
func _bowl_faces() -> PackedVector3Array:
	var faces := PackedVector3Array()
	var half := CELLS / 2.0
	for x in CELLS:
		for z in CELLS:
			var p00 := _bowl_point(x - half, z - half)
			var p10 := _bowl_point(x + 1 - half, z - half)
			var p01 := _bowl_point(x - half, z + 1 - half)
			var p11 := _bowl_point(x + 1 - half, z + 1 - half)
			faces.append_array([p00, p10, p11, p00, p11, p01])
	var a := Vector3(WALL_X, -1, -3)
	var b := Vector3(WALL_X, 4, -3)
	var c := Vector3(WALL_X, 4, 3)
	var d := Vector3(WALL_X, -1, 3)
	faces.append_array([a, b, c, a, c, d])
	return faces


func _bowl_point(x: float, z: float) -> Vector3:
	return Vector3(x, CURVE * (x * x + z * z), z)


func _ray(ray_name: String, at: Vector3, target: Vector3, back_faces: bool) -> RayCast3D:
	var ray := _add(RayCast3D.new(), ray_name, _root) as RayCast3D
	ray.position = at
	ray.target_position = target
	ray.hit_back_faces = back_faces
	return ray


func _add(node: Node, node_name: String, parent: Node) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
