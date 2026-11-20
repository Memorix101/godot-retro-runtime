# Declaration only. The logic lives in runtime/game/player_car.c.
class_name PlayerCar
extends Node3D

## Top speed in metres per second.
@export var max_speed: float = 45.0
## Metres per second gained per second while accelerating.
@export var acceleration: float = 20.0
## Sideways metres per second at full speed and full steering.
@export var steer_speed: float = 8.0
## The car cannot leave this distance from the middle of the track.
@export var track_half_width: float = 5.0
## Camera that follows the car.
@export var camera: Camera3D
## Label that shows the speed.
@export var speed_label: Label
## Scene to return to when the car reaches the finish.
@export_file("*.tscn") var menu_scene: String

func _on_finish_area_entered(_area: Area3D) -> void: pass
