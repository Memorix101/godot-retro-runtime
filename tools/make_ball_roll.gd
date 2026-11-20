extends SceneTree
## Builds scenes/ball_roll.tscn, the ball demo: a long course of floors without railings
## that hangs in the sky, and a ball with the kit's character inside that is rolled over it
## to the flag. The result is an ordinary scene that can be opened and edited in Godot;
## running this tool again overwrites it.
##
##   godot --headless --path . --script res://tools/make_ball_roll.gd
##
## The ball is a RigidBody3D with a sphere shape, which the runtime lets fall and roll off
## edges by itself; its component (runtime/game/ball_roll.c) only pushes it the way the
## stick points. The floors are StaticBody3D nodes with one box shape each, flat, sloped
## or banked.

const OUTPUT := "res://scenes/ball_roll.tscn"
const KIT := "res://assets/kenney/platformer-kit/%s"

## Radius of the ball, metres.
const RADIUS := 0.6
## How thick a floor is. Thick enough that a fast ball never gets through in one frame.
const THICK := 1.0

const SAND := Color(0.95, 0.82, 0.55)
const MINT := Color(0.55, 0.9, 0.7)
const SKY := Color(0.55, 0.75, 0.98)
const ROSE := Color(0.98, 0.62, 0.66)
const PLUM := Color(0.72, 0.6, 0.95)
const STONE := Color(0.55, 0.57, 0.65)

var _root: Node3D
var _floors: Node3D
var _level: Node3D
var _floor_material: StandardMaterial3D
var _meshes := {}
var _coins := 0


func _init() -> void:
	_root = Node3D.new()
	_root.name = "BallRoll"
	_environment()
	_floors = _add(_root, Node3D.new(), "Floors") as Node3D
	_level = _add(_root, Node3D.new(), "Level") as Node3D
	_floor_material = StandardMaterial3D.new()
	_floor_material.albedo_texture = load("res://assets/sprites/tiles.png")
	_floor_material.vertex_color_use_as_albedo = true

	var start := Vector3(0, 0, 1)
	_course()
	_player(start)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote %s (%d floors, %d coins)" % [OUTPUT, _floors.get_child_count(), _coins])
	_root.free()
	quit(0 if status == OK else 1)


## The course, from the start pad at the origin to the flag. Every floor is a strip from
## one point of its surface to another; the points are in metres.
func _course() -> void:
	# The start pad, and a first bridge to get used to the ball.
	_strip("Start", Vector3(0, 0, 4), Vector3(0, 0, -4), 8.0, SAND)
	_strip("Bridge1", Vector3(0, 0, -4), Vector3(0, 0, -20), 4.0, MINT)
	for i in 5:
		_coin(Vector3(0, 0, -6 - i * 3))

	# A square with four posts to steer around and a ring of coins.
	_strip("Square", Vector3(0, 0, -20), Vector3(0, 0, -36), 16.0, SKY)
	for corner: Vector2 in [Vector2(-4, -24), Vector2(4, -24), Vector2(-4, -32), Vector2(4, -32)]:
		_post("Post%d" % _floors.get_child_count(), Vector3(corner.x, 0, corner.y))
	for i in 8:
		var angle := i * TAU / 8.0
		_coin(Vector3(sin(angle) * 6.5, 0, -28 + cos(angle) * 6.5))

	# Down a slope, which is where the ball gets fast, and out over a narrow bridge.
	_strip("Slope", Vector3(0, 0, -36), Vector3(0, -3, -50), 5.0, ROSE)
	_strip("Runway", Vector3(0, -3, -50), Vector3(0, -3, -58), 6.0, SAND)
	_strip("Bridge2", Vector3(0, -3, -58), Vector3(0, -3, -74), 2.0, MINT)
	for i in 4:
		_coin(Vector3(0, -3, -61 - i * 3.5))

	# A place to turn right, with the first checkpoint.
	_strip("Turn", Vector3(0, -3, -74), Vector3(0, -3, -88), 14.0, SKY)
	_instance("res://scenes/platformer/checkpoint.tscn", "Checkpoint1", Vector3(0, -3, -81))

	# An S of three narrow floors.
	_strip("Bend1", Vector3(7, -3, -81), Vector3(19, -3, -81), 3.0, PLUM)
	_strip("Bend2", Vector3(17.5, -3, -79.5), Vector3(17.5, -3, -69.5), 3.0, PLUM)
	_strip("Bend3", Vector3(19, -3, -71), Vector3(29, -3, -71), 3.0, PLUM)
	for at: Vector3 in [Vector3(11, -3, -81), Vector3(15, -3, -81), Vector3(17.5, -3, -77), Vector3(17.5, -3, -73),
			Vector3(22, -3, -71), Vector3(26, -3, -71)]:
		_coin(at)

	# Up a slope to the upper square, with the second checkpoint.
	_strip("Climb", Vector3(29, -3, -71), Vector3(41, -1, -71), 4.0, ROSE)
	_strip("Upper", Vector3(41, -1, -71), Vector3(55, -1, -71), 14.0, SKY)
	_instance("res://scenes/platformer/checkpoint.tscn", "Checkpoint2", Vector3(46, -1, -71))
	for i in 6:
		var angle := i * TAU / 6.0
		_coin(Vector3(48 + sin(angle) * 4.5, -1, -71 + cos(angle) * 4.5))

	# A trough: a narrow floor between two banks that carry the ball back to the middle.
	# The coins are up on the banks.
	var from := Vector3(48, -1, -78)
	var to := Vector3(48, -1, -104)
	var bank := deg_to_rad(32.0)
	var reach := 1.5 + 1.5 * cos(bank)
	var lift := 1.5 * sin(bank)
	_strip("Trough", from, to, 3.0, SAND)
	_strip("BankLeft", from + Vector3(-reach, lift, 0), to + Vector3(-reach, lift, 0), 3.0, MINT, -bank)
	_strip("BankRight", from + Vector3(reach, lift, 0), to + Vector3(reach, lift, 0), 3.0, MINT, bank)
	for i in 6:
		var side := -1.0 if i % 2 == 0 else 1.0
		_coin(Vector3(48 + side * (reach + 0.3), -1 + lift + 0.2, -81 - i * 4))

	# The trough ends in the air above the last floor, where the flag is.
	_strip("Goal", Vector3(48, -3.5, -103), Vector3(48, -3.5, -124), 12.0, SKY)
	for i in 3:
		_coin(Vector3(48, -3.5, -108 - i * 2.5))
	# The goal is a gate between two flags: a ball that comes down at full speed need not
	# hit one of them.
	_instance("res://scenes/platformer/flag.tscn", "FlagLeft", Vector3(44.5, -3.5, -116))
	_instance("res://scenes/platformer/flag.tscn", "FlagRight", Vector3(51.5, -3.5, -116))
	var gate := _add(_level, Area3D.new(), "Gate") as Area3D
	gate.add_to_group("goal", true)
	gate.monitoring = false
	gate.position = Vector3(48, -2.5, -116)
	var gate_box := BoxShape3D.new()
	gate_box.size = Vector3(7.0, 2.0, 1.0)
	var gate_shape := _add(gate, CollisionShape3D.new(), "Shape") as CollisionShape3D
	gate_shape.shape = gate_box


## A floor whose surface runs from `from` to `to` along its middle, `width` metres wide.
## `roll` (radians) lifts its right edge, as seen along the way.
func _strip(node_name: String, from: Vector3, to: Vector3, width: float, tint: Color, roll := 0.0) -> void:
	var back := (from - to).normalized()
	var right := Vector3.UP.cross(back).normalized()
	var basis := Basis(right, back.cross(right), back).rotated(back, roll)
	var size := Vector3(width, THICK, from.distance_to(to))
	_box(node_name, Transform3D(basis, (from + to) * 0.5 - basis.y * THICK * 0.5), size, tint)


## A post that stands on a floor, turned by 45 degrees so the ball glances off it.
func _post(node_name: String, at: Vector3) -> void:
	var size := Vector3(1.6, 1.6, 1.6)
	_box(node_name, Transform3D(Basis(Vector3.UP, PI * 0.25), at + Vector3(0, size.y * 0.5, 0)), size, STONE)


func _box(node_name: String, xform: Transform3D, size: Vector3, tint: Color) -> void:
	var body := _add(_floors, StaticBody3D.new(), node_name) as StaticBody3D
	body.transform = xform
	var box := BoxShape3D.new()
	box.size = size
	var shape := _add(body, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = box
	var mesh := _add(body, MeshInstance3D.new(), "Mesh") as MeshInstance3D
	mesh.mesh = _box_mesh(size, tint)


## A box whose sides repeat the tile texture every two metres, in the colour `tint` on
## top and a darker one on the other sides.
func _box_mesh(size: Vector3, tint: Color) -> ArrayMesh:
	var key := [size, tint]
	if _meshes.has(key):
		return _meshes[key]
	var box := BoxMesh.new()
	box.size = size
	var arrays := box.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var uvs := PackedVector2Array()
	var colors := PackedColorArray()
	for v in positions.size():
		# Counted from a corner, so the coordinates are never negative.
		var p := positions[v] + size * 0.5
		var n := normals[v]
		if absf(n.y) > 0.5:
			uvs.append(Vector2(p.x, p.z) * 0.5)
		elif absf(n.x) > 0.5:
			uvs.append(Vector2(p.z, p.y) * 0.5)
		else:
			uvs.append(Vector2(p.x, p.y) * 0.5)
		colors.append(tint if n.y > 0.5 else tint.darkened(0.3))
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_TANGENT] = null
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, _floor_material)
	_meshes[key] = mesh
	return mesh


## A coin at the height of the ball's middle above a place on a floor.
func _coin(at: Vector3) -> void:
	_instance("res://scenes/platformer/coin.tscn", "Coin%d" % _coins, at + Vector3(0, RADIUS, 0))
	_coins += 1


func _instance(scene: String, node_name: String, at: Vector3) -> void:
	var node := (load(scene) as PackedScene).instantiate() as Node3D
	_add(_level, node, node_name)
	node.position = at


func _environment() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.2, 0.42, 0.9)
	sky_material.sky_horizon_color = Color(0.8, 0.9, 1.0)
	sky_material.ground_horizon_color = Color(0.8, 0.9, 1.0)
	sky_material.ground_bottom_color = Color(0.45, 0.65, 0.95)
	var sky := Sky.new()
	sky.sky_material = sky_material
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.55, 0.55, 0.55)
	var world := _add(_root, WorldEnvironment.new(), "Environment") as WorldEnvironment
	world.environment = environment
	var sun := _add(_root, DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-55, 35, 0)


func _player(start: Vector3) -> void:
	var slippery := PhysicsMaterial.new()
	# The runtime's bodies do not turn, so rubbing would only brake the ball. Its rolling
	# resistance is the linear damp.
	slippery.friction = 0.0
	slippery.bounce = 0.2
	var sphere := SphereShape3D.new()
	sphere.radius = RADIUS

	var ball := _add(_root, RigidBody3D.new(), "Ball") as RigidBody3D
	ball.set_script(load("res://components/ball_roller.gd"))
	ball.position = start + Vector3(0, RADIUS + 0.05, 0)
	ball.physics_material_override = slippery
	ball.gravity_scale = 2.0
	ball.linear_damp = 0.2
	ball.lock_rotation = true
	var shape := _add(ball, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = sphere

	var shell := _add(ball, MeshInstance3D.new(), "Shell") as MeshInstance3D
	shell.mesh = _shell_mesh()

	# The rider stands on the bottom of the ball. The component turns "Rider" to face the
	# way the ball rolls, which would lose a scale, so the model below it carries that.
	var rider := _add(ball, Node3D.new(), "Rider") as Node3D
	rider.position = Vector3(0, -RADIUS + 0.12, 0)
	var model := (load(KIT % "character-oobi.glb") as PackedScene).instantiate() as Node3D
	_add(rider, model, "Model")
	model.scale = Vector3.ONE * 0.75
	_root.set_editable_instance(model, true)

	var ray := _add(ball, RayCast3D.new(), "ShadowRay") as RayCast3D
	ray.target_position = Vector3(0, -12, 0)
	var hitbox := _add(ball, Area3D.new(), "Hitbox") as Area3D
	hitbox.monitorable = false
	var hit_shape := _add(hitbox, CollisionShape3D.new(), "Shape") as CollisionShape3D
	hit_shape.shape = sphere
	hitbox.area_entered.connect(Callable(ball, "_on_hitbox_area_entered"), CONNECT_PERSIST)

	var shadow := _add(_root, Sprite3D.new(), "Shadow") as Sprite3D
	shadow.pixel_size = 0.045
	shadow.axis = Vector3.AXIS_Y
	shadow.texture = load("res://assets/sprites/shadow.png")

	# Behind and above the ball. The component keeps this distance and height and swings
	# the camera round to look the way the ball rolls, so the two must have the same parent.
	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.position = ball.position + Vector3(0, 2.7, 6.5)
	camera.rotation_degrees = Vector3(-22, 0, 0)
	camera.current = true
	camera.fov = 60.0
	camera.near = 0.4
	camera.far = 150.0

	var hud := _add(_root, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	var coins := _label(hud, "Coins", Rect2(16, 10, 300, 28), 20, "COINS 0/0", HORIZONTAL_ALIGNMENT_LEFT)
	var status := _label(hud, "Status", Rect2(250, 10, 374, 28), 20, "LIVES 3  TIME 0.0", HORIZONTAL_ALIGNMENT_RIGHT)
	_label(hud, "Hint", Rect2(20, 452, 600, 22), 14, "Arrows or stick: tilt   Esc or B: back",
			HORIZONTAL_ALIGNMENT_CENTER)

	var sound := _add(_root, AudioStreamPlayer.new(), "CoinSound") as AudioStreamPlayer
	sound.stream = load("res://assets/sounds/beep.wav")

	# One burst of sparks serves all coins; the ball moves it to the coin it took.
	var spark := StandardMaterial3D.new()
	spark.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	spark.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	spark.vertex_color_use_as_albedo = true
	spark.albedo_texture = load("res://assets/kenney/pixel-platformer/tiles/tile_0151.png")
	spark.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	spark.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	var quad := QuadMesh.new()
	quad.size = Vector2(0.28, 0.28)
	quad.material = spark
	var ramp := Gradient.new()
	ramp.colors = PackedColorArray([Color(1, 1, 1, 1), Color(1, 1, 1, 0)])
	var burst := _add(_root, CPUParticles3D.new(), "CoinBurst") as CPUParticles3D
	burst.emitting = false
	burst.amount = 14
	burst.lifetime = 0.7
	burst.one_shot = true
	burst.explosiveness = 1.0
	burst.mesh = quad
	burst.direction = Vector3(0, 1, 0)
	burst.spread = 55.0
	burst.gravity = Vector3(0, -9, 0)
	burst.initial_velocity_min = 2.5
	burst.initial_velocity_max = 4.5
	burst.scale_amount_min = 0.6
	burst.scale_amount_max = 1.2
	burst.color_ramp = ramp

	ball.push = 9.0
	ball.max_speed = 13.0
	ball.tilt_degrees = 9.0
	ball.radius = RADIUS
	ball.fall_limit = -14.0
	ball.shell = shell
	ball.rider = rider
	ball.anim = model.get_node("AnimationPlayer")
	ball.camera = camera
	ball.coin_label = coins
	ball.coin_sound = sound
	ball.coin_burst = burst
	ball.shadow_ray = ray
	ball.shadow = shadow
	ball.status_label = status
	ball.menu_scene = "res://scenes/main_menu.tscn"


## The ball itself: a sphere one can see through, in eight fields of two colours so that
## its rolling shows. Unlit, because the component turns it and baked light would turn too.
func _shell_mesh() -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radius = RADIUS
	sphere.height = RADIUS * 2.0
	sphere.radial_segments = 12
	sphere.rings = 5
	var arrays := sphere.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	for t in range(0, indices.size(), 3):
		var corners: Array[Vector3] = [positions[indices[t]], positions[indices[t + 1]], positions[indices[t + 2]]]
		var middle := (corners[0] + corners[1] + corners[2]) / 3.0
		if corners[0].is_equal_approx(corners[1]) or corners[1].is_equal_approx(corners[2]) \
				or corners[0].is_equal_approx(corners[2]):
			continue  # the poles end in triangles without an area
		# Four fields round the ball, and the other colour below the middle.
		var field := int(floor((atan2(middle.z, middle.x) + PI) / (PI * 0.5) + 0.001)) % 4
		var first := (field + (0 if middle.y > 0.0 else 1)) % 2 == 0
		tool.set_color(Color(1.0, 1.0, 1.0, 0.34) if first else Color(0.45, 0.8, 1.0, 0.2))
		for corner in corners:
			tool.add_vertex(corner)
	var glass := StandardMaterial3D.new()
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	glass.vertex_color_use_as_albedo = true
	tool.set_material(glass)
	return tool.commit()


func _label(parent: Control, node_name: String, rect: Rect2, font_size: int, text: String,
		alignment: HorizontalAlignment) -> Label:
	var label := _add(parent, Label.new(), node_name) as Label
	label.position = rect.position
	label.size = rect.size
	label.add_theme_font_size_override("font_size", font_size)
	label.text = text
	label.horizontal_alignment = alignment
	return label


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
