# A custom resource, used by scenes/tests/features.tscn. In C it is the struct TestStats
# in runtime/game/test_features.c.
class_name TestStats
extends Resource

@export var power: float = 1.0
@export var gears: int = 5
@export var next: TestStats
