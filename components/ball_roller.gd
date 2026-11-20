# Declaration only. The logic lives in runtime/game/ball_roll.c.
#
# Give this body a CollisionShape3D with a sphere, and a physics material without
# friction: the runtime's bodies do not turn, so rubbing would only brake the ball (use
# the linear damp as its rolling resistance). The floors are StaticBody3D nodes with box
# shapes, flat, sloped or banked. Coins, checkpoints and the goal are Area3D nodes in the
# groups "coin", "checkpoint" and "goal"; connect `area_entered` of this node's hitbox
# area to `_on_hitbox_area_entered`. The lives come from the autoload "Game".
# This node and the camera must have the same parent.
class_name BallRoller
extends RigidBody3D

## Metres per second the stick adds every second, as if the floor were leaned that way.
@export var push: float = 9.0
## Fastest the ball goes over the ground, metres per second.
@export var max_speed: float = 13.0
## How far the view leans with the stick.
@export var tilt_degrees: float = 12.0
## Radius of the ball, which says how fast the shell turns.
@export var radius: float = 0.6
## Below this height the ball starts again.
@export var fall_limit: float = -14.0
## The mesh of the ball, which is turned as it rolls.
@export var shell: Node3D
## The node that is turned to face the way the ball rolls. A scale on it is lost.
@export var rider: Node3D
## AnimationPlayer of the rider with the clips "idle" and "walk".
@export var anim: AnimationPlayer
## Camera behind the ball. Where it is in the scene says how far behind and above the
## ball it stays.
@export var camera: Camera3D
## Label that shows the collected coins.
@export var coin_label: Label
## Sound played for every coin.
@export var coin_sound: AudioStreamPlayer
## Particle system that is moved to every collected coin and restarted there.
@export var coin_burst: CPUParticles3D
## RayCast3D below this node that points down. Where it hits a floor, `shadow` is put.
@export var shadow_ray: RayCast3D
## Sprite3D that lies flat (axis Y) and shows where the ball would land.
@export var shadow: Sprite3D
## Label that shows the lives, the time of this run and the best time.
@export var status_label: Label
## Scene to return to at the goal and on ui_cancel.
@export_file("*.tscn") var menu_scene: String

func _on_hitbox_area_entered(_area: Area3D) -> void: pass
