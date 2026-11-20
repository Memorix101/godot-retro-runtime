extends SceneTree
## Builds scenes/tests/anim_test.tscn: an AnimationPlayer with a speed scale and a blend
## time, and two clips that move and turn the same node.
##
##   godot --headless --path . --script res://tools/make_anim_test.gd
##
## The component (AnimTest in runtime/game/test_features.c) starts the second clip while
## the first one plays and prints where the node is before, during and after the change.
## tools/anim_reference.gd does the same in Godot, so the lines can be compared.

const OUTPUT := "res://scenes/tests/anim_test.tscn"

var _root: Node3D


func _initialize() -> void:
	_root = Node3D.new()
	_root.name = "AnimTest"
	_root.set_script(load("res://components/tests/anim_test.gd"))

	var mover := _add(MeshInstance3D.new(), "Mover") as MeshInstance3D
	var box := BoxMesh.new()
	box.size = Vector3(1, 0.5, 2)
	mover.mesh = box

	var library := AnimationLibrary.new()
	# Clip a: along X while turning about Y, round and round.
	library.add_animation("a", _clip(true, Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3.UP))
	# Clip b: higher up, along Z while turning about X, once.
	library.add_animation("b", _clip(false, Vector3(0, 2, 0), Vector3(0, 2, 4), Vector3.RIGHT))
	var anim := _add(AnimationPlayer.new(), "Anim") as AnimationPlayer
	anim.add_animation_library("", library)
	anim.autoplay = "a"
	anim.speed_scale = 1.5
	anim.playback_default_blend_time = 0.4

	var camera := _add(Camera3D.new(), "Camera") as Camera3D
	camera.position = Vector3(2, 3, 9)
	camera.rotation_degrees = Vector3(-10, 0, 0)
	camera.current = true

	_root.mover = mover
	_root.anim = anim

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


## One second from `from` to `to`, turning a quarter about `axis` on the way.
func _clip(loop: bool, from: Vector3, to: Vector3, axis: Vector3) -> Animation:
	var animation := Animation.new()
	animation.length = 1.0
	animation.loop_mode = Animation.LOOP_LINEAR if loop else Animation.LOOP_NONE
	var track := animation.add_track(Animation.TYPE_POSITION_3D)
	animation.track_set_path(track, "Mover")
	animation.position_track_insert_key(track, 0.0, from)
	animation.position_track_insert_key(track, 1.0, to)
	track = animation.add_track(Animation.TYPE_ROTATION_3D)
	animation.track_set_path(track, "Mover")
	animation.rotation_track_insert_key(track, 0.0, Quaternion.IDENTITY)
	animation.rotation_track_insert_key(track, 1.0, Quaternion(axis, PI / 2))
	return animation


func _add(node: Node, node_name: String) -> Node:
	node.name = node_name
	_root.add_child(node)
	node.owner = _root
	return node
