# Declaration only. The logic lives in runtime/game/highway.c.
#
# A title: `prompt` blinks, the action "start" loads `game_scene`, ui_cancel loads
# `menu_scene` (if set).
class_name TitleScreen
extends Node

@export var prompt: Control
@export_file("*.tscn") var game_scene: String
@export_file("*.tscn") var menu_scene: String
