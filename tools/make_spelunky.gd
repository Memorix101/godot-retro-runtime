extends SceneTree
## Builds scenes/spelunky/slot_*.tscn, scenes/spelunky_title.tscn, and scenes/spelunky_level.tscn
## for the 640x480 retro runtime.
##
##   godot --headless --path . --script res://tools/make_spelunky.gd

const FONT := "res://assets/airplane/fonts/LiberationSans.ttf"

var _root: Node


func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://scenes/spelunky"))

	_make_slot("res://scenes/spelunky/slot_16a.tscn", "res://assets/spelunky/atlas_16a.png", 32, 32)
	_make_slot("res://scenes/spelunky/slot_16b.tscn", "res://assets/spelunky/atlas_16b.png", 32, 4)
	_make_slot("res://scenes/spelunky/slot_32a.tscn", "res://assets/spelunky/atlas_32a.png", 16, 16)
	_make_slot("res://scenes/spelunky/slot_32b.tscn", "res://assets/spelunky/atlas_32b.png", 16, 16)
	_make_slot("res://scenes/spelunky/slot_64a.tscn", "res://assets/spelunky/atlas_64a.png", 8, 8)
	_make_slot("res://scenes/spelunky/slot_64b.tscn", "res://assets/spelunky/atlas_64b.png", 8, 4)

	_make_title_scene()
	_make_level_scene()

	print("Spelunky scenes built successfully.")
	quit(0)


func _save_scene(root: Node, path: String) -> void:
	var scene := PackedScene.new()
	var err := scene.pack(root)
	if err == OK:
		err = ResourceSaver.save(scene, path)
	root.free()
	if err != OK:
		printerr("Failed to save %s: %s" % [path, error_string(err)])
		quit(1)


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node


func _make_slot(path: String, tex_path: String, hf: int, vf: int) -> void:
	var s := Sprite2D.new()
	s.name = "Slot"
	s.texture = load(tex_path)
	s.hframes = hf
	s.vframes = vf
	s.centered = false
	s.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_save_scene(s, path)


func _add_z_anchors(world: Node2D) -> void:
	# Ensure z-levels 0..7 exist in the exported scene level table so pooled sprites
	# can switch z dynamically at runtime.
	var anchors := _add(world, Node2D.new(), "ZAnchors")
	for z in 8:
		var a := _add(anchors, Node2D.new(), "Z%d" % z) as Node2D
		a.z_index = z
		a.z_as_relative = false


func _label(parent: Node, name_str: String, text: String, rect: Rect2, size: int, color: Color = Color.WHITE, align: HorizontalAlignment = HORIZONTAL_ALIGNMENT_LEFT) -> Label:
	var l := _add(parent, Label.new(), name_str) as Label
	l.position = rect.position
	l.size = rect.size
	l.text = text
	l.horizontal_alignment = align
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.add_theme_font_override("font", load(FONT))
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l


func _button(parent: Node, name_str: String, text: String, rect: Rect2, target: Node, method: String, size: int = 16) -> Button:
	var b := _add(parent, Button.new(), name_str) as Button
	b.position = rect.position
	b.size = rect.size
	b.text = text
	b.add_theme_font_override("font", load(FONT))
	b.add_theme_font_size_override("font_size", size)
	b.pressed.connect(Callable(target, method), CONNECT_PERSIST)
	return b


func _wire_vertical_focus(buttons: Array) -> void:
	for i in buttons.size():
		var b := buttons[i] as Control
		var prev := buttons[(i - 1 + buttons.size()) % buttons.size()] as Control
		var next := buttons[(i + 1) % buttons.size()] as Control
		b.focus_neighbor_top = b.get_path_to(prev)
		b.focus_neighbor_bottom = b.get_path_to(next)
		b.focus_neighbor_left = NodePath(".")
		b.focus_neighbor_right = NodePath(".")


func _wire_horizontal_focus(buttons: Array) -> void:
	for i in buttons.size():
		var b := buttons[i] as Control
		var prev := buttons[(i - 1 + buttons.size()) % buttons.size()] as Control
		var next := buttons[(i + 1) % buttons.size()] as Control
		b.focus_neighbor_left = b.get_path_to(prev)
		b.focus_neighbor_right = b.get_path_to(next)
		b.focus_neighbor_top = NodePath(".")
		b.focus_neighbor_bottom = NodePath(".")


func _panel(parent: Node, name_str: String, rect: Rect2, bg: Color = Color(0.08, 0.06, 0.05, 0.92), border: Color = Color(0.85, 0.68, 0.25, 1.0)) -> Panel:
	var p := _add(parent, Panel.new(), name_str) as Panel
	p.position = rect.position
	p.size = rect.size
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_color = border
	sb.set_border_width_all(2)
	p.add_theme_stylebox_override("panel", sb)
	return p


func _tex_rect(parent: Node, name_str: String, path: String, rect: Rect2) -> TextureRect:
	var tr := _add(parent, TextureRect.new(), name_str) as TextureRect
	tr.position = rect.position
	tr.size = rect.size
	tr.texture = load(path)
	tr.stretch_mode = TextureRect.STRETCH_SCALE
	tr.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	return tr


func _audio(parent: Node, name_str: String, path: String, volume_db: float = 0.0) -> AudioStreamPlayer:
	var a := _add(parent, AudioStreamPlayer.new(), name_str) as AudioStreamPlayer
	a.stream = load(path)
	a.volume_db = volume_db
	return a


func _make_title_scene() -> void:
	_root = Node.new()
	_root.name = "SpelunkyTitle"
	_root.set_script(load("res://components/spelunky_title.gd"))

	var env_node := _add(_root, WorldEnvironment.new(), "Env") as WorldEnvironment
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.04, 0.02, 0.03, 1.0)
	env_node.environment = env

	var world := _add(_root, Node2D.new(), "World") as Node2D
	world.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_add_z_anchors(world)

	var cam := _add(world, Camera2D.new(), "Camera2D") as Camera2D
	cam.anchor_mode = Camera2D.ANCHOR_MODE_FIXED_TOP_LEFT
	cam.zoom = Vector2(2, 2)
	cam.limit_left = 0
	cam.limit_top = 0
	cam.limit_right = 640
	cam.limit_bottom = 240

	# Sky & desert backdrop behind the title cave entrance
	var sky := _add(world, Sprite2D.new(), "BgSky") as Sprite2D
	sky.texture = load("res://assets/spelunky/bg/bg_sky.png")
	sky.centered = false
	sky.position = Vector2(320, 0)
	sky.scale = Vector2(2.0, 2.0)
	sky.z_index = 0
	sky.z_as_relative = false

	var moon := _add(world, Sprite2D.new(), "Moon") as Sprite2D
	moon.texture = load("res://assets/spelunky/large/moon_0.png")
	moon.centered = false
	moon.position = Vector2(380, 12)
	moon.z_index = 0
	moon.z_as_relative = false

	var intro_bg := _add(world, Sprite2D.new(), "IntroBg") as Sprite2D
	intro_bg.texture = load("res://assets/spelunky/large/intro_bg_0.png")
	intro_bg.centered = false
	intro_bg.position = Vector2(240, 128)
	intro_bg.z_index = 0
	intro_bg.z_as_relative = false

	var title_art := _add(world, Node2D.new(), "TitleArt") as Node2D
	title_art.z_index = 6
	title_art.z_as_relative = false
	var logo_bg := _add(title_art, Sprite2D.new(), "LogoBg") as Sprite2D
	logo_bg.texture = load("res://assets/spelunky/large/title_bg_0.png")
	logo_bg.centered = false
	logo_bg.position = Vector2(384, 20)
	var logo := _add(title_art, Sprite2D.new(), "Logo") as Sprite2D
	logo.texture = load("res://assets/spelunky/large/title_0.png")
	logo.centered = false
	logo.position = Vector2(416, 48)
	var copy := _add(title_art, Sprite2D.new(), "Copy") as Sprite2D
	copy.texture = load("res://assets/spelunky/large/copy_0.png")
	copy.centered = false
	copy.position = Vector2(408, 72)

	var layer := _add(_root, CanvasLayer.new(), "HudLayer") as CanvasLayer
	var hud := _add(layer, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)

	_label(hud, "TitleHint", "Z/Space: Jump   X: Whip   Up/Enter: Enter Door   Esc: Menu", Rect2(16, 448, 608, 24), 16, Color(0.95, 0.9, 0.7), HORIZONTAL_ALIGNMENT_CENTER)
	_label(hud, "PromptLabel", "", Rect2(120, 412, 400, 28), 16, Color(1.0, 0.92, 0.35), HORIZONTAL_ALIGNMENT_CENTER)
	_label(hud, "StatusLabel", "CHAR: EXPLORER", Rect2(16, 10, 300, 24), 16, Color(0.9, 0.95, 1.0))

	# Intro overlay
	var intro_panel := _panel(hud, "IntroPanel", Rect2(0, 0, 640, 480), Color(0, 0, 0, 0.92), Color(0, 0, 0, 0))
	_label(intro_panel, "Line1", "", Rect2(40, 160, 560, 32), 20, Color.WHITE, HORIZONTAL_ALIGNMENT_CENTER)
	_label(intro_panel, "Line2", "", Rect2(40, 200, 560, 32), 20, Color.WHITE, HORIZONTAL_ALIGNMENT_CENTER)
	_label(intro_panel, "Line3", "", Rect2(40, 240, 560, 32), 20, Color(1.0, 0.88, 0.45), HORIZONTAL_ALIGNMENT_CENTER)
	_label(intro_panel, "Hint", "Press Jump / Attack / Enter to continue", Rect2(40, 360, 560, 28), 16, Color(0.75, 0.75, 0.8), HORIZONTAL_ALIGNMENT_CENTER)

	# Highscore overlay (kept visible in .tscn so exporter bakes focus neighbors; hidden in ready)
	var hs_panel := _panel(hud, "HighscorePanel", Rect2(110, 70, 420, 340))
	_label(hs_panel, "HSTitle", "HIGH SCORES & STATS", Rect2(20, 14, 380, 32), 20, Color(1.0, 0.86, 0.3), HORIZONTAL_ALIGNMENT_CENTER)
	_label(hs_panel, "HSBody", "", Rect2(28, 56, 364, 210), 16, Color.WHITE, HORIZONTAL_ALIGNMENT_LEFT)
	var b_hs := _button(hs_panel, "BtnCloseScores", "Back", Rect2(140, 282, 140, 40), _root, "_on_close_scores_pressed")
	_wire_vertical_focus([b_hs])

	# Pause / Quick menu overlay
	var pause_panel := _panel(hud, "PausePanel", Rect2(170, 76, 300, 320))
	_label(pause_panel, "PauseTitle", "SPELUNKY", Rect2(20, 14, 260, 32), 20, Color(1.0, 0.86, 0.3), HORIZONTAL_ALIGNMENT_CENTER)
	var b_t0 := _button(pause_panel, "BtnResume", "Resume Cave", Rect2(40, 56, 220, 40), _root, "_on_resume_pressed")
	var b_t1 := _button(pause_panel, "BtnStartRun", "Start Adventure (1-1)", Rect2(40, 104, 220, 40), _root, "_on_start_run_pressed")
	var b_t2 := _button(pause_panel, "BtnTutorial", "Tutorial Cave", Rect2(40, 152, 220, 40), _root, "_on_tutorial_pressed")
	var b_t3 := _button(pause_panel, "BtnChar", "Switch Character", Rect2(40, 200, 220, 40), _root, "_on_toggle_char_pressed")
	var b_t4 := _button(pause_panel, "BtnMainMenu", "Retro Main Menu", Rect2(40, 248, 220, 40), _root, "_on_back_menu_pressed")
	_wire_vertical_focus([b_t0, b_t1, b_t2, b_t3, b_t4])

	var audio := _add(_root, Node.new(), "Audio")
	_audio(audio, "mus_title", "res://assets/spelunky/sounds/title.ogg", -4.0)
	_audio(audio, "sfx_jump", "res://assets/spelunky/sounds/jump.wav")
	_audio(audio, "sfx_whip", "res://assets/spelunky/sounds/whip.wav")
	_audio(audio, "sfx_pickup", "res://assets/spelunky/sounds/pickup.wav")
	_audio(audio, "sfx_ignite", "res://assets/spelunky/sounds/ignite.wav")
	_audio(audio, "sfx_click", "res://assets/spelunky/sounds/click.wav")
	_audio(audio, "sfx_steps", "res://assets/spelunky/sounds/steps.wav", -6.0)
	_audio(audio, "sfx_land", "res://assets/spelunky/sounds/land.wav")
	_audio(audio, "sfx_climb1", "res://assets/spelunky/sounds/climb1.wav")
	_audio(audio, "sfx_climb2", "res://assets/spelunky/sounds/climb2.wav")
	_audio(audio, "sfx_throw", "res://assets/spelunky/sounds/throw.wav")
	_audio(audio, "sfx_bombready", "res://assets/spelunky/sounds/bombready.wav")
	_audio(audio, "sfx_explosion", "res://assets/spelunky/sounds/explosion.wav")

	_root.set("level_scene", "res://scenes/spelunky_level.tscn")
	_root.set("menu_scene", "res://scenes/main_menu.tscn")
	_root.set("world_node", world)
	_root.set("hud_node", hud)
	_root.set("audio_node", audio)
	_root.set("slot_16a_scene", load("res://scenes/spelunky/slot_16a.tscn"))
	_root.set("slot_16a_scene_max", 340)
	_root.set("slot_16b_scene", load("res://scenes/spelunky/slot_16b.tscn"))
	_root.set("slot_16b_scene_max", 40)
	_root.set("slot_32a_scene", load("res://scenes/spelunky/slot_32a.tscn"))
	_root.set("slot_32a_scene_max", 16)
	_root.set("slot_32b_scene", load("res://scenes/spelunky/slot_32b.tscn"))
	_root.set("slot_32b_scene_max", 8)
	_root.set("slot_64b_scene", load("res://scenes/spelunky/slot_64b.tscn"))
	_root.set("slot_64b_scene_max", 8)

	_save_scene(_root, "res://scenes/spelunky_title.tscn")


func _make_area_bg(world: Node2D, name_str: String, tex_path: String, tint: Color) -> void:
	var holder := _add(world, Node2D.new(), name_str) as Node2D
	holder.z_index = 0
	holder.z_as_relative = false
	var tex := load(tex_path) as Texture2D
	var tw := tex.get_width()
	var th := tex.get_height()
	# Tile across 640x544 world space (40x34 tiles of 16px)
	var cols := int(ceil(672.0 / float(tw)))
	var rows := int(ceil(560.0 / float(th)))
	for ry in rows:
		for rx in cols:
			var s := _add(holder, Sprite2D.new(), "T_%d_%d" % [rx, ry]) as Sprite2D
			s.texture = tex
			s.centered = false
			s.position = Vector2(rx * tw, ry * th)
			s.modulate = tint


func _make_level_scene() -> void:
	_root = Node.new()
	_root.name = "SpelunkyLevel"
	_root.set_script(load("res://components/spelunky_level.gd"))

	var env_node := _add(_root, WorldEnvironment.new(), "Env") as WorldEnvironment
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.06, 0.04, 0.03, 1.0)
	env_node.environment = env

	var world := _add(_root, Node2D.new(), "World") as Node2D
	world.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_add_z_anchors(world)

	var cam := _add(world, Camera2D.new(), "Camera2D") as Camera2D
	cam.anchor_mode = Camera2D.ANCHOR_MODE_FIXED_TOP_LEFT
	cam.zoom = Vector2(2, 2)
	cam.limit_left = 0
	cam.limit_top = 0
	cam.limit_right = 2048
	cam.limit_bottom = 2048

	_make_area_bg(world, "BgCave", "res://assets/spelunky/bg/bg_cave_tile.png", Color(0.65, 0.58, 0.52, 1.0))
	_make_area_bg(world, "BgLush", "res://assets/spelunky/bg/bg_trees_tile.png", Color(0.55, 0.72, 0.55, 1.0))
	_make_area_bg(world, "BgIce", "res://assets/spelunky/bg/bg_sky_tile.png", Color(0.75, 0.85, 1.0, 1.0))
	_make_area_bg(world, "BgTemple", "res://assets/spelunky/bg/bg_temple_tile.png", Color(0.68, 0.58, 0.45, 1.0))

	# Dark level dim sprite (32x32 black tile scaled across 672x576 world space at z=7)
	var dark_sprite := _add(world, Sprite2D.new(), "DarkSprite") as Sprite2D
	dark_sprite.texture = load("res://assets/spelunky/bg/background_night.png")
	dark_sprite.centered = false
	dark_sprite.scale = Vector2(21.0, 18.0)
	dark_sprite.z_index = 7
	dark_sprite.z_as_relative = false
	dark_sprite.visible = false

	var layer := _add(_root, CanvasLayer.new(), "HudLayer") as CanvasLayer
	var hud := _add(layer, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)

	# Top HUD bar (640x480 screen coordinates)
	_tex_rect(hud, "HeartIcon", "res://assets/spelunky/hud/heart_0.png", Rect2(16, 12, 24, 24))
	_label(hud, "LifeLabel", "4", Rect2(44, 10, 48, 28), 20)

	_tex_rect(hud, "BombIcon", "res://assets/spelunky/hud/bomb_icon_0.png", Rect2(104, 12, 24, 24))
	_tex_rect(hud, "StickyBombIcon", "res://assets/spelunky/hud/sticky_bomb_icon_0.png", Rect2(104, 12, 24, 24)).visible = false
	_label(hud, "BombLabel", "4", Rect2(132, 10, 48, 28), 20)

	_tex_rect(hud, "RopeIcon", "res://assets/spelunky/hud/rope_icon_0.png", Rect2(192, 12, 24, 24))
	_label(hud, "RopeLabel", "4", Rect2(220, 10, 48, 28), 20)

	_tex_rect(hud, "DollarIcon", "res://assets/spelunky/hud/dollar_sign_0.png", Rect2(280, 12, 24, 24))
	_label(hud, "MoneyLabel", "0", Rect2(308, 10, 120, 28), 20)
	_label(hud, "CollectLabel", "", Rect2(308, 36, 110, 20), 16, Color(1.0, 0.9, 0.2))

	_label(hud, "LevelLabel", "1-1", Rect2(444, 10, 76, 28), 20, Color(1.0, 0.88, 0.5), HORIZONTAL_ALIGNMENT_RIGHT)
	_label(hud, "TimeLabel", "0:00", Rect2(528, 10, 96, 28), 20, Color.WHITE, HORIZONTAL_ALIGNMENT_RIGHT)

	_tex_rect(hud, "HoldIconBox", "res://assets/spelunky/hud/hold_item_icon_0.png", Rect2(16, 44, 28, 28))
	_label(hud, "HeldLabel", "", Rect2(48, 46, 160, 24), 16, Color(0.95, 0.95, 0.8))

	# Passive item icons strip
	var icons := _add(hud, Control.new(), "Icons") as Control
	icons.position = Vector2(16, 78)
	var passive_list := [
		["Udjat", "udjat_eye_icon_0.png"],
		["Ankh", "ankh_icon_0.png"],
		["Crown", "crown_icon_0.png"],
		["Kapala", "kapala_icon_0.png"],
		["Spectacles", "spectacles_icon_0.png"],
		["Gloves", "gloves_icon_0.png"],
		["Mitt", "mitt_icon_0.png"],
		["Spring", "spring_shoes_icon_0.png"],
		["Spike", "spike_shoes_icon_0.png"],
		["Cape", "cape_icon_0.png"],
		["Jetpack", "jetpack_icon_0.png"],
		["Compass", "compass_icon_0.png"],
		["Parachute", "parachute_icon_0.png"],
	]
	for i in passive_list.size():
		var tr := _tex_rect(icons, passive_list[i][0], "res://assets/spelunky/hud/" + passive_list[i][1], Rect2(i * 22, 0, 20, 20))
		tr.visible = false

	# Compass direction label / indicator
	_label(hud, "CompassLabel", "", Rect2(540, 44, 84, 24), 16, Color(1.0, 0.85, 0.3), HORIZONTAL_ALIGNMENT_RIGHT)

	# Bottom messages
	_label(hud, "MsgLine1", "", Rect2(20, 424, 600, 24), 16, Color.WHITE, HORIZONTAL_ALIGNMENT_CENTER)
	_label(hud, "MsgLine2", "", Rect2(20, 446, 600, 24), 16, Color(1.0, 0.9, 0.25), HORIZONTAL_ALIGNMENT_CENTER)

	# Pause panel (kept visible in .tscn so exporter bakes focus neighbors; hidden in ready)
	var pause_panel := _panel(hud, "PausePanel", Rect2(180, 100, 280, 270))
	_label(pause_panel, "PauseTitle", "PAUSED", Rect2(20, 14, 240, 32), 20, Color(1.0, 0.86, 0.3), HORIZONTAL_ALIGNMENT_CENTER)
	var b_l0 := _button(pause_panel, "BtnResume", "Resume", Rect2(40, 56, 200, 40), _root, "_on_resume_pressed")
	var b_l1 := _button(pause_panel, "BtnRestart", "Restart (1-1)", Rect2(40, 106, 200, 40), _root, "_on_restart_pressed")
	var b_l2 := _button(pause_panel, "BtnTitle", "Title Cave", Rect2(40, 156, 200, 40), _root, "_on_title_pressed")
	var b_l3 := _button(pause_panel, "BtnMenu", "Retro Main Menu", Rect2(40, 206, 200, 40), _root, "_on_main_menu_pressed")
	_wire_vertical_focus([b_l0, b_l1, b_l2, b_l3])

	# Level transition / death / victory summary panel
	var sum_panel := _panel(hud, "SummaryPanel", Rect2(100, 66, 440, 348))
	_label(sum_panel, "SumTitle", "LEVEL COMPLETED!", Rect2(20, 16, 400, 36), 20, Color(1.0, 0.88, 0.35), HORIZONTAL_ALIGNMENT_CENTER)
	_label(sum_panel, "SumBody", "", Rect2(28, 60, 384, 210), 16, Color.WHITE, HORIZONTAL_ALIGNMENT_LEFT)
	var b_s0 := _button(sum_panel, "BtnNext", "Continue", Rect2(40, 286, 170, 42), _root, "_on_next_level_pressed")
	var b_s1 := _button(sum_panel, "BtnQuitTitle", "Title Cave", Rect2(230, 286, 170, 42), _root, "_on_title_pressed")
	_wire_horizontal_focus([b_s0, b_s1])

	var audio := _add(_root, Node.new(), "Audio")
	_audio(audio, "mus_cave", "res://assets/spelunky/sounds/cave.ogg", -4.0)
	_audio(audio, "mus_lush", "res://assets/spelunky/sounds/lush.ogg", -4.0)
	_audio(audio, "mus_ice", "res://assets/spelunky/sounds/ice.ogg", -4.0)
	_audio(audio, "mus_temple", "res://assets/spelunky/sounds/temple.ogg", -4.0)
	_audio(audio, "mus_boss", "res://assets/spelunky/sounds/boss.ogg", -4.0)
	_audio(audio, "mus_credits", "res://assets/spelunky/sounds/credits.ogg", -4.0)

	var sfx_names := [
		"jump", "bigjump", "whip", "throw", "pickup", "coin", "gem", "hurt", "die", "hit",
		"explosion", "smallexplode", "break", "land", "climb1", "climb2", "steps", "splash",
		"arrowtrap", "shotgun", "bowpull", "bat", "frog", "ghost", "giantspider", "spiderjump",
		"alien", "laser", "yetiyell", "zombie", "cavemandie", "alert", "chestopen", "kiss",
		"damsel", "jetpack", "teleport", "slam", "boing", "trap", "push", "thud", "pause",
		"click", "letsexplore", "mattockbreak", "psychic", "ignite", "bombready", "blink1",
	]
	for sfx in sfx_names:
		var vol := -6.0 if sfx == "steps" else 0.0
		_audio(audio, "sfx_" + sfx, "res://assets/spelunky/sounds/%s.wav" % sfx, vol)

	_root.set("title_scene", "res://scenes/spelunky_title.tscn")
	_root.set("menu_scene", "res://scenes/main_menu.tscn")
	_root.set("world_node", world)
	_root.set("hud_node", hud)
	_root.set("audio_node", audio)
	_root.set("slot_16a_scene", load("res://scenes/spelunky/slot_16a.tscn"))
	_root.set("slot_16a_scene_max", 540)
	_root.set("slot_16b_scene", load("res://scenes/spelunky/slot_16b.tscn"))
	_root.set("slot_16b_scene_max", 96)
	_root.set("slot_32a_scene", load("res://scenes/spelunky/slot_32a.tscn"))
	_root.set("slot_32a_scene_max", 48)
	_root.set("slot_32b_scene", load("res://scenes/spelunky/slot_32b.tscn"))
	_root.set("slot_32b_scene_max", 32)
	_root.set("slot_64a_scene", load("res://scenes/spelunky/slot_64a.tscn"))
	_root.set("slot_64a_scene_max", 16)
	_root.set("slot_64b_scene", load("res://scenes/spelunky/slot_64b.tscn"))
	_root.set("slot_64b_scene_max", 8)

	_save_scene(_root, "res://scenes/spelunky_level.tscn")
