extends SceneTree
## Builds scenes/platformer_2d.tscn from the text map below, with the tiles of Kenney's
## "Pixel Platformer" pack (assets/kenney/pixel-platformer, CC0). The result is an
## ordinary scene that can be opened and edited in Godot; running this tool again
## overwrites it.
##
##   godot --headless --path . --script res://tools/make_platformer_2d.gd
##
## Ground, platforms, crates and decoration are one TileMapLayer whose TileSet cuts the
## tiles out of the pack's atlas; the solid tiles have a collision rectangle there. Coins,
## spikes and the flag are Area2D nodes whose groups tell the player component
## (runtime/game/platformer_2d.c) what they are.

const TILE := 18
const TILES := "res://assets/kenney/pixel-platformer/tiles/tile_%04d.png"
const ATLAS := "res://assets/kenney/pixel-platformer/tilemap_packed.png"
## Tiles per row of the atlas; tile number n is the same picture as tiles/tile_n.png.
const ATLAS_COLUMNS := 20
const CHARACTERS := "res://assets/kenney/pixel-platformer/characters/tile_%04d.png"
const OUTPUT := "res://scenes/platformer_2d.tscn"

## # ground   = floating platform   B crate   o coin   ^ spikes   F flag   | flag pole
## P player   t tree   m mushroom   p plant   s sign   c cloud (three tiles wide)
const MAP: Array[String] = [
	"............................................................",
	"....c...............c..................c.............c......",
	"...........c..................c.................c...........",
	"...............................o.o..........................",
	"............................................................",
	"..............................=====.........................",
	"..................oo........................ooo.............",
	"...........o.....====.......................===.........F...",
	"...........................B............................|...",
	"..P..t..p..B...........^.BBB..m.....^........t....oo.s..|.t.",
	"##############..########################...#################",
	"##############..########################...#################",
	"##############..########################...#################",
]

const DECOR := {"t": 126, "m": 128, "p": 125, "s": 85}

var _root: Node2D
var _textures := {}
var _tile_shape := RectangleShape2D.new()
var _shared_frames := {}
var _tile_set := TileSet.new()
var _atlas := TileSetAtlasSource.new()


func _initialize() -> void:
	_root = Node2D.new()
	_root.name = "Platformer2D"
	# Pixel art: no smoothing. The children inherit the setting.
	_root.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST

	_tile_set.tile_size = Vector2i(TILE, TILE)
	_tile_set.add_physics_layer()
	_atlas.texture = load(ATLAS)
	_atlas.texture_region_size = Vector2i(TILE, TILE)
	_tile_set.add_source(_atlas, 0)

	var world := _add(_root, Node2D.new(), "World")
	var tiles := _add(world, TileMapLayer.new(), "Tiles") as TileMapLayer
	tiles.tile_set = _tile_set
	var items := _add(world, Node2D.new(), "Items")
	_tile_shape.size = Vector2(TILE, TILE)

	# The solid tiles carry their collision in the TileSet. A wall at each end of the
	# level keeps the player inside.
	var width := MAP[0].length() * TILE
	var height := MAP.size() * TILE
	_body(world, "WallLeft", Rect2(-TILE, -height, TILE, height * 2))
	_body(world, "WallRight", Rect2(width, -height, TILE, height * 2))

	var start := Vector2.ZERO
	for row in MAP.size():
		for column in MAP[row].length():
			var cell := MAP[row][column]
			var at := Vector2(column * TILE + TILE / 2, row * TILE + TILE / 2)
			var where := Vector2i(column, row)
			var label := "%d_%d" % [column, row]
			match cell:
				"#":
					var top := _cell(column, row - 1) != "#"
					var base := 21 if top else 121
					_tile(tiles, where, base + _edge(column, row, "#"), true)
				"=":
					var edge := _edge(column, row, "=")
					var single := _cell(column - 1, row) != "=" and _cell(column + 1, row) != "="
					_tile(tiles, where, 0 if single else 1 + edge, true)
				"B":
					_tile(tiles, where, 26, true)
				"^":
					_sprite(_area(items, "Spikes" + label, at, "hazard"), "Sprite", 68, Vector2.ZERO)
				"|":
					_sprite(_area(items, "Pole" + label, at, "goal"), "Sprite", 131, Vector2.ZERO)
				"o":
					_animated(items, "Coin" + label, [151, 152], 4.0, at, "coin")
				"F":
					_animated(items, "Flag" + label, [111, 112], 3.0, at, "goal")
				"c":
					for i in 3:
						_tile(tiles, where + Vector2i(i, 0), 153 + i, false)
				"P":
					start = Vector2(column * TILE + TILE / 2, (row + 1) * TILE)
				_:
					if DECOR.has(cell):
						_tile(tiles, where, DECOR[cell], false)

	# The HUD sits in a CanvasLayer, so the camera does not move it.
	var layer := _add(_root, CanvasLayer.new(), "HudLayer")
	var hud := _add(layer, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	var coins := _label(hud, "Coins", "COINS 0/0", Rect2(16, 10, 300, 28), 20)
	var hint := _label(hud, "Hint", "Left/Right: move   Space or A: jump   Esc or B: back", Rect2(20, 452, 600, 22), 14)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER

	var sound := _add(_root, AudioStreamPlayer.new(), "CoinSound") as AudioStreamPlayer
	sound.stream = load("res://assets/sounds/beep.wav")

	# The player comes last in the world, so it is drawn over tiles and items.
	var player := _add(world, CharacterBody2D.new(), "Player") as CharacterBody2D
	player.position = start
	player.set_script(load("res://components/platform_player_2d.gd"))
	# The same rectangle stops the player at solid tiles and, as an area, finds the items.
	var player_shape := RectangleShape2D.new()
	player_shape.size = Vector2(12, 20)
	var body_shape := _add(player, CollisionShape2D.new(), "Shape") as CollisionShape2D
	body_shape.shape = player_shape
	body_shape.position = Vector2(0, -10)
	var hitbox := _add(player, Area2D.new(), "Hitbox") as Area2D
	hitbox.monitorable = false
	var hitbox_shape := _add(hitbox, CollisionShape2D.new(), "Shape") as CollisionShape2D
	hitbox_shape.shape = player_shape
	hitbox_shape.position = Vector2(0, -10)
	hitbox.area_entered.connect(Callable(player, "_on_hitbox_area_entered"), CONNECT_PERSIST)
	# The node origin is between the feet.
	var sprite := _add(player, AnimatedSprite2D.new(), "Sprite") as AnimatedSprite2D
	sprite.position = Vector2(0, -12)
	sprite.sprite_frames = _frames({
		"idle": [CHARACTERS % 0], "walk": [CHARACTERS % 0, CHARACTERS % 1], "jump": [CHARACTERS % 1],
	}, 8.0)
	sprite.animation = &"idle"
	# The camera shows the level at twice its size and stops at its ends. The limits
	# leave exactly one screen of height, with the bottom row at the screen's edge.
	var camera := _add(player, Camera2D.new(), "Camera") as Camera2D
	camera.zoom = Vector2(2, 2)
	camera.limit_left = 0
	camera.limit_right = width
	camera.limit_top = height - 240
	camera.limit_bottom = height
	player.set("fall_limit", float(height + 60))
	player.set("sprite", sprite)
	player.set("coin_label", coins)
	player.set("coin_sound", sound)
	player.set("menu_scene", "res://scenes/main_menu.tscn")

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	_root.free()
	if status != OK:
		printerr("cannot write %s (%s)" % [OUTPUT, error_string(status)])
		quit(1)
		return
	print("wrote ", OUTPUT)
	quit(0)


func _cell(column: int, row: int) -> String:
	if row < 0 or row >= MAP.size() or column < 0 or column >= MAP[row].length():
		return "."
	return MAP[row][column]


## 0 for the left end of a run of equal cells, 2 for the right end, 1 in between.
func _edge(column: int, row: int, cell: String) -> int:
	if _cell(column - 1, row) != cell:
		return 0
	return 2 if _cell(column + 1, row) != cell else 1


## Adds a node below `parent` and makes it part of the saved scene.
func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node


func _texture(path: String) -> Texture2D:
	if not _textures.has(path):
		_textures[path] = load(path)
	return _textures[path]


func _sprite(parent: Node, node_name: String, tile: int, at: Vector2) -> Sprite2D:
	var sprite := _add(parent, Sprite2D.new(), node_name) as Sprite2D
	sprite.texture = _texture(TILES % tile)
	sprite.position = at
	return sprite


## Puts tile number `tile` of the atlas into a cell. The tile is added to the TileSet the
## first time it is used; a solid one gets a collision rectangle of its full size.
func _tile(layer: TileMapLayer, cell: Vector2i, tile: int, solid: bool) -> void:
	var coords := Vector2i(tile % ATLAS_COLUMNS, tile / ATLAS_COLUMNS)
	if not _atlas.has_tile(coords):
		_atlas.create_tile(coords)
		if solid:
			var half := TILE / 2.0
			var data := _atlas.get_tile_data(coords, 0)
			data.add_collision_polygon(0)
			data.set_collision_polygon_points(0, 0, PackedVector2Array([
				Vector2(-half, -half), Vector2(half, -half), Vector2(half, half), Vector2(-half, half)]))
	layer.set_cell(cell, 0, coords)


## A rectangle the player cannot walk through.
func _body(parent: Node, node_name: String, rect: Rect2) -> void:
	var body := _add(parent, StaticBody2D.new(), node_name) as StaticBody2D
	body.position = rect.get_center()
	var shape := RectangleShape2D.new()
	shape.size = rect.size
	(_add(body, CollisionShape2D.new(), "Shape") as CollisionShape2D).shape = shape


## An area of one tile that the player's hitbox reports; the group says what it is.
func _area(parent: Node, node_name: String, at: Vector2, group: String) -> Area2D:
	var area := _add(parent, Area2D.new(), node_name) as Area2D
	area.position = at
	area.monitoring = false
	area.add_to_group(group, true)
	(_add(area, CollisionShape2D.new(), "Shape") as CollisionShape2D).shape = _tile_shape
	return area


## An area with an AnimatedSprite2D that plays the given tiles in a loop. Areas with the
## same pictures share one SpriteFrames resource.
func _animated(parent: Node, node_name: String, tiles: Array, fps: float, at: Vector2, group: String) -> void:
	var key := "%s@%s" % [tiles, fps]
	if not _shared_frames.has(key):
		var paths: Array = []
		for tile in tiles:
			paths.append(TILES % tile)
		_shared_frames[key] = _frames({"default": paths}, fps)
	var sprite := _add(_area(parent, node_name, at, group), AnimatedSprite2D.new(), "Sprite") as AnimatedSprite2D
	sprite.sprite_frames = _shared_frames[key]
	sprite.autoplay = "default"


## A SpriteFrames resource: animation name -> texture paths, all looping at `fps`.
func _frames(animations: Dictionary, fps: float) -> SpriteFrames:
	var frames := SpriteFrames.new()
	frames.remove_animation(&"default")
	for animation_name: String in animations:
		frames.add_animation(animation_name)
		frames.set_animation_speed(animation_name, fps)
		frames.set_animation_loop(animation_name, true)
		for path: String in animations[animation_name]:
			frames.add_frame(animation_name, _texture(path))
	return frames


func _label(parent: Node, node_name: String, text: String, rect: Rect2, font_size: int) -> Label:
	var label := _add(parent, Label.new(), node_name) as Label
	label.position = rect.position
	label.size = rect.size
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	return label
