# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/features.tscn, which tools/make_feature_test.gd builds.
class_name FeatureTest
extends Node

@export var ray_down: RayCast3D
@export var ray_miss: RayCast3D
@export var ray_2d: RayCast2D
@export var track: Path3D
@export var follower: PathFollow3D
@export var follower_loop: PathFollow3D
@export var follower_2d: PathFollow2D
@export var bar: ProgressBar
@export var speaker: AudioStreamPlayer3D
@export var speaker_2d: AudioStreamPlayer2D
@export var beep: AudioStreamPlayer
@export var music: AudioStreamPlayer
@export var mover: Node2D
@export var tween_target: Node3D
@export var greeting: Label
@export var stats: TestStats
@export_file("*.tscn") var self_scene: String

func _on_beep_finished() -> void: pass
func _on_sensor_body_entered(_body: Node3D) -> void: pass
func _on_anim_mark() -> void: pass
func _on_anim_finished(_anim_name: StringName) -> void: pass
