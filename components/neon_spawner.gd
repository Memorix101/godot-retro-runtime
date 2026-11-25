# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Fades `preview` in (a MeshInstance3D whose material_override is local to the scene)
# and asks NeonGame for an enemy at its place when it is full.
class_name NeonSpawner
extends Node3D

@export var preview: Node
