# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/canvas_test.tscn, which tools/make_canvas_test.gd builds.
class_name CanvasTest
extends Node2D

## Moves across the level; the Camera2D is its child.
@export var target: Node2D
## The Parallax2D nodes whose places are printed.
@export var far: Parallax2D
@export var strip: Parallax2D
@export var bound: Parallax2D
