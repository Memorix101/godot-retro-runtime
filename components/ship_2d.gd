# Declaration only. The logic lives in runtime/game/test_2d.c.
class_name Ship2D
extends Node2D

## Pixels per second.
@export var speed: float = 240.0
## The scene the fire button makes an instance of.
@export var bullet_scene: PackedScene
## How many bullets can exist at the same time. The exporter puts this many instances
## into the package; the runtime only hands them out.
@export var bullet_scene_max: int = 4
## Scene to return to on ui_cancel.
@export_file("*.tscn") var menu_scene: String
