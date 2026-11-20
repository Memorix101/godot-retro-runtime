# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/grid_test.tscn, which tools/make_grid_test.gd builds.
class_name GridTest
extends Node3D

## Walks across the floor of the GridMap into its wall.
@export var walker: CharacterBody3D
