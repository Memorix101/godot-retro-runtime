# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# The player: moves with nl_move_*, turns `gun` to nl_aim_* and fires lasers while it is
# held, throws a bomb on nl_bomb. Connect `area_entered` of its Area3D child "Hitbox" to
# `_on_hitbox_area_entered`: an area of an enemy (group "enemy") or of something deadly
# (group "deadly") kills it. Must be in the group "player".
class_name NeonPlayer
extends CharacterBody3D

@export var speed: float
@export var gun: Node
@export var muzzle: float
@export var laser_speed: float
@export var fire_interval: float
@export var laser: PackedScene
@export var laser_max: int
@export var bomb: PackedScene
@export var bomb_max: int
@export var spawn_sound: Node
@export var fire_sound: Node
@export var bomb_sound: Node

func _on_hitbox_area_entered(..._arguments: Array) -> void: pass
