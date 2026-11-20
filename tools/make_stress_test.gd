extends SceneTree
## Builds scenes/tests/stress_triggers.tscn: many areas and bodies, to check that the
## runtime reports every overlap and to measure what the trigger tests cost.
##
##   godot --headless --path . --script res://tools/make_stress_test.gd
##
## A 10 x 10 grid of Area3D nodes, one unit apart, each a sphere of radius 0.6: every area
## overlaps the neighbours left, right, above and below it (180 pairs), not the diagonal
## ones. Far behind them stand 100 StaticBody3D nodes that overlap nothing. The component
## (TriggerStress in runtime/game/test_features.c) counts the signals, moves the first row
## away and counts again. In front of the bodies, 20 rays point at one body each and 10
## through the gaps between them, and two capsule bodies walk along the bodies: one
## through a gap, one into a body.

const OUTPUT := "res://scenes/tests/stress_triggers.tscn"
const SIDE := 10

var _root: Node3D


func _init() -> void:
	_root = Node3D.new()
	_root.name = "StressTriggers"
	var probe := _add(_root, Node.new(), "Probe")
	probe.set_script(load("res://components/tests/trigger_stress.gd"))

	var sphere := SphereShape3D.new()
	sphere.radius = 0.6
	var box := BoxShape3D.new()
	box.size = Vector3(0.4, 0.4, 0.4)
	for y in SIDE:
		var row := _add(_root, Node3D.new(), "Row%d" % y) as Node3D
		row.position = Vector3(0, y, 0)
		if y == 0:
			probe.row = row
		for x in SIDE:
			var area := _add(row, Area3D.new(), "Area%d" % x) as Area3D
			area.position = Vector3(x, 0, 0)
			var shape := _add(area, CollisionShape3D.new(), "Shape") as CollisionShape3D
			shape.shape = sphere
			area.area_entered.connect(Callable(probe, "_on_area_entered"), CONNECT_PERSIST)
			area.area_exited.connect(Callable(probe, "_on_area_exited"), CONNECT_PERSIST)
	for i in SIDE * SIDE:
		var body := _add(_root, StaticBody3D.new(), "Body%d" % i) as StaticBody3D
		body.position = Vector3(i % SIDE, i / SIDE, -20)
		var shape := _add(body, CollisionShape3D.new(), "Shape") as CollisionShape3D
		shape.shape = box

	for i in 30:
		var ray := _add(_root, RayCast3D.new(), "Ray%d" % i) as RayCast3D
		if i < 20:
			ray.position = Vector3(i % SIDE, i / SIDE, -18)
			ray.add_to_group("hit_ray", true)
		else:
			ray.position = Vector3(i - 20 + 0.5, 0.5, -18)
			ray.add_to_group("gap_ray", true)
		ray.target_position = Vector3(0, 0, -4)
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.2
	capsule.height = 0.4
	for w in 2:
		var walker := _add(_root, CharacterBody3D.new(), "Walker%d" % w) as CharacterBody3D
		walker.position = Vector3(-1, 4.5 if w == 0 else 0.0, -20)
		var walker_shape := _add(walker, CollisionShape3D.new(), "Shape") as CollisionShape3D
		walker_shape.shape = capsule
		if w == 0:
			probe.free_walker = walker
		else:
			probe.blocked_walker = walker

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
