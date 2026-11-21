# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/rigid2d_test.tscn, which tools/make_rigid2d_test.gd builds.
class_name Rigid2DTest
extends Node2D

## Dropped at an angle; comes to lie flat and reports its contacts.
@export var box: RigidBody2D
## Three boxes that become a stack.
@export var stack_a: RigidBody2D
@export var stack_b: RigidBody2D
@export var stack_c: RigidBody2D
## Bounces.
@export var ball: RigidBody2D
## Its mask does not have the floor's layer.
@export var ghost: RigidBody2D
## Up through the one-way ledge, then down onto it.
@export var riser: RigidBody2D
## Without friction and with its rotation locked; gets a push.
@export var slider: RigidBody2D
## Onto solid tiles; up through one-way tiles and down onto them.
@export var tile_box: RigidBody2D
@export var tile_riser: RigidBody2D
