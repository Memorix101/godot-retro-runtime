extends SceneTree
## Builds scenes/stunt_track.tscn, the stunt demo: a road that hangs in the sky, from a
## start gate down a slope, through a loop, round a banked turn, over a jump and up a hill
## to the finish, driven against the clock. The result is an ordinary scene that can be
## opened and edited in Godot; running this tool again overwrites it.
##
##   godot --headless --path . --script res://tools/make_stunt_track.gd
##
## The road is laid like a pen draws a line: _run() moves on by a length and turns on the
## way (to the side, up or down, banking), and leaves a cross section every few metres.
## Between two cross sections lies one box of collision, and the road one sees is a
## ribbon through all of them. The car is a RigidBody3D with a sphere shape; how it
## drives is in its component (runtime/game/stunt_car.c).

const OUTPUT := "res://scenes/stunt_track.tscn"
const KIT := "res://assets/kenney/racing-kit/%s.obj"

## Width of the road, metres.
const WIDTH := 8.0
## How thick the collision of the road is, and how thick the road looks.
const THICK := 1.0
const LOOK_THICK := 0.4
## Longest piece between two cross sections, metres, and the sharpest turn between them.
const STEP := 3.0
const STEP_ANGLE := deg_to_rad(6.0)
## Cross sections per StaticBody3D and mesh, so the runtime can skip what is far away.
const CHUNK := 10
## How high the car's node rides above the road, the radius of its collision sphere (which
## is smaller, so that it only meets the rails), and the scale of the kit's car.
const RIDE_HEIGHT := 0.7
const BODY_RADIUS := 0.35
## Length of a wheel's spring that carries nothing. The car sinks into it by 0.1 m.
const SPRING_LENGTH := 0.8
const CAR_SCALE := 2.0
const RAIL_HEIGHT := 0.7
## The square around the start: how wide, how long, and how far behind the start gate it begins.
const PLAZA_WIDTH := 170.0
const PLAZA_LENGTH := 170.0
const PLAZA_BACK := 128.0
const RAIL_WIDTH := 0.35
## How far to the side a rail begins, and over how many metres it comes in to the edge.
const RAIL_FLARE := 1.6
const RAIL_FLARE_LENGTH := 12.0

var _root: Node3D
var _track: Node3D
var _gates: Node3D
var _road_material: StandardMaterial3D
var _plain_material: StandardMaterial3D

# Where the pen is: a place on the middle of the road and the three turns of the road there.
var _at := Vector3.ZERO
var _yaw := 0.0
var _pitch := 0.0
var _roll := 0.0
# The cross sections laid so far, in runs without a gap: [Transform3D, rails: bool].
var _runs: Array = [[]]
var _length := 0.0
var _checkpoints := 0


func _init() -> void:
	_root = Node3D.new()
	_root.name = "StuntTrack"
	_environment()
	_track = _add(_root, Node3D.new(), "Track") as Node3D
	_gates = _add(_root, Node3D.new(), "Gates") as Node3D
	_road_material = StandardMaterial3D.new()
	_road_material.albedo_texture = load("res://assets/sprites/road.png")
	_road_material.vertex_color_use_as_albedo = true
	_plain_material = StandardMaterial3D.new()
	_plain_material.vertex_color_use_as_albedo = true

	_plaza()
	_mark()
	var start := _course()
	_build()
	_player(start)

	var scene := PackedScene.new()
	var status := scene.pack(_root)
	if status == OK:
		status = ResourceSaver.save(scene, OUTPUT)
	if status != OK:
		push_error("cannot save %s: %s" % [OUTPUT, error_string(status)])
	else:
		print("wrote %s (%d m of road, %d checkpoints)" % [OUTPUT, int(_length), _checkpoints])
	_root.free()
	quit(0 if status == OK else 1)


## A wide square around the start, to try the car on before the course: room to turn,
## to slide and to drift. Its floor lies a hair below the road, which runs across it.
func _plaza() -> void:
	var size := Vector3(PLAZA_WIDTH, THICK, PLAZA_LENGTH)
	var middle := Vector3(0, -0.03, PLAZA_BACK - PLAZA_LENGTH * 0.5)
	var body := _add(_track, StaticBody3D.new(), "Plaza") as StaticBody3D
	body.position = middle
	var box := BoxShape3D.new()
	box.size = size
	var shape := _add(body, CollisionShape3D.new(), "Shape") as CollisionShape3D
	shape.shape = box
	shape.position = Vector3(0, -THICK * 0.5, 0)
	var paint := StandardMaterial3D.new()
	paint.albedo_texture = load("res://assets/sprites/tiles.png")
	paint.vertex_color_use_as_albedo = true
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	tool.set_material(paint)
	var x := size.x * 0.5
	var z := size.z * 0.5
	# The tile texture repeats every 8 metres.
	_quad(tool, [Vector3(-x, 0, z), Vector3(-x, 0, -z), Vector3(x, 0, -z), Vector3(x, 0, z)],
			[Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP],
			[Vector2(0, size.z / 8.0), Vector2(0, 0), Vector2(size.x / 8.0, 0), Vector2(size.x / 8.0, size.z / 8.0)],
			Color(0.62, 0.66, 0.74))
	var no_uv := [Vector2(0.4, 0.4), Vector2(0.4, 0.4), Vector2(0.4, 0.4), Vector2(0.4, 0.4)]
	var dark := Color(0.3, 0.32, 0.38)
	var low := Vector3(0, -LOOK_THICK, 0)
	for side: Array in [[Vector3(-x, 0, z), Vector3(x, 0, z), Vector3.BACK], [Vector3(x, 0, -z), Vector3(-x, 0, -z), Vector3.FORWARD],
			[Vector3(-x, 0, -z), Vector3(-x, 0, z), Vector3.LEFT], [Vector3(x, 0, z), Vector3(x, 0, -z), Vector3.RIGHT]]:
		_quad(tool, [side[0], side[1], side[1] + low, side[0] + low], [side[2], side[2], side[2], side[2]], no_uv, dark)
	_quad(tool, [Vector3(-x, 0, z) + low, Vector3(x, 0, z) + low, Vector3(x, 0, -z) + low, Vector3(-x, 0, -z) + low],
			[Vector3.DOWN, Vector3.DOWN, Vector3.DOWN, Vector3.DOWN], no_uv, dark)
	(_add(body, MeshInstance3D.new(), "Mesh") as MeshInstance3D).mesh = tool.commit()


## The course. Returns where the car starts.
func _course() -> Transform3D:
	_run(12.0)
	var start := _here()
	_run(10.0)
	_gate("Start", "", Color(0.2, 0.75, 0.3))
	_run(22.0)

	# Down a slope to get fast, and through the loop. The loop moves over by more than the
	# width of the road, so that its end clears its beginning.
	_run(28.0, 0.0, -15.0)
	_run(55.0)
	_run(30.0, 0.0, 15.0)
	_run(20.0)
	_loop(11.0, WIDTH + 2.5)
	_run(30.0)
	_gate("Checkpoint1", "checkpoint", Color(0.95, 0.75, 0.2))

	# Half a circle to the right, banked.
	_run(16.0, -30.0, 0.0, -30.0, true)
	_run(63.0, -120.0, 0.0, -30.0, true)
	_run(16.0, -30.0, 0.0, 0.0, true)
	_run(30.0)

	# The jump: up a ramp, over a gap, down onto a floor that slopes away.
	_run(14.0, 0.0, 12.0)
	_gap(13.0, -2.5, -7.0)
	_run(26.0)
	_run(16.0, 0.0, 7.0)
	_run(14.0)
	_gate("Checkpoint2", "checkpoint", Color(0.95, 0.75, 0.2))

	# A quarter circle to the left, banked a little, then over a hill.
	_run(10.0, 20.0, 0.0, 20.0, true)
	_run(26.0, 50.0, 0.0, 20.0, true)
	_run(10.0, 20.0, 0.0, 0.0, true)
	_run(20.0)
	_run(22.0, 0.0, 12.0)
	_run(24.0)
	_run(40.0, 0.0, -24.0)
	_run(24.0)
	_run(22.0, 0.0, 12.0)

	# An S between rails, and the finish.
	_run(30.0, -45.0, 0.0, 0.0, true)
	_run(30.0, 45.0, 0.0, 0.0, true)
	_run(14.0)
	_gate("Checkpoint3", "checkpoint", Color(0.95, 0.75, 0.2))
	_run(40.0)
	_gate("Finish", "goal", Color(0.9, 0.2, 0.2))
	_run(45.0, 0.0, 0.0, 0.0, true)
	# A wall at the very end stops a car that does not brake.
	var wall := _add(_track, StaticBody3D.new(), "EndWall") as StaticBody3D
	wall.transform = _here().translated_local(Vector3(0, 1.0, 0))
	var wall_box := BoxShape3D.new()
	wall_box.size = Vector3(WIDTH, 2.0, 0.6)
	(_add(wall, CollisionShape3D.new(), "Shape") as CollisionShape3D).shape = wall_box
	var wall_mesh := BoxMesh.new()
	wall_mesh.size = wall_box.size
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.85, 0.2, 0.2)
	wall_mesh.material = paint
	(_add(wall, MeshInstance3D.new(), "Mesh") as MeshInstance3D).mesh = wall_mesh
	return start


## The cross section at the pen: X to the right, Y out of the road, -Z along it.
func _here() -> Transform3D:
	return Transform3D(Basis(Vector3.UP, _yaw) * Basis(Vector3.RIGHT, _pitch) * Basis(Vector3.BACK, _roll), _at)


func _mark(rails := false) -> void:
	(_runs.back() as Array).append([_here(), rails])


## Lays `length` metres of road. On the way it turns by `yaw` degrees (to the left), climbs
## by `pitch` degrees (360 is a loop), goes over to a bank of `roll_to` degrees (the right
## edge up) and moves `shift` metres to the right. `rails` puts a rail on both edges.
func _run(length: float, yaw := 0.0, pitch := 0.0, roll_to := 0.0, rails := false, shift := 0.0) -> void:
	# A change of bank counts double: flat boxes cannot follow a road that twists.
	var sharpest := maxf(maxf(absf(deg_to_rad(yaw)), absf(deg_to_rad(pitch))), absf(deg_to_rad(roll_to) - _roll) * 2.0)
	var steps := maxi(1, maxi(ceili(length / STEP), ceili(sharpest / STEP_ANGLE)))
	var roll_from := _roll
	for i in steps:
		# Half the turn, the step, the other half: the pieces then lie on the circle.
		_yaw += deg_to_rad(yaw) / steps * 0.5
		_pitch += deg_to_rad(pitch) / steps * 0.5
		var heading := Basis(Vector3.UP, _yaw) * Basis(Vector3.RIGHT, _pitch)
		_at += heading * Vector3(0, 0, -length / steps) + Basis(Vector3.UP, _yaw) * Vector3(shift / steps, 0, 0)
		_yaw += deg_to_rad(yaw) / steps * 0.5
		_pitch += deg_to_rad(pitch) / steps * 0.5
		_roll = lerpf(roll_from, deg_to_rad(roll_to), float(i + 1) / steps)
		_mark(rails)
	_pitch = wrapf(_pitch, -PI, PI)
	_length += length


## A loop of this radius that ends `shift` metres to the right of where it begins.
## A car that drives straight on keeps what it has of speed along the axis of the loop,
## and only the rest of its speed goes round. So the loop is turned a little against the
## road that leads into it, by just so much that this car is carried over by `shift` on
## its way round: it comes out as straight as it went in, without steering.
func _loop(radius: float, shift: float) -> void:
	var skew := atan(shift / (TAU * radius))
	_yaw += skew
	_run(TAU * radius, 0.0, 360.0, 0.0, true, shift)
	_yaw -= skew


## Ends the road and begins it again `length` metres on and `drop` metres higher (lower,
## if negative), sloping by `pitch` degrees.
func _gap(length: float, drop: float, pitch: float) -> void:
	_at += Basis(Vector3.UP, _yaw) * Vector3(0, drop, -length)
	_pitch = deg_to_rad(pitch)
	_runs.append([])
	_mark()
	_length += length


## Turns the cross sections into collision and into the road one sees.
func _build() -> void:
	var count := 0
	for run: Array in _runs:
		# A rail does not begin as a blunt edge across the way: it comes in from the side
		# over its first metres. Its end stays straight, or a car that scrapes along it
		# would be led off the road there. `outs` is how far out the rails are at each
		# cross section.
		var outs := PackedFloat32Array()
		outs.resize(run.size())
		var railed := 0.0
		for i in run.size():
			if i > 0 and run[i][1]:
				railed += (run[i][0] as Transform3D).origin.distance_to((run[i - 1][0] as Transform3D).origin)
			else:
				railed = 0.0
			outs[i] = RAIL_FLARE * maxf(0.0, 1.0 - railed / RAIL_FLARE_LENGTH)
		for first in range(0, run.size() - 1, CHUNK):
			var last := mini(first + CHUNK, run.size() - 1)
			var body := _add(_track, StaticBody3D.new(), "Road%d" % count) as StaticBody3D
			# The body sits at its first cross section, so the mesh has a middle near its
			# content and the runtime can tell whether it is in view.
			var origin: Vector3 = (run[first][0] as Transform3D).origin
			body.position = origin
			var road := SurfaceTool.new()
			road.begin(Mesh.PRIMITIVE_TRIANGLES)
			road.set_material(_road_material)
			var plain := SurfaceTool.new()
			plain.begin(Mesh.PRIMITIVE_TRIANGLES)
			plain.set_material(_plain_material)
			var any_rails := false
			for i in range(first, last):
				var a: Transform3D = run[i][0]
				var b: Transform3D = run[i + 1][0]
				var rails: bool = run[i + 1][1]
				var out_a := outs[i]
				var out_b := outs[i + 1]
				_piece(body, origin, a, b, i, rails, out_a, out_b)
				_ribbon(road, origin, a, b, i)
				if rails:
					any_rails = true
					for side: float in [-1.0, 1.0]:
						_rail(plain, origin, a, b, side, i, out_a, out_b)
			var mesh := road.commit()
			if any_rails:
				plain.commit(mesh)
			(_add(body, MeshInstance3D.new(), "Mesh") as MeshInstance3D).mesh = mesh
			count += 1


## The collision between two cross sections: a box under the road, and one per rail.
func _piece(body: StaticBody3D, origin: Vector3, a: Transform3D, b: Transform3D, index: int, rails: bool,
		out_a: float, out_b: float) -> void:
	var back := (a.origin - b.origin).normalized()
	var right := (a.basis.x + b.basis.x)
	right = (right - back * right.dot(back)).normalized()
	var basis := Basis(right, back.cross(right), back)
	# Where the road turns, boxes that only reach from section to section would leave a
	# wedge open on the outside of the turn. They overlap by what the turn takes.
	var turn := a.basis.z.angle_to(b.basis.z)
	var length := a.origin.distance_to(b.origin) + (WIDTH + THICK) * turn + 0.05
	var middle := (a.origin + b.origin) * 0.5 - origin
	var floor_box := BoxShape3D.new()
	floor_box.size = Vector3(WIDTH, THICK, length)
	var shape := _add(body, CollisionShape3D.new(), "Floor%d" % index) as CollisionShape3D
	shape.shape = floor_box
	shape.transform = Transform3D(basis, middle - basis.y * THICK * 0.5)
	if rails:
		for side: float in [-1.0, 1.0]:
			var high := RAIL_HEIGHT + 0.6
			var from := a * Vector3(side * ((WIDTH - RAIL_WIDTH) * 0.5 + out_a), high * 0.5, 0)
			var to := b * Vector3(side * ((WIDTH - RAIL_WIDTH) * 0.5 + out_b), high * 0.5, 0)
			var along := (from - to).normalized()
			var across := (basis.y.cross(along)).normalized()
			var rail_box := BoxShape3D.new()
			rail_box.size = Vector3(RAIL_WIDTH, high, from.distance_to(to) + (WIDTH + THICK) * turn + 0.05)
			var rail := _add(body, CollisionShape3D.new(), "Rail%d%s" % [index, "L" if side < 0.0 else "R"]) as CollisionShape3D
			rail.shape = rail_box
			rail.transform = Transform3D(Basis(across, along.cross(across), along), (from + to) * 0.5 - origin)


## The road one sees between two cross sections: its top with the road texture, which
## repeats every 8 metres, and its sides and underside in a dark grey.
func _ribbon(tool: SurfaceTool, origin: Vector3, a: Transform3D, b: Transform3D, index: int) -> void:
	var half := WIDTH * 0.5
	var al := a * Vector3(-half, 0, 0) - origin
	var ar := a * Vector3(half, 0, 0) - origin
	var bl := b * Vector3(-half, 0, 0) - origin
	var br := b * Vector3(half, 0, 0) - origin
	var down_a := a.basis.y * LOOK_THICK
	var down_b := b.basis.y * LOOK_THICK
	# One piece of texture per two pieces of road, so the dashes of the middle line show.
	var v0 := (index % 2) * 0.5
	var v1 := v0 + 0.5
	_quad(tool, [al, bl, br, ar], [a.basis.y, b.basis.y, b.basis.y, a.basis.y],
			[Vector2(0, v0), Vector2(0, v1), Vector2(1, v1), Vector2(1, v0)], Color.WHITE)
	var dark := Color(0.55, 0.57, 0.62)
	var grey := Vector2(0.25, 0.75)  # a place on the texture without a line
	_quad(tool, [al - down_a, ar - down_a, br - down_b, bl - down_b],
			[-a.basis.y, -a.basis.y, -b.basis.y, -b.basis.y], [grey, grey, grey, grey], dark)
	_quad(tool, [al, al - down_a, bl - down_b, bl], [-a.basis.x, -a.basis.x, -b.basis.x, -b.basis.x],
			[grey, grey, grey, grey], dark)
	_quad(tool, [ar, br, br - down_b, ar - down_a], [a.basis.x, b.basis.x, b.basis.x, a.basis.x],
			[grey, grey, grey, grey], dark)


## A rail along one edge of the road, red and white in turns.
func _rail(tool: SurfaceTool, origin: Vector3, a: Transform3D, b: Transform3D, side: float, index: int,
		out_a: float, out_b: float) -> void:
	var color := Color(0.88, 0.16, 0.16) if index % 2 == 0 else Color(0.95, 0.95, 0.95)
	var points: Array[Vector3] = []
	for end: Array in [[a, out_a], [b, out_b]]:
		var section: Transform3D = end[0]
		var outer: float = side * (WIDTH * 0.5 + end[1])
		var inner: float = side * (WIDTH * 0.5 - RAIL_WIDTH + end[1])
		points.append(section * Vector3(inner, 0, 0) - origin)
		points.append(section * Vector3(inner, RAIL_HEIGHT, 0) - origin)
		points.append(section * Vector3(outer, RAIL_HEIGHT, 0) - origin)
		points.append(section * Vector3(outer, -LOOK_THICK, 0) - origin)
	var no_uv := [Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO]
	var to_road := -a.basis.x * side
	_quad(tool, [points[0], points[1], points[5], points[4]], [to_road, to_road, to_road, to_road], no_uv, color)
	_quad(tool, [points[1], points[2], points[6], points[5]], [a.basis.y, a.basis.y, b.basis.y, b.basis.y], no_uv, color)
	_quad(tool, [points[2], points[3], points[7], points[6]], [-to_road, -to_road, -to_road, -to_road], no_uv, color)


## Adds a quad whose corners go round it. Which way round does not matter: the side that
## shows is the one the normals point to.
func _quad(tool: SurfaceTool, corners: Array, normals: Array, uvs: Array, color: Color) -> void:
	var order := [0, 1, 2, 0, 2, 3]
	# Godot's front faces go clockwise.
	var facing: Vector3 = (corners[2] - corners[0]).cross(corners[1] - corners[0])
	if facing.dot(normals[0]) < 0.0:
		order = [0, 2, 1, 0, 3, 2]
	for corner: int in order:
		tool.set_color(color)
		tool.set_normal(normals[corner])
		tool.set_uv(uvs[corner])
		tool.add_vertex(corners[corner])


## A gate over the road at the pen: two posts and a beam, and in `group` an area across
## the road that the car reports ("checkpoint", "goal").
func _gate(node_name: String, group: String, color: Color) -> void:
	var here := _here()
	var gate := _add(_gates, Node3D.new(), node_name) as Node3D
	gate.transform = here
	var paint := StandardMaterial3D.new()
	paint.albedo_color = color
	var height := 5.0
	for part: Array in [[Vector3(0.5, height, 0.5), Vector3(-WIDTH * 0.5 - 0.25, height * 0.5, 0)],
			[Vector3(0.5, height, 0.5), Vector3(WIDTH * 0.5 + 0.25, height * 0.5, 0)],
			[Vector3(WIDTH + 1.0, 0.8, 0.5), Vector3(0, height + 0.4, 0)]]:
		var box := BoxMesh.new()
		box.size = part[0]
		box.material = paint
		var mesh := _add(gate, MeshInstance3D.new(), "Part%d" % gate.get_child_count()) as MeshInstance3D
		mesh.mesh = box
		mesh.position = part[1]
	if group.is_empty():
		return
	var area := _add(gate, Area3D.new(), "Area") as Area3D
	area.add_to_group(group, true)
	area.monitoring = false
	area.position = Vector3(0, 2.0, 0)
	var box_shape := BoxShape3D.new()
	box_shape.size = Vector3(WIDTH, 4.0, 1.0)
	(_add(area, CollisionShape3D.new(), "Shape") as CollisionShape3D).shape = box_shape
	if group == "checkpoint":
		_checkpoints += 1


func _environment() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color(0.16, 0.36, 0.82)
	sky_material.sky_horizon_color = Color(0.86, 0.9, 0.98)
	sky_material.ground_horizon_color = Color(0.86, 0.9, 0.98)
	sky_material.ground_bottom_color = Color(0.5, 0.68, 0.94)
	var sky := Sky.new()
	sky.sky_material = sky_material
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.5, 0.5, 0.5)
	var world := _add(_root, WorldEnvironment.new(), "Environment") as WorldEnvironment
	world.environment = environment
	var sun := _add(_root, DirectionalLight3D.new(), "Sun") as DirectionalLight3D
	sun.rotation_degrees = Vector3(-50, 40, 0)


func _player(start: Transform3D) -> void:
	var slippery := PhysicsMaterial.new()
	# The car's own grip is worked out by its component; the body only keeps it out of
	# the road and the rails.
	slippery.friction = 0.0
	slippery.bounce = 0.0
	var sphere := SphereShape3D.new()
	sphere.radius = BODY_RADIUS

	var car := _add(_root, RigidBody3D.new(), "Car") as RigidBody3D
	car.set_script(load("res://components/stunt_car.gd"))
	car.transform = start.translated_local(Vector3(0, RIDE_HEIGHT, 0))
	car.physics_material_override = slippery
	car.gravity_scale = 2.5
	car.lock_rotation = true
	# Four spheres near the corners of the car, above the road: they stop it at the rails,
	# and it lies on them when it lands on its side or its roof.
	for place: Array in [["ShapeFL", -0.55, -0.95], ["ShapeFR", 0.55, -0.95], ["ShapeRL", -0.55, 0.85], ["ShapeRR", 0.55, 0.85]]:
		var shape := _add(car, CollisionShape3D.new(), place[0]) as CollisionShape3D
		shape.shape = sphere
		shape.position = Vector3(place[1], 0, place[2])

	var model := _add(car, MeshInstance3D.new(), "Model") as MeshInstance3D
	model.mesh = load(KIT % "raceCarRed")
	model.transform = Transform3D(Basis.IDENTITY.scaled(Vector3.ONE * CAR_SCALE), Vector3(0, -RIDE_HEIGHT, 0))

	# One ray per wheel, pointing out of the car's underside whichever way up the car is.
	# They are longer than the springs, so a wheel is found before its spring begins to push.
	var wheels: Array[RayCast3D] = []
	for place: Array in [["WheelFL", -0.85, -1.2], ["WheelFR", 0.85, -1.2], ["WheelRL", -0.85, 1.1], ["WheelRR", 0.85, 1.1]]:
		var wheel := _add(car, RayCast3D.new(), place[0]) as RayCast3D
		wheel.position = Vector3(place[1], 0, place[2])
		wheel.target_position = Vector3(0, -(SPRING_LENGTH + 0.3), 0)
		wheels.append(wheel)
	var hitbox := _add(car, Area3D.new(), "Hitbox") as Area3D
	hitbox.monitorable = false
	var reach := SphereShape3D.new()
	reach.radius = 0.6
	(_add(hitbox, CollisionShape3D.new(), "Shape") as CollisionShape3D).shape = reach
	hitbox.area_entered.connect(Callable(car, "_on_hitbox_area_entered"), CONNECT_PERSIST)
	var engine := _add(car, AudioStreamPlayer.new(), "Engine") as AudioStreamPlayer
	engine.stream = load("res://assets/sounds/engine.wav")
	engine.volume_db = -8.0
	engine.autoplay = true

	# Behind and above the car. The component keeps this place in the car's own
	# directions, so the camera goes through the loop with it.
	var camera := _add(_root, Camera3D.new(), "Camera") as Camera3D
	camera.transform = car.transform.translated_local(Vector3(0, 2.6, 6.5))
	camera.current = true
	camera.fov = 65.0
	camera.near = 0.4
	camera.far = 400.0

	var hud := _add(_root, Control.new(), "Hud") as Control
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	var speed := _label(hud, "Speed", Rect2(16, 10, 220, 28), 20, "0 KM/H", HORIZONTAL_ALIGNMENT_LEFT)
	var time := _label(hud, "Time", Rect2(250, 10, 374, 28), 20, "TIME 0.00", HORIZONTAL_ALIGNMENT_RIGHT)
	var info := _label(hud, "Info", Rect2(120, 44, 400, 28), 20, "", HORIZONTAL_ALIGNMENT_CENTER)
	_label(hud, "Hint", Rect2(20, 452, 600, 22), 14, "Brake held: reverse   R or Y: back to the checkpoint   Esc or Start: menu",
			HORIZONTAL_ALIGNMENT_CENTER)

	car.engine = 14.0
	car.brake = 30.0
	car.max_speed = 38.0
	car.steer = 0.5
	car.steer_fast = 0.09
	car.grip = 1.6
	car.drift = 0.35
	# The car sinks into its springs by gravity / (4 * spring) = 0.1 m.
	car.suspension = SPRING_LENGTH
	car.spring = 61.0
	car.damping = 5.5
	car.downforce = 0.012
	car.gravity = 9.8 * car.gravity_scale
	car.model = model
	car.fall_limit = -60.0
	car.wheel_fl = wheels[0]
	car.wheel_fr = wheels[1]
	car.wheel_rl = wheels[2]
	car.wheel_rr = wheels[3]
	car.camera = camera
	car.engine_sound = engine
	car.speed_label = speed
	car.time_label = time
	car.info_label = info
	car.menu_scene = "res://scenes/main_menu.tscn"


func _label(parent: Control, node_name: String, rect: Rect2, font_size: int, text: String,
		alignment: HorizontalAlignment) -> Label:
	var label := _add(parent, Label.new(), node_name) as Label
	label.position = rect.position
	label.size = rect.size
	label.add_theme_font_size_override("font_size", font_size)
	label.text = text
	label.horizontal_alignment = alignment
	return label


func _add(parent: Node, node: Node, node_name: String) -> Node:
	node.name = node_name
	parent.add_child(node)
	node.owner = _root
	return node
