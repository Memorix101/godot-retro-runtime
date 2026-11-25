# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Heads for the player at `speed`, keeping `radius` from the other enemies, and turns
# `look` to where it goes. Dies when a laser or a bomb meets its Area3D child, or when the
# stage ends. Must be in the group "enemy".
class_name NeonEnemy
extends CharacterBody3D

@export var speed: float
@export var acceleration: float
@export var turn_speed: float
@export var radius: float
@export var look: Node
