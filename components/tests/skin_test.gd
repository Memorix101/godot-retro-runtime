# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/skin_test.tscn, which tools/make_skin_test.gd builds.
class_name SkinTest
extends Node3D

## The mesh that the bones bend.
@export var tube: MeshInstance3D
