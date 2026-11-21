extends RefCounted
## Walks a scene tree and writes it as a package for one platform.
##
## The file format is described in docs/package-format.md and read by
## runtime/core/rr_package.c. Change all three together.
##
## The scene must be inside the tree, in a viewport of the target's screen size, with at
## least one frame processed: the rectangles and focus neighbours of controls are taken
## as Godot laid them out (see platforms.gd).
##
## Anything the runtime cannot handle is an error that names the node. Errors are
## collected in `errors`, and no file is written if there is at least one.

const FORMAT_VERSION := 28
const MAX_Z_LEVELS := 16
const HEADER_SIZE := 136
const MAX_BUSES := 16
const NONE16 := 0xFFFF
const NONE32 := 0xFFFFFFFF

const NODE_NODE := 0
const NODE_NODE3D := 1
const NODE_MESH := 2
const NODE_CAMERA := 3
const NODE_AREA := 4
const NODE_SHAPE := 5
const NODE_NODE2D := 6
const NODE_SPRITE := 7
const NODE_CONTROL := 8
const NODE_LABEL := 9
const NODE_BUTTON := 10
const NODE_TEXTURE_RECT := 11
const NODE_AUDIO := 12
const NODE_ANIMATION_PLAYER := 13
const NODE_PARTICLES := 14
const NODE_STATIC_BODY := 15
const NODE_CHARACTER_BODY := 16
const NODE_ANIMATED_SPRITE := 17
const NODE_CAMERA2D := 18
const NODE_TIMER := 19
const NODE_ENVIRONMENT := 20
const NODE_TILEMAP := 21
const NODE_RAYCAST := 22
const NODE_PATH := 23
const NODE_PATH_FOLLOW := 24
const NODE_COLOR_RECT := 25
const NODE_PROGRESS_BAR := 26
const NODE_NINE_PATCH := 27
const NODE_AUDIO_SPATIAL := 28
const NODE_SPRITE3D := 29
const NODE_LABEL3D := 30
const NODE_RIGID_BODY := 31
const NODE_SLIDER := 32
const NODE_PARALLAX := 33
const NODE_PANEL := 34
const NODE_TEXTURE_PROGRESS := 35
const NODE_SCROLL := 36
const NODE_RICH_TEXT := 37

const FLAG_VISIBLE := 1
const FLAG_CURRENT_CAMERA := 2
const FLAG_AUTOLOAD := 128

const FIELD_FLOAT := 1
const FIELD_INT := 2
const FIELD_BOOL := 3
const FIELD_NODE := 4
const FIELD_SCENE := 5
const FIELD_PACKED_SCENE := 6
const FIELD_RESOURCE := 7

const ASSET_MESH := 1
const ASSET_TEXTURE := 2
const ASSET_FONT := 3
const ASSET_SOUND := 4
const ASSET_ANIMATION := 5
const ASSET_SPRITE_FRAMES := 6
const ASSET_TILEMAP := 7
const ASSET_CURVE := 8
const ASSET_MUSIC := 9
const ASSET_HEIGHT_MAP := 10
const ASSET_SKIN := 11

const AUDIO_PLAIN := 0
const AUDIO_2D := 1
const AUDIO_3D := 2

const PATH_ROTATION_NONE := 0
const PATH_ROTATION_Y := 1
const PATH_ROTATION_FORWARD := 2
const PATH_ROTATION_ORIENTED := 3

const TRACK_POSITION := 1
const TRACK_ROTATION := 2
const TRACK_SCALE := 3
const TRACK_VISIBLE := 4
const TRACK_POSITION_2D := 5
const TRACK_ROTATION_2D := 6
const TRACK_SCALE_2D := 7
const TRACK_FRAME := 8
const TRACK_VALUE := 9
const TRACK_METHOD := 10

## Upper limit of particles per system, RR_MAX_PARTICLES in the runtime.
const MAX_PARTICLES := 512

## Most instances one PackedScene field may ask for, and how deep instanced scenes may
## ask for instances of their own.
const MAX_POOL_INSTANCES := 1024
const MAX_POOL_DEPTH := 4

## Set on every instance of a pool but the first. Errors inside them are not reported,
## because the first instance has the same ones.
const POOL_COPY_META := &"retro_pool_copy"

## Set on the root node of every autoload that prepare() added to the scene.
const AUTOLOAD_META := &"retro_autoload"

## Set on the node that replaces a Skeleton3D: bone name -> the node of that bone.
const BONES_META := &"retro_bones"

## Set on a MeshInstance3D whose vertices follow more than one bone: {"bones": the node
## of every bind of its skin, "poses": the bind pose of each}.
const SKIN_META := &"retro_skin"

const BLEND_OPAQUE := 0
const BLEND_PUNCH_THROUGH := 1
const BLEND_TRANSLUCENT := 2

const SHAPE_BOX := 1
const SHAPE_SPHERE := 2
const SHAPE_CAPSULE := 3
const SHAPE_HEIGHT_MAP := 4

const TILE_FLIP_H := 1
const TILE_FLIP_V := 2
const TILE_TRANSPOSE := 4

const FILTER_LINEAR := 0
const FILTER_NEAREST := 1

## Godot class name -> node type in the package. The match is exact on purpose: a
## subclass with extra behaviour must not slip through as its base class.
## DirectionalLight3D is only an input for the light bake and becomes a plain Node3D.
## The containers only arrange their children in the editor; their result is baked into
## the rectangles, so they become plain controls.
const NODE_TYPES := {
	"Node": NODE_NODE,
	"Node3D": NODE_NODE3D,
	"MeshInstance3D": NODE_MESH,
	"Camera3D": NODE_CAMERA,
	"DirectionalLight3D": NODE_NODE3D,
	"OmniLight3D": NODE_NODE3D,
	"SpotLight3D": NODE_NODE3D,
	"Area3D": NODE_AREA,
	"CollisionShape3D": NODE_SHAPE,
	"Node2D": NODE_NODE2D,
	"Sprite2D": NODE_SPRITE,
	"Control": NODE_CONTROL,
	"VBoxContainer": NODE_CONTROL,
	"HBoxContainer": NODE_CONTROL,
	"CenterContainer": NODE_CONTROL,
	"MarginContainer": NODE_CONTROL,
	"Label": NODE_LABEL,
	"Button": NODE_BUTTON,
	"TextureRect": NODE_TEXTURE_RECT,
	"AudioStreamPlayer": NODE_AUDIO,
	"AudioStreamPlayer2D": NODE_AUDIO_SPATIAL,
	"AudioStreamPlayer3D": NODE_AUDIO_SPATIAL,
	"AnimationPlayer": NODE_ANIMATION_PLAYER,
	"CPUParticles3D": NODE_PARTICLES,
	"CPUParticles2D": NODE_PARTICLES,
	"Sprite3D": NODE_SPRITE3D,
	"Label3D": NODE_LABEL3D,
	"Area2D": NODE_AREA,
	"CollisionShape2D": NODE_SHAPE,
	"StaticBody3D": NODE_STATIC_BODY,
	"StaticBody2D": NODE_STATIC_BODY,
	"CharacterBody3D": NODE_CHARACTER_BODY,
	"CharacterBody2D": NODE_CHARACTER_BODY,
	"RigidBody3D": NODE_RIGID_BODY,
	"RigidBody2D": NODE_RIGID_BODY,
	"AnimatedSprite2D": NODE_ANIMATED_SPRITE,
	"Camera2D": NODE_CAMERA2D,
	"CanvasLayer": NODE_NODE,
	"Timer": NODE_TIMER,
	"WorldEnvironment": NODE_ENVIRONMENT,
	"TileMapLayer": NODE_TILEMAP,
	"Marker3D": NODE_NODE3D,
	"Marker2D": NODE_NODE2D,
	"RayCast3D": NODE_RAYCAST,
	"RayCast2D": NODE_RAYCAST,
	"ShapeCast3D": NODE_RAYCAST,
	"ShapeCast2D": NODE_RAYCAST,
	"Path3D": NODE_PATH,
	"Path2D": NODE_PATH,
	"PathFollow3D": NODE_PATH_FOLLOW,
	"PathFollow2D": NODE_PATH_FOLLOW,
	"ColorRect": NODE_COLOR_RECT,
	"ProgressBar": NODE_PROGRESS_BAR,
	"NinePatchRect": NODE_NINE_PATCH,
	"CheckBox": NODE_BUTTON,
	"CheckButton": NODE_BUTTON,
	"TextureButton": NODE_BUTTON,
	"HSlider": NODE_SLIDER,
	"VSlider": NODE_SLIDER,
	"Parallax2D": NODE_PARALLAX,
	"GridContainer": NODE_CONTROL,
	"HFlowContainer": NODE_CONTROL,
	"VFlowContainer": NODE_CONTROL,
	"Panel": NODE_PANEL,
	"PanelContainer": NODE_PANEL,
	"HSeparator": NODE_PANEL,
	"VSeparator": NODE_PANEL,
	"TextureProgressBar": NODE_TEXTURE_PROGRESS,
	"ScrollContainer": NODE_SCROLL,
	"RichTextLabel": NODE_RICH_TEXT,
}

## Built-in actions the runtime's button navigation uses. They are always exported;
## other ui_* actions are not.
const UI_ACTIONS: Array[String] = ["ui_left", "ui_up", "ui_right", "ui_down", "ui_accept", "ui_cancel"]

## Characters baked into every font: ASCII and the Latin-1 supplement.
const FONT_RANGES: Array[Vector2i] = [Vector2i(32, 126), Vector2i(160, 255)]

## Light bake. Lit vertices get albedo * (ambient + light colour * energy * N.L). The
## ambient light comes from the WorldEnvironment of the scene; this is the value without one.
const DEFAULT_AMBIENT := 0.4
const DEFAULT_LIGHT_DIRECTION := Vector3(-0.4, -1.0, -0.3)

var errors: PackedStringArray = []

var _baker
var _root: Node
var _nodes: Array[Node] = []
var _node_index := {}  # Node -> index
var _strings := PackedByteArray()
var _string_offsets := {}  # String -> offset
var _light_direction := DEFAULT_LIGHT_DIRECTION.normalized()
var _light_color := Color.WHITE
var _sun_lights: Array = []  # [direction, colour] of every DirectionalLight3D
var _point_lights: Array[Dictionary] = []  # the OmniLight3D and SpotLight3D nodes
var _ambient := Color(DEFAULT_AMBIENT, DEFAULT_AMBIENT, DEFAULT_AMBIENT)
var _clear_color := Color.BLACK
var _sky_faces: Array[Image] = []  # six pictures if the environment has a sky

var _pools: Array[Dictionary] = []  # name, instances (Array[Node]), depth
var _pool_of := {}  # "<instance id of the node>/<field>" -> pool index
var _prepare_errors: PackedStringArray = []
var _resources: Array[Resource] = []  # custom resources, in the order of the resource table
var _resource_users: Array[Node] = []  # for each, the node that referred to it first
var _music_files := {}  # file name -> PackedByteArray, written next to the package

var _asset_names: Array[String] = []
var _asset_types: Array[int] = []
var _asset_data: Array[PackedByteArray] = []
var _asset_cache := {}  # resource or key -> asset index
var _disabled := {}  # buttons that are disabled in the scene -> true


## Makes the instances that the runtime hands out with rr_instantiate. Call it once
## with the scene's root before the scene enters the tree and before export_scene().
##
## A component that exports a PackedScene must also export an int named `<field>_max`.
## That many instances of the scene are added as the last children of the root, so they
## are exported like any other node; the pool table of the package says where they are.
func prepare(root: Node) -> void:
	_root = root
	_pools = []
	_pool_of = {}
	errors = []
	_add_autoloads()
	_make_pools(root, 0)
	# Instanced scenes may ask for instances themselves.
	var done := 0
	while done < _pools.size():
		for instance: Node in _pools[done].instances:
			_make_pools(instance, _pools[done].depth)
		done += 1
	_prepare_errors = errors.duplicate()


## Adds the autoloads of the project (Project Settings > Globals) to the scene, below its
## root: the runtime has one scene at a time, so what Godot keeps next to the current
## scene is part of every package here.
func _add_autoloads() -> void:
	for property in ProjectSettings.get_property_list():
		var setting: String = property.name
		if not setting.begins_with("autoload/"):
			continue
		var path := String(ProjectSettings.get_setting(setting)).trim_prefix("*")
		var resource := ResourceLoader.load(path)
		var node: Node = null
		if resource is PackedScene:
			node = resource.instantiate()
		elif resource is Script:
			node = ClassDB.instantiate(resource.get_instance_base_type()) as Node
			if node != null:
				node.set_script(resource)
		if node == null:
			errors.append("autoload '%s': cannot make a node from '%s'" % [setting.trim_prefix("autoload/"), path])
			continue
		node.name = setting.trim_prefix("autoload/")
		node.set_meta(AUTOLOAD_META, true)
		_root.add_child(node)


func _in_autoload(node: Node) -> bool:
	while node != null and node != _root:
		if node.has_meta(AUTOLOAD_META):
			return true
		node = node.get_parent()
	return false


func _make_pools(node: Node, depth: int) -> void:
	var script: Script = node.get_script()
	if script != null:
		for property in script.get_script_property_list():
			var usage: int = property.usage
			if not (usage & PROPERTY_USAGE_SCRIPT_VARIABLE) or not (usage & PROPERTY_USAGE_STORAGE):
				continue
			if (property.type != TYPE_OBJECT or property.hint != PROPERTY_HINT_RESOURCE_TYPE
					or property.hint_string != "PackedScene"):
				continue
			var field: String = property.name
			var packed := node.get(field) as PackedScene
			if packed == null:
				continue
			var count = node.get(field + "_max")
			if not (count is int) or count < 1 or count > MAX_POOL_INSTANCES:
				_fail(node, ("exported field '%s' is a PackedScene, so the script must also export an int "
						+ "'%s_max' from 1 to %d: how many instances can exist at the same time")
						% [field, field, MAX_POOL_INSTANCES])
				continue
			if depth >= MAX_POOL_DEPTH:
				_fail(node, "exported field '%s': instanced scenes ask for instances of each other more than %d deep"
						% [field, MAX_POOL_DEPTH])
				continue
			var instances: Array[Node] = []
			for k in count:
				var instance := packed.instantiate()
				if k > 0:
					instance.set_meta(POOL_COPY_META, true)
				_root.add_child(instance, true)
				instances.append(instance)
			_check_pooled(instances[0], field)
			_pool_of[_pool_key(node, field)] = _pools.size()
			_pools.append({
				"name": packed.resource_path.get_file().get_basename(),
				"instances": instances,
				"depth": depth + 1,
			})
	for child in node.get_children():
		# The instances that were just added to the root are visited by prepare().
		if not _is_pool_instance(child):
			_make_pools(child, depth)


func _is_pool_instance(node: Node) -> bool:
	for pool in _pools:
		if pool.instances.has(node):
			return true
	return false


## Nodes that only make sense once per scene, or that do not move with their parent.
func _check_pooled(node: Node, field: String) -> void:
	if node is Control or node is Camera3D or node is Camera2D or node is WorldEnvironment:
		_fail(node, "is not supported in a scene that is instanced at runtime (field '%s')" % field)
	for child in node.get_children():
		_check_pooled(child, field)


func _pool_key(node: Node, field: String) -> String:
	return "%d/%s" % [node.get_instance_id(), field]


func _subtree_size(node: Node) -> int:
	var size := 1
	for child in node.get_children():
		size += _subtree_size(child)
	return size


## Writes the pool table: where the instances of each PackedScene field are.
func _write_pools() -> StreamPeerBuffer:
	var out := _new_buffer()
	for pool in _pools:
		var instances: Array[Node] = pool.instances
		var first: int = _node_index.get(instances[0], -1)
		var size := _subtree_size(instances[0])
		for k in instances.size():
			if first < 0 or _node_index.get(instances[k], -1) != first + k * size:
				errors.append("internal error: the instances of '%s' are not stored one after the other" % pool.name)
				break
		out.put_u32(_string(pool.name))
		out.put_u16(maxi(first, 0))
		out.put_u16(size)
		out.put_u16(instances.size())
		out.put_u16(0)
	return out


## Exports the tree below `root` with the given baker (see bakers/). Returns true and
## writes `out_path` on success. On failure `errors` says why and nothing is written.
func export_scene(root: Node, baker, out_path: String) -> bool:
	if root != _root:
		# prepare() was not called for this scene: it has no pools.
		_pools = []
		_pool_of = {}
		_prepare_errors = []
	errors = _prepare_errors.duplicate()
	_baker = baker
	_root = root
	_nodes = []
	_node_index = {}
	_strings = PackedByteArray([0])
	_string_offsets = {"": 0}
	_asset_names = []
	_asset_types = []
	_asset_data = []
	_asset_cache = {}
	_resources = []
	_resource_users = []
	_music_files = {}

	_expand_instanced(root)
	_expand_skeletons(root)
	_collect(root)
	# Godot may leave a disabled button out when it looks for focus neighbours, but the
	# game can enable it later. So all are enabled here, and the state is noted.
	_disabled = {}
	for node in _nodes:
		if node is BaseButton and node.disabled:
			_disabled[node] = true
			node.disabled = false
	_check_canvas()
	if _nodes.size() >= NONE16:
		errors.append("the scene has %d nodes, the limit is %d" % [_nodes.size(), NONE16 - 1])
	if not errors.is_empty():
		return false
	_find_light()
	_find_environment()

	var nodes := _new_buffer()
	var node_data := _new_buffer()
	var tags := _new_buffer()
	var comps := _new_buffer()
	var fields := _new_buffer()
	var conns := _new_buffer()
	var actions := _new_buffer()
	var bindings := _new_buffer()
	var tag_count := 0
	var comp_count := 0
	var field_count := 0
	var levels: Array[Vector2i] = []  # the draw levels in use: z index, inside a CanvasLayer or not

	for i in _nodes.size():
		var node := _nodes[i]
		var type: int = NODE_TYPES[node.get_class()]
		var flags := 0
		var xform := Transform3D.IDENTITY

		if node is Node3D:
			if node.top_level:
				_fail(node, "top_level is not supported")
			xform = node.transform
		elif node is Node2D:
			if node.top_level:
				_fail(node, "top_level is not supported")
			var t: Transform2D = node.transform
			xform = Transform3D(Vector3(t.x.x, t.x.y, 0), Vector3(t.y.x, t.y.y, 0), Vector3(0, 0, 1),
					Vector3(t.origin.x, t.origin.y, 0))
		if not (node is Node3D or node is CanvasItem or node is CanvasLayer) or node.visible:
			flags |= FLAG_VISIBLE
		if node is Camera3D and node.current:
			flags |= FLAG_CURRENT_CAMERA
		if _in_autoload(node):
			flags |= FLAG_AUTOLOAD

		var data := _node_data(node, type)
		var data_offset := NONE32
		if data.get_size() > 0:
			data_offset = node_data.get_size()
			node_data.put_data(data.data_array)

		var groups: Array[String] = []
		for group in node.get_groups():
			# Groups starting with an underscore are Godot's own bookkeeping.
			if not String(group).begins_with("_"):
				groups.append(String(group))
		groups.sort()

		var has_component := node.get_script() != null

		nodes.put_u32(_string(node.name))
		nodes.put_u16(NONE16 if i == 0 else _node_index[node.get_parent()])
		nodes.put_u8(type)
		nodes.put_u8(flags)
		_put_transform(nodes, xform)
		nodes.put_u16(tag_count)
		nodes.put_u16(groups.size())
		nodes.put_u16(comp_count)
		nodes.put_u16(1 if has_component else 0)
		nodes.put_u32(data_offset)
		var level := _draw_level(node)
		nodes.put_16(level.x)
		nodes.put_u8(level.y)
		nodes.put_u8(1 if _translates(node) else 0)
		if not levels.has(level):
			levels.append(level)

		for group in groups:
			tags.put_u32(_string(group))
		tag_count += groups.size()

		if has_component:
			var written := _write_component(node, i, comps, fields, field_count)
			comp_count += 1
			field_count += written

	if levels.size() > MAX_Z_LEVELS:
		errors.append("the scene uses %d different draw levels (z_index values, inside and outside of a CanvasLayer); the limit is %d"
				% [levels.size(), MAX_Z_LEVELS])
	var resources := _write_resources(fields, field_count)
	field_count = fields.get_size() / 12
	var pools := _write_pools()
	var buses := _write_buses()
	var translations := _write_translations()
	var conn_count := _write_connections(conns)
	var action_count := _write_actions(actions, bindings)

	if not errors.is_empty():
		return false

	# Layout: header, the tables, the string table, then the asset data region.
	var asset_name_offsets: Array[int] = []
	for asset_name in _asset_names:
		asset_name_offsets.append(_string(asset_name))

	var sections: Array[StreamPeerBuffer] = [nodes, node_data, tags, comps, fields, conns, actions, bindings]
	var offsets: Array[int] = []
	var cursor := HEADER_SIZE
	for section in sections:
		offsets.append(cursor)
		cursor += section.get_size()
	var pools_off := cursor
	var resources_off := pools_off + pools.get_size()
	var buses_off := resources_off + resources.get_size()
	var translations_off := buses_off + buses.get_size()
	var assets_off := translations_off + translations.get_size()
	var strings_off := assets_off + _asset_data.size() * 16
	var data_off := _align4(strings_off + _strings.size())

	var assets := _new_buffer()
	var data := _new_buffer()
	for i in _asset_data.size():
		assets.put_u32(asset_name_offsets[i])
		assets.put_u8(_asset_types[i])
		assets.put_u8(0)
		assets.put_u16(0)
		assets.put_u32(data_off + data.get_size())
		assets.put_u32(_asset_data[i].size())
		data.put_data(_asset_data[i])
		_pad4(data)
	var file_size := data_off + data.get_size()

	var out := _new_buffer()
	out.put_data("RPKG".to_ascii_buffer())
	out.put_u8(_baker.platform_id())
	out.put_u8(1 if _baker.big_endian() else 0)
	out.put_u16(FORMAT_VERSION)
	out.put_u32(file_size)
	# The table list of the header, in its fixed order: offset, then count or size.
	var counts: Array[int] = [_nodes.size(), node_data.get_size(), tag_count, comp_count, field_count,
			conn_count, action_count, bindings.get_size() / 4]
	out.put_u32(strings_off)
	out.put_u32(_strings.size())
	for i in sections.size():
		out.put_u32(offsets[i])
		out.put_u32(counts[i])
	out.put_u32(assets_off)
	out.put_u32(_asset_data.size())
	out.put_u32(data_off)
	out.put_u32(data.get_size())
	out.put_u32(_rgba(Color(_clear_color.r, _clear_color.g, _clear_color.b, 1.0)))
	out.put_u32(pools_off)
	out.put_u32(_pools.size())
	out.put_u32(resources_off)
	out.put_u32(_resources.size())
	out.put_u32(buses_off)
	out.put_u32(buses.get_size() / 12)
	out.put_u32(translations_off)
	out.put_u32(translations.get_size())
	assert(out.get_size() == HEADER_SIZE)
	for section in sections:
		out.put_data(section.data_array)
	out.put_data(pools.data_array)
	out.put_data(resources.data_array)
	out.put_data(buses.data_array)
	out.put_data(translations.data_array)
	out.put_data(assets.data_array)
	out.put_data(_strings)
	_pad4(out)
	out.put_data(data.data_array)
	assert(out.get_size() == file_size)

	var dir := out_path.get_base_dir()
	if dir != "" and not DirAccess.dir_exists_absolute(dir):
		DirAccess.make_dir_recursive_absolute(dir)
	var file := FileAccess.open(out_path, FileAccess.WRITE)
	if file == null:
		errors.append("cannot write '%s' (%s)" % [out_path, error_string(FileAccess.get_open_error())])
		return false
	file.store_buffer(out.data_array)
	file.close()
	for file_name: String in _music_files:
		var music := FileAccess.open(dir.path_join(file_name), FileAccess.WRITE)
		if music == null:
			errors.append("cannot write '%s'" % dir.path_join(file_name))
			return false
		music.store_buffer(_music_files[file_name])
		music.close()
	return true


# --- scene walk -----------------------------------------------------------------------

func _collect(node: Node) -> void:
	if not NODE_TYPES.has(node.get_class()):
		_fail(node, "unsupported node type")
	_node_index[node] = _nodes.size()
	_nodes.append(node)
	for child in node.get_children():
		_collect(child)


## The lights of the scene, for the light bake. Without any, a default light shines from
## above. Shadows are not baked.
func _find_light() -> void:
	_sun_lights = []
	_point_lights = []
	for node in _nodes:
		if not (node is Light3D) or not node.visible:
			continue
		if node.light_negative:
			_fail(node, "negative lights are not supported")
		var color: Color = node.light_color * node.light_energy
		var xform := _world_transform(node)
		var direction := -xform.basis.z.normalized()
		if node is DirectionalLight3D:
			_sun_lights.append([direction, color])
		elif node is OmniLight3D:
			_point_lights.append({"position": xform.origin, "color": color, "range": node.omni_range,
					"decay": node.omni_attenuation, "spot": false})
		elif node is SpotLight3D:
			_point_lights.append({"position": xform.origin, "color": color, "range": node.spot_range,
					"decay": node.spot_attenuation, "spot": true, "direction": direction,
					"cone": cos(deg_to_rad(node.spot_angle)), "cone_decay": node.spot_angle_attenuation})
	if _sun_lights.is_empty() and _point_lights.is_empty():
		_sun_lights.append([DEFAULT_LIGHT_DIRECTION.normalized(), Color.WHITE])
	if not _sun_lights.is_empty():
		_light_direction = _sun_lights[0][0]
		_light_color = _sun_lights[0][1]


## The light that reaches a point of a surface: every DirectionalLight3D, and the
## OmniLight3D and SpotLight3D nodes in reach, with the fall-off Godot's renderer uses.
func _light_at(position: Vector3, normal: Vector3) -> Color:
	var light := Color(0, 0, 0)
	for sun: Array in _sun_lights:
		light += sun[1] * maxf(0.0, -normal.dot(sun[0]))
	for point: Dictionary in _point_lights:
		var to_light: Vector3 = point.position - position
		var distance := maxf(to_light.length(), 0.0001)
		if distance >= point.range:
			continue
		var toward := to_light / distance
		var fade: float = distance / point.range
		fade *= fade
		fade *= fade
		fade = maxf(1.0 - fade, 0.0)
		var amount: float = fade * fade * pow(distance, -point.decay) * maxf(0.0, normal.dot(toward))
		if point.spot:
			var facing := maxf((-toward).dot(point.direction), point.cone)
			var rim := maxf(0.0001, (1.0 - facing) / (1.0 - point.cone))
			amount *= 1.0 - pow(rim, point.cone_decay)
		light += point.color * amount
	return light


## World transform inside the exported scene only, independent of where the scene hangs
## in the tree during the export. A non-3D parent restarts the chain, as in Godot.
func _world_transform(node: Node3D) -> Transform3D:
	var parent := node.get_parent() if node != _root else null
	if parent is Node3D:
		return _world_transform(parent) * node.transform
	return node.transform


## True if the node's axes are the scene's axes: scaled perhaps, but not rotated.
func _is_axis_aligned(node: Node) -> bool:
	if node is Node3D:
		var b := _world_transform(node).basis
		return (is_zero_approx(b.x.y) and is_zero_approx(b.x.z) and is_zero_approx(b.y.x)
				and is_zero_approx(b.y.z) and is_zero_approx(b.z.x) and is_zero_approx(b.z.y))
	var t := Transform2D.IDENTITY
	var at := node
	while at is Node2D:
		t = at.transform * t
		if at == _root:
			break
		at = at.get_parent()
	return is_zero_approx(t.x.y) and is_zero_approx(t.y.x)


func _node_path(node: Node) -> String:
	var path := String(node.name)
	while node != _root:
		node = node.get_parent()
		path = String(node.name) + "/" + path
	return path


func _fail(node: Node, message: String) -> void:
	var at := node
	while at != null and at != _root:
		if at.has_meta(POOL_COPY_META):
			return
		at = at.get_parent()
	errors.append("node '%s' (%s): %s" % [_node_path(node), node.get_class(), message])


# --- type-specific node data ------------------------------------------------------------

## Returns the node data record of a node (empty for types without one).
func _node_data(node: Node, type: int) -> StreamPeerBuffer:
	var out := _new_buffer()
	match type:
		NODE_MESH:
			var influences: Array = [PackedByteArray()]
			var data := _bake_mesh(node, influences)
			# Instances that bake to the same bytes share one asset.
			var asset := _asset_data.find(data)
			if asset < 0:
				asset = _add_asset(ASSET_MESH, _node_path(node), data)
			out.put_u32(asset)
			var instance := node as MeshInstance3D
			if (instance.visibility_range_begin_margin != 0.0 or instance.visibility_range_end_margin != 0.0
					or instance.visibility_range_fade_mode != GeometryInstance3D.VISIBILITY_RANGE_FADE_DISABLED):
				_fail(node, "visibility range margins and fading are not supported")
			out.put_float(instance.visibility_range_begin)
			out.put_float(instance.visibility_range_end)
			out.put_u32(_skin_asset(node, influences[0]) if node.has_meta(SKIN_META) else NONE32)
		NODE_CAMERA:
			var camera := node as Camera3D
			var orthogonal := camera.projection == Camera3D.PROJECTION_ORTHOGONAL
			if camera.projection == Camera3D.PROJECTION_FRUSTUM:
				_fail(node, "the Frustum projection is not supported; use Perspective or Orthogonal")
			if camera.keep_aspect != Camera3D.KEEP_HEIGHT:
				_fail(node, "keep_aspect must be Keep Height (the runtime uses a vertical FOV)")
			if orthogonal and camera.current:
				# What depends on the distance has no meaning without perspective.
				var world: Environment = null
				for other in _nodes:
					if other is WorldEnvironment and other.environment != null:
						world = other.environment
				if world != null and (world.fog_enabled or world.background_mode == Environment.BG_SKY):
					_fail(node, "an orthogonal camera cannot be used with fog or a sky background")
			out.put_float(camera.fov)
			out.put_float(camera.near)
			out.put_float(camera.far)
			out.put_float(camera.size if orthogonal else 0.0)
		NODE_AREA:
			# Area3D and Area2D have the same properties.
			out.put_u32(node.collision_layer)
			out.put_u32(node.collision_mask)
			out.put_u8(1 if node.monitoring else 0)
			out.put_u8(1 if node.monitorable else 0)
			out.put_u16(0)
		NODE_STATIC_BODY, NODE_CHARACTER_BODY, NODE_RIGID_BODY:
			_body_data(node, out)
		NODE_SHAPE:
			_shape_data(node, out)
		NODE_RAYCAST:
			_raycast_data(node, out)
		NODE_PATH:
			_path_data(node, out)
		NODE_PATH_FOLLOW:
			_path_follow_data(node, out)
		NODE_COLOR_RECT:
			_color_rect_data(node, out)
		NODE_PROGRESS_BAR:
			_progress_bar_data(node, out)
		NODE_SLIDER:
			_slider_data(node, out)
		NODE_NINE_PATCH:
			_nine_patch_data(node, out)
		NODE_TIMER:
			_timer_data(node, out)
		NODE_ENVIRONMENT:
			_environment_data(node, out)
		NODE_TILEMAP:
			_tilemap_data(node, out)
		NODE_ANIMATED_SPRITE:
			_animated_sprite_data(node, out)
		NODE_PANEL:
			_panel_data(node, out)
		NODE_TEXTURE_PROGRESS:
			_texture_progress_data(node, out)
		NODE_SCROLL:
			_scroll_data(node, out)
		NODE_RICH_TEXT:
			_rich_text_data(node, out)
		NODE_PARALLAX:
			_parallax_data(node, out)
		NODE_CAMERA2D:
			_camera_2d_data(node, out)
		NODE_SPRITE:
			_sprite_data(node, out)
		NODE_CONTROL:
			_put_rect(out, node)
		NODE_LABEL:
			_label_data(node, out)
		NODE_BUTTON:
			_button_data(node, out)
		NODE_TEXTURE_RECT:
			var rect := node as TextureRect
			if rect.stretch_mode != TextureRect.STRETCH_SCALE or rect.flip_h or rect.flip_v:
				_fail(node, "only stretch mode Scale without flipping is supported")
			_put_rect(out, node)
			out.put_u32(_texture_asset(node, rect.texture, _canvas_filter(rect)))
			out.put_u32(_rgba(rect.modulate * rect.self_modulate))
		NODE_ANIMATION_PLAYER:
			out.put_u32(_animations_asset(node))
			out.put_float(node.speed_scale)
			out.put_float(node.playback_default_blend_time)
		NODE_PARTICLES:
			if node is CPUParticles2D:
				_particles_2d_data(node, out)
			else:
				_particles_data(node, out)
		NODE_SPRITE3D:
			_sprite_3d_data(node, out)
		NODE_LABEL3D:
			_label_3d_data(node, out)
		NODE_AUDIO, NODE_AUDIO_SPATIAL:
			_audio_data(node, out)
	return out


## StaticBody, CharacterBody and RigidBody, 2D and 3D. The runtime moves character bodies
## against static ones (rr_body_move_and_slide) and lets rigid bodies fall and bounce; it
## is not Godot's physics engine.
func _body_data(node: Node, out: StreamPeerBuffer) -> void:
	var is_3d := node is Node3D
	var rigid := node is RigidBody3D or node is RigidBody2D
	var material: PhysicsMaterial = null
	if node is CharacterBody3D or node is CharacterBody2D:
		var default_up := Vector3.UP if is_3d else Vector3(0, -1, 0)
		var up := Vector3(node.up_direction.x, node.up_direction.y, node.up_direction.z if is_3d else 0.0)
		if not up.is_equal_approx(default_up):
			_fail(node, "up_direction must stay at its default; the runtime's floor is always below the body")
		# The enum value is the same in 2D and 3D.
		if node.motion_mode != CharacterBody3D.MOTION_MODE_GROUNDED:
			_fail(node, "only motion mode Grounded is supported")
	elif rigid:
		material = node.physics_material_override
		# platforms.gd freezes rigid bodies for the export and notes what the scene had.
		if node.get_meta(&"retro_frozen", node.freeze) or node.custom_integrator:
			_fail(node, "freeze and a custom integrator are not supported")
		if not is_3d and node.contact_monitor:
			_fail(node, "contact monitoring is not supported for 2D bodies")
		# The enum values are the same in 2D and 3D.
		if node.linear_damp_mode != RigidBody3D.DAMP_MODE_COMBINE:
			_fail(node, "linear damp mode must be Combine")
		if is_3d:
			_check_rigid_3d(node)
	else:
		material = node.physics_material_override
		var moving := false
		if is_3d:
			moving = node.constant_linear_velocity != Vector3.ZERO or node.constant_angular_velocity != Vector3.ZERO
		else:
			moving = node.constant_linear_velocity != Vector2.ZERO or node.constant_angular_velocity != 0.0
		if moving:
			_fail(node, "constant velocities are not supported")
	if material != null and (material.rough or material.absorbent):
		_fail(node, "rough and absorbent physics materials are not supported")

	out.put_u32(node.collision_layer)
	out.put_u32(node.collision_mask)
	out.put_u8(1 if not is_3d else 0)
	out.put_u8(0)
	out.put_u16(0)
	out.put_float(material.bounce if material != null else 0.0)
	out.put_float(material.friction if material != null else 1.0)

	var gravity := Vector3.ZERO
	var mass := 1.0
	var damp := 0.0
	var velocity := Vector3.ZERO
	if rigid:
		var space := "physics/3d/" if is_3d else "physics/2d/"
		var direction = ProjectSettings.get_setting(space + "default_gravity_vector")
		var strength: float = ProjectSettings.get_setting(space + "default_gravity") * node.gravity_scale
		gravity = Vector3(direction.x, direction.y, direction.z if is_3d else 0.0) * strength
		mass = node.mass
		damp = ProjectSettings.get_setting(space + "default_linear_damp") + node.linear_damp
		velocity = Vector3(node.linear_velocity.x, node.linear_velocity.y, node.linear_velocity.z if is_3d else 0.0)
	for vector: Vector3 in [gravity]:
		out.put_float(vector.x)
		out.put_float(vector.y)
		out.put_float(vector.z)
	out.put_float(mass)
	out.put_float(damp)
	out.put_float(velocity.x)
	out.put_float(velocity.y)
	out.put_float(velocity.z)

	# What only a RigidBody3D has: it turns.
	var angular_damp := 0.0
	var angular_velocity := Vector3.ZERO
	var lock := 0
	var contacts := 0
	if node is RigidBody3D:
		angular_damp = ProjectSettings.get_setting("physics/3d/default_angular_damp") + node.angular_damp
		angular_velocity = node.angular_velocity
		if node.lock_rotation or node.axis_lock_angular_x:
			lock |= 1
		if node.lock_rotation or node.axis_lock_angular_y:
			lock |= 2
		if node.lock_rotation or node.axis_lock_angular_z:
			lock |= 4
		if node.contact_monitor:
			contacts = clampi(node.max_contacts_reported, 0, 8)
	out.put_float(angular_damp)
	out.put_float(angular_velocity.x)
	out.put_float(angular_velocity.y)
	out.put_float(angular_velocity.z)
	out.put_u8(lock)
	out.put_u8(contacts)
	out.put_u16(0)


## What the runtime's 3D rigid bodies (picophysics) cannot do, said at export time.
func _check_rigid_3d(node: RigidBody3D) -> void:
	if node.angular_damp_mode != RigidBody3D.DAMP_MODE_COMBINE:
		_fail(node, "angular damp mode must be Combine")
	if node.axis_lock_linear_x or node.axis_lock_linear_y or node.axis_lock_linear_z:
		_fail(node, "locking the movement along an axis is not supported (only the rotation)")
	if node.center_of_mass_mode != RigidBody3D.CENTER_OF_MASS_MODE_AUTO or node.inertia != Vector3.ZERO:
		_fail(node, "a custom centre of mass or inertia is not supported")
	if node.constant_force != Vector3.ZERO or node.constant_torque != Vector3.ZERO:
		_fail(node, "constant forces are not supported")
	if node.contact_monitor and node.max_contacts_reported > 8:
		_fail(node, "at most 8 contacts can be reported (max_contacts_reported)")
	if not node.scale.is_equal_approx(Vector3.ONE):
		_fail(node, "a rigid body must not be scaled (scale its shapes and meshes)")
	var shapes := 0
	for child in node.get_children():
		if not (child is CollisionShape3D) or child.disabled:
			continue
		shapes += 1
		if not (child.shape is BoxShape3D or child.shape is SphereShape3D):
			_fail(child, "a RigidBody3D takes box and sphere shapes only")
		if not child.transform.basis.is_equal_approx(Basis.IDENTITY):
			_fail(child, "the shape of a RigidBody3D must not be rotated or scaled (move it, or turn the body)")
	if shapes == 0:
		_fail(node, "a RigidBody3D needs a CollisionShape3D with a box or a sphere")
	elif shapes > 8:
		_fail(node, "a RigidBody3D takes at most 8 shapes")


## CollisionShape3D and CollisionShape2D. A 2D shape becomes a box that is half a unit
## thick, a sphere or a capsule, which gives the same overlaps as long as everything stays
## at Z = 0.
func _shape_data(node: Node, out: StreamPeerBuffer) -> void:
	var owner_type: int = NODE_TYPES.get(node.get_parent().get_class(), NODE_NODE) if node != _root else NODE_NODE
	var moving := owner_type == NODE_CHARACTER_BODY or owner_type == NODE_RIGID_BODY
	if owner_type != NODE_AREA and owner_type != NODE_STATIC_BODY and not moving:
		_fail(node, "must be a direct child of an area or a body")
	if (node is Node3D) != (node.get_parent() is Node3D):
		_fail(node, "a 2D shape needs a 2D parent and a 3D shape a 3D parent")
	var shape: Resource = node.shape
	var size := Vector3.ZERO
	var kind := SHAPE_BOX
	var map := NONE32
	if shape is HeightMapShape3D:
		# Ground: the heights go into an asset, the shape only names it.
		kind = SHAPE_HEIGHT_MAP
		if owner_type != NODE_STATIC_BODY:
			_fail(node, "a HeightMapShape3D must belong to a StaticBody3D")
		var scale := _world_transform(node).basis.get_scale()
		if not _is_axis_aligned(node) or _world_transform(node).basis.determinant() <= 0.0 \
				or scale.x <= 0.0 or scale.y <= 0.0 or scale.z <= 0.0:
			_fail(node, "a HeightMapShape3D must not be rotated or mirrored, and neither may its parents")
		if shape.map_width < 2 or shape.map_depth < 2 or shape.map_width > 4096 or shape.map_depth > 4096:
			_fail(node, "the height map must be 2 to 4096 points wide and deep")
		elif _asset_cache.has(shape):
			map = _asset_cache[shape]
		else:
			var heights: PackedFloat32Array = shape.map_data
			var lowest := heights[0]
			var highest := heights[0]
			for h in heights:
				lowest = minf(lowest, h)
				highest = maxf(highest, h)
			var data := _new_buffer()
			data.put_u16(shape.map_width)
			data.put_u16(shape.map_depth)
			data.put_float(lowest)
			data.put_float(highest)
			for h in heights:
				data.put_float(h)
			map = _add_asset(ASSET_HEIGHT_MAP, _resource_name(shape, _node_path(node)), data.data_array)
			_asset_cache[shape] = map
	elif shape is BoxShape3D:
		size = shape.size * 0.5
	elif shape is SphereShape3D:
		kind = SHAPE_SPHERE
		size.x = shape.radius
	elif shape is RectangleShape2D:
		size = Vector3(shape.size.x * 0.5, shape.size.y * 0.5, 0.5)
	elif shape is CircleShape2D:
		kind = SHAPE_SPHERE
		size.x = shape.radius
	elif (shape is CapsuleShape3D or shape is CapsuleShape2D) and moving:
		# A line along Y with a radius around it; the height includes both round ends.
		kind = SHAPE_CAPSULE
		size.x = shape.radius
		size.y = maxf(shape.height * 0.5 - shape.radius, 0.0)
	elif moving:
		_fail(node, "shape must be a box, a sphere or a capsule (a rectangle, a circle or a capsule in 2D)")
	elif node is Node3D:
		_fail(node, "shape must be a BoxShape3D or a SphereShape3D (or a HeightMapShape3D below a StaticBody3D)")
	else:
		_fail(node, "shape must be a RectangleShape2D or a CircleShape2D")
	var one_way: bool = node is CollisionShape2D and node.one_way_collision
	if one_way and (owner_type != NODE_STATIC_BODY or kind != SHAPE_BOX or not _is_axis_aligned(node)):
		_fail(node, "one-way collision needs a RectangleShape2D below a StaticBody2D that is not rotated")
	if moving and not _is_axis_aligned(node):
		# What a body runs into may be rotated; the body itself may not.
		_fail(node, "the shape of a CharacterBody or RigidBody must not be rotated, and neither may its parents")
	out.put_u32(kind)
	out.put_float(size.x)
	out.put_float(size.y)
	out.put_float(size.z)
	out.put_u8(1 if node.disabled else 0)
	out.put_u8(1 if one_way else 0)
	out.put_u16(0)
	out.put_u32(map)


func _sprite_data(node: Sprite2D, out: StreamPeerBuffer) -> void:
	var size := node.texture.get_size() if node.texture != null else Vector2.ZERO
	# The sprite sheet: the region if there is one, else the whole texture. The runtime
	# cuts it into hframes x vframes cells and shows the one `frame` selects.
	var sheet := Rect2(Vector2.ZERO, size)
	if node.region_enabled:
		sheet = node.region_rect
		if not sheet.has_area() or not Rect2(Vector2.ZERO, size).encloses(sheet):
			_fail(node, "the region must have a size and lie inside the texture")
	out.put_u32(_texture_asset(node, node.texture, _canvas_filter(node)))
	out.put_u32(_rgba(node.modulate * node.self_modulate))
	out.put_float(node.offset.x)
	out.put_float(node.offset.y)
	out.put_float(sheet.position.x)
	out.put_float(sheet.position.y)
	out.put_float(sheet.size.x)
	out.put_float(sheet.size.y)
	out.put_u16(node.hframes)
	out.put_u16(node.vframes)
	out.put_u16(node.frame)
	out.put_u8(1 if node.centered else 0)
	out.put_u8(1 if node.flip_h else 0)
	out.put_u8(1 if node.flip_v else 0)
	out.put_u8(0)
	out.put_u16(0)


func _animated_sprite_data(node: AnimatedSprite2D, out: StreamPeerBuffer) -> void:
	var frames := node.sprite_frames
	if frames == null:
		_fail(node, "has no SpriteFrames")
		return
	var names: Array[String] = []
	for animation_name in frames.get_animation_names():
		names.append(String(animation_name))
	names.sort()
	# With autoplay the node already plays while the scene is in the tree for the export,
	# so its current animation and frame say nothing; the autoplay name does.
	var start := String(node.autoplay) if node.autoplay != "" else String(node.animation)
	if node.autoplay == "" and node.frame != 0:
		_fail(node, "a start frame other than 0 is not supported")
	if not names.has(start):
		_fail(node, "its animation '%s' is not in the SpriteFrames" % start)
		return
	out.put_u32(_sprite_frames_asset(node, frames, names))
	out.put_u32(_rgba(node.modulate * node.self_modulate))
	out.put_float(node.offset.x)
	out.put_float(node.offset.y)
	out.put_float(node.speed_scale)
	out.put_u16(names.find(start))
	out.put_u8(1 if node.centered else 0)
	out.put_u8(1 if node.flip_h else 0)
	out.put_u8(1 if node.flip_v else 0)
	out.put_u8(1 if node.autoplay != "" else 0)
	out.put_u16(0)


## Writes a SpriteFrames resource as an asset: the animations sorted by name, each with
## its frames. A frame is a whole texture or a region of one (AtlasTexture).
func _sprite_frames_asset(node: AnimatedSprite2D, frames: SpriteFrames, names: Array[String]) -> int:
	var filter := _canvas_filter(node)
	var key := "frames:%d:%d" % [frames.get_instance_id(), filter]
	if _asset_cache.has(key):
		return _asset_cache[key]
	var table := _new_buffer()
	var body := _new_buffer()
	var body_start := 4 + names.size() * 20
	table.put_u32(names.size())
	for animation_name in names:
		var count := frames.get_frame_count(animation_name)
		if count == 0:
			_fail(node, "animation '%s' has no frames" % animation_name)
		table.put_u32(_string(animation_name))
		table.put_float(frames.get_animation_speed(animation_name))
		table.put_u8(1 if frames.get_animation_loop(animation_name) else 0)
		table.put_u8(0)
		table.put_u16(0)
		table.put_u32(count)
		table.put_u32(body_start + body.get_size())
		for i in count:
			var texture := frames.get_frame_texture(animation_name, i)
			var region := Rect2()
			if texture is AtlasTexture:
				if texture.margin != Rect2() or texture.atlas == null or texture.atlas is AtlasTexture:
					_fail(node, "frame %d of '%s': atlas margins and nested atlases are not supported"
							% [i, animation_name])
					continue
				region = texture.region
				texture = texture.atlas
			elif texture != null:
				region = Rect2(Vector2.ZERO, texture.get_size())
			body.put_u32(_texture_asset(node, texture, filter))
			body.put_float(region.position.x)
			body.put_float(region.position.y)
			body.put_float(region.size.x)
			body.put_float(region.size.y)
			body.put_float(frames.get_frame_duration(animation_name, i))
	table.put_data(body.data_array)
	var asset := _add_asset(ASSET_SPRITE_FRAMES, _resource_name(frames, _node_path(node) + " frames"), table.data_array)
	_asset_cache[key] = asset
	return asset


## The level a 2D node or control is drawn at: x is its z_index together with that of the
## parents it is relative to, y is 1 inside a CanvasLayer. Other nodes are at (0, 0).
func _draw_level(node: Node) -> Vector2i:
	if not (node is CanvasItem):
		return Vector2i.ZERO
	if node.y_sort_enabled:
		_fail(node, "y_sort_enabled is not supported")
	var z := 0
	var in_layer := 0
	var relative := true
	var at := node
	while at != null:
		if at is CanvasLayer:
			in_layer = 1
			break
		if at is CanvasItem and relative:
			z += at.z_index
			relative = at.z_as_relative
		at = at.get_parent() if at != _root else null
	return Vector2i(clampi(z, RenderingServer.CANVAS_ITEM_Z_MIN, RenderingServer.CANVAS_ITEM_Z_MAX), in_layer)


func _parallax_data(node: Parallax2D, out: StreamPeerBuffer) -> void:
	var above := node.get_parent() if node != _root else null
	while above != null:
		if above is Parallax2D:
			_fail(node, "a Parallax2D inside another one is not supported")
		above = above.get_parent() if above != _root else null
	if node.repeat_times < 0 or node.repeat_times > 16:
		_fail(node, "repeat_times must be between 0 and 16")
	# platforms.gd stops the node from scrolling while the scene is in the tree and notes
	# what the scene file says: scroll_offset, autoscroll, screen_offset.
	var kept: Array = node.get_meta(&"retro_parallax", [node.scroll_offset, node.autoscroll, node.screen_offset])
	for value: Vector2 in [node.scroll_scale, kept[0], node.repeat_size, kept[1],
			node.limit_begin, node.limit_end, kept[2]]:
		out.put_float(value.x)
		out.put_float(value.y)
	out.put_u16(node.repeat_times)
	out.put_u8(1 if node.follow_viewport else 0)
	out.put_u8(1 if node.ignore_camera_scroll else 0)


func _camera_2d_data(node: Camera2D, out: StreamPeerBuffer) -> void:
	if not node.ignore_rotation:
		_fail(node, "a camera that rotates the view is not supported; keep ignore_rotation on")
	if node.zoom.x <= 0.0 or node.zoom.y <= 0.0:
		_fail(node, "zoom must be positive")
	# Older 4.x versions have no switch for the limits; they are always on there.
	var limited: bool = node.get("limit_enabled") if "limit_enabled" in node else true
	var far := 10000000.0
	out.put_float(node.limit_left if limited else -far)
	out.put_float(node.limit_top if limited else -far)
	out.put_float(node.limit_right if limited else far)
	out.put_float(node.limit_bottom if limited else far)
	out.put_float(node.offset.x)
	out.put_float(node.offset.y)
	out.put_float(node.zoom.x)
	out.put_float(node.zoom.y)
	out.put_u8(1 if node.enabled else 0)
	out.put_u8(1 if node.anchor_mode == Camera2D.ANCHOR_MODE_FIXED_TOP_LEFT else 0)
	out.put_u8(1 if node.position_smoothing_enabled else 0)
	out.put_u8(1 if node.limit_smoothed else 0)
	out.put_float(node.drag_left_margin)
	out.put_float(node.drag_top_margin)
	out.put_float(node.drag_right_margin)
	out.put_float(node.drag_bottom_margin)
	out.put_float(node.drag_horizontal_offset)
	out.put_float(node.drag_vertical_offset)
	out.put_float(node.position_smoothing_speed)
	out.put_u8(1 if node.drag_horizontal_enabled else 0)
	out.put_u8(1 if node.drag_vertical_enabled else 0)
	out.put_u16(0)


## What a Camera2D means for the rest of the scene. In Godot the camera moves everything
## on the default canvas, controls included, and nothing inside a CanvasLayer. The runtime
## moves 2D nodes and never controls, so a scene has to be built the way that gives the
## same picture in both: the HUD in a CanvasLayer, the 2D world outside of one.
func _check_canvas() -> void:
	var camera: Camera2D = null
	for node in _nodes:
		if node is CanvasLayer:
			if node.layer != 1 or node.offset != Vector2.ZERO or node.rotation != 0.0 \
					or node.scale != Vector2.ONE or node.follow_viewport_enabled:
				_fail(node, "only a plain CanvasLayer is supported (layer 1, no offset, rotation, "
						+ "scale or follow viewport)")
		if node is Camera2D and node.enabled:
			if camera != null:
				_fail(node, "a second enabled Camera2D; the runtime uses one ('%s')" % _node_path(camera))
			else:
				camera = node
	if camera == null:
		return
	for node in _nodes:
		if not (node is CanvasItem):
			continue
		var in_layer := false
		var at := node
		while at != _root:
			at = at.get_parent()
			if at is CanvasLayer:
				in_layer = true
				break
		if node is Control and not in_layer:
			_fail(node, "would scroll with the Camera2D in Godot but stays fixed in the runtime; "
					+ "put the HUD into a CanvasLayer")
		elif node is Node2D and in_layer:
			_fail(node, "2D nodes inside a CanvasLayer are not supported while a Camera2D is used")


## RayCast3D and RayCast2D.
func _raycast_data(node: Node, out: StreamPeerBuffer) -> void:
	var casts_shape := node is ShapeCast3D or node is ShapeCast2D
	if not casts_shape and node.hit_from_inside:
		_fail(node, "hit_from_inside is not supported")
	# The shape a ShapeCast moves along its line, as _shape_data writes the shape of a body.
	var kind := 0
	var size := Vector3.ZERO
	if casts_shape:
		var shape: Resource = node.shape
		if node.margin != 0.0:
			_fail(node, "a margin is not supported")
		if shape is BoxShape3D:
			kind = SHAPE_BOX
			size = shape.size * 0.5
		elif shape is RectangleShape2D:
			kind = SHAPE_BOX
			size = Vector3(shape.size.x * 0.5, shape.size.y * 0.5, 0.5)
		elif shape is SphereShape3D or shape is CircleShape2D:
			kind = SHAPE_SPHERE
			size.x = shape.radius
		elif shape is CapsuleShape3D or shape is CapsuleShape2D:
			kind = SHAPE_CAPSULE
			size.x = shape.radius
			size.y = maxf(shape.height * 0.5 - shape.radius, 0.0)
		else:
			_fail(node, "shape must be a box, a sphere or a capsule (a rectangle, a circle or a capsule in 2D)")
	var target := Vector3.ZERO
	if node is RayCast3D or node is ShapeCast3D:
		target = node.target_position
	else:
		target = Vector3(node.target_position.x, node.target_position.y, 0)
	out.put_float(target.x)
	out.put_float(target.y)
	out.put_float(target.z)
	out.put_u32(node.collision_mask)
	out.put_u8(1 if node.enabled else 0)
	out.put_u8(1 if node.collide_with_areas else 0)
	out.put_u8(1 if node.collide_with_bodies else 0)
	out.put_u8(1 if node.exclude_parent else 0)
	out.put_u32(kind)
	out.put_float(size.x)
	out.put_float(size.y)
	out.put_float(size.z)


## Most points one curve is sampled at. A longer curve needs a larger bake interval.
const MAX_CURVE_POINTS := 8192

## Path3D and Path2D: the curve, sampled at equal distances of about its bake interval.
func _path_data(node: Node, out: StreamPeerBuffer) -> void:
	var curve: Resource = node.curve
	if curve == null or curve.point_count < 2 or curve.get_baked_length() <= 0.0:
		_fail(node, "needs a curve with at least two points")
		out.put_u32(0)
		return
	if _asset_cache.has(curve):
		out.put_u32(_asset_cache[curve])
		return
	var length: float = curve.get_baked_length()
	var segments := maxi(1, ceili(length / curve.bake_interval))
	if segments + 1 > MAX_CURVE_POINTS:
		_fail(node, "the curve would need %d points (limit %d); raise its bake interval"
				% [segments + 1, MAX_CURVE_POINTS])
		segments = 1
	var spacing := length / segments
	var data := _new_buffer()
	data.put_u32(segments + 1)
	data.put_float(spacing)
	data.put_float(length)
	for i in segments + 1:
		var offset := minf(i * spacing, length)
		var pos := Vector3.ZERO
		var up := Vector3.UP
		if curve is Curve3D:
			pos = curve.sample_baked(offset, false)
			if curve.up_vector_enabled:
				up = curve.sample_baked_up_vector(offset, true)
		else:
			var p: Vector2 = curve.sample_baked(offset, false)
			pos = Vector3(p.x, p.y, 0)
		for v: Vector3 in [pos, up]:
			data.put_float(v.x)
			data.put_float(v.y)
			data.put_float(v.z)
	var asset := _add_asset(ASSET_CURVE, _resource_name(curve, _node_path(node)), data.data_array)
	_asset_cache[curve] = asset
	out.put_u32(asset)


## PathFollow3D and PathFollow2D.
func _path_follow_data(node: Node, out: StreamPeerBuffer) -> void:
	var rotation := PATH_ROTATION_NONE
	if node is PathFollow3D:
		if not (node.get_parent() is Path3D):
			_fail(node, "must be a child of a Path3D")
		if node.use_model_front:
			_fail(node, "use_model_front is not supported")
		match node.rotation_mode:
			PathFollow3D.ROTATION_NONE:
				rotation = PATH_ROTATION_NONE
			PathFollow3D.ROTATION_Y:
				rotation = PATH_ROTATION_Y
			PathFollow3D.ROTATION_XY, PathFollow3D.ROTATION_XYZ:
				rotation = PATH_ROTATION_FORWARD
			PathFollow3D.ROTATION_ORIENTED:
				rotation = PATH_ROTATION_ORIENTED
			_:
				_fail(node, "rotation mode X is not supported")
	else:
		if not (node.get_parent() is Path2D):
			_fail(node, "must be a child of a Path2D")
		rotation = PATH_ROTATION_FORWARD if node.rotates else PATH_ROTATION_NONE
	out.put_float(node.progress)
	out.put_float(node.h_offset)
	out.put_float(node.v_offset)
	out.put_u8(rotation)
	out.put_u8(1 if node.loop else 0)
	out.put_u8(1 if node is PathFollow2D else 0)
	out.put_u8(0)


func _color_rect_data(node: ColorRect, out: StreamPeerBuffer) -> void:
	_put_rect(out, node)
	out.put_u32(_rgba(node.color * node.modulate * node.self_modulate))


func _progress_bar_data(node: ProgressBar, out: StreamPeerBuffer) -> void:
	if node.indeterminate:
		_fail(node, "indeterminate progress bars are not supported")
	_put_rect(out, node)
	out.put_float(node.min_value)
	out.put_float(node.max_value)
	out.put_float(node.value)
	out.put_u32(_stylebox_color(node, "background"))
	out.put_u32(_stylebox_color(node, "fill"))
	if node.show_percentage:
		out.put_u32(_font_asset(node, node.get_theme_font("font"), node.get_theme_font_size("font_size")))
	else:
		out.put_u32(NONE32)
	out.put_u32(_rgba(node.get_theme_color("font_color")))
	out.put_u8(node.fill_mode)
	out.put_u8(1 if node.show_percentage else 0)
	out.put_u16(0)


func _nine_patch_data(node: NinePatchRect, out: StreamPeerBuffer) -> void:
	if (node.axis_stretch_horizontal != NinePatchRect.AXIS_STRETCH_MODE_STRETCH
			or node.axis_stretch_vertical != NinePatchRect.AXIS_STRETCH_MODE_STRETCH):
		_fail(node, "only axis stretch mode Stretch is supported")
	var size := node.texture.get_size() if node.texture != null else Vector2.ZERO
	var region := node.region_rect if node.region_rect.has_area() else Rect2(Vector2.ZERO, size)
	_put_rect(out, node)
	out.put_u32(_texture_asset(node, node.texture, _canvas_filter(node)))
	out.put_u32(_rgba(node.modulate * node.self_modulate))
	out.put_float(region.position.x)
	out.put_float(region.position.y)
	out.put_float(region.size.x)
	out.put_float(region.size.y)
	for side: Side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		out.put_float(node.get_patch_margin(side))
	out.put_u8(1 if node.draw_center else 0)
	out.put_u8(0)
	out.put_u16(0)


func _timer_data(node: Timer, out: StreamPeerBuffer) -> void:
	if node.wait_time <= 0.0:
		_fail(node, "wait_time must be above 0")
	out.put_float(node.wait_time)
	out.put_u8(1 if node.one_shot else 0)
	# platforms.gd switches autostart off while the scene is in the tree and notes it here.
	out.put_u8(1 if node.get_meta(&"retro_autoplay", node.autostart) else 0)
	out.put_u16(0)


## Reads the WorldEnvironment of the scene: the background colour for the header and the
## ambient light for the light bake. Without one, the background is the default clear
## colour of the project and the ambient light the default of the runtime.
## The six faces of the sky cube: middle, right and up of each picture. The same table is
## g_sky in runtime/core/rr_render.c.
const SKY_FACES: Array = [
	[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)],
	[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0)],
	[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)],
	[Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1)],
	[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)],
	[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0)],
]
const SKY_FACE_NAMES: Array[String] = ["+x", "-x", "+y", "-y", "+z", "-z"]

## Side of a sky face for a sky without a picture: a gradient needs few pixels.
const PROCEDURAL_SKY_SIZE := 64


## Turns the sky of an environment into the six pictures of a cube (`_sky_faces`). A
## PanoramaSkyMaterial is cut up; a ProceduralSkyMaterial is worked out with the formula
## of Godot's sky shader for its colours and curves, without the sun disc.
func _bake_sky(node: WorldEnvironment, env: Environment) -> void:
	_sky_faces = []
	if env.sky == null or env.sky.sky_material == null:
		_fail(node, "the background is Sky, but the environment has no sky material")
		return
	if env.sky_rotation != Vector3.ZERO:
		_fail(node, "a rotated sky is not supported")
	var material := env.sky.sky_material
	var energy: float = env.background_energy_multiplier
	var panorama: Image = null
	var size := PROCEDURAL_SKY_SIZE
	if material is PanoramaSkyMaterial:
		var texture: Texture2D = material.panorama
		if texture == null:
			_fail(node, "the PanoramaSkyMaterial has no panorama")
			return
		panorama = texture.get_image()
		if panorama == null or panorama.is_empty():
			_fail(node, "cannot read the pixels of the panorama")
			return
		if panorama.is_compressed():
			panorama.decompress()
		panorama.convert(Image.FORMAT_RGBAF)
		energy *= material.energy_multiplier
		# A face covers a quarter of the panorama's width.
		size = _baker.sky_face_size()
		while size > 16 and size * 4 > panorama.get_width():
			size /= 2
		panorama.resize(size * 4, size * 2, Image.INTERPOLATE_LANCZOS)
	elif not (material is ProceduralSkyMaterial):
		_fail(node, "sky material %s is not supported (PanoramaSkyMaterial or ProceduralSkyMaterial)"
				% material.get_class())
		return

	for face: Array in SKY_FACES:
		var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
		for y in size:
			for x in size:
				var u := (x + 0.5) / size * 2.0 - 1.0
				var v := 1.0 - (y + 0.5) / size * 2.0
				var direction: Vector3 = (face[0] + face[1] * u + face[2] * v).normalized()
				var color := Color.BLACK
				if panorama != null:
					# Godot's panorama: the middle of the picture is -Z, the top is +Y.
					var px := (atan2(direction.x, -direction.z) / TAU + 0.5) * panorama.get_width()
					var py := acos(clampf(direction.y, -1.0, 1.0)) / PI * panorama.get_height()
					color = panorama.get_pixel(clampi(int(px), 0, panorama.get_width() - 1),
							clampi(int(py), 0, panorama.get_height() - 1))
				else:
					color = _procedural_sky(material, direction)
				image.set_pixel(x, y, Color(color.r * energy, color.g * energy, color.b * energy, 1.0))
		_sky_faces.append(image)


## Colour of a ProceduralSkyMaterial in one direction: horizon to top above, horizon to
## bottom below, each along its curve.
func _procedural_sky(material: ProceduralSkyMaterial, direction: Vector3) -> Color:
	var angle := acos(clampf(direction.y, -1.0, 1.0))
	if direction.y >= 0.0:
		var c := 1.0 - angle / (PI * 0.5)
		var amount := clampf(1.0 - pow(1.0 - c, 1.0 / maxf(material.sky_curve, 0.0001)), 0.0, 1.0)
		return material.sky_horizon_color.lerp(material.sky_top_color, amount) * material.sky_energy_multiplier
	var d := (angle - PI * 0.5) / (PI * 0.5)
	var below := clampf(1.0 - pow(1.0 - d, 1.0 / maxf(material.ground_curve, 0.0001)), 0.0, 1.0)
	return material.ground_horizon_color.lerp(material.ground_bottom_color, below) * material.ground_energy_multiplier


## Average colour of the sky above the horizon: what it lights the scene with.
func _sky_light() -> Color:
	var sum := Color(0, 0, 0)
	var count := 0
	for index in [0, 1, 2, 4, 5]:
		var image: Image = _sky_faces[index]
		# The upper half of the sides, all of the top.
		var rows := image.get_height() if index == 2 else image.get_height() / 2
		for y in range(0, rows, 4):
			for x in range(0, image.get_width(), 4):
				sum += image.get_pixel(x, y)
				count += 1
	return Color(sum.r / count, sum.g / count, sum.b / count) if count > 0 else Color.BLACK


func _find_environment() -> void:
	_sky_faces = []
	_clear_color = ProjectSettings.get_setting("rendering/environment/defaults/default_clear_color", Color(0.3, 0.3, 0.3))
	_ambient = Color(DEFAULT_AMBIENT, DEFAULT_AMBIENT, DEFAULT_AMBIENT)
	var found: WorldEnvironment = null
	for node in _nodes:
		if not (node is WorldEnvironment):
			continue
		if found != null:
			_fail(node, "a second WorldEnvironment; a scene can have one ('%s')" % _node_path(found))
			continue
		found = node
		var env: Environment = node.environment
		if env == null:
			_fail(node, "has no Environment")
			continue
		if node.camera_attributes != null or node.compositor != null:
			_fail(node, "camera attributes and compositors are not supported")
		match env.background_mode:
			Environment.BG_CLEAR_COLOR:
				pass
			Environment.BG_COLOR:
				_clear_color = env.background_color
			Environment.BG_SKY:
				_bake_sky(node, env)
				if _sky_faces.size() == 6:
					# What shows where there is no sky to see, and what lights the scene.
					_clear_color = _sky_light()
			_:
				_fail(node, "the background must be Clear Color, Custom Color or Sky (no canvas or camera feed)")
		match env.ambient_light_source:
			Environment.AMBIENT_SOURCE_BG:
				_ambient = _clear_color * env.ambient_light_energy
			Environment.AMBIENT_SOURCE_COLOR:
				_ambient = env.ambient_light_color * env.ambient_light_energy
			Environment.AMBIENT_SOURCE_DISABLED:
				_ambient = Color.BLACK
			_:
				if _sky_faces.size() == 6:
					_ambient = _sky_light() * env.ambient_light_energy
				else:
					_fail(node, "ambient light from a sky needs a sky as background")
		if env.tonemap_mode != Environment.TONE_MAPPER_LINEAR:
			_fail(node, "only the Linear tonemapper is supported")
		if env.glow_enabled or env.ssr_enabled or env.ssao_enabled or env.ssil_enabled \
				or env.sdfgi_enabled or env.volumetric_fog_enabled or env.adjustment_enabled:
			_fail(node, "glow, SSR, SSAO, SSIL, SDFGI, volumetric fog and adjustments are not supported")


func _environment_data(node: WorldEnvironment, out: StreamPeerBuffer) -> void:
	var env: Environment = node.environment
	var fog := env != null and env.fog_enabled
	var color := Color.BLACK
	var density := 0.0
	var begin := 0.0
	var end := 0.0
	var mode := 0
	if fog:
		if env.fog_sun_scatter != 0.0 or env.fog_height_density != 0.0 or env.fog_aerial_perspective != 0.0:
			_fail(node, "fog sun scatter, height fog and aerial perspective are not supported")
		color = env.fog_light_color * env.fog_light_energy
		color.a = 1.0
		mode = 1 if env.fog_mode == Environment.FOG_MODE_DEPTH else 0
		if mode == 1:
			# The consoles blend linearly between begin and end.
			if not is_equal_approx(env.fog_depth_curve, 1.0):
				_fail(node, "fog depth curve must be 1 (linear)")
			if env.fog_depth_end <= env.fog_depth_begin:
				_fail(node, "fog depth end must be beyond its begin")
			# In depth mode Godot uses the density as the strongest the fog gets.
			if not is_equal_approx(env.fog_density, 1.0):
				_fail(node, "in fog mode Depth the density must be 1 (fog that ends fully opaque)")
			begin = env.fog_depth_begin
			end = env.fog_depth_end
		else:
			density = env.fog_density
	out.put_u32(_rgba(color))
	out.put_float(density)
	out.put_float(begin)
	out.put_float(end)
	out.put_u8(1 if fog else 0)
	out.put_u8(mode)
	out.put_u16(0)
	for index in 6:
		if _sky_faces.size() == 6:
			out.put_u32(_bake_image(node, _sky_faces[index], "sky " + SKY_FACE_NAMES[index], FILTER_LINEAR))
		else:
			out.put_u32(NONE32)


## TileMapLayer: the used cells with the place of their picture in the atlas, and the
## rectangles that are solid. One atlas per layer, square tiles of the size of the grid.
## Joins rectangles that are neighbours in a row into one: fewer tests at runtime.
func _merge_rows(rects: Array[Rect2]) -> Array[Rect2]:
	rects.sort_custom(func(a: Rect2, b: Rect2) -> bool:
		if a.position.y != b.position.y:
			return a.position.y < b.position.y
		if a.size.y != b.size.y:
			return a.size.y < b.size.y
		return a.position.x < b.position.x)
	var merged: Array[Rect2] = []
	for rect in rects:
		if not merged.is_empty():
			var last: Rect2 = merged[-1]
			if is_equal_approx(last.position.y, rect.position.y) and is_equal_approx(last.size.y, rect.size.y) \
					and is_equal_approx(last.end.x, rect.position.x):
				last.size.x += rect.size.x
				merged[-1] = last
				continue
		merged.append(rect)
	return merged


func _tilemap_data(node: TileMapLayer, out: StreamPeerBuffer) -> void:
	var tile_set := node.tile_set
	if tile_set == null:
		_fail(node, "has no TileSet")
		return
	if tile_set.tile_shape != TileSet.TILE_SHAPE_SQUARE:
		_fail(node, "only square tile shapes are supported")
	if tile_set.get_physics_layers_count() > 1:
		_fail(node, "the TileSet has more than one physics layer")
	var has_physics := tile_set.get_physics_layers_count() == 1 and node.collision_enabled
	var size := Vector2(tile_set.tile_size)

	var used := node.get_used_cells()
	used.sort_custom(func(a: Vector2i, b: Vector2i) -> bool: return a.y < b.y or (a.y == b.y and a.x < b.x))
	var source: TileSetAtlasSource = null
	var cells := _new_buffer()
	var rects: Array[Rect2] = []
	var one_way_rects: Array[Rect2] = []
	var animated: Array[Vector2i] = []  # atlas coordinates of the animated tiles in use
	var transform_bits := (TileSetAtlasSource.TRANSFORM_FLIP_H | TileSetAtlasSource.TRANSFORM_FLIP_V
			| TileSetAtlasSource.TRANSFORM_TRANSPOSE)
	for cell in used:
		var where := "cell (%d, %d): " % [cell.x, cell.y]
		var cell_source := tile_set.get_source(node.get_cell_source_id(cell)) as TileSetAtlasSource
		if cell_source == null:
			_fail(node, where + "only atlas sources are supported (no scene collections)")
			continue
		if source != null and cell_source != source:
			_fail(node, where + "uses a second atlas; one TileMapLayer can take its tiles from one atlas")
			continue
		source = cell_source
		var coords := node.get_cell_atlas_coords(cell)
		# A tile that was flipped or turned in the editor has the transform in the upper
		# bits of its alternative id.
		var alternative := node.get_cell_alternative_tile(cell)
		if alternative & ~transform_bits != 0:
			_fail(node, where + "alternative tiles are not supported (flipped and turned tiles are)")
			continue
		var flip_h := alternative & TileSetAtlasSource.TRANSFORM_FLIP_H != 0
		var flip_v := alternative & TileSetAtlasSource.TRANSFORM_FLIP_V != 0
		var transpose := alternative & TileSetAtlasSource.TRANSFORM_TRANSPOSE != 0
		var anim := NONE16
		if cell_source.get_tile_animation_frames_count(coords) > 1:
			if cell_source.get_tile_animation_mode(coords) != TileSetAtlasSource.TILE_ANIMATION_MODE_DEFAULT:
				_fail(node, where + "only animation mode Default is supported (no random start times)")
			if not animated.has(coords):
				animated.append(coords)
			anim = animated.find(coords)
		var region := source.get_tile_texture_region(coords)
		var tile := source.get_tile_data(coords, 0)
		if Vector2(region.size) != size or tile.texture_origin != Vector2i.ZERO:
			_fail(node, where + "the picture of the tile must have the tile size of the grid and no texture origin")
			continue
		if tile.modulate != Color.WHITE:
			_fail(node, where + "a modulate on a tile is not supported")
		if cell.x < -32768 or cell.x > 32767 or cell.y < -32768 or cell.y > 32767:
			_fail(node, where + "is too far from the origin")
			continue
		cells.put_16(cell.x)
		cells.put_16(cell.y)
		cells.put_u16(region.position.x)
		cells.put_u16(region.position.y)
		cells.put_u8((TILE_FLIP_H if flip_h else 0) | (TILE_FLIP_V if flip_v else 0)
				| (TILE_TRANSPOSE if transpose else 0))
		cells.put_u8(0)
		cells.put_u16(anim)
		if not has_physics:
			continue
		for p in tile.get_collision_polygons_count(0):
			var points := tile.get_collision_polygon_points(0, p)
			# The shape turns with the picture: X and Y swapped first, then flipped.
			for i in points.size():
				var point := points[i]
				if transpose:
					point = Vector2(point.y, point.x)
				points[i] = Vector2(-point.x if flip_h else point.x, -point.y if flip_v else point.y)
			var box := Rect2(points[0], Vector2.ZERO) if points.size() > 0 else Rect2()
			for point in points:
				box = box.expand(point)
			var is_box := points.size() == 4 and box.has_area()
			for point in points:
				if not (is_equal_approx(point.x, box.position.x) or is_equal_approx(point.x, box.end.x)) \
						or not (is_equal_approx(point.y, box.position.y) or is_equal_approx(point.y, box.end.y)):
					is_box = false
			if not is_box:
				_fail(node, where + "its collision polygon must be a rectangle that is not rotated")
				continue
			var one_way := tile.is_collision_polygon_one_way(0, p)
			if one_way and (transpose or flip_v):
				_fail(node, where + "a tile with a one-way collision polygon must not be turned or flipped upside down")
				continue
			# Polygon points are relative to the middle of the tile.
			box.position += (Vector2(cell) + Vector2(0.5, 0.5)) * size
			if one_way:
				one_way_rects.append(box)
			else:
				rects.append(box)

	var merged := _merge_rows(rects)
	var merged_one_way := _merge_rows(one_way_rects)

	var asset := 0
	if source != null:
		# The animated tiles: where each of their pictures is and when it ends.
		var anims := _new_buffer()
		var frames := _new_buffer()
		var cells_off := 36
		var boxes_off := cells_off + cells.get_size()
		var anims_off := boxes_off + (merged.size() + merged_one_way.size()) * 20
		var frames_off := anims_off + animated.size() * 12
		for coords in animated:
			var speed := source.get_tile_animation_speed(coords)
			var count := source.get_tile_animation_frames_count(coords)
			var time := 0.0
			anims.put_u32(count)
			anims.put_u32(frames_off + frames.get_size())
			for frame in count:
				var frame_region := source.get_tile_texture_region(coords, frame)
				time += source.get_tile_animation_frame_duration(coords, frame) / speed
				frames.put_u16(frame_region.position.x)
				frames.put_u16(frame_region.position.y)
				frames.put_float(time)
			anims.put_float(time)

		var data := _new_buffer()
		data.put_u32(_texture_asset(node, source.texture, _canvas_filter(node)))
		data.put_float(size.x)
		data.put_float(size.y)
		data.put_u32(cells.get_size() / 12)
		data.put_u32(merged.size() + merged_one_way.size())
		data.put_u32(cells_off)
		data.put_u32(boxes_off)
		data.put_u32(animated.size())
		data.put_u32(anims_off)
		data.put_data(cells.data_array)
		for one_way in [false, true]:
			for rect: Rect2 in (merged_one_way if one_way else merged):
				data.put_float(rect.position.x)
				data.put_float(rect.position.y)
				data.put_float(rect.size.x)
				data.put_float(rect.size.y)
				data.put_u32(1 if one_way else 0)
		data.put_data(anims.data_array)
		data.put_data(frames.data_array)
		asset = _add_asset(ASSET_TILEMAP, _node_path(node) + " tiles", data.data_array)
	else:
		_fail(node, "has no tiles")
	out.put_u32(asset)
	out.put_u32(_rgba(node.modulate * node.self_modulate))
	out.put_u32(tile_set.get_physics_layer_collision_layer(0) if has_physics else 0)
	out.put_u32(tile_set.get_physics_layer_collision_mask(0) if has_physics else 0)


## The rectangle Godot laid the control out in. With a `size` the control is drawn at
## that size from the rectangle's top left corner instead.
func _put_rect(out: StreamPeerBuffer, control: Control, size := Vector2(-1, -1), offset := Vector2.ZERO) -> void:
	if control.rotation != 0.0 or control.scale != Vector2.ONE:
		_fail(control, "rotated or scaled controls are not supported")
	# A RichTextLabel clips by default; its text is checked to fit.
	if control.clip_contents and not (control is ScrollContainer or control is RichTextLabel):
		_fail(control, "clip_contents is not supported; a ScrollContainer cuts off what is inside it")
	var rect := control.get_global_rect()
	rect.position += offset
	if size.x >= 0.0:
		rect.size = size
	out.put_float(rect.position.x)
	out.put_float(rect.position.y)
	out.put_float(rect.size.x)
	out.put_float(rect.size.y)


## Where the focus goes from a button or slider: the neighbours the editor would move it
## to, explicit or found by position. Only buttons and sliders take the focus.
func _put_neighbors(out: StreamPeerBuffer, control: Control) -> void:
	for side: Side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		var neighbor := control.find_valid_focus_neighbor(side)
		out.put_u16(_node_index[neighbor] if (neighbor is BaseButton or neighbor is Slider)
				and _node_index.has(neighbor) else NONE16)


## Panel and PanelContainer: the theme's panel as a fill colour and a border. HSeparator
## and VSeparator: the line of their theme, as a thin rectangle in the middle of the node.
func _panel_data(node: Control, out: StreamPeerBuffer) -> void:
	var color := 0
	var border_color := 0
	var border := [0.0, 0.0, 0.0, 0.0]
	if node is Separator:
		var box := node.get_theme_stylebox("separator")
		var thickness := 0.0
		if box is StyleBoxLine:
			color = _rgba(box.color)
			thickness = box.thickness
		elif not (box is StyleBoxEmpty):
			_fail(node, "style 'separator' is a %s; only StyleBoxLine and StyleBoxEmpty are supported" % box.get_class())
		if node is HSeparator:
			_put_rect(out, node, Vector2(node.size.x, thickness), Vector2(0, (node.size.y - thickness) * 0.5))
		else:
			_put_rect(out, node, Vector2(thickness, node.size.y), Vector2((node.size.x - thickness) * 0.5, 0))
	else:
		var box := node.get_theme_stylebox("panel")
		if box is StyleBoxFlat:
			color = _rgba(box.bg_color) if box.draw_center else 0
			border_color = _rgba(box.border_color)
			border = [float(box.border_width_left), float(box.border_width_top), float(box.border_width_right),
					float(box.border_width_bottom)]
		elif not (box is StyleBoxEmpty):
			_fail(node, "style 'panel' is a %s; only StyleBoxFlat and StyleBoxEmpty are supported" % box.get_class())
		_put_rect(out, node)
	out.put_u32(color)
	out.put_u32(border_color)
	for width: float in border:
		out.put_float(width)


func _texture_progress_data(node: TextureProgressBar, out: StreamPeerBuffer) -> void:
	if node.fill_mode > TextureProgressBar.FILL_BOTTOM_TO_TOP:
		_fail(node, "only the four straight fill modes are supported (no radial or bilinear fill)")
	if node.nine_patch_stretch:
		_fail(node, "nine_patch_stretch is not supported; the textures are drawn at their own size")
	if node.texture_progress_offset != Vector2.ZERO:
		_fail(node, "texture_progress_offset is not supported")
	if node.texture_under == null and node.texture_progress == null and node.texture_over == null:
		_fail(node, "has no texture")
	_put_rect(out, node)
	out.put_float(node.min_value)
	out.put_float(node.max_value)
	out.put_float(node.value)
	for texture: Texture2D in [node.texture_under, node.texture_progress, node.texture_over]:
		out.put_u32(_texture_asset(node, texture, _canvas_filter(node)) if texture != null else NONE32)
	var shade := node.modulate * node.self_modulate
	for tint: Color in [node.tint_under, node.tint_progress, node.tint_over]:
		out.put_u32(_rgba(tint * shade))
	out.put_u8(node.fill_mode)
	out.put_u8(0)
	out.put_u16(0)


## RichTextLabel. The runtime reads the tags itself while it draws, so the text can change
## while the game runs; it knows the ones in RICH_TAGS. A colour by name becomes its hex
## value here, which is the only form the runtime reads.
const RICH_TAGS: Array[String] = ["b", "i", "color", "center", "right", "left", "lb", "rb"]

func _rich_text_data(node: RichTextLabel, out: StreamPeerBuffer) -> void:
	if node.vertical_alignment > VERTICAL_ALIGNMENT_BOTTOM or node.horizontal_alignment != HORIZONTAL_ALIGNMENT_LEFT:
		_fail(node, "vertical alignment Fill and a horizontal alignment are not supported; use [center] and [right]")
	if node.visible_characters != -1 or node.visible_ratio < 1.0:
		_fail(node, "visible_characters and visible_ratio are not supported")
	if node.get_content_height() > node.size.y + 1.0:
		_fail(node, "the text is higher than the label; scrolling inside a RichTextLabel is not supported")
	var text := node.text
	var bold := false
	var italic := false
	if node.bbcode_enabled:
		var tag := RegEx.create_from_string("\\[(/?)([a-z_]+)(=[^\\]\\[]*)?\\]")
		var rebuilt := ""
		var at := 0
		for found in tag.search_all(text):
			var tag_name := found.get_string(2)
			var written := found.get_string(0)
			if not RICH_TAGS.has(tag_name):
				_fail(node, "BBCode tag [%s] is not supported (supported: %s)" % [tag_name, ", ".join(RICH_TAGS)])
			elif tag_name == "color" and found.get_string(1) == "":
				var value := found.get_string(3).trim_prefix("=")
				var none := Color(-1, -1, -1, -1)
				var color := Color.from_string(value, none)
				if color == none:
					_fail(node, "'%s' in a [color] tag is no colour" % value)
				written = "[color=#%s]" % color.to_html(true)
			bold = bold or tag_name == "b"
			italic = italic or tag_name == "i"
			rebuilt += text.substr(at, found.get_start() - at) + written
			at = found.get_end()
		text = rebuilt + text.substr(at)

	_put_rect(out, node)
	out.put_u32(_string(text))
	# A font is baked only if the text of the scene uses it; a [b] that a component writes
	# later into a label without one comes out in the normal font.
	var styles := [["normal_font", true], ["bold_font", bold], ["italics_font", italic],
			["bold_italics_font", bold and italic]]
	for style: Array in styles:
		out.put_u32(_font_asset(node, node.get_theme_font(style[0]), node.get_theme_font_size(style[0] + "_size"))
				if style[1] else NONE32)
	out.put_u32(_rgba(node.get_theme_color("default_color") * node.modulate * node.self_modulate))
	out.put_u8(1 if node.bbcode_enabled else 0)
	out.put_u8(node.autowrap_mode)
	out.put_u8(node.vertical_alignment)
	out.put_u8(0)


## The one colour that stands for a StyleBox, or -1 if it is of a kind the runtime cannot draw.
func _flat_color(box: StyleBox) -> int:
	if box is StyleBoxEmpty:
		return 0
	if box is StyleBoxFlat:
		return _rgba(box.bg_color) if box.draw_center else 0
	return -1


func _scroll_data(node: ScrollContainer, out: StreamPeerBuffer) -> void:
	var content := Vector2.ZERO
	for child in node.get_children():
		if child is Control and child.visible:
			content = content.max(child.size)
	_put_rect(out, node)
	out.put_float(content.x)
	out.put_float(content.y)
	out.put_float(node.scroll_horizontal)
	out.put_float(node.scroll_vertical)
	# The bars are where Godot put them; the runtime draws them as two rectangles.
	var bar_color := 0
	var grabber_color := 0
	for bar: ScrollBar in [node.get_h_scroll_bar(), node.get_v_scroll_bar()]:
		var rect := bar.get_global_rect() if bar.visible else Rect2()
		out.put_float(rect.position.x)
		out.put_float(rect.position.y)
		out.put_float(rect.size.x)
		out.put_float(rect.size.y)
		if bar.visible:
			bar_color = _flat_color(bar.get_theme_stylebox("scroll"))
			grabber_color = _flat_color(bar.get_theme_stylebox("grabber"))
			if bar_color < 0 or grabber_color < 0:
				_fail(node, "the styles of its scroll bar must be StyleBoxFlat or StyleBoxEmpty")
	out.put_u32(maxi(bar_color, 0))
	out.put_u32(maxi(grabber_color, 0))
	out.put_u8(1 if node.horizontal_scroll_mode != ScrollContainer.SCROLL_MODE_DISABLED else 0)
	out.put_u8(1 if node.vertical_scroll_mode != ScrollContainer.SCROLL_MODE_DISABLED else 0)
	out.put_u8(1 if node.follow_focus else 0)
	out.put_u8(0)


## A picture of the theme (the box of a CheckBox, the grabber of a slider) as a texture.
func _theme_icon(control: Control, icon_name: String) -> int:
	return _texture_asset(control, control.get_theme_icon(icon_name), _canvas_filter(control),
			"theme:%s/%s" % [control.get_class(), icon_name])


func _label_data(label: Label, out: StreamPeerBuffer) -> void:
	if label.label_settings != null:
		_fail(label, "LabelSettings are not supported; use theme overrides")
	if label.horizontal_alignment > HORIZONTAL_ALIGNMENT_RIGHT or label.vertical_alignment > VERTICAL_ALIGNMENT_BOTTOM:
		_fail(label, "alignment Fill is not supported")
	_put_rect(out, label)
	out.put_u32(_string(label.text.to_upper() if label.uppercase else label.text))
	out.put_u32(_font_asset(label, label.get_theme_font("font"), label.get_theme_font_size("font_size")))
	out.put_u32(_rgba(label.get_theme_color("font_color") * label.modulate * label.self_modulate))
	out.put_u8(label.horizontal_alignment)
	out.put_u8(label.vertical_alignment)
	# The runtime breaks the lines itself, so the text can change while the game runs.
	out.put_u8(label.autowrap_mode)
	out.put_u8(0)


## Button, CheckBox, CheckButton and TextureButton.
func _button_data(button: BaseButton, out: StreamPeerBuffer) -> void:
	if button.button_group != null or button.shortcut != null:
		_fail(button, "button groups and shortcuts are not supported")
	var plain := button as Button  # with text; null for a TextureButton
	var textured := button as TextureButton
	var icon := NONE32
	var icon_pressed := NONE32
	var textures: Array[int] = [NONE32, NONE32, NONE32, NONE32]
	var size := Vector2(-1, -1)

	if plain != null:
		if plain.alignment > HORIZONTAL_ALIGNMENT_RIGHT:
			_fail(button, "alignment Fill is not supported")
		if button is CheckBox or button is CheckButton:
			if plain.icon != null:
				_fail(button, "an icon next to the box of a %s is not supported" % button.get_class())
			icon = _theme_icon(button, "unchecked")
			icon_pressed = _theme_icon(button, "checked")
		elif plain.icon != null:
			if (plain.icon_alignment != HORIZONTAL_ALIGNMENT_LEFT or plain.expand_icon
					or plain.vertical_icon_alignment != VERTICAL_ALIGNMENT_CENTER):
				_fail(button, "the icon must be on the left, centred vertically and not expanded")
			icon = _texture_asset(button, plain.icon, _canvas_filter(button))
	else:
		if textured.texture_normal == null:
			_fail(button, "a TextureButton needs a normal texture")
			return
		if textured.flip_h or textured.flip_v:
			_fail(button, "flipped textures are not supported")
		if textured.stretch_mode == TextureButton.STRETCH_KEEP:
			size = textured.texture_normal.get_size()
		elif textured.stretch_mode != TextureButton.STRETCH_SCALE:
			_fail(button, "only stretch modes Scale and Keep are supported")
		# Nothing hovers without a mouse: the focus shows the hover picture where there is
		# no picture for the focus itself.
		var focused := textured.texture_focused if textured.texture_focused != null else textured.texture_hover
		var states: Array[Texture2D] = [textured.texture_normal, textured.texture_pressed, focused,
				textured.texture_disabled]
		for i in states.size():
			if states[i] != null:
				textures[i] = _texture_asset(button, states[i], _canvas_filter(button))

	_put_rect(out, button, size)
	out.put_u32(_string(plain.text if plain != null else ""))
	out.put_u32(_font_asset(button, button.get_theme_font("font"), button.get_theme_font_size("font_size"))
			if plain != null else NONE32)
	out.put_u32(_rgba(button.get_theme_color("font_color")) if plain != null else 0)
	out.put_u32(_rgba(button.get_theme_color("font_focus_color")) if plain != null else 0)
	out.put_u32(0 if plain == null or plain.flat else _stylebox_color(button, "normal"))
	out.put_u32(_stylebox_color(button, "focus"))
	_put_neighbors(out, button)
	out.put_u32(_rgba(button.get_theme_color("font_disabled_color")) if plain != null else 0)
	out.put_u32(_rgba(button.get_theme_color("font_pressed_color")) if plain != null else 0)
	out.put_u32(0 if plain == null or plain.flat else _stylebox_color(button, "disabled"))
	out.put_u32(_stylebox_color(button, "pressed") if plain != null else 0)
	out.put_u32(icon)
	out.put_u32(icon_pressed)
	for texture in textures:
		out.put_u32(texture)
	out.put_float(button.get_theme_stylebox("normal").get_margin(SIDE_LEFT) if plain != null else 0.0)
	out.put_float(button.get_theme_constant("h_separation") if plain != null else 0.0)
	out.put_u8(plain.alignment if plain != null else HORIZONTAL_ALIGNMENT_CENTER)
	out.put_u8(1 if button.toggle_mode else 0)
	# export_scene() enabled the button while the focus neighbours are worked out.
	out.put_u8(1 if _disabled.has(button) else 0)
	out.put_u8(1 if button.toggle_mode and button.button_pressed else 0)


## HSlider and VSlider.
func _slider_data(slider: Slider, out: StreamPeerBuffer) -> void:
	if slider.exp_edit or slider.allow_greater or slider.allow_lesser or slider.rounded:
		_fail(slider, "exp_edit, allow_greater, allow_lesser and rounded are not supported")
	if slider.tick_count > 0:
		_fail(slider, "ticks are not supported")
	if slider.step <= 0.0:
		_fail(slider, "step must be above 0: a direction on the pad changes the value by one step")
	var vertical := slider is VSlider
	var track_size := slider.get_theme_stylebox("slider").get_minimum_size()
	_put_rect(out, slider)
	out.put_float(slider.min_value)
	out.put_float(slider.max_value)
	out.put_float(slider.step)
	out.put_float(slider.value)
	out.put_float(maxf(track_size.x if vertical else track_size.y, 1.0))
	out.put_u32(_stylebox_color(slider, "slider"))
	out.put_u32(_stylebox_color(slider, "grabber_area"))
	out.put_u32(_stylebox_color(slider, "grabber_area_highlight"))
	out.put_u32(_theme_icon(slider, "grabber"))
	out.put_u32(_theme_icon(slider, "grabber_highlight"))
	out.put_u32(_theme_icon(slider, "grabber_disabled"))
	_put_neighbors(out, slider)
	out.put_u8(1 if vertical else 0)
	out.put_u8(1 if slider.editable else 0)
	out.put_u16(0)


## The runtime draws a button as a filled rectangle and marks the focus with an outline.
## This picks the one colour that stands for a StyleBox of the theme.
func _stylebox_color(button: Control, style_name: String) -> int:
	var box := button.get_theme_stylebox(style_name)
	if box is StyleBoxEmpty:
		return 0
	if box is StyleBoxFlat:
		if not box.draw_center and box.border_width_left > 0:
			return _rgba(box.border_color)
		return _rgba(box.bg_color) if box.draw_center else 0
	_fail(button, "style '%s' is a %s; only StyleBoxFlat and StyleBoxEmpty are supported"
			% [style_name, box.get_class()])
	return 0


## The project's audio buses (the Audio tab of the editor; default_bus_layout.tres): name,
## volume, mute and the bus each one plays into. Effects are not part of the runtime.
func _write_buses() -> StreamPeerBuffer:
	var out := _new_buffer()
	if AudioServer.bus_count > MAX_BUSES:
		errors.append("the project has %d audio buses, the limit is %d" % [AudioServer.bus_count, MAX_BUSES])
		return out
	for i in AudioServer.bus_count:
		var bus_name := AudioServer.get_bus_name(i)
		var send := AudioServer.get_bus_index(AudioServer.get_bus_send(i)) if i > 0 else -1
		if AudioServer.get_bus_effect_count(i) > 0:
			errors.append("audio bus '%s' has effects, which are not supported" % bus_name)
		if AudioServer.is_bus_solo(i) or AudioServer.is_bus_bypassing_effects(i):
			errors.append("audio bus '%s': solo and bypass are not supported" % bus_name)
		out.put_u32(_string(bus_name))
		out.put_float(db_to_linear(AudioServer.get_bus_volume_db(i)))
		# A send to a bus that is gone plays into Master, as in Godot.
		out.put_u16(NONE16 if i == 0 else maxi(send, 0))
		out.put_u8(1 if AudioServer.is_bus_mute(i) else 0)
		out.put_u8(0)
	return out


## True for a node with a text that Godot would translate by itself: its text is a key of
## the project's translations, unless auto translate is switched off for it.
func _translates(node: Node) -> bool:
	return (node is Label or node is Button or node is Label3D or node is RichTextLabel) \
			and node.can_auto_translate()


## The project's translations (Project Settings > Localization > Translations): the
## locales, every key, and the message of every key in every locale. Contexts and plural
## forms are not part of the runtime. The fonts are baked with FONT_RANGES only, so a
## message with a character outside of them could not be drawn and is refused.
func _write_translations() -> StreamPeerBuffer:
	var out := _new_buffer()
	var by_locale := {}  # locale -> {key -> message}
	var keys := {}
	for path: String in ProjectSettings.get_setting("internationalization/locale/translations", PackedStringArray()):
		var translation := load(path) as Translation
		if translation == null:
			errors.append("cannot load the translation '%s'" % path)
			continue
		if translation is OptimizedTranslation:
			errors.append(("the translation '%s' is compressed and does not hold its keys; "
					+ "set Compress to Disabled in the Import dock of its CSV file") % path)
			continue
		if not by_locale.has(translation.locale):
			by_locale[translation.locale] = {}
		for key: String in translation.get_message_list():
			var message := String(translation.get_message(key))
			for i in message.length():
				var code := message.unicode_at(i)
				var known := code == 10
				for codes in FONT_RANGES:
					known = known or (code >= codes.x and code <= codes.y)
				if not known:
					errors.append("translation '%s' of '%s' has the character '%s', which the fonts are not baked with"
							% [translation.locale, key, message[i]])
					break
			by_locale[translation.locale][key] = message
			keys[key] = true
	if by_locale.is_empty():
		return out

	var locales: Array = by_locale.keys()
	locales.sort()
	# The order the runtime searches in: by the bytes of the keys, as strcmp does.
	var sorted_keys: Array = keys.keys()
	sorted_keys.sort_custom(func(a: String, b: String) -> bool:
		var bytes_a := a.to_utf8_buffer()
		var bytes_b := b.to_utf8_buffer()
		for i in mini(bytes_a.size(), bytes_b.size()):
			if bytes_a[i] != bytes_b[i]:
				return bytes_a[i] < bytes_b[i]
		return bytes_a.size() < bytes_b.size())
	var fallback: String = ProjectSettings.get_setting("internationalization/locale/fallback", "en")
	out.put_u32(locales.size())
	out.put_u32(sorted_keys.size())
	out.put_u32(maxi(locales.find(fallback), 0))
	for locale: String in locales:
		out.put_u32(_string(locale))
	for key: String in sorted_keys:
		out.put_u32(_string(key))
	for locale: String in locales:
		for key: String in sorted_keys:
			var message: String = by_locale[locale].get(key, "")
			out.put_u32(_string(message) if message != "" else 0)
	return out


## AudioStreamPlayer, AudioStreamPlayer2D and AudioStreamPlayer3D.
func _audio_data(player: Node, out: StreamPeerBuffer) -> void:
	if player.pitch_scale <= 0.0:
		_fail(player, "pitch_scale must be above 0")
	var looping := false
	var music := false
	var stream: AudioStream = player.stream
	if stream is AudioStreamWAV:
		looping = stream.loop_mode == AudioStreamWAV.LOOP_FORWARD
		if stream.loop_mode > AudioStreamWAV.LOOP_FORWARD or (looping and stream.loop_begin != 0):
			_fail(player, "only forward loops over the whole sound are supported")
	elif stream is AudioStreamOggVorbis or stream is AudioStreamMP3:
		# Compressed streams are music: decoded here, streamed from a file at runtime.
		music = true
		looping = stream.loop
		if stream.loop_offset != 0.0:
			_fail(player, "a loop offset is not supported")
		if not (player is AudioStreamPlayer):
			_fail(player, "music (Ogg or MP3) can only be played by a plain AudioStreamPlayer")
		if player.pitch_scale != 1.0:
			_fail(player, "pitch_scale is not supported for music (Ogg or MP3)")

	var mode := AUDIO_PLAIN
	var model := 0
	var unit_size := 1.0
	var max_distance := 0.0
	var max_gain := 1.0
	var attenuation := 1.0
	var pan_strength := 0.0
	if player is AudioStreamPlayer3D:
		mode = AUDIO_3D
		if player.emission_angle_enabled or player.doppler_tracking != AudioStreamPlayer3D.DOPPLER_TRACKING_DISABLED:
			_fail(player, "emission angle and doppler tracking are not supported")
		model = player.attenuation_model
		unit_size = player.unit_size
		max_distance = player.max_distance
		max_gain = db_to_linear(player.max_db)
		pan_strength = player.panning_strength
	elif player is AudioStreamPlayer2D:
		mode = AUDIO_2D
		max_distance = player.max_distance
		attenuation = player.attenuation
		pan_strength = player.panning_strength

	out.put_u32(_music_asset(player, stream) if music else _sound_asset(player, stream))
	out.put_float(db_to_linear(player.volume_db))
	# platforms.gd switches autoplay off while the scene is in the tree and notes it here.
	out.put_u8(1 if player.autoplay or player.get_meta(&"retro_autoplay", false) else 0)
	out.put_u8(1 if looping else 0)
	out.put_u8(mode)
	out.put_u8(model)
	out.put_float(unit_size)
	out.put_float(max_distance)
	out.put_float(max_gain)
	out.put_float(attenuation)
	out.put_float(pan_strength)
	out.put_float(player.pitch_scale)
	# A bus that does not exist plays on Master, as in Godot.
	out.put_u16(maxi(AudioServer.get_bus_index(player.bus), 0))
	out.put_u16(0)


## Music: decodes a compressed stream and writes it as a 16-bit PCM WAV file next to the
## package, at the sample rate the baker asks for. The asset only names that file.
func _music_asset(node: Node, stream: AudioStream) -> int:
	if _asset_cache.has(stream):
		return _asset_cache[stream]
	var file_name := stream.resource_path.get_file().get_basename() + ".wav"
	var mix_rate := int(AudioServer.get_mix_rate())
	var step := maxi(1, roundi(float(mix_rate) / _baker.music_rate()))
	var playback := stream.instantiate_playback()
	if playback == null:
		_fail(node, "cannot decode '%s'" % stream.resource_path)
		return 0
	playback.start(0.0)
	# A WAV file is little-endian whatever the platform is.
	var pcm := StreamPeerBuffer.new()
	var left := 0.0
	var right := 0.0
	var held := 0
	var frames_left := int(stream.get_length() * mix_rate)
	while frames_left > 0:
		var chunk: PackedVector2Array = playback.mix_audio(1.0, mini(4096, frames_left))
		if chunk.is_empty():
			break
		frames_left -= chunk.size()
		for frame in chunk:
			# Averaging neighbours is the whole resampler: good enough for halving the rate.
			left += frame.x
			right += frame.y
			held += 1
			if held == step:
				pcm.put_16(clampi(roundi(left / step * 32767.0), -32768, 32767))
				pcm.put_16(clampi(roundi(right / step * 32767.0), -32768, 32767))
				left = 0.0
				right = 0.0
				held = 0
	playback.stop()
	var rate := mix_rate / step
	var wav := StreamPeerBuffer.new()
	wav.put_data("RIFF".to_ascii_buffer())
	wav.put_u32(36 + pcm.get_size())
	wav.put_data("WAVEfmt ".to_ascii_buffer())
	wav.put_u32(16)
	wav.put_u16(1)  # PCM
	wav.put_u16(2)  # stereo
	wav.put_u32(rate)
	wav.put_u32(rate * 4)
	wav.put_u16(4)
	wav.put_u16(16)
	wav.put_data("data".to_ascii_buffer())
	wav.put_u32(pcm.get_size())
	wav.put_data(pcm.data_array)
	_music_files[file_name] = wav.data_array

	var data := _new_buffer()
	data.put_u32(_string(file_name))
	data.put_float(float(pcm.get_size() / 4) / rate)
	var asset := _add_asset(ASSET_MUSIC, _resource_name(stream, _node_path(node)), data.data_array)
	_asset_cache[stream] = asset
	return asset


# --- components, connections, input actions --------------------------------------------------

## Writes the component of a node (the class name of its placeholder script plus the
## exported values). Returns the number of fields written.
func _write_component(node: Node, index: int, comps: StreamPeerBuffer, fields: StreamPeerBuffer,
		field_first: int) -> int:
	var script: Script = node.get_script()
	var component_name := String(script.get_global_name())
	if component_name == "":
		_fail(node, "the script '%s' has no class_name, so it cannot be matched to a C component"
				% script.resource_path)
	var count := _write_fields(node, node, script, fields)
	comps.put_u32(_string(component_name))
	comps.put_u16(index)
	comps.put_u16(count)
	comps.put_u32(field_first)
	return count


## Writes the resources that components and other resources refer to, with their fields
## after the ones of the components. Returns the resource table.
func _write_resources(fields: StreamPeerBuffer, field_first: int) -> StreamPeerBuffer:
	var out := _new_buffer()
	var done := 0
	# The list grows while it is written: resources may hold resources.
	while done < _resources.size():
		var resource: Resource = _resources[done]
		var script: Script = resource.get_script()
		var count := _write_fields(_resource_users[done], resource, script, fields)
		out.put_u32(_string(String(script.get_global_name())))
		out.put_u16(count)
		out.put_u16(0)
		out.put_u32(field_first)
		field_first += count
		done += 1
	return out


## Index of a custom resource in the resource table, NONE32 for none. `node` is the node
## that uses it, for error messages.
func _resource_index(node: Node, field: String, resource: Resource) -> int:
	if resource == null:
		return NONE32
	var script: Script = resource.get_script()
	if script == null or String(script.get_global_name()) == "":
		_fail(node, "exported field '%s' holds a %s; only resources with a script that has a class_name are supported"
				% [field, resource.get_class()])
		return NONE32
	var index := _resources.find(resource)
	if index < 0:
		index = _resources.size()
		_resources.append(resource)
		_resource_users.append(node)
	return index


## Writes the exported values of `object` (a node or a resource) with the script `script`.
## Returns the number of fields written.
func _write_fields(node: Node, object: Object, script: Script, fields: StreamPeerBuffer) -> int:
	var count := 0
	for property in script.get_script_property_list():
		var usage: int = property.usage
		if not (usage & PROPERTY_USAGE_SCRIPT_VARIABLE) or not (usage & PROPERTY_USAGE_STORAGE):
			continue
		var property_name: String = property.name
		var value = object.get(property_name)
		var field_type := 0
		var bits := _new_buffer()
		match property.type:
			TYPE_FLOAT:
				field_type = FIELD_FLOAT
				bits.put_float(value)
			TYPE_INT:
				field_type = FIELD_INT
				if value < -0x80000000 or value > 0x7FFFFFFF:
					_fail(node, "exported field '%s' does not fit into 32 bits" % property_name)
					value = 0
				bits.put_32(value)
			TYPE_BOOL:
				field_type = FIELD_BOOL
				bits.put_u32(1 if value else 0)
			TYPE_OBJECT when property.hint == PROPERTY_HINT_NODE_TYPE and object is Node:
				field_type = FIELD_NODE
				# In the editor, scripts are not running and the value is still a path.
				if value is NodePath:
					value = node.get_node_or_null(value)
				if value == null:
					bits.put_u32(NONE32)
				elif _node_index.has(value):
					bits.put_u32(_node_index[value])
				else:
					_fail(node, "exported field '%s' points to a node outside the scene" % property_name)
					bits.put_u32(NONE32)
			TYPE_OBJECT when property.hint == PROPERTY_HINT_RESOURCE_TYPE and property.hint_string == "PackedScene" and object is Node:
				# The index of the pool that prepare() made for this field.
				field_type = FIELD_PACKED_SCENE
				bits.put_u32(_pool_of.get(_pool_key(node, property_name), NONE32))
			TYPE_OBJECT when property.hint == PROPERTY_HINT_RESOURCE_TYPE and ProjectSettings.get_global_class_list().any(
					func(c: Dictionary) -> bool: return c["class"] == property.hint_string):
				# A custom resource: a script class that extends Resource.
				field_type = FIELD_RESOURCE
				bits.put_u32(_resource_index(node, property_name, value))
			TYPE_STRING when property.hint == PROPERTY_HINT_FILE and "tscn" in property.hint_string:
				# A scene reference is stored as the scene's name; the runtime loads
				# <name>.rpk from its data directory.
				field_type = FIELD_SCENE
				var scene_path := String(value)
				if scene_path != "" and not ResourceLoader.exists(scene_path):
					_fail(node, "exported field '%s' points to '%s', which does not exist"
							% [property_name, scene_path])
				bits.put_u32(_string(scene_path.get_file().get_basename()))
			_:
				_fail(node, "exported field '%s' has a type the runtime does not support "
						% property_name + "(supported: float, int, bool, node, scene file path, PackedScene, custom Resource)")
				continue
		fields.put_u32(_string(property_name))
		fields.put_u8(field_type)
		fields.put_u8(0)
		fields.put_u16(0)
		fields.put_data(bits.data_array)
		count += 1
	return count


## Writes the signal connections that were made in the editor. Returns their number.
func _write_connections(conns: StreamPeerBuffer) -> int:
	var count := 0
	for i in _nodes.size():
		var node := _nodes[i]
		for sig in node.get_signal_list():
			for connection in node.get_signal_connection_list(sig.name):
				if not (connection.flags & CONNECT_PERSIST):
					continue
				var callable: Callable = connection.callable
				var target := callable.get_object() as Node
				if target == null or not _node_index.has(target):
					_fail(node, "signal '%s' is connected to something outside the scene" % sig.name)
					continue
				if callable.get_bound_arguments_count() > 0 or callable.get_unbound_arguments_count() > 0:
					_fail(node, "signal '%s' is connected with bound or unbound arguments, "
							% sig.name + "which is not supported")
					continue
				if target.get_script() == null:
					_fail(node, "signal '%s' is connected to node '%s', which has no component"
							% [sig.name, _node_path(target)])
					continue
				conns.put_u16(i)
				conns.put_u16(_node_index[target])
				conns.put_u32(_string(sig.name))
				conns.put_u32(_string(callable.get_method()))
				count += 1
	return count


## Writes the project's Input Map with this platform's key and button codes. Events for
## devices the platform does not have are left out; an action that ends up with no
## binding at all is an error. Returns the number of actions.
func _write_actions(actions: StreamPeerBuffer, bindings: StreamPeerBuffer) -> int:
	var count := 0
	# The actions are read from the project settings, not from the InputMap singleton:
	# inside the editor that one holds the editor's own shortcuts instead.
	var names: Array[String] = []
	for property in ProjectSettings.get_property_list():
		var setting: String = property.name
		if setting.begins_with("input/") and ProjectSettings.get_setting(setting) is Dictionary:
			names.append(setting.trim_prefix("input/"))
	names.sort()
	for action_name in names:
		var action: Dictionary = ProjectSettings.get_setting("input/" + action_name)
		var builtin := action_name.begins_with("ui_")
		if builtin and not UI_ACTIONS.has(action_name):
			continue
		var list: Array = []  # of [kind, code, sign]
		for event in action.get("events", []):
			var binding := _binding_of(action_name, event)
			if not binding.is_empty() and not list.has(binding):
				list.append(binding)
		if list.is_empty() and builtin:
			list = _baker.default_ui_bindings(action_name)
		if list.is_empty():
			errors.append("input action '%s' has no key or button that exists on platform '%s'"
					% [action_name, _baker.platform_name()])
			continue
		actions.put_u32(_string(action_name))
		actions.put_u16(bindings.get_size() / 4)
		actions.put_u16(list.size())
		actions.put_float(action.get("deadzone", 0.5))
		for binding in list:
			bindings.put_u8(binding[0])
			bindings.put_8(binding[2])
			bindings.put_u16(binding[1])
		count += 1
	return count


## Returns [kind, code, sign] for an input event, or an empty array if the platform has
## no such input.
func _binding_of(action_name: String, event: InputEvent) -> Array:
	var kind := 0
	var code: int = _baker.NOT_AVAILABLE
	var axis_sign := 0
	var what := ""
	if event is InputEventKey:
		var keycode: int = event.physical_keycode if event.physical_keycode != 0 else event.keycode
		kind = _baker.BIND_KEY
		code = _baker.key_code(keycode)
		what = "key '%s'" % OS.get_keycode_string(keycode)
	elif event is InputEventJoypadButton:
		kind = _baker.BIND_PAD_BUTTON
		code = _baker.pad_button_code(event.button_index)
		what = "joypad button %d" % event.button_index
	elif event is InputEventJoypadMotion:
		kind = _baker.BIND_PAD_AXIS
		code = _baker.pad_axis_code(event.axis)
		axis_sign = -1 if event.axis_value < 0.0 else 1
		what = "joypad axis %d" % event.axis
	if code == _baker.UNKNOWN:
		# The built-in ui actions carry many default keys; an odd one there is no reason
		# to stop, a key the project bound itself is.
		if not action_name.begins_with("ui_"):
			errors.append("input action '%s': %s is missing from the %s baker's table"
					% [action_name, what, _baker.platform_name()])
		return []
	if code == _baker.NOT_AVAILABLE:
		return []
	return [kind, code, axis_sign]


# --- assets ---------------------------------------------------------------------------

func _add_asset(type: int, asset_name: String, data: PackedByteArray) -> int:
	_asset_types.append(type)
	_asset_names.append(asset_name)
	_asset_data.append(data)
	return _asset_data.size() - 1


func _resource_name(resource: Resource, fallback: String) -> String:
	return resource.resource_path.trim_prefix("res://") if resource.resource_path != "" else fallback


func _texture_asset(node: Node, texture: Texture2D, filter: int, fallback_name := "") -> int:
	if texture == null:
		_fail(node, "has no texture")
		return 0
	# The filter is stored with the pixels, so a texture used both ways is stored twice.
	var key := "texture:%d:%d" % [texture.get_instance_id(), filter]
	if _asset_cache.has(key):
		return _asset_cache[key]
	# Read the source file where there is one, so the export does not depend on what the
	# importer made of it (compression, mipmaps).
	var image: Image = null
	var path := texture.resource_path
	if path.get_extension().to_lower() in ["png", "jpg", "jpeg", "webp", "bmp", "tga"] and FileAccess.file_exists(path):
		image = Image.load_from_file(ProjectSettings.globalize_path(path))
	else:
		image = texture.get_image()
	if image == null or image.is_empty():
		_fail(node, "cannot read the pixels of texture '%s'" % path)
		return 0
	var asset := _bake_image(node, image, _resource_name(texture,
			fallback_name if fallback_name != "" else _node_path(node)), filter)
	_asset_cache[key] = asset
	return asset


## Filter of a 2D node's texture: its own setting, or the one it inherits from its
## parents and finally from the project settings. There are no mipmaps in the runtime,
## so the mipmap variants count as their plain filter.
func _canvas_filter(item: CanvasItem) -> int:
	var filter := item.texture_filter
	while filter == CanvasItem.TEXTURE_FILTER_PARENT_NODE:
		var parent := item.get_parent() if item != _root else null
		if not (parent is CanvasItem):
			# Project setting: 0 nearest, 1 linear, 2 linear mipmap, 3 nearest mipmap.
			var setting: int = ProjectSettings.get_setting(
					"rendering/textures/canvas_textures/default_texture_filter", 1)
			return FILTER_NEAREST if setting == 0 or setting == 3 else FILTER_LINEAR
		item = parent
		filter = item.texture_filter
	if filter in [CanvasItem.TEXTURE_FILTER_NEAREST, CanvasItem.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS,
			CanvasItem.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC]:
		return FILTER_NEAREST
	return FILTER_LINEAR


func _material_filter(material: BaseMaterial3D) -> int:
	if material.texture_filter in [BaseMaterial3D.TEXTURE_FILTER_NEAREST,
			BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS,
			BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC]:
		return FILTER_NEAREST
	return FILTER_LINEAR


func _bake_image(node: Node, image: Image, asset_name: String, filter: int) -> int:
	if image.is_compressed():
		image.decompress()
	image.convert(Image.FORMAT_RGBA8)
	var messages := PackedStringArray()
	var data: PackedByteArray = _baker.bake_texture(image, filter, messages)
	for message in messages:
		_fail(node, "texture '%s': %s" % [asset_name, message])
	return _add_asset(ASSET_TEXTURE, asset_name, data)


## Bakes a font at one size into an atlas texture plus a glyph table. The glyphs are
## rendered by Godot's text server, so they match the editor.
func _font_asset(node: Node, font: Font, size: int) -> int:
	var rids := font.get_rids() if font != null else []
	if rids.is_empty():
		_fail(node, "has no usable font")
		return 0
	var rid: RID = rids[0]
	var key := "font:%d:%d" % [rid.get_id(), size]
	if _asset_cache.has(key):
		return _asset_cache[key]

	var server := TextServerManager.get_primary_interface()
	if server.font_is_multichannel_signed_distance_field(rid):
		_fail(node, "MSDF fonts are not supported")
		return 0
	var font_size := Vector2i(size, 0)
	var glyphs: Array[Dictionary] = []
	for codepoints in FONT_RANGES:
		for codepoint in range(codepoints.x, codepoints.y + 1):
			var glyph := server.font_get_glyph_index(rid, size, codepoint, 0)
			if glyph == 0:
				continue
			server.font_render_glyph(rid, font_size, glyph)
			glyphs.append({
				"codepoint": codepoint,
				"rect": Rect2i(server.font_get_glyph_uv_rect(rid, font_size, glyph)),
				"offset": server.font_get_glyph_offset(rid, font_size, glyph),
				"advance": server.font_get_glyph_advance(rid, size, glyph).x,
				"page": server.font_get_glyph_texture_idx(rid, font_size, glyph),
				"place": Vector2i.ZERO,
			})

	# Shelf packing into the smallest power-of-two atlas that takes all glyphs.
	var atlas_size := Vector2i(64, 64)
	while not _place_glyphs(glyphs, atlas_size):
		if atlas_size.x > atlas_size.y:
			atlas_size.y *= 2
		else:
			atlas_size.x *= 2
		if atlas_size.x > 1024:
			_fail(node, "font size %d does not fit into a 1024x1024 atlas" % size)
			return 0

	var atlas := Image.create_empty(atlas_size.x, atlas_size.y, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(1, 1, 1, 0))
	var pages := {}
	for glyph in glyphs:
		var rect: Rect2i = glyph.rect
		if glyph.page < 0 or rect.size.x <= 0 or rect.size.y <= 0:
			continue
		if not pages.has(glyph.page):
			# A copy: the image belongs to the font cache and must keep its format.
			var page: Image = server.font_get_texture_image(rid, font_size, glyph.page).duplicate()
			page.convert(Image.FORMAT_RGBA8)
			pages[glyph.page] = page
		atlas.blit_rect(pages[glyph.page], rect, glyph.place)

	var font_name := "%s %d" % [_resource_name(font, "default font"), size]
	var texture := _bake_image(node, atlas, font_name + " atlas", FILTER_LINEAR)

	var out := _new_buffer()
	out.put_u32(texture)
	out.put_float(font.get_height(size))
	out.put_float(font.get_ascent(size))
	out.put_u32(glyphs.size())
	for glyph in glyphs:
		var rect: Rect2i = glyph.rect
		var visible: bool = glyph.page >= 0
		out.put_u32(glyph.codepoint)
		out.put_u16(glyph.place.x)
		out.put_u16(glyph.place.y)
		out.put_u16(rect.size.x if visible else 0)
		out.put_u16(rect.size.y if visible else 0)
		out.put_16(roundi(glyph.offset.x))
		out.put_16(roundi(glyph.offset.y))
		out.put_float(glyph.advance)
	var asset := _add_asset(ASSET_FONT, font_name, out.data_array)
	_asset_cache[key] = asset
	return asset


## Assigns atlas positions row by row. Returns false if the atlas is too small.
func _place_glyphs(glyphs: Array[Dictionary], atlas_size: Vector2i) -> bool:
	var pen := Vector2i(1, 1)
	var row_height := 0
	for glyph in glyphs:
		var size: Vector2i = glyph.rect.size
		if glyph.page < 0 or size.x <= 0:
			continue
		if pen.x + size.x + 1 > atlas_size.x:
			pen = Vector2i(1, pen.y + row_height + 1)
			row_height = 0
		if pen.x + size.x + 1 > atlas_size.x or pen.y + size.y + 1 > atlas_size.y:
			return false
		glyph.place = pen
		pen.x += size.x + 1
		row_height = maxi(row_height, size.y)
	return true


func _sound_asset(node: Node, stream: AudioStream) -> int:
	if stream == null:
		_fail(node, "has no stream")
		return 0
	if _asset_cache.has(stream):
		return _asset_cache[stream]
	var wav := stream as AudioStreamWAV
	if wav == null:
		_fail(node, "stream type %s is not supported, use a WAV file" % stream.get_class())
		return 0
	var bytes := wav.data
	var samples := PackedInt32Array()
	match wav.format:
		AudioStreamWAV.FORMAT_16_BITS:
			samples.resize(bytes.size() / 2)
			for i in samples.size():
				samples[i] = bytes.decode_s16(i * 2)
		AudioStreamWAV.FORMAT_8_BITS:
			samples.resize(bytes.size())
			for i in samples.size():
				samples[i] = bytes.decode_s8(i) * 256
		_:
			_fail(node, "the WAV is imported with compression; set Compress Mode to PCM (Uncompressed) in the import dock")
			return 0
	var asset := _add_asset(ASSET_SOUND, _resource_name(stream, _node_path(node)),
			_baker.bake_sound(samples, wav.mix_rate, 2 if wav.stereo else 1))
	_asset_cache[stream] = asset
	return asset


# --- skeletons ----------------------------------------------------------------------------

## The runtime has no skinning. A skeleton whose meshes are skinned rigidly (every vertex
## belongs to exactly one bone, as in most low-poly characters) is turned into plain
## nodes instead: one Node3D per bone, and the mesh cut into one piece per bone that
## hangs below it. Bone tracks of an AnimationPlayer then move those nodes, which costs
## the console nothing per vertex.
##
## This rewrites the tree that is being exported, which is an instance made for the
## export, never the scene file.
## A MultiMeshInstance3D is cut into pieces this many units wide, a GridMap into pieces of
## this many cells (the size of Godot's own octants), so that the runtime can leave out
## the pieces that are not in view and test collisions only near a body.
const MULTIMESH_PIECE := 16.0
const GRIDMAP_PIECE := 8

## Puts `stand_in` where `node` was: under its name, with its transform and its children.
func _swap_node(node: Node3D, stand_in: Node3D) -> void:
	var parent := node.get_parent()
	var index := node.get_index()
	var node_name := node.name
	stand_in.transform = node.transform
	stand_in.visible = node.visible
	for child in node.get_children():
		node.remove_child(child)
		stand_in.add_child(child)
	parent.remove_child(node)
	node.queue_free()
	stand_in.name = node_name
	parent.add_child(stand_in)
	parent.move_child(stand_in, index)


## One mesh out of many: every part is a mesh and where it is. Parts of the same mesh
## share their surfaces. The light bake then lights every copy where it stands.
func _merged_mesh(parts: Array, override: Material) -> ArrayMesh:
	var tools := {}  # mesh and surface -> [SurfaceTool, Material]
	var order: Array[String] = []
	for part: Array in parts:
		var mesh: Mesh = part[0]
		for surface in mesh.get_surface_count():
			var key := "%d:%d" % [mesh.get_instance_id(), surface]
			if not tools.has(key):
				tools[key] = [SurfaceTool.new(), override if override != null else mesh.surface_get_material(surface)]
				order.append(key)
			(tools[key][0] as SurfaceTool).append_from(mesh, surface, part[1])
	var merged := ArrayMesh.new()
	for key in order:
		(tools[key][0] as SurfaceTool).commit(merged)
		merged.surface_set_material(merged.get_surface_count() - 1, tools[key][1])
	return merged


## The runtime has no MultiMeshInstance3D and no GridMap. Both are turned into what they
## amount to: plain nodes with the copies of their meshes joined into one mesh per piece,
## and for a GridMap a StaticBody3D per piece with the collision shapes of its cells.
func _expand_instanced(root: Node) -> void:
	for multi: MultiMeshInstance3D in root.find_children("*", "MultiMeshInstance3D", true, false):
		var multimesh := multi.multimesh
		if multimesh == null or multimesh.mesh == null:
			_fail(multi, "has no MultiMesh with a mesh")
			continue
		if multimesh.transform_format != MultiMesh.TRANSFORM_3D:
			_fail(multi, "only a MultiMesh with 3D transforms is supported")
			continue
		if multimesh.use_colors or multimesh.use_custom_data:
			_fail(multi, "instance colours and custom data are not supported")
		var count := multimesh.visible_instance_count if multimesh.visible_instance_count >= 0 \
				else multimesh.instance_count
		var pieces := {}  # Vector3i -> parts
		# The transforms are read from the buffer: that is what a scene file holds, and a
		# Godot without a screen keeps nothing else of a MultiMesh. Twelve numbers each,
		# the rows of a 3 x 4 matrix.
		var buffer := multimesh.buffer
		count = mini(count, buffer.size() / 12)
		for i in count:
			var b := i * 12
			var xform := Transform3D(Vector3(buffer[b], buffer[b + 4], buffer[b + 8]),
					Vector3(buffer[b + 1], buffer[b + 5], buffer[b + 9]),
					Vector3(buffer[b + 2], buffer[b + 6], buffer[b + 10]),
					Vector3(buffer[b + 3], buffer[b + 7], buffer[b + 11]))
			var key := Vector3i((xform.origin / MULTIMESH_PIECE).floor())
			if not pieces.has(key):
				pieces[key] = []
			pieces[key].append([multimesh.mesh, xform])
		var stand_in := Node3D.new()
		var keys := pieces.keys()
		keys.sort()
		for key: Vector3i in keys:
			var piece := MeshInstance3D.new()
			piece.name = "Piece_%d_%d_%d" % [key.x, key.y, key.z]
			piece.mesh = _merged_mesh(pieces[key], multi.material_override)
			piece.visibility_range_begin = multi.visibility_range_begin
			piece.visibility_range_end = multi.visibility_range_end
			stand_in.add_child(piece)
		_swap_node(multi, stand_in)

	for grid: GridMap in root.find_children("*", "GridMap", true, false):
		var library := grid.mesh_library
		if library == null:
			_fail(grid, "has no MeshLibrary")
			continue
		if grid.cell_scale != 1.0:
			_fail(grid, "cell_scale is not supported")
		var pieces := {}  # Vector3i -> {"meshes": parts, "shapes": [[Shape3D, Transform3D]]}
		for cell: Vector3i in grid.get_used_cells():
			var item := grid.get_cell_item(cell)
			var xform := Transform3D(grid.get_cell_item_basis(cell), grid.map_to_local(cell))
			var key := Vector3i((Vector3(cell) / GRIDMAP_PIECE).floor())
			if not pieces.has(key):
				pieces[key] = {"meshes": [], "shapes": []}
			var mesh := library.get_item_mesh(item)
			if mesh != null:
				pieces[key].meshes.append([mesh, xform * library.get_item_mesh_transform(item)])
			# The shapes of an item come as a list of shape, transform, shape, transform...
			var shapes := library.get_item_shapes(item)
			for i in range(0, shapes.size(), 2):
				pieces[key].shapes.append([shapes[i], xform * (shapes[i + 1] as Transform3D)])
		var stand_in := Node3D.new()
		var keys := pieces.keys()
		keys.sort()
		for key: Vector3i in keys:
			var tail := "_%d_%d_%d" % [key.x, key.y, key.z]
			if not pieces[key].meshes.is_empty():
				var cells := MeshInstance3D.new()
				cells.name = "Cells" + tail
				cells.mesh = _merged_mesh(pieces[key].meshes, null)
				stand_in.add_child(cells)
			if not pieces[key].shapes.is_empty():
				var solid := StaticBody3D.new()
				solid.name = "Solid" + tail
				solid.collision_layer = grid.collision_layer
				solid.collision_mask = grid.collision_mask
				solid.physics_material_override = grid.physics_material
				stand_in.add_child(solid)
				for i in pieces[key].shapes.size():
					var shape := CollisionShape3D.new()
					shape.name = "Shape%d" % i
					shape.shape = pieces[key].shapes[i][0]
					shape.transform = pieces[key].shapes[i][1]
					solid.add_child(shape)
		_swap_node(grid, stand_in)


func _expand_skeletons(root: Node) -> void:
	for skeleton: Skeleton3D in root.find_children("*", "Skeleton3D", true, false):
		var stand_in := Node3D.new()
		var bone_nodes: Array[Node3D] = []
		var by_name := {}
		for bone in skeleton.get_bone_count():
			var bone_node := Node3D.new()
			bone_node.name = skeleton.get_bone_name(bone)
			bone_node.transform = skeleton.get_bone_pose(bone)
			bone_nodes.append(bone_node)
			by_name[skeleton.get_bone_name(bone)] = bone_node
		for bone in skeleton.get_bone_count():
			var parent_bone := skeleton.get_bone_parent(bone)
			(stand_in if parent_bone < 0 else bone_nodes[parent_bone]).add_child(bone_nodes[bone])

		# The meshes this skeleton deforms, wherever they sit in the tree.
		for instance: MeshInstance3D in root.find_children("*", "MeshInstance3D", true, false):
			if instance.skin == null or instance.get_node_or_null(instance.skeleton) != skeleton:
				continue
			if not _skin_is_rigid(instance):
				# Vertices between bones: the mesh stays whole, and the runtime moves its
				# vertices with the bones every frame.
				var bones: Array[Node3D] = []
				var poses: Array[Transform3D] = []
				for bind in instance.skin.get_bind_count():
					var bone := instance.skin.get_bind_bone(bind)
					if bone < 0:
						bone = skeleton.find_bone(instance.skin.get_bind_name(bind))
					if bone < 0:
						_fail(instance, "the skin refers to a bone that the skeleton does not have")
						break
					bones.append(bone_nodes[bone])
					poses.append(instance.skin.get_bind_pose(bind))
				if bones.size() > 256:
					_fail(instance, "the skin has %d bones, the limit is 256" % bones.size())
				instance.set_meta(SKIN_META, {"bones": bones, "poses": poses})
			elif _split_skinned_mesh(instance, skeleton, bone_nodes):
				instance.get_parent().remove_child(instance)
				instance.queue_free()

		# Put the stand-in where the skeleton was, under its name, with its other children.
		var parent := skeleton.get_parent()
		var index := skeleton.get_index()
		var skeleton_name := skeleton.name
		stand_in.transform = skeleton.transform
		stand_in.visible = skeleton.visible
		stand_in.set_meta(BONES_META, by_name)
		for child in skeleton.get_children():
			skeleton.remove_child(child)
			stand_in.add_child(child)
		parent.remove_child(skeleton)
		skeleton.queue_free()
		stand_in.name = skeleton_name
		parent.add_child(stand_in)
		parent.move_child(stand_in, index)


## True if every vertex of a skinned mesh belongs to one bone alone (as in most low-poly
## characters). Such a mesh is cut into one piece per bone, which costs nothing while the
## game runs.
func _skin_is_rigid(instance: MeshInstance3D) -> bool:
	var mesh := instance.mesh as ArrayMesh
	if mesh == null:
		return true  # _split_skinned_mesh says what is wrong with it
	for s in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(s)
		var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var weights = arrays[Mesh.ARRAY_WEIGHTS]
		if weights == null or positions.is_empty():
			return true
		var per_vertex: int = weights.size() / positions.size()
		for v in positions.size():
			var most := 0.0
			for k in per_vertex:
				most = maxf(most, weights[v * per_vertex + k])
			if most < 0.99:
				return false
	return true


## The bones a vertex follows, as the runtime wants them: the four with the most weight,
## their indices and their weights as bytes that add up to 255.
func _influence(bones, weights, vertex: int, vertex_count: int) -> PackedByteArray:
	var per_vertex: int = bones.size() / vertex_count
	var picked: Array = []  # [weight, bone]
	for k in per_vertex:
		var weight: float = weights[vertex * per_vertex + k]
		if weight > 0.0:
			picked.append([weight, bones[vertex * per_vertex + k]])
	picked.sort_custom(func(a: Array, b: Array) -> bool: return a[0] > b[0])
	picked.resize(mini(picked.size(), 4))
	var total := 0.0
	for entry: Array in picked:
		total += entry[0]
	var out := PackedByteArray([0, 0, 0, 0, 0, 0, 0, 0])
	var left := 255
	for k in picked.size():
		# The last one takes what is left, so the bytes add up exactly.
		var share := left if k == picked.size() - 1 else mini(left, roundi(picked[k][0] / total * 255.0))
		out[k] = picked[k][1]
		out[4 + k] = share
		left -= share
	if picked.is_empty():
		out[4] = 255
	return out


## The skin asset of a mesh that bones deform: the nodes of its bones, the bind poses and
## the influences of every vertex of the baked mesh.
func _skin_asset(instance: MeshInstance3D, influences: PackedByteArray) -> int:
	var skin: Dictionary = instance.get_meta(SKIN_META)
	var bones: Array = skin.bones
	var data := _new_buffer()
	var bones_off := 20
	var binds_off := bones_off + _align4(bones.size() * 2)
	var influences_off := binds_off + bones.size() * 48
	data.put_u32(bones.size())
	data.put_u32(influences.size() / 8)
	data.put_u32(bones_off)
	data.put_u32(binds_off)
	data.put_u32(influences_off)
	for bone: Node3D in bones:
		data.put_u16(_node_index[bone])
	_pad4(data)
	for pose: Transform3D in skin.poses:
		_put_transform(data, pose)
	data.put_data(influences)
	return _add_asset(ASSET_SKIN, _node_path(instance) + " skin", data.data_array)


## Cuts a skinned mesh into one mesh per bone and adds them below the bone nodes.
## Returns false, with an error, if the mesh cannot be cut.
func _split_skinned_mesh(instance: MeshInstance3D, skeleton: Skeleton3D, bone_nodes: Array[Node3D]) -> bool:
	var mesh := instance.mesh as ArrayMesh
	var skin := instance.skin
	if mesh == null:
		_fail(instance, "a skinned mesh must be an ArrayMesh")
		return false
	# Bind index (what the vertices store) -> bone index of the skeleton.
	var bind_bones := PackedInt32Array()
	for bind in skin.get_bind_count():
		var bone := skin.get_bind_bone(bind)
		if bone < 0:
			bone = skeleton.find_bone(skin.get_bind_name(bind))
		if bone < 0:
			_fail(instance, "the skin refers to a bone that the skeleton does not have")
			return false
		bind_bones.append(bone)

	for s in mesh.get_surface_count():
		if mesh.surface_get_primitive_type(s) != Mesh.PRIMITIVE_TRIANGLES:
			_fail(instance, "surface %d is not made of triangles" % s)
			return false
		var arrays := mesh.surface_get_arrays(s)
		var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals = arrays[Mesh.ARRAY_NORMAL]
		var uvs = arrays[Mesh.ARRAY_TEX_UV]
		var colors = arrays[Mesh.ARRAY_COLOR]
		var indices = arrays[Mesh.ARRAY_INDEX]
		var bones = arrays[Mesh.ARRAY_BONES]
		var weights = arrays[Mesh.ARRAY_WEIGHTS]
		if bones == null or weights == null or positions.is_empty():
			_fail(instance, "surface %d has no bone weights" % s)
			return false
		var per_vertex: int = bones.size() / positions.size()

		# The one bind every vertex belongs to.
		var vertex_bind := PackedInt32Array()
		vertex_bind.resize(positions.size())
		for v in positions.size():
			var best := 0
			for k in per_vertex:
				if weights[v * per_vertex + k] > weights[v * per_vertex + best]:
					best = k
			if weights[v * per_vertex + best] < 0.99:
				_fail(instance, "surface %d has vertices that are shared between bones; " % s
						+ "only rigid skinning (one bone per vertex) is supported")
				return false
			vertex_bind[v] = bones[v * per_vertex + best]

		# Triangles go to the bone of their first corner; all their corners are moved
		# into that bone's space, so a triangle between two bones stays in one piece.
		var pieces := {}  # bind -> {"vertices", "normals", "uvs", "colors"}
		var corner_count: int = indices.size() if indices != null else positions.size()
		for corner in corner_count - corner_count % 3:
			var first: int = indices[corner - corner % 3] if indices != null else corner - corner % 3
			var v: int = indices[corner] if indices != null else corner
			var bind := vertex_bind[first]
			if not pieces.has(bind):
				pieces[bind] = {"vertices": PackedVector3Array(), "normals": PackedVector3Array(),
						"uvs": PackedVector2Array(), "colors": PackedColorArray()}
			var piece: Dictionary = pieces[bind]
			var to_bone := skin.get_bind_pose(bind)
			piece.vertices.append(to_bone * positions[v])
			if normals != null:
				piece.normals.append((to_bone.basis * normals[v]).normalized())
			if uvs != null:
				piece.uvs.append(uvs[v])
			if colors != null:
				piece.colors.append(colors[v])

		var material := instance.get_active_material(s)
		for bind: int in pieces:
			var piece: Dictionary = pieces[bind]
			var piece_arrays := []
			piece_arrays.resize(Mesh.ARRAY_MAX)
			piece_arrays[Mesh.ARRAY_VERTEX] = piece.vertices
			if normals != null:
				piece_arrays[Mesh.ARRAY_NORMAL] = piece.normals
			if uvs != null:
				piece_arrays[Mesh.ARRAY_TEX_UV] = piece.uvs
			if colors != null:
				piece_arrays[Mesh.ARRAY_COLOR] = piece.colors
			var piece_mesh := ArrayMesh.new()
			piece_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, piece_arrays)
			piece_mesh.surface_set_material(0, material)
			var piece_node := MeshInstance3D.new()
			piece_node.name = "%s_%d" % [instance.name, s] if mesh.get_surface_count() > 1 else String(instance.name)
			piece_node.mesh = piece_mesh
			piece_node.visible = instance.visible
			bone_nodes[bind_bones[bind]].add_child(piece_node)
	return true


# --- animations and particles ---------------------------------------------------------------

## Bakes all clips of an AnimationPlayer into one asset. Tracks are resolved to node
## indices here, so the asset belongs to this player alone.
##
## Supported tracks: 3D position, rotation and scale of a Node3D, and the `visible`
## property of any node. Everything else is an error.
func _animations_asset(player: AnimationPlayer) -> int:
	if player.speed_scale < 0.0:
		_fail(player, "a negative speed_scale (playing backwards) is not supported")
	var base := player.get_node_or_null(player.root_node)
	if base == null:
		_fail(player, "root_node does not point to a node")
		return 0
	# platforms.gd switches autoplay off while the scene is in the tree and notes it here.
	var autoplay := String(player.get_meta(&"retro_autoplay", player.autoplay))

	var clip_names: Array[String] = []
	for clip_name in player.get_animation_list():
		# RESET is the editor's own bookkeeping, not something to play.
		if String(clip_name) != "RESET":
			clip_names.append(String(clip_name))
	clip_names.sort()
	if clip_names.is_empty():
		_fail(player, "has no animations")
		return 0
	for from_clip in clip_names:
		for to_clip in clip_names:
			if player.get_blend_time(from_clip, to_clip) != 0.0:
				_fail(player, "a blend time of its own from '%s' to '%s' is not supported; " % [from_clip, to_clip]
						+ "the player's default blend time is")

	var header := _new_buffer()
	var body := _new_buffer()
	var body_start := 8 + clip_names.size() * 20
	header.put_u32(clip_names.size())
	header.put_u32(clip_names.find(autoplay) if clip_names.has(autoplay) else NONE32)
	if autoplay != "" and not clip_names.has(autoplay):
		_fail(player, "autoplay animation '%s' does not exist" % autoplay)

	for clip_name in clip_names:
		var animation := player.get_animation(clip_name)
		var where := "animation '%s': " % clip_name
		if animation.loop_mode == Animation.LOOP_PINGPONG:
			_fail(player, where + "ping-pong looping is not supported")
		if animation.length <= 0.0:
			_fail(player, where + "has no length")
		var looping := animation.loop_mode == Animation.LOOP_LINEAR

		var records: Array = []  # of [node index, kind, linear, key count, offset inside `keys`]
		var keys := _new_buffer()
		for t in animation.get_track_count():
			if not animation.track_is_enabled(t):
				continue
			var path := animation.track_get_path(t)
			var what := where + "track '%s': " % path
			var target := base.get_node_or_null(NodePath(path.get_concatenated_names()))
			if target == null or not _node_index.has(target):
				_fail(player, what + "the node does not exist in this scene")
				continue
			var kind := 0
			match animation.track_get_type(t):
				Animation.TYPE_POSITION_3D:
					kind = TRACK_POSITION
				Animation.TYPE_ROTATION_3D:
					kind = TRACK_ROTATION
				Animation.TYPE_SCALE_3D:
					kind = TRACK_SCALE
				Animation.TYPE_VALUE when path.get_concatenated_subnames() == "visible":
					kind = TRACK_VISIBLE
				Animation.TYPE_VALUE:
					var property := String(path.get_concatenated_subnames())
					if target is Node2D and property == "position":
						kind = TRACK_POSITION_2D
					elif target is Node2D and property == "rotation":
						kind = TRACK_ROTATION_2D
					elif target is Node2D and property == "scale":
						kind = TRACK_SCALE_2D
					elif target is Sprite2D and property == "frame":
						kind = TRACK_FRAME
					elif (target is ProgressBar and property == "value") or (
							(target is PathFollow3D or target is PathFollow2D) and property == "progress"):
						kind = TRACK_VALUE
				Animation.TYPE_METHOD:
					kind = TRACK_METHOD
					if target.get_script() == null:
						_fail(player, what + "the node has no component whose handlers could be called")
						continue
			# A track of a skeleton's bone moves the node that stands for the bone
			# (see _expand_skeletons).
			var bone_name := String(path.get_concatenated_subnames())
			if kind >= TRACK_POSITION and kind <= TRACK_SCALE and bone_name != "":
				var bones: Dictionary = target.get_meta(BONES_META, {})
				if not bones.has(bone_name):
					_fail(player, what + "there is no bone with that name")
					continue
				target = bones[bone_name]
			if kind == 0:
				_fail(player, what + "this cannot be animated (supported: 3D position, rotation and scale; "
						+ "'visible'; position, rotation and scale of 2D nodes; 'frame' of a Sprite2D; "
						+ "'value' of a ProgressBar; 'progress' of a PathFollow; method calls)")
				continue
			if kind >= TRACK_POSITION and kind <= TRACK_SCALE and not (target is Node3D):
				_fail(player, what + "the node is not a Node3D")
				continue
			var key_count := animation.track_get_key_count(t)
			if key_count == 0:
				continue
			var linear := false
			var discrete := kind == TRACK_VISIBLE or kind == TRACK_FRAME or kind == TRACK_METHOD or (
					kind >= TRACK_POSITION_2D and animation.value_track_get_update_mode(t) == Animation.UPDATE_DISCRETE)
			if not discrete:
				match animation.track_get_interpolation_type(t):
					Animation.INTERPOLATION_NEAREST:
						linear = false
					Animation.INTERPOLATION_LINEAR:
						linear = true
					_:
						_fail(player, what + "only linear and nearest interpolation are supported")
			# The runtime does not blend from the last key back to the first one. With keys
			# at both ends of a loop that makes no difference; without them it would.
			if looping and linear and key_count > 1 and (animation.track_get_key_time(t, 0) > 0.001
					or animation.track_get_key_time(t, key_count - 1) < animation.length - 0.001):
				_fail(player, what + "a looping track needs a key at the start and at the end of the animation")

			records.append([_node_index[target], kind, linear, key_count, keys.get_size()])
			for k in key_count:
				var value = animation.track_get_key_value(t, k)
				var numbers := [0.0, 0.0, 0.0, 0.0]
				match kind:
					TRACK_ROTATION:
						numbers = [value.x, value.y, value.z, value.w]
					TRACK_VISIBLE:
						numbers[0] = 1.0 if value else 0.0
					TRACK_POSITION_2D, TRACK_SCALE_2D:
						numbers = [value.x, value.y, 0.0, 0.0]
					TRACK_ROTATION_2D, TRACK_FRAME, TRACK_VALUE:
						numbers[0] = float(value)
					TRACK_METHOD:
						if not animation.method_track_get_params(t, k).is_empty():
							_fail(player, what + "method calls with arguments are not supported")
						# The offset of the name in the string table; small enough to be exact.
						numbers[0] = float(_string(String(animation.method_track_get_name(t, k))))
					_:
						numbers = [value.x, value.y, value.z, 0.0]
				if linear and absf(animation.track_get_key_transition(t, k) - 1.0) > 0.001:
					_fail(player, what + "easing of single keys is not supported")
				keys.put_float(animation.track_get_key_time(t, k))
				for number: float in numbers:
					keys.put_float(number)

		# Clip record, then its tracks and keys in the body.
		var tracks_off := body_start + body.get_size()
		var keys_off := tracks_off + records.size() * 12
		header.put_u32(_string(clip_name))
		header.put_float(animation.length)
		header.put_u8(1 if looping else 0)
		header.put_u8(0)
		header.put_u16(0)
		header.put_u32(records.size())
		header.put_u32(tracks_off)
		for record: Array in records:
			body.put_u16(record[0])
			body.put_u8(record[1])
			body.put_u8(1 if record[2] else 0)
			body.put_u32(record[3])
			body.put_u32(keys_off + record[4])
		body.put_data(keys.data_array)

	header.put_data(body.data_array)
	return _add_asset(ASSET_ANIMATION, _node_path(player), header.data_array)


## Particle systems: a CPUParticles3D whose particles are quads (a QuadMesh), using the
## part of its settings that the runtime simulates. Other settings must stay at their
## defaults, so the editor preview and the console do not drift apart unnoticed.
## CPUParticles2D: the same record as the 3D system, with pixels as its unit.
func _particles_2d_data(node: CPUParticles2D, out: StreamPeerBuffer) -> void:
	if node.amount > MAX_PARTICLES:
		_fail(node, "amount is %d, the limit is %d" % [node.amount, MAX_PARTICLES])
	if node.local_coords:
		_fail(node, "local_coords is not supported; particles live in world space")
	if node.emission_shape != CPUParticles2D.EMISSION_SHAPE_POINT and node.emission_shape != CPUParticles2D.EMISSION_SHAPE_SPHERE:
		_fail(node, "emission shape must be Point or Sphere")
	if node.speed_scale != 1.0 or node.randomness != 0.0 or node.lifetime_randomness != 0.0 \
			or node.preprocess != 0.0 or node.split_scale \
			or node.scale_amount_curve != null or node.color_initial_ramp != null:
		_fail(node, "speed scale, randomness, preprocess, split scale, scale curve "
				+ "and initial colour ramp are not supported")
	for parameter in [CPUParticles2D.PARAM_ANGULAR_VELOCITY, CPUParticles2D.PARAM_ORBIT_VELOCITY,
			CPUParticles2D.PARAM_LINEAR_ACCEL, CPUParticles2D.PARAM_RADIAL_ACCEL,
			CPUParticles2D.PARAM_TANGENTIAL_ACCEL, CPUParticles2D.PARAM_DAMPING, CPUParticles2D.PARAM_ANGLE,
			CPUParticles2D.PARAM_HUE_VARIATION, CPUParticles2D.PARAM_ANIM_SPEED, CPUParticles2D.PARAM_ANIM_OFFSET]:
		if node.get_param_min(parameter) != 0.0 or node.get_param_max(parameter) != 0.0:
			_fail(node, "only velocity and scale may be set; accelerations, damping, angle, "
					+ "angular and orbit velocity, hue variation and animation are not supported")
			break

	# A particle is the texture at its own size, or a square of one pixel without one.
	var size := Vector2.ONE
	var texture := NONE32
	if node.texture != null:
		size = node.texture.get_size()
		texture = _texture_asset(node, node.texture, _canvas_filter(node))
	var color := node.color * node.modulate * node.self_modulate
	var color_end := color
	if node.color_ramp != null:
		if node.color_ramp.get_point_count() > 2:
			_fail(node, "the colour ramp may have two points only (start and end)")
		color_end = color * node.color_ramp.sample(1.0)
		color *= node.color_ramp.sample(0.0)

	var direction := node.direction.normalized() if node.direction.length() > 0.0 else Vector2.RIGHT
	out.put_u32(texture)
	out.put_u32(clampi(node.amount, 1, MAX_PARTICLES))
	out.put_float(maxf(node.lifetime, 0.01))
	out.put_float(clampf(node.explosiveness, 0.0, 1.0))
	out.put_float(direction.x)
	out.put_float(direction.y)
	out.put_float(0.0)
	out.put_float(deg_to_rad(node.spread))
	out.put_float(node.gravity.x)
	out.put_float(node.gravity.y)
	out.put_float(0.0)
	out.put_float(node.get_param_min(CPUParticles2D.PARAM_INITIAL_LINEAR_VELOCITY))
	out.put_float(node.get_param_max(CPUParticles2D.PARAM_INITIAL_LINEAR_VELOCITY))
	out.put_float(size.x)
	out.put_float(size.y)
	out.put_float(node.get_param_min(CPUParticles2D.PARAM_SCALE))
	out.put_float(node.get_param_max(CPUParticles2D.PARAM_SCALE))
	out.put_u32(_rgba(color))
	out.put_u32(_rgba(color_end))
	out.put_float(node.emission_sphere_radius if node.emission_shape == CPUParticles2D.EMISSION_SHAPE_SPHERE else 0.0)
	out.put_u8(1 if node.one_shot else 0)
	out.put_u8(1 if node.emitting else 0)
	out.put_u8(1)
	out.put_u8(0)


## Billboard mode of a Sprite3D or Label3D as the runtime knows it.
func _billboard(node: Node, mode: int) -> int:
	match mode:
		BaseMaterial3D.BILLBOARD_DISABLED:
			return 0
		BaseMaterial3D.BILLBOARD_ENABLED:
			return 1
		BaseMaterial3D.BILLBOARD_FIXED_Y:
			return 2
	_fail(node, "this billboard mode is not supported")
	return 0


## Sprite3D: drawn as one translucent quad, unlit.
func _sprite_3d_data(node: Sprite3D, out: StreamPeerBuffer) -> void:
	var size := node.texture.get_size() if node.texture != null else Vector2.ZERO
	var sheet := Rect2(Vector2.ZERO, size)
	if node.region_enabled:
		sheet = node.region_rect
		if not sheet.has_area() or not Rect2(Vector2.ZERO, size).encloses(sheet):
			_fail(node, "the region must have a size and lie inside the texture")
	var filter := FILTER_NEAREST if node.texture_filter == BaseMaterial3D.TEXTURE_FILTER_NEAREST \
			or node.texture_filter == BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS else FILTER_LINEAR
	out.put_u32(_texture_asset(node, node.texture, filter))
	out.put_u32(_rgba(node.modulate))
	out.put_float(sheet.position.x)
	out.put_float(sheet.position.y)
	out.put_float(sheet.size.x)
	out.put_float(sheet.size.y)
	out.put_float(node.offset.x)
	out.put_float(node.offset.y)
	out.put_float(node.pixel_size)
	out.put_u16(node.hframes)
	out.put_u16(node.vframes)
	out.put_u16(node.frame)
	out.put_u8(1 if node.centered else 0)
	out.put_u8(1 if node.flip_h else 0)
	out.put_u8(1 if node.flip_v else 0)
	out.put_u8(_billboard(node, node.billboard))
	out.put_u8(node.axis)
	out.put_u8(0)


## Label3D: the glyphs as quads in the world. The outline is not drawn.
func _label_3d_data(node: Label3D, out: StreamPeerBuffer) -> void:
	if node.autowrap_mode != TextServer.AUTOWRAP_OFF:
		_fail(node, "autowrap is not supported; break lines by hand")
	if node.horizontal_alignment > HORIZONTAL_ALIGNMENT_RIGHT or node.vertical_alignment > VERTICAL_ALIGNMENT_BOTTOM:
		_fail(node, "alignment Fill is not supported")
	var font := node.font if node.font != null else ThemeDB.fallback_font
	out.put_u32(_string(node.text.to_upper() if node.uppercase else node.text))
	out.put_u32(_font_asset(node, font, node.font_size))
	out.put_u32(_rgba(node.modulate))
	out.put_float(node.pixel_size)
	out.put_float(node.offset.x)
	out.put_float(node.offset.y)
	# Godot centres the text on the node; Left and Top put its other edge there.
	out.put_u8(node.horizontal_alignment)
	out.put_u8(node.vertical_alignment)
	out.put_u8(_billboard(node, node.billboard))
	out.put_u8(0)


func _particles_data(node: CPUParticles3D, out: StreamPeerBuffer) -> void:
	if node.amount > MAX_PARTICLES:
		_fail(node, "amount is %d, the limit is %d" % [node.amount, MAX_PARTICLES])
	if node.local_coords:
		_fail(node, "local_coords is not supported; particles live in world space")
	if node.emission_shape != CPUParticles3D.EMISSION_SHAPE_POINT and node.emission_shape != CPUParticles3D.EMISSION_SHAPE_SPHERE:
		_fail(node, "emission shape must be Point or Sphere")
	if node.speed_scale != 1.0 or node.randomness != 0.0 or node.lifetime_randomness != 0.0 \
			or node.preprocess != 0.0 or node.flatness != 0.0 or node.split_scale \
			or node.scale_amount_curve != null or node.color_initial_ramp != null:
		_fail(node, "speed scale, randomness, preprocess, flatness, split scale, scale curve "
				+ "and initial colour ramp are not supported")
	for parameter in [CPUParticles3D.PARAM_ANGULAR_VELOCITY, CPUParticles3D.PARAM_ORBIT_VELOCITY,
			CPUParticles3D.PARAM_LINEAR_ACCEL, CPUParticles3D.PARAM_RADIAL_ACCEL,
			CPUParticles3D.PARAM_TANGENTIAL_ACCEL, CPUParticles3D.PARAM_DAMPING, CPUParticles3D.PARAM_ANGLE,
			CPUParticles3D.PARAM_HUE_VARIATION, CPUParticles3D.PARAM_ANIM_SPEED, CPUParticles3D.PARAM_ANIM_OFFSET]:
		if node.get_param_min(parameter) != 0.0 or node.get_param_max(parameter) != 0.0:
			_fail(node, "only velocity and scale may be set; accelerations, damping, angle, "
					+ "angular and orbit velocity, hue variation and animation are not supported")
			break

	var quad := node.mesh as QuadMesh
	var size := Vector2.ONE
	var color := node.color
	var texture := NONE32
	if quad == null:
		_fail(node, "the mesh must be a QuadMesh; particles are drawn as quads that face the camera")
	else:
		size = quad.size
		var material := quad.material
		if material is StandardMaterial3D:
			color *= material.albedo_color
			if material.albedo_texture != null:
				texture = _texture_asset(node, material.albedo_texture, _material_filter(material))
		elif material != null:
			_fail(node, "the mesh material must be a StandardMaterial3D")

	# The colour ramp is reduced to its two ends.
	var color_end := color
	if node.color_ramp != null:
		if node.color_ramp.get_point_count() > 2:
			_fail(node, "the colour ramp may have two points only (start and end)")
		color_end = color * node.color_ramp.sample(1.0)
		color *= node.color_ramp.sample(0.0)

	var direction := node.direction.normalized() if node.direction.length() > 0.0 else Vector3.UP
	out.put_u32(texture)
	out.put_u32(clampi(node.amount, 1, MAX_PARTICLES))
	out.put_float(maxf(node.lifetime, 0.01))
	out.put_float(clampf(node.explosiveness, 0.0, 1.0))
	out.put_float(direction.x)
	out.put_float(direction.y)
	out.put_float(direction.z)
	out.put_float(deg_to_rad(node.spread))
	out.put_float(node.gravity.x)
	out.put_float(node.gravity.y)
	out.put_float(node.gravity.z)
	out.put_float(node.get_param_min(CPUParticles3D.PARAM_INITIAL_LINEAR_VELOCITY))
	out.put_float(node.get_param_max(CPUParticles3D.PARAM_INITIAL_LINEAR_VELOCITY))
	out.put_float(size.x)
	out.put_float(size.y)
	out.put_float(node.get_param_min(CPUParticles3D.PARAM_SCALE))
	out.put_float(node.get_param_max(CPUParticles3D.PARAM_SCALE))
	out.put_u32(_rgba(color))
	out.put_u32(_rgba(color_end))
	out.put_float(node.emission_sphere_radius if node.emission_shape == CPUParticles3D.EMISSION_SHAPE_SPHERE else 0.0)
	out.put_u8(1 if node.one_shot else 0)
	out.put_u8(1 if node.emitting else 0)
	out.put_u8(0)
	out.put_u8(0)


# --- meshes ---------------------------------------------------------------------------

## Bakes the mesh of one MeshInstance3D: material colour and lighting go into the vertex
## colours, triangles become strips in the platform's vertex layout. The albedo texture
## of a material becomes the texture of its surface and is multiplied with those colours.
## Lighting depends on the node's orientation, so the bake is done per instance.
## With `influences` (an empty PackedByteArray in an array) the mesh is one that bones
## deform: the bones and weights of its vertices are put there, in the order the vertices
## have in the mesh asset.
func _bake_mesh(instance: MeshInstance3D, influences: Array = []) -> PackedByteArray:
	var mesh := instance.mesh
	if mesh == null:
		_fail(instance, "has no mesh")
		return PackedByteArray()
	if not (mesh is ArrayMesh or mesh is PrimitiveMesh):
		_fail(instance, "mesh type %s is not supported" % mesh.get_class())
		return PackedByteArray()
	var skinned := instance.has_meta(SKIN_META)
	if (instance.skin != null and not skinned) or (mesh is ArrayMesh and mesh.get_blend_shape_count() > 0):
		_fail(instance, "blend shapes are not supported, and a skinned mesh needs its Skeleton3D in the scene")
		return PackedByteArray()

	var world_xform := _world_transform(instance)
	var normal_basis := world_xform.basis.inverse().transposed()
	var surface_count := mesh.get_surface_count()
	# What was baked of each surface: texture, blend mode, strip lengths, vertex data.
	var parts: Array[Array] = []
	# Bounding sphere for the runtime's culling: the centre of the box around all vertices
	# and the distance to the farthest one.
	var all_positions := PackedVector3Array()
	for s in surface_count:
		all_positions.append_array(mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX])
	var center := Vector3.ZERO
	var radius := 0.0
	if not all_positions.is_empty():
		var box := AABB(all_positions[0], Vector3.ZERO)
		for p in all_positions:
			box = box.expand(p)
		center = box.get_center()
		for p in all_positions:
			radius = maxf(radius, center.distance_to(p))

	for s in surface_count:
		if mesh is ArrayMesh and mesh.surface_get_primitive_type(s) != Mesh.PRIMITIVE_TRIANGLES:
			_fail(instance, "surface %d is not made of triangles" % s)
			continue
		var material := instance.get_active_material(s)
		var blend := _blend_mode(instance, s, material)
		var arrays := mesh.surface_get_arrays(s)
		var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals = arrays[Mesh.ARRAY_NORMAL]
		var colors = arrays[Mesh.ARRAY_COLOR]
		var uvs = arrays[Mesh.ARRAY_TEX_UV]
		var indices = arrays[Mesh.ARRAY_INDEX]
		var bone_indices = arrays[Mesh.ARRAY_BONES]
		var bone_weights = arrays[Mesh.ARRAY_WEIGHTS]
		if skinned and (bone_indices == null or bone_weights == null):
			_fail(instance, "surface %d has no bone weights" % s)
			continue

		var albedo := Color.WHITE
		var lit := true
		var use_vertex_color := false
		if material is StandardMaterial3D:
			albedo = material.albedo_color
			lit = material.shading_mode != BaseMaterial3D.SHADING_MODE_UNSHADED
			use_vertex_color = material.vertex_color_use_as_albedo and colors != null
		if lit and normals == null:
			_fail(instance, "surface %d has no normals, which the light bake needs "
					% s + "(or make its material unshaded)")
			continue

		# The texture goes in before the mesh, which the loader relies on. Where the
		# platform pads its textures, the coordinates shrink to the part with the image.
		var texture_asset := NONE32
		var uv_scale := Vector2.ONE
		var padded := false
		if material is StandardMaterial3D and material.albedo_texture != null:
			if uvs == null:
				_fail(instance, "surface %d has a texture but no texture coordinates" % s)
				continue
			var content := Vector2i(material.albedo_texture.get_size())
			var stored: Vector2i = _baker.texture_size(content)
			texture_asset = _texture_asset(instance, material.albedo_texture, _material_filter(material))
			uv_scale = Vector2(content) / Vector2(stored)
			padded = stored != content
		var repeats := false

		# Bake every source vertex once and merge identical results, so the stripifier
		# can walk across vertices that Godot duplicated.
		var baked: Array[PackedByteArray] = []
		var baked_influences: Array[PackedByteArray] = []
		var unique := {}  # baked bytes as hex -> index into `baked`
		var remap := PackedInt32Array()
		remap.resize(positions.size())
		for v in positions.size():
			var color := albedo
			if use_vertex_color:
				color *= colors[v]
			if lit:
				var normal: Vector3 = (normal_basis * normals[v]).normalized()
				var light := _light_at(world_xform * positions[v], normal)
				color.r *= _ambient.r + light.r
				color.g *= _ambient.g + light.g
				color.b *= _ambient.b + light.b
			var uv := Vector2.ZERO
			if texture_asset != NONE32:
				uv = uvs[v]
				if uv.x < -0.001 or uv.x > 1.001 or uv.y < -0.001 or uv.y > 1.001:
					repeats = true
				uv *= uv_scale
			var vertex := _new_buffer()
			_baker.write_vertex(vertex, positions[v], uv, color)
			# Vertices that look the same but follow other bones must stay apart.
			var influence := _influence(bone_indices, bone_weights, v, positions.size()) if skinned else PackedByteArray()
			var key := vertex.data_array.hex_encode() + influence.hex_encode()
			if not unique.has(key):
				unique[key] = baked.size()
				baked.append(vertex.data_array)
				baked_influences.append(influence)
			remap[v] = unique[key]
		if repeats and padded:
			_fail(instance, "surface %d repeats its texture, which needs a power-of-two texture size on platform '%s'"
					% [s, _baker.platform_name()])

		# Godot's front faces are clockwise, the package wants counter-clockwise.
		var triangles := PackedInt32Array()
		var corner_count: int = indices.size() if indices != null else positions.size()
		for t in range(0, corner_count - 2, 3):
			var a := remap[indices[t] if indices != null else t]
			var b := remap[indices[t + 2] if indices != null else t + 2]
			var c := remap[indices[t + 1] if indices != null else t + 1]
			if a != b and b != c and a != c:
				triangles.append_array([a, b, c])
		if triangles.is_empty():
			_fail(instance, "surface %d has no triangles" % s)
			continue

		var strips := _stripify(triangles)
		var lengths := _new_buffer()
		var vertices := _new_buffer()
		var strip_influences := _new_buffer()
		for strip: PackedInt32Array in strips:
			lengths.put_u16(strip.size())
			for index in strip:
				vertices.put_data(baked[index])
				if skinned:
					strip_influences.put_data(baked_influences[index])
		var part := [texture_asset, blend, lengths.data_array, vertices.data_array, strip_influences.data_array]
		# Some model files hold every face twice (Kenney's OBJ files do). A surface that
		# bakes to exactly what another one did would only be drawn over it.
		if not parts.has(part):
			parts.append(part)

	var header := _new_buffer()
	var body := _new_buffer()
	var body_start := 20 + parts.size() * 24
	header.put_u32(parts.size())
	header.put_float(center.x)
	header.put_float(center.y)
	header.put_float(center.z)
	# Rounded up a little, so the 32-bit value never ends up below the real distance.
	header.put_float(radius * 1.001 + 0.0001)
	for part: Array in parts:
		if skinned and not influences.is_empty():
			influences[0] = (influences[0] as PackedByteArray) + (part[4] as PackedByteArray)
		var lengths: PackedByteArray = part[2]
		var vertices: PackedByteArray = part[3]
		var strips_off := body_start + body.get_size()
		body.put_data(lengths)
		_pad4(body)
		var verts_off := body_start + body.get_size()
		body.put_data(vertices)
		header.put_u32(part[0])
		header.put_u8(part[1])
		header.put_u8(0)
		header.put_u16(0)
		header.put_u32(lengths.size() / 2)
		header.put_u32(vertices.size() / 24)
		header.put_u32(strips_off)
		header.put_u32(verts_off)

	header.put_data(body.data_array)
	return header.data_array


## Checks that a material stays inside the shared material model and returns its blend mode.
func _blend_mode(instance: MeshInstance3D, surface: int, material: Material) -> int:
	if material == null:
		return BLEND_OPAQUE
	var where := "surface %d: " % surface
	if not (material is StandardMaterial3D):
		_fail(instance, where + "material type %s is not supported, use StandardMaterial3D"
				% material.get_class())
		return BLEND_OPAQUE
	var standard := material as StandardMaterial3D
	if standard.uv1_scale != Vector3.ONE or standard.uv1_offset != Vector3.ZERO or standard.uv1_triplanar:
		_fail(instance, where + "UV scale, offset and triplanar mapping are not supported")
	for feature in BaseMaterial3D.FEATURE_MAX:
		if standard.get_feature(feature):
			_fail(instance, where + "the material uses a feature the runtime does not have "
					+ "(emission, normal map, rim, clearcoat, ...)")
			break
	match standard.transparency:
		BaseMaterial3D.TRANSPARENCY_DISABLED:
			return BLEND_OPAQUE
		BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR:
			return BLEND_PUNCH_THROUGH
		BaseMaterial3D.TRANSPARENCY_ALPHA:
			return BLEND_TRANSLUCENT
	_fail(instance, where + "transparency mode is not supported (use Disabled, Alpha or Alpha Scissor)")
	return BLEND_OPAQUE


## Turns a triangle list (counter-clockwise, three indices per triangle) into strips.
## Greedy: start at an unused triangle, try its three rotations, keep the longest run.
func _stripify(triangles: PackedInt32Array) -> Array[PackedInt32Array]:
	var triangle_count := triangles.size() / 3
	# Directed edge (from, to) -> position of `from` in `triangles`.
	var edges := {}
	for corner in triangles.size():
		var t := corner - corner % 3
		edges[_edge_key(triangles[corner], triangles[t + (corner + 1) % 3])] = corner

	var used := PackedByteArray()
	used.resize(triangle_count)
	var strips: Array[PackedInt32Array] = []
	for t in triangle_count:
		if used[t]:
			continue
		var best_strip := PackedInt32Array()
		var best_triangles := PackedInt32Array()
		for rotation in 3:
			var strip := PackedInt32Array([
				triangles[t * 3 + rotation],
				triangles[t * 3 + (rotation + 1) % 3],
				triangles[t * 3 + (rotation + 2) % 3],
			])
			var visited := PackedInt32Array([t])
			while strip.size() < 0xFFFF:
				# The next triangle shares the last edge, walked in the opposite
				# direction. Which direction that is flips with every triangle.
				var n := strip.size()
				var key := _edge_key(strip[n - 2], strip[n - 1])
				if n % 2 == 1:
					key = _edge_key(strip[n - 1], strip[n - 2])
				var corner: int = edges.get(key, -1)
				if corner < 0:
					break
				var next := corner / 3
				if used[next] or visited.has(next):
					break
				visited.append(next)
				strip.append(triangles[next * 3 + (corner + 2) % 3])
			if strip.size() > best_strip.size():
				best_strip = strip
				best_triangles = visited
		for index in best_triangles:
			used[index] = 1
		strips.append(best_strip)
	return strips


func _edge_key(from: int, to: int) -> int:
	return (from << 32) | to


# --- low-level writing ------------------------------------------------------------------

func _new_buffer() -> StreamPeerBuffer:
	var buffer := StreamPeerBuffer.new()
	buffer.big_endian = _baker.big_endian()
	return buffer


## Returns the offset of a string in the string table, adding it if needed.
func _string(text: String) -> int:
	if not _string_offsets.has(text):
		_string_offsets[text] = _strings.size()
		_strings.append_array(text.to_utf8_buffer())
		_strings.append(0)
	return _string_offsets[text]


func _put_transform(buffer: StreamPeerBuffer, xform: Transform3D) -> void:
	for column: Vector3 in [xform.basis.x, xform.basis.y, xform.basis.z, xform.origin]:
		buffer.put_float(column.x)
		buffer.put_float(column.y)
		buffer.put_float(column.z)


## Colour as the 0xRRGGBBAA value used in node data.
func _rgba(color: Color) -> int:
	return (color.r8 << 24) | (color.g8 << 16) | (color.b8 << 8) | color.a8


func _align4(value: int) -> int:
	return (value + 3) & ~3


func _pad4(buffer: StreamPeerBuffer) -> void:
	while buffer.get_size() % 4 != 0:
		buffer.put_u8(0)
