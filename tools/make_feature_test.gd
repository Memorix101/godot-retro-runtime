extends SceneTree
## Builds scenes/features.tscn: a scene that checks runtime features by itself.
## Its component (runtime/game/test_features.c) looks at what the nodes report after a
## few frames and prints "features: ok" or what went wrong. Started on its own (as
## `make test` does) it quits then; started from the main menu it stays, shows the report
## on the screen and goes back with ui_cancel.
##
##   godot --headless --path . --script res://tools/make_feature_test.gd

const OUTPUT := "res://scenes/features.tscn"

var _root: Node3D


func _init() -> void:
	_root = Node3D.new()
	_root.name = "Features"

	# A ray that hits the top of a box, and one that hits nothing.
	var floor_body := _add(_root, StaticBody3D.new(), "Floor") as StaticBody3D
	# A sky without a picture: the exporter works out its colours.
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.2, 0.4, 0.8)
	sky_material.sky_horizon_color = Color(0.7, 0.8, 0.9)
	sky_material.ground_horizon_color = Color(0.6, 0.65, 0.6)
	sky_material.ground_bottom_color = Color(0.2, 0.25, 0.2)
	var sky := Sky.new()
	sky.sky_material = sky_material
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.4, 0.4, 0.4)
	var world := _add(_root, WorldEnvironment.new(), "Environment") as WorldEnvironment
	world.environment = environment
	var sun := _add(_root, DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-50, 30, 0)
	var floor_shape := _add(floor_body, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var box := BoxShape3D.new()
	box.size = Vector3(4, 2, 4)
	floor_shape.shape = box
	_box_mesh(floor_body, box.size, Color(0.45, 0.5, 0.55))
	var ray_down := _add(_root, RayCast3D.new(), "RayDown") as RayCast3D
	ray_down.position = Vector3(0, 5, 0)
	ray_down.target_position = Vector3(0, -10, 0)
	var ray_miss := _add(_root, RayCast3D.new(), "RayMiss") as RayCast3D
	ray_miss.position = Vector3(20, 5, 0)
	ray_miss.target_position = Vector3(0, -10, 0)

	# A 2D ray that hits a round area.
	var layer := _add(_root, Node2D.new(), "Layer2D") as Node2D
	var bubble := _add(layer, Area2D.new(), "Bubble") as Area2D
	bubble.position = Vector2(50, 0)
	var bubble_shape := _add(bubble, CollisionShape2D.new(), "Shape") as CollisionShape2D
	var circle := CircleShape2D.new()
	circle.radius = 10.0
	bubble_shape.shape = circle
	var ray_2d := _add(layer, RayCast2D.new(), "Ray2D") as RayCast2D
	ray_2d.target_position = Vector2(100, 0)
	ray_2d.collide_with_areas = true

	# A straight 3D path with two followers, and a 2D one.
	var track := _add(_root, Path3D.new(), "Track") as Path3D
	var curve := Curve3D.new()
	curve.add_point(Vector3(0, 0, 0))
	curve.add_point(Vector3(10, 0, 0))
	track.curve = curve
	var follower := _add(track, PathFollow3D.new(), "Follower") as PathFollow3D
	follower.rotation_mode = PathFollow3D.ROTATION_Y
	follower.loop = false
	follower.progress = 2.5
	var follower_loop := _add(track, PathFollow3D.new(), "FollowerLoop") as PathFollow3D
	follower_loop.rotation_mode = PathFollow3D.ROTATION_ORIENTED
	follower_loop.loop = true
	_add(follower_loop, Marker3D.new(), "Seat")
	_box_mesh(follower_loop, Vector3(0.5, 0.5, 1.0), Color(0.9, 0.8, 0.2)).position = Vector3(0, 1.6, 0)
	_box_mesh(follower, Vector3(0.5, 0.5, 0.5), Color(0.3, 0.8, 0.9)).position = Vector3(0, 2.4, 0)
	var track_2d := _add(layer, Path2D.new(), "Track2D") as Path2D
	var curve_2d := Curve2D.new()
	curve_2d.add_point(Vector2(0, 0))
	curve_2d.add_point(Vector2(0, 100))
	track_2d.curve = curve_2d
	var follower_2d := _add(track_2d, PathFollow2D.new(), "Follower2D") as PathFollow2D
	follower_2d.loop = false
	follower_2d.progress = 30.0

	# A mesh that is only drawn from nearby.
	var far := _add(_root, MeshInstance3D.new(), "FarBox") as MeshInstance3D
	far.mesh = BoxMesh.new()
	far.position = Vector3(0, 0, -50)
	far.visibility_range_end = 5.0
	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.position = Vector3(0, 3, 10)

	# Controls.
	var hud := _add(_root, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	var backdrop := _add(hud, ColorRect.new(), "Backdrop") as ColorRect
	backdrop.position = Vector2(20, 20)
	backdrop.size = Vector2(200, 40)
	backdrop.color = Color(0.1, 0.2, 0.5, 0.8)
	var bar := _add(hud, ProgressBar.new(), "Bar") as ProgressBar
	bar.position = Vector2(20, 70)
	bar.size = Vector2(200, 27)
	bar.value = 30.0
	var frame := _add(hud, NinePatchRect.new(), "Frame") as NinePatchRect
	frame.position = Vector2(20, 110)
	frame.size = Vector2(160, 80)
	frame.texture = load("res://assets/sprites/logo.png")
	for side: Side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		frame.set_patch_margin(side, 8)

	# Sound: a 3D player 10 units (its unit size) to the right of the camera, a 2D player
	# 160 pixels to the right of the middle of the screen, a short sound and music.
	var beep_stream: AudioStream = load("res://assets/sounds/beep.wav")
	var speaker := _add(_root, AudioStreamPlayer3D.new(), "Speaker") as AudioStreamPlayer3D
	speaker.stream = beep_stream
	speaker.position = Vector3(10, 3, 10)
	speaker.unit_size = 10.0
	var speaker_2d := _add(layer, AudioStreamPlayer2D.new(), "Speaker2D") as AudioStreamPlayer2D
	speaker_2d.stream = beep_stream
	speaker_2d.position = Vector2(480, 240)
	speaker_2d.max_distance = 320.0
	var beep := _add(_root, AudioStreamPlayer.new(), "Beep") as AudioStreamPlayer
	beep.stream = beep_stream
	beep.autoplay = true
	# A stereo sound, so that one is loaded and played on every platform.
	var wide := _add(_root, AudioStreamPlayer.new(), "Wide") as AudioStreamPlayer
	wide.stream = load("res://assets/sounds/sweep.wav")
	wide.autoplay = true
	var music := _add(_root, AudioStreamPlayer.new(), "Music") as AudioStreamPlayer
	music.stream = load("res://assets/sounds/music.ogg")
	music.autoplay = true
	music.volume_db = -6.0

	# An animation with 2D tracks and a method call, and a node for a tween to move.
	var mover := _add(layer, Node2D.new(), "Mover") as Node2D
	var tween_target := _add(_root, Node3D.new(), "TweenTarget") as Node3D
	_box_mesh(tween_target, Vector3(0.6, 0.6, 0.6), Color(0.9, 0.5, 0.9)).position = Vector3(0, 3.4, 0)
	var animation := Animation.new()
	animation.length = 0.5
	var anim_track := animation.add_track(Animation.TYPE_VALUE)
	animation.track_set_path(anim_track, "Layer2D/Mover:position")
	animation.track_insert_key(anim_track, 0.0, Vector2(0, 0))
	animation.track_insert_key(anim_track, 0.5, Vector2(100, 50))
	anim_track = animation.add_track(Animation.TYPE_VALUE)
	animation.track_set_path(anim_track, "Layer2D/Mover:rotation")
	animation.track_insert_key(anim_track, 0.0, 0.0)
	animation.track_insert_key(anim_track, 0.5, PI / 2)
	anim_track = animation.add_track(Animation.TYPE_METHOD)
	animation.track_set_path(anim_track, "Probe")
	animation.track_insert_key(anim_track, 0.25, {"method": &"_on_anim_mark", "args": []})
	var library := AnimationLibrary.new()
	library.add_animation("move", animation)
	var anim := _add(_root, AnimationPlayer.new(), "Anim") as AnimationPlayer
	anim.add_animation_library("", library)
	anim.autoplay = "move"

	# An area inside the floor's box, to be told about that body.
	var sensor := _add(_root, Area3D.new(), "Sensor") as Area3D
	var sensor_shape := _add(sensor, CollisionShape3D.new(), "Shape") as CollisionShape3D
	sensor_shape.shape = BoxShape3D.new()

	# Things to look at: a sprite and a label in the 3D world, and 2D particles.
	var sprite_3d := _add(_root, Sprite3D.new(), "Sprite3D") as Sprite3D
	sprite_3d.texture = load("res://assets/sprites/ship.png")
	sprite_3d.position = Vector3(-3, 3, 0)
	sprite_3d.pixel_size = 0.08
	sprite_3d.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sprite_3d.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	var label_3d := _add(_root, Label3D.new(), "Label3D") as Label3D
	label_3d.text = "LABEL 3D"
	label_3d.position = Vector3(3, 3, 0)
	label_3d.pixel_size = 0.03
	label_3d.rotation_degrees = Vector3(0, -30, 0)
	var sparks := _add(layer, CPUParticles2D.new(), "Sparks2D") as CPUParticles2D
	sparks.position = Vector2(450, 380)
	sparks.amount = 24
	sparks.lifetime = 0.6
	sparks.direction = Vector2(0, -1)
	sparks.spread = 35.0
	sparks.gravity = Vector2(0, 300)
	sparks.initial_velocity_min = 120.0
	sparks.initial_velocity_max = 200.0
	sparks.scale_amount_min = 6.0
	sparks.scale_amount_max = 10.0
	sparks.color = Color(1.0, 0.6, 0.1)

	# Far to the right: a ramp turned by 30 degrees and a capsule that walks up on it.
	var ramp := _add(_root, StaticBody3D.new(), "Ramp") as StaticBody3D
	ramp.position = Vector3(9, 0, -4)
	ramp.rotation_degrees = Vector3(0, 0, 30)
	var ramp_shape := _add(ramp, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var ramp_box := BoxShape3D.new()
	ramp_box.size = Vector3(12, 1, 4)
	ramp_shape.shape = ramp_box
	_box_mesh(ramp, ramp_box.size, Color(0.6, 0.45, 0.3))
	var walker := _add(_root, CharacterBody3D.new(), "Walker") as CharacterBody3D
	walker.position = Vector3(8, 2.2, -4)
	walker.add_to_group("walker", true)
	var walker_shape := _add(walker, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.5
	capsule.height = 2.0
	walker_shape.shape = capsule
	var capsule_mesh := CapsuleMesh.new()
	capsule_mesh.radius = 0.5
	capsule_mesh.height = 2.0
	capsule_mesh.radial_segments = 10
	capsule_mesh.rings = 3
	_mesh(walker, capsule_mesh, Color(0.3, 0.8, 0.4))

	# Far to the left: a pad and a ball that falls on it and bounces.
	var pad := _add(_root, StaticBody3D.new(), "Pad") as StaticBody3D
	pad.position = Vector3(-8, 0, -4)
	var pad_shape := _add(pad, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var pad_box := BoxShape3D.new()
	pad_box.size = Vector3(4, 1, 4)
	pad_shape.shape = pad_box
	_box_mesh(pad, pad_box.size, Color(0.4, 0.4, 0.7))
	var ball := _add(_root, RigidBody3D.new(), "Ball") as RigidBody3D
	ball.position = Vector3(-8, 2.5, -4)
	ball.add_to_group("ball", true)
	var bouncy := PhysicsMaterial.new()
	bouncy.bounce = 0.5
	ball.physics_material_override = bouncy
	var ball_shape := _add(ball, CollisionShape3D.new(), "Shape") as CollisionShape3D
	ball_shape.shape = SphereShape3D.new()
	var ball_mesh := SphereMesh.new()
	ball_mesh.radial_segments = 12
	ball_mesh.rings = 6
	_mesh(ball, ball_mesh, Color(0.9, 0.3, 0.3))

	# Where the report goes when the scene was started from the menu.
	var result := _add(hud, Label.new(), "Result") as Label
	result.position = Vector2(300, 14)
	result.size = Vector2(326, 300)
	result.text = "RUNNING..."
	result.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	result.add_to_group("result", true)
	# A label whose text is a key of the project's translations (translations/strings.csv).
	var greeting := _add(hud, Label.new(), "Greeting") as Label
	greeting.position = Vector2(20, 414)
	greeting.size = Vector2(300, 24)
	greeting.text = "GREETING"
	var hint := _add(hud, Label.new(), "Hint") as Label
	hint.position = Vector2(20, 446)
	hint.size = Vector2(600, 24)
	hint.text = "Esc or B: back"
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER

	var probe := _add(_root, Node.new(), "Probe")
	probe.set_script(load("res://components/tests/feature_test.gd"))
	probe.ray_down = ray_down
	probe.ray_miss = ray_miss
	probe.ray_2d = ray_2d
	probe.track = track
	probe.follower = follower
	probe.follower_loop = follower_loop
	probe.follower_2d = follower_2d
	probe.bar = bar
	var stats := TestStats.new()
	stats.power = 2.5
	stats.gears = 6
	stats.next = TestStats.new()
	stats.next.gears = 4
	probe.stats = stats
	probe.self_scene = OUTPUT
	probe.speaker = speaker
	probe.speaker_2d = speaker_2d
	probe.beep = beep
	probe.greeting = hud.get_node("Greeting")
	probe.music = music
	probe.mover = mover
	probe.tween_target = tween_target
	sensor.body_entered.connect(Callable(probe, "_on_sensor_body_entered"), CONNECT_PERSIST)
	anim.animation_finished.connect(Callable(probe, "_on_anim_finished"), CONNECT_PERSIST)
	beep.finished.connect(Callable(probe, "_on_beep_finished"), CONNECT_PERSIST)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote ", OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


## A mesh below `parent`, so that what the scene checks can also be seen.
func _mesh(parent: Node, mesh: PrimitiveMesh, color: Color) -> MeshInstance3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	mesh.material = material
	var instance := _add(parent, MeshInstance3D.new(), "Mesh") as MeshInstance3D
	instance.mesh = mesh
	return instance


func _box_mesh(parent: Node, size: Vector3, color: Color) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	return _mesh(parent, mesh, color)


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
