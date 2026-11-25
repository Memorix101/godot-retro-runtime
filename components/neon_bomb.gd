# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Grows to the scale `size`, holds, and shrinks again; enemies whose areas it touches die.
# Connect its own `area_entered` to `_on_area_entered`.
class_name NeonBomb
extends Area3D

@export var size: float
@export var grow_time: float
@export var hold_time: float

func _on_area_entered(..._arguments: Array) -> void: pass
