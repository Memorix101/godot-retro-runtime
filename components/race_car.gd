# Declaration only. The logic lives in runtime/game/racer.c.
#
# The player's car in the racing demo. Connect `area_entered` of its hitbox area to
# `_on_hitbox_area_entered`, `timeout` of the countdown Timer to `_on_countdown_timeout`
# and `timeout` of the finish Timer to `_on_finish_timer_timeout`.
class_name RaceCar
extends CharacterBody3D

## Top speed, metres per second.
@export var max_speed: float = 34.0
## Metres per second gained per second at full throttle.
@export var accel: float = 13.0
## Metres per second lost per second at full brake.
@export var brake: float = 30.0
## Turn rate at speed, radians per second.
@export var steer: float = 1.6
## How fast the velocity follows the heading; lower values slide more.
@export var grip: float = 7.0
## Laps of a race.
@export var laps: int = 3
## Where on the track's path the car starts, metres from the path's beginning.
@export var start_distance: float = 0.0
## The scale of the car's model in the scene.
@export var model_scale: float = 1.0
@export var model: Node3D
## Camera that follows the car.
@export var camera: Camera3D
## Path3D along the middle of the track.
@export var path: Path3D
## Parent of the checkpoint PathFollow3D nodes, in race order; the first is the finish line.
@export var checkpoints: Node3D
## Parent of the opponents' PathFollow3D nodes (each with an AiDriver).
@export var opponents: Node3D
## Timer that ticks once a second for the countdown.
@export var countdown: Timer
## One-shot Timer from the finish back to the menu.
@export var finish_timer: Timer
@export var lap_label: Label
@export var time_label: Label
@export var best_label: Label
@export var pos_label: Label
@export var speed_label: Label
@export var message_label: Label
@export_file("*.tscn") var menu_scene: String

func _on_hitbox_area_entered(_area: Area3D) -> void: pass
func _on_countdown_timeout() -> void: pass
func _on_finish_timer_timeout() -> void: pass
