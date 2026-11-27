# picophysics

A single-header physics engine for low power systems (N64, Dreamcast) by Luke Benstead
(Kazade), in the public domain (CC0; the licence text is at the end of `picophysics.h`).

- Source: https://gitlab.com/Kazade/picophysics
- Copied from commit `af5519a` of 2026-06-06 ("Update readme and license"). The project
  calls itself alpha and has no releases.

It moves the `RigidBody3D` nodes of the runtime (`runtime/core/rr_physics.c`).

## What was changed

Four places. In `pp_integrate_velocities` the turn of a step is put in front of the body's
rotation instead of behind it,

```
pp_quat_multiply(&q_rot, &body->rot, &body->rot);   /* was: (&body->rot, &q_rot, ...) */
```

The angular velocity of a body is in the directions of the world: that is how the
collision code works out how fast a point of the body moves (`a_vel x arm`). Put behind
the rotation, the same velocity turned the body around its own axes instead, so a body
that was already tipped turned the wrong way. A body tipped 90 degrees around X and given
an angular velocity around the world's Y turned around the world's Z before the change
and around Y after it.

The second change is in `pp_solve_velocities`. A contact that is not there yet but will
be within the step (a "speculative" one, which is what keeps fast bodies from going
through a triangle) was given no bounce, and the body was braked so that it just reached
the surface; the next step then found it lying there, too slow to bounce. So a body that
came in fast never bounced at all. Now a body that has any bounce bounces from where it
is, which is at most one step's way before the surface:

```
if (manifold->separation > 0.0f && r > 0.0f) {
    allowed = 0.0f;     /* was: if (manifold->separation > 0.0f) r = 0.0f; */
}
```

The third and fourth changes mix two physics materials the way Godot does, also in
`pp_solve_velocities`: the bounce of a contact is the sum of the two (was: the larger
one), kept between 0 and 1, and its friction the smaller of the two, as a size (was: the
two multiplied). The runtime gives the engine Godot's computed values, which are negative
for an absorbent bounce and a rough friction; so a rough material's friction wins, as in
Godot.

```
float r = fminf(fmaxf(manifold->obj1_bounce + manifold->obj2_bounce, 0.0f), 1.0f);
float f = fabsf(fminf(manifold->obj1_friction, manifold->obj2_friction));
```

Nothing else differs from the source. To take a newer version, copy the header and make
these changes again where they are still needed.
