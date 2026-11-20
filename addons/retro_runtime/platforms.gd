extends RefCounted
## The list of target platforms and the one function both front ends (editor menu and
## command line) use to export a scene file.

const Exporter := preload("exporter.gd")

## Set on AudioStreamPlayer, AnimationPlayer and Timer nodes whose autoplay was switched off for
## the export. Holds `true` for sounds and the name of the clip for animations.
const AUTOPLAY_META := &"retro_autoplay"

## Set on RigidBody nodes, which are frozen for the export: the `freeze` the scene had.
const FROZEN_META := &"retro_frozen"

## Set on Parallax2D nodes: scroll_offset, autoscroll and screen_offset as the scene has them.
const PARALLAX_META := &"retro_parallax"

const BAKERS := {
	"pc": preload("bakers/pc_baker.gd"),
	"dreamcast": preload("bakers/dreamcast_baker.gd"),
}


## Where the scenes of the game are. Scenes in folders below it are parts that the game
## scenes instance.
const SCENES_DIR := "res://scenes"


static func names() -> PackedStringArray:
	return PackedStringArray(BAKERS.keys())


## The scenes of the game: every scene file directly inside SCENES_DIR, sorted.
static func game_scenes() -> PackedStringArray:
	var scenes := PackedStringArray()
	for file in DirAccess.get_files_at(SCENES_DIR):
		if file.get_extension() == "tscn":
			scenes.append(SCENES_DIR.path_join(file))
	scenes.sort()
	return scenes


## The project's main scene (Project Settings > Application > Run) as a res:// path, ""
## if none is set.
static func main_scene() -> String:
	var path: String = ProjectSettings.get_setting("application/run/main_scene", "")
	if path.begins_with("uid://"):
		path = ResourceUID.get_id_path(ResourceUID.text_to_id(path))
	return path


## Loads a scene file and exports it for one platform. Returns the error messages; an
## empty array means the package was written.
##
## The scene is put into an off-screen viewport of the target's screen size below `host`
## (any node inside the tree) for two frames, so Godot lays out the controls exactly as
## it would on the target. This makes the function a coroutine: call it with `await`.
static func export_scene_file(host: Node, scene_path: String, platform: String, out_path: String) -> PackedStringArray:
	if not BAKERS.has(platform):
		return PackedStringArray(["unknown platform '%s' (known: %s)" % [platform, ", ".join(names())]])
	var packed := ResourceLoader.load(scene_path) as PackedScene
	if packed == null:
		return PackedStringArray(["cannot load scene '%s'" % scene_path])
	var root := packed.instantiate()
	if root == null:
		return PackedStringArray(["cannot instantiate scene '%s'" % scene_path])

	# The addon's own editor helpers (HeightTerrain) are scripts on nodes too, but they are
	# no components: what they made is in the scene as plain nodes.
	for node in root.find_children("*", "", true, false) + [root]:
		var script: Script = node.get_script()
		if script != null and script.resource_path.begins_with("res://addons/retro_runtime/"):
			node.set_script(null)
	# The instances of exported PackedScene fields become nodes of the scene.
	var exporter := Exporter.new()
	exporter.prepare(root)

	# Autoplay would start real playback inside the exporting process. Switch it off for
	# the time in the tree and leave a note for the exporter.
	var players: Array[Node] = [root]
	for audio_class in ["AudioStreamPlayer", "AudioStreamPlayer2D", "AudioStreamPlayer3D"]:
		players.append_array(root.find_children("*", audio_class, true, false))
	for player in players:
		if not (player is AudioStreamPlayer or player is AudioStreamPlayer2D or player is AudioStreamPlayer3D):
			continue
		if player.autoplay:
			player.autoplay = false
			player.set_meta(AUTOPLAY_META, true)

	# The same for animations: a clip that plays during the export would move nodes away
	# from the places they have in the scene file.
	for player in root.find_children("*", "AnimationPlayer", true, false) + ([root] if root is AnimationPlayer else []):
		player.set_meta(AUTOPLAY_META, player.autoplay)
		player.autoplay = ""

	# A Timer clears its autostart when it enters the tree, and would run during the export.
	for timer in root.find_children("*", "Timer", true, false) + ([root] if root is Timer else []):
		timer.set_meta(AUTOPLAY_META, timer.autostart)
		timer.autostart = false

	# Godot's physics would move rigid bodies while the scene is in the tree. They are frozen
	# for that time; the note says whether the scene had them frozen itself.
	for body_class in ["RigidBody3D", "RigidBody2D"]:
		for body in root.find_children("*", body_class, true, false):
			body.set_meta(FROZEN_META, body.freeze)
			body.freeze = true

	# A Parallax2D scrolls by itself and takes its screen offset from the camera while it is
	# in the tree. The note keeps what the scene file says.
	for parallax: Parallax2D in root.find_children("*", "Parallax2D", true, false):
		parallax.set_meta(PARALLAX_META, [parallax.scroll_offset, parallax.autoscroll, parallax.screen_offset])
		parallax.autoscroll = Vector2.ZERO

	var baker = BAKERS[platform].new()
	var viewport := SubViewport.new()
	viewport.size = baker.screen_size()
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	host.add_child(viewport)
	viewport.add_child(root)
	var tree := Engine.get_main_loop() as SceneTree
	await tree.process_frame
	await tree.process_frame

	exporter.export_scene(root, baker, out_path)
	viewport.queue_free()
	return exporter.errors
