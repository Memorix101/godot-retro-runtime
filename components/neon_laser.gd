# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# A laser: bounces off the walls (give it a physics material with bounce 1) and keeps
# `speed`. Connect `area_entered` of its Area3D child "Hit" to `_on_hit_area_entered`: it
# kills an enemy or the player and ends, and two lasers end each other. Must be in the
# group "laser".
class_name NeonLaser
extends RigidBody3D

@export var speed: float
@export var lifetime: float
## The look of the laser itself: hidden when it ends, while its trail goes.
@export var ball: Node3D
## Unity's TrailRenderer: the children of this node (up to 32 MeshInstance3D, each a
## ribbon one metre long along +Z and one metre wide at its front, with a material_override local to
## the scene) are laid along the way the laser came, through its bounces. When the laser
## ends, it stops, and the trail shrinks to it in half a second before it is freed.
@export var trail: Node3D
## Seconds of flight the trail shows (Unity's TrailRenderer.time).
@export var trail_time: float = 1.2
## Metres wide at the laser; it narrows to nothing.
@export var trail_width: float = 0.52

func _on_hit_area_entered(..._arguments: Array) -> void: pass
