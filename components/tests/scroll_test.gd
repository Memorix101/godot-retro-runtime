# Declaration only. The logic lives in runtime/game/test_features.c.
# Used by scenes/tests/scroll_test.tscn, which tools/make_scroll_test.gd builds.
class_name ScrollTest
extends Control

## The ScrollContainer whose scroll position is printed.
@export var list: ScrollContainer
