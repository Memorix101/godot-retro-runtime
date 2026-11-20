# Declaration only. The logic lives in runtime/game/path_patrol.c.
#
# Put this on a PathFollow3D below a Path3D. The node runs along the path and back, and
# what is below it rides along: a StaticBody3D as a moving platform, an enemy.
class_name PathPatrol
extends PathFollow3D

## Units per second.
@export var speed: float = 1.5
