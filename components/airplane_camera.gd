# Declaration only. The logic lives in runtime/game/airplane.c.
#
# Follows the target like Unity's SmoothFollow (behind its heading, at a height that
# lags behind), or circles the world's origin. The AirplaneGame switches between the two.
class_name AirplaneCamera
extends Camera3D

## The plane.
@export var target: Node3D
## Metres behind the target.
@export var distance: float = 15.0
## Metres above it.
@export var height: float = 5.0
@export var height_damping: float = 5.0
@export var rotation_damping: float = 5.0
## Degrees per second around the world's origin while not following.
@export var orbit_speed: float = 5.0
## Follow the target from the start.
@export var follow: bool = false
