# Declaration only. The logic lives in runtime/game/highway.c.
#
# Stays above and behind its target, turned as it is in the scene, and shakes when a
# component asks it to. The camera must be in the group "main_camera" for that.
class_name ChaseCamera
extends Camera3D

@export var target: Node3D
## Metres above the target.
@export var height: float = 25.0
## Metres behind it, along Godot's +Z.
@export var back: float = 15.0
