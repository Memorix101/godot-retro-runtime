# Declaration only. The logic lives in runtime/game/highway.c.
#
# The wreck of a car in the highway game: a RigidBody3D with the car's model, which
# HighwayRoad puts where a car was wrecked and throws the way it was going. It fires its
# burst and plays its sound when it appears and is freed after `lifetime` seconds.
class_name HighwayWreck
extends RigidBody3D

@export var lifetime: float = 5.0
## Particle systems fired when it appears: a CPUParticles3D, or a node with them.
@export var burst: Node3D
@export var sound: Node
## How far it reaches out from its centre, metres: the police car shoves it away when it
## comes this close (Unity's police car knocked wrecks off its road).
@export var radius: float = 1.5
