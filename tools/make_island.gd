extends SceneTree
## Builds scenes/island.tscn, the terrain demo: an island made by the addon's HeightTerrain
## node from a noise, with the player of the 3D platformer on it. The result is an
## ordinary scene that can be opened and edited in Godot (select "Terrain" and change the
## noise, the size or the colours); running this tool again overwrites it.
##
##   godot --headless --path . --script res://tools/make_island.gd
##
## The player collects the coins and climbs to the flag on the highest hill. Water that
## is more than knee-deep sends it back. The logic is the platformer's
## (runtime/game/platformer_3d.c); what is new is the ground.

const OUTPUT := "res://scenes/island.tscn"
const KIT := "res://assets/kenney/platformer-kit/%s"
const HeightTerrainScript := preload("res://addons/retro_runtime/height_terrain.gd")

## Metres across, which is also the number of cells.
const SIZE := 64
## Metres per cell of the ground. Two triangles per cell: the Dreamcast feels every one.
const CELL := 2.0
const COINS := 12
const TREES := 10
const FLOWERS := 6
## Beyond this distance from the camera a tree is drawn as two boxes, and beyond TREE_END
## not at all. The kit's tree has some 400 triangles, and the Dreamcast feels every one.
const FAR_TREE := 22.0
const TREE_END := 40.0

var _root: Node3D
var _terrain: StaticBody3D
var _random := RandomNumberGenerator.new()


func _init() -> void:
	_random.seed = 11
	_root = Node3D.new()
	_root.name = "Island"
	_environment()
	_make_terrain()
	_water()

	var level := _add(_root, Node3D.new(), "Level") as Node3D
	var top := _highest()
	var start := _beach()
	_instance(level, "res://scenes/platformer/flag.tscn", "Flag", top)
	# Half way up, so a fall into the water does not cost the whole climb.
	_instance(level, "res://scenes/platformer/checkpoint.tscn", "Checkpoint", _on_ground(start.lerp(top, 0.5)))
	var coins := 0
	while coins < COINS:
		var spot := _spot(0.5, 6.5, 0.8)
		if spot.distance_to(top) > 3.0 and spot.distance_to(start) > 3.0:
			_instance(level, "res://scenes/platformer/coin.tscn", "Coin%d" % coins, spot + Vector3(0, 0.5, 0))
			coins += 1
	for i in TREES:
		var at := _spot(0.6, 5.0, 0.85)
		_prop(level, "tree.obj", "Tree%d" % i, at, FAR_TREE)
		_far_tree(level, "Tree%dFar" % i, at)
	for i in FLOWERS:
		_prop(level, "flowers.obj", "Flowers%d" % i, _spot(0.5, 4.0, 0.9), 18.0)
	_player(start)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote %s (start %.1f %.1f %.1f, flag %.1f %.1f %.1f)" % [OUTPUT, start.x, start.y, start.z,
				top.x, top.y, top.z])
	_root.free()
	quit(0 if status == OK else 1)


func _environment() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.22, 0.45, 0.85)
	sky_material.sky_horizon_color = Color(0.74, 0.86, 0.94)
	sky_material.ground_horizon_color = Color(0.74, 0.86, 0.94)
	sky_material.ground_bottom_color = Color(0.2, 0.42, 0.62)
	var sky := Sky.new()
	sky.sky_material = sky_material
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.45, 0.45, 0.45)
	# The fog hides where the sea ends.
	environment.fog_enabled = true
	environment.fog_mode = Environment.FOG_MODE_DEPTH
	environment.fog_light_color = Color(0.74, 0.86, 0.94)
	environment.fog_density = 1.0
	environment.fog_depth_begin = 25.0
	environment.fog_depth_end = 60.0
	var world := _add(_root, WorldEnvironment.new(), "Environment") as WorldEnvironment
	world.environment = environment
	var sun := _add(_root, DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-50, 40, 0)


## The ground: everything about it is a property of the HeightTerrain node, as it would
## be set in the inspector.
func _make_terrain() -> void:
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.seed = 7
	noise.frequency = 0.03
	noise.fractal_octaves = 3
	var colors := Gradient.new()
	# The sea is at height 0, a quarter of the way up from the lowest place.
	colors.offsets = PackedFloat32Array([0.0, 0.27, 0.34, 0.7, 0.92])
	colors.colors = PackedColorArray([Color(0.75, 0.68, 0.45), Color(0.9, 0.83, 0.58), Color(0.42, 0.68, 0.32),
			Color(0.28, 0.52, 0.26), Color(0.6, 0.58, 0.54)])

	_terrain = StaticBody3D.new()
	_terrain.set_script(HeightTerrainScript)
	_add(_root, _terrain, "Terrain")
	_terrain.noise = noise
	_terrain.cells = Vector2i(int(SIZE / CELL), int(SIZE / CELL))
	_terrain.cell_size = CELL
	_terrain.base_height = -3.5
	_terrain.height = 14.0
	_terrain.island = 1.0
	_terrain.chunk_cells = 8
	_terrain.texture = load("res://assets/sprites/ground.png")
	_terrain.texture_size = 4.0
	_terrain.colors = colors
	_terrain.rebuild()


## The sea, and below its surface an area that sends the player back.
func _water() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(400, 400)
	var blue := StandardMaterial3D.new()
	blue.albedo_color = Color(0.2, 0.5, 0.8, 0.7)
	blue.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	blue.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	plane.material = blue
	var water := _add(_root, MeshInstance3D.new(), "Water") as MeshInstance3D
	water.mesh = plane

	# The sea bed goes on where the ground ends, at its lowest height and in its lowest
	# colour, so the border of the ground does not show through the water.
	var bed_plane := PlaneMesh.new()
	bed_plane.size = Vector2(400, 400)
	var sand := StandardMaterial3D.new()
	sand.albedo_color = Color(0.75, 0.68, 0.45)
	bed_plane.material = sand
	var bed := _add(_root, MeshInstance3D.new(), "SeaBed") as MeshInstance3D
	bed.mesh = bed_plane
	bed.position.y = _terrain.base_height - 0.02

	var deep := _add(_root, Area3D.new(), "DeepWater") as Area3D
	deep.add_to_group("hazard", true)
	deep.monitoring = false
	deep.position = Vector3(0, -1.6, 0)
	var box := BoxShape3D.new()
	box.size = Vector3(400, 1.6, 400)
	var shape := _add(deep, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = box


func _player(start: Vector3) -> void:
	var box := BoxShape3D.new()
	box.size = Vector3(0.6, 0.9, 0.6)
	var player := _add(_root, CharacterBody3D.new(), "Player") as CharacterBody3D
	player.set_script(load("res://components/platform_player_3d.gd"))
	player.position = start + Vector3(0, 0.2, 0)
	var shape := _add(player, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = box
	shape.position = Vector3(0, 0.45, 0)
	var ray := _add(player, RayCast3D.new(), "ShadowRay") as RayCast3D
	ray.position = Vector3(0, 0.3, 0)
	ray.target_position = Vector3(0, -8, 0)
	var model := (load(KIT % "character-oobi.glb") as PackedScene).instantiate() as Node3D
	_add(player, model, "Model")
	_root.set_editable_instance(model, true)
	var hitbox := _add(player, Area3D.new(), "Hitbox") as Area3D
	hitbox.monitorable = false
	var hit_shape := _add(hitbox, CollisionShape3D.new(), "Shape") as CollisionShape3D
	hit_shape.shape = box
	hit_shape.position = Vector3(0, 0.45, 0)
	hitbox.area_entered.connect(Callable(player, "_on_hitbox_area_entered"), CONNECT_PERSIST)

	var shadow := _add(_root, Sprite3D.new(), "Shadow") as Sprite3D
	shadow.pixel_size = 0.028
	shadow.axis = Vector3.AXIS_Y
	shadow.texture = load("res://assets/sprites/shadow.png")

	# Above and behind the player, looking down at it. The player's component keeps this
	# distance, so the two must have the same parent.
	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.position = player.position + Vector3(0, 5.5, 8.0)
	camera.rotation_degrees = Vector3(-30, 0, 0)
	camera.current = true
	camera.fov = 55.0
	camera.near = 0.5
	camera.far = 60.0

	var hud := _add(_root, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	var coins := _label(hud, "Coins", Rect2(16, 10, 300, 28), 20, "COINS 0/0", HORIZONTAL_ALIGNMENT_LEFT)
	var status := _label(hud, "Status", Rect2(250, 10, 374, 28), 20, "LIVES 3  TIME 0.0", HORIZONTAL_ALIGNMENT_RIGHT)
	_label(hud, "Hint", Rect2(20, 452, 600, 22), 14, "Arrows or stick: move   Space or A: jump   Esc or B: back",
			HORIZONTAL_ALIGNMENT_CENTER)

	var sound := _add(_root, AudioStreamPlayer.new(), "CoinSound") as AudioStreamPlayer
	sound.stream = load("res://assets/sounds/beep.wav")

	# One burst of sparks serves all coins; the player moves it to the coin it took.
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

	player.run_speed = 5.0
	player.jump_speed = 7.5
	player.gravity = 20.0
	player.fall_limit = -6.0
	player.model = model
	player.anim = model.get_node("AnimationPlayer")
	player.camera = camera
	player.coin_label = coins
	player.coin_sound = sound
	player.coin_burst = burst
	player.shadow_ray = ray
	player.shadow = shadow
	player.status_label = status
	player.menu_scene = "res://scenes/main_menu.tscn"
	player.level = 1


func _label(parent: Control, node_name: String, rect: Rect2, font_size: int, text: String,
		alignment: HorizontalAlignment) -> Label:
	var label := _add(parent, Label.new(), node_name) as Label
	label.position = rect.position
	label.size = rect.size
	label.add_theme_font_size_override("font_size", font_size)
	label.text = text
	label.horizontal_alignment = alignment
	return label


## A place on the ground: (x, z) of `at` with the height of the ground there.
func _on_ground(at: Vector3) -> Vector3:
	return Vector3(at.x, _terrain.height_at(at.x, at.z), at.z)


## How flat the ground is at a place: the Y of its normal, 1 for level ground.
func _flatness(at: Vector3) -> float:
	var dx: float = _terrain.height_at(at.x + 0.5, at.z) - _terrain.height_at(at.x - 0.5, at.z)
	var dz: float = _terrain.height_at(at.x, at.z + 0.5) - _terrain.height_at(at.x, at.z - 0.5)
	return Vector3(-dx, 1.0, -dz).normalized().y


## A random place on the ground between two heights that is at least this flat.
func _spot(lowest: float, highest: float, flatness: float) -> Vector3:
	for attempt in 1000:
		var at := _on_ground(Vector3(_random.randf_range(-0.5, 0.5) * SIZE, 0, _random.randf_range(-0.5, 0.5) * SIZE))
		if at.y >= lowest and at.y <= highest and _flatness(at) >= flatness:
			return at
	push_error("the island has no place between %.1f and %.1f m" % [lowest, highest])
	return Vector3.ZERO


## The top of the highest hill.
func _highest() -> Vector3:
	var best := Vector3(0, -1000, 0)
	for z in range(-SIZE / 2, SIZE / 2 + 1):
		for x in range(-SIZE / 2, SIZE / 2 + 1):
			var at := _on_ground(Vector3(x, 0, z))
			if at.y > best.y:
				best = at
	return best


## Where the player starts: the first dry place coming in from the south, the side the
## camera is on.
func _beach() -> Vector3:
	for z in range(SIZE / 2, -SIZE / 2, -1):
		var at := _on_ground(Vector3(0, 0, z))
		if at.y > 0.5:
			return at
	return _on_ground(Vector3.ZERO)


func _instance(parent: Node, scene: String, node_name: String, at: Vector3) -> void:
	var node := (load(scene) as PackedScene).instantiate() as Node3D
	_add(parent, node, node_name)
	node.position = at


## Decoration. It is left out beyond `far` metres from the camera, and stands in one of
## four directions: the light is baked into a mesh, so every direction is a mesh of its own.
func _prop(parent: Node, mesh: String, node_name: String, at: Vector3, far: float) -> void:
	var prop := _add(parent, MeshInstance3D.new(), node_name) as MeshInstance3D
	prop.mesh = load(KIT % mesh)
	prop.position = at
	prop.rotation.y = _random.randi_range(0, 3) * PI * 0.5
	prop.visibility_range_end = far


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node


var _far_tree_mesh: ArrayMesh


## A tree for the distance: a box as trunk and a box as crown, in the colours of the kit's.
func _far_tree(parent: Node, node_name: String, at: Vector3) -> void:
	if _far_tree_mesh == null:
		_far_tree_mesh = ArrayMesh.new()
		for part: Array in [[Color(0.86, 0.52, 0.38), Vector3(0.3, 0.9, 0.3), 0.45],
				[Color(0.24, 0.62, 0.47), Vector3(1.0, 1.1, 1.0), 1.4]]:
			var paint := StandardMaterial3D.new()
			paint.albedo_color = part[0]
			var box := BoxMesh.new()
			box.size = part[1]
			var tool := SurfaceTool.new()
			tool.begin(Mesh.PRIMITIVE_TRIANGLES)
			tool.append_from(box, 0, Transform3D(Basis.IDENTITY, Vector3(0, part[2], 0)))
			tool.set_material(paint)
			tool.commit(_far_tree_mesh)
	var tree := _add(parent, MeshInstance3D.new(), node_name) as MeshInstance3D
	tree.mesh = _far_tree_mesh
	tree.position = at
	tree.visibility_range_begin = FAR_TREE
	tree.visibility_range_end = TREE_END
