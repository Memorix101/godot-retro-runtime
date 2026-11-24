# Declaration only. The logic lives in runtime/game/airplane.c.
#
# Flies along its -Z. The RayCast3D looks ahead as far as the bullet flies in a frame;
# what it hits is passed to the plane (the node in the group "player"), and the bullet
# is freed.
class_name AirplaneBullet
extends Node3D

## Metres per second.
@export var speed: float = 500.0
## Seconds until it is freed.
@export var lifetime: float = 5.0
## Points along -Z, at least speed / 30 metres long.
@export var ray: RayCast3D
