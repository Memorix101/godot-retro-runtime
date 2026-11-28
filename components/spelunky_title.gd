extends Node
class_name SpelunkyTitle

@export_file("*.tscn") var level_scene: String = "res://scenes/spelunky_level.tscn"
@export_file("*.tscn") var menu_scene: String = "res://scenes/main_menu.tscn"

@export var world_node: Node2D
@export var hud_node: Control
@export var audio_node: Node

@export var slot_16a_scene: PackedScene
@export var slot_16a_scene_max: int = 340
@export var slot_16b_scene: PackedScene
@export var slot_16b_scene_max: int = 40
@export var slot_32a_scene: PackedScene
@export var slot_32a_scene_max: int = 16
@export var slot_32b_scene: PackedScene
@export var slot_32b_scene_max: int = 8
@export var slot_64b_scene: PackedScene
@export var slot_64b_scene_max: int = 8


func _on_start_run_pressed() -> void:
	pass


func _on_tutorial_pressed() -> void:
	pass


func _on_resume_pressed() -> void:
	pass


func _on_toggle_char_pressed() -> void:
	pass


func _on_back_menu_pressed() -> void:
	pass


func _on_close_scores_pressed() -> void:
	pass
