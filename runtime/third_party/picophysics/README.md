# picophysics

A single-header physics engine for low power systems (N64, Dreamcast) by Luke Benstead
(Kazade), in the public domain (CC0; the licence text is at the end of `picophysics.h`).

- Source: https://gitlab.com/Kazade/picophysics
- Copied from commit `af5519a` of 2026-06-06 ("Update readme and license"). The project
  calls itself alpha and has no releases.

It is used by the stunt demo only (`runtime/game/stunt_car.c`), as the body of the car.

## What was changed

One line, in `pp_integrate_velocities`: the turn of a step is put in front of the body's
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

Nothing else differs from the source. To take a newer version, copy the header and make
this change again if it is still needed.
