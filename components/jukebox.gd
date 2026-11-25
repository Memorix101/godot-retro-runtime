# Declaration only. The logic lives in runtime/game/airplane.c.
#
# Plays its AudioStreamPlayer children in random order once it is visible in the tree,
# and the next one on the action "next_song". A child is named "Artist - Title"; the
# two parts are shown in the labels when it starts.
class_name Jukebox
extends Node

@export var artist_label: Label
@export var title_label: Label
## Shown for `show_time` seconds after a piece starts.
@export var panel: Control
@export var show_time: float = 6.0
## The action next_song plays the next piece (off where its buttons do something else).
@export var can_skip: bool = true
