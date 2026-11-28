extends Node
class_name SpelunkyLevel

@export_file("*.tscn") var title_scene: String = "res://scenes/spelunky_title.tscn"
@export_file("*.tscn") var menu_scene: String = "res://scenes/main_menu.tscn"

@export var world_node: Node2D
@export var hud_node: Control
@export var audio_node: Node

@export var slot_16a_scene: PackedScene
@export var slot_16a_scene_max: int = 820
@export var slot_16b_scene: PackedScene
@export var slot_16b_scene_max: int = 128
@export var slot_32a_scene: PackedScene
@export var slot_32a_scene_max: int = 64
@export var slot_32b_scene: PackedScene
@export var slot_32b_scene_max: int = 48
@export var slot_64a_scene: PackedScene
@export var slot_64a_scene_max: int = 24
@export var slot_64b_scene: PackedScene
@export var slot_64b_scene_max: int = 12


func _on_resume_pressed() -> void:
	pass


func _on_restart_pressed() -> void:
	pass


func _on_title_pressed() -> void:
	pass


func _on_main_menu_pressed() -> void:
	pass


func _on_next_level_pressed() -> void:
	pass
