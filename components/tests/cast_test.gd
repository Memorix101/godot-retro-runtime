# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/cast_test.tscn, which tools/make_cast_test.gd builds.
class_name CastTest
extends Node3D

## The shape casts whose results are printed.
@export var sphere_cast: ShapeCast3D
@export var box_cast: ShapeCast3D
@export var capsule_cast: ShapeCast3D
@export var miss_cast: ShapeCast3D
@export var circle_cast: ShapeCast2D
