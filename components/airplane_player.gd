# Declaration only. The logic lives in runtime/game/airplane.c.
#
# The plane of the airplane game, an Area3D moved by code. It waits until it is visible
# in the tree, counts down and flies. Connect the area's `area_entered` to
# `_on_area_entered` and `body_entered` to `_on_body_entered`: an area in the group
# "world" or any body crashes the plane, an area with a Collectable is collected.
# Actions: steer_left, steer_right, pitch_up, pitch_down, throttle_up, throttle_down,
# boost, fire. The node must be in the group "player" (bullets look for it).
class_name AirplanePlayer
extends Area3D

## Metres per second.
@export var min_speed: float = 30.0
@export var max_speed: float = 200.0
## Top speed while boosting.
@export var boost_speed: float = 350.0
## Degrees per second.
@export var turn_speed: float = 75.0
@export var roll_speed: float = 75.0
@export var max_roll: float = 45.0
@export var pitch_speed: float = 75.0
@export var max_pitch: float = 50.0
## Seconds on the clock when the countdown ends.
@export var start_time: float = 7.0
## Seconds from the start to "GO!".
@export var countdown: float = 5.0
@export var game: AirplaneGame
## Hidden when the plane crashes.
@export var model: Node3D
@export var time_label: Label
@export var clock_minutes: Label
@export var clock_seconds: Label
@export var clock_millis: Label
@export var countdown_label: Label
@export var mode_panel: Control
@export var boost_label: Label
@export var boost_bar: TextureProgressBar
@export var time_up_label: Label
## Loops; its pitch follows the speed.
@export var engine_sound: AudioStreamPlayer
@export var crash_sound: AudioStreamPlayer
@export var count_sound: AudioStreamPlayer
@export var go_sound: AudioStreamPlayer
@export var fire_sound: AudioStreamPlayer
## Fired where the plane crashes, like a Collectable's burst.
@export var crash_burst: Node3D
@export var bullet_spawn: Node3D
@export var bullet: PackedScene
@export var bullet_max: int = 8

func _on_area_entered(_area: Node) -> void: pass
func _on_body_entered(_body: Node) -> void: pass
func _on_shot(_hit: Node) -> void: pass
