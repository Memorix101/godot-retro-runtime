# Declaration only. Used by scenes/tests/dump_test.tscn to cover every exported field
# type; there is no C component behind it.
class_name TestProbe
extends Node3D

@export var ratio: float = 0.5
@export var count: int = 3
@export var enabled: bool = true
@export var target: Node3D
@export var unassigned: Node3D
@export_file("*.tscn") var next_scene: String
@export var spawn: PackedScene
@export var spawn_max: int = 2
@export var no_spawn: PackedScene
@export var stats: TestStats

func _on_box_visibility_changed() -> void: pass
func _on_zone_area_entered(_area: Area3D) -> void: pass
