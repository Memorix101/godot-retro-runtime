# Declaration only. The logic lives in runtime/game/highway.c.
#
# Drives straight on along its -Z: the camera car of the highway title.
class_name ForwardDummy
extends Node3D

## Metres per second.
@export var speed: float = 60.0
