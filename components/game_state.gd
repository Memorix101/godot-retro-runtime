# Declaration only. The logic lives in runtime/game/game_state.c.
# This is the autoload "Game" (Project Settings > Globals): it is part of every scene, and
# what its C component counts survives the change from one scene to the next.
class_name GameState
extends Node

## Lives a run starts with.
@export var start_lives: int = 3
