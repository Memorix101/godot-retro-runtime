# Declaration only. The logic lives in runtime/game/starter_racing.c.
# The camera rig of the Starter Kit Racing demo (scenes/starter_racing.tscn), after the
# kit's view.gd by Kenney (MIT licence): it eases after the car and backs off the faster
# the car goes. The `leave` action goes back to the main menu.
class_name StarterView
extends Node3D

## The car to follow.
@export var target: StarterVehicle
## Moved along its own Z with the speed of the car.
@export var camera: Camera3D
## Where the `leave` action goes.
@export_file("*.tscn") var menu_scene: String
