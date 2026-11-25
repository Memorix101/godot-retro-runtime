# Declaration only. The logic lives in runtime/game/neonleague.c.
#
# Changes the colour of its material_override (which must be local to the scene): mode 0
# a random colour every `interval` seconds, mode 1 a red pulse. `brightness` scales it.
class_name NeonDisco
extends MeshInstance3D

@export var mode: int
@export var interval: float
@export var brightness: float
