# Declaration only. The logic lives in runtime/game/highway.c.
#
# The police car of the highway game, moved with move_and_slide against the rails (the
# StaticBody3D nodes in the group "guardrail"). Connect `area_entered` of the Area3D
# child "Hitbox" to `_on_hitbox_area_entered` (traffic wrecks the car) and `area_exited`
# of the Area3D child "NearMiss" to `_on_near_miss_area_exited` (traffic passed close:
# score and boost). The node must be in the groups "player" and "vehicle".
# Actions: accelerate, brake, steer_left, steer_right, boost; start on the game over
# screen, ui_cancel back to the title.
class_name HighwayPlayer
extends CharacterBody3D

## Half the width and length of the car, for the other cars' lane checks.
@export var half_width: float = 1.35
@export var half_length: float = 2.3
## Which of HighwayRoad's wrecks it becomes: 0 car, 1 truck, 2 rival, 3 police car.
@export var wreck_kind: int = 0
@export var max_speed: float = 120.0
@export var boost_speed: float = 150.0
## Seconds the speed curve takes up to the top speed (Unity's script: 2).
@export var accel_time: float = 2.0
## Degrees per second it turns at speed (Unity's script: 50).
@export var turn_rate: float = 50.0
@export var lives: int = 3
## A rail hit more head-on than this, in degrees, at speed wrecks the car.
@export var wall_crash_angle: float = 55.0
## The share of the speed a square rail hit costs; glancing hits cost far less.
@export var wall_speed_loss: float = 0.6
## The child that turns with the car: the model and its lights. The body itself must
## not turn (its shape stays square to the world), so the car turns this instead.
@export var turn: Node3D
@export var speed_label: Label
@export var score_label: Label
@export var takedown_label: Label
## 0 to 100.
@export var boost_bar: TextureProgressBar
## Hidden while the car is wrecked.
@export var hud: Control
## Shown on the last life.
@export var damage_label: Control
@export var game_over: Control
@export var result_label: Label
## Loops; its pitch follows the speed.
@export var engine_sound: AudioStreamPlayer
@export var crash_sound: AudioStreamPlayer
@export var boost_sound: AudioStreamPlayer
@export var takedown_sound: AudioStreamPlayer
@export var near_miss_sound: AudioStreamPlayer
@export var impact_sound: AudioStreamPlayer
@export_file("*.tscn") var title_scene: String

func _on_hitbox_area_entered(_area: Area3D) -> void: pass
func _on_near_miss_area_exited(_area: Area3D) -> void: pass
