# Declaration only. The logic lives in runtime/game/stunt_car.c.
#
# Give this body a CollisionShape3D with a small sphere (smaller than the car rides high,
# so that it only meets the rails, and the road when the car lies on it) and a physics
# material without friction or bounce: the grip is worked out by the component. Below it
# go four RayCast3D nodes where the wheels are, each pointing down by more than the
# length of the suspension. The road and its rails are StaticBody3D nodes with box shapes,
# turned any way, also upside down in a loop.
# Gates are Area3D nodes in the groups "checkpoint" and "goal", two metres above the road;
# connect `area_entered` of this node's hitbox area to `_on_hitbox_area_entered`. A run
# counts at the goal when it went through every checkpoint.
# This node and the camera must have the same parent.
class_name StuntCar
extends RigidBody3D

## Metres per second the car gains per second with the gas down.
@export var engine: float = 14.0
## Metres per second it loses per second with the brake down.
@export var brake: float = 30.0
## Fastest the engine takes it, metres per second. Downhill it gets faster.
@export var max_speed: float = 38.0
## How far the front wheels turn at a crawl, radians, and at top speed (`steer_fast`);
## in between it goes from the one to the other.
@export var steer: float = 0.5
@export var steer_fast: float = 0.09
## What a tyre holds before it slides, as a part of the load on its wheel (Godot's
## wheel_friction_slip). The downforce adds load at speed. High is a car on rails, low
## one on ice.
@export var grip: float = 1.6
## The part of that which the tyres keep in a drift: a slide at speed, which the brake in
## a turn starts and the steering holds. Lower makes wider curves.
@export_range(0.05, 1.0) var drift: float = 0.35
## Length of a spring that carries nothing, from the wheel's ray node down to the road,
## metres. The car rides a little lower than this.
@export var suspension: float = 0.8
## What one spring pushes with per metre it is pressed in, as an acceleration of the car.
## Four of them carry it: the car sinks in by gravity / (4 * spring).
@export var spring: float = 61.0
## What one damper takes away per metre per second the car comes towards the road.
@export var damping: float = 5.5
## How hard the air presses the car onto the road, per metre per second squared. This is
## what holds it in a loop.
@export var downforce: float = 0.012
## What the runtime lets this body fall with, metres per second squared: the project's
## gravity times the gravity scale of this body. The component needs it to tell a hit
## from falling.
@export var gravity: float = 24.5
## Below this height the car is put back at the last checkpoint.
@export var fall_limit: float = -60.0
## The model of the car. It may have a scale.
@export var model: Node3D
## The four RayCast3D nodes, one where each wheel is: front left, front right, rear left,
## rear right.
@export var wheel_fl: RayCast3D
@export var wheel_fr: RayCast3D
@export var wheel_rl: RayCast3D
@export var wheel_rr: RayCast3D
## Camera behind the car. Where it is in the scene says how far behind and above the car
## it stays; it goes through a loop with the car.
@export var camera: Camera3D
## Looping sound whose pitch follows the speed.
@export var engine_sound: AudioStreamPlayer
## Label that shows the speed.
@export var speed_label: Label
## Label that shows the time of this run and the best time.
@export var time_label: Label
## Label that shows the checkpoints gone through, and the finish.
@export var info_label: Label
## Scene to return to after the finish and on ui_cancel.
@export_file("*.tscn") var menu_scene: String

func _on_hitbox_area_entered(_area: Area3D) -> void: pass
