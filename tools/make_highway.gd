extends SceneTree
## Builds the highway game from the Unity project "Highway Pursuit": its scenes Game.unity
## and TitleScreen.unity, written out by tools/unity/RetroSceneExport.cs (once, in Unity):
##
##   Unity.exe -batchmode -projectPath <copy> -executeMethod RetroSceneExport.Run
##       -exportScene Assets/Scenes/Game.unity -exportOut <repo>/build/highway_export -quit
##   (the same with TitleScreen.unity and build/highway_title_export)
##
##   godot --headless --path . --script res://tools/make_highway.gd
##
## Writes scenes/highway.tscn (the game), scenes/highway_title.tscn and the scenes that
## are instanced while the game runs: scenes/highway/road_segment.tscn and the traffic
## and rival cars. The logic is runtime/game/highway.c, in place of the C# scripts.
##
## The road: Unity copied the road object "Road" ahead whenever the player entered one;
## here the road segment is a PackedScene that HighwayRoad places one after another.
## The cars: Unity's rigid bodies were driven by setting their velocity and held at one
## height; here the police car is a CharacterBody3D against the rails and the others are
## areas moved by code, which is what the scripts did with them anyway.

const UnityImport := preload("res://tools/unity_import.gd")

const EXPORT_DIR := "build/highway_export"
const TITLE_EXPORT_DIR := "build/highway_title_export"
const ASSETS := "res://assets/highway"
const OUTPUT := "res://scenes/highway.tscn"
const TITLE := "res://scenes/highway_title.tscn"
const PARTS := "res://scenes/highway"
const MENU := "res://scenes/main_menu.tscn"

## The camera sees this far; the desert haze (fog) hides where the world ends.
const FAR := 230.0
## Static meshes of a road segment are joined into one mesh per this many metres of road
## and material, as Unity's MeshCombine script did: one draw instead of hundreds.
const MERGE_LENGTH := 30.0
## Half the width of the band along the road that is joined apart from the roadside.
const BAND_HALF_WIDTH := 12.0
## Scenery meshes with more vertices than this are simplified first (the Dreamcast draws
## some 12000 vertices of solid polygons a frame).
const DETAIL_LIMIT := 60
## The bridges carry the road: they keep more.
const BRIDGE_LIMIT := 200
## Thickness of the rails' collision boxes, metres.
const RAIL_THICKNESS := 8.0
## Surfaces left out of the scenery: the cacti's thorns, thousands of tiny translucent
## triangles that cannot be seen from the chase camera.
const DROP_MATERIALS := ["Thorn"]
## The title's camera looks along the road, so it sees much more of it: it sees less far.
const TITLE_FAR := 130.0
## Size of the glows that stand in for Unity's lens flares, metres across at full
## brightness: the police lights, and the title's, which fill the screen.
const POLICE_GLOW := 1.6
const TITLE_GLOW := 40.0
## How strongly they colour what is behind them at their middle.
const POLICE_GLOW_ALPHA := 0.8
const TITLE_GLOW_ALPHA := 0.5

const CAR_PREFABS := {"car": "CarD", "truck": "TruckAmericanA"}
const RIVAL_PREFAB := "EnemyCar"
const POOL := {"road": 3, "car": 6, "truck": 3, "rival": 5}
## Wrecks that can fly at the same time (rigid bodies; the runtime takes up to 64).
const WRECK_POOL := {"car": 3, "truck": 2, "rival": 3, "police": 1, "rival_red": 3}
## Top of the invisible ground the wrecks land on: just below the police car's body.
const FLOOR_TOP := -158.95

var unity := UnityImport.new()
var scene_root: Node3D
var nodes: Array
var made: Array
var _lod_cache := {}
var _wrecks := {}


func _init() -> void:
	unity.max_texture_size = 256
	# The street pack's atlas holds 64 pictures (the road with its markings among them):
	# at 256 pixels the lines are gone.
	unity.texture_sizes = {"atlas": 512}
	unity.ui_reference = Vector2(1280, 720)
	unity.ui_scale = 0.5
	# Only the rails and the cars keep colliders: the road and the scenery have mesh
	# colliders, which the runtime has not got, and the cars are held at one height.
	unity.keep_colliders = func(n: Dictionary) -> bool:
		return n.tag == "Guardrail" or n.name in ["PlayerCar", "NearMiss_Controller", RIVAL_PREFAB] \
				or n.name in CAR_PREFABS.values() or _on_ramp(n)
	unity.body_types = {"PlayerCar": "CharacterBody3D", RIVAL_PREFAB: "Area3D", "CarD": "Area3D",
			"TruckAmericanA": "Area3D"}
	if not unity.open(EXPORT_DIR, ASSETS) or not unity.prepare_files():
		quit(1)
		return
	_scissor_fences(unity)
	nodes = unity.data.nodes
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(PARTS))

	var segment := _save_segment()
	var cars := {}
	for key: String in CAR_PREFABS:
		cars[key] = _save_car(key, CAR_PREFABS[key])
	var rival := _save_rival()

	scene_root = Node3D.new()
	scene_root.name = "HighwayPursuit"
	_environment()
	made = unity.build_tree(nodes, scene_root, scene_root, _skip)
	_world_static(_node("World"))
	_flare_glows(POLICE_GLOW, POLICE_GLOW_ALPHA)
	var player := _player()
	# The ground of the start (the on-ramp and the road behind it) for the wrecks.
	_floor(scene_root, scene_root, Vector3(150.0, FLOOR_TOP - 1.0, 80.0), Vector2(140.0, 220.0))
	_camera(player)
	_hud(player)
	_jukebox()
	_road(player, segment, cars, rival)
	var status := _save(scene_root, OUTPUT)
	scene_root.free()

	if status == OK:
		status = _title(segment, cars)
	print("highway: %s (%d warnings)" % ["done" if status == OK else "failed", unity.warnings.size()])
	quit(0 if status == OK else 1)


func _save(root: Node, path: String) -> int:
	var scene := PackedScene.new()
	var status := scene.pack(root)
	if status == OK:
		status = ResourceSaver.save(scene, path)
	if status != OK:
		push_error("cannot save %s: %s" % [path, error_string(status)])
	return status


func _add(parent: Node, node: Node, node_name: String, owner: Node) -> Node:
	node.name = node_name
	parent.add_child(node, true)
	node.owner = owner
	return node


func _remove(node: Node) -> void:
	if node != null:
		node.get_parent().remove_child(node)
		node.free()


## The exported nodes below (and with) one node, as a tree of its own: the parents are
## renumbered and the top node stands at the origin.
func _subtree(list: Array, top: int) -> Array:
	var index := {top: 0}
	var out: Array = []
	var first: Dictionary = list[top].duplicate(true)
	first.parent = -1
	first.position = [0, 0, 0]
	first.rotation = [0, 0, 0, 1]
	out.append(first)
	for i in range(top + 1, list.size()):
		var up := int(list[i].parent)
		if index.has(up):
			var copy: Dictionary = list[i].duplicate(true)
			copy.parent = index[up]
			index[i] = out.size()
			out.append(copy)
	return out


func _index(name: String) -> int:
	for i in nodes.size():
		if nodes[i].name == name:
			return i
	return -1


## The Godot node of the first exported node with this name (below `under`, if given).
func _node(name: String, under: String = "") -> Node:
	var top := _index(under) if not under.is_empty() else -1
	for i in nodes.size():
		if nodes[i].name == name and made[i] != null and (top < 0 or _is_below(i, top)):
			return made[i]
	push_error("no node '%s' in the scene" % name)
	return null


func _is_below(i: int, ancestor: int) -> bool:
	var up := int(nodes[i].parent)
	while up >= 0:
		if up == ancestor:
			return true
		up = int(nodes[up].parent)
	return false


func _fields(node_name: String, script: String) -> Dictionary:
	var i := _index(node_name)
	if i < 0:
		return {}
	for comp: Dictionary in nodes[i].components:
		if comp.type == script:
			return comp.get("fields", {})
	return {}


func _sound(parent: Node, key: Variant, node_name: String, owner: Node, volume := 1.0, spatial := false) -> Node:
	var stream := unity.sound(key)
	if stream == null:
		return null
	var player: Node = AudioStreamPlayer3D.new() if spatial else AudioStreamPlayer.new()
	player.stream = stream
	player.volume_db = linear_to_db(volume)
	return _add(parent, player, node_name, owner)


# --- The road segment --------------------------------------------------------------------

## What of a segment stays out: Unity's triggers and markers for copying the road (the
## component does that), the respawn marker, and the wire fences beyond the rails
## (translucent, 32 to a bridge: too much for the Dreamcast).
func _segment_skip(_i: int, n: Dictionary) -> bool:
	return n.name in ["Enter", "Exit", "SpawnPos", "RespawnGO", "Fence_E", "LightShape", "Terrain", "Asphalt"]


func _save_segment() -> PackedScene:
	var top := _index("Road")
	var tree := _subtree(nodes, top)
	var holder := Node3D.new()
	var built: Array = unity.build_tree(tree, holder, holder, _segment_skip)
	var segment: Node3D = built[0]
	holder.remove_child(segment)
	holder.free()
	segment.name = "RoadSegment"
	_own_all(segment, segment)
	# Spawn markers: the debug cubes become Marker3D nodes, turned the way cars drive.
	var spawns := segment.get_node("TrafficSpawn")
	for cube in spawns.get_children():
		var marker := Marker3D.new()
		marker.name = cube.name
		marker.transform = Transform3D(cube.transform.basis.orthonormalized(), cube.transform.origin)
		cube.replace_by(marker)
		cube.free()
		marker.owner = segment
	# The rails: Unity's are 1 m thin and needed continuous collision detection at 150 m/s
	# (2.5 m a frame). Here their boxes reach 8 m outwards from the same inner face, so a
	# car cannot pass one in a frame.
	var rails := segment.find_children("*", "StaticBody3D", true, false).filter(
			func(b): return b.is_in_group("guardrail"))
	if rails.size() == 2:
		var middle: float = (rails[0].position.x + rails[1].position.x) * 0.5
		for rail: StaticBody3D in rails:
			var outward := signf(rail.position.x - middle)
			for shape in rail.get_children():
				if shape is CollisionShape3D and shape.shape is BoxShape3D:
					var box := shape.shape.duplicate() as BoxShape3D
					var thick := RAIL_THICKNESS - box.size.x
					box.size.x = RAIL_THICKNESS
					shape.shape = box
					shape.position.x += outward * thick * 0.5
	if rails.size() == 2:
		var rail_shape: CollisionShape3D = rails[0].get_child(0) if rails[0].get_child(0) is CollisionShape3D else null
		var length: float = (rail_shape.shape as BoxShape3D).size.z if rail_shape != null else 250.0
		var middle: float = (rails[0].position.x + rails[1].position.x) * 0.5
		var root_y: float = nodes[top].global_position[1]
		_floor(segment, segment, Vector3(middle, FLOOR_TOP - 1.0 - root_y, rails[0].position.z), Vector2(80.0, length + 10.0))
	_merge_static(segment)
	var status := _save(segment, PARTS.path_join("road_segment.tscn"))
	segment.free()
	return load(PARTS.path_join("road_segment.tscn")) if status == OK else null


func _own_all(node: Node, owner: Node) -> void:
	for child in node.get_children():
		child.owner = owner
		_own_all(child, owner)


## Joins the meshes of everything that does not move (no body, area or script on the way
## up) into one MeshInstance3D per material and stretch of road.
func _merge_static(segment: Node3D, owner: Node = null) -> void:
	if owner == null:
		owner = segment
	var groups := {}  # "<material id>/<stretch>" -> [SurfaceTool, material]
	var removed: Array[Node] = []
	for instance: MeshInstance3D in segment.find_children("*", "MeshInstance3D", true, false):
		if instance.mesh == null or not instance.visible or _moves(instance, segment):
			continue
		var place := _relative(instance, segment)
		var mesh: Mesh = instance.mesh
		if String(instance.name).begins_with("StreetRail"):
			# A guard rail seen from above is a strip: a box of its size in its colour (its
			# model has some 400 vertices and does not simplify far).
			if not _lod_cache.has(mesh):
				_lod_cache[mesh] = _rail_box(mesh)
			mesh = _lod_cache[mesh]
		var limit := BRIDGE_LIMIT if String(instance.name).begins_with("Bridge") else DETAIL_LIMIT
		if _vertices(mesh) > limit:
			# Scenery made for a PC: far fewer triangles, from Godot's LOD generator.
			if not _lod_cache.has(mesh):
				_lod_cache[mesh] = unity.make_lod(mesh, float(limit) / _vertices(mesh))
			mesh = _lod_cache[mesh]
		var stretch := int(floor(-place.origin.z / MERGE_LENGTH))
		# Left of the road, the road, right of it: small pieces, so few contain the camera
		# (those are clipped against its near plane, which costs the Dreamcast four times).
		var band := -1 if place.origin.x < -BAND_HALF_WIDTH else (1 if place.origin.x > BAND_HALF_WIDTH else 0)
		for s in mesh.get_surface_count():
			var material := mesh.surface_get_material(s)
			if material != null and material.resource_name in DROP_MATERIALS:
				continue
			var key := "%d/%d/%d" % [material.get_instance_id() if material != null else 0, stretch, band]
			if not groups.has(key):
				var tool := SurfaceTool.new()
				tool.begin(Mesh.PRIMITIVE_TRIANGLES)
				groups[key] = [tool, material, stretch]
			(groups[key][0] as SurfaceTool).append_from(mesh, s, place)
		removed.append(instance)
	for instance in removed:
		if instance.get_child_count() == 0:
			_remove(instance)
		else:
			# Its children stay; it becomes a plain node.
			var plain := Node3D.new()
			plain.name = instance.name
			plain.transform = instance.transform
			instance.replace_by(plain)
			instance.free()
			plain.owner = owner
	var holder := _add(segment, Node3D.new(), "Scenery", owner)
	for key: String in groups:
		var tool: SurfaceTool = groups[key][0]
		tool.set_material(groups[key][1])
		# With Godot's LOD levels: the far stretches are drawn simpler.
		var mesh := unity.with_lods(tool.commit())
		var piece := _add(holder, MeshInstance3D.new(), "Piece_%s" % key.replace("/", "_"), owner) as MeshInstance3D
		piece.mesh = mesh
		piece.visibility_range_end = FAR


func _moves(node: Node, top: Node) -> bool:
	var at := node
	while at != null and at != top:
		if at is CollisionObject3D and not (at is StaticBody3D) or at.get_script() != null:
			return true
		at = at.get_parent()
	return false


func _relative(node: Node3D, top: Node3D) -> Transform3D:
	var t := node.transform
	var at := node.get_parent()
	while at != null and at != top:
		t = (at as Node3D).transform * t
		at = at.get_parent()
	return t


# --- Cars --------------------------------------------------------------------------------

## What of a car stays out: the headlight glow (particles in local space), the per-car
## engine sounds (the Dreamcast has 64 voices for everything), the showroom platform and
## the name plate of the car pack.
func _car_skip(_i: int, n: Dictionary) -> bool:
	return n.name in ["Glow", "GameObject", "CarEngine_Snd", "Platform06", "carName"]


func _build_prefab(key: String) -> Area3D:
	var holder := Node3D.new()
	var built: Array = unity.build_tree(unity.data.prefabs[key].nodes, holder, holder, _car_skip)
	var car: Area3D = built[0]
	holder.remove_child(car)
	holder.free()
	car.transform = Transform3D.IDENTITY
	_own_all(car, car)
	car.monitoring = true
	car.monitorable = true
	return car


## Half the width and length of the box of a car's collision shape.
func _footprint(car: Node) -> Vector2:
	for child in car.get_children():
		if child is CollisionShape3D and child.shape is BoxShape3D:
			return Vector2(child.shape.size.x, child.shape.size.z) * 0.5
	return Vector2(1.2, 2.5)


func _save_car(kind: String, key: String) -> PackedScene:
	var car := _build_prefab(key)
	var info: Dictionary = {}
	for comp: Dictionary in unity.data.prefabs[key].nodes[0].components:
		if comp.type == "TrafficVehicle":
			info = comp.fields
	car.set_script(load("res://components/highway_car.gd"))
	car.add_to_group("traffic", true)
	car.add_to_group("vehicle", true)
	var size := _footprint(car)
	car.half_width = size.x
	car.half_length = size.y
	car.cruise = info.get("v_speed", 50.0)
	car.accel = info.get("accel", 10.0)
	car.brake = info.get("brake", 25.0)
	car.wreck_kind = 0 if kind == "car" else 1
	car.area_entered.connect(Callable(car, "_on_area_entered"), CONNECT_PERSIST)
	car.body_entered.connect(Callable(car, "_on_body_entered"), CONNECT_PERSIST)
	var path := PARTS.path_join("traffic_%s.tscn" % kind)
	var status := _save(car, path)
	car.free()
	if status == OK:
		_save_wreck(path, kind, 5.0)
	return load(path) if status == OK else null


func _save_rival() -> PackedScene:
	var car := _build_prefab(RIVAL_PREFAB)
	var info: Dictionary = {}
	for comp: Dictionary in unity.data.prefabs[RIVAL_PREFAB].nodes[0].components:
		if comp.type == "RivalCar":
			info = comp.fields
	car.set_script(load("res://components/highway_rival.gd"))
	car.add_to_group("rival", true)
	car.add_to_group("vehicle", true)
	var size := _footprint(car)
	car.half_width = size.x
	car.half_length = size.y
	car.wreck_kind = 2
	for field in ["top_speed", "accel", "brake", "pass_margin", "flee_margin", "takedown_impact", "grind_time"]:
		if info.has(field):
			car.set(field, info[field])
	# The explosion at a takedown: a few of its particle systems, fired once.
	var explosion: Node3D = car.find_child("Explosion", true, false)
	if explosion != null:
		_trim_particles(explosion, 3, 16)
		explosion.visible = false
	car.explosion = explosion
	car.indicator = car.find_child("down-arrow", true, false)
	# Unity painted the real rivals by swapping the texture of the body; here there is a
	# second body in that texture, and the component shows one of the two.
	var body: MeshInstance3D = car.find_child("Body", true, false)
	var red: Variant = info.get("texture")
	if body != null and red is Dictionary and unity.texture(red.texture) != null:
		var paint := body.duplicate() as MeshInstance3D
		paint.name = "BodyRed"
		var mesh := body.mesh.duplicate() as ArrayMesh
		for s in mesh.get_surface_count():
			var material := (mesh.surface_get_material(s) as StandardMaterial3D).duplicate() as StandardMaterial3D
			material.albedo_texture = unity.texture(red.texture)
			mesh.surface_set_material(s, material)
		paint.mesh = mesh
		body.get_parent().add_child(paint)
		paint.owner = car
		car.body = body
		car.paint = paint
	var crash: Variant = info.get("crash_snd")
	if crash is Dictionary:
		car.crash_sound = _sound(car, crash.audio, "CrashSound", car, 0.75, true)
	car.area_entered.connect(Callable(car, "_on_area_entered"), CONNECT_PERSIST)
	car.area_exited.connect(Callable(car, "_on_area_exited"), CONNECT_PERSIST)
	var path := PARTS.path_join("rival.tscn")
	var status := _save(car, path)
	car.free()
	if status == OK:
		_save_wreck(path, "rival", 5.0)
		_save_wreck(path, "rival_red", 5.0, "BodyRed")
	return load(path) if status == OK else null


func _trim_particles(holder: Node, systems: int, particles: int) -> void:
	var kept := 0
	for p: CPUParticles3D in holder.find_children("*", "CPUParticles3D", true, false) + \
			([holder] if holder is CPUParticles3D else []):
		if not is_instance_valid(p):
			continue
		if not p.visible or kept >= systems:
			if p != holder:
				_remove(p)
			continue
		kept += 1
		p.amount = mini(p.amount, particles)
		p.one_shot = true
		p.emitting = false
		p.lifetime = minf(p.lifetime, 2.0)
	for audio in holder.find_children("*", "AudioStreamPlayer*", true, false):
		_remove(audio)


func _vertices(mesh: Mesh) -> int:
	var count := 0
	for s in mesh.get_surface_count():
		count += (mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()
	return count


# --- The game scene ----------------------------------------------------------------------

## What of the game scene stays out: Unity's event system, a spare camera, a switched-off
## bridge, the road (a PackedScene now), the rival placed in the scene (HighwayRoad brings
## them), the lens flare, an out-of-bounds trigger, the FPS counter and the demo logos,
## and the parts of cars named in _car_skip.
func _skip(i: int, n: Dictionary) -> bool:
	if n.name in ["EventSystem", "Camera", "Road", RIVAL_PREFAB, "FlareMobile", "OOB_Cube", "FPS", "Logo",
			"Logo_Demo", "EnemyTargetPoint", "Crashed", "Fence_E", "LightShape"]:
		return true
	if n.name == "GameObject (off)" or (n.name == "GameObject" and not n.active and int(n.parent) == -1):
		return true
	return _car_skip(i, n) and n.name != "GameObject"


func _environment() -> void:
	var render: Dictionary = unity.data.render
	var env := Environment.new()
	# The desert haze is the background too: the sky hardly shows from the chase camera.
	env.background_mode = Environment.BG_COLOR
	env.background_color = unity._color(render.fog_color)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	# Brighter than Unity's ambient: there the cars and the road shone with reflections,
	# which baked light has not got.
	env.ambient_light_color = Color(0.78, 0.74, 0.68)
	env.fog_enabled = true
	env.fog_light_color = unity._color(render.fog_color)
	env.fog_mode = Environment.FOG_MODE_DEPTH
	env.fog_density = 1.0
	env.fog_depth_begin = 80.0
	env.fog_depth_end = FAR
	var world := WorldEnvironment.new()
	world.environment = env
	_add(scene_root, world, "Environment", scene_root)


func _player() -> CharacterBody3D:
	var player := _node("PlayerCar") as CharacterBody3D
	var f := _fields("PlayerCar", "PlayerCar")
	player.set_script(load("res://components/highway_player.gd"))
	player.add_to_group("vehicle", true)
	# The body: a sphere as wide as the car keeps it off the rails whichever way it turns;
	# the box of the Unity collider becomes the hitbox.
	var box: CollisionShape3D = null
	for child in player.get_children():
		if child is CollisionShape3D:
			box = child
	var size := Vector3(2.7, 1.8, 4.6)
	if box != null and box.shape is BoxShape3D:
		size = box.shape.size
		box.reparent(_add(player, Area3D.new(), "Hitbox", scene_root), false)
		box.owner = scene_root
	var sphere := CollisionShape3D.new()
	var round_shape := SphereShape3D.new()
	round_shape.radius = size.x * 0.5
	sphere.shape = round_shape
	_add(player, sphere, "Body", scene_root)
	var hitbox := player.get_node("Hitbox") as Area3D
	hitbox.monitorable = true
	var near_miss := _node("NearMiss_Controller") as Area3D
	near_miss.name = "NearMiss"
	near_miss.monitorable = false
	for child in near_miss.get_children():
		if child is MeshInstance3D:
			_remove(child)
	player.half_width = size.x * 0.5
	player.half_length = size.z * 0.5
	player.max_speed = 120.0
	player.boost_speed = 150.0
	# Livelier than the Unity script (2 s, 50 degrees per second), which felt sluggish.
	player.accel_time = 1.2
	player.turn_rate = 65.0
	player.lives = 3
	player.wall_crash_angle = f.get("wall_crash_angle", 55.0)
	player.wall_speed_loss = f.get("wall_speed_loss", 0.6)
	# The body must not turn (its shape stays square to the world): what turns with the car
	# goes below "Turn", which the component turns.
	var turn := Node3D.new()
	turn.name = "Turn"
	turn.transform = Transform3D(player.transform.basis, Vector3.ZERO)
	for child in player.get_children():
		if not (child is CollisionShape3D or child is Area3D or child.name.ends_with("_Sound")):
			child.reparent(turn, false)
	player.add_child(turn)
	_own_all(turn, scene_root)
	turn.owner = scene_root
	player.transform = Transform3D(Basis.IDENTITY, player.transform.origin)
	player.turn = turn
	player.wreck_kind = 3
	_save_police_wreck(turn, box)
	var sounds := _add(scene_root, Node.new(), "Sounds", scene_root)
	player.crash_sound = _sound(sounds, f.get("crash_snd", {}).get("audio"), "Crash", scene_root, 0.5)
	player.boost_sound = _sound(sounds, f.get("boost_snd", {}).get("audio"), "Boost", scene_root, 0.5)
	player.takedown_sound = _sound(sounds, f.get("takedown_snd", {}).get("audio"), "Takedown", scene_root, 0.4)
	var miss := _fields("NearMiss_Controller", "NearMissController")
	player.near_miss_sound = _sound(sounds, miss.get("nearmiss_snd", {}).get("audio"), "NearMiss", scene_root, 0.3)
	var rival := {}
	for comp: Dictionary in unity.data.prefabs[RIVAL_PREFAB].nodes[0].components:
		if comp.type == "RivalCar":
			rival = comp.fields
	player.impact_sound = _sound(sounds, rival.get("impact_snd", {}).get("audio"), "Impact", scene_root)
	var engine := _node("Engine_Sound")
	player.engine_sound = engine.get_node_or_null("AudioSource") if engine != null else null
	player.title_scene = TITLE
	hitbox.area_entered.connect(Callable(player, "_on_hitbox_area_entered"), CONNECT_PERSIST)
	near_miss.area_exited.connect(Callable(player, "_on_near_miss_area_exited"), CONNECT_PERSIST)
	return player


func _camera(player: Node3D) -> void:
	var camera := _node("Main Camera") as Camera3D
	var f := _fields("Main Camera", "ChaseCamera")
	camera.set_script(load("res://components/chase_camera.gd"))
	camera.current = true
	camera.far = FAR
	camera.near = 1.0
	camera.target = player
	camera.height = f.get("y_offset", 25.0)
	camera.back = f.get("z_offset", 15.0)


## The HUD: the parts the car hides when it is wrecked go into one control.
func _hud(player: Node) -> void:
	var canvas := _node("Canvas")
	var hud := _add(canvas, Control.new(), "HUD", scene_root) as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	hud.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for part in ["Speedometer", "Score", "Takedown", "Boostbar"]:
		var node := _node(part, "Canvas")
		if node != null:
			node.reparent(hud, false)
			node.owner = scene_root
			_own_all(node, scene_root)
	player.hud = hud
	player.speed_label = _node("Value", "Speedometer")
	player.score_label = _node("Value", "Score")
	player.takedown_label = _node("Value", "Takedown")
	player.boost_bar = _node("Image (4)", "Boostbar")
	(player.boost_bar as TextureProgressBar).value = 0.0
	player.damage_label = _node("DamageCritical")
	var over := _node("GameOverScreen") as Control
	over.visible = false
	# Unity laid it over its 16:9 screen: here it covers the whole 4:3 one (controls cannot
	# be scaled in the runtime), and the texts at its top come down to the middle.
	over.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for name in ["Text", "GameOver"]:
		var label := _node(name, "GameOverScreen") as Control
		if label != null:
			label.offset_top += 30.0
			label.offset_bottom += 30.0
	player.game_over = over
	player.result_label = _node("Text", "GameOverScreen")
	(_node("Text (Legacy)", "GameOverScreen") as Label).text = "PRESS START TO CONTINUE"


## The jukebox gets one AudioStreamPlayer per piece, named "Artist - Title".
func _jukebox() -> void:
	var holder := _node("Jukebox")
	var f := _fields("Jukebox", "Jukebox")
	var jukebox := Node.new()
	jukebox.name = "Jukebox"
	holder.replace_by(jukebox)
	holder.free()
	jukebox.owner = scene_root
	jukebox.set_script(load("res://components/jukebox.gd"))
	var clips: Array = f.get("music", [])
	var titles: Array = f.get("music_id", [])
	var artists: Array = f.get("artist_id", [])
	for k in clips.size():
		if not (clips[k] is Dictionary):
			continue
		var stream := unity.sound(clips[k].audio)
		if stream == null:
			continue
		var player := AudioStreamPlayer.new()
		player.stream = stream
		player.volume_db = linear_to_db(0.5)
		var song := "%s - %s" % [artists[k] if k < artists.size() else "?", titles[k] if k < titles.size() else "?"]
		_add(jukebox, player, song.replace(".", "").replace(":", "").replace("&", "and"), scene_root)
	jukebox.artist_label = _node("Artist")
	jukebox.title_label = _node("Song")
	for i in nodes.size():
		if nodes[i].name == "Jukebox" and int(nodes[i].parent) >= 0 and made[i] != null:
			jukebox.panel = made[i]
	jukebox.show_time = 6.0


func _road(player: Node3D, segment: PackedScene, cars: Dictionary, rival: Variant) -> void:
	var road_index := _index("Road")
	var first := _add(scene_root, Marker3D.new(), "FirstSegment", scene_root) as Marker3D
	var at: Array = nodes[road_index].global_position
	first.position = Vector3(at[0], at[1], at[2])
	var road := _add(scene_root, Node.new(), "HighwayRoad", scene_root)
	road.set_script(load("res://components/highway_road.gd"))
	road.player = player
	road.first = first
	# Unity put each copy at the marker "SpawnPos" of the one before.
	var spawn: Array = nodes[_index("SpawnPos")].global_position
	road.segment_length = absf(at[2] - spawn[2])
	road.ahead = 1
	road.road = segment
	road.road_max = POOL.road
	road.car = cars.car
	road.car_max = POOL.car
	road.truck = cars.truck
	road.truck_max = POOL.truck
	if rival != null:
		road.rival = rival
		road.rival_max = POOL.rival
	for kind in WRECK_POOL:
		if _wrecks.has(kind) and rival != null:
			road.set("wreck_" + kind, _wrecks[kind])
			road.set("wreck_" + kind + "_max", WRECK_POOL[kind])


# --- The title -----------------------------------------------------------------------------

## The title: the road seen from a car that drives on by itself (Unity's ForwardDummy)
## with traffic, the logo over it, the jukebox. Unity's post-processing turned the picture
## red; a translucent red rectangle over the picture does that here.
func _title(segment: PackedScene, cars: Dictionary) -> int:
	var title := UnityImport.new()
	title.max_texture_size = 512
	title.texture_sizes = unity.texture_sizes
	title.ui_reference = unity.ui_reference
	title.ui_scale = unity.ui_scale
	title.keep_colliders = unity.keep_colliders
	title.body_types = unity.body_types
	if not title.open(TITLE_EXPORT_DIR, ASSETS) or not title.prepare_files():
		return FAILED
	# The helpers of the game scene work on these.
	unity = title
	_scissor_fences(title)
	nodes = title.data.nodes
	scene_root = Node3D.new()
	scene_root.name = "HighwayTitle"
	_environment()
	made = title.build_tree(nodes, scene_root, scene_root, _title_skip)
	_world_static(_node("World"))

	var cube := _node("Cube")
	var dummy := Node3D.new()
	dummy.name = "CameraCar"
	dummy.transform = cube.transform
	cube.replace_by(dummy)
	cube.free()
	dummy.owner = scene_root
	dummy.set_script(load("res://components/forward_dummy.gd"))
	dummy.speed = _fields("Cube", "ForwardDummy").get("v_speed", 50.0)
	var camera := _node("Main Camera") as Camera3D
	camera.current = true
	camera.far = TITLE_FAR
	camera.near = 1.0
	var env := (scene_root.get_node("Environment") as WorldEnvironment).environment
	env.fog_depth_begin = 40.0
	env.fog_depth_end = TITLE_FAR
	_jukebox()
	_road(dummy, segment, cars, null)

	# The red and blue light that washes over the title: the camera car's two lens flares.
	_flare_glows(TITLE_GLOW, TITLE_GLOW_ALPHA)

	var manager := _node("GameManager")
	var screen := Node.new()
	screen.name = "TitleScreen"
	manager.replace_by(screen)
	manager.free()
	screen.owner = scene_root
	screen.set_script(load("res://components/title_screen.gd"))
	var prompt := _node("Start Button UI") as Label
	prompt.text = "PRESS START"
	screen.prompt = prompt
	screen.game_scene = OUTPUT
	screen.menu_scene = MENU
	var status := _save(scene_root, TITLE)
	scene_root.free()
	return status


## What of the title stays out: as in the game scene, plus Unity's UI camera and the
## camera car's point lights (switched off in Unity).
func _title_skip(_i: int, n: Dictionary) -> bool:
	return n.name in ["EventSystem", "FlareMobile", "FPS", "Logo_Demo", "Road", "Camera", "Fence_E",
			"LightShape", "Point Light", "Point Light (1)"] \
			or (n.name == "GameObject" and not n.active)


## The rails of the on-ramp where the police car starts keep it on the ramp, as in Unity.
func _on_ramp(n: Dictionary) -> bool:
	if not String(n.name).begins_with("StreetRail"):
		return false
	var list: Array = unity.data.nodes
	var up := int(n.parent)
	while up >= 0 and up < list.size():
		if list[up].name == "Bridge_ToStreet_Double_Exit":
			return true
		up = int(list[up].parent)
	return false


## The static world of a scene (Unity's "World": the on-ramp, the road behind the start)
## joined and simplified like a road segment.
func _world_static(world: Node3D) -> void:
	if world != null:
		_merge_static(world, scene_root)


## The rails' material was a multiplying particle shader with holes in its texture: punched
## through here (the Dreamcast's punch-through list), not blended.
func _scissor_fences(importer: RefCounted) -> void:
	for key in ["Fence"]:
		var m := importer.material(key) as StandardMaterial3D
		if m != null:
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
			m.alpha_scissor_threshold = 0.5
			m.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL


## Unity's lens flares that the Flare script swings (the police lights, red and blue in
## turn): Godot has none, so each becomes a glow, a billboard Sprite3D in the flare's
## colour `size` metres across, with the Flare component making it grow and shrink as the
## flare's brightness did.
func _flare_glows(size: float, alpha: float) -> void:
	for i in mini(nodes.size(), made.size()):
		if not is_instance_valid(made[i]) or not (made[i] is Node3D):
			continue
		var node: Node3D = made[i]
		var flare := {}
		var lens := {}
		for comp: Dictionary in nodes[i].components:
			if comp.type == "Flare":
				flare = comp.get("fields", {})
			elif comp.type == "LensFlare":
				lens = comp.get("fields", {})
		if flare.is_empty() or lens.is_empty():
			continue
		var glow := Sprite3D.new()
		glow.name = node.name
		glow.texture = _glow_texture()
		glow.pixel_size = size / glow.texture.get_width()
		glow.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		var c: Array = lens.get("m_Color", [1.0, 1.0, 1.0, 1.0])
		glow.modulate = Color(c[0], c[1], c[2], alpha)
		glow.transform = node.transform
		glow.set_script(load("res://components/flare.gd"))
		glow.start_range = float(flare.get("startRange", 10.0))
		glow.end_range = float(flare.get("endRange", 50.0))
		glow.speed = float(flare.get("speed", 20.0))
		glow.start_first = bool(flare.get("startFirst", true))
		node.replace_by(glow)
		node.free()
		glow.owner = scene_root
		made[i] = glow


var _glow: Texture2D

## A round glow, white, fading out to the edge.
func _glow_texture() -> Texture2D:
	if _glow == null:
		var path := ASSETS.path_join("textures/glow.res")
		var image := Image.create(64, 64, false, Image.FORMAT_RGBA8)
		for y in 64:
			for x in 64:
				var d := Vector2(x - 31.5, y - 31.5).length() / 32.0
				image.set_pixel(x, y, Color(1.0, 1.0, 1.0, pow(clampf(1.0 - d, 0.0, 1.0), 2.0)))
		var texture := ImageTexture.create_from_image(image)
		ResourceSaver.save(texture, path)
		texture.take_over_path(path)
		_glow = texture
	return _glow


var _rail_material: StandardMaterial3D

func _rail_box(mesh: Mesh) -> Mesh:
	if _rail_material == null:
		_rail_material = StandardMaterial3D.new()
		_rail_material.resource_name = "RailColour"
		_rail_material.albedo_color = Color(0.62, 0.55, 0.38)
	var aabb := mesh.get_aabb()
	var box := BoxMesh.new()
	box.size = aabb.size
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	tool.append_from(box, 0, Transform3D(Basis.IDENTITY, aabb.get_center()))
	tool.set_material(_rail_material)
	return tool.commit()


# --- Wrecks and the ground ---------------------------------------------------------------

## A rigid body with the car's model: the wreck that flies when the car is wrecked (Unity
## let the car's own rigid body loose). Made from the saved car scene; the rival's
## explosion and crash sound come along. A rival's wreck keeps one of its two bodies:
## `paint` "BodyRed" for the real rivals' red one, else the dummies' "Body".
func _save_wreck(car_path: String, kind: String, lifetime: float, paint := "Body") -> void:
	var car: Node3D = (load(car_path) as PackedScene).instantiate()
	var wreck := _wreck_body()
	for child in car.get_children():
		car.remove_child(child)
		wreck.add_child(child)
	if paint == "BodyRed":
		var red := wreck.find_child("BodyRed", true, false) as Node3D
		if red != null:
			red.visible = true
		_remove(wreck.find_child("Body", true, false))
	for unwanted in ["down-arrow", "BodyRed" if paint == "Body" else ""]:
		if unwanted == "":
			continue
		var node := wreck.find_child(unwanted, true, false)
		if node != null:
			_remove(node)
	_own_all(wreck, wreck)
	var burst: Node3D = wreck.find_child("Explosion", true, false)
	if burst != null:
		burst.visible = true
	wreck.burst = burst
	wreck.sound = wreck.find_child("CrashSound", true, false)
	wreck.lifetime = lifetime
	wreck.radius = float(car.get("half_width")) if car.get("half_width") != null else 1.5
	var path := PARTS.path_join("wreck_%s.tscn" % kind)
	if _save(wreck, path) == OK:
		_wrecks[kind] = load(path)
	wreck.free()
	car.free()


func _wreck_body() -> RigidBody3D:
	var wreck := RigidBody3D.new()
	wreck.name = "Wreck"
	# Unity's CollisionParticle made a wrecked car heavy (crashMass) and gave it the
	# CrashPhys material.
	wreck.mass = 100.0
	wreck.linear_damp = 0.1
	wreck.angular_damp = 0.3
	var material := PhysicsMaterial.new()
	material.friction = 0.6
	material.bounce = 0.15
	wreck.physics_material_override = material
	wreck.set_script(load("res://components/highway_wreck.gd"))
	return wreck


## The police car's wreck: its model (what turns with it) and the box of its hitbox.
func _save_police_wreck(turn: Node3D, hitbox: CollisionShape3D) -> void:
	var wreck := _wreck_body()
	var model := turn.duplicate() as Node3D
	model.transform = Transform3D.IDENTITY
	wreck.add_child(model)
	var shape := hitbox.duplicate() as CollisionShape3D
	shape.position = Vector3(0, shape.position.y, 0)
	wreck.add_child(shape)
	_own_all(wreck, wreck)
	wreck.lifetime = 7.0
	var path := PARTS.path_join("wreck_police.tscn")
	if _save(wreck, path) == OK:
		_wrecks["police"] = load(path)
	wreck.free()


## Invisible ground for the wrecks to land on (Unity's road and desert had mesh colliders;
## the driving cars need none, they keep their height). Its top is just below the police
## car's body, which therefore never touches it.
func _floor(parent: Node3D, owner: Node, center: Vector3, size: Vector2) -> void:
	var ground := _add(parent, StaticBody3D.new(), "Floor", owner) as StaticBody3D
	var shape := _add(ground, CollisionShape3D.new(), "Shape", owner) as CollisionShape3D
	var box := BoxShape3D.new()
	box.size = Vector3(size.x, 2.0, size.y)
	shape.shape = box
	ground.position = center
