extends SceneTree
## Builds scenes/tests/ui_test.tscn: the controls that take input, to check focus, toggle
## buttons, disabled buttons and sliders with a scripted input.
##
##   godot --headless --path . --script res://tools/make_ui_test.gd
##
## From top to bottom: a label that is wrapped, a button with an icon, a disabled button,
## a CheckBox, a CheckButton that starts switched on, an HSlider, a TextureButton and a
## button that enables the disabled one. Next to them a VSlider that is not editable. The
## component (UiTest in runtime/game/test_features.c) prints every signal.

const OUTPUT := "res://scenes/tests/ui_test.tscn"

var _root: Control


func _init() -> void:
	_root = Control.new()
	_root.name = "UiTest"
	_root.size = Vector2(640, 480)
	_root.set_script(load("res://components/tests/ui_test.gd"))

	var back := _place(ColorRect.new(), "Back", Rect2(0, 0, 640, 480)) as ColorRect
	back.color = Color(0.12, 0.13, 0.18)

	var help := _place(Label.new(), "Help", Rect2(40, 16, 300, 90)) as Label
	help.text = "A long line of text that the runtime has to break into several lines by itself."
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

	var plain := _place(Button.new(), "Plain", Rect2(40, 120, 240, 40)) as Button
	plain.text = "Plain"
	plain.icon = load("res://assets/sprites/coin.png")
	plain.pressed.connect(Callable(_root, "_on_plain"), CONNECT_PERSIST)

	var locked := _place(Button.new(), "Locked", Rect2(40, 166, 240, 40)) as Button
	locked.text = "Locked"
	locked.disabled = true
	locked.pressed.connect(Callable(_root, "_on_locked"), CONNECT_PERSIST)
	_root.locked = locked

	var check := _place(CheckBox.new(), "Check", Rect2(40, 212, 240, 36)) as CheckBox
	check.text = "Check"
	check.toggled.connect(Callable(_root, "_on_toggled"), CONNECT_PERSIST)

	var switch := _place(CheckButton.new(), "Switch", Rect2(40, 254, 240, 36)) as CheckButton
	switch.text = "Switch"
	switch.button_pressed = true
	switch.toggled.connect(Callable(_root, "_on_toggled"), CONNECT_PERSIST)

	var volume := _place(HSlider.new(), "Volume", Rect2(40, 300, 240, 24)) as HSlider
	volume.min_value = 0.0
	volume.max_value = 10.0
	volume.step = 1.0
	volume.value = 5.0
	volume.value_changed.connect(Callable(_root, "_on_value_changed"), CONNECT_PERSIST)

	var picture := _place(TextureButton.new(), "Picture", Rect2(40, 336, 64, 64)) as TextureButton
	picture.texture_normal = load("res://assets/sprites/ship.png")
	picture.texture_hover = load("res://assets/sprites/coin.png")
	picture.stretch_mode = TextureButton.STRETCH_SCALE
	picture.pressed.connect(Callable(_root, "_on_picture"), CONNECT_PERSIST)

	var unlock := _place(Button.new(), "Unlock", Rect2(40, 420, 240, 40)) as Button
	unlock.text = "Unlock"
	unlock.pressed.connect(Callable(_root, "_on_unlock"), CONNECT_PERSIST)

	var level := _place(VSlider.new(), "Level", Rect2(340, 120, 24, 200)) as VSlider
	level.min_value = 0.0
	level.max_value = 1.0
	level.step = 0.25
	level.value = 0.5
	level.editable = false

	# A sound to hear the controls work: the slider is the volume of its bus, the box
	# mutes the bus, the switch changes the pitch.
	var engine := AudioStreamPlayer.new()
	engine.name = "Engine"
	_root.add_child(engine)
	engine.owner = _root
	engine.stream = load("res://assets/sounds/engine.wav")
	engine.bus = &"SFX"
	engine.autoplay = true
	_root.engine = engine

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	print("wrote ", OUTPUT) if status == OK else push_error("cannot save %s" % OUTPUT)
	_root.free()
	quit(0 if status == OK else 1)


func _place(control: Control, control_name: String, rect: Rect2) -> Control:
	control.name = control_name
	_root.add_child(control)
	control.owner = _root
	control.position = rect.position
	control.size = rect.size
	return control
