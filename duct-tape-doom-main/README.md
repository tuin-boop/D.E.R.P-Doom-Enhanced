# Duct-Tape DOOM

An idea of how DOOM could have looked if id had kept iterating on this engine,
and if we still played on CRTs. It tries to look newer without looking modern:
light comes from lamps and windows, monsters are lit as rounded bodies, water
reflects the room, and the map hums, bubbles and echoes around you, while the
light still steps
like DOOM's light levels, shadow edges break into dither and the pixels stay
DOOM-sized.

Under the hood it is a for-fun experimental port of id Software's 1997 DOOM
source release to macOS, Linux and Windows. It keeps the original game
simulation and adds an optional GPU renderer that builds a 3D scene from the
loaded WAD while the game runs.

Everything the renderer adds is worked out from the original artwork, maps and
game state. It needs no replacement textures, models, authored lighting or
edited maps. The sound works the same way: not a single recorded sample is
added. Every sound DOOM didn't have, from a lamp's buzz to a window breaking,
is synthesized by code when the game starts, and where it plays is read from
the map. DOOM should feel more physically coherent while staying the same
game.

## Why this exists

This started as a visual experiment to see how far DOOM's presentation could
be pushed while it still looks and plays like DOOM. I think of it as an art
project. Effects stay or go based on how they look in-game, and the code is
written for trying ideas out quickly. It is not meant to be a reference engine
or a library to build on, and the "duct tape" in the name is accurate.

To fit the idea at the top, most of the effects are fake-modern: cheap tricks
of the kind I liked in late-90s games, not the techniques a modern engine
would use. They work from the WAD data alone, so ordinary vanilla and
limit-removing WADs get them without their authors preparing anything. I'd be
glad if some of these ideas end up in other projects.

Claude writes much of the code. I decide what the renderer should do and how
it should look, test the results in-game and keep iterating until they match
what I had in mind. Without that workflow I wouldn't have the spare time for a
project like this.

The defaults are how I like to play. Almost everything can be changed in the
graphics panel with **F4**.

## At a glance

- **Accelerated 3D renderer:** SDL_gpu, using Metal on macOS and Vulkan on
  Linux and Windows.
- **Native resolution and any aspect ratio,** with mouse look, interpolated
  movement and optional MSAA.
- **Classic renderer:** the original 320×200 software view is one keypress
  away.
- **No asset pack:** the WAD's own textures, flats, sprites and palettes are
  the source material, and there are no added sound files either.
- **Compatibility:** vanilla and limit-removing maps, including DEHACKED
  patches.
- **Not supported:** Boom, MBF21 and GZDoom maps, slopes, 3D floors, model
  packs, ray tracing and multiplayer.
- **Window glass:** windows found in the map get glass that reflects the room
  and shatters when something goes through it.
- **Lights can be shot out:** lamps and screens break when shot or caught in a
  blast, and the room goes dark with them.
- **Blinking lights:** lamps in flickering sectors blink, strobe and pulse with
  DOOM's light effects, and the baked light they throw follows them.
- **Room acoustics:** echoes and reverb measured from the map, with sounds
  finding their way round corners, through doors and windows, and dulled by
  distance.
- **Sounds DOOM never had, synthesized:** sparks, dying lamps, breaking
  glass, footsteps, and the hum, bubble and wind of the map around you. None
  of them is a recording; small synths written for each sound generate them
  at startup in DOOM's own 8-bit style.
- **Music synths of its own:** a SoundFont synth with the bundled GeneralUser
  GS bank, an OPL FM synth written from the chip's manual, or the Mac's system
  synth, plus an offline renderer for comparing them.
- **Settings panel:** **F4** switches the individual graphics and sound effects
  on and off and tunes them.
- A headless demo runner, regression tests and a GPU performance smoke test
  for development.

The Mac build gets the most testing. The Linux and Windows builds compile in CI
but have had less real play.

## Download and play

Ready-to-play builds for macOS, Windows and Linux are on the
[Releases page](https://github.com/robongrui/duct-tape-doom/releases). Each one
includes Freedoom, so it plays straight away.

On Windows and Linux, put your own `.wad` files in the `wads` folder next to
the game. On the Mac, choose your WAD folder when the app starts.

[PLAY.md](PLAY.md) has the controls and a description of the graphics and
sound options.

The ambience is quiet by design and the music covers it. For the soundscape,
turn the music down or off in **Options → Sound Volume**.

## Screenshots

<p>
<img src="screens/stage-room.jpg" width="49%" alt="A big red-lit room with a blue stage and wall screens" />
<img src="screens/stage-room-monsters.jpg" width="49%" alt="Zombies coming off the blue stage" />
<img src="screens/computer-pool.jpg" width="49%" alt="A computer room around a pool of water" />
<img src="screens/blue-corridor.jpg" width="49%" alt="A blue-lit corridor with a green room at the end" />
</p>

## How the graphics cheat

### Light comes from things that look like lights

DOOM hints at its lighting through sector brightness, glowing textures and
flats, decorations, windows and the shapes of rooms. The renderer tries to turn
those hints into actual light sources.

Lamp strips, computer panels, lava and other glowing artwork are recognised by
texture name and palette color. Their bright pixels become glow masks and
approximate light sources. Wall panels shine mostly forward, so they don't
light through their own wall. Computer walls that are only dotted with a few
LEDs keep their glowing pixels but light the room only as much as those
LEDs would, so a wall of tech panels doesn't turn a room red.

A lamp lights as strongly as its sector's light level says. The level is the
mapper's stand-in for the lamp's brightness. A strip set only a notch above its
room was meant as a dim fixture, so it lights little: the strength rises with
the square of the level, from nothing at 96 to full at 208.

Windows and other gaps open to the sky become broad sky-colored lights. With
sun lighting on, windows that look out onto sunlit areas give more light than
windows facing shade. The light from several windows is capped together so a
room can't get implausibly brighter than outdoors.

When lamps, screens and windows account for most of a room's brightness, the
renderer takes away part of DOOM's flat sector light and lets those sources put
it back. Light then seems to come from somewhere, and far corners can fall
darker.

<p>
<img src="screens/light-red-panel.jpg" width="49%" alt="A red wall panel lighting a dark room" />
<img src="screens/light-room-window.jpg" width="49%" alt="A room lit by a ceiling lamp, a red door and a window" />
</p>

See
[`platform/scene3d/bake_lights.cpp`](platform/scene3d/bake_lights.cpp),
[`platform/emissive.h`](platform/emissive.h) and
[`platform/surface_lighting.h`](platform/surface_lighting.h).

### Static light is baked when a level loads

The bake works on DOOM's 2D map and is much simpler than a modern offline
lightmap. It gathers light from lamps, glowing textures and liquids,
decorations, windows and the sky, with walls and closed portals blocking it.
It adds one bounce, sky fill, sun shadows, ambient occlusion in corners and
under ledges, and shadows from decorations such as columns, trees and hanging
bodies.

The result can be snapped to the texture grid, so the light changes in steps
of DOOM texels.

Doors, lifts and lowering walls re-light the area around them as they move, so
light spreads under an opening while it opens.

The bake keeps more than a color. With bake-only lights (see below), each wall
texel and floor or ceiling cell also stores the direction most of its light
comes from, how much comes from that direction, and which flickering lamp
dominates it. The flicker part is stored in every mode as soon as a map has
lamps in flickering sectors (see the blinking lights below). Torches, candles
and other animated decorations are sorted into flicker groups that step like
DOOM's flickering sectors. The shaders read
the direction to light bump, gloss and the monsters' rounded shading, and they
step each texel with its group's flicker. A torch-lit wall keeps flickering
with its torch, and its seams still catch the light from the right side, even
though the torch is no longer a live light.

![A corridor with baked light falling darker into the corners](screens/baked-corridor.jpg)

See
[`platform/baked_lighting.h`](platform/baked_lighting.h) and
[`platform/scene3d/relight.cpp`](platform/scene3d/relight.cpp).

### Bump and gloss come from the original pixels

World textures need no normal maps or material definitions. The renderer reads
surface relief from the artwork. Broad brightness structure becomes panel
seams, mortar and grooves, and small noisy changes are softened, so a riveted
ceiling catches light along its seams while painted specks stay flat.

The palette ramps give a rough material guess. Greys and blues lean toward
metal and greens toward wet or polished surfaces. Browns, flesh and most stone
colors stay fairly matte. With varied highlights switched on, smooth artwork
gets tighter reflections than busy artwork.

It is a rough guess meant to make the existing art react to light. There is no
attempt at physically based materials.

![Metal walls catching light along their panels](screens/gloss-metal-walls.jpg)

See
[`platform/scene3d/images.cpp`](platform/scene3d/images.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### The weapon sprite is de-lit and lit again

The weapon is still the original 2D sprite, with no model behind it.

Before drawing it, the renderer removes part of the broad light and painted
sheen in the artwork and keeps the outlines, detail and the palette's color
ramps. It builds rounded normals from the silhouette and painted shading, and a
gloss map from highlights and metal-like colors.

The shader lights the sprite from lamps, muzzle flashes and the room around the
player. Six directional probes stand in for the surroundings, so bright walls
and windows show up on glossy metal and the top of a barrel can catch a lamp
ahead.

See
[`platform/weapon_lighting.h`](platform/weapon_lighting.h),
[`platform/scene3d/weapon.cpp`](platform/scene3d/weapon.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### Monsters are lit as rounded bodies

When a sprite loads, each pixel of its silhouette rises as a tube as wide as
the largest disk that fits inside the outline at that point. The torso becomes
a broad cylinder, and arms, legs and guns become thinner ones. Light falls on
that shape, and the painted shading stays visible on top.

<p>
<img src="screens/monster-blue-light.jpg" width="49%" alt="A shotgun guy lit as a rounded body in blue light" />
<img src="screens/monsters-red-light.jpg" width="49%" alt="Zombies lit red by the lamps of a big room" />
</p>

Monsters' eyes glow in the dark and get a little bloom: the zombies' and
imp's red, the demon's yellow, the cacodemon's and mancubus's green, the
arachnotron's blue. For the stock DOOM and DOOM II monsters the glowing pixels
are picked by hand, frame by frame, so a monster turned away or squeezing its
eyes shut in pain shows none. Each entry only holds for the original artwork:
a replaced sprite with other pixels falls back to a search for eyes painted in
a color the body around them never uses.

See [`platform/scene3d/sprite_normals.cpp`](platform/scene3d/sprite_normals.cpp),
[`platform/sprite_eyes_stock.h`](platform/sprite_eyes_stock.h) and
[`platform/sprite_eyes.h`](platform/sprite_eyes.h).

### Per-frame light work can move into the bake

By default the bake is only part of the picture. Lamps near the player also
run as live lights for flicker, bump and gloss detail, and every monster and
item traces rays to the baked lights each frame. Those rays find the
static light's color, the direction most of it comes from, its flicker group
and the lamp the thing's shadow should fall from.

Two switches under **Performance** in F4 move that work to level load. They
are off by default and meant to look nearly the same:

- **Bake-only static lights:** lamps and glowing textures stop being live
  lights. Their flicker, bump detail and gloss come from the direction and
  flicker group stored in the bake, and lava and nukage pools each glow as a
  single light. Shots, explosions and the flashlight stay live.
- **Grid lighting for monsters:** the traces are replaced by a grid baked
  after the lights: one sample every 32 units, a second one higher up in tall
  rooms, blended between the nearest cells. Monsters keep their per-pixel
  rounded shading, which now gets cheaper inputs.

See
[`platform/scene3d/bake_lights.cpp`](platform/scene3d/bake_lights.cpp),
[`platform/scene3d/thing_light.cpp`](platform/scene3d/thing_light.cpp) and
[`platform/scene3d/lights.cpp`](platform/scene3d/lights.cpp).

### Shots and explosions light in their own colors

The color of a dynamic light is sampled from the sprite that makes it. Bright
saturated pixels count, and transparent outlines and white cores are ignored.
A plasma shot casts blue light because the plasma sprite is blue. The same goes
for weapon flashes, enemy attacks and glowing projectiles. Walls, doors, raised
floors and other map geometry block this light.

![A rocket explosion lighting a zombie and the floor red](screens/rocket-light.jpg)

See [`platform/flash_lighting.h`](platform/flash_lighting.h).

### The flashlight sits at the shoulder

The flashlight is mounted at the player's right shoulder, a little behind the
eyes as in Doom 3. It lights the floor ahead from above, and wall edges, door
frames and things cast visible shadows. It pulls back out of walls the player
is pressed against and sways with DOOM's weapon bob.

The classic beam, which is the default, brightens in DOOM's light-level steps.
With the classic option off, the beam bounces a soft light in the color of
whatever it hits, so a lit wall spills onto the dark floor in front of it.

Dynamic light also pushes saturated colors toward its own hue, the way the
palette ramps brighten, so dark blue carpet shows the beam. The baked light of
lamps does the same, so a lamp's pool shows on a saturated floor as clearly as
on the grey one next to it. What dynamic light adds rolls off on bright
texels, so a lit wall doesn't get far ahead of the dark floor next to it.

See [`platform/scene3d/lights.cpp`](platform/scene3d/lights.cpp) and
[`platform/scene3d/exposure.cpp`](platform/scene3d/exposure.cpp).

### Flashlight shadows have stippled edges

The flashlight's shadows have dithered edges by default. This is a deliberate
choice: a lamp that is a single point casts perfectly sharp shadows, and those
read as too sharp, closer to a modern renderer than to DOOM. Its beam already
brightens in hard steps, so its shadows step too. They stay hard, but each edge
breaks into a coarse ordered dither in four steps: lit, sparse dots,
checkerboard, dense dots, shadow. On a CRT, the dither blurs into something
like a soft shadow.

The edge is about a pixel wide where a wall meets its own shadow and widens the
farther the shadow falls behind it, as a real lamp's penumbra (soft edge band)
does. Each pixel still casts one ray, so it costs nothing extra. The pattern is
pinned to the walls and floors instead of the screen, so it holds still while
the swaying beam's edge moves over it. Monster silhouettes thrown on walls get
the same dither, laid on the sprite's own pixels.

To get the sharp shadows back, turn off **Stippled shadow edges** in the F4
menu under Flashlight and shadows.

See `flashStipple` in [`platform/shaders/lighting.glsl`](platform/shaders/lighting.glsl)
and [`platform/shaders/shadow.frag`](platform/shaders/shadow.frag).

### Shadows are squashed sprites

A monster's shadow is its current animation frame flattened onto the floor as a
dark translucent decal, clipped to the sector's floor polygons. It spreads and
fades when the monster rises into the air. Monsters get a second flattened
shadow pointing away from the strongest dynamic light nearby.

The flashlight can throw sprite silhouettes onto walls, with stippled edges by
default (see above), and the
player can cast a shadow from the sun or the strongest suitable light.

See [`platform/scene3d/shadows.cpp`](platform/scene3d/shadows.cpp).

### Monitor textures get fake curved CRT glass

Computer textures are searched for dark rectangular screens framed by lighter
housing. Vent grilles and similar shapes are rejected.

No geometry is added. The world shader puts a curved normal over the flat
texture, bends the picture a little like a weak lens, darkens the rim and lets
dynamic lights slide across it as a highlight on the glass.

![A computer screen with curved glass next to two zombies](screens/crt-screen.jpg)

See [`platform/screen_glass.h`](platform/screen_glass.h).

### Water reflects the room

When water is visible near the player, the nearest visible water level becomes
a mirror plane and the world is drawn again at half resolution from a camera
flipped across it. The water shader distorts that image with simple waves and
with the brightness changes in the animated water flat, so DOOM's own texels
break up the reflection. Nukage, slime, blood and lava are left without
reflections on purpose.

The optional retro reflection mode draws the mirror at one-eighth resolution
with nearest-neighbour pixels and maps the result back to the WAD palette.

![A zombie wading through water that reflects the room](screens/water-reflection.jpg)

See
[`platform/gpu3d/frame.cpp`](platform/gpu3d/frame.cpp),
[`platform/scene3d/geometry.cpp`](platform/scene3d/geometry.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### Liquids affect what's around them

Liquid flats can throw moving caustics onto nearby walls and, optionally,
ceilings. The pattern is taken from the level's own liquid artwork. Banks just
above liquids that aren't hot can darken as if damp.

Bullets hitting liquid throw palette-colored drops and expanding rings, and
things landing or wading in it disturb it too. Nukage, slime, blood and lava
act heavier, with slower rings, bigger drops, bubbles and a wobbling collar
around anything standing in them. Water is lighter and quicker. The particles
are chunky DOOM-sized blocks.

<p>
<img src="screens/nukage-caustics.jpg" width="49%" alt="Nukage glowing and throwing caustics on the wall" />
<img src="screens/nukage-room.jpg" width="49%" alt="A room with nukage pools and zombies" />
</p>

See [`platform/scene3d/liquids.cpp`](platform/scene3d/liquids.cpp),
[`platform/scene3d/caustics.cpp`](platform/scene3d/caustics.cpp) and
[`platform/shaders/world.frag`](platform/shaders/world.frag).

### Windows are found in the map and glazed

DOOM maps have plenty of windows but no glass. When a level loads, the renderer
looks for them in the geometry: a thin sector between two others, with a sill
and a lintel, that the player can't walk into. Lifts, doors and anything a
switch moves are skipped. The DOOM episodes have about a hundred of these
windows, DOOM II about eighty.

Each window gets a pane over the opening. It is mostly clear, with grime
collected at the frame and on the sill, a few shine streaks painted on the way
Duke Nukem 3D did its glass, and sharp highlights from lamps, muzzle flashes and
the flashlight.

The game code doesn't know the glass is there. Shots, sight and movement pass
through exactly as in vanilla, so the renderer watches for anything crossing a
window. A render-only hook reports hitscan shots crossing a line, and
projectiles and explosions are checked every tic. The first thing through
shatters the window into palette-colored shards that fall, bounce and lie on
the floor for a while, leaving a jagged rim in the frame. Monster shots count
too, so a fight across a window breaks it. Once it is broken, the wind and
other sounds from outside come through it clearly (see the sound section).

<p>
<img src="screens/window-intact.jpg" width="49%" alt="An intact window with painted shine streaks" />
<img src="screens/window-shattered.jpg" width="49%" alt="The same window after it shattered" />
</p>

The window the player is looking at reflects with the same mirror trick as the
water, so walls, lamps, monsters, shots and the player's own sprite all appear
in it. Sprites are turned around for the mirrored view, so a monster coming up
from behind shows its face in the glass.

Every other window shows a snapshot: a small cube map taken in front of it
after the level's lighting has baked, holding only the map. The reflected ray
is projected onto a box around the room, so nearby walls stay put as the player
moves. When the view turns to another window, it fades from its snapshot to a
live reflection.

How strongly the reflection shows depends on contrast. Bright things in it are
visible and dim walls stay faint, so the window stays see-through.

<p>
<img src="screens/window-zombies.jpg" width="49%" alt="A window with zombies behind the glass" />
<img src="screens/windows-blue-pool.jpg" width="49%" alt="Two glazed windows above a pool of water" />
</p>

See [`platform/scene3d/windows.cpp`](platform/scene3d/windows.cpp) and
[`platform/shaders/glass.frag`](platform/shaders/glass.frag).

### Lights can be shot out

Every lamp, light strip and screen that lights the room can be shot out. The
renderer already knows which pixels of a texture glow and which light each of
them feeds, so a shot that lands on the glowing part breaks the fixture. A
fixture is the glowing surface that was hit: a lamp strip spread over a few
small sectors breaks as one, while a long lit ceiling or a row of computer
boxes breaks a piece at a time. Lamps forgive a near miss, so a shot into the
dark middle of a ring light still counts; screens need a hit on the glass.

A broken fixture sputters for a moment and goes dark: tubes turn grey and
dead, and a screen loses its curved glass to cracks over a black tube. A lamp
sputters out with a pop, a screen dies in a burst of static.

The light goes with it. The fixture is taken out of the baked lighting, and the
floors, walls, monsters and items it lit are baked again around it on a
background thread. Where the bake had merged it with neighbouring fixtures into
one light, that light is rebuilt from the ones still burning. DOOM also lights
a lamp-lit room by its sector light level, so the room's level drops by the
share of its light that fixture gave. A room lit only by its lamps ends up
close to pitch black.

Bullets, fireballs and plasma break what they hit; rockets and exploding
barrels break every lamp and screen their blast can see, while lava and nukage
glow on. Monsters' shots count too, and a bullet that kills its target glances
off and carries on to the wall behind it, a little off its line, so the fight
itself wears the light down. Tech walls crackle and throw blue sparks when
hit.

Like the window glass, all of this happens in the renderer only. The game
doesn't know the lights are gone: monsters see as they always did, and loading
a save brings the lights back.

There is no screenshot of this on purpose. It's better found in a fight.

See [`platform/scene3d/shot_lights.cpp`](platform/scene3d/shot_lights.cpp).

### Blinking and pulsing lights came back

DOOM flickers a light by changing a sector's light level. A thinker makes it
blink at random, strobe, glow up and down, or flicker like fire. In DOOM the
lamp in that sector flickers along for free, because every pixel is drawn at
its sector's brightness. Once lamps became light sources here, that broke.
Their glowing pixels are drawn at full brightness, and the light they throw is
baked when the level loads. Every lamp shone steadily, and the flicker shrank to
a faint pulse of the sector light. A lot of DOOM's mood turned out to live in
those blinking lights.

Getting them back took several pieces:

- **Reading the swing.** Each flickering sector's light thinker says how far
  its level swings. Lamps in that sector follow where the level sits within it.
  Blinking and strobing sectors switch their lamps off in the lower half of the
  swing. Glowing and fire-flickering sectors fade them up and down.
- **Keeping the light baked.** One live light per lamp would cost frame time
  and lose the bake's soft, stepped shadows. A first try with live lights cast
  razor-sharp ones. Instead, each flickering sector's lamps get a flicker
  group, the same mechanism that makes torch-lit walls flicker with bake-only
  lights. Every wall texel and floor cell records which group lights it most
  and what share of its light that is, and the shader scales that share every
  frame. There are 15 groups. Sectors at least 1024 units apart can share one,
  and the one nearest the player decides how it flickers.
- **Switching each cell before blending.** The light blends the four nearest
  bake cells, and neighbours can belong to different groups. Blending first
  and switching after left blocks of lamp light glowing on when the lamp was
  out. Each cell is now switched by its own group before the blend, and the
  bounce light switches with it.
- **The shot-out look.** A switched-off tube is drawn exactly like one that's
  been shot out: grey and dead. A glowing sector fades its lamp's glowing pixels
  instead.
- **DOOM's level with the lamps out.** Rooms whose light comes mostly from
  their lamps get part of their sector light taken away, so the lamps can put
  it back with direction. That used to count flickering lamps too, so a room
  went near black whenever they went out. Now only steady lamps count. A lamp's
  own sector sits at the bottom of its swing, and the baked lamp light carries
  the rest. With the lamps out, a room looks as DOOM has it; with them on, it
  is lit from the lamps.

Shoot out every lamp of a flickering room and it stops flickering: nothing is
left to carry the swing.

See [`platform/scene3d/surface_lights.cpp`](platform/scene3d/surface_lights.cpp),
[`platform/scene3d/light_swings.c`](platform/scene3d/light_swings.c),
[`platform/scene3d/bake_lights.cpp`](platform/scene3d/bake_lights.cpp) and
[`platform/shaders/world/bake.glsl`](platform/shaders/world/bake.glsl).

### Fog, mist and sunbeams are simple fakes

Distance fog is an exponential formula with no raymarching. Pits, liquids and
damaging floors can carry low cloud-like layers of mist. Sunlit openings can
cast simple light shafts back toward the sun, with dust motes showing only
where the sun or flashlight catches them. Hot floors and flames create small
heat volumes, which a screen pass turns into a sideways shimmer that steps with
the game tics.

The sun's position is inferred from the level's sky lighting. It can be drawn
in the sky, optionally with forward scattering.

![Sunlight falling into a grey stone room](screens/sunbeam.jpg)

See
[`platform/volumetric_fog.h`](platform/volumetric_fog.h),
[`platform/scene3d/atmosphere.cpp`](platform/scene3d/atmosphere.cpp),
[`platform/shaders/mist.frag`](platform/shaders/mist.frag) and
[`platform/shaders/present.frag`](platform/shaders/present.frag).

### Blood is made from the WAD too

Hitscan attacks can leave splats on floors and walls, and corpses slowly grow
pools beneath them. The decals are generated at runtime from colors sampled
from the WAD's blood sprites. Fresh floor blood catches dynamic highlights and
then dries darker and matte. Pools grow in visible steps.

Bullet impacts leave marks as well and throw small chunks colored like the
surface they hit.

See [`platform/scene3d/blood.cpp`](platform/scene3d/blood.cpp) and
[`platform/scene3d/impacts.cpp`](platform/scene3d/impacts.cpp).

### Pixel art stays sharp

The renderer draws at the display's native resolution and can keep the
textures sharp. World textures can use nearest filtering, plain bilinear, or a
sharp-bilinear mode that keeps the middle of each texel flat and softens only
its edge. Palette-aware mipmaps reduce shimmer in the distance.

Sprites can stay nearest-neighbour or use soft bilinear filtering.

An optional final pass quantizes the whole image back to the current WAD
palette, including lighting, fog and bloom.

Animated walls and flats crossfade between their frames. Doors, lifts, sprites
and the camera are interpolated between DOOM's 35 Hz game tics.

## How the sound cheats

Sound gets the same treatment as the graphics, and it is as big a part of the
project. The WAD's own effects still play as crunchy 8-bit samples, and
everything around them is worked out from the map while you play: where sound
can travel, how big and how hard the room is, and what in it should be making
a noise.

No sound files are dropped in. The WAD has nothing for sparks, breaking glass,
footsteps, a humming lamp or bubbling nukage, and instead of adding recordings
the game generates them. Each one is a small synth that models how the thing
makes its sound: a ballast buzzing at twice the mains frequency, bubbles
rising in pitch as they near the surface, a crack front branching through a
pane, shards falling under DOOM's own gravity and ticking where they land.
At startup, in a fraction of a second, they are all rendered in the same
8-bit, 11 kHz form as DOOM's own effects, so they sit in the mix as if they had
always been there. They are the same in every WAD, and which of them plays
where is read from the map's textures.

The target is a late-90s A3D or EAX sound card, with a fairly short and lo-fi
reverb. The settings are in the **Sound** tab of the F4 panel.

### Quiet by design, so turn the music down

The soundscape is quiet on purpose. A lamp's buzz, distant bubbling and wind
coming through a broken window are meant to be heard under the gunfire, and
DOOM's music, mixed loud and played dry, covers them. With the music at its
usual level, most of this section goes unheard. To hear it, turn the music
down or off in **Options → Sound Volume → Music Volume**. The music synths
below are kept for playing the way DOOM is usually played.

### Sound finds its way through the map

When a level loads, every two-sided line becomes an opening between two
sectors. Sound travels from opening to opening, and one search outward from
the player finds, for each opening in reach, how far the way there is and what
it lost on the way. The search runs again whenever the player moves or a few
tics pass, and openings are read live, so a door that opens, a lift that rises
or a window that breaks changes what gets through as it happens.

Distance and loss are kept apart. A closed door, a narrow slit or a window's
glass makes a sound quieter and duller, but not farther away. Sectors are only
topology here, so a staircase of twenty sectors counts as one way, not twenty
rooms.

### Walls muffle, distance dulls

Each positioned sound has its own two-stage lowpass filter. The cutoff is set
when the sound starts and updated every other tic while the sound or the player
moves.

- **In sight:** only distance removes treble, from about 200 units out to
  DOOM's clipping distance.
- **In the same room, out of sight:** over a ledge or behind a pillar, it is
  muffled a little.
- **Elsewhere:** it is heard the way the openings let it through. The longer
  the way round compared to the straight line, the duller: a way half again as
  long as straight is as dull as a corner gets. A shut door, a slit or a pane
  of glass on the way dulls it further.
- **Sealed off:** with no way through at all, it is muffled heavily.

Muffling only changes the tone. Volume is the same as in vanilla DOOM, so you
can locate monsters by ear as well as before.

### Rooms are measured with rays

Every other tic, 32 rays go out from the player across the map in a flat
circle. A ray stops at a one-sided wall or at an opening too low for sound,
such as a closed door or a raised lift. Rays that leave through open sky don't
come back.

The rays give the distance to the walls and also sketch the floor plan. That
plan, the ceiling heights the rays crossed and how much of it is open sky give
a room volume for Sabine's reverberation formula. The result is squeezed into a
game-like range: corridors slap back, big halls boom and outdoors stays almost
dry.

WADs have no material data, so materials are guessed from texture names.
`STARTAN`, `COMP` and `METAL` ring like metal, `WOOD` and the panel textures
sound dull, and flesh, liquids and dirt each absorb sound differently. Anything
else counts as stone.

### Echoes and a reverb tail

The rays are grouped into eight directions around the player. Each group makes
one echo, delayed by the round trip to its walls and panned by where they are
relative to the way the player faces. A ninth echo comes off the ceiling.

A small feedback delay network makes the late tail: eight damped delay lines
mixed through a Householder matrix. Metal rooms keep their treble and wooden
ones lose it. When the room changes, the echoes crossfade and the tail glides
over a third of a second, so walking through a door doesn't click.

Far sounds send more to the reverb relative to the dry mix, so they sound
further away as well as quieter.

### The map hums

When a level starts, its textures and things are read for what should make a
sound:

- **Lamps** buzz like a ballast. A steady lamp is barely heard, but one in a
  flickering sector buzzes with its light, cuts out when it drops and ticks
  and crackles as the tube strikes again.
- **Screens** hum under faint static, and **computer walls** chatter in little
  bleeping tunes.
- **Nukage and slime** bubble thick and low, with a big blorp now and then.
  **Blood** bubbles slower and deeper, **slime falls** pour, and DOOM's
  **water**, which mostly stands still, drips now and then.
- **Lava** rumbles and heaves up heavy bubbles, **fire walls**, torches and
  burning barrels roar and crackle, and cracked **magma** rock only ticks over
  a faint rumble. **Teleporter pads** throb.
- **Wind** blows wherever the sky is open, and comes indoors through the
  windows and doorways that open onto it.

Sources of a kind close together join into one spot, and each spot is heard
through the same openings as any other sound: round corners it is farther,
through a shut door duller, and it comes from the direction of the opening it
arrives by. Window glass holds the wind back to a dull rush until the glass is
shot. Each kind plays in up to three layers with different takes of its loop,
so a room full of computers sounds fuller rather than just louder. A faint room
tone runs under it all.

Only nearby sources are heard, loud effects push the ambience down for a
moment, and a fixture that is shot out goes quiet for the rest of the level.
**Ambience volume** sets how loud it is next to the effects.

### Sounds DOOM never had

The rest of the made-up sounds are effects that answer what happens in the
fight. Each comes in six variations, and the same one never plays twice in a
row.

- **Sparks:** tech walls crackle, fizz or zap after the gunshot that hit them.
- **Dying lights:** a lamp's hum sputters into a pop and a few bits of tube; a
  screen bursts into static and thumps off.
- **Breaking glass:** each shard falls from its place in the pane under DOOM's
  gravity and ticks where it lands, with a longer break for big windows.
  **Breaking glass** picks how the break itself sounds: **Shatter**, a hard
  crack and a rush of splinters, or **Fracture**, a crack front branching
  through the pane as hundreds of tiny clicks, with no noise in it at all.
- **Footsteps:** the player's steps sound by what the floor is, stone, metal,
  wood or something soft like grass and carpet, about twice a second walking
  and three times running. Water splashes, nukage, slime, blood and lava gloop,
  and monsters wading through liquids splash too. Monsters on dry floors stay
  as silent as ever, so hearing tells no more about where they are than before.

`doom-sfx-render` writes all of them to WAV files, dry and in rooms, for
listening outside the game:

```sh
build/play/doom-sfx-render freedoom1.wad -kind ambient -room room
```

### Samples get treble they never had

DOOM's effects are mostly 11 kHz samples, so they hold nothing above about
5.5 kHz. Played back at 44.1 kHz with each sample held, as vanilla does, the
gap fills with mirror images of the sound. That sizzle is most of the crack in
the pistol, but it rings metallic because it turns the sound's spectrum upside
down. **Sample playback** in the Sound tab offers three ways to read between
the samples, for the WAD's effects and the made-up ones alike:

- **Original** (the default) holds each sample: raw, gritty and sizzling.
- **Smooth** draws a cubic curve through the samples and keeps a little of the
  sizzle.
- **Clean** uses a windowed-sinc filter and removes the sizzle entirely.

Smooth and Clean on their own sound muffled, because the sizzle was the
treble. So they make up new treble as the sound plays. The top octave the
sample does have is rectified, which doubles its frequencies: tones gain their
own overtones, and noise, which is most of a shot or a blast, stays noise, only
higher. A second pass adds air above 11 kHz. Only what lands above the
sample's own band is mixed in, so the recording itself comes through
untouched. The extra treble follows the sound's pitch and goes in before the
muffling, so a sound behind a wall still loses it.

The **Made-up treble** slider sets how much. At 100% it is about as bright as
Original, without the metallic ring. Rare spikes are rounded off so they don't
set off the limiter.

The mixer uses DOOM's 0–15 effects volume scale and DMX-style panning. A peak
limiter briefly ducks the mix when many shots fire at once, so it doesn't
crackle.

### Music plays on synths of its own

DOOM's scores are MUS and MIDI files, and how they sound depends on the synth
that plays them. Bobby Prince wrote them on a Roland Sound Canvas. Three
synths are available, picked under **Music** in the Sound tab. The first two
are built in: both read MUS and MIDI into one list of timed events and render
it in the game's own mixer.

- **SoundFont** (the default): the bundled GeneralUser GS bank through
  TinySoundFont, restrained and close to the Sound Canvas. A `.sf2` bank in the
  `soundfonts` folder beside `doom.cfg` replaces it.
- **OPL FM:** an emulated YMF262 chip playing the WAD's own GENMIDI
  instruments, the way DOS DOOM sounded on Sound Blaster cards. The voice
  handling was written from the chip's manual and the published lump formats
  alone; [`opl_music_spec.md`](platform/audio/opl_music_spec.md) documents it.
- **System synth** (Mac only): Apple's built-in General MIDI synthesizer.

`doom-music-render` renders a WAD's songs to WAV files without starting the
game, so synths and banks can be compared by ear:

```sh
build/play/doom-music-render freedoom1.wad -synth sf2 -song D_E1M1
```

See
[`platform/audio/acoustic_graph.c`](platform/audio/acoustic_graph.c),
[`platform/audio/occlusion.c`](platform/audio/occlusion.c),
[`platform/audio/room.c`](platform/audio/room.c),
[`platform/audio/reverb.c`](platform/audio/reverb.c),
[`platform/audio/ambience_map.c`](platform/audio/ambience_map.c),
[`platform/audio/ambience.c`](platform/audio/ambience.c),
[`platform/audio/made_bank.c`](platform/audio/made_bank.c),
[`platform/audio/steps.c`](platform/audio/steps.c),
the synths in [`platform/audio/`](platform/audio/) (`*_synth.c`),
[`platform/audio/resample.c`](platform/audio/resample.c),
[`platform/audio/treble.c`](platform/audio/treble.c),
[`platform/i_sdl_sound.c`](platform/i_sdl_sound.c),
[`platform/i_music.c`](platform/i_music.c) and
[`platform/audio/mixer_music.c`](platform/audio/mixer_music.c).

## Game data

This repository contains **no commercial game data**. To build and play from
source you need an IWAD:

- **Freedoom** (free): download `freedoom1.wad` or `freedoom2.wad` from
  [freedoom.github.io](https://freedoom.github.io/).
- **Original DOOM / DOOM II** (commercial): use the `.wad` files from a copy
  you own, for example from Steam or GOG.

Please don't commit or upload commercial WAD files to this repository.

## Build

You need CMake 3.21+ and a C11/C++17 compiler (Apple Clang, Clang, GCC or
Visual Studio 2022). SDL3 is downloaded automatically if it isn't installed.

```sh
cmake -S . -B build/play -DCMAKE_BUILD_TYPE=Release -DDOOM_BUILD_SDL=ON \
  -DDOOM_GAME_DATA="/absolute/path/to/freedoom1.wad"
cmake --build build/play --parallel
ctest --test-dir build/play --output-on-failure
```

## License

Copyright © 2026 Robert Heidenreich. Licensed under the GNU General Public
License version 3 ([`LICENSE.TXT`](LICENSE.TXT)), with one additional term
under section 7(b): redistributions and derivative works containing the
original Duct-Tape DOOM contributions must preserve the attribution
"Duct-Tape DOOM — https://github.com/robongrui/duct-tape-doom". The full
notice is in [`NOTICE.txt`](NOTICE.txt).

The DOOM source by id Software, which this is built on, is GPL version 2 or
later. [`THIRD_PARTY.md`](THIRD_PARTY.md) lists it and the libraries and data
that come from elsewhere, with their licenses.
