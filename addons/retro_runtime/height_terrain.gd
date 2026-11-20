@tool
class_name HeightTerrain
extends StaticBody3D
## Ground from a height map, made in the editor.
##
## Add this node to a scene, give it a noise or a grey image and press "Rebuild" (changing
## a value rebuilds as well). It writes plain nodes below itself, which are saved with the
## scene and are all the exporter sees:
##
## - "Ground", a CollisionShape3D with a HeightMapShape3D, for bodies and rays,
## - "Chunk_x_z", one MeshInstance3D per piece of the ground. The pieces are what lets the
##   runtime leave out the ground that is not in view.
##
## The look fits what the consoles can draw: one texture that repeats, tinted by vertex
## colours from `colors` (by height) and `steep_color` (by slope). The light is baked into
## the vertex colours by the exporter. There is no texture blending; that needs shaders.
##
## This script is an editor helper, not a component: the exporter takes it off the node.

## Grey image, brighter is higher. Used if set; otherwise `noise` is.
@export var height_image: Texture2D:
	set(value):
		height_image = value
		_changed()
## Noise the heights are taken from if there is no image.
@export var noise: FastNoiseLite:
	set(value):
		if noise != null and noise.changed.is_connected(_changed):
			noise.changed.disconnect(_changed)
		noise = value
		if noise != null:
			noise.changed.connect(_changed)
		_changed()
## Cells along X and Z. Every cell is two triangles, so mind the console.
@export var cells := Vector2i(64, 64):
	set(value):
		cells = value.clamp(Vector2i(2, 2), Vector2i(256, 256))
		_changed()
## Metres per cell.
@export_range(0.25, 8.0, 0.25) var cell_size := 1.0:
	set(value):
		cell_size = value
		_changed()
## Height of the lowest place, metres.
@export var base_height := -2.0:
	set(value):
		base_height = value
		_changed()
## Metres from the lowest to the highest place.
@export var height := 8.0:
	set(value):
		height = value
		_changed()
## How much the ground is pulled down to `base_height` towards its border: 0 leaves it
## as it is, 1 makes an island.
@export_range(0.0, 1.0) var island := 0.0:
	set(value):
		island = value
		_changed()
## Cells per piece of mesh along each side.
@export_range(4, 64) var chunk_cells := 16:
	set(value):
		chunk_cells = value
		_changed()

@export_group("Look")
## Texture that repeats over the ground. On the Dreamcast its size must be a power of two.
@export var texture: Texture2D:
	set(value):
		texture = value
		_changed()
## Metres one repeat of the texture covers.
@export var texture_size := 4.0:
	set(value):
		texture_size = maxf(value, 0.01)
		_changed()
## Colour by height: the left end is the lowest place, the right end the highest.
@export var colors: Gradient:
	set(value):
		if colors != null and colors.changed.is_connected(_changed):
			colors.changed.disconnect(_changed)
		colors = value
		if colors != null:
			colors.changed.connect(_changed)
		_changed()
## Colour of steep ground, and the slope in degrees from which it is fully that colour.
@export var steep_color := Color(0.5, 0.48, 0.45):
	set(value):
		steep_color = value
		_changed()
@export_range(5.0, 89.0) var steep_angle := 50.0:
	set(value):
		steep_angle = value
		_changed()

@export_tool_button("Rebuild", "Reload") var rebuild_button := rebuild

var _heights := PackedFloat32Array()  # (cells.x + 1) * (cells.y + 1), metres
var _pending := false


## A value changed in the inspector. Nothing happens while the scene is loaded, and
## several changes in a row lead to one rebuild.
func _changed() -> void:
	if not Engine.is_editor_hint() or not is_node_ready() or _pending:
		return
	_pending = true
	rebuild.call_deferred()


## The height of the ground at a place in this node's own space, metres. Outside of the
## ground it is the height of the nearest border.
func height_at(x: float, z: float) -> float:
	if _heights.is_empty():
		_sample_heights()
	var gx := clampf(x / cell_size + cells.x * 0.5, 0.0, cells.x)
	var gz := clampf(z / cell_size + cells.y * 0.5, 0.0, cells.y)
	var cx := mini(int(gx), cells.x - 1)
	var cz := mini(int(gz), cells.y - 1)
	var fx := gx - cx
	var fz := gz - cz
	var h00 := _height(cx, cz)
	var h10 := _height(cx + 1, cz)
	var h01 := _height(cx, cz + 1)
	var h11 := _height(cx + 1, cz + 1)
	# The same two triangles per cell as the mesh and the collision shape have.
	if fx + fz <= 1.0:
		return h00 + (h10 - h00) * fx + (h01 - h00) * fz
	return h11 - (h11 - h01) * (1.0 - fx) - (h11 - h10) * (1.0 - fz)


## Throws away the nodes of the last build and makes them anew.
func rebuild() -> void:
	_pending = false
	for child in get_children():
		if child.name == "Ground" or String(child.name).begins_with("Chunk_"):
			remove_child(child)
			child.queue_free()
	_sample_heights()
	# Nodes are saved with the scene if they belong to its root.
	var scene_owner: Node = owner if owner != null else self

	# Godot's height map has its points one unit apart, so the shape is scaled to the
	# cell size, the same along all axes, and the heights are given in cells.
	var shape := HeightMapShape3D.new()
	shape.map_width = cells.x + 1
	shape.map_depth = cells.y + 1
	var map_data := PackedFloat32Array()
	map_data.resize(_heights.size())
	for i in _heights.size():
		map_data[i] = _heights[i] / cell_size
	shape.map_data = map_data
	var ground := CollisionShape3D.new()
	ground.name = "Ground"
	ground.shape = shape
	ground.scale = Vector3.ONE * cell_size
	add_child(ground)
	ground.owner = scene_owner

	var material := StandardMaterial3D.new()
	material.albedo_texture = texture
	material.vertex_color_use_as_albedo = true
	for chunk_z in range(0, cells.y, chunk_cells):
		for chunk_x in range(0, cells.x, chunk_cells):
			var chunk := MeshInstance3D.new()
			chunk.name = "Chunk_%d_%d" % [chunk_x / chunk_cells, chunk_z / chunk_cells]
			chunk.mesh = _chunk_mesh(chunk_x, chunk_z, material)
			add_child(chunk)
			chunk.owner = scene_owner


func _height(x: int, z: int) -> float:
	return _heights[clampi(z, 0, cells.y) * (cells.x + 1) + clampi(x, 0, cells.x)]


func _sample_heights() -> void:
	var width := cells.x + 1
	var depth := cells.y + 1
	var image: Image = null
	if height_image != null:
		image = height_image.get_image()
	if image != null:
		if image.is_compressed():
			image.decompress()
		image.resize(width, depth, Image.INTERPOLATE_BILINEAR)
	_heights.resize(width * depth)
	for z in depth:
		for x in width:
			var level := 0.5
			if image != null:
				level = image.get_pixel(x, z).r
			elif noise != null:
				level = noise.get_noise_2d(x * cell_size, z * cell_size) * 0.5 + 0.5
			if island > 0.0:
				# 1 in the middle, 0 at the border, with a soft shoulder.
				var d := Vector2(x / float(cells.x) - 0.5, z / float(cells.y) - 0.5).length() * 2.0
				level *= lerpf(1.0, 1.0 - smoothstep(0.35, 1.0, d), island)
			_heights[z * width + x] = base_height + clampf(level, 0.0, 1.0) * height


## The mesh of the cells from (first_x, first_z) on, chunk_cells along each side.
func _chunk_mesh(first_x: int, first_z: int, material: Material) -> ArrayMesh:
	var count_x := mini(chunk_cells, cells.x - first_x)
	var count_z := mini(chunk_cells, cells.y - first_z)
	var positions := PackedVector3Array()
	var normals := PackedVector3Array()
	var vertex_colors := PackedColorArray()
	var uvs := PackedVector2Array()
	var indices := PackedInt32Array()
	var steep_cos := cos(deg_to_rad(steep_angle))
	for z in count_z + 1:
		for x in count_x + 1:
			var gx := first_x + x
			var gz := first_z + z
			var h := _height(gx, gz)
			var pos := Vector3((gx - cells.x * 0.5) * cell_size, h, (gz - cells.y * 0.5) * cell_size)
			# The slope from the neighbours on the whole map, so the light does not show
			# where one piece ends and the next begins.
			var normal := Vector3(_height(gx - 1, gz) - _height(gx + 1, gz), 2.0 * cell_size,
					_height(gx, gz - 1) - _height(gx, gz + 1)).normalized()
			var color := Color.WHITE
			if colors != null:
				color = colors.sample(clampf((h - base_height) / maxf(height, 0.001), 0.0, 1.0))
			color = color.lerp(steep_color, smoothstep(1.0, steep_cos, normal.y))
			positions.append(pos)
			normals.append(normal)
			vertex_colors.append(color)
			uvs.append(Vector2(pos.x, pos.z) / texture_size)
	var row := count_x + 1
	for z in count_z:
		for x in count_x:
			var p00 := z * row + x
			# Godot's fronts are clockwise seen from above; the cell is split along the
			# diagonal from (x + 1, z) to (x, z + 1), as in the collision shape.
			indices.append_array([p00, p00 + 1, p00 + row, p00 + 1, p00 + row + 1, p00 + row])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = positions
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = vertex_colors
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	return mesh
