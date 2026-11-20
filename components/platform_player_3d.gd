# Declaration only. The logic lives in runtime/game/platformer_3d.c.
#
# Give this body a CollisionShape3D with a box. The ground is made of StaticBody3D nodes
# with box shapes. Coins, hazards and the goal are Area3D nodes in the groups "coin",
# "hazard" and "goal"; connect `area_entered` of this node's hitbox area to
# `_on_hitbox_area_entered`.
# A coin with an AnimationPlayer child plays its "collect" animation when it is taken.
# An Area3D in the group "enemy" hurts from the side and is removed when landed on.
# An Area3D in the group "checkpoint" is where the player starts again after a fall, one
# in the group "spring" throws it up. The lives come from the autoload "Game".
# This node and the camera must have the same parent.
class_name PlatformPlayer3D
extends CharacterBody3D

## Metres per second.
@export var run_speed: float = 4.0
## Upward speed at the start of a jump, metres per second.
@export var jump_speed: float = 7.5
## Metres per second lost per second in the air.
@export var gravity: float = 20.0
## Below this height the player starts again.
@export var fall_limit: float = -6.0
## The node that is turned to face the walking direction. A scale on it is lost.
@export var model: Node3D
## AnimationPlayer of the model. The player starts its clips "idle", "walk", "jump"
## and "fall" as it moves.
@export var anim: AnimationPlayer
## Camera that follows the player.
@export var camera: Camera3D
## Label that shows the collected coins.
@export var coin_label: Label
## Sound played for every coin.
@export var coin_sound: AudioStreamPlayer
## Particle system that is moved to every collected coin and restarted there.
@export var coin_burst: CPUParticles3D
## RayCast3D below this node that points down. Where it hits the ground, `shadow` is put.
@export var shadow_ray: RayCast3D
## Sprite3D that lies flat (axis Y) and shows where the player would land.
@export var shadow: Sprite3D
## Label that shows the lives, the time of this run and the best time.
@export var status_label: Label
## Scene to return to at the goal and on ui_cancel.
@export_file("*.tscn") var menu_scene: String
## Which of the best times in the saved game belongs to this level: 0 to 3, a different
## number in every scene that uses this player.
@export_range(0, 3) var level: int = 0

func _on_hitbox_area_entered(_area: Area3D) -> void: pass
