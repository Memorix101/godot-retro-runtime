# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Blinks (`blink`) for `duration` seconds after it is shown and then hides, and/or takes
# a random colour every frame (`colors`), through its modulate.
class_name NeonFlicker
extends Control

@export var blink: bool
@export var colors: bool
@export var duration: float
