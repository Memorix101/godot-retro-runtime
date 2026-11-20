# Declaration only. The logic lives in runtime/game/racer.c.
#
# An opponent in the racing demo: put it on a PathFollow3D of the track's path, with the
# car (a StaticBody3D with its model) below it. It starts driving at the race's start.
class_name AiDriver
extends PathFollow3D

## Metres per second on average.
@export var speed: float = 28.0
## 0..1: how its speed rises and falls around the lap.
@export var character: float = 0.5
