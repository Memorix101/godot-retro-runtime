# Declaration only. The logic lives in runtime/game/airplane.c.
#
# A gate, coin, target or ship of the airplane game. The plane collects it by flying
# through it (an Area3D) or by shooting it (`destroyable`; a body, which bullets hit).
# The game counts the children of the lists it is in.
class_name Collectable
extends Node3D

## Seconds added to the clock.
@export var extend_time: float = 2.0
## Collected by a bullet, not by flying through. Flying into it crashes the plane.
@export var destroyable: bool = false
## Played when it is collected.
@export var sound: AudioStreamPlayer
## Moved to where it was and fired: a CPUParticles3D, or a node whose CPUParticles3D
## children are fired together.
@export var burst: Node3D
