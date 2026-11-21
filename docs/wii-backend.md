# The Wii and GameCube backend

The platform layer for both consoles is `runtime/platform/wii/`: `wii_gx.c` for the
graphics, on GX through libogc, and `wii_system.c` for everything else. The two consoles
share the package platform `wii`; what only the Wii has (the Wii Remote, the console's
settings) is inside `HW_RVL` in the source. The Wii build was written against Dolphin and
has not run on a real console yet. The GameCube build compiles and has not been run at all.

## Byte order

Both consoles are big-endian, and so are their packages: the exporter writes every value in
the byte order of the platform, and the runtime uses the tables as they are, as on the
other platforms. The one file that stays little-endian is the music, because a WAV file
is; the mixer is told so.

## Graphics

- The graphics chip transforms and clips by itself. A baked vertex is the three values it
  takes for one (position, colour as `0xRRGGBBAA`, texture coordinates), so a strip is
  drawn by handing its vertices over one after the other.
- Textures are GX's own 16-bit formats, padded to powers of two, at most 1024 x 1024, and
  stored in the blocks of 4 x 4 pixels the chip reads: RGB565 for images without alpha,
  IA8 (eight bits of grey, eight of alpha) for grey images with alpha, which is what a font
  atlas is, and RGB5A3 for the rest, where every pixel is either solid with five bits a
  colour or has three bits of alpha and four bits a colour. A texture is copied once when
  its scene loads, because the chip wants it at an address that is a multiple of 32.
- Translucent surfaces are held back and drawn after the solid ones, the far ones first,
  without writing the depth, as on the PC.
- Fog is the chip's own. Depth fog is its linear fog; its exponential fog with the end at
  8 ln 2 / density is exactly Godot's `1 - exp(-density * depth)`.
- The picture is 640 x 480. On a television with 576 lines the copy to the screen stretches
  it. A Wii that is set to a 16:9 television stretches every picture to that shape; there
  the game is drawn into the middle three quarters of the picture, which gives it its own
  shape back, with black bars left and right.

## Sound, input, files

- Sound effects play on the 16 voices of libogc's mixer (ASND), with volume, panning and
  pitch. They are copied when a scene loads, for the same reason as the textures. Music is
  one more voice, fed by a thread that reads the WAV file while it plays.
- The binding codes are the buttons of a GameCube pad; Godot's joypad buttons A, B, X, Y
  and Start are the ones with those names, Back is Z, the shoulder buttons are L and R, and
  the trigger axes are how far L and R are pressed. On the Wii the first Wii Remote presses
  the same buttons: held sideways on its own (2 is A, 1 is B, A is X, B is Y, + is Start),
  upright with a Nunchuk (A, B, C is X, Z is Y, its stick is the stick), or through a
  Classic Controller. HOME leaves the program.
- Files are on the SD card, through libfat: the program is `apps/retro/boot.dol`, which
  the Homebrew Channel lists, and the packages are in `apps/retro/data/`. On the GameCube
  the card is in an SD adapter. A saved game is a file `<name>.sav` on the card.
- The language is the one the console is set to.

## What was checked

In Dolphin, set to a 4:3 television, with a build that prints its frame rate (`FPS=1`): all
demo scenes start and are drawn, the island scene looks like the PC's picture of the same
moment, the feature scene reports `features: ok`, and keys that Dolphin maps to the pad
(d-pad, stick, A) move through the menu and start a scene. Set to 16:9, the picture has its
bars and its shape. Dolphin showed 60 frames per second in all scenes; that says little
about a real console. The sound has not been listened to.
