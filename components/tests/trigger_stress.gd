# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/stress_triggers.tscn, which tools/make_stress_test.gd builds.
class_name TriggerStress
extends Node

## The first row of areas; it is moved away after the first count.
@export var row: Node3D
## Walks through a gap between the bodies.
@export var free_walker: CharacterBody3D
## Walks into the first body and must stop there.
@export var blocked_walker: CharacterBody3D

func _on_area_entered(_area: Area3D) -> void: pass
func _on_area_exited(_area: Area3D) -> void: pass
