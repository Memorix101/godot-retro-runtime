# Declaration only. The logic lives in runtime/game/airplane.c.
#
# The airplane game: the title (until the action "start"), counting what is left to
# collect, the game over screen and the best time, which is kept in a saved game.
# At the start it shows the player, the HUD, the counters and the four lists.
class_name AirplaneGame
extends Node

@export var player: AirplanePlayer
@export var camera: AirplaneCamera
@export var title_ui: CanvasLayer
## Blinks on the title.
@export var press_start: Control
@export var hud: CanvasLayer
@export var collect_ui: CanvasLayer
@export var gates_label: Label
@export var coins_label: Label
@export var targets_label: Label
@export var ships_label: Label
## The children of these four are what is to collect.
@export var gates: Node3D
@export var coins: Node3D
@export var targets: Node3D
@export var ships: Node3D
@export var game_over_ui: CanvasLayer
## Says "FINISHED!" when everything was collected.
@export var result_label: Label
@export var time_label: Label
@export var best_label: Label
@export var new_record: Control
## Blinks on the game over screen.
@export var retry_label: Control
## Loaded by "start" on the game over screen.
@export_file("*.tscn") var restart_scene: String
## Loaded by "ui_cancel" (Escape, B). Not "leave": that is the Start button too.
@export_file("*.tscn") var menu_scene: String

func _on_player_game_over(_player: Node) -> void: pass
