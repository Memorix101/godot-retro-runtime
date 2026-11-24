extends SceneTree
## Builds scenes/airplane.tscn from the Unity prototype "Airplane Arcade" (2019): its
## scene Game1.unity, written out by tools/unity/RetroSceneExport.cs. The Unity side once:
##
##   Unity.exe -batchmode -projectPath <copy of the project> -executeMethod RetroSceneExport.Run
##       -exportScene Assets/Scenes/Game1.unity -exportOut <repo>/build/airplane_export -quit
##
## then this tool (it converts the images and sounds into assets/airplane/ and imports
## them):
##
##   godot --headless --path . --script res://tools/make_airplane.gd
##
## tools/unity_import.gd turns the Unity nodes into Godot nodes; this script leaves out
## what the consoles do not draw (the minimap, trails, the speed lines), attaches the
## components of runtime/game/airplane.c in place of the C# scripts and sets their
## fields from the ones the scripts had. The result is an ordinary scene: open it in Godot
## to change it, or change this tool and run it again (which overwrites it).
##
## The prototype's collect mode is what is ported (RaceMode was off in the scene): fly
## through gates and coins, shoot targets and ships, before the clock runs out.

const UnityImport := preload("res://tools/unity_import.gd")

const EXPORT_DIR := "build/airplane_export"
const ASSETS := "res://assets/airplane"
const OUTPUT := "res://scenes/airplane.tscn"
const BULLET_SCENE := "res://scenes/airplane/bullet.tscn"
const MENU := "res://scenes/main_menu.tscn"

## Cells per Unity terrain (each is 1000 m across): 24 makes cells of about 42 m.
const TERRAIN_CELLS := 24
const TERRAIN_CHUNK := 8
## The camera sees this far; fog hides where the world ends.
const FAR := 2000.0
const FOG_BEGIN := 700.0
## The sea: a square around the middle of the level, which reaches from -1800 to 2100 m
## along X and from -3400 to 3400 m along Z.
const SEA_CENTER := Vector3(150, 0, 0)
const SEA_SIZE := 12000.0
## Particle systems per effect, and particles per system, so all effects fit the pool.
const BURST_SYSTEMS := 3
const BURST_PARTICLES := 16

var unity := UnityImport.new()
var scene_root: Node3D
var nodes: Array
var made: Array


func _init() -> void:
	unity.max_texture_size = 256
	# The prototype's UI was made for 1280x720 and is shown at half size.
	unity.ui_reference = Vector2(1280, 720)
	unity.ui_scale = 0.5
	# The plane is moved by code and reports what it flies into.
	unity.body_types = {"Player": "Area3D"}
	if not unity.open(EXPORT_DIR, ASSETS) or not unity.prepare_files():
		quit(1)
		return
	nodes = unity.data.nodes
	scene_root = Node3D.new()
	scene_root.name = "AirplaneArcade"

	var environment := unity.make_environment()
	environment.name = "Environment"
	scene_root.add_child(environment)
	environment.owner = scene_root
	_tune_environment(environment.environment)

	made = unity.build_tree(nodes, scene_root, scene_root, _skip)
	_terrains()
	_water()
	var sounds := _sounds()
	var bursts := _bursts()
	_save_bullet_scene()
	# The scripts first: their typed fields refer to each other.
	_node("Player").set_script(load("res://components/airplane_player.gd"))
	_node("Main Camera").set_script(load("res://components/airplane_camera.gd"))
	_node("GameManager").set_script(load("res://components/airplane_game.gd"))
	_collectables(sounds, bursts)
	_player(sounds, bursts)
	_camera()
	_game()
	_jukebox()
	_trim_ui()
	_far_versions()

	var scene := PackedScene.new()
	var status := scene.pack(scene_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote %s (%d warnings)" % [OUTPUT, unity.warnings.size()])
	scene_root.free()
	quit(0 if status == OK else 1)


## What is left out: Unity's event system and editor-only parts, the minimap and race
## arrow (cameras into textures), the radar icons that only the minimap camera saw, the
## trails and speed lines (TrailRenderer, particles in local space), the walls at the
## edge of the world (switched off in the prototype), a stray plane and the sea's ambience
## (the runtime streams one piece of music at a time; the jukebox has it).
func _skip(_index: int, n: Dictionary) -> bool:
	var name := String(n.name)
	if name in ["EventSystem", "Canvas_Prototype", "OutOfBoundList", "Arrow_UI", "Camera_ArrowUI",
			"Camera_Minimap", "Radar_Icon", "Minimap_Mask", "Radar_UI", "Particle System",
			"Trail_Left", "Trail_Right", "Audio Source", "Prop1", "Plane_GameObject"]:
		return true
	if name.begins_with("TMP SubMeshUI"):
		return true
	if name == "Plane" and int(n.parent) == -1:
		return true
	return false


## The Godot node of the first exported node with this name (below `under`, if given).
func _node(name: String, under: String = "") -> Node:
	var parent_index := -1
	if not under.is_empty():
		for i in nodes.size():
			if nodes[i].name == under and made[i] != null:
				parent_index = i
				break
	for i in nodes.size():
		if nodes[i].name == name and made[i] != null:
			if parent_index < 0 or _is_below(i, parent_index):
				return made[i]
	push_error("no node '%s'%s in the export" % [name, " below " + under if not under.is_empty() else ""])
	return null


func _is_below(i: int, ancestor: int) -> bool:
	var up := int(nodes[i].parent)
	while up >= 0:
		if up == ancestor:
			return true
		up = int(nodes[up].parent)
	return false


## The fields a C# script had, by the index of its node.
func _fields(index: int, script: String) -> Dictionary:
	for comp: Dictionary in nodes[index].components:
		if comp.type == script:
			return comp.get("fields", {})
	return {}


func _index_of(node: Node) -> int:
	return made.find(node)


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node, true)
	node.owner = scene_root
	return node


# --- World -------------------------------------------------------------------------------

func _tune_environment(env: Environment) -> void:
	# Unity lit the scene from its skybox, which is brighter than its ambient colour.
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.52, 0.56)
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_DEPTH
	env.fog_light_color = Color(0.72, 0.84, 0.95)
	env.fog_density = 1.0
	env.fog_depth_begin = FOG_BEGIN
	env.fog_depth_end = FAR


func _terrains() -> void:
	var detail := _detail_texture()
	for node in made:
		if node != null and node.has_meta(&"unity_terrain"):
			node.texture = detail
			node.texture_size = 40.0
			# The splat colours are made a little brighter: the texture darkens them.
			node.colors = _brighten_gradient()
			# The terrains stand at -5 m and the sea is at 0: a flat one is sea bed only.
			var t: Dictionary = unity.data.terrains[node.get_meta(&"unity_terrain")]
			if float(t.max_height) * float(t.size[1]) < 5.5:
				unity.setup_terrain(node, 4, 4)
			else:
				unity.setup_terrain(node, TERRAIN_CELLS, TERRAIN_CHUNK)


## A grey texture with the pattern of the terrain's stones and an average near white,
## so the vertex colours of the splat map keep their brightness.
func _detail_texture() -> Texture2D:
	var path := ASSETS.path_join("textures/terrain_detail.res")
	var stones := unity.texture("Grey_Stones")
	if stones == null:
		return null
	var image := stones.get_image()
	if image.is_compressed():
		image.decompress()
	image.resize(64, 64, Image.INTERPOLATE_LANCZOS)
	image.convert(Image.FORMAT_RGB8)
	var sum := 0.0
	for y in 64:
		for x in 64:
			sum += image.get_pixel(x, y).get_luminance()
	var mean := sum / (64 * 64)
	for y in 64:
		for x in 64:
			var v := clampf(0.86 + (image.get_pixel(x, y).get_luminance() - mean) * 0.6, 0.0, 1.0)
			image.set_pixel(x, y, Color(v, v, v))
	var texture := ImageTexture.create_from_image(image)
	ResourceSaver.save(texture, path)
	texture.take_over_path(path)
	return texture


func _brighten_gradient() -> Gradient:
	var g := Gradient.new()
	g.colors = PackedColorArray([Color(1.16, 1.16, 1.16), Color(1.16, 1.16, 1.16)])
	return g


## The sea: an area in the group "world" (it crashes the plane) a little below the
## surface and reaching deep, so a fast plane cannot pass through it in one frame.
func _water() -> void:
	var water := _node("Water Type 2") as Area3D
	if water == null:
		return
	for child in water.get_children():
		if child is CollisionShape3D and child.shape is BoxShape3D:
			child.shape.size.y = 60.0
			child.position.y = -30.2
		elif child is MeshInstance3D:
			# Unity's water was a disc of 24 km made of a few huge triangles. Triangles that
			# reach far beyond the camera's far plane get wrong depths, so the sea is a plane
			# of 500 m squares over the level instead, a little larger than the fog lets see.
			var sea := StandardMaterial3D.new()
			# See-through, as in the prototype, where the sand shows below the water.
			sea.albedo_color = Color(0.1, 0.5, 0.85, 0.72)
			sea.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			# Tiles of 2000 m, so the ones out of view are left out.
			var plane := PlaneMesh.new()
			plane.size = Vector2(2000, 2000)
			plane.subdivide_width = 3
			plane.subdivide_depth = 3
			plane.material = sea
			var tiles := int(SEA_SIZE / 2000.0)
			for tz in tiles:
				for tx in tiles:
					var tile := MeshInstance3D.new()
					tile.mesh = plane
					tile.position = SEA_CENTER - water.position + Vector3((tx - (tiles - 1) * 0.5) * 2000.0, 0,
							(tz - (tiles - 1) * 0.5) * 2000.0)
					_add(water, tile, "Sea_%d_%d" % [tx, tz])
			water.remove_child(child)
			child.free()
			# A sea bed of the same tiles where no terrain is (the flat terrains under the
			# water were left out), a little below the terrains' lowest level.
			var sand := StandardMaterial3D.new()
			sand.albedo_color = Color(0.36, 0.36, 0.34)
			var bed := PlaneMesh.new()
			bed.size = Vector2(2000, 2000)
			bed.subdivide_width = 3
			bed.subdivide_depth = 3
			bed.material = sand
			for tz in tiles:
				for tx in tiles:
					var tile := MeshInstance3D.new()
					tile.mesh = bed
					tile.position = SEA_CENTER - water.position + Vector3((tx - (tiles - 1) * 0.5) * 2000.0, -5.5,
							(tz - (tiles - 1) * 0.5) * 2000.0)
					_add(water, tile, "Bed_%d_%d" % [tx, tz])
	water.monitoring = false


## One AudioStreamPlayer per sound the scripts played, below "Sounds".
func _sounds() -> Dictionary:
	var holder := _add(scene_root, Node.new(), "Sounds")
	var out := {}
	for key in ["explosion", "coin10", "coin5", "hit", "coin2", "explodemini", "chaingun_once"]:
		var stream := unity.sound(key)
		if stream == null:
			continue
		var player := _add(holder, AudioStreamPlayer.new(), key.capitalize().replace(" ", "")) as AudioStreamPlayer
		player.stream = stream
		out[key] = player
	return out


## The effects that scripts made instances of, made once each in the scene: a component
## moves one to where it is needed and fires its particle systems.
func _bursts() -> Dictionary:
	var holder := _add(scene_root, Node3D.new(), "Effects")
	var out := {}
	for key in ["Collect_FX", "WFX_Explosion_StarSmoke", "WFX_ExplosiveSmokeGround", "WFX_ExplosiveSmokeGround_Big"]:
		if not unity.data.prefabs.has(key):
			continue
		var tree: Array = unity.build_tree(unity.data.prefabs[key].nodes, holder, scene_root)
		var burst: Node3D = tree[0]
		if burst == null:
			continue
		burst.position = Vector3.ZERO
		var systems := 0
		for node in tree:
			if is_instance_valid(node) and node is CPUParticles3D:
				if systems >= BURST_SYSTEMS or not node.visible:
					node.get_parent().remove_child(node)
					node.free()
					continue
				systems += 1
				node.amount = mini(node.amount, BURST_PARTICLES)
				node.one_shot = true
				node.emitting = false
				node.explosiveness = maxf(node.explosiveness, 0.8)
				node.lifetime = minf(node.lifetime, 2.5)
		for child in burst.get_children():
			if child is AudioStreamPlayer or child is AudioStreamPlayer3D:
				burst.remove_child(child)
				child.free()
		out[key] = burst
	return out


# --- Things to collect -------------------------------------------------------------------

func _collectables(sounds: Dictionary, bursts: Dictionary) -> void:
	var spin := _spin_library()
	for i in nodes.size():
		var node: Node = made[i]
		if node == null:
			continue
		var gate := _fields(i, "CheckpointController")
		var item := _fields(i, "Collectable")
		if gate.is_empty() and item.is_empty():
			continue
		node.set_script(load("res://components/collectable.gd"))
		if not gate.is_empty():
			node.extend_time = gate.get("extendTime", 5.0)
			node.destroyable = false
			node.sound = sounds.get("hit")
		else:
			node.extend_time = item.get("extendTime", 2.0)
			node.destroyable = item.get("destroyable", false)
			var clip: Variant = item.get("chp_snd")
			if clip is Dictionary:
				node.sound = sounds.get(clip.audio)
			var fx: Variant = item.get("explo_fx")
			if fx is Dictionary:
				node.burst = bursts.get(fx.prefab)
			if item.get("rotateAround", false):
				# The coins turn about Y at `speed` degrees per second: an animation.
				var anim := _add(node, AnimationPlayer.new(), "Spin") as AnimationPlayer
				anim.add_animation_library("", spin)
				anim.autoplay = "spin"
				var speed := float(item.get("speed", 25.0))
				anim.speed_scale = speed / 360.0 * spin.get_animation("spin").length
		if node is Area3D:
			node.monitoring = false


## A clip that turns its node once about Y in four steps (rotations are interpolated the
## short way, so a whole turn needs keys in between).
func _spin_library() -> AnimationLibrary:
	var clip := Animation.new()
	clip.length = 1.0
	clip.loop_mode = Animation.LOOP_LINEAR
	var track := clip.add_track(Animation.TYPE_ROTATION_3D)
	clip.track_set_path(track, NodePath("."))
	for k in 5:
		clip.rotation_track_insert_key(track, k * 0.25, Quaternion(Vector3.UP, -k * PI * 0.5))
	var library := AnimationLibrary.new()
	library.add_animation("spin", clip)
	return library


# --- The plane ---------------------------------------------------------------------------

func _save_bullet_scene() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(BULLET_SCENE.get_base_dir()))
	var bullet := Node3D.new()
	bullet.name = "Bullet"
	bullet.set_script(load("res://components/airplane_bullet.gd"))
	var prefab: Dictionary = unity.data.prefabs.get("Sphere", {})
	if not prefab.is_empty():
		# The prefab is a sphere with a rigid body; only its look is kept.
		var tree: Array = unity.build_tree(prefab.nodes, bullet, bullet, func(_i, n): return n.name == "Cube")
		var body: Node3D = tree[0]
		if body != null:
			var look: Node = body.get_node_or_null("Mesh")
			if look != null:
				var mesh := MeshInstance3D.new()
				mesh.name = "Mesh"
				mesh.mesh = look.mesh
				mesh.transform = look.transform
				bullet.add_child(mesh)
				mesh.owner = bullet
			bullet.remove_child(body)
			body.free()
	var ray := RayCast3D.new()
	ray.name = "Ray"
	# A frame's flight at 500 m/s and 30 frames per second, and some.
	ray.target_position = Vector3(0, 0, -20)
	ray.collide_with_areas = false
	ray.collide_with_bodies = true
	bullet.add_child(ray)
	ray.owner = bullet
	bullet.speed = 500.0
	bullet.lifetime = 5.0
	bullet.ray = ray
	var scene := PackedScene.new()
	scene.pack(bullet)
	ResourceSaver.save(scene, BULLET_SCENE)
	bullet.free()


func _player(sounds: Dictionary, bursts: Dictionary) -> void:
	var player := _node("Player") as Area3D
	var i := _index_of(player)
	var f := _fields(i, "Player")
	player.add_to_group("player", true)
	player.monitorable = false

	# The HUD is taken out of the plane at the start (the prototype's Unparent script).
	var hud := _node("Canvas_HUD")
	hud.reparent(scene_root)
	hud.owner = scene_root

	# BulletSpawn had a collider in Unity, part of the plane's; here it only marks a place.
	var spawn := _node("BulletSpawn")
	var marker := Marker3D.new()
	marker.name = "BulletSpawn"
	marker.transform = spawn.transform
	spawn.replace_by(marker)
	spawn.free()
	marker.owner = scene_root

	var model := _node("Plane1")
	var blur := _node("Prop1Blur")
	if blur != null:
		# The axis the plane flies along, in the propeller's own space.
		var to_player: Transform3D = model.transform * blur.transform
		_spin_propeller(blur, (to_player.basis.inverse() * Vector3.FORWARD).normalized())

	player.min_speed = 30.0
	player.max_speed = 200.0
	player.boost_speed = 350.0
	player.turn_speed = 75.0
	player.roll_speed = 75.0
	player.max_roll = 45.0
	player.pitch_speed = 75.0
	player.max_pitch = 50.0
	player.start_time = 7.0
	player.countdown = 5.0
	player.game = _node("GameManager")
	player.model = model
	player.time_label = _node("Time (TMP)")
	player.clock_minutes = _node("Time Attack HUD(TMP) (1)")
	player.clock_seconds = _node("Time Attack HUD(TMP) (3)")
	player.clock_millis = _node("Time Attack HUD(TMP) (5)")
	var countdown := _node("Countdown (TMP)") as Label
	countdown.text = ""
	player.countdown_label = countdown
	player.mode_panel = _node("Panel", "Countdown (TMP)")
	player.boost_label = _node("Boost (TMP)")
	player.time_up_label = _node("Game Over (TMP)")
	player.engine_sound = player.get_node_or_null("AudioSource")
	if player.engine_sound != null:
		player.engine_sound.name = "Engine"
	player.crash_sound = sounds.get("explosion")
	player.count_sound = sounds.get("coin10")
	player.go_sound = sounds.get("coin5")
	player.fire_sound = sounds.get("chaingun_once")
	player.crash_burst = bursts.get(f.get("crash_fx", {}).get("prefab", ""))
	player.bullet_spawn = marker
	player.bullet = load(BULLET_SCENE)
	player.bullet_max = 8
	player.boost_bar = _boost_bar()

	# Only shown in the race mode.
	_node("Checkpoint_Msg (TMP)").visible = false

	player.area_entered.connect(Callable(player, "_on_area_entered"), CONNECT_PERSIST)
	player.body_entered.connect(Callable(player, "_on_body_entered"), CONNECT_PERSIST)


## The bar was a filled image masking another; here it is one TextureProgressBar with the
## inner image as its progress texture.
func _boost_bar() -> TextureProgressBar:
	var bar := _node("Boostbar (2)") as TextureProgressBar
	var inner := _node("Boostbar (1)") as TextureRect
	if bar == null:
		return null
	if inner != null:
		# A TextureProgressBar is never smaller than its texture, and the runtime does not
		# stretch one: the picture is scaled to the bar's size instead (its anchors are
		# one point, so the offsets are its size).
		var size := Vector2i(roundi(bar.offset_right - bar.offset_left), roundi(bar.offset_bottom - bar.offset_top))
		var image := inner.texture.get_image()
		if image.is_compressed():
			image.decompress()
		image.resize(maxi(size.x, 1), maxi(size.y, 1), Image.INTERPOLATE_BILINEAR)
		var fill := ImageTexture.create_from_image(image)
		var path := ASSETS.path_join("textures/boost_fill.res")
		ResourceSaver.save(fill, path)
		fill.take_over_path(path)
		bar.texture_progress = fill
		inner.get_parent().remove_child(inner)
		inner.free()
	# The fill direction comes from Unity (from the top), turned with the bar: from the left.
	bar.value = 0.0
	return bar


## The blurred propeller turns about the axis the plane flies along.
func _spin_propeller(prop: Node3D, axis: Vector3) -> void:
	var clip := Animation.new()
	clip.length = 0.2
	clip.loop_mode = Animation.LOOP_LINEAR
	var track := clip.add_track(Animation.TYPE_ROTATION_3D)
	clip.track_set_path(track, NodePath("."))
	var base := prop.quaternion
	for k in 5:
		clip.rotation_track_insert_key(track, k * 0.05, base * Quaternion(axis, k * PI * 0.5))
	var library := AnimationLibrary.new()
	library.add_animation("spin", clip)
	var anim := _add(prop, AnimationPlayer.new(), "Spin") as AnimationPlayer
	anim.add_animation_library("", library)
	anim.autoplay = "spin"


func _camera() -> void:
	var camera := _node("Main Camera") as Camera3D
	var f := _fields(_index_of(camera), "SmoothFollow")
	var orbit := _fields(_index_of(camera), "RotateAround")
	camera.current = true
	camera.far = FAR
	camera.near = 1.0
	camera.target = _node("Player")
	camera.distance = f.get("distance", 15.0)
	camera.height = f.get("height", 5.0)
	camera.height_damping = f.get("heightDamping", 5.0)
	camera.rotation_damping = f.get("rotationDamping", 5.0)
	camera.orbit_speed = orbit.get("angle", 5.0)
	camera.follow = false
	for child in camera.get_children():
		camera.remove_child(child)
		child.free()


func _game() -> void:
	var game := _node("GameManager")
	game.player = _node("Player")
	game.camera = _node("Main Camera")
	game.title_ui = _node("Canvas_TitleScreen")
	game.press_start = _node("Text (TMP)", "Canvas_TitleScreen")
	game.hud = _node("Canvas_HUD")
	game.collect_ui = _node("Canvas_Collect")
	game.gates_label = _node("Gates (TMP)")
	game.coins_label = _node("Coins (TMP)")
	game.targets_label = _node("Coins (TMP) (1)")
	game.ships_label = _node("Coins (TMP) (2)")
	game.gates = _node("CheckpointList")
	game.coins = _node("CoinList")
	game.targets = _node("TargetList")
	game.ships = _node("ShipList")
	game.game_over_ui = _node("Canvas_GameOver")
	game.result_label = _node("GameOver_Text (TMP)")
	game.time_label = _node("HighScore_Text (TMP)")
	game.best_label = _node("CurrentHighScore_Text (TMP)")
	game.new_record = _node("NewRecord_Text (TMP)")
	game.retry_label = _node("Text (TMP)", "Canvas_GameOver")
	game.restart_scene = OUTPUT
	game.menu_scene = MENU
	# The HUD belongs to the plane, which is hidden on the title.
	game.hud.visible = false
	# Unity's prompts name keys of the PC.
	(game.press_start as Label).text = "PRESS START"
	(game.retry_label as Label).text = "PRESS START TO RETRY"


## The jukebox gets one AudioStreamPlayer per piece, named "Artist - Title".
func _jukebox() -> void:
	var music := _node("Music")
	var f := _fields(_index_of(music), "Jukebox")
	var jukebox := Node.new()
	jukebox.name = "Jukebox"
	music.replace_by(jukebox)
	music.free()
	jukebox.owner = scene_root
	jukebox.set_script(load("res://components/jukebox.gd"))
	var clips: Array = f.get("music", [])
	var titles: Array = f.get("music_id", [])
	var artists: Array = f.get("artist_id", [])
	for k in clips.size():
		var clip: Variant = clips[k]
		if not (clip is Dictionary):
			continue
		var stream := unity.sound(clip.audio)
		if stream == null:
			continue
		var song_name := "%s - %s" % [artists[k] if k < artists.size() else "?", titles[k] if k < titles.size() else "?"]
		var player := AudioStreamPlayer.new()
		player.stream = stream
		player.volume_db = linear_to_db(0.4)
		_add(jukebox, player, song_name.replace(".", "").replace(":", "").replace("&", "and"))
	jukebox.artist_label = _node("Artist (TMP)")
	jukebox.title_label = _node("Song (TMP)")
	jukebox.panel = _node("Jukebox_Panel")
	jukebox.show_time = 6.0


## Small fixes to the converted controls.
func _trim_ui() -> void:
	# The time flown was shown as one label and as five; the five are kept.
	var single := _node("Time Attack HUD(TMP) (6)")
	if single != null:
		single.get_parent().remove_child(single)
		single.free()


var _lod_cache := {}

## The Dreamcast draws some 12000 vertices of solid polygons in a frame. Palms, gates,
## coins and ships are swapped for simpler copies from some distance on (made by Godot's
## LOD generator), and the ships' ropes are left out.
func _far_versions() -> void:
	for list: Array in [["PalmtreeList", 350.0, 1300.0, 0.45], ["CheckpointList", 500.0, FAR, 0.12],
			["CoinList", 300.0, 1500.0, 0.2], ["ShipList", 500.0, FAR, 0.3]]:
		var holder := _node(list[0])
		if holder == null:
			continue
		for instance in holder.find_children("*", "MeshInstance3D", true, false):
			if instance.mesh == null:
				continue
			if instance.name == "ropes":
				instance.visibility_range_end = 400.0
				continue
			if _vertex_count(instance.mesh) < 200:
				continue
			_with_far_copy(instance, list[1], list[2], list[3])


func _vertex_count(mesh: Mesh) -> int:
	var count := 0
	for s in mesh.get_surface_count():
		count += (mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()
	return count


func _with_far_copy(instance: MeshInstance3D, near: float, far: float, ratio: float) -> void:
	if not _lod_cache.has(instance.mesh):
		_lod_cache[instance.mesh] = unity.make_lod(instance.mesh, ratio)
	var copy := MeshInstance3D.new()
	copy.mesh = _lod_cache[instance.mesh]
	copy.transform = instance.transform
	copy.visibility_range_begin = near
	copy.visibility_range_end = far
	instance.visibility_range_end = near
	instance.get_parent().add_child(copy, true)
	copy.name = String(instance.name) + "Far"
	copy.owner = scene_root
