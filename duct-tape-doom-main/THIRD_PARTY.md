# Third-party code and data

Duct-Tape DOOM is licensed under the GNU General Public License, version 3
([`LICENSE.TXT`](LICENSE.TXT)), with the attribution term in
[`NOTICE.txt`](NOTICE.txt). This file lists everything in the game that
comes from elsewhere, and its license.

| Part | Where | License |
| --- | --- | --- |
| DOOM source code, id Software | `linuxdoom-1.10/`, the base of the engine | GPL 2 or later, used here under GPL 3 |
| [SDL](https://libsdl.org/) 3.4.18 | downloaded by `CMakeLists.txt`, linked into the program | zlib, with the notices in [`third_party/SDL3-NOTICES.txt`](third_party/SDL3-NOTICES.txt) |
| [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3), Nuke.YKT | downloaded by `CMakeLists.txt`, linked into the program | GNU LGPL 2.1 or later |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont), Bernhard Schelling | downloaded by `CMakeLists.txt`, linked into the program | MIT |
| [GeneralUser GS](https://schristiancollins.com/generaluser.php) 2.0.3, S. Christian Collins | SoundFont downloaded by `CMakeLists.txt`, shipped in `soundfonts/` | its own license, allowing use in software ([`soundfonts/GeneralUser-GS-LICENSE.txt`](https://github.com/mrbumpy409/GeneralUser-GS/blob/main/documentation/LICENSE.txt)) |
| [Freedoom](https://freedoom.github.io/) 0.13.0 | game data in the downloads only, not in this repository | BSD 3-Clause |

**DOOM source.** id Software released the Linux DOOM source under the GPL on
3 October 1999, "either version 2 of the License, or (at your option) any
later version" (`doomsrc.zip` in the idgames archive, `source/`). This copy
started from id's GitHub repository, whose headers still named the 1997
"DOOM Source Code License"; they now carry the 1999 notice again.

**SDL, Nuked OPL3, TinySoundFont and GeneralUser GS** are not in this
repository; CMake downloads the pinned versions while building. Each release
has a `DuctTapeDOOM-source.tar.gz` with this repository and the exact library
sources and SoundFont the downloads were built from, so the game, with or
without changes to those libraries, can be rebuilt without an internet
connection (see `dependencies/README.txt` inside it).

**Freedoom** is bundled as the default game. Its license and credits are in
the downloads' `licenses/Freedoom` folder. The game reads every other graphic,
font, sound and piece of music from the WAD you play; GeneralUser GS only
supplies instrument sounds for the SoundFont music device.

**Hash functions.** A few small integer hashes use published, public-domain
constructions: Chris Wellons' `lowbias32` and a two-round mixer from his
[hash-prospector](https://github.com/skeeto/hash-prospector) (Unlicense), and
the final mixing step of Austin Appleby's MurmurHash2 (public domain), plus
well-known multipliers from the literature (Knuth's multiplicative hash,
Teschner et al.'s spatial-hash primes, the Numerical Recipes generator).

**File formats.** The WAD, DeHackEd, BEX, MUS, MIDI and GENMIDI readers are
written from the format descriptions. Names those formats define, such as
DeHackEd's "Hit points" or BEX's `GOTARMOR`, appear exactly as patch files
spell them. The OPL music player is built from the Yamaha YMF262 register
manual and the Doom Wiki's GENMIDI and MUS pages; its behaviour is written up
in [`platform/audio/opl_music_spec.md`](platform/audio/opl_music_spec.md).

## This Windows native-port package
This package bundles no IWAD, Freedoom, SoundFont, or proprietary mod assets.
It retains Duct Tape Doom's renderer and adds a targeted Fluid Weapons port
based on the user's supplied mickromash mod, loaded externally at runtime.
Additional vendored libraries are miniz 3.1.0, stb_image/stb_vorbis (commit
2c980bb59875b0d32144a71867fbdebb2f77cd20), and dr_mp3 (commit
dfe8377631000664666519fdb83da193fd8037f4). Their source and licenses are in
third_party. dr_mp3 provides streaming playback of SIGIL's MP3 music.

## Selected upstream effects (19 October 2026)
Body reactions, varied deaths, procedural falls, sprite wounds, torch embers,
shoreline effects and synthesized bullet flybys were integrated/adapted from
the user-supplied derp-main.zip. The upstream license is retained in LICENSE.TXT.
The newer glass implementation was not imported.
