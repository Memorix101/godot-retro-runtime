# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Follows `target` along X and Z, `back` metres behind it, and shakes to the sides when
# an enemy is killed. Without a target it turns about the world's up axis at `spin_speed`
# degrees per second (the title).
class_name NeonCamera
extends Camera3D

@export var target: Node
@export var back: float
@export var shake_amount: float
@export var spin_speed: float
