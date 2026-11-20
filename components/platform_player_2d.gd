# Declaration only. The logic lives in runtime/game/platformer_2d.c.
#
# Give this body a CollisionShape2D with a rectangle. The ground is made of StaticBody2D
# nodes with rectangle shapes. Coins, hazards and the goal are Area2D nodes in the groups
# "coin", "hazard" and "goal"; connect `area_entered` of this node's hitbox area to
# `_on_hitbox_area_entered`. A Camera2D below this node makes the view follow it.
class_name PlatformPlayer2D
extends CharacterBody2D

## Pixels per second.
@export var run_speed: float = 90.0
## Upward speed at the start of a jump, pixels per second.
@export var jump_speed: float = 290.0
## Pixels per second gained per second while falling.
@export var gravity: float = 700.0
## Below this Y position (in the units of this node's parent) the player starts again.
@export var fall_limit: float = 300.0
## The player's pictures, with the animations "idle", "walk" and "jump". The art looks
## to the left; the sprite is flipped while the player walks to the right.
@export var sprite: AnimatedSprite2D
## Label that shows the collected coins.
@export var coin_label: Label
## Sound played for every coin.
@export var coin_sound: AudioStreamPlayer
## Scene to return to at the goal and on ui_cancel.
@export_file("*.tscn") var menu_scene: String

func _on_hitbox_area_entered(_area: Area2D) -> void: pass
