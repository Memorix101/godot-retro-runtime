# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# The game of Neon League: stages, score, HUD, pause and game over (Unity's GameManager
# and StageBehaviour). The other Neon components ask it for effects and sounds: `bursts`
# holds four nodes (Boom, Blast, Explosion, Spawn), each with copies of one effect whose
# particle systems are fired one copy after the other. The AnimationPlayers below `walls`
# play "Move" from stage 4 on; those below `cones` play "Cone_Rising" and "Cone_Fading" in
# stage 5 and later. Actions: nl_pause (pause; restart at the game over), nl_back.
class_name NeonGame
extends Node

@export var player: Node
@export var camera: Node
@export var hud: Node
@export var score_label: Node
@export var lives_label: Node
@export var bombs_label: Node
@export var stage_label: Node
@export var survive_label: Node
@export var time_label: Node
@export var ready_picture: Node
@export var win: Node
@export var panel: Node
@export var game_over: Node
@export var final_score: Node
@export var pause_menu: Node
@export var resume_button: Node
@export var music_slider: Node
@export var sound_slider: Node
@export var rumble_toggle: Node
@export var walls: Node
@export var cones: Node
@export var bursts: Node
@export var enemy_sound: Node
@export var blast_sound: Node
@export var death_sound: Node
@export var enemy: PackedScene
@export var enemy_max: int
@export_file("*.tscn") var restart_scene: String
@export_file("*.tscn") var title_scene: String
@export var background_time: float

func _on_resume_pressed(..._arguments: Array) -> void: pass
func _on_menu_pressed(..._arguments: Array) -> void: pass
func _on_music_value_changed(..._arguments: Array) -> void: pass
func _on_sound_value_changed(..._arguments: Array) -> void: pass
func _on_rumble_toggled(..._arguments: Array) -> void: pass
