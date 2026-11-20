# Declaration only. The logic lives in runtime/game/platformer_3d.c.
#
# A platform that falls a moment after the player stepped on it and comes back later.
# Give it an Area3D child above its top and connect that area's `body_entered` to
# `_on_sense_body_entered`, and two one-shot Timer children whose `timeout` signals go to
# `_on_fall_timeout` and `_on_return_timeout`.
class_name CrumblePlatform
extends StaticBody3D

## Runs from the first step on the platform to its fall.
@export var fall_timer: Timer
## Runs from the fall to the platform being back in its place.
@export var return_timer: Timer
## How far the platform falls.
@export var drop: float = 12.0

func _on_sense_body_entered(_body: Node3D) -> void: pass
func _on_fall_timeout() -> void: pass
func _on_return_timeout() -> void: pass
