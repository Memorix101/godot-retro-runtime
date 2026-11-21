extends SceneTree
## Builds scenes/tests/rigid2d_test.tscn: RigidBody2D nodes that fall, turn, stack, bounce
## and slide, on a StaticBody2D, on tiles, and through one-way platforms.
##
##   godot --headless --path . --script res://tools/make_rigid2d_test.gd
##
## The component (Rigid2DTest in runtime/game/test_features.c) checks where the bodies are
## after five seconds, and prints that. tools/rigid2d_reference.gd prints the same line
## from Godot's own physics, and `make test` compares the two. Everything that is printed
## is a body at rest, rounded to the pixel: on the way there two physics engines differ.

const OUTPUT := "res://scenes/tests/rigid2d_test.tscn"
const ATLAS := "res://assets/kenney/pixel-platformer/tilemap_packed.png"
const ATLAS_COLUMNS := 20
const TILE := 18
const LOOK := "res://assets/kenney/pixel-platformer/characters/tile_0000.png"
const FLOOR_TILE := 2
const PLATFORM_TILE := 146

var _root: Node2D
var _atlas := TileSetAtlasSource.new()


func _initialize() -> void:
	_root = Node2D.new()
	_root.name = "Rigid2DTest"
	_root.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_root.set_script(load("res://components/tests/rigid2d_test.gd"))

	# The floor: its top is at y = 390.
	var floor_body := _add(_root, StaticBody2D.new(), "Floor") as StaticBody2D
	floor_body.position = Vector2(300, 400)
	_shape(floor_body, _rect(600, 20))
	var floor_look := _add(floor_body, ColorRect.new(), "Look") as ColorRect
	floor_look.position = Vector2(-300, -10)
	floor_look.size = Vector2(600, 20)
	floor_look.color = Color(0.4, 0.5, 0.4)

	# The ledge: a one-way shape. Its top is at y = 295.
	var ledge := _add(_root, StaticBody2D.new(), "Ledge") as StaticBody2D
	ledge.position = Vector2(480, 300)
	_shape(ledge, _rect(80, 10)).one_way_collision = true
	var ledge_look := _add(ledge, ColorRect.new(), "Look") as ColorRect
	ledge_look.position = Vector2(-40, -5)
	ledge_look.size = Vector2(80, 10)
	ledge_look.color = Color(0.9, 0.7, 0.2)

	# Tiles to the right: a solid row with its top at y = 360, and a one-way row with its
	# top at y = 252.
	var tile_set := TileSet.new()
	tile_set.tile_size = Vector2i(TILE, TILE)
	tile_set.add_physics_layer()
	_atlas.texture = load(ATLAS)
	_atlas.texture_region_size = Vector2i(TILE, TILE)
	tile_set.add_source(_atlas, 0)
	var tiles := _add(_root, TileMapLayer.new(), "Tiles") as TileMapLayer
	tiles.position = Vector2(700, 0)
	tiles.tile_set = tile_set
	var floor_tile := _make_tile(FLOOR_TILE, false)
	var platform_tile := _make_tile(PLATFORM_TILE, true)
	for x in 10:
		tiles.set_cell(Vector2i(x, 20), 0, floor_tile)
	for x in range(2, 6):
		tiles.set_cell(Vector2i(x, 14), 0, platform_tile)

	# Dropped at an angle: comes to lie flat. It reports what it touches.
	var box := _body("Box", Vector2(100, 200), _rect(32, 32))
	box.rotation = 0.3
	box.contact_monitor = true
	box.max_contacts_reported = 4
	_root.box = box
	# Three boxes above each other with gaps: a stack.
	_root.stack_a = _body("StackA", Vector2(200, 370), _rect(32, 32))
	_root.stack_b = _body("StackB", Vector2(200, 330), _rect(32, 32))
	_root.stack_c = _body("StackC", Vector2(200, 290), _rect(32, 32))
	# A ball that bounces.
	var bouncy := PhysicsMaterial.new()
	bouncy.bounce = 0.6
	var circle := CircleShape2D.new()
	circle.radius = 16.0
	_root.ball = _body("Ball", Vector2(300, 100), circle)
	_root.ball.physics_material_override = bouncy
	# Its mask has nothing of the floor's layer: it falls through.
	_root.ghost = _body("Ghost", Vector2(380, 200), _rect(32, 32))
	_root.ghost.collision_mask = 2
	# Thrown up through the one-way ledge from below; comes down onto it.
	_root.riser = _body("Riser", Vector2(480, 360), _rect(32, 32))
	_root.riser.linear_velocity = Vector2(0, -600)
	# Lies on the floor without friction and cannot turn; the test gives it a push.
	var smooth := PhysicsMaterial.new()
	smooth.friction = 0.0
	_root.slider = _body("Slider", Vector2(560, 374), _rect(32, 32))
	_root.slider.physics_material_override = smooth
	_root.slider.lock_rotation = true
	_root.slider.mass = 2.0
	# Onto the solid tiles, and up through the one-way tiles.
	_root.tile_box = _body("TileBox", Vector2(835, 200), _rect(16, 16))
	_root.tile_riser = _body("TileRiser", Vector2(763, 330), _rect(16, 16))
	_root.tile_riser.linear_velocity = Vector2(0, -500)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _rect(width: float, height: float) -> RectangleShape2D:
	var rect := RectangleShape2D.new()
	rect.size = Vector2(width, height)
	return rect


func _shape(body: Node, shape: Shape2D) -> CollisionShape2D:
	var node := _add(body, CollisionShape2D.new(), "Shape") as CollisionShape2D
	node.shape = shape
	return node


## A full-size collision rectangle on tile number `tile` of the atlas. Returns its atlas
## coordinates.
func _make_tile(tile: int, one_way: bool) -> Vector2i:
	@warning_ignore("integer_division")
	var coords := Vector2i(tile % ATLAS_COLUMNS, tile / ATLAS_COLUMNS)
	_atlas.create_tile(coords)
	var half := TILE / 2.0
	var data := _atlas.get_tile_data(coords, 0)
	data.add_collision_polygon(0)
	data.set_collision_polygon_points(0, 0, PackedVector2Array([
		Vector2(-half, -half), Vector2(half, -half), Vector2(half, half), Vector2(-half, half)]))
	data.set_collision_polygon_one_way(0, 0, one_way)
	return coords


func _body(body_name: String, at: Vector2, shape: Shape2D) -> RigidBody2D:
	var body := _add(_root, RigidBody2D.new(), body_name) as RigidBody2D
	body.position = at
	_shape(body, shape)
	var look := _add(body, Sprite2D.new(), "Look") as Sprite2D
	look.texture = load(LOOK)
	return body


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
