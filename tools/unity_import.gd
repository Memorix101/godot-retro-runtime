extends RefCounted
## Turns what tools/unity/RetroSceneExport.cs wrote out of a Unity project into Godot
## resources and nodes. A tool script that builds a scene uses it (tools/make_airplane.gd):
##
##   var unity := UnityImport.new()
##   if not unity.open("build/airplane_export", "res://assets/airplane"):
##       return
##   unity.prepare_files()            # converts the images, sounds and fonts, imports them
##   var made := unity.build_tree(unity.data.nodes, root, root)
##
## The export is already in Godot's axes (Unity's Z negated), so positions and rotations
## are taken as they are. What a Godot node cannot be is left out with a warning, and game
## scripts are not turned into anything: the scene's own tool attaches its components.
##
## What becomes what:
##   MeshFilter + MeshRenderer   MeshInstance3D, an ArrayMesh with StandardMaterial3Ds
##   Box/Sphere/Capsule collider CollisionShape3D below an Area3D (trigger) or a body
##   Terrain                     HeightTerrain (the addon's helper) with the heights and
##                               the colours of its splat map
##   Camera, Light               Camera3D, DirectionalLight3D / OmniLight3D / SpotLight3D
##   AudioSource                 AudioStreamPlayer (or ...3D if it is spatial)
##   ParticleSystem              CPUParticles3D, roughly
##   Canvas                      CanvasLayer; RectTransforms below it become controls:
##                               TextMeshPro text a Label, an Image a TextureRect,
##                               ColorRect or TextureProgressBar (filled image)
##   tag                         group (the tag in snake_case)
##   inactive GameObject         hidden node
##
## Unity's physics nodes may be scaled, Godot's should not be: the scale of a body or an
## area goes into the sizes of its shapes and the transforms of its children instead.

const HeightTerrainScript := preload("res://addons/retro_runtime/height_terrain.gd")

## Longest side of a converted texture, in pixels; textures are made powers of two.
var max_texture_size := 256
## Sample rate of converted sounds (music is left as it is).
var sound_rate := 22050
## Longest sound in samples. The Dreamcast's sound chip plays at most 65534, and its
## ADPCM data is padded, which adds a few: so some room is left. A longer sound gets a
## lower rate (down to 11025 Hz), and what is still too long is cut off.
var max_sound_samples := 64000
## Unity's UI is laid out for its screen in pixels; Godot's for 640x480. Offsets and font
## sizes are multiplied by this.
var ui_scale := 0.5
## The screen size Unity's UI was laid out for. Where it differs in shape from Godot's
## window, controls that stretch with the screen keep their size instead. Zero: off.
var ui_reference := Vector2.ZERO
## Font sizes are rounded to these, so the runtime bakes few font atlases.
var font_sizes := PackedInt32Array([10, 12, 16, 20, 24, 32])
## Godot classes for nodes with colliders, by the Unity node's name, in place of the
## one picked from the colliders (Area3D for a trigger, else a body).
var body_types := {}

var data: Dictionary
var source := ""  # the export folder, absolute
var target := ""  # res:// folder of the converted files
var warnings := PackedStringArray()

var _meshes := {}
var _materials := {}
var _textures := {}
var _sounds := {}
var _fonts := {}
var _looping_audio := {}  # audio keys that an AudioSource plays in a loop


## Reads scene.json of an export. `export_dir` may be relative to the project folder.
func open(export_dir: String, res_dir: String) -> bool:
	source = export_dir if export_dir.is_absolute_path() else ProjectSettings.globalize_path("res://").path_join(export_dir)
	target = res_dir
	var text := FileAccess.get_file_as_string(source.path_join("scene.json"))
	if text.is_empty():
		push_error("no scene.json in %s: run tools/unity/RetroSceneExport.cs first" % source)
		return false
	var parsed: Variant = JSON.parse_string(text)
	if not (parsed is Dictionary):
		push_error("%s/scene.json is not valid JSON" % source)
		return false
	data = parsed
	for list: Array in _all_trees():
		for node: Dictionary in list:
			for comp: Dictionary in node.components:
				if comp.type == "AudioSource" and comp.get("loop", false) and comp.get("clip") != null:
					_looping_audio[comp.clip] = true
	return true


func _all_trees() -> Array:
	var trees: Array = [data.nodes]
	for key: String in data.prefabs:
		trees.append(data.prefabs[key].nodes)
	return trees


func warn(message: String) -> void:
	warnings.append(message)
	push_warning(message)


# --- Files -------------------------------------------------------------------------------

## Writes the textures, sounds and fonts into the target folder in a form the runtime
## takes, and lets Godot import what is new. Run it before anything is built.
func prepare_files() -> bool:
	var folder := ProjectSettings.globalize_path(target)
	for sub in ["textures", "audio", "fonts", "meshes", "terrain"]:
		DirAccess.make_dir_recursive_absolute(folder.path_join(sub))
	var fresh := 0
	for key: String in data.textures:
		fresh += _prepare_texture(key, data.textures[key], folder)
	for key: String in data.audio:
		fresh += _prepare_audio(key, data.audio[key], folder)
	for key: String in data.fonts:
		var font: Dictionary = data.fonts[key]
		if font.get("file") == null:
			continue
		var out := folder.path_join("fonts").path_join(String(font.file).get_file())
		if not FileAccess.file_exists(out):
			DirAccess.copy_absolute(source.path_join(font.file), out)
			fresh += 1
	if fresh > 0:
		print("unity_import: %d new files, importing them" % fresh)
		var output := []
		var code := OS.execute(OS.get_executable_path(), ["--headless", "--path",
				ProjectSettings.globalize_path("res://"), "--import"], output, true)
		if code != 0:
			push_error("the import of the new files failed:\n%s" % "\n".join(output))
			return false
	return true


func _texture_path(key: String) -> String:
	return target.path_join("textures").path_join(key + ".png")


## A copy of the image, at most max_texture_size and with power-of-two sides.
func _prepare_texture(key: String, info: Dictionary, folder: String) -> int:
	if info.get("file") == null:
		return 0
	var out := folder.path_join("textures").path_join(key + ".png")
	if FileAccess.file_exists(out):
		return 0
	var image := Image.load_from_file(source.path_join(info.file))
	if image == null or image.is_empty():
		warn("cannot read the image %s" % info.file)
		return 0
	if image.is_compressed():
		image.decompress()
	var size := Vector2i(_pow2_at_most(image.get_width(), max_texture_size),
			_pow2_at_most(image.get_height(), max_texture_size))
	if size != image.get_size():
		image.resize(size.x, size.y, Image.INTERPOLATE_LANCZOS)
	image.save_png(out)
	return 1


func _pow2_at_most(value: int, limit: int) -> int:
	var p := 1
	while p * 2 <= value and p * 2 <= limit:
		p *= 2
	return p


func _audio_path(key: String) -> String:
	var info: Dictionary = data.audio[key]
	return target.path_join("audio").path_join(key + (".ogg" if _is_music(info) else ".wav"))


## Long Ogg files are music, which the runtime streams; everything else is a sound in
## memory and must be a WAV.
func _is_music(info: Dictionary) -> bool:
	return String(info.file).get_extension() == "ogg" and float(info.length) > 30.0


func _prepare_audio(key: String, info: Dictionary, folder: String) -> int:
	var music := _is_music(info)
	var out := folder.path_join("audio").path_join(key + (".ogg" if music else ".wav"))
	if FileAccess.file_exists(out):
		return 0
	var loop: bool = _looping_audio.has(key)
	if music:
		DirAccess.copy_absolute(source.path_join(info.file), out)
		_write_import(out, "oggvorbisstr", {"loop": loop})
		return 1
	# A sound: decoded and written as mono 16-bit PCM at sound_rate.
	var path := source.path_join(info.file)
	var stream: AudioStream = null
	if path.get_extension() == "ogg":
		stream = AudioStreamOggVorbis.load_from_file(path)
	else:
		stream = AudioStreamWAV.load_from_file(path)
	if stream == null:
		warn("cannot read the sound %s" % info.file)
		return 0
	var rate := sound_rate
	if stream.get_length() * rate > max_sound_samples:
		rate = maxi(11025, int(max_sound_samples / stream.get_length()))
		warn("the sound %s is long: %d Hz%s" % [info.file, rate,
				", cut to %.1f s" % (float(max_sound_samples) / rate) if stream.get_length() * rate > max_sound_samples else ""])
	var wav := _resampled(stream, rate, max_sound_samples)
	if wav.is_empty():
		warn("cannot decode the sound %s" % info.file)
		return 0
	var file := FileAccess.open(out, FileAccess.WRITE)
	file.store_buffer(wav)
	file.close()
	_write_import(out, "wav", {"compress/mode": 0, "edit/loop_mode": 2 if loop else 1})
	return 1


## The stream played through and averaged down to `rate`, mono, as a WAV file.
func _resampled(stream: AudioStream, rate: int, max_samples: int) -> PackedByteArray:
	var mix_rate := AudioServer.get_mix_rate()
	var playback := stream.instantiate_playback()
	if playback == null:
		return PackedByteArray()
	playback.start(0.0)
	var pcm := StreamPeerBuffer.new()
	var frames_left := int(stream.get_length() * mix_rate)
	var step := mix_rate / rate
	var acc := 0.0
	var held := 0
	var due := step
	var position := 0.0
	while frames_left > 0:
		var chunk: PackedVector2Array = playback.mix_audio(1.0, mini(4096, frames_left))
		if chunk.is_empty():
			break
		frames_left -= chunk.size()
		for frame in chunk:
			acc += (frame.x + frame.y) * 0.5
			held += 1
			position += 1.0
			if position >= due and pcm.get_size() < max_samples * 2:
				pcm.put_16(clampi(roundi(acc / held * 32767.0), -32768, 32767))
				acc = 0.0
				held = 0
				due += step
	playback.stop()
	var bytes := pcm.data_array
	var wav := StreamPeerBuffer.new()
	wav.put_data("RIFF".to_ascii_buffer())
	wav.put_u32(36 + bytes.size())
	wav.put_data("WAVEfmt ".to_ascii_buffer())
	wav.put_u32(16)
	wav.put_u16(1)  # PCM
	wav.put_u16(1)  # mono
	wav.put_u32(rate)
	wav.put_u32(rate * 2)
	wav.put_u16(2)
	wav.put_u16(16)
	wav.put_data("data".to_ascii_buffer())
	wav.put_u32(bytes.size())
	wav.put_data(bytes)
	return wav.data_array


## A minimal .import file, so Godot imports the file with these settings.
func _write_import(path: String, importer: String, params: Dictionary) -> void:
	var text := "[remap]\n\nimporter=\"%s\"\n\n[params]\n\n" % importer
	for key: String in params:
		var value: Variant = params[key]
		text += "%s=%s\n" % [key, ("true" if value else "false") if value is bool else str(value)]
	var file := FileAccess.open(path + ".import", FileAccess.WRITE)
	file.store_string(text)
	file.close()


# --- Resources ---------------------------------------------------------------------------

func texture(key: Variant) -> Texture2D:
	if key == null or not data.textures.has(key):
		return null
	if not _textures.has(key):
		var path := _texture_path(key)
		_textures[key] = load(path) if ResourceLoader.exists(path) else null
		if _textures[key] == null:
			warn("texture %s is missing" % key)
	return _textures[key]


func sound(key: Variant) -> AudioStream:
	if key == null or not data.audio.has(key):
		return null
	if not _sounds.has(key):
		var path := _audio_path(key)
		_sounds[key] = load(path) if ResourceLoader.exists(path) else null
		if _sounds[key] == null:
			warn("sound %s is missing" % key)
	return _sounds[key]


func font(key: Variant) -> Font:
	if key == null or not data.fonts.has(key) or data.fonts[key].get("file") == null:
		return null
	if not _fonts.has(key):
		var path := target.path_join("fonts").path_join(String(data.fonts[key].file).get_file())
		_fonts[key] = load(path) if ResourceLoader.exists(path) else null
	return _fonts[key]


## A StandardMaterial3D that looks like the Unity material, as far as one texture and a
## colour go.
func material(key: Variant) -> Material:
	if key == null or not data.materials.has(key):
		return null
	if _materials.has(key):
		return _materials[key]
	var info: Dictionary = data.materials[key]
	var shader := String(info.shader)
	var m := StandardMaterial3D.new()
	m.resource_name = key
	var colors: Dictionary = info.colors
	for name in ["_Color", "_BaseColor", "_TintColor"]:
		if colors.has(name):
			m.albedo_color = _color(colors[name])
			if name == "_TintColor":
				# Unity's particle shaders double the tint.
				m.albedo_color = Color(m.albedo_color.r * 2.0, m.albedo_color.g * 2.0,
						m.albedo_color.b * 2.0, minf(m.albedo_color.a * 2.0, 1.0))
			break
	var textures: Dictionary = info.textures
	for name in ["_MainTex", "_BaseMap"]:
		if textures.has(name):
			var slot: Dictionary = textures[name]
			m.albedo_texture = texture(slot.texture)
			var scale := Vector2(slot.scale[0], slot.scale[1])
			var offset := Vector2(slot.offset[0], slot.offset[1])
			# The runtime has no UV transform in its materials, so mesh() puts it into the
			# texture coordinates. Unity counts v from the bottom of the image, Godot from
			# the top.
			if scale != Vector2.ONE or offset != Vector2.ZERO:
				m.set_meta(&"uv_transform", Transform2D(Vector2(scale.x, 0), Vector2(0, scale.y),
						Vector2(offset.x, 1.0 - offset.y - scale.y)))
			break
	var floats: Dictionary = info.floats
	var mode := int(floats.get("_Mode", 0))
	var lower := shader.to_lower()
	if mode == 1 or lower.contains("cutout"):
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		m.alpha_scissor_threshold = float(floats.get("_Cutoff", 0.5))
	elif mode >= 2 or lower.contains("transparent") or lower.contains("particle") \
			or lower.contains("sprite") or lower.contains("additive") or lower.contains("alpha") \
			or int(info.render_queue) >= 3000:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if lower.begins_with("unlit") or lower.contains("particle") or lower.contains("sprite") \
			or lower.contains("additive") or lower.begins_with("wfx") or lower.contains("skybox"):
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if lower.contains("particle") or lower.contains("sprite") or lower.begins_with("wfx"):
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	_materials[key] = m
	return m


## The ArrayMesh of an exported mesh, one surface per submesh, with `materials` (keys)
## on the surfaces. Meshes are saved into the target folder, so equal ones are shared.
func mesh(key: Variant, material_keys: Array = []) -> ArrayMesh:
	if key == null or not data.meshes.has(key):
		return null
	var cache_key := "%s|%s" % [key, ",".join(material_keys.map(func(k): return str(k)))]
	if _meshes.has(cache_key):
		return _meshes[cache_key]
	var info: Dictionary = data.meshes[key]
	var file := FileAccess.open(source.path_join(info.file), FileAccess.READ)
	if file == null or file.get_buffer(4).get_string_from_ascii() != "RMSH":
		warn("cannot read the mesh %s" % info.file)
		return null
	file.get_32()  # version
	var count := file.get_32()
	var flags := file.get_32()
	var submeshes := file.get_32()
	var positions := _read_vec3(file, count)
	var normals := _read_vec3(file, count) if flags & 1 else PackedVector3Array()
	var uvs := _read_vec2(file, count) if flags & 2 else PackedVector2Array()
	var uv2s := _read_vec2(file, count) if flags & 4 else PackedVector2Array()
	var colors := PackedColorArray()
	if flags & 8:
		var raw := file.get_buffer(count * 16).to_float32_array()
		colors.resize(count)
		for i in count:
			colors[i] = Color(raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2], raw[i * 4 + 3])
	var result := ArrayMesh.new()
	result.resource_name = key
	for s in submeshes:
		var index_count := file.get_32()
		var indices := file.get_buffer(index_count * 4).to_int32_array()
		if index_count == 0:
			continue
		# Every surface gets the vertices it uses, numbered anew.
		var remap := {}
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		var p := PackedVector3Array()
		var n := PackedVector3Array()
		var t := PackedVector2Array()
		var t2 := PackedVector2Array()
		var c := PackedColorArray()
		var out_indices := PackedInt32Array()
		for index in indices:
			if not remap.has(index):
				remap[index] = p.size()
				p.append(positions[index])
				if not normals.is_empty(): n.append(normals[index])
				if not uvs.is_empty(): t.append(uvs[index])
				if not uv2s.is_empty(): t2.append(uv2s[index])
				if not colors.is_empty(): c.append(colors[index])
			out_indices.append(remap[index])
		var material_key: Variant = material_keys[mini(s, material_keys.size() - 1)] if not material_keys.is_empty() else null
		var mat := material(material_key)
		if mat != null and mat.has_meta(&"uv_transform"):
			# The material's tiling and offset, put into the coordinates.
			var uv: Transform2D = mat.get_meta(&"uv_transform")
			for k in t.size():
				t[k] = uv * t[k]
		arrays[Mesh.ARRAY_VERTEX] = p
		if not n.is_empty(): arrays[Mesh.ARRAY_NORMAL] = n
		if not t.is_empty(): arrays[Mesh.ARRAY_TEX_UV] = t
		if not t2.is_empty(): arrays[Mesh.ARRAY_TEX_UV2] = t2
		if not c.is_empty(): arrays[Mesh.ARRAY_COLOR] = c
		arrays[Mesh.ARRAY_INDEX] = out_indices
		result.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var surface := result.get_surface_count() - 1
		if mat != null:
			if not c.is_empty():
				mat.vertex_color_use_as_albedo = true
			result.surface_set_material(surface, mat)
	var path := target.path_join("meshes").path_join(_file_name(cache_key) + ".res")
	if ResourceSaver.save(result, path) == OK:
		result.take_over_path(path)
	_meshes[cache_key] = result
	return result


func _file_name(text: String) -> String:
	var out := ""
	for ch in text:
		out += ch if (ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z") or (ch >= "0" and ch <= "9") or ch == "_" or ch == "-" else "_"
	if out.length() > 80:
		out = out.substr(0, 60) + "_%08x" % text.hash()
	return out


func _read_vec3(file: FileAccess, count: int) -> PackedVector3Array:
	var raw := file.get_buffer(count * 12).to_float32_array()
	var out := PackedVector3Array()
	out.resize(count)
	for i in count:
		out[i] = Vector3(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2])
	return out


func _read_vec2(file: FileAccess, count: int) -> PackedVector2Array:
	var raw := file.get_buffer(count * 8).to_float32_array()
	var out := PackedVector2Array()
	out.resize(count)
	for i in count:
		out[i] = Vector2(raw[i * 2], raw[i * 2 + 1])
	return out


# --- Nodes -------------------------------------------------------------------------------

## Makes Godot nodes of an exported tree (data.nodes or a prefab's nodes) below `parent`,
## all owned by `owner`. `skip(index, node)` may leave a node and everything below it out.
## Returns the Godot node of every exported node, null where it was left out.
func build_tree(nodes: Array, parent: Node, owner: Node, skip := Callable()) -> Array:
	var made: Array = []
	made.resize(nodes.size())
	var globals: Array = []  # Godot global transform of every node, scale included
	globals.resize(nodes.size())
	var carried: Array = []  # what the children of a node are placed against
	carried.resize(nodes.size())
	var canvas_of: Array = []  # the Unity canvas node a RectTransform belongs to, or -1
	canvas_of.resize(nodes.size())
	for i in nodes.size():
		var n: Dictionary = nodes[i]
		var up := int(n.parent)
		canvas_of[i] = canvas_of[up] if up >= 0 else -1
		if up >= 0 and made[up] == null:
			continue
		if skip.is_valid() and skip.call(i, n):
			continue
		var comps := _components(n)
		if comps.has("Canvas"):
			canvas_of[i] = i
		var node: Node
		var into: Node = made[up] if up >= 0 else parent
		if canvas_of[i] >= 0:
			# The canvas is the screen Unity laid the UI out for.
			var parent_size := ui_reference if ui_reference != Vector2.ZERO else _screen_size() / ui_scale
			var parent_turns := 0
			if up >= 0 and made[up] is Control:
				parent_size = made[up].get_meta(&"unity_size", parent_size)
				parent_turns = made[up].get_meta(&"unity_turns", 0)
			node = _make_control(n, comps, i == canvas_of[i], up >= 0 and up == canvas_of[i], parent_size,
					parent_turns)
			if node == null:
				continue
			# A control inside a CanvasLayer's child list, or below another control.
			if into is CanvasLayer or into is Control:
				pass
			else:
				into = parent
		else:
			var parent_global: Transform3D = globals[up] if up >= 0 else Transform3D.IDENTITY
			var parent_carry: Transform3D = carried[up] if up >= 0 else Transform3D.IDENTITY
			var local := Transform3D(Basis(_quat(n.rotation)).scaled_local(_vec3(n.scale)), _vec3(n.position))
			var global := parent_global * local
			globals[i] = global
			node = _make_3d(n, comps, owner)
			if node == null:
				continue
			# Bodies, areas and terrains keep no scale of their own.
			if node is CollisionObject3D:
				var placed := Transform3D(global.basis.orthonormalized(), global.origin)
				var scale := global.basis.get_scale()
				node.transform = parent_carry.affine_inverse() * placed if up >= 0 else placed
				carried[i] = placed
				_scale_parts(node, scale)
			else:
				node.transform = (parent_carry.affine_inverse() * global) if up >= 0 else global
				carried[i] = global
		node.name = _node_name(n.name)
		into.add_child(node, true)
		_own(node, owner)
		if not n.active:
			node.visible = false
		var tag := String(n.tag)
		if tag != "Untagged" and tag != "":
			node.add_to_group(tag.to_snake_case(), true)
		made[i] = node
	return made


func _own(node: Node, owner: Node) -> void:
	if node != owner:
		node.owner = owner
	for child in node.get_children():
		_own(child, owner)


func _components(n: Dictionary) -> Dictionary:
	var out := {}
	for comp: Dictionary in n.components:
		if not out.has(comp.type):
			out[comp.type] = comp
	return out


## Node names may not have some characters in Godot.
func _node_name(text: String) -> String:
	var out := text
	for ch in [".", ":", "@", "/", "\"", "%"]:
		out = out.replace(ch, "_")
	return out if not out.is_empty() else "Node"


func _make_3d(n: Dictionary, comps: Dictionary, owner: Node) -> Node:
	var node: Node3D
	var shapes: Array = []
	for type in ["BoxCollider", "SphereCollider", "CapsuleCollider", "MeshCollider"]:
		for comp: Dictionary in n.components:
			if comp.type == type and comp.get("collider_enabled", true):
				shapes.append(comp)
	var trigger := shapes.any(func(s): return s.get("trigger", false))
	var has_mesh: bool = comps.has("MeshFilter") and comps.has("MeshRenderer") and comps.MeshRenderer.get("renderer_enabled", true)
	if comps.has("Terrain"):
		node = _make_terrain(comps.Terrain)
		return node
	elif not shapes.is_empty():
		if body_types.has(String(n.name)):
			node = ClassDB.instantiate(body_types[String(n.name)])
		elif trigger:
			node = Area3D.new()
		elif comps.has("Rigidbody") and not comps.Rigidbody.kinematic:
			node = RigidBody3D.new()
			node.mass = comps.Rigidbody.mass
			node.gravity_scale = 1.0 if comps.Rigidbody.gravity else 0.0
		else:
			node = StaticBody3D.new()
		for shape_info: Dictionary in shapes:
			var shape := _make_shape(shape_info)
			if shape != null:
				node.add_child(shape, true)
		if has_mesh:
			var child := _make_mesh_instance(comps)
			child.name = "Mesh"
			node.add_child(child, true)
	elif comps.has("Camera"):
		node = _make_camera(comps.Camera)
	elif comps.has("Light"):
		node = _make_light(comps.Light)
	elif has_mesh or comps.has("SkinnedMeshRenderer"):
		node = _make_mesh_instance(comps)
	elif comps.has("ParticleSystem"):
		node = _make_particles(comps.ParticleSystem)
	else:
		node = Node3D.new()
	if comps.has("AudioSource"):
		for comp: Dictionary in n.components:
			if comp.type == "AudioSource":
				var player := _make_audio(comp, n.active)
				if player != null:
					node.add_child(player, true)
	return node


## Scales the shapes and meshes right below a node that has lost its scale.
func _scale_parts(node: Node3D, scale: Vector3) -> void:
	if scale.is_equal_approx(Vector3.ONE):
		return
	for child in node.get_children():
		if child is CollisionShape3D:
			child.position *= scale
			var shape: Shape3D = child.shape
			if shape is BoxShape3D:
				shape.size *= scale
			elif shape is SphereShape3D:
				shape.radius *= maxf(scale.x, maxf(scale.y, scale.z))
			elif shape is CapsuleShape3D:
				shape.radius *= maxf(scale.x, scale.z)
				shape.height *= scale.y
			elif shape is ConcavePolygonShape3D:
				var faces: PackedVector3Array = shape.get_faces()
				for i in faces.size():
					faces[i] *= scale
				shape.set_faces(faces)
		elif child is Node3D:
			child.transform = Transform3D(Basis.from_scale(scale), Vector3.ZERO) * child.transform


func _make_shape(info: Dictionary) -> CollisionShape3D:
	var node := CollisionShape3D.new()
	node.name = "Shape"
	match String(info.type):
		"BoxCollider":
			var box := BoxShape3D.new()
			box.size = _vec3(info.size).abs().max(Vector3(0.01, 0.01, 0.01))
			node.shape = box
			node.position = _vec3(info.center)
		"SphereCollider":
			var sphere := SphereShape3D.new()
			sphere.radius = info.radius
			node.shape = sphere
			node.position = _vec3(info.center)
		"CapsuleCollider":
			var capsule := CapsuleShape3D.new()
			capsule.radius = info.radius
			capsule.height = maxf(info.height, info.radius * 2.0)
			node.shape = capsule
			node.position = _vec3(info.center)
			if int(info.direction) == 0:
				node.rotation.z = PI * 0.5
			elif int(info.direction) == 2:
				node.rotation.x = PI * 0.5
		"MeshCollider":
			var m := mesh(info.mesh)
			if m == null:
				return null
			if info.get("convex", false):
				node.shape = m.create_convex_shape()
			else:
				node.shape = m.create_trimesh_shape()
	return node


func _make_mesh_instance(comps: Dictionary) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	if comps.has("MeshFilter"):
		node.mesh = mesh(comps.MeshFilter.mesh, comps.get("MeshRenderer", {}).get("materials", []))
	elif comps.has("SkinnedMeshRenderer"):
		node.mesh = mesh(comps.SkinnedMeshRenderer.mesh, comps.SkinnedMeshRenderer.get("materials", []))
	return node


func _make_camera(info: Dictionary) -> Camera3D:
	var cam := Camera3D.new()
	cam.fov = info.fov
	cam.near = info.near
	cam.far = info.far
	if info.orthographic:
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = info.ortho_size * 2.0
	return cam


func _make_light(info: Dictionary) -> Light3D:
	var light: Light3D
	match String(info.light_type):
		"Directional":
			light = DirectionalLight3D.new()
		"Spot":
			light = SpotLight3D.new()
			light.spot_range = info.range
			light.spot_angle = info.spot_angle * 0.5
		_:
			light = OmniLight3D.new()
			light.omni_range = info.range
	light.light_color = _color(info.color)
	light.light_energy = info.intensity
	return light


func _make_audio(info: Dictionary, active: bool) -> Node:
	var stream := sound(info.get("clip"))
	if stream == null:
		return null
	var player: Node
	if float(info.spatial_blend) > 0.5:
		var p3 := AudioStreamPlayer3D.new()
		p3.unit_size = info.min_distance
		p3.max_distance = info.max_distance
		player = p3
	else:
		player = AudioStreamPlayer.new()
	player.name = "AudioSource"
	player.stream = stream
	player.volume_db = linear_to_db(maxf(float(info.volume), 0.0001))
	if not (stream is AudioStreamOggVorbis):
		player.pitch_scale = maxf(float(info.pitch), 0.01)
	player.autoplay = bool(info.play_on_awake) and active
	return player


## A Unity terrain: its corner is at the node's place and it reaches along +X and
## Unity's +Z, which is Godot's -Z. HeightTerrain is centred on its node, so the node is
## moved to the middle afterwards by the caller of build_tree (see place_terrain).
func _make_terrain(info: Dictionary) -> Node3D:
	var key: Variant = info.get("terrain")
	if key == null or not data.terrains.has(key):
		return Node3D.new()
	var t: Dictionary = data.terrains[key]
	var node := StaticBody3D.new()
	node.set_script(HeightTerrainScript)
	node.set_meta(&"unity_terrain", key)
	return node


## Sets up a HeightTerrain node made by build_tree: the heights at `cells` cells, the
## colours of the splat map, one texture over all. The node is moved from Unity's corner
## to the middle of the ground. Returns false if the node is not a terrain.
func setup_terrain(node: Node, cells: int, chunk_cells: int, texture_key: Variant = null,
		texture_size := 20.0) -> bool:
	if node.get_script() != HeightTerrainScript or not node.has_meta(&"unity_terrain"):
		return false
	var t: Dictionary = data.terrains[node.get_meta(&"unity_terrain")]
	var size := _vec3(t.size)
	var res := int(t.resolution)
	var raw := FileAccess.get_file_as_bytes(source.path_join(t.heights_file)).to_float32_array()
	# Averaged down to the points of the cells, so no hill falls between two of them.
	var points := cells + 1
	var heights := Image.create(points, points, false, Image.FORMAT_RF)
	for z in points:
		for x in points:
			var fx := float(x) / cells * (res - 1)
			var fz := float(z) / cells * (res - 1)
			var reach := maxi(1, int(float(res - 1) / cells * 0.5))
			var sum := 0.0
			var taken := 0
			for dz in range(-reach, reach + 1):
				for dx in range(-reach, reach + 1):
					var sx := clampi(roundi(fx) + dx, 0, res - 1)
					var sz := clampi(roundi(fz) + dz, 0, res - 1)
					sum += raw[sz * res + sx]
					taken += 1
			# Unity's terrain edges meet their neighbours: the border points keep their
			# own height, so the pieces close up.
			if x == 0 or z == 0 or x == cells or z == cells:
				sum = raw[clampi(roundi(fz), 0, res - 1) * res + clampi(roundi(fx), 0, res - 1)]
				taken = 1
			heights.set_pixel(x, z, Color(sum / taken, 0, 0))
	var height_texture := ImageTexture.create_from_image(heights)
	var name := _file_name(str(node.get_meta(&"unity_terrain")))
	var path := target.path_join("terrain").path_join(name + "_height.res")
	ResourceSaver.save(height_texture, path)
	height_texture.take_over_path(path)

	node.height_image = height_texture
	node.cells = Vector2i(cells, cells)
	node.cell_size = size.x / cells
	node.base_height = 0.0
	node.height = size.y
	node.chunk_cells = chunk_cells
	node.island = 0.0
	if t.has("color_file"):
		var image := Image.load_from_file(source.path_join(t.color_file))
		if image != null:
			# The PNG's top row is Unity's far side (Godot -Z), as the image wants it.
			image.resize(points, points, Image.INTERPOLATE_BILINEAR)
			var colour_texture := ImageTexture.create_from_image(image)
			var colour_path := target.path_join("terrain").path_join(name + "_color.res")
			ResourceSaver.save(colour_texture, colour_path)
			colour_texture.take_over_path(colour_path)
			node.color_image = colour_texture
	# The splat colours already tell rock from sand.
	node.steep_color = Color(0, 0, 0, 0)
	if texture_key != null:
		node.texture = texture(texture_key)
		node.texture_size = texture_size
	# From the corner to the middle: half the size along +X and along Godot's -Z.
	node.position += node.transform.basis * Vector3(size.x * 0.5, 0.0, -size.z * 0.5)
	node.rebuild()
	return true


func _make_particles(info: Dictionary) -> CPUParticles3D:
	var p := CPUParticles3D.new()
	var lifetime: Array = info.start_lifetime
	var speed: Array = info.start_speed
	var size: Array = info.start_size
	var burst_total := 0
	for burst: Dictionary in info.get("bursts", []):
		burst_total += int(burst.count[1])
	var rate: Array = info.get("rate_over_time", [0, 0])
	var amount := maxi(burst_total, int(ceil(float(rate[1]) * float(lifetime[1]))))
	p.amount = clampi(amount, 1, 64)
	p.lifetime = maxf(float(lifetime[1]), 0.05)
	p.one_shot = not info.looping
	p.explosiveness = 1.0 if burst_total > 0 and float(rate[1]) == 0.0 else 0.0
	p.emitting = info.play_on_awake
	p.initial_velocity_min = speed[0]
	p.initial_velocity_max = speed[1]
	p.scale_amount_min = size[0]
	p.scale_amount_max = size[1]
	p.color = _color(info.start_color)
	p.gravity = Vector3(0, -9.81 * float(info.gravity[1]), 0)
	# Unity emits along the local +Z of a cone, which is Godot's -Z.
	p.direction = Vector3(0, 0, -1)
	match String(info.get("shape", "")):
		"Sphere", "Hemisphere":
			p.spread = 180.0
			p.emission_shape = CPUParticles3D.EMISSION_SHAPE_SPHERE
			p.emission_sphere_radius = maxf(float(info.shape_radius), 0.01)
		"Cone", "ConeVolume":
			p.spread = clampf(float(info.shape_angle), 0.0, 180.0)
		_:
			p.spread = 180.0
	if info.get("color_over_lifetime", false) and info.has("gradient_colors"):
		var ramp := Gradient.new()
		var first: Dictionary = info.gradient_colors[0]
		var last: Dictionary = info.gradient_colors[info.gradient_colors.size() - 1]
		var a0 := float(info.gradient_alphas[0].alpha)
		var a1 := float(info.gradient_alphas[info.gradient_alphas.size() - 1].alpha)
		var c0 := _color(first.color)
		var c1 := _color(last.color)
		ramp.colors = PackedColorArray([Color(c0, a0), Color(c1, a1)])
		p.color_ramp = ramp
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	var mat := material(info.get("material"))
	var spark := StandardMaterial3D.new()
	if mat is StandardMaterial3D:
		spark.albedo_texture = mat.albedo_texture
		spark.albedo_color = mat.albedo_color
	spark.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	spark.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	spark.vertex_color_use_as_albedo = true
	spark.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	quad.material = spark
	p.mesh = quad
	return p


# --- UI ----------------------------------------------------------------------------------

## `parent_size` is the parent's size in Unity's units before it was turned, and
## `parent_turns` how many quarter turns it and its parents make together.
func _make_control(n: Dictionary, comps: Dictionary, is_canvas: bool, on_canvas: bool,
		parent_size: Vector2, parent_turns: int) -> Node:
	if is_canvas:
		var layer := CanvasLayer.new()
		return layer
	var rect: Dictionary = comps.get("RectTransform", {})
	if rect.is_empty():
		return null
	var control: Control
	var amin := Vector2(rect.anchor_min[0], rect.anchor_min[1])
	var amax := Vector2(rect.anchor_max[0], rect.anchor_max[1])
	var omin := Vector2(rect.offset_min[0], rect.offset_min[1])
	var omax := Vector2(rect.offset_max[0], rect.offset_max[1])
	if comps.has("TextMeshProUGUI"):
		var f: Dictionary = comps.TextMeshProUGUI.fields
		control = _make_label(f, rect)
		# TextMeshPro draws inside its rectangle less its margins (left, top, right,
		# bottom), which may also be negative.
		var margin: Variant = f.get("m_margin")
		if margin is Array and margin.size() == 4:
			omin += Vector2(margin[0], margin[3])
			omax -= Vector2(margin[2], margin[1])
	elif comps.has("Image") or comps.has("RawImage"):
		control = _make_image(comps.get("Image", comps.get("RawImage")).fields)
	else:
		control = Control.new()
	control.mouse_filter = Control.MOUSE_FILTER_IGNORE

	# Turned controls. Controls cannot turn in the runtime, but quarter turns can be made
	# by turning the rectangle and the pictures instead. The rectangle is worked out in
	# the parent's own (unturned) frame first, Unity's units, y up.
	var lo := amin * parent_size + omin
	var hi := amax * parent_size + omax
	var own_turns := 0
	if rect.has("unity_euler"):
		var z := float(rect.unity_euler[2])
		own_turns = posmod(roundi(z / 90.0), 4)
		if not is_equal_approx(z, own_turns * 90.0) and not is_equal_approx(z, own_turns * 90.0 - 360.0):
			warn("%s is turned by %.0f degrees; only quarter turns are kept" % [n.name, z])
	var turns := posmod(parent_turns + own_turns, 4)
	control.set_meta(&"unity_size", hi - lo)
	control.set_meta(&"unity_turns", turns)
	if parent_turns != 0 or own_turns != 0:
		# Into the frame the parent is shown in: each quarter turn (anticlockwise, as
		# Unity turns about +Z) takes (x, y) to (height - y, x).
		var size := parent_size
		for k in parent_turns:
			var new_lo := Vector2(size.y - hi.y, lo.x)
			var new_hi := Vector2(size.y - lo.y, hi.x)
			lo = new_lo
			hi = new_hi
			size = Vector2(size.y, size.x)
		if own_turns % 2 == 1:
			# Turned about its own middle (Unity's default pivot).
			var middle := (lo + hi) * 0.5
			var half := Vector2(hi.y - lo.y, hi.x - lo.x) * 0.5
			lo = middle - half
			hi = middle + half
		# The anchors are kept where they are one point; else the bottom left corner.
		if parent_turns != 0 or not amin.is_equal_approx(amax):
			amin = Vector2.ZERO
			amax = Vector2.ZERO
		omin = lo - amin * size
		omax = hi - amin * size
		_turn_pictures(control, turns)

	if on_canvas and ui_reference != Vector2.ZERO:
		# Laid out for another shape of screen: a control stretched over the height (or
		# width) of the screen keeps the size it had there, and one held at the top (or
		# bottom) edge but lying in the other half of the screen goes with that half.
		# Both end up at the nearer edge.
		var screen := _screen_size()
		for axis in 2:
			if is_equal_approx(ui_reference[axis] * ui_scale, screen[axis]):
				continue
			var point := is_equal_approx(amin[axis], amax[axis])
			if point and not (is_equal_approx(amin[axis], 0.0) or is_equal_approx(amin[axis], 1.0)):
				continue  # centred: stays centred
			var a: float = amin[axis] * ui_reference[axis] + omin[axis]
			var b: float = amax[axis] * ui_reference[axis] + omax[axis]
			var edge := 0.0 if (a + b) * 0.5 < ui_reference[axis] * 0.5 else 1.0
			amin[axis] = edge
			amax[axis] = edge
			omin[axis] = a - edge * ui_reference[axis]
			omax[axis] = b - edge * ui_reference[axis]
	_place_control(control, amin, amax, omin * ui_scale, omax * ui_scale)
	return control


## Turns the pictures of a control by quarter turns (anticlockwise) and its fill
## direction with them.
func _turn_pictures(control: Control, turns: int) -> void:
	if turns == 0:
		return
	if control is TextureRect and control.texture != null:
		control.texture = _turned_texture(control.texture, turns)
	elif control is TextureProgressBar:
		if control.texture_progress != null:
			control.texture_progress = _turned_texture(control.texture_progress, turns)
		# Anticlockwise, the top goes to the left, the left to the bottom, and so on.
		var next := {
			TextureProgressBar.FILL_TOP_TO_BOTTOM: TextureProgressBar.FILL_LEFT_TO_RIGHT,
			TextureProgressBar.FILL_LEFT_TO_RIGHT: TextureProgressBar.FILL_BOTTOM_TO_TOP,
			TextureProgressBar.FILL_BOTTOM_TO_TOP: TextureProgressBar.FILL_RIGHT_TO_LEFT,
			TextureProgressBar.FILL_RIGHT_TO_LEFT: TextureProgressBar.FILL_TOP_TO_BOTTOM,
		}
		for k in turns:
			control.fill_mode = next.get(control.fill_mode, control.fill_mode)


func _turned_texture(texture: Texture2D, turns: int) -> Texture2D:
	var image := texture.get_image()
	if image == null:
		return texture
	if image.is_compressed():
		image.decompress()
	for k in turns:
		image.rotate_90(COUNTERCLOCKWISE)
	var turned := ImageTexture.create_from_image(image)
	var path := target.path_join("textures").path_join("%s_turned%d.res" % [texture.resource_path.get_file().get_basename(), turns])
	ResourceSaver.save(turned, path)
	turned.take_over_path(path)
	return turned


## Unity anchors count from the bottom left, Godot's from the top left.
func _place_control(control: Control, amin: Vector2, amax: Vector2, omin: Vector2, omax: Vector2) -> void:
	control.anchor_left = amin.x
	control.anchor_right = amax.x
	control.anchor_top = 1.0 - amax.y
	control.anchor_bottom = 1.0 - amin.y
	control.offset_left = omin.x
	control.offset_right = omax.x
	control.offset_top = -omax.y
	control.offset_bottom = -omin.y


func _make_label(f: Dictionary, rect: Dictionary) -> Label:
	var label := Label.new()
	# Text wider than its box grows to both sides, as TextMeshPro lets it overflow.
	label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	label.grow_vertical = Control.GROW_DIRECTION_BOTH
	var text := String(f.get("m_text", ""))
	text = RegEx.create_from_string("<[^>]*>").sub(text, "", true).strip_edges()
	var style := int(f.get("m_fontStyle", 0))
	if style & 16:
		text = text.to_upper()
	label.text = text
	var scale_y := absf(float(rect.unity_scale[1])) if rect.has("unity_scale") else 1.0
	var size := float(f.get("m_fontSize", 24)) * ui_scale * (scale_y if scale_y > 0.01 else 1.0)
	label.add_theme_font_size_override("font_size", _font_size(size))
	var asset: Variant = f.get("m_fontAsset")
	if asset is Dictionary and asset.has("font"):
		var ttf := font(asset.font)
		if ttf != null:
			label.add_theme_font_override("font", ttf)
	label.add_theme_color_override("font_color", _color(f.get("m_fontColor", [1, 1, 1, 1])))
	# The old alignment field holds both directions; the new ones are used when it is
	# marked as converted (65535).
	var align := int(f.get("m_textAlignment", 65535))
	var h := int(f.get("m_HorizontalAlignment", 1))
	var v := int(f.get("m_VerticalAlignment", 256))
	if align != 65535:
		h = align & 0xff
		v = align & 0xff00
		if v == 0:
			v = align & 0xf000
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER if h & (2 | 32) else \
			(HORIZONTAL_ALIGNMENT_RIGHT if h & 4 else HORIZONTAL_ALIGNMENT_LEFT)
	label.vertical_alignment = VERTICAL_ALIGNMENT_TOP if v & 256 else \
			(VERTICAL_ALIGNMENT_BOTTOM if v & 1024 else VERTICAL_ALIGNMENT_CENTER)
	return label


func _font_size(size: float) -> int:
	var best := font_sizes[0]
	for s in font_sizes:
		if absf(s - size) < absf(best - size):
			best = s
	return best


func _make_image(f: Dictionary) -> Control:
	var color := _color(f.get("m_Color", [1, 1, 1, 1]))
	var tex: Texture2D = null
	var ref: Variant = f.get("m_Sprite", f.get("m_Texture"))
	if ref is Dictionary and ref.has("texture") and not String(ref.get("asset", "")).begins_with("Resources/unity_builtin"):
		tex = texture(ref.texture)
	if tex == null:
		var rect := ColorRect.new()
		rect.color = color
		return rect
	if int(f.get("m_Type", 0)) == 3:
		var bar := TextureProgressBar.new()
		bar.texture_progress = tex
		bar.tint_progress = color
		var method := int(f.get("m_FillMethod", 0))
		var origin := int(f.get("m_FillOrigin", 0))
		if method == 1:
			bar.fill_mode = TextureProgressBar.FILL_TOP_TO_BOTTOM if origin == 1 else TextureProgressBar.FILL_BOTTOM_TO_TOP
		else:
			bar.fill_mode = TextureProgressBar.FILL_RIGHT_TO_LEFT if origin == 1 else TextureProgressBar.FILL_LEFT_TO_RIGHT
		bar.max_value = 100.0
		bar.value = float(f.get("m_FillAmount", 1.0)) * 100.0
		bar.nine_patch_stretch = false
		return bar
	var image := TextureRect.new()
	image.texture = tex
	image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	image.stretch_mode = TextureRect.STRETCH_SCALE
	image.modulate = color
	return image


# --- Environment -------------------------------------------------------------------------

## A WorldEnvironment from the scene's render settings: the six-sided skybox as a
## panorama, the ambient light, the fog.
func make_environment() -> WorldEnvironment:
	var render: Dictionary = data.render
	var env := Environment.new()
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = _color(render.ambient_sky)
	var sky_key: Variant = render.get("skybox")
	if sky_key != null and data.materials.has(sky_key) and String(data.materials[sky_key].shader) == "Skybox/6 Sided":
		var panorama := _panorama(data.materials[sky_key])
		if panorama != null:
			var sky_material := PanoramaSkyMaterial.new()
			sky_material.panorama = panorama
			var sky := Sky.new()
			sky.sky_material = sky_material
			env.background_mode = Environment.BG_SKY
			env.sky = sky
	if render.fog:
		env.fog_enabled = true
		env.fog_light_color = _color(render.fog_color)
		if String(render.fog_mode) == "Linear":
			env.fog_mode = Environment.FOG_MODE_DEPTH
			env.fog_depth_begin = render.fog_start
			env.fog_depth_end = render.fog_end
		else:
			env.fog_density = render.fog_density
	var world := WorldEnvironment.new()
	world.environment = env
	return world


## Godot's panorama of Unity's six faces, as seen from inside the box.
func _panorama(info: Dictionary) -> Texture2D:
	var faces := {}
	for slot in ["_FrontTex", "_BackTex", "_LeftTex", "_RightTex", "_UpTex", "_DownTex"]:
		if not info.textures.has(slot):
			return null
		var key: String = info.textures[slot].texture
		var image := Image.load_from_file(ProjectSettings.globalize_path(_texture_path(key)))
		if image == null:
			return null
		image.convert(Image.FORMAT_RGB8)
		faces[slot] = image
	# Looking direction, right and up of each face, in Unity's axes.
	var layout := [
		["_FrontTex", Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)],
		["_BackTex", Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0)],
		["_LeftTex", Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)],
		["_RightTex", Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0)],
		["_UpTex", Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1)],
		["_DownTex", Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)],
	]
	var width := 512
	var height := 256
	var out := Image.create(width, height, false, Image.FORMAT_RGB8)
	for y in height:
		for x in width:
			# The inverse of the exporter's lookup: the middle of the picture is -Z.
			var phi := ((x + 0.5) / width - 0.5) * TAU
			var theta := (y + 0.5) / height * PI
			var g := Vector3(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi))
			var d := Vector3(g.x, g.y, -g.z)  # in Unity's axes
			var best: Array = layout[0]
			var best_dot := -2.0
			for face: Array in layout:
				var dot: float = d.dot(face[1])
				if dot > best_dot:
					best_dot = dot
					best = face
			var image: Image = faces[best[0]]
			var s: float = d.dot(best[2]) / best_dot
			var t: float = d.dot(best[3]) / best_dot
			var px := clampi(int((s * 0.5 + 0.5) * image.get_width()), 0, image.get_width() - 1)
			var py := clampi(int((0.5 - t * 0.5) * image.get_height()), 0, image.get_height() - 1)
			out.set_pixel(x, y, image.get_pixel(px, py))
	var texture := ImageTexture.create_from_image(out)
	var path := target.path_join("textures").path_join("sky_panorama.res")
	ResourceSaver.save(texture, path)
	texture.take_over_path(path)
	return texture


# --- Values ------------------------------------------------------------------------------

func _vec3(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])


func _quat(a: Array) -> Quaternion:
	return Quaternion(a[0], a[1], a[2], a[3]).normalized()


func _color(a: Variant) -> Color:
	if a is Array and a.size() >= 3:
		return Color(a[0], a[1], a[2], a[3] if a.size() > 3 else 1.0)
	return Color.WHITE


# --- Level of detail ---------------------------------------------------------------------

## A version of a mesh with fewer triangles, made with Godot's own LOD generator
## (meshoptimizer): about `ratio` of the triangles of every surface, with the materials
## kept. For a far copy that a visibility range switches to.
func make_lod(source_mesh: Mesh, ratio: float) -> ArrayMesh:
	var importer := ImporterMesh.new()
	for s in source_mesh.get_surface_count():
		importer.add_surface(source_mesh.surface_get_primitive_type(s), source_mesh.surface_get_arrays(s),
				[], {}, source_mesh.surface_get_material(s))
	importer.generate_lods(25.0, 60.0, [])
	var out := ArrayMesh.new()
	out.resource_name = source_mesh.resource_name + " far"
	for s in importer.get_surface_count():
		var arrays: Array = importer.get_surface_arrays(s)
		var full: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var want := int(full.size() * ratio)
		var best := full
		for lod in importer.get_surface_lod_count(s):
			var indices := importer.get_surface_lod_indices(s, lod)
			if absi(indices.size() - want) < absi(best.size() - want):
				best = indices
		out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _compact(arrays, best))
		out.surface_set_material(out.get_surface_count() - 1, importer.get_surface_material(s))
	return out


## The arrays of a surface with only the vertices that `indices` use.
func _compact(arrays: Array, indices: PackedInt32Array) -> Array:
	var remap := {}
	var order := PackedInt32Array()
	var out_indices := PackedInt32Array()
	for index in indices:
		if not remap.has(index):
			remap[index] = order.size()
			order.append(index)
		out_indices.append(remap[index])
	var out := []
	out.resize(Mesh.ARRAY_MAX)
	for slot in [Mesh.ARRAY_VERTEX, Mesh.ARRAY_NORMAL, Mesh.ARRAY_TEX_UV, Mesh.ARRAY_TEX_UV2, Mesh.ARRAY_COLOR]:
		var values: Variant = arrays[slot]
		if values == null or values.is_empty():
			continue
		var picked: Variant = values.duplicate()
		picked.resize(order.size())
		for k in order.size():
			picked[k] = values[order[k]]
		out[slot] = picked
	out[Mesh.ARRAY_INDEX] = out_indices
	return out


func _screen_size() -> Vector2:
	return Vector2(ProjectSettings.get_setting("display/window/size/viewport_width", 640),
			ProjectSettings.get_setting("display/window/size/viewport_height", 480))
