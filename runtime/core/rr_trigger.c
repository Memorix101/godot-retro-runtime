/* Triggers: overlap tests between Area nodes (2D and 3D) and between areas and bodies.
 *
 * When a pair starts or stops overlapping, `area_entered` / `area_exited` is emitted on
 * each area that monitors the other one, with the other area as argument, and
 * `body_entered` / `body_exited` on an area that monitors a body (StaticBody,
 * CharacterBody, RigidBody) whose layer is in its mask. The rules are Godot's.
 *
 * Cost: every frame the box around each area and body is worked out, and the boxes are
 * swept along X (sort and sweep), so only pairs that are close are tested shape against
 * shape. The pairs that touch are kept in a sorted list; comparing it with the list of
 * the last frame gives the signals. Nothing is allocated while the game runs. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "rr_internal.h"

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void rr_volume_from_shape(RRVolume *v, const Node *shape)
{
    const RRPkgShapeNode *s = (const RRPkgShapeNode *)shape->data;

    rr_volume_make(v, &shape->world, s);
    if (s->kind == RR_SHAPE_HEIGHT_MAP) {
        /* The exporter saw to it that the shape is not rotated, so the matrix is three
         * scales and a position. The box around the ground reaches a little below its
         * lowest point: what got under the surface there is still found and lifted out. */
        const RRPkgHeightMap *map = (const RRPkgHeightMap *)rr_package_asset(shape->scene->pkg, s->map);
        const float          *m = shape->world.m;
        const float           low = map->min * m[4] - 1.0f, high = map->max * m[4];
        int                   i, k;

        v->map       = map;
        v->scale[0]  = m[0];
        v->scale[1]  = m[4];
        v->scale[2]  = m[8];
        v->half[0]   = 0.5f * (float)(map->width - 1) * m[0];
        v->half[1]   = 0.5f * (high - low);
        v->half[2]   = 0.5f * (float)(map->depth - 1) * m[8];
        v->origin[0] = m[9] - v->half[0];
        v->origin[1] = m[10];
        v->origin[2] = m[11] - v->half[2];
        v->center[1] = m[10] + 0.5f * (high + low);
        for (i = 0; i < 3; i++) {
            for (k = 0; k < 3; k++) {
                v->axis[i][k] = i == k ? 1.0f : 0.0f;
            }
        }
    } else if (s->kind == RR_SHAPE_CONCAVE) {
        /* The triangles stay in the shape's space; the box around them is turned and
         * scaled with the shape, a little larger, so that what touches a face that lies
         * in a side of the box is inside it. */
        const RRPkgTriangles *t = (const RRPkgTriangles *)rr_package_asset(shape->scene->pkg, s->map);
        const float          *m = shape->world.m;
        float                 mid[3];
        int                   i, k;

        v->tris     = t;
        v->backface = s->backface != 0;
        v->frame    = shape->world;
        for (k = 0; k < 3; k++) {
            mid[k] = 0.5f * (t->min[k] + t->max[k]);
        }
        for (i = 0; i < 3; i++) {
            const float length = sqrtf(dot3(&m[i * 3], &m[i * 3]));
            v->half[i]   = 0.5f * (t->max[i] - t->min[i]) * length + 0.01f;
            v->center[i] = m[i] * mid[0] + m[3 + i] * mid[1] + m[6 + i] * mid[2] + m[9 + i];
        }
    }
}

/* --- concave shapes --------------------------------------------------------------------
 * A ConcavePolygonShape3D is a list of triangles with nothing inside: what is in front of
 * a face is pushed out to the front, what is behind it goes through, unless the shape is
 * solid from both sides (backface_collision). Every test goes through all triangles of
 * the shape whose box is near; the shapes of a GridMap are small, one per cell. */

bool rr_concave_triangle(const RRVolume *v, uint32_t index, float *a, float *b, float *c, float *normal)
{
    const float *m = v->frame.m;
    const float *p = &v->tris->corners[index * 9];
    float       *out[3];
    float        e1[3], e2[3], length;
    int          i, k;

    out[0] = a;
    out[1] = b;
    out[2] = c;
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            out[i][k] = m[k] * p[i * 3] + m[3 + k] * p[i * 3 + 1] + m[6 + k] * p[i * 3 + 2] + m[9 + k];
        }
    }
    /* Godot's normal of a face: (a - c) x (a - b). */
    for (k = 0; k < 3; k++) {
        e1[k] = a[k] - c[k];
        e2[k] = a[k] - b[k];
    }
    normal[0] = e1[1] * e2[2] - e1[2] * e2[1];
    normal[1] = e1[2] * e2[0] - e1[0] * e2[2];
    normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
    length    = sqrtf(dot3(normal, normal));
    if (length < 1.0e-12f) {
        return false;
    }
    for (k = 0; k < 3; k++) {
        normal[k] /= length;
    }
    return true;
}

/* The point of triangle a b c that is closest to p (Ericson, Real-Time Collision
 * Detection, 5.1.5). */
static void closest_on_triangle(const float *p, const float *a, const float *b, const float *c, float *out)
{
    float ab[3], ac[3], ap[3], bp[3], cp[3], d1, d2, d3, d4, d5, d6, va, vb, vc, v, w, denom;
    int   k;

    for (k = 0; k < 3; k++) {
        ab[k] = b[k] - a[k];
        ac[k] = c[k] - a[k];
        ap[k] = p[k] - a[k];
        bp[k] = p[k] - b[k];
        cp[k] = p[k] - c[k];
    }
    d1 = dot3(ab, ap);
    d2 = dot3(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        for (k = 0; k < 3; k++) out[k] = a[k];
        return;
    }
    d3 = dot3(ab, bp);
    d4 = dot3(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        for (k = 0; k < 3; k++) out[k] = b[k];
        return;
    }
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        v = d1 / (d1 - d3);
        for (k = 0; k < 3; k++) out[k] = a[k] + ab[k] * v;
        return;
    }
    d5 = dot3(ab, cp);
    d6 = dot3(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        for (k = 0; k < 3; k++) out[k] = c[k];
        return;
    }
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        w = d2 / (d2 - d6);
        for (k = 0; k < 3; k++) out[k] = a[k] + ac[k] * w;
        return;
    }
    va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (k = 0; k < 3; k++) out[k] = b[k] + (c[k] - b[k]) * w;
        return;
    }
    denom = 1.0f / (va + vb + vc);
    v     = vb * denom;
    w     = vc * denom;
    for (k = 0; k < 3; k++) {
        out[k] = a[k] + ab[k] * v + ac[k] * w;
    }
}

/* How far a box (center, axis, half) reaches from its centre along a direction. */
static float box_reach(const RRVolume *box, const float *dir)
{
    return box->half[0] * fabsf(dot3(box->axis[0], dir)) + box->half[1] * fabsf(dot3(box->axis[1], dir)) +
           box->half[2] * fabsf(dot3(box->axis[2], dir));
}

/* Separating axis test of a box and a triangle (13 axes: the face normal, the box's three
 * axes and the nine crosses of their edges). */
static bool box_triangle(const RRVolume *box, const float *a, const float *b, const float *c, const float *normal)
{
    const float *corner[3];
    float        edge[3][3], axis[3];
    int          i, j, k;

    corner[0] = a;
    corner[1] = b;
    corner[2] = c;
    for (k = 0; k < 3; k++) {
        edge[0][k] = b[k] - a[k];
        edge[1][k] = c[k] - b[k];
        edge[2][k] = a[k] - c[k];
    }
    for (i = 0; i < 13; i++) {
        float lo = 1.0e30f, hi = -1.0e30f, mid, r, length2;
        if (i == 0) {
            for (k = 0; k < 3; k++) axis[k] = normal[k];
        } else if (i < 4) {
            for (k = 0; k < 3; k++) axis[k] = box->axis[i - 1][k];
        } else {
            const float *u = box->axis[(i - 4) / 3], *e = edge[(i - 4) % 3];
            axis[0] = u[1] * e[2] - u[2] * e[1];
            axis[1] = u[2] * e[0] - u[0] * e[2];
            axis[2] = u[0] * e[1] - u[1] * e[0];
        }
        length2 = dot3(axis, axis);
        if (length2 < 1.0e-10f) {
            continue; /* an edge along an axis of the box: another axis covers it */
        }
        for (j = 0; j < 3; j++) {
            const float p = dot3(corner[j], axis);
            lo = p < lo ? p : lo;
            hi = p > hi ? p : hi;
        }
        mid = dot3(box->center, axis);
        r   = box_reach(box, axis);
        if (mid + r < lo || mid - r > hi) {
            return false;
        }
    }
    return true;
}

/* One sphere against the faces: the deepest of its overlaps, kept in `best` if deeper. */
static void concave_sphere(const RRVolume *v, const float *center, float radius, float *best_normal, float *best)
{
    uint32_t i;
    int      k;

    for (i = 0; i < v->tris->count; i++) {
        float a[3], b[3], c[3], n[3], q[3], d[3], dist2, dist, side, depth;
        if (!rr_concave_triangle(v, i, a, b, c, n)) {
            continue;
        }
        side = (center[0] - a[0]) * n[0] + (center[1] - a[1]) * n[1] + (center[2] - a[2]) * n[2];
        if (side > radius || side < -radius || (side < 0.0f && !v->backface)) {
            continue; /* too far from the plane, or behind a face that is open from behind */
        }
        closest_on_triangle(center, a, b, c, q);
        for (k = 0; k < 3; k++) {
            d[k] = center[k] - q[k];
        }
        dist2 = dot3(d, d);
        if (dist2 >= radius * radius) {
            continue;
        }
        dist  = sqrtf(dist2);
        depth = radius - dist;
        if (depth <= *best) {
            continue;
        }
        *best = depth;
        for (k = 0; k < 3; k++) {
            /* Away from the closest point; on the face itself, along its normal. */
            best_normal[k] = dist > 1.0e-5f ? d[k] / dist : (side >= 0.0f ? n[k] : -n[k]);
        }
    }
}

bool rr_concave_contact(const RRVolume *v, const RRVolume *other, float *normal, float *depth)
{
    float best = 0.0f;
    int   k;

    if (other->sphere) {
        concave_sphere(v, other->center, other->half[0], normal, &best);
    } else if (other->capsule) {
        /* Spheres along the capsule's line, no farther apart than its radius. */
        int count = (int)(2.0f * other->seg / (other->radius > 0.0f ? other->radius : 1.0f)) + 2, i;
        count     = count > 9 ? 9 : count;
        for (i = 0; i < count; i++) {
            const float along = -other->seg + 2.0f * other->seg * (float)i / (float)(count - 1);
            float       p[3];
            for (k = 0; k < 3; k++) {
                p[k] = other->center[k] + other->axis[1][k] * along;
            }
            concave_sphere(v, p, other->radius, normal, &best);
        }
    } else if (other->map == NULL && other->tris == NULL) {
        /* A box is pushed out of a face it overlaps along the face's normal, by as much
         * as it reaches over the face's plane. */
        uint32_t i;
        for (i = 0; i < v->tris->count; i++) {
            float a[3], b[3], c[3], n[3], side, reach, d;
            if (!rr_concave_triangle(v, i, a, b, c, n) || !box_triangle(other, a, b, c, n)) {
                continue;
            }
            side  = (other->center[0] - a[0]) * n[0] + (other->center[1] - a[1]) * n[1] +
                   (other->center[2] - a[2]) * n[2];
            reach = box_reach(other, n);
            if (side < 0.0f && !v->backface) {
                continue;
            }
            d = reach - fabsf(side);
            if (d > best) {
                best = d;
                for (k = 0; k < 3; k++) {
                    normal[k] = side >= 0.0f ? n[k] : -n[k];
                }
            }
        }
    }
    *depth = best;
    return best > 0.0f;
}

bool rr_height_at(const RRVolume *v, float x, float z, float *height, float *normal)
{
    const RRPkgHeightMap *map = v->map;
    const float           gx = (x - v->origin[0]) / v->scale[0], gz = (z - v->origin[2]) / v->scale[2];
    const float          *row;
    int                   cx = (int)gx, cz = (int)gz;
    float                 fx, fz, h00, h10, h01, h11, slope_x, slope_z;

    if (gx < 0.0f || gz < 0.0f || gx > (float)(map->width - 1) || gz > (float)(map->depth - 1)) {
        return false;
    }
    /* The last row and column of points belong to the cells before them. */
    cx  = cx > map->width - 2 ? map->width - 2 : cx;
    cz  = cz > map->depth - 2 ? map->depth - 2 : cz;
    fx  = gx - (float)cx;
    fz  = gz - (float)cz;
    row = &map->heights[cz * map->width + cx];
    h00 = row[0];
    h10 = row[1];
    h01 = row[map->width];
    h11 = row[map->width + 1];
    /* The cell is two triangles, split along the diagonal from (1, 0) to (0, 1). */
    if (fx + fz <= 1.0f) {
        slope_x = h10 - h00;
        slope_z = h01 - h00;
        *height = h00 + slope_x * fx + slope_z * fz;
    } else {
        slope_x = h11 - h01;
        slope_z = h11 - h10;
        *height = h11 - slope_x * (1.0f - fx) - slope_z * (1.0f - fz);
    }
    *height = v->origin[1] + *height * v->scale[1];
    if (normal != NULL) {
        float length;
        normal[0] = -slope_x * v->scale[1] / v->scale[0];
        normal[1] = 1.0f;
        normal[2] = -slope_z * v->scale[1] / v->scale[2];
        length    = sqrtf(dot3(normal, normal));
        normal[0] /= length;
        normal[1] /= length;
        normal[2] /= length;
    }
    return true;
}

/* World-space volume of a collision shape under a world transform, with its scale. */
void rr_volume_make(RRVolume *v, const RRMat34 *world, const RRPkgShapeNode *s)
{
    const float *m = world->m;
    float        longest = 0.0f;
    int          i, k;

    v->sphere  = s->kind == RR_SHAPE_SPHERE;
    v->capsule = s->kind == RR_SHAPE_CAPSULE;
    v->one_way = s->one_way != 0;
    v->map     = NULL;
    v->tris    = NULL;
    for (i = 0; i < 3; i++) {
        const float length = sqrtf(dot3(&m[i * 3], &m[i * 3]));
        const float inv = length > 0.0f ? 1.0f / length : 0.0f;
        for (k = 0; k < 3; k++) {
            v->axis[i][k] = m[i * 3 + k] * inv;
        }
        v->half[i]   = s->size[i] * length;
        v->center[i] = m[9 + i];
        if (length > longest) {
            longest = length;
        }
    }
    if (v->sphere) {
        v->half[0] = s->size[0] * longest;
    } else if (v->capsule) {
        /* Upright in the shape's own space: a line along Y with a radius around it. Who
         * does not know capsules sees the box around it. */
        v->radius  = s->size[0] * longest;
        v->seg     = s->size[1] * sqrtf(dot3(&m[3], &m[3]));
        v->half[0] = v->radius;
        v->half[1] = v->seg + v->radius;
        v->half[2] = v->radius;
    }
}

static bool sphere_sphere(const RRVolume *a, const RRVolume *b)
{
    const float d[3] = { b->center[0] - a->center[0], b->center[1] - a->center[1],
                         b->center[2] - a->center[2] };
    const float r = a->half[0] + b->half[0];
    return dot3(d, d) <= r * r;
}

static bool sphere_box(const RRVolume *sphere, const RRVolume *box)
{
    const float d[3] = { sphere->center[0] - box->center[0], sphere->center[1] - box->center[1],
                         sphere->center[2] - box->center[2] };
    float       dist2 = 0.0f;
    int         i;
    /* Distance from the sphere centre to the box, measured in the box's own axes. */
    for (i = 0; i < 3; i++) {
        const float p = dot3(d, box->axis[i]);
        const float excess = fabsf(p) - box->half[i];
        if (excess > 0.0f) {
            dist2 += excess * excess;
        }
    }
    return dist2 <= sphere->half[0] * sphere->half[0];
}

/* Separating axis test for two oriented boxes (15 axes). */
static bool box_box(const RRVolume *a, const RRVolume *b)
{
    const float d[3] = { b->center[0] - a->center[0], b->center[1] - a->center[1],
                         b->center[2] - a->center[2] };
    float       r[3][3], abs_r[3][3], t[3], ra, rb;
    int         i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            r[i][j]     = dot3(a->axis[i], b->axis[j]);
            abs_r[i][j] = fabsf(r[i][j]) + 0.0001f; /* keeps parallel edges stable */
        }
        t[i] = dot3(d, a->axis[i]);
    }
    for (i = 0; i < 3; i++) {
        rb = b->half[0] * abs_r[i][0] + b->half[1] * abs_r[i][1] + b->half[2] * abs_r[i][2];
        if (fabsf(t[i]) > a->half[i] + rb) {
            return false;
        }
    }
    for (j = 0; j < 3; j++) {
        ra = a->half[0] * abs_r[0][j] + a->half[1] * abs_r[1][j] + a->half[2] * abs_r[2][j];
        if (fabsf(t[0] * r[0][j] + t[1] * r[1][j] + t[2] * r[2][j]) > ra + b->half[j]) {
            return false;
        }
    }
    for (i = 0; i < 3; i++) {
        const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        for (j = 0; j < 3; j++) {
            const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            ra = a->half[i1] * abs_r[i2][j] + a->half[i2] * abs_r[i1][j];
            rb = b->half[j1] * abs_r[i][j2] + b->half[j2] * abs_r[i][j1];
            if (fabsf(t[i2] * r[i1][j] - t[i1] * r[i2][j]) > ra + rb) {
                return false;
            }
        }
    }
    return true;
}

/* A shape touches the ground of a height map if the lowest point of the box around it,
 * at its middle, is below the ground there. */
static bool touches_ground(const RRVolume *ground, const RRVolume *other)
{
    float lo[3], hi[3], height;

    rr_volume_bounds(other, lo, hi);
    return rr_height_at(ground, other->center[0], other->center[2], &height, NULL) && lo[1] <= height &&
           hi[1] >= ground->center[1] - ground->half[1];
}

static bool volumes_overlap(const RRVolume *a, const RRVolume *b)
{
    float normal[3], depth;

    if (a->tris != NULL || b->tris != NULL) {
        /* Two concave shapes are both static: they never meet. */
        if (a->tris != NULL && b->tris != NULL) {
            return false;
        }
        return a->tris != NULL ? rr_concave_contact(a, b, normal, &depth) : rr_concave_contact(b, a, normal, &depth);
    }
    if (a->map != NULL || b->map != NULL) {
        return a->map != NULL ? (b->map == NULL && touches_ground(a, b)) : touches_ground(b, a);
    }
    if (a->sphere) {
        return b->sphere ? sphere_sphere(a, b) : sphere_box(a, b);
    }
    return b->sphere ? sphere_box(b, a) : box_box(a, b);
}

/* --- colliders -------------------------------------------------------------------------
 * Every Area and every body is a collider; its shapes are its CollisionShape children. The
 * list is made once, when the scene is created. */

static bool is_body_type(uint8_t type)
{
    return type == RR_NODE_STATIC_BODY || type == RR_NODE_CHARACTER_BODY ||
           type == RR_NODE_RIGID_BODY;
}

uint32_t rr_trigger_shape_count(const RRPackage *pkg)
{
    uint32_t i, count = 0;
    for (i = 1; i < pkg->header->nodes.count; i++) {
        const uint8_t parent = pkg->nodes[pkg->nodes[i].parent].type;
        if (pkg->nodes[i].type == RR_NODE_SHAPE && (parent == RR_NODE_AREA || is_body_type(parent))) {
            count++;
        }
    }
    return count;
}

uint32_t rr_trigger_pair_capacity(uint32_t areas, uint32_t bodies)
{
    /* As many pairs as can overlap at all, but no more than eight per collider: a lot
     * more than a game has touching at the same time. */
    const uint64_t possible = (uint64_t)areas * (areas > 0 ? areas - 1 : 0) / 2u + (uint64_t)areas * bodies;
    const uint64_t enough = 8u * (uint64_t)(areas + bodies) + 64u;
    return (uint32_t)(possible < enough ? (possible > 0 ? possible : 1) : enough);
}

/* Called during scene creation, after `areas` and `bodies` are filled in tree order. */
void rr_trigger_init(RRScene *scene)
{
    uint32_t c, shape = 0;

    scene->collider_count = scene->area_count + scene->body_count;
    for (c = 0; c < scene->collider_count; c++) {
        RRCollider    *col = &scene->colliders[c];
        const uint16_t node = c < scene->area_count ? scene->areas[c] : scene->bodies[c - scene->area_count];
        uint32_t       n;

        col->node        = node;
        col->area        = c < scene->area_count;
        col->shape_first = (uint16_t)shape;
        /* The subtree of the node follows it directly; its shapes are direct children. */
        for (n = node + 1u; n < scene->node_count && scene->nodes[n].parent >= &scene->nodes[node]; n++) {
            if (scene->nodes[n].type == RR_NODE_SHAPE && scene->nodes[n].parent == &scene->nodes[node]) {
                scene->collider_shapes[shape++] = (uint16_t)n;
            }
        }
        col->shape_count = (uint16_t)(shape - col->shape_first);
        scene->sweep[c]  = (uint16_t)c;
    }
}

void rr_volume_bounds(const RRVolume *v, float *min, float *max)
{
    int a;
    for (a = 0; a < 3; a++) {
        /* A sphere reaches its radius every way; a box (and the box around a capsule) as
         * far as its three axes take it. */
        const float reach = v->sphere ? v->half[0]
                                      : fabsf(v->axis[0][a]) * v->half[0] + fabsf(v->axis[1][a]) * v->half[1] +
                                            fabsf(v->axis[2][a]) * v->half[2];
        min[a] = v->center[a] - reach;
        max[a] = v->center[a] + reach;
    }
}

/* Works out the volumes of a collider's shapes and the box around all of them. Returns
 * false if the collider is out of the tree or has no shape that counts. */
static bool collider_update(RRScene *scene, RRCollider *col)
{
    uint32_t k;
    int      a;
    bool     any = false;

    if (scene->nodes[col->node].flags & RR_NODE_FLAG_FREE) {
        return false;
    }
    for (k = 0; k < col->shape_count; k++) {
        const Node *shape = &scene->nodes[scene->collider_shapes[col->shape_first + k]];
        RRVolume   *v = &scene->volumes[col->shape_first + k];
        float       lo[3], hi[3];

        v->used = !(shape->flags & RR_NODE_FLAG_FREE) && !((const RRPkgShapeNode *)shape->data)->disabled;
        if (!v->used) {
            continue;
        }
        rr_volume_from_shape(v, shape);
        rr_volume_bounds(v, lo, hi);
        for (a = 0; a < 3; a++) {
            if (!any || lo[a] < col->min[a]) {
                col->min[a] = lo[a];
            }
            if (!any || hi[a] > col->max[a]) {
                col->max[a] = hi[a];
            }
        }
        any = true;
    }
    return any;
}

/* True if any shape of one collider touches any shape of the other. */
static bool colliders_overlap(const RRScene *scene, const RRCollider *a, const RRCollider *b)
{
    uint32_t i, j;

    for (i = 0; i < a->shape_count; i++) {
        const RRVolume *va = &scene->volumes[a->shape_first + i];
        if (!va->used) {
            continue;
        }
        for (j = 0; j < b->shape_count; j++) {
            const RRVolume *vb = &scene->volumes[b->shape_first + j];
            if (vb->used && volumes_overlap(va, vb)) {
                return true;
            }
        }
    }
    return false;
}

static bool detects(const RRPkgAreaNode *watcher, const RRPkgAreaNode *other)
{
    return watcher->monitoring && other->monitorable && (watcher->mask & other->layer) != 0;
}

/* True if the rules let the two colliders notice each other at all: two areas where one
 * monitors the other, or an area that monitors a body on a layer of its mask. Two bodies
 * are not a trigger matter. */
static bool may_notice(const RRScene *scene, const RRCollider *a, const RRCollider *b)
{
    const void *da = scene->nodes[a->node].data, *db = scene->nodes[b->node].data;

    if (a->area && b->area) {
        return detects((const RRPkgAreaNode *)da, (const RRPkgAreaNode *)db) ||
               detects((const RRPkgAreaNode *)db, (const RRPkgAreaNode *)da);
    }
    if (a->area) {
        return ((const RRPkgAreaNode *)da)->monitoring &&
               (((const RRPkgAreaNode *)da)->mask & ((const RRPkgBodyNode *)db)->layer) != 0;
    }
    if (b->area) {
        return ((const RRPkgAreaNode *)db)->monitoring &&
               (((const RRPkgAreaNode *)db)->mask & ((const RRPkgBodyNode *)da)->layer) != 0;
    }
    return false;
}

/* Sends the signals for a pair that started (`now`) or stopped touching. A node that left
 * the tree is not told; the other one is. */
static void notify(RRScene *scene, uint32_t key, bool now)
{
    const RRCollider *a = &scene->colliders[key >> 16], *b = &scene->colliders[key & 0xFFFFu];
    Node             *na = &scene->nodes[a->node], *nb = &scene->nodes[b->node];

    if (a->area && b->area) {
        if (detects((const RRPkgAreaNode *)na->data, (const RRPkgAreaNode *)nb->data) &&
            !(na->flags & RR_NODE_FLAG_FREE)) {
            rr_emit(na, now ? "area_entered" : "area_exited", nb);
        }
        if (detects((const RRPkgAreaNode *)nb->data, (const RRPkgAreaNode *)na->data) &&
            !(nb->flags & RR_NODE_FLAG_FREE)) {
            rr_emit(nb, now ? "area_entered" : "area_exited", na);
        }
    } else {
        Node *area = a->area ? na : nb, *body = a->area ? nb : na;
        if (!(area->flags & RR_NODE_FLAG_FREE)) {
            rr_emit(area, now ? "body_entered" : "body_exited", body);
        }
    }
}

static int compare_keys(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

void rr_colliders_refresh(RRScene *scene)
{
    uint32_t  i, j;
    uint16_t *sweep = scene->sweep;

    /* Where every collider is, and whether it counts at all. */
    for (i = 0; i < scene->collider_count; i++) {
        RRCollider *col = &scene->colliders[i];
        col->live = collider_update(scene, col);
        if (!col->live) {
            col->min[0] = FLT_MAX; /* sorts to the end, where the sweep never gets to */
        }
    }

    /* The colliders sorted by where they begin along X. Little moves from one frame to
     * the next, so the order of the last frame is almost right already. */
    for (i = 1; i < scene->collider_count; i++) {
        const uint16_t c = sweep[i];
        const float    x = scene->colliders[c].min[0];
        for (j = i; j > 0 && scene->colliders[sweep[j - 1]].min[0] > x; j--) {
            sweep[j] = sweep[j - 1];
        }
        sweep[j] = c;
    }
}

void rr_triggers_update(RRScene *scene)
{
    uint32_t  i, j, count = 0, old_count, *old;
    uint16_t *sweep = scene->sweep;

    /* rr_colliders_refresh ran after the nodes moved; the rr_trigger_forget calls since
     * the last update apply to this one. */
    for (i = 0; i < scene->collider_count; i++) {
        scene->colliders[i].forget = scene->colliders[i].fresh;
        scene->colliders[i].fresh  = false;
    }

    /* 3. The pairs whose boxes overlap along X: for each collider, the ones that begin
     *    before it ends. Then the other two axes, the rules, and the shapes. */
    for (i = 0; i < scene->collider_count; i++) {
        const RRCollider *a = &scene->colliders[sweep[i]];
        if (!a->live) {
            break; /* only colliders that are out of the tree follow */
        }
        for (j = i + 1; j < scene->collider_count; j++) {
            const RRCollider *b = &scene->colliders[sweep[j]];
            if (b->min[0] > a->max[0]) {
                break;
            }
            if (b->min[1] > a->max[1] || a->min[1] > b->max[1] || b->min[2] > a->max[2] ||
                a->min[2] > b->max[2] || !may_notice(scene, a, b) || !colliders_overlap(scene, a, b)) {
                continue;
            }
            if (count == scene->pair_capacity) {
                if (!scene->pair_overflow) {
                    printf("retro: more than %u overlapping pairs of areas and bodies; the rest is "
                           "not reported\n", (unsigned)scene->pair_capacity);
                    scene->pair_overflow = true;
                }
                break;
            }
            scene->pairs_next[count++] = sweep[i] < sweep[j] ? ((uint32_t)sweep[i] << 16) | sweep[j]
                                                             : ((uint32_t)sweep[j] << 16) | sweep[i];
        }
    }
    qsort(scene->pairs_next, count, sizeof(uint32_t), compare_keys);

    /* 4. The new list becomes the current one before any handler runs, so a handler that
     *    makes an instance (and so calls rr_trigger_forget) sees the state of now. */
    old              = scene->pairs;
    old_count        = scene->pair_count;
    scene->pairs     = scene->pairs_next;
    scene->pair_count = count;
    scene->pairs_next = old;

    /* 5. What changed: both lists are sorted, so one walk through them finds it. A pair
     *    with a collider that was forgotten starts anew: no exit, and an enter if it
     *    touches now. */
    for (i = 0, j = 0; i < old_count || j < count;) {
        const uint32_t was = i < old_count ? old[i] : UINT32_MAX;
        const uint32_t now = j < count ? scene->pairs[j] : UINT32_MAX;
        const uint32_t key = was < now ? was : now;
        const bool     forgotten = scene->colliders[key >> 16].forget || scene->colliders[key & 0xFFFFu].forget;

        if (was == now) {
            if (forgotten) {
                notify(scene, key, true);
            }
            i++;
            j++;
        } else if (was < now) {
            if (!forgotten) {
                notify(scene, key, false);
            }
            i++;
        } else {
            notify(scene, key, true);
            j++;
        }
    }
}

void rr_trigger_forget(RRScene *scene, const Node *node)
{
    const uint16_t index = (uint16_t)(node - scene->nodes);
    uint32_t       c;

    for (c = 0; c < scene->collider_count; c++) {
        if (scene->colliders[c].node == index) {
            scene->colliders[c].fresh = true;
            /* Where it is now, for the rays and bodies of this frame. */
            scene->colliders[c].live = collider_update(scene, &scene->colliders[c]);
            if (!scene->colliders[c].live) {
                scene->colliders[c].min[0] = FLT_MAX;
            }
            return;
        }
    }
}
