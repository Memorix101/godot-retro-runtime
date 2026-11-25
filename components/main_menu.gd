# Declaration only. The logic lives in runtime/game/main_menu.c.
class_name MainMenu
extends Control

## Scene the Start button loads.
@export_file("*.tscn") var start_scene: String
## Scene the 2D Test button loads.
@export_file("*.tscn") var test_2d_scene: String
## Scenes of the two platformer demos.
@export_file("*.tscn") var platformer_2d_scene: String
@export_file("*.tscn") var platformer_3d_scene: String
## Scene of the racing demo.
@export_file("*.tscn") var racing_scene: String
## Scene of the terrain demo.
@export_file("*.tscn") var island_scene: String
## Scene of the ball demo.
@export_file("*.tscn") var ball_scene: String
## Scenes of the two Unity ports (the airplane game and Highway Pursuit's title).
@export_file("*.tscn") var airplane_scene: String
@export_file("*.tscn") var highway_scene: String
## Scene of the third Unity port (Neon League's title).
@export_file("*.tscn") var neonleague_scene: String
## The scene that checks the runtime's features and shows what it found.
@export_file("*.tscn") var feature_scene: String

func _on_start_pressed() -> void: pass
func _on_test_2d_pressed() -> void: pass
func _on_platformer_2d_pressed() -> void: pass
func _on_platformer_3d_pressed() -> void: pass
func _on_racing_pressed() -> void: pass
func _on_island_pressed() -> void: pass
func _on_ball_pressed() -> void: pass
func _on_airplane_pressed() -> void: pass
func _on_highway_pressed() -> void: pass
func _on_neonleague_pressed() -> void: pass
func _on_features_pressed() -> void: pass
func _on_quit_pressed() -> void: pass
