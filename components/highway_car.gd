# Declaration only. The logic lives in runtime/game/highway.c.
#
# Traffic: an Area3D in the groups "traffic" and "vehicle", moved by code. HighwayRoad
# puts it on the road. It keeps its lane and its distance and changes lanes when held
# up. Connect its `area_entered` to `_on_area_entered` and `body_entered` to
# `_on_body_entered`: any other car wrecks it.
class_name HighwayCar
extends Area3D

@export var half_width: float = 1.5
@export var half_length: float = 3.0
## Which of HighwayRoad's wrecks it becomes: 0 car, 1 truck, 2 rival, 3 police car.
@export var wreck_kind: int = 0
## Its own speed, metres per second.
@export var cruise: float = 50.0
@export var accel: float = 10.0
@export var brake: float = 25.0

func _on_area_entered(_area: Area3D) -> void: pass
func _on_body_entered(_body: Node3D) -> void: pass
