extends SceneTree
## Builds scenes/tests/cast_test.tscn: ShapeCast3D and ShapeCast2D nodes that move a
## sphere, a box, a capsule and a circle into static bodies, and one that hits nothing.
##
##   godot --headless --path . --script res://tools/make_cast_test.gd
##
## The component (CastTest in runtime/game/test_features.c) prints what each of them
## reports. tools/cast_reference.gd asks Godot's own physics the same, so the lines can
## be compared.

const OUTPUT := "res://scenes/tests/cast_test.tscn"

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "CastTest"
	_root.set_script(load("res://components/tests/cast_test.gd"))

	# A wall whose near side is at x = 4.5, and a ball of radius 1 off to the side.
	var wall_shape := BoxShape3D.new()
	wall_shape.size = Vector3(1, 4, 4)
	_body_3d("Wall", Vector3(5, 0, 0), wall_shape)
	var ball_shape := SphereShape3D.new()
	ball_shape.radius = 1.0
	_body_3d("Ball", Vector3(0, 0, -6), ball_shape)

	var sphere := SphereShape3D.new()
	sphere.radius = 0.5
	var box := BoxShape3D.new()
	box.size = Vector3(1, 1, 1)
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.3
	capsule.height = 1.4
	_root.sphere_cast = _cast_3d("SphereCast", Vector3(0, 0, 0), Vector3(10, 0, 0), sphere)
	_root.box_cast = _cast_3d("BoxCast", Vector3(0, 1, 0), Vector3(8, 0, 0), box)
	_root.capsule_cast = _cast_3d("CapsuleCast", Vector3(-5, 0, -6), Vector3(5, 0, 0), capsule)
	_root.miss_cast = _cast_3d("MissCast", Vector3(0, 10, 0), Vector3(10, 0, 0), sphere)

	# The same in 2D: a circle towards a block whose left side is at x = 280.
	var layer := Node2D.new()
	layer.name = "Layer2D"
	_root.add_child(layer)
	layer.owner = _root
	var block := StaticBody2D.new()
	block.name = "Block"
	layer.add_child(block)
	block.owner = _root
	block.position = Vector2(300, 100)
	var block_shape := CollisionShape2D.new()
	block_shape.name = "Shape"
	block.add_child(block_shape)
	block_shape.owner = _root
	var rectangle := RectangleShape2D.new()
	rectangle.size = Vector2(40, 200)
	block_shape.shape = rectangle
	var cast_2d := ShapeCast2D.new()
	cast_2d.name = "CircleCast"
	layer.add_child(cast_2d)
	cast_2d.owner = _root
	cast_2d.position = Vector2(100, 100)
	cast_2d.target_position = Vector2(400, 0)
	var circle := CircleShape2D.new()
	circle.radius = 10.0
	cast_2d.shape = circle
	_root.circle_cast = cast_2d

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _body_3d(body_name: String, at: Vector3, shape: Shape3D) -> void:
	var body := StaticBody3D.new()
	body.name = body_name
	_root.add_child(body)
	body.owner = _root
	body.position = at
	var body_shape := CollisionShape3D.new()
	body_shape.name = "Shape"
	body.add_child(body_shape)
	body_shape.owner = _root
	body_shape.shape = shape


func _cast_3d(cast_name: String, at: Vector3, target: Vector3, shape: Shape3D) -> ShapeCast3D:
	var cast := ShapeCast3D.new()
	cast.name = cast_name
	_root.add_child(cast)
	cast.owner = _root
	cast.position = at
	cast.target_position = target
	cast.shape = shape
	return cast
