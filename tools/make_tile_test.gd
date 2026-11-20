extends SceneTree
## Builds scenes/tests/tile_test.tscn: tiles that are flipped, turned and animated, and
## one-way platforms made of tiles and of a CollisionShape2D.
##
##   godot --headless --path . --script res://tools/make_tile_test.gd
##
## A floor of solid tiles, above it a row of tiles whose collision is one-way, and next to
## that a StaticBody2D ledge with a one-way shape. The component (TileTest in
## runtime/game/test_features.c) sends bodies up through the platforms from below, lets
## them come down onto them, walks one sideways through the row, and prints where they
## end up. To the right, one tile in its four orientations and an animated tile, to look at.

const OUTPUT := "res://scenes/tests/tile_test.tscn"
const ATLAS := "res://assets/kenney/pixel-platformer/tilemap_packed.png"
const ATLAS_COLUMNS := 20
const TILE := 18
const CHARACTER := "res://assets/kenney/pixel-platformer/characters/tile_0000.png"

const FLOOR_TILE := 2
const PLATFORM_TILE := 146
const SIGN_TILE := 88
const ANIMATED_TILE := 151  # and the two tiles to its right

var _root: Node2D
var _atlas := TileSetAtlasSource.new()


func _initialize() -> void:
	_root = Node2D.new()
	_root.name = "TileTest"
	_root.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_root.set_script(load("res://components/tests/tile_test.gd"))

	var tile_set := TileSet.new()
	tile_set.tile_size = Vector2i(TILE, TILE)
	tile_set.add_physics_layer()
	_atlas.texture = load(ATLAS)
	_atlas.texture_region_size = Vector2i(TILE, TILE)
	tile_set.add_source(_atlas, 0)

	var tiles := TileMapLayer.new()
	tiles.name = "Tiles"
	_root.add_child(tiles)
	tiles.owner = _root
	tiles.tile_set = tile_set

	var floor_tile := _make_tile(FLOOR_TILE, true, false)
	var platform_tile := _make_tile(PLATFORM_TILE, true, true)
	var sign_tile := _make_tile(SIGN_TILE, false, false)
	var animated_tile := _make_tile(ANIMATED_TILE, false, false)
	_atlas.set_tile_animation_frames_count(animated_tile, 3)
	for frame in 3:
		_atlas.set_tile_animation_frame_duration(animated_tile, frame, 0.2 + 0.1 * frame)

	for x in 30:
		tiles.set_cell(Vector2i(x, 20), 0, floor_tile)
	for x in range(4, 10):
		tiles.set_cell(Vector2i(x, 14), 0, platform_tile)
	# As it is, mirrored, upside down, and turned a quarter to the right.
	var transforms: Array[int] = [0, TileSetAtlasSource.TRANSFORM_FLIP_H, TileSetAtlasSource.TRANSFORM_FLIP_V,
			TileSetAtlasSource.TRANSFORM_TRANSPOSE | TileSetAtlasSource.TRANSFORM_FLIP_H]
	for i in transforms.size():
		tiles.set_cell(Vector2i(20 + i * 2, 12), 0, sign_tile, transforms[i])
	tiles.set_cell(Vector2i(20, 16), 0, animated_tile)
	tiles.set_cell(Vector2i(22, 16), 0, animated_tile)

	# The ledge: a one-way shape on a static body. Its top is at y = 235.
	var ledge := _add(_root, StaticBody2D.new(), "Ledge") as StaticBody2D
	ledge.position = Vector2(260, 240)
	var ledge_shape := _add(ledge, CollisionShape2D.new(), "Shape") as CollisionShape2D
	var ledge_box := RectangleShape2D.new()
	ledge_box.size = Vector2(80, 10)
	ledge_shape.shape = ledge_box
	ledge_shape.one_way_collision = true
	var ledge_look := _add(ledge, ColorRect.new(), "Look") as ColorRect
	ledge_look.position = Vector2(-40, -5)
	ledge_look.size = Vector2(80, 10)
	ledge_look.color = Color(0.9, 0.7, 0.2)

	var box := RectangleShape2D.new()
	box.size = Vector2(12, 16)
	var ball := CircleShape2D.new()
	ball.radius = 8.0
	# Up through the row of tiles from below and down onto it: a box and a ball, which
	# the runtime moves in two different ways.
	_root.jumper = _body("Jumper", Vector2(100, 330), box)
	_root.roller = _body("Roller", Vector2(140, 330), ball)
	# Down onto the ledge from above, and up through it from below.
	_root.faller = _body("Faller", Vector2(240, 180), box)
	_root.riser = _body("Riser", Vector2(280, 330), box)
	# Sideways through the row of tiles, at its height.
	_root.sider = _body("Sider", Vector2(40, 261), box)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


## Adds tile number `tile` of the atlas to the tile set, with a collision rectangle of its
## full size if it is solid. Returns its atlas coordinates.
func _make_tile(tile: int, solid: bool, one_way: bool) -> Vector2i:
	@warning_ignore("integer_division")
	var coords := Vector2i(tile % ATLAS_COLUMNS, tile / ATLAS_COLUMNS)
	_atlas.create_tile(coords)
	if solid:
		var half := TILE / 2.0
		var data := _atlas.get_tile_data(coords, 0)
		data.add_collision_polygon(0)
		data.set_collision_polygon_points(0, 0, PackedVector2Array([
			Vector2(-half, -half), Vector2(half, -half), Vector2(half, half), Vector2(-half, half)]))
		data.set_collision_polygon_one_way(0, 0, one_way)
	return coords


func _body(body_name: String, at: Vector2, shape: Shape2D) -> CharacterBody2D:
	var body := _add(_root, CharacterBody2D.new(), body_name) as CharacterBody2D
	body.position = at
	var body_shape := _add(body, CollisionShape2D.new(), "Shape") as CollisionShape2D
	body_shape.shape = shape
	# A sprite, not a control: it has to move with the body.
	var look := _add(body, Sprite2D.new(), "Look") as Sprite2D
	look.texture = load(CHARACTER)
	return body


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
