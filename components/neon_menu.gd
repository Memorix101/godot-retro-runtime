# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# The title of Neon League: the pages main menu, options and credits, the volume sliders
# and the rumble switch (kept in the saved game "NEONLEAGUE").
class_name NeonMenu
extends Node

@export var main_menu: Node
@export var options: Node
@export var credits: Node
@export var play_button: Node
@export var options_back: Node
@export var credits_back: Node
@export var music_slider: Node
@export var sound_slider: Node
@export var rumble_toggle: Node
@export_file("*.tscn") var game_scene: String
@export_file("*.tscn") var exit_scene: String
@export var background_time: float

func _on_play_pressed(..._arguments: Array) -> void: pass
func _on_options_pressed(..._arguments: Array) -> void: pass
func _on_credits_pressed(..._arguments: Array) -> void: pass
func _on_back_pressed(..._arguments: Array) -> void: pass
func _on_exit_pressed(..._arguments: Array) -> void: pass
func _on_music_value_changed(..._arguments: Array) -> void: pass
func _on_sound_value_changed(..._arguments: Array) -> void: pass
func _on_rumble_toggled(..._arguments: Array) -> void: pass
