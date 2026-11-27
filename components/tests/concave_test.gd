# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/concave_test.tscn, which tools/make_concave_test.gd builds.
class_name ConcaveTest
extends Node3D

## Rays straight down onto the bowl.
@export var down0: RayCast3D
@export var down1: RayCast3D
@export var down2: RayCast3D
@export var down3: RayCast3D
## A ray that meets the wall from its front, and two that meet it from behind: without
## and with hit_back_faces.
@export var wall_front: RayCast3D
@export var wall_back: RayCast3D
@export var wall_back_hit: RayCast3D
## Two rays that meet a fence that is solid from both sides from behind: without and
## with hit_back_faces.
@export var fence_back: RayCast3D
@export var fence_back_hit: RayCast3D
## Walks up the bowl into the wall.
@export var walker: CharacterBody3D
## Rolls down into the bowl.
@export var ball: RigidBody3D


func _on_ball_body_entered(_body: Node) -> void: pass
