# Declaration only. The logic lives in runtime/game/starter_racing.c.
# The car of the Starter Kit Racing demo (scenes/starter_racing/vehicle.tscn), after the
# kit's vehicle.gd by Kenney (MIT licence): a rolling sphere carries it, and the model
# follows the sphere, turns with the steering and lies down on the ground.
class_name StarterVehicle
extends Node3D

## The RigidBody3D that rolls: the gas spins it, its rough material makes it grip.
@export var sphere: RigidBody3D
## Looks for the ground below the car, to lay the model on it.
@export var ground: RayCast3D
## Holds the model, the trails and the sounds; follows the sphere.
@export var container: Node3D
## Optional: the parts of the model that lean, tilt and turn.
@export var vehicle_body: Node3D
@export var wheel_fl: Node3D
@export var wheel_fr: Node3D
@export var wheel_bl: Node3D
@export var wheel_br: Node3D
## Optional: the smoke behind the rear wheels when the car slides.
@export var trail_left: CPUParticles3D
@export var trail_right: CPUParticles3D
@export var screech_sound: AudioStreamPlayer3D
@export var engine_sound: AudioStreamPlayer3D
@export var impact_sound: AudioStreamPlayer3D


func _on_sphere_body_entered(_body: Node) -> void: pass
