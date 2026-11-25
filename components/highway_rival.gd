# Declaration only. The logic lives in runtime/game/highway.c.
#
# A rival: an Area3D in the groups "rival" and "vehicle", moved by code. It chases or
# flees the police car and picks lanes, also the oncoming ones. Hits and grinding
# damage it; at full damage, when pushed into a rail or when it hits traffic, it is
# wrecked. Half of the rivals (chosen at random) are wrecked by the first touch. Against
# the police car it is a box of half_width by half_length that the police car cannot
# drive into (both were rigid bodies in Unity). Connect `area_entered`/`area_exited` to
# the handlers of the same names.
class_name HighwayRival
extends Area3D

@export var half_width: float = 1.35
@export var half_length: float = 2.3
## Which of HighwayRoad's wrecks it becomes: 0 car, 1 truck, 2 rival, 3 police car.
@export var wreck_kind: int = 0
@export var top_speed: float = 135.0
@export var accel: float = 28.0
@export var brake: float = 45.0
## How much faster than the police car it goes to pull past.
@export var pass_margin: float = 8.0
## How much slower than the police car it flees, so it can be caught.
@export var flee_margin: float = 10.0
## A hit this hard (relative speed) wrecks it at once; weaker hits add up.
@export var takedown_impact: float = 45.0
## Seconds of grinding until it is wrecked.
@export var grind_time: float = 2.0
## Shown and fired when it is taken down: CPUParticles3D, or a node with them.
@export var explosion: Node3D
## Shown while the police car is close.
@export var indicator: Node3D
## The body as the dummies look, and a copy in the rivals' red shown for the others.
@export var body: Node3D
@export var paint: Node3D
@export var crash_sound: AudioStreamPlayer3D

func _on_area_entered(_area: Area3D) -> void: pass
func _on_area_exited(_area: Area3D) -> void: pass
