# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/tile_test.tscn, which tools/make_tile_test.gd builds.
class_name TileTest
extends Node2D

## Go up through the one-way row of tiles and come down onto it.
@export var jumper: CharacterBody2D
@export var roller: CharacterBody2D
## Falls onto the one-way ledge; rises through it from below and lands on it.
@export var faller: CharacterBody2D
@export var riser: CharacterBody2D
## Walks sideways through the one-way row of tiles.
@export var sider: CharacterBody2D
