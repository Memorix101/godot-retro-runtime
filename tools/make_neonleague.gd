extends SceneTree
## Builds Neon League from the Unity project "NeonLeague" (Freak Mind Games): its scenes
## Level1.unity and Start.unity, written out by tools/unity/RetroSceneExport.cs (once, in
## Unity, on a copy of the project):
##
##   Unity.exe -batchmode -projectPath <copy> -executeMethod RetroSceneExport.Run
##       -exportScene Assets/Scene/Level1.unity -exportOut <repo>/build/neonleague_export -quit
##   (the same with Assets/Scene/Start.unity and build/neonleague_start_export)
##
##   godot --headless --path . --script res://tools/make_neonleague.gd
##
## Writes scenes/neonleague.tscn (the game), scenes/neonleague_title.tscn (the title) and
## the scenes made while the game runs: scenes/neonleague/enemy.tscn, laser.tscn and
## bomb.tscn. The logic is runtime/game/neonleague.c, in place of the C# scripts.
##
## What changes on the way:
## - Unity drew most of the game with a wireframe shader and made it glow with bloom. The
##   runtime draws textured triangles: the wireframes become small textures of lines (a
##   grid cell, a sphere's grid), and the floor's 40 wire planes of 200 triangles each
##   become one grid of 40 quads.
## - The colours that Unity's scripts changed (the walls and the floor every half second,
##   the enemies and cones pulsing red, the spawners fading in) are material_overrides that
##   are local to the scene, which the runtime can colour (rr_mesh_set_albedo_color).
## - Unity's NavMesh was one flat rectangle: the enemies head straight for the player and
##   slide along the walls (CharacterBody3D), keeping off each other.
## - The leaderboard (GameJolt), the ASCII mode, the screen resolution and the
##   post-processing are left out.

const UnityImport := preload("res://tools/unity_import.gd")

const EXPORT_DIR := "build/neonleague_export"
const TITLE_EXPORT_DIR := "build/neonleague_start_export"
const ASSETS := "res://assets/neonleague"
const OUTPUT := "res://scenes/neonleague.tscn"
const TITLE := "res://scenes/neonleague_title.tscn"
const PARTS := "res://scenes/neonleague"
const MENU := "res://scenes/main_menu.tscn"

## Collision layers (bits).
const L_WORLD := 1
const L_PLAYER := 2
const L_ENEMY := 4
const L_LASER := 8
const L_DEADLY := 16
const L_BOMB := 32

## Instances made while the game runs.
const ENEMY_MAX := 40
const LASER_MAX := 24
const BOMB_MAX := 2
## The lasers' trails: this many seconds of flight (Unity's TrailRenderer kept 5, which at
## 37 m/s reached across the arena several times) in this many pieces.
const TRAIL_TIME := 1.2
const TRAIL_PIECES := 24
## Copies of each effect, and what one copy may have.
## The particle systems kept of each (the black smoke and the huge shockwaves stay out).
const BURSTS := [["Boom", "Boom", 4, ["Boom", "Sparkles"]],
		["Blast", "Holy_Blast", 4, ["Glow", "Spakles", "Ray"]],
		["Explosion", "Explosion", 1, ["Fireball", "Shower"]],
		["Spawn", "SpawnEffect", 1, ["Spakles", "Ring"]]]
const BURST_PARTICLES := 16

## Unity's buttons: the text in the normal, focused and pressed colours of the Button.
const BUTTON_COLOR := Color(0.21, 0.36, 0.40)
const BUTTON_FOCUS := Color(0.0, 1.0, 0.96)
const BUTTON_PRESSED := Color(0.53, 0.0, 1.0)

var unity: UnityImport
var scene_root: Node3D
var nodes: Array
var made: Array
var _textures := {}


func _init() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(PARTS))
	var status := _game()
	if status == OK:
		status = _title()
	print("neonleague: %s" % ("done" if status == OK else "failed"))
	quit(0 if status == OK else 1)


func _importer(export_dir: String) -> UnityImport:
	var importer := UnityImport.new()
	importer.max_texture_size = 256
	# The big pictures of the UI (the title, "Ready", "Game Over", "You win").
	importer.texture_sizes = {"Neon_Rider_Title": 512, "Ready": 512, "GameOver": 512, "win": 512}
	importer.ui_reference = Vector2(1280, 720)
	importer.ui_scale = 0.5
	importer.generate_lods = false
	# Colliders of the walls, the deadly planes, the cones and the player only.
	importer.keep_colliders = func(n: Dictionary) -> bool:
		var name := String(n.name)
		return name.begins_with("pb-Cube") or name.begins_with("pb-Cone") or name == "Player" \
				or (name.begins_with("Plane") and n.components.any(func(c): return c.type == "DeadlyTrigger"))
	importer.body_types = {"Player": "CharacterBody3D", "pb-Cone-1164496": "Area3D"}
	if not importer.open(export_dir, ASSETS) or not importer.prepare_files():
		return null
	return importer


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
	if node != null and is_instance_valid(node):
		node.get_parent().remove_child(node)
		node.free()


func _own_all(node: Node, owner: Node) -> void:
	for child in node.get_children():
		child.owner = owner
		_own_all(child, owner)


func _index(name: String, under := -1) -> int:
	for i in nodes.size():
		if nodes[i].name == name and made[i] != null and (under < 0 or _is_below(i, under)):
			return i
	return -1


## The Godot node of the first exported node with this name (below `under`, if given).
func _node(name: String, under: String = "") -> Node:
	var top := _index(under) if not under.is_empty() else -1
	var i := _index(name, top)
	if i < 0:
		push_error("no node '%s'%s" % [name, " below " + under if not under.is_empty() else ""])
		return null
	return made[i]


func _is_below(i: int, ancestor: int) -> bool:
	var up := int(nodes[i].parent)
	while up >= 0:
		if up == ancestor:
			return true
		up = int(nodes[up].parent)
	return false


func _fields(index: int, script: String) -> Dictionary:
	for comp: Dictionary in nodes[index].components:
		if comp.type == script:
			return comp.get("fields", {})
	return {}


func _has_component(n: Dictionary, type: String) -> bool:
	return n.components.any(func(c): return c.type == type)


func _sound(parent: Node, key: String, node_name: String, owner: Node, volume := 1.0) -> AudioStreamPlayer:
	var stream := unity.sound(key)
	if stream == null:
		push_error("no sound %s" % key)
		return null
	var player := AudioStreamPlayer.new()
	player.stream = stream
	player.volume_db = linear_to_db(volume)
	player.bus = &"SFX"
	return _add(parent, player, node_name, owner)


# --- Looks -----------------------------------------------------------------------------------

## A texture made here, saved as a resource so the scenes refer to it.
func _texture(key: String, image: Image) -> Texture2D:
	if _textures.has(key):
		return _textures[key]
	var path := ASSETS.path_join("textures/%s.res" % key)
	var texture := ImageTexture.create_from_image(image)
	ResourceSaver.save(texture, path)
	texture.take_over_path(path)
	_textures[key] = texture
	return texture


## A glowing cell: bright edges, a dark inside (Unity's prototype texture with bloom). It
## repeats every metre on the walls and once per face on the cubes.
func _cell_texture() -> Texture2D:
	var size := 64
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for y in size:
		for x in size:
			var edge := mini(mini(x, size - 1 - x), mini(y, size - 1 - y))
			var v := 1.0 if edge < 3 else (0.55 if edge < 5 else 0.12)
			image.set_pixel(x, y, Color(v, v, v))
	return _texture("neon_cell", image)


## A cell of a wireframe grid: lines on the left and top edge (the neighbours make the
## others), a fainter diagonal, transparent between them. The size is about the size the
## cell has on the screen, so the lines stay thin without mipmaps.
func _grid_texture(size := 16) -> Texture2D:
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(Color(1, 1, 1, 0))
	for k in size:
		image.set_pixel(k, 0, Color.WHITE)
		image.set_pixel(0, k, Color.WHITE)
		image.set_pixel(k, size - 1 - k, Color(1, 1, 1, 0.35))
	return _texture("neon_grid_%d" % size, image)


## The wireframe of a sphere: 16 meridians and 8 rings on a SphereMesh's coordinates.
func _wire_texture() -> Texture2D:
	var w := 256
	var h := 128
	var image := Image.create(w, h, false, Image.FORMAT_RGBA8)
	image.fill(Color(1, 1, 1, 0))
	for y in h:
		for x in w:
			if x % 16 < 2 or y % 16 < 2:
				image.set_pixel(x, y, Color.WHITE)
	return _texture("neon_wire", image)


func _material(color: Color, texture: Texture2D = null, transparent := false, tinted := false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = color
	m.albedo_texture = texture
	if transparent:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	# Every instance has its own, so the game can colour it (rr_mesh_set_albedo_color).
	m.resource_local_to_scene = tinted
	return m


## A flat grid in the XZ plane, centred, `size` big, with the texture repeated `cells`
## times and split into `parts` quads (long triangles get clipped badly on some consoles).
func _grid_mesh(size: Vector2, cells: Vector2, parts: Vector2i) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for pz in parts.y:
		for px in parts.x:
			var corners: Array[Vector2] = []
			for c in [Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]:
				corners.append(Vector2((px + c.x) / parts.x, (pz + c.y) / parts.y))
			for k in [0, 1, 2, 0, 2, 3]:
				var f: Vector2 = corners[k]
				st.set_normal(Vector3.UP)
				st.set_uv(f * cells)
				st.add_vertex(Vector3((f.x - 0.5) * size.x, 0.0, (f.y - 0.5) * size.y))
	return st.commit()


## A piece of a trail: a flat strip in the XZ plane from z = 0 (one metre wide) to z = 1
## (`back` metres wide), its texture repeated `repeat` times along it. The component
## stretches and colours it; the pieces one after the other narrow to a point.
func _ribbon_mesh(back: float, repeat: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var quad := [Vector3(-0.5, 0, 0), Vector3(0.5, 0, 0), Vector3(back * 0.5, 0, 1), Vector3(-back * 0.5, 0, 1)]
	var uv := [Vector2(0, 0), Vector2(1, 0), Vector2(1, repeat), Vector2(0, repeat)]
	for k in [0, 1, 2, 0, 2, 3]:
		st.set_normal(Vector3.UP)
		st.set_uv(uv[k])
		st.add_vertex(quad[k])
	return st.commit()


## The wireframe of a trail (Unity drew its TrailRenderer with the wireframe shader): the
## two edges and a zigzag between them, transparent elsewhere.
func _zigzag_texture() -> Texture2D:
	# About as many pixels as the trail has on the screen, so the lines stay whole.
	var w := 8
	var h := 16
	var image := Image.create(w, h, false, Image.FORMAT_RGBA8)
	image.fill(Color(1, 1, 1, 0))
	for y in h:
		image.set_pixel(0, y, Color.WHITE)
		image.set_pixel(w - 1, y, Color.WHITE)
		# Down to the right in the first half, back to the left in the second.
		var across := float(y) / (h / 2) if y < h / 2 else float(h - 1 - y) / (h / 2)
		image.set_pixel(clampi(roundi(across * (w - 1)), 0, w - 1), y, Color.WHITE)
	return _texture("neon_zigzag", image)


func _wire_sphere(radius: float) -> SphereMesh:
	var sphere := SphereMesh.new()
	sphere.radius = radius
	sphere.height = radius * 2.0
	sphere.radial_segments = 16
	sphere.rings = 8
	return sphere


# --- The arena (game and title) -------------------------------------------------------------

## What is left out of both scenes: Unity's event systems, the parts of the leaderboard and
## of the options that the runtime has not got, the floor's wire planes (one grid instead),
## markers that were hidden in Unity.
func _skip(_i: int, n: Dictionary) -> bool:
	var name := String(n.name)
	if name in ["EventSystem", "GameJolt_Leaderboard", "ASCII Mode Hint", "Directional Light",
			"MenuManager", "Intermission", "Dropdown", "Lightray", "Circle", "OldMenu", "Discord",
			"Canvas_C"] or name.begins_with("TMP SubMeshUI"):
		return true
	if name == "Image" and not n.active:
		return true
	if name == "GameObject" and not n.active:
		return true  # the player's spawn effect, which the game fires itself
	# The floor's 40 wire planes.
	var up := int(n.parent)
	if name.begins_with("Plane") and up >= 0 and nodes[up].name == "Floor":
		return true
	return false


func _arena() -> void:
	var geometry := _node("Geometry")
	var cell := _cell_texture()
	var fine := _grid_texture(64)
	for i in nodes.size():
		var node: Node = made[i]
		if node == null:
			continue
		var n: Dictionary = nodes[i]
		var name := String(n.name)
		if name.begins_with("pb-Cube"):
			# A wall: solid for the player, the enemies and the lasers; changes its colour
			# every half second (DiscoFloor); moves along the edge from stage 4 on.
			var body := node as StaticBody3D
			body.collision_layer = L_WORLD
			body.collision_mask = 0
			var look := body.get_node("Mesh") as MeshInstance3D
			look.material_override = _material(Color.WHITE, cell, false, true)
			_disco(look, 0, 0.5, 1.0)
		elif name.begins_with("Plane") and node is Area3D:
			# The deadly planes at the edges, drawn as red wire grids.
			var area := node as Area3D
			area.add_to_group("deadly", true)
			area.collision_layer = L_DEADLY
			area.collision_mask = 0
			area.monitoring = false
			var look := area.get_node("Mesh") as MeshInstance3D
			look.mesh = _grid_mesh(Vector2(10, 10), Vector2(10, 10), Vector2i(2, 2))
			look.material_override = _material(Color(1.0, 0.0, 0.13, 0.7), fine, true)
		elif name == "Floor":
			var floor_mesh := node as MeshInstance3D
			floor_mesh.material_override = _material(Color(0.1, 0.1, 0.1), null, false, true)
			# Unity lit it by the ambient light only: dark colours.
			_disco(floor_mesh, 0, 0.5, 0.3)
			_floor_grid(i, floor_mesh)
	# The cones of stage 5: deadly, pulsing red.
	for i in nodes.size():
		if made[i] != null and String(nodes[i].name).begins_with("pb-Cone"):
			var cone := made[i] as Area3D
			cone.add_to_group("deadly", true)
			cone.collision_layer = L_DEADLY
			cone.collision_mask = 0
			cone.monitoring = false
			var look := cone.get_node("Mesh") as MeshInstance3D
			look.material_override = _material(Color(0.58, 0, 0), null, false, true)
			# Areas take boxes and spheres: a box a little smaller than the cone's mesh in
			# place of Unity's capsule.
			for child in cone.get_children():
				if child is CollisionShape3D:
					_remove(child)
			var bounds: Dictionary = unity.data.meshes[_mesh_key(i)]
			var scale := look.transform.basis.get_scale()
			var box := _box_shape(Vector3(bounds.bounds_size[0], bounds.bounds_size[1], bounds.bounds_size[2]).abs() * scale * 0.85)
			_add(cone, box, "Shape", scene_root)
			box.position = look.transform * Vector3(bounds.bounds_center[0], bounds.bounds_center[1], bounds.bounds_center[2])
			_disco(look, 1, 0.0, 1.0)
	geometry.name = "Geometry"


func _mesh_key(index: int) -> String:
	for comp: Dictionary in nodes[index].components:
		if comp.type == "MeshFilter":
			return String(comp.mesh)
	return ""


func _disco(mesh: MeshInstance3D, mode: int, interval: float, brightness: float) -> void:
	mesh.set_script(load("res://components/neon_disco.gd"))
	mesh.mode = mode
	mesh.interval = interval
	mesh.brightness = brightness


## One green wire grid over the floor where Unity had 40 wire planes of 10 x 10 cells.
func _floor_grid(floor_index: int, floor_mesh: MeshInstance3D) -> void:
	var lo := Vector3(INF, 0, INF)
	var hi := Vector3(-INF, 0, -INF)
	var y := 0.0
	var tile := 5.0
	for i in nodes.size():
		if int(nodes[i].parent) == floor_index and String(nodes[i].name).begins_with("Plane"):
			var p: Array = nodes[i].global_position
			var s: Array = nodes[i].global_scale
			tile = 10.0 * float(s[0])
			lo = Vector3(minf(lo.x, p[0]), 0, minf(lo.z, p[2]))
			hi = Vector3(maxf(hi.x, p[0]), 0, maxf(hi.z, p[2]))
			y = p[1]
	if lo.x == INF:
		return
	var size := Vector2(hi.x - lo.x + tile, hi.z - lo.z + tile)
	var cells := Vector2(roundf(size.x / tile) * 10.0, roundf(size.y / tile) * 10.0)
	var grid := MeshInstance3D.new()
	grid.mesh = _grid_mesh(size, cells, Vector2i(roundi(size.x / tile), roundi(size.y / tile)))
	# Unity's wire colour had an alpha of 0.35.
	grid.material_override = _material(Color(0.0, 1.0, 0.67, 0.35), _grid_texture(), true)
	var parent := floor_mesh.get_parent() as Node3D
	_add(parent, grid, "FloorGrid", scene_root)
	# The parent (Geometry) stands at the top of the scene: its transform is its global one.
	grid.position = parent.transform.affine_inverse() * Vector3((lo.x + hi.x) * 0.5, y + 0.01, (lo.z + hi.z) * 0.5)


func _environment() -> void:
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.19, 0.3, 0.47)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color.WHITE
	_add(scene_root, environment, "Environment", scene_root)


# --- UI ----------------------------------------------------------------------------------------

func _label_in(node: Node) -> Label:
	for child in node.find_children("*", "Label", true, false):
		return child
	return null


func _copy_rect(from: Control, to: Control) -> void:
	to.anchor_left = from.anchor_left
	to.anchor_top = from.anchor_top
	to.anchor_right = from.anchor_right
	to.anchor_bottom = from.anchor_bottom
	to.offset_left = from.offset_left
	to.offset_top = from.offset_top
	to.offset_right = from.offset_right
	to.offset_bottom = from.offset_bottom
	to.grow_horizontal = from.grow_horizontal
	to.grow_vertical = from.grow_vertical


## Puts `node` where `old` is and frees `old` (and what is below it, except `keep`).
func _swap(old: Node, node: Node, keep: Array = []) -> Node:
	var parent := old.get_parent()
	var at := old.get_index()
	var node_name := old.name
	for child in keep:
		child.reparent(node, false)
	var index := made.find(old)
	if index >= 0:
		made[index] = node
	parent.remove_child(old)
	old.free()
	node.name = node_name
	parent.add_child(node)
	parent.move_child(node, at)
	node.owner = scene_root
	_own_all(node, scene_root)
	return node


func _style_text(control: Control, label: Label) -> void:
	if label == null:
		return
	control.add_theme_font_override("font", label.get_theme_font("font"))
	control.add_theme_font_size_override("font_size", label.get_theme_font_size("font_size"))


## A Unity button (a rectangle with a TextMeshPro text) becomes a flat Button with the text
## in Unity's colours: dim teal, cyan with the focus, purple while pressed.
func _button(old: Control, target: Node, method: String) -> Button:
	var label := _label_in(old)
	var button := Button.new()
	_copy_rect(old, button)
	button.text = label.text if label != null else ""
	_style_text(button, label)
	button.add_theme_color_override("font_color", BUTTON_COLOR)
	button.add_theme_color_override("font_focus_color", BUTTON_FOCUS)
	button.add_theme_color_override("font_pressed_color", BUTTON_PRESSED)
	button.add_theme_color_override("font_hover_color", BUTTON_FOCUS)
	for style in ["normal", "pressed", "hover", "disabled", "focus"]:
		button.add_theme_stylebox_override(style, StyleBoxEmpty.new())
	_swap(old, button)
	button.pressed.connect(Callable(target, method), CONNECT_PERSIST)
	return button


## A Unity slider becomes an HSlider from 0 to 1 that keeps its label.
func _slider(old: Control, target: Node, method: String) -> HSlider:
	var label := _label_in(old)
	var slider := HSlider.new()
	_copy_rect(old, slider)
	slider.min_value = 0.0
	slider.max_value = 1.0
	slider.step = 0.05
	slider.value = 0.8
	var track := StyleBoxFlat.new()
	track.bg_color = BUTTON_COLOR
	track.content_margin_top = 3
	track.content_margin_bottom = 3
	slider.add_theme_stylebox_override("slider", track)
	var fill := StyleBoxFlat.new()
	fill.bg_color = BUTTON_FOCUS
	slider.add_theme_stylebox_override("grabber_area", fill)
	var focus_fill := StyleBoxFlat.new()
	focus_fill.bg_color = Color(1, 1, 1)
	slider.add_theme_stylebox_override("grabber_area_highlight", focus_fill)
	_swap(old, slider, [label] if label != null else [])
	slider.value_changed.connect(Callable(target, method), CONNECT_PERSIST)
	return slider


## Unity's rumble toggle becomes a CheckBox with its text.
func _toggle(old: Control, target: Node, method: String) -> CheckBox:
	var label := _label_in(old)
	var box := CheckBox.new()
	_copy_rect(old, box)
	box.text = label.text if label != null else ""
	_style_text(box, label)
	box.add_theme_color_override("font_color", Color.WHITE)
	box.add_theme_color_override("font_focus_color", BUTTON_FOCUS)
	box.add_theme_color_override("font_pressed_color", Color.WHITE)
	for style in ["normal", "pressed", "hover", "disabled", "focus", "hover_pressed"]:
		box.add_theme_stylebox_override(style, StyleBoxEmpty.new())
	box.button_pressed = true
	_swap(old, box)
	box.toggled.connect(Callable(target, method), CONNECT_PERSIST)
	return box


## A TextMeshPro text that only showed a sprite: the sprite's picture in the middle of it.
func _icon(label: Label, texture_key: String, size: float) -> TextureRect:
	var rect := TextureRect.new()
	rect.texture = unity.texture(texture_key)
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_SCALE
	_copy_rect(label, rect)
	# Left of the number, which TextMeshPro drew after the sprite.
	var middle := Vector2((label.offset_left + label.offset_right) * 0.5 - 52.0, (label.offset_top + label.offset_bottom) * 0.5)
	rect.offset_left = middle.x - size * 0.5
	rect.offset_right = middle.x + size * 0.5
	rect.offset_top = middle.y - size * 0.5
	rect.offset_bottom = middle.y + size * 0.5
	label.text = ""
	_add(label.get_parent(), rect, String(label.name) + "_Icon", scene_root)
	return rect


## The dark panels that cover the screen behind a menu: Unity stretched them over its
## 16:9 screen, here they cover the whole 4:3 one.
func _fill_panels() -> void:
	for i in nodes.size():
		if made[i] == null or not is_instance_valid(made[i]):
			continue  # left out, or swapped for a button
		var node: Node = made[i]
		if node is Control and String(nodes[i].name) == "Panel" and node.get_parent() is CanvasLayer:
			node.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)


## Up and down between the controls of a menu, in this order and round again. Godot works
## neighbours out by itself, but finds none for controls right on a CanvasLayer here.
func _chain(controls: Array) -> void:
	for k in controls.size():
		var c: Control = controls[k]
		var up: Control = controls[(k + controls.size() - 1) % controls.size()]
		var down: Control = controls[(k + 1) % controls.size()]
		c.focus_neighbor_top = c.get_path_to(up)
		c.focus_neighbor_bottom = c.get_path_to(down)
		if c is BaseButton:
			c.focus_neighbor_left = NodePath(".")
			c.focus_neighbor_right = NodePath(".")


func _flicker(control: Control, blink: bool, colors: bool, duration: float) -> void:
	control.set_script(load("res://components/neon_flicker.gd"))
	control.blink = blink
	control.colors = colors
	control.duration = duration


# --- Pool scenes -------------------------------------------------------------------------------

func _box_shape(size: Vector3) -> CollisionShape3D:
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	return shape


func _sphere_shape(radius: float) -> CollisionShape3D:
	var shape := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = radius
	shape.shape = sphere
	return shape


## The enemy (Unity's prefab "Enemy"): a cube that pulses red.
func _save_enemy() -> PackedScene:
	var f := {}
	for comp: Dictionary in unity.data.prefabs.Enemy.nodes[0].components:
		if comp.type == "NavMeshAgent":
			f = comp.fields
	var enemy := CharacterBody3D.new()
	enemy.name = "Enemy"
	enemy.set_script(load("res://components/neon_enemy.gd"))
	enemy.add_to_group("enemy", true)
	enemy.collision_layer = 0
	enemy.collision_mask = L_WORLD
	_add(enemy, _box_shape(Vector3(0.9, 0.9, 0.9)), "Shape", enemy)
	var hitbox := _add(enemy, Area3D.new(), "Hitbox", enemy) as Area3D
	hitbox.collision_layer = L_ENEMY
	hitbox.collision_mask = 0
	hitbox.monitoring = false
	_add(hitbox, _box_shape(Vector3.ONE), "Shape", enemy)
	var look := MeshInstance3D.new()
	look.mesh = unity.mesh("Cube")
	look.material_override = _material(Color(1, 0, 0), _cell_texture(), false, true)
	_add(enemy, look, "Look", enemy)
	_disco(look, 1, 0.0, 1.0)
	enemy.speed = float(f.get("m_Speed", 8.0))
	enemy.acceleration = float(f.get("m_Acceleration", 8.0))
	enemy.turn_speed = float(f.get("m_AngularSpeed", 120.0))
	enemy.radius = float(f.get("m_Radius", 0.5))
	enemy.look = look
	var path := PARTS.path_join("enemy.tscn")
	_save(enemy, path)
	enemy.free()
	return load(path)


## The laser (Unity's prefab "PlayerLaser"): a ball that bounces off the walls, drawn as a
## short streak along its flight (Unity left a trail behind it).
func _save_laser() -> PackedScene:
	var laser := RigidBody3D.new()
	laser.name = "Laser"
	laser.set_script(load("res://components/neon_laser.gd"))
	laser.add_to_group("laser", true)
	laser.collision_layer = 0
	laser.collision_mask = L_WORLD
	laser.gravity_scale = 0.0
	laser.lock_rotation = true
	# The component keeps the speed; the project's damping does not slow it.
	var bounce := PhysicsMaterial.new()
	bounce.bounce = 1.0
	bounce.friction = 0.0
	laser.physics_material_override = bounce
	_add(laser, _sphere_shape(0.25), "Shape", laser)
	var hit := _add(laser, Area3D.new(), "Hit", laser) as Area3D
	hit.collision_layer = L_LASER
	hit.collision_mask = L_PLAYER | L_ENEMY | L_LASER
	_add(hit, _sphere_shape(0.3), "Shape", laser)
	hit.area_entered.connect(Callable(laser, "_on_hit_area_entered"), CONNECT_PERSIST)
	# Unity's laser: a small cyan wire ball.
	var ball := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 0.22
	sphere.height = 0.44
	sphere.radial_segments = 8
	sphere.rings = 4
	ball.mesh = sphere
	ball.material_override = _material(Color(0.6, 1.0, 0.95))
	_add(laser, ball, "Ball", laser)
	# Its TrailRenderer (0.52 m wide at the laser, a point at the end, cyan, drawn as a
	# wireframe): pieces the component lays along the way the laser came.
	var trail := _add(laser, Node3D.new(), "Trail", laser) as Node3D
	var zigzag := _zigzag_texture()
	var piece_length := TRAIL_TIME * 37.0 / TRAIL_PIECES
	for k in TRAIL_PIECES:
		var piece := MeshInstance3D.new()
		# Piece k is as wide as (pieces - k) / pieces of the trail at its front, and as the
		# next one at its back: the last one ends in a point.
		var back := float(TRAIL_PIECES - k - 1) / float(TRAIL_PIECES - k)
		piece.mesh = _ribbon_mesh(back, piece_length / 1.0)
		piece.material_override = _material(Color(0.41, 1.0, 0.92, 0.9), zigzag, true, true)
		piece.visible = false
		_add(trail, piece, "Piece%d" % k, laser)
	# Unity: a push of 1850 for one physics step of 0.02 s on a mass of 1.
	laser.speed = 37.0
	laser.lifetime = 6.0
	laser.ball = ball
	laser.trail = trail
	laser.trail_time = TRAIL_TIME
	laser.trail_width = 0.52
	var path := PARTS.path_join("laser.tscn")
	_save(laser, path)
	laser.free()
	return load(path)


## The bomb (Unity's prefab "BombDamageZone"): an orange wire ball that grows to 15 times
## its size and shrinks again.
func _save_bomb() -> PackedScene:
	var fields := {}
	for comp: Dictionary in unity.data.prefabs.BombDamageZone.nodes[0].components:
		if comp.type == "BombBahavior":
			fields = comp.fields
	var bomb := Area3D.new()
	bomb.name = "Bomb"
	bomb.set_script(load("res://components/neon_bomb.gd"))
	bomb.collision_layer = L_BOMB
	bomb.collision_mask = L_ENEMY
	bomb.monitorable = false
	_add(bomb, _sphere_shape(0.5), "Shape", bomb)
	var look := MeshInstance3D.new()
	look.mesh = _wire_sphere(0.5)
	look.material_override = _material(Color(1.0, 0.55, 0.0, 0.8), _wire_texture(), true)
	_add(bomb, look, "Look", bomb)
	bomb.area_entered.connect(Callable(bomb, "_on_area_entered"), CONNECT_PERSIST)
	bomb.size = 15.0
	bomb.grow_time = 1.0
	bomb.hold_time = float(fields.get("destroyTime", 2.0))
	var path := PARTS.path_join("bomb.tscn")
	_save(bomb, path)
	bomb.free()
	return load(path)


# --- The game ----------------------------------------------------------------------------------

func _game() -> int:
	unity = _importer(EXPORT_DIR)
	if unity == null:
		return FAILED
	nodes = unity.data.nodes
	var enemy := _save_enemy()
	var laser := _save_laser()
	var bomb := _save_bomb()

	scene_root = Node3D.new()
	scene_root.name = "NeonLeague"
	_environment()
	made = unity.build_tree(nodes, scene_root, scene_root, _skip)
	_arena()

	var game := _add(scene_root, Node.new(), "Game", scene_root)
	game.set_script(load("res://components/neon_game.gd"))
	var player := _player(laser, bomb)
	var camera := _camera(player)
	_spawners()
	var cones := _add(scene_root, Node3D.new(), "Cones", scene_root)
	for name in ["Cone_Obstacle", "Cone_Obstacle (1)"]:
		var holder := _node(name)
		holder.reparent(cones, false)
		holder.owner = scene_root
	var sounds := _add(scene_root, Node.new(), "Sounds", scene_root)
	game.enemy_sound = _sound(sounds, "explosion_dirty_rnd_01", "EnemyExplodes", scene_root, 0.6)
	game.blast_sound = _sound(sounds, "big_thud_01", "Blast", scene_root, 0.6)
	game.death_sound = _sound(sounds, "explosion_dirty_rnd_01", "PlayerExplodes", scene_root)
	player.fire_sound = _sound(sounds, "tiny_whitenoise_laser_rnd_01", "Laser", scene_root, 0.5)
	player.bomb_sound = _sound(sounds, "sword_clash_rnd_01", "Bomb", scene_root)
	game.bursts = _bursts()
	game.player = player
	game.camera = camera
	game.walls = _node("Geometry")
	game.cones = cones
	game.enemy = enemy
	game.enemy_max = ENEMY_MAX
	game.restart_scene = OUTPUT
	game.title_scene = TITLE
	game.background_time = 1.0
	_fill_panels()
	_hud(game)
	_pause(game)
	_jukebox()
	var status := _save(scene_root, OUTPUT)
	scene_root.free()
	print("neonleague: game (%d warnings)" % unity.warnings.size())
	return status


func _player(laser: PackedScene, bomb: PackedScene) -> CharacterBody3D:
	var player := _node("Player") as CharacterBody3D
	player.set_script(load("res://components/neon_player.gd"))
	player.add_to_group("player", true)
	player.collision_layer = 0
	player.collision_mask = L_WORLD
	# The hitbox reports enemies and the deadly planes and cones.
	var hitbox := _add(player, Area3D.new(), "Hitbox", scene_root) as Area3D
	hitbox.collision_layer = L_PLAYER
	hitbox.collision_mask = L_ENEMY | L_DEADLY
	_add(hitbox, _box_shape(Vector3.ONE), "Shape", scene_root)
	hitbox.area_entered.connect(Callable(player, "_on_hitbox_area_entered"), CONNECT_PERSIST)
	# Unity's wireframe sphere.
	var look := player.get_node("Mesh") as MeshInstance3D
	look.mesh = _wire_sphere(0.5)
	look.material_override = _material(Color(0.0, 1.0, 0.97), _wire_texture(), true)
	var gun := _node("Cube", "Player")
	gun.name = "Gun"
	# Unity's gun and muzzle had no look of their own.
	for child in gun.get_children():
		_remove(child)
	var spawn := player.get_node_or_null("AudioSource") as AudioStreamPlayer
	if spawn != null:
		spawn.name = "SpawnSound"
		spawn.bus = &"SFX"
		spawn.autoplay = false  # the component plays it with the spawn effect
	# Unity: 600 * the physics step of 0.02 s.
	player.speed = 12.0
	player.gun = gun
	player.muzzle = 1.13
	player.laser_speed = 37.0
	player.fire_interval = 0.25
	player.laser = laser
	player.laser_max = LASER_MAX
	player.bomb = bomb
	player.bomb_max = BOMB_MAX
	player.spawn_sound = spawn
	return player


func _camera(player: Node3D) -> Camera3D:
	var camera := _node("Camera") as Camera3D
	camera.set_script(load("res://components/neon_camera.gd"))
	camera.current = true
	camera.near = 0.5
	camera.far = 100.0
	camera.target = player
	camera.back = 5.0
	camera.shake_amount = 0.18
	camera.spin_speed = 0.0
	# Where it is once it follows the player.
	camera.position = Vector3(player.position.x, camera.position.y, player.position.z + 5.0)
	for child in camera.get_children():
		_remove(child)
	return camera


func _spawners() -> void:
	var cell := _cell_texture()
	for i in nodes.size():
		if made[i] != null and _has_component(nodes[i], "Spawner"):
			var spawner: Node3D = made[i]
			spawner.set_script(load("res://components/neon_spawner.gd"))
			var preview := spawner.get_node_or_null("Enemy") as MeshInstance3D
			if preview != null:
				preview.material_override = _material(Color(1, 1, 1, 0), cell, true, true)
				spawner.preview = preview


## The effects the scripts made instances of: copies of each, below "Bursts", fired by the
## game one copy after the other.
func _bursts() -> Node3D:
	var holder := _add(scene_root, Node3D.new(), "Bursts", scene_root) as Node3D
	for spec: Array in BURSTS:
		var kind := _add(holder, Node3D.new(), spec[0], scene_root)
		if not unity.data.prefabs.has(spec[1]):
			push_error("no prefab %s" % spec[1])
			continue
		for copy in int(spec[2]):
			var tree: Array = unity.build_tree(unity.data.prefabs[spec[1]].nodes, kind, scene_root)
			var burst: Node3D = tree[0]
			burst.position = Vector3(0, -100, 0)
			var keep: Array = spec[3]
			for node in burst.find_children("*", "CPUParticles3D", true, false) + ([burst] if burst is CPUParticles3D else []):
				if not is_instance_valid(node):
					continue
				var kept: bool = String(node.name).replace("_", " ") in keep or String(node.name) in keep
				if not kept:
					if node == burst:
						# The top of the effect holds the others: it stays, without particles
						# to see.
						node.amount = 1
						node.scale_amount_min = 0.0
						node.scale_amount_max = 0.0
					else:
						_remove(node)
					continue
				node.amount = mini(node.amount, BURST_PARTICLES)
				node.one_shot = true
				node.emitting = false
				node.lifetime = minf(node.lifetime, 2.0)
			for node in burst.find_children("*", "AudioStreamPlayer*", true, false):
				_remove(node)
			for node in burst.find_children("*", "Light3D", true, false):
				_remove(node)
	return holder


func _hud(game: Node) -> void:
	var canvas := _node("Canvas_Main")
	var f := _fields(_index("Camera"), "GameManager")
	var hud := _add(canvas, Control.new(), "HUD", scene_root) as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	hud.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for ref in f.get("game_hud", []):
		var node: Node = made[int(ref.node)]
		if node != null:
			node.reparent(hud, false)
			node.owner = scene_root
	game.hud = hud
	game.score_label = made[int(f.score_text.node)]
	game.final_score = made[int(f.gmov_score_text.node)]
	game.lives_label = made[int(f.lives_hud.node)]
	game.time_label = made[int(f.survive_timer_text.node)]
	game.bombs_label = _node("Bomb (TMP) (1)")
	game.stage_label = _node("Stage")
	game.survive_label = _node("Survive")
	# The sprites of the bomb and lives texts.
	_icon(_node("Bomb (TMP)"), "shield", 18)
	_icon(_node("Lives"), "suit_hearts", 20)
	game.ready_picture = _node("Title", "Title_UI")
	var panel := _node("Panel", "Canvas_Main")
	panel.visible = false
	game.panel = panel
	var over := _node("GameOver_UI")
	over.visible = false
	game.game_over = over
	_flicker(_node("GameOver"), true, true, 2.0)
	for name in ["Text_Gp_Quit", "Text_Gp_Restart"]:
		_remove(_node(name))
	var restart := _node("Text_Kb_Restart") as Label
	restart.text = "PRESS START TO RESTART"
	var quit := _node("Text_Kb_Quit") as Label
	quit.text = "B: EXIT"
	_flicker(restart, false, true, 0.0)
	_flicker(quit, false, true, 0.0)
	var win := _node("Canvas_Win")
	win.visible = false
	game.win = win


func _pause(game: Node) -> void:
	var pause := _node("Canvas_Pause")
	pause.visible = false
	game.pause_menu = pause
	game.resume_button = _button(_node("Button (3)", "Canvas_Pause"), game, "_on_resume_pressed")
	var back := _button(_node("Button (2)", "Canvas_Pause"), game, "_on_menu_pressed")
	game.music_slider = _slider(_node("Slider_Mus", "Canvas_Pause"), game, "_on_music_value_changed")
	game.sound_slider = _slider(_node("Slider_Snd", "Canvas_Pause"), game, "_on_sound_value_changed")
	game.rumble_toggle = _toggle(_node("Toggle", "Canvas_Pause"), game, "_on_rumble_toggled")
	# Top to bottom on the screen.
	_chain([game.sound_slider, game.music_slider, game.rumble_toggle, game.resume_button, back])


## The jukebox of Unity's prefab "Music": one AudioStreamPlayer per piece, named "Artist -
## Title", and the panel that names the piece that starts.
func _jukebox() -> void:
	var prefab: Dictionary = unity.data.prefabs.Music
	var tree: Array = unity.build_tree(prefab.nodes, scene_root, scene_root)
	var holder: Node = tree[0]
	var f := {}
	for comp: Dictionary in prefab.nodes[0].components:
		if comp.type == "Jukebox":
			f = comp.fields
	var jukebox := Node.new()
	jukebox.name = "Jukebox"
	var canvas: Node = tree[1]
	canvas.reparent(scene_root, false)
	canvas.owner = scene_root
	holder.replace_by(jukebox)
	holder.free()
	jukebox.owner = scene_root
	for child in jukebox.get_children():
		_remove(child)
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
		player.volume_db = linear_to_db(0.6)
		player.bus = &"Music"
		var song := "%s - %s" % [artists[k] if k < artists.size() else "?", titles[k] if k < titles.size() else "?"]
		_add(jukebox, player, song.replace(".", "").replace(":", "").replace("&", "and"), scene_root)
	jukebox.artist_label = tree[3]
	jukebox.title_label = tree[4]
	jukebox.panel = tree[2]
	jukebox.show_time = 6.0
	# The face buttons move the player; next_song has one of them.
	jukebox.can_skip = false


# --- The title ---------------------------------------------------------------------------------

func _title() -> int:
	unity = _importer(TITLE_EXPORT_DIR)
	if unity == null:
		return FAILED
	nodes = unity.data.nodes
	scene_root = Node3D.new()
	scene_root.name = "NeonLeagueTitle"
	_environment()
	made = unity.build_tree(nodes, scene_root, scene_root, _skip)
	_arena()
	var camera := _node("Main Camera") as Camera3D
	camera.set_script(load("res://components/neon_camera.gd"))
	camera.current = true
	camera.near = 0.3
	camera.far = 100.0
	camera.spin_speed = float(_fields(_index("Main Camera"), "Rotater").get("speed", 5.0))
	camera.back = 0.0
	camera.shake_amount = 0.0
	var music := _node("Music").get_node_or_null("AudioSource") as AudioStreamPlayer
	if music != null:
		music.bus = &"Music"

	_fill_panels()
	var menu := _add(scene_root, Node.new(), "Menu", scene_root)
	menu.set_script(load("res://components/neon_menu.gd"))
	menu.main_menu = _node("Canvas_Menu")
	menu.options = _node("Canvas_Options")
	menu.credits = _node("Canvas_Credits")
	var main_buttons := []
	for name in ["Button", "Button (1)", "Button (2)", "Button (3)"]:
		var old := _node(name, "Canvas_Menu") as Control
		var label := _label_in(old)
		var text := label.text.to_lower() if label != null else ""
		var method := "_on_play_pressed"
		if text.contains("option"):
			method = "_on_options_pressed"
		elif text.contains("credit"):
			method = "_on_credits_pressed"
		elif text.contains("exit"):
			method = "_on_exit_pressed"
		var button := _button(old, menu, method)
		main_buttons.append(button)
		if method == "_on_play_pressed":
			menu.play_button = button
	menu.options_back = _button(_node("Button (2)", "Canvas_Options"), menu, "_on_back_pressed")
	menu.credits_back = _button(_node("Button (2)", "Canvas_Credits"), menu, "_on_back_pressed")
	menu.music_slider = _slider(_node("Slider_Mus", "Canvas_Options"), menu, "_on_music_value_changed")
	menu.sound_slider = _slider(_node("Slider_Snd", "Canvas_Options"), menu, "_on_sound_value_changed")
	menu.rumble_toggle = _toggle(_node("Toggle", "Canvas_Options"), menu, "_on_rumble_toggled")
	_chain(main_buttons)
	_chain([menu.sound_slider, menu.music_slider, menu.rumble_toggle, menu.options_back])
	menu.game_scene = OUTPUT
	menu.exit_scene = MENU
	menu.background_time = 0.5
	var status := _save(scene_root, TITLE)
	scene_root.free()
	print("neonleague: title (%d warnings)" % unity.warnings.size())
	return status
