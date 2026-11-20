# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/anim_test.tscn, which tools/make_anim_test.gd builds.
class_name AnimTest
extends Node3D

## The node the clips move, and their player.
@export var mover: Node3D
@export var anim: AnimationPlayer
