extends SceneTree
## Builds scenes/racing.tscn, the racing demo, from Kenney's "Racing Kit" (CC0,
## assets/kenney/racing-kit). The result is an ordinary scene that can be opened and
## edited in Godot; running this tool again overwrites it.
##
##   godot --headless --path . --script res://tools/make_racing.gd
##
## The track is laid out piece by piece from TRACK, like a model railway: road tiles,
## barriers along both sides (StaticBody3D), a Path3D along the middle for the opponents
## and the checkpoints, and decoration around it. The logic is in runtime/game/racer.c.

const OUTPUT := "res://scenes/racing.tscn"
const KIT := "res://assets/kenney/racing-kit/%s.obj"
## Metres per tile of the kit, and the scale of the cars.
const TILE := 12.0
const CAR_SCALE := 2.5
const LAPS := 3

## The track from the start: G start grid (2 tiles), S straight (1 tile), R and L right and
## left corners (2 x 2 tiles, a quarter circle of radius 1.5 tiles). It must close.
const TRACK := "GSSSRSSLRSRSSSSSSSSRSSSSSSR"

## Where a piece is entered and left, in its own space and tile units: the middle of the
## road and the direction of travel. A left corner is the right corner driven backwards.
const PIECES := {
	"S": {"mesh": "roadStraight", "entry": Vector3(-0.5, 0, 0), "in": Vector3(0, 0, 1),
			"exit": Vector3(-0.5, 0, 1), "out": Vector3(0, 0, 1), "length": 1},
	"G": {"mesh": "roadStartPositions", "entry": Vector3(-0.5, 0, 0), "in": Vector3(0, 0, 1),
			"exit": Vector3(-0.5, 0, 2), "out": Vector3(0, 0, 1), "length": 2},
	"R": {"mesh": "roadCornerLarge", "entry": Vector3(-0.5, 0, 0), "in": Vector3(0, 0, 1),
			"exit": Vector3(-2, 0, 1.5), "out": Vector3(-1, 0, 0), "center": Vector3(-2, 0, 0)},
	"L": {"mesh": "roadCornerLarge", "entry": Vector3(-2, 0, 1.5), "in": Vector3(1, 0, 0),
			"exit": Vector3(-0.5, 0, 0), "out": Vector3(0, 0, -1), "center": Vector3(-2, 0, 0)},
}

## Distances along the path, in metres from its start (the start of the grid piece).
const FINISH := 36.0
## The opponents and the player on the grid: distance, lane (metres to the right).
const GRID := [[31.0, -2.2], [25.0, 2.2], [19.0, -2.2], [13.0, 2.2]]
const OPPONENTS := [["raceCarGreen", 30.5, 0.2, Color(0.3, 0.56, 0.43)], ["raceCarOrange", 29.8, 0.6, Color(0.96, 0.73, 0.26)],
		["raceCarWhite", 29.0, 0.9, Color(0.95, 0.95, 0.96)]]

var _root: Node3D
var _middle := PackedVector3Array()  # the middle of the road, tile units


func _init() -> void:
	_root = Node3D.new()
	_root.name = "Racing"
	_environment()
	_make_barrier()
	var track := _add(_root, Node3D.new(), "Track") as Node3D
	var walls := _add(_root, Node3D.new(), "Barriers") as Node3D
	_lay_track(track, walls)
	var path := _make_path()
	_decorate()
	var checkpoints := _checkpoints(path)
	var opponents := _opponents(path)
	_player(path, checkpoints, opponents)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote %s (lap %.0f m)" % [OUTPUT, path.curve.get_baked_length()])
	_root.free()
	quit(0 if status == OK else 1)


func _environment() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.25, 0.45, 0.85)
	sky_material.sky_horizon_color = Color(0.72, 0.82, 0.92)
	sky_material.ground_horizon_color = Color(0.6, 0.7, 0.62)
	sky_material.ground_bottom_color = Color(0.25, 0.4, 0.3)
	var sky := Sky.new()
	sky.sky_material = sky_material
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.45, 0.45, 0.45)
	environment.fog_enabled = true
	environment.fog_mode = Environment.FOG_MODE_DEPTH
	environment.fog_light_color = Color(0.72, 0.82, 0.92)
	environment.fog_density = 1.0
	environment.fog_depth_begin = 140.0
	environment.fog_depth_end = 300.0
	var world := _add(_root, WorldEnvironment.new(), "Environment") as WorldEnvironment
	world.environment = environment
	var sun := _add(_root, DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-55, 35, 0)

	# Grass under and around everything; the road tiles lie on it.
	var plane := PlaneMesh.new()
	plane.size = Vector2(560, 560)
	var grass := StandardMaterial3D.new()
	grass.albedo_color = Color(0.3, 0.56, 0.43)
	plane.material = grass
	var ground := _add(_root, MeshInstance3D.new(), "Ground") as MeshInstance3D
	ground.mesh = plane
	ground.position = Vector3(-55, -0.03, 50)


## Puts the road pieces down one after the other and the barriers along them.
func _lay_track(track: Node3D, walls: Node3D) -> void:
	var pos := Vector3.ZERO
	var dir := Vector3(0, 0, 1)
	var index := 0
	for c in TRACK:
		var piece: Dictionary = PIECES[c]
		var yaw := atan2(dir.x, dir.z) - atan2(piece["in"].x, piece["in"].z)
		var basis := Basis(Vector3.UP, yaw)
		var origin: Vector3 = pos - basis * piece["entry"]
		var road := _add(track, MeshInstance3D.new(), "Road%d" % index) as MeshInstance3D
		road.mesh = load(KIT % piece["mesh"])
		road.transform = Transform3D(basis.scaled(Vector3.ONE * TILE), origin * TILE)
		var exit: Vector3 = origin + basis * piece["exit"]
		var right := Vector3(-dir.z, 0, dir.x)
		if piece.has("center"):
			var center: Vector3 = origin + basis * piece["center"]
			_middle.append_array(_arc(center, pos, exit, 1.5, 8))
			# The outer barrier is on the side away from the centre, 2 tiles from it, the
			# inner one 1 tile from it.
			_arc_walls(walls, center, pos, exit, 2.0, 4, true)
			_arc_walls(walls, center, pos, exit, 1.0, 2, false)
		else:
			for k in piece["length"]:
				var start: Vector3 = pos + dir * k
				_middle.append(start)
				_wall(walls, start + dir + right * 0.5, dir, 1.0)
				_wall(walls, start - right * 0.5, -dir, 1.0)
		pos = exit
		dir = (basis * piece["out"]).normalized().round()
		index += 1
	if pos.length() > 0.001 or not dir.is_equal_approx(Vector3(0, 0, 1)):
		push_error("the track does not close: it ends at %s heading %s" % [pos, dir])


## Points on a quarter circle around `center` from `from` to `to` (tile units), without
## the last one, which the next piece adds.
func _arc(center: Vector3, from: Vector3, to: Vector3, radius: float, steps: int) -> PackedVector3Array:
	var a0 := atan2((from - center).z, (from - center).x)
	var a1 := atan2((to - center).z, (to - center).x)
	var sweep := wrapf(a1 - a0, -PI, PI)
	var points := PackedVector3Array()
	for i in steps:
		var a := a0 + sweep * i / steps
		points.append(center + Vector3(cos(a), 0, sin(a)) * radius)
	return points


func _arc_walls(walls: Node3D, center: Vector3, from: Vector3, to: Vector3, radius: float,
		steps: int, outer: bool) -> void:
	var a0 := atan2((from - center).z, (from - center).x)
	var a1 := atan2((to - center).z, (to - center).x)
	var sweep := wrapf(a1 - a0, -PI, PI)
	for i in steps:
		var p0 := center + Vector3(cos(a0 + sweep * i / steps), 0, sin(a0 + sweep * i / steps)) * radius
		var p1 := center + Vector3(cos(a0 + sweep * (i + 1) / steps), 0, sin(a0 + sweep * (i + 1) / steps)) * radius
		var u := (p1 - p0).normalized()
		var away := ((p0 + p1) * 0.5 - center).normalized() * (1.0 if outer else -1.0)
		# The barrier's thickness grows along its own Z, which must point away from the road.
		if Vector3(-u.z, 0, u.x).dot(away) < 0.0:
			_wall(walls, p0, -u, p0.distance_to(p1))
		else:
			_wall(walls, p1, u, p0.distance_to(p1))


## The barriers are one shape everywhere, so they share one mesh and one collision box.
## The mesh is deliberately plain: a front, a top and a back face in one strip, 8 corners,
## with red and white stripes from a small repeating texture. The kit's barrier has some
## 150 corners, and there are 80 barriers around the track.
const BARRIER_LENGTH := 12.0
const BARRIER_HEIGHT := 1.2
const BARRIER_DEPTH := 1.0
var _barrier_mesh: ArrayMesh
var _barrier_shape: BoxShape3D


func _make_barrier() -> void:
	# 8 x 8 pixels: a red and a white half, repeated three times along a barrier.
	var image := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	for y in 8:
		for x in 8:
			image.set_pixel(x, y, Color(0.85, 0.15, 0.15) if x < 4 else Color(0.95, 0.95, 0.95))
	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST

	# The profile from the road side over the top to the back; X runs along the barrier
	# from -BARRIER_LENGTH to 0, Z away from the road.
	var profile := [Vector2(0, 0), Vector2(BARRIER_HEIGHT, 0), Vector2(BARRIER_HEIGHT, BARRIER_DEPTH),
			Vector2(0, BARRIER_DEPTH)]
	var normals := [Vector3(0, 0, -1), Vector3(0, 0.7, -0.7), Vector3(0, 0.7, 0.7), Vector3(0, 0, 1)]
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	tool.set_material(material)
	for side in 3:
		var corners := []
		for k in [side, side + 1]:
			for end in [0.0, -BARRIER_LENGTH]:
				corners.append([Vector3(end, profile[k].x, profile[k].y), normals[k],
						Vector2(-end / BARRIER_LENGTH * 3.0, k / 3.0)])
		# Two triangles per face, clockwise seen from outside, as Godot wants its fronts.
		for index in [0, 2, 1, 1, 2, 3]:
			tool.set_normal(corners[index][1])
			tool.set_uv(corners[index][2])
			tool.add_vertex(corners[index][0])
	_barrier_mesh = tool.commit()
	_barrier_shape = BoxShape3D.new()
	_barrier_shape.size = Vector3(BARRIER_LENGTH, BARRIER_HEIGHT, BARRIER_DEPTH)


## One barrier: it runs from `end` back along `along` for `length` tiles.
func _wall(walls: Node3D, end: Vector3, along: Vector3, length: float) -> void:
	var barrier := _add(walls, StaticBody3D.new(), "Barrier%d" % walls.get_child_count()) as StaticBody3D
	var basis := Basis(along * length, Vector3.UP, Vector3(-along.z, 0, along.x))
	barrier.transform = Transform3D(basis, end * TILE)
	var mesh := _add(barrier, MeshInstance3D.new(), "Mesh") as MeshInstance3D
	mesh.mesh = _barrier_mesh
	var shape := _add(barrier, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = _barrier_shape
	shape.position = Vector3(-BARRIER_LENGTH * 0.5, BARRIER_HEIGHT * 0.5, BARRIER_DEPTH * 0.5)


func _make_path() -> Path3D:
	var curve := Curve3D.new()
	curve.bake_interval = 2.0
	for p in _middle:
		curve.add_point(p * TILE)
	curve.add_point(_middle[0] * TILE)  # closed
	var path := _add(_root, Path3D.new(), "RacePath") as Path3D
	path.curve = curve
	return path


func _follower(path: Path3D, node_name: String, progress: float, lane: float) -> PathFollow3D:
	var follow := _add(path, PathFollow3D.new(), node_name) as PathFollow3D
	follow.rotation_mode = PathFollow3D.ROTATION_Y
	follow.loop = true
	follow.progress = progress
	follow.h_offset = lane
	return follow


## Gates across the road, the first one at the finish line, the others spread over the lap.
func _checkpoints(path: Path3D) -> Node3D:
	var length := path.curve.get_baked_length()
	# A second path with the same curve, so that its only followers are the gates.
	var gates := _add(_root, Path3D.new(), "Checkpoints") as Path3D
	gates.curve = path.curve
	var shape := BoxShape3D.new()
	shape.size = Vector3(18, 6, 2)
	for i in 4:
		# The gates sit on followers of the path, so they turn with the road.
		var follow := _add(gates, PathFollow3D.new(), "Checkpoint%d" % i) as PathFollow3D
		follow.rotation_mode = PathFollow3D.ROTATION_Y
		follow.loop = true
		follow.progress = fmod(FINISH + length * i / 4.0, length)
		var gate := _add(follow, Area3D.new(), "Gate") as Area3D
		gate.monitoring = false
		gate.collision_layer = 2
		gate.collision_mask = 0
		var gate_shape := _add(gate, CollisionShape3D.new(), "Shape") as CollisionShape3D
		gate_shape.shape = shape
		gate_shape.position = Vector3(0, 3, 0)
	return gates


## Beyond this distance from the camera an opponent is drawn as a few boxes. The kit's car
## has 1430 triangles, which the Dreamcast feels four times over at the start.
const FAR_CAR := 20.0
var _far_cars := {}  # colour -> ArrayMesh


## A car made of boxes, in its own colour and black: body, side pods, wings and wheels.
## It looks along -Z like the kit's cars, in metres.
func _far_car(color: Color) -> ArrayMesh:
	if _far_cars.has(color):
		return _far_cars[color]
	var paint := StandardMaterial3D.new()
	paint.albedo_color = color
	var black := StandardMaterial3D.new()
	black.albedo_color = Color(0.2, 0.2, 0.22)
	var mesh := ArrayMesh.new()
	for part in [[paint, [[Vector3(0.8, 0.45, 3.2), Vector3(0, 0.5, 0)], [Vector3(1.5, 0.3, 1.2), Vector3(0, 0.4, 0.2)],
			[Vector3(1.6, 0.1, 0.45), Vector3(0, 0.95, 1.45)], [Vector3(1.7, 0.08, 0.4), Vector3(0, 0.22, -1.6)]]],
			[black, [[Vector3(0.35, 0.62, 0.62), Vector3(0.85, 0.31, 1.0)], [Vector3(0.35, 0.62, 0.62), Vector3(-0.85, 0.31, 1.0)],
			[Vector3(0.3, 0.5, 0.5), Vector3(0.8, 0.25, -1.05)], [Vector3(0.3, 0.5, 0.5), Vector3(-0.8, 0.25, -1.05)]]]]:
		var tool := SurfaceTool.new()
		tool.begin(Mesh.PRIMITIVE_TRIANGLES)
		for box: Array in part[1]:
			var cube := BoxMesh.new()
			cube.size = box[0]
			tool.append_from(cube, 0, Transform3D(Basis.IDENTITY, box[1]))
		tool.set_material(part[0])
		tool.commit(mesh)
	_far_cars[color] = mesh
	return mesh


func _car_body(parent: Node, mesh: String, color: Color, layer: int) -> Node3D:
	var body := _add(parent, StaticBody3D.new(), "Car") as StaticBody3D
	body.position = Vector3(0, 0.24, 0)
	body.collision_layer = layer
	var shape := _add(body, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var box := BoxShape3D.new()
	box.size = Vector3(1.8, 1.0, 3.3)
	shape.shape = box
	shape.position = Vector3(0, 0.55, 0)
	var model := _add(body, MeshInstance3D.new(), "Model") as MeshInstance3D
	model.mesh = load(KIT % mesh)
	# The cars of the kit look along -Z, as a follower of the path does.
	model.scale = Vector3.ONE * CAR_SCALE
	model.visibility_range_end = FAR_CAR
	var far := _add(body, MeshInstance3D.new(), "Far") as MeshInstance3D
	far.mesh = _far_car(color)
	far.visibility_range_begin = FAR_CAR
	return body


func _opponents(path: Path3D) -> Node3D:
	for i in OPPONENTS.size():
		var info: Array = OPPONENTS[i]
		var grid: Array = GRID[i]
		var follow := _follower(path, "Opponent%d" % i, grid[0], grid[1])
		follow.set_script(load("res://components/ai_driver.gd"))
		follow.speed = info[1]
		follow.character = info[2]
		_car_body(follow, info[0], info[3], 1)
	# The opponents are the only followers of the race path.
	return path


func _player(path: Path3D, checkpoints: Node3D, opponents: Node3D) -> void:
	var grid: Array = GRID[GRID.size() - 1]
	var player := _add(_root, CharacterBody3D.new(), "Player") as CharacterBody3D
	# The grid is on the first straight, which runs along +Z from the origin; right is -X.
	player.position = Vector3(-grid[1], 0.24, grid[0])
	var shape := _add(player, CollisionShape3D.new(), "Shape") as CollisionShape3D
	var box := BoxShape3D.new()
	box.size = Vector3(1.8, 1.0, 3.3)
	shape.shape = box
	shape.position = Vector3(0, 0.55, 0)
	var model := _add(player, MeshInstance3D.new(), "Model") as MeshInstance3D
	model.mesh = load(KIT % "raceCarRed")
	# The cars of the kit look along -Z; the player drives along +Z at the start.
	model.transform = Transform3D(Basis(Vector3.UP, PI).scaled(Vector3.ONE * CAR_SCALE), Vector3.ZERO)
	var hitbox := _add(player, Area3D.new(), "Hitbox") as Area3D
	hitbox.monitorable = false
	hitbox.collision_layer = 0
	hitbox.collision_mask = 2
	var hit_shape := _add(hitbox, CollisionShape3D.new(), "Shape") as CollisionShape3D
	hit_shape.shape = box
	hit_shape.position = Vector3(0, 0.55, 0)

	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.fov = 70.0
	camera.near = 0.5
	camera.far = 320.0
	camera.position = player.position + Vector3(0, 3.2, -8)
	camera.rotation_degrees = Vector3(-12, 180, 0)
	camera.current = true

	var countdown := _add(_root, Timer.new(), "Countdown") as Timer
	countdown.wait_time = 1.0
	countdown.autostart = true
	var finish := _add(_root, Timer.new(), "FinishTimer") as Timer
	finish.wait_time = 4.0
	finish.one_shot = true

	var music := _add(_root, AudioStreamPlayer.new(), "Music") as AudioStreamPlayer
	music.stream = load("res://assets/sounds/music.ogg")
	music.volume_db = -10.0
	music.autoplay = true

	var hud := _add(_root, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	# A dark strip along the bottom, so the small texts stay readable over the road.
	var strip := _add(hud, ColorRect.new(), "Strip") as ColorRect
	strip.position = Vector2(0, 436)
	strip.size = Vector2(640, 44)
	strip.color = Color(0, 0, 0, 0.45)
	var labels := {}
	for spec in [["Position", Vector2(16, 10), 24, "POS 4/4", 0], ["Lap", Vector2(16, 42), 16, "LAP 1/3", 0],
			["Time", Vector2(380, 10), 24, "0:00.00", 2], ["Best", Vector2(380, 42), 16, "BEST -:--.--", 2],
			["Speed", Vector2(380, 438), 24, "0 KM/H", 2], ["Message", Vector2(0, 170), 32, "3", 1],
			["Hint", Vector2(16, 438), 14, "Steer: arrows/stick  Gas: W/RT  Brake: S/LT  Back: Esc/B", 0]]:
		var label := _add(hud, Label.new(), spec[0]) as Label
		label.position = spec[1]
		label.size = Vector2(640 if spec[0] == "Message" else (400 if spec[0] == "Hint" else 244), 40)
		label.add_theme_font_size_override("font_size", spec[2])
		label.text = spec[3]
		label.horizontal_alignment = spec[4]
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		labels[spec[0]] = label

	player.set_script(load("res://components/race_car.gd"))
	player.laps = LAPS
	player.start_distance = grid[0]
	player.model_scale = CAR_SCALE
	player.model = model
	player.camera = camera
	player.path = path
	player.checkpoints = checkpoints
	player.opponents = opponents
	player.countdown = countdown
	player.finish_timer = finish
	player.lap_label = labels["Lap"]
	player.time_label = labels["Time"]
	player.best_label = labels["Best"]
	player.pos_label = labels["Position"]
	player.speed_label = labels["Speed"]
	player.message_label = labels["Message"]
	player.menu_scene = "res://scenes/main_menu.tscn"
	hitbox.area_entered.connect(Callable(player, "_on_hitbox_area_entered"), CONNECT_PERSIST)
	countdown.timeout.connect(Callable(player, "_on_countdown_timeout"), CONNECT_PERSIST)
	finish.timeout.connect(Callable(player, "_on_finish_timer_timeout"), CONNECT_PERSIST)


## Grandstands and the gantry at the finish, trees and tents around the track.
func _decorate() -> void:
	var deco := _add(_root, Node3D.new(), "Decoration") as Node3D
	for i in 3:
		_prop(deco, "grandStandCovered", Vector3(1.6, 0, 0.5 + i), PI * 0.5)
	_prop(deco, "overhead", Vector3(0.63, 0, FINISH / TILE), 0.0)
	_prop(deco, "flagCheckers", Vector3(0.75, 0, FINISH / TILE - 0.3), 0.0)
	_prop(deco, "flagCheckers", Vector3(-0.6, 0, FINISH / TILE - 0.3), 0.0)
	for spot in [Vector3(-4.5, 0, 3.5), Vector3(-6.5, 0, 2.0), Vector3(-5.0, 0, 1.0)]:
		_prop(deco, "tent", spot, 0.3)
	var random := RandomNumberGenerator.new()
	random.seed = 7
	var placed := 0
	while placed < 24:
		var spot := Vector3(random.randf_range(-15, 6), 0, random.randf_range(-7, 15))
		var clear := true
		for p in _middle:
			clear = clear and p.distance_to(spot) > 1.4
		if clear and not (spot.x > 0.8 and spot.z > -0.5 and spot.z < 4.0):  # the grandstands
			_prop(deco, "treeLarge" if random.randf() < 0.6 else "treeSmall", spot, random.randf() * TAU)
			placed += 1


func _prop(parent: Node, mesh: String, spot: Vector3, yaw: float) -> void:
	var prop := _add(parent, MeshInstance3D.new(), "%s%d" % [mesh, parent.get_child_count()]) as MeshInstance3D
	prop.mesh = load(KIT % mesh)
	prop.transform = Transform3D(Basis(Vector3.UP, yaw).scaled(Vector3.ONE * TILE), spot * TILE)
	if mesh.begins_with("tree"):
		# Trees far away are not drawn; the fog hides most of them there anyway.
		prop.visibility_range_end = 100.0


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
