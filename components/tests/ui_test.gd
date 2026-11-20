# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/ui_test.tscn, which tools/make_ui_test.gd builds.
class_name UiTest
extends Control

## The disabled button that the Unlock button enables.
@export var locked: Button
## A sound on the SFX bus: the slider sets the volume of the bus, the box mutes it, the
## switch changes the pitch.
@export var engine: AudioStreamPlayer

func _on_plain() -> void: pass
func _on_locked() -> void: pass
func _on_picture() -> void: pass
func _on_unlock() -> void: pass
func _on_toggled(_toggled_on: bool) -> void: pass
func _on_value_changed(_value: float) -> void: pass
