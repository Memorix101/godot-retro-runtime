/* Meshes that bones deform (a MeshInstance3D with a skin whose vertices follow more than
 * one bone).
 *
 * The bones are nodes of the scene, moved by an AnimationPlayer or a component. A mesh
 * with a skin has a copy of its mesh asset in the scene's memory; once per frame every
 * vertex of the copy is put where its bones take it, and the copy is what is drawn. The
 * colours stay as they were baked for the mesh at rest. */
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

#define MAX_BONES 256

/* The inverse of a transform: a 3 x 3 matrix with a place. */
static void invert(RRMat34 *out, const RRMat34 *in)
{
    const float *m = in->m;
    float        c[9], det, inv;
    int          i;

    c[0] = m[4] * m[8] - m[5] * m[7];
    c[1] = m[5] * m[6] - m[3] * m[8];
    c[2] = m[3] * m[7] - m[4] * m[6];
    c[3] = m[2] * m[7] - m[1] * m[8];
    c[4] = m[0] * m[8] - m[2] * m[6];
    c[5] = m[1] * m[6] - m[0] * m[7];
    c[6] = m[1] * m[5] - m[2] * m[4];
    c[7] = m[2] * m[3] - m[0] * m[5];
    c[8] = m[0] * m[4] - m[1] * m[3];
    det = m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
    inv = det != 0.0f ? 1.0f / det : 0.0f;
    /* The columns of the inverse are the rows of the cofactors. */
    for (i = 0; i < 3; i++) {
        out->m[i * 3]     = c[i] * inv;
        out->m[i * 3 + 1] = c[3 + i] * inv;
        out->m[i * 3 + 2] = c[6 + i] * inv;
    }
    for (i = 0; i < 3; i++) {
        out->m[9 + i] = -(out->m[i] * m[9] + out->m[3 + i] * m[10] + out->m[6 + i] * m[11]);
    }
}

void rr_skin_init(RRScene *scene, Node *node, uint8_t *memory)
{
    const RRPkgMeshNode *d = (const RRPkgMeshNode *)node->data;
    RRSkinState         *state = &scene->skins[scene->skin_count++];

    state->node = (uint16_t)(node - scene->nodes);
    state->mesh = (RRPkgMesh *)memory;
    memcpy(memory, rr_package_asset(scene->pkg, d->mesh), scene->pkg->assets[d->mesh].size);
}

const RRPkgMesh *rr_skin_mesh(const RRScene *scene, const Node *node)
{
    const uint16_t index = (uint16_t)(node - scene->nodes);
    uint32_t       i;

    for (i = 0; i < scene->skin_count; i++) {
        if (scene->skins[i].node == index) {
            return scene->skins[i].mesh;
        }
    }
    return NULL;
}

void rr_skins_update(RRScene *scene)
{
    static RRMat34 matrices[MAX_BONES]; /* from the mesh at rest to the mesh as it is posed */
    uint32_t       i, b, s, v, k;

    for (i = 0; i < scene->skin_count; i++) {
        const Node           *node = &scene->nodes[scene->skins[i].node];
        const RRPkgMeshNode  *d = (const RRPkgMeshNode *)node->data;
        const RRPkgSkin      *skin = (const RRPkgSkin *)rr_package_asset(scene->pkg, d->skin);
        const RRPkgMesh      *rest = rr_package_mesh(scene->pkg, d->mesh);
        RRPkgMesh            *posed = scene->skins[i].mesh;
        const uint16_t       *bones = (const uint16_t *)((const uint8_t *)skin + skin->bones_off);
        const RRMat34        *binds = (const RRMat34 *)((const uint8_t *)skin + skin->binds_off);
        const RRPkgInfluence *influence = (const RRPkgInfluence *)((const uint8_t *)skin + skin->influences_off);
        const uint32_t        position = rr_vertex_position_offset(scene->pkg->header->platform);
        RRMat34               to_mesh, in_mesh;

        if (!rr_node_visible_in_tree(node)) {
            continue;
        }
        /* The vertices stay in the space of the mesh node, which is drawn with its own
         * transform: bone space -> world -> mesh space. */
        invert(&to_mesh, &node->world);
        for (b = 0; b < skin->bone_count; b++) {
            rr_mat34_mul(&in_mesh, &to_mesh, &scene->nodes[bones[b]].world);
            rr_mat34_mul(&matrices[b], &in_mesh, &binds[b]);
        }
        for (s = 0; s < rest->surface_count; s++) {
            const uint8_t *from = (const uint8_t *)rr_surface_verts(rest, &rest->surfaces[s]) + position;
            uint8_t       *to = (uint8_t *)posed + rest->surfaces[s].verts_off + position;

            for (v = 0; v < rest->surfaces[s].vertex_count;
                 v++, influence++, from += RR_VERTEX_SIZE, to += RR_VERTEX_SIZE) {
                const float *p = (const float *)(const void *)from;
                float       *out = (float *)(void *)to;
                float        x = 0.0f, y = 0.0f, z = 0.0f;

                for (k = 0; k < 4 && influence->weight[k] != 0; k++) {
                    const float *m = matrices[influence->bone[k]].m;
                    const float  w = (float)influence->weight[k] * (1.0f / 255.0f);
                    x += w * (m[0] * p[0] + m[3] * p[1] + m[6] * p[2] + m[9]);
                    y += w * (m[1] * p[0] + m[4] * p[1] + m[7] * p[2] + m[10]);
                    z += w * (m[2] * p[0] + m[5] * p[1] + m[8] * p[2] + m[11]);
                }
                out[0] = x;
                out[1] = y;
                out[2] = z;
            }
        }
        rr_gfx_mesh_changed(posed, scene->pkg->assets[d->mesh].size);
    }
}
