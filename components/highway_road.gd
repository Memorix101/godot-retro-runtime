# Declaration only. The logic lives in runtime/game/highway.c.
#
# The endless road: instances of `road` placed one after another along -Z, starting at
# `first`, `ahead` of them in front of the player's. A segment has a node "TrafficSpawn"
# whose Marker3D children are where cars may appear (facing the way they drive; the
# last marker is not used), and two StaticBody3D rails in the group "guardrail". Each new
# segment brings traffic (`car`, `truck`) and rivals at random markers.
class_name HighwayRoad
extends Node

@export var player: Node3D
## Where the root of the first segment goes.
@export var first: Node3D
## Metres from one segment's root to the next.
@export var segment_length: float = 239.0
@export var ahead: int = 1
@export var rail_half_width: float = 0.5
@export var road: PackedScene
@export var road_max: int = 3
@export var car: PackedScene
@export var car_max: int = 6
@export var truck: PackedScene
@export var truck_max: int = 4
@export var rival: PackedScene
@export var rival_max: int = 8
## Wrecks, RigidBody3D scenes with a HighwayWreck, by the cars' wreck_kind. A wrecked car
## is swapped for one, which flies on with its speed.
@export var wreck_car: PackedScene
@export var wreck_car_max: int = 3
@export var wreck_truck: PackedScene
@export var wreck_truck_max: int = 2
@export var wreck_rival: PackedScene
@export var wreck_rival_max: int = 3
@export var wreck_police: PackedScene
@export var wreck_police_max: int = 1
## A real rival's wreck, in its red (the other is the dummies' look).
@export var wreck_rival_red: PackedScene
@export var wreck_rival_red_max: int = 3
