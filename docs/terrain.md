# Terrain

Ground with hills is a `StaticBody3D` with a `HeightMapShape3D` for the collision and
ordinary meshes for the look. Godot has no terrain editor of its own, and the terrain
plugins for it draw with shaders, which the consoles do not have. So the addon brings a
small editor helper, the `HeightTerrain` node (`addons/retro_runtime/height_terrain.gd`):

1. Add a `HeightTerrain` node to the scene.
2. Give it a `FastNoiseLite` or a grey image as height map, and set the number of cells,
   the metres per cell, the heights and how much of an island it is.
3. Under "Look" set a texture that repeats and a colour gradient by height; steep ground
   gets its own colour.
4. Every change rebuilds the ground; the "Rebuild" button does it by hand.

The node writes plain nodes below itself, which are saved with the scene: "Ground" (the
collision shape) and one `MeshInstance3D` per piece of 8 or 16 cells, so the runtime can
leave out the pieces that are not in view. The exporter takes the helper script off the
node and sees only those plain nodes; a `HeightMapShape3D` made any other way works the
same. The colours go into the vertex colours and the exporter bakes the light into them,
so the ground needs one texture and no shader. There is no blending between textures.

At runtime a body on a height map is moved the second way described under
[Collision](scenes.md#collision). Rays hit the ground too, and an area that reaches into
the ground gets `body_entered` for it.

`scenes/island.tscn` ("Island" in the main menu) shows it: an island of 64 x 64 m with
the player of the 3D platformer, coins, a checkpoint and the flag on the highest hill.
Water deeper than the knees sends the player back. The scene is written by
`tools/make_island.gd`, which sets up a `HeightTerrain` node the way one would in the
inspector; open the scene and select "Terrain" to change the island.

In Flycast it runs at 60 fps from the start to the flag. The numbers that follow are from
the GLdc backend, which the scene was tuned with: 46 to 60 fps, about 55 at the start
where the whole island is in view. The ground is the cheap part: 32 x 32 cells of 2 m are
2300 vertices, and the island with nothing but the ground and the player drew in 7 ms.
What costs is the kit's decoration (a tree has 690 vertices, a coin 200). So trees further
away than 22 m are drawn as two boxes and coins beyond 14 m as one
(`visibility_range_begin/end`), and everything small disappears beyond 35 to 40 m. With
1 m cells and every tree in full the start ran at 17 fps. The PVR backend leaves room to
turn these distances up again; that has not been tried.

