# Declaration only. The logic lives in runtime/game/highway.c.
#
# The Flare script of the police lights: the brightness of a lens flare swings between
# start_range and end_range with sin(time * speed), and start_first picks the phase, so
# a red and a blue one take turns. Put it on a glow (a billboard Sprite3D): its size
# follows the brightness, the size it has in the editor at end_range.
class_name Flare
extends Node3D

@export var start_range: float = 10.0
@export var end_range: float = 50.0
## Radians per second.
@export var speed: float = 20.0
@export var start_first: bool = true
