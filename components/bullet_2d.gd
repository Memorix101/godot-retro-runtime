# Declaration only. The logic lives in runtime/game/test_2d.c.
class_name Bullet2D
extends Area2D

## Pixels per second, up the screen.
@export var speed: float = 480.0

func _on_area_entered(_area: Area2D) -> void: pass
