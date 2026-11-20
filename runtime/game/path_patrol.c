/* PathPatrol: moves a PathFollow node along its path at a steady speed, there and back
 * again (or round and round if the node loops). Whatever hangs below the node rides
 * along: a platform, an enemy. Placeholder script: components/path_patrol.gd. */
#include "../core/rr_scene.h"

typedef struct {
    float speed; /* exported, units per second */

    float direction; /* 1 or -1 */
} PathPatrol;

static void path_patrol_ready(Node *self, PathPatrol *patrol)
{
    (void)self;
    patrol->direction = 1.0f;
}

static void path_patrol_update(Node *self, PathPatrol *patrol, float dt)
{
    const float length = rr_path_get_length(self->parent);

    self->value += patrol->speed * patrol->direction * dt;
    if (((const RRPkgPathFollowNode *)self->data)->loop) {
        return; /* the runtime wraps the progress around */
    }
    if (self->value >= length) {
        self->value       = length;
        patrol->direction = -1.0f;
    } else if (self->value <= 0.0f) {
        self->value       = 0.0f;
        patrol->direction = 1.0f;
    }
}

COMPONENT_BEGIN(PathPatrol, "PathPatrol")
    EXPORT_FLOAT(speed)
    ON_READY(path_patrol_ready)
    ON_UPDATE(path_patrol_update)
COMPONENT_END()
