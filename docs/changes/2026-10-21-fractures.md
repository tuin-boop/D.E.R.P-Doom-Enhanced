Tuin-Doom-Enhanced - fracture effects and lift performance

Download Tuin-Doom-Enhanced.exe and select your own IWAD. Commercial game data is not included.

Standard defaults: 250% destruction damage; varied deaths, procedural falls, face-viewer deaths, death reactions and FluidMonsters are off. Fluid Weapons and its sounds remain enabled. Existing profiles retain saved settings; use F4 to change these options on an existing installation. Destruction damage does not increase damage dealt to monsters.

Changes since the earlier October 21 release:
- New recessed fracture shading, bloom, darkened crust and cooling for rocket, plasma and BFG impacts, extended to monster fireballs, green Baron/Knight shots, Arachnotron plasma and Revenant missiles.
- Ground destruction beneath grounded projectile victims, including BFG spray and Cyberdemon rockets hitting a Spider Mastermind. Duplicate BFG spray cuts and rocket surface blasts are suppressed.
- Repair shallow-crater see-through patches and align wall clipping with replacement geometry.
- Reuse fixed map-outline distances during lift/door movement and avoid unnecessary collision-tile rebuilds. In the LAGELEVATOR replay, median moving-sector geometry update fell from 18.8 to 6.2 ms (not total frame time).
- Allow ordinary chase/attack updates while monster route planning is unfinished, instead of forcing a standstill.
- Keep classic deaths free of cached procedural falls and living hit-flinch poses when death effects are disabled.

Validation: all three geometry suites, shallow-cut and moving-floor cache regressions, saved LAGELEVATOR and HOLE replays, moving-door tests, actual rocket/Spider collision, projectile shader coverage, and native gameplay/save/load checks passed. The specific reported Cyberdemon wall pause has not been reproduced exactly.

Older releases remain available. Save/settings folders retain their existing names.

Based on Duct-Tape DOOM, the engine and renderer by Robert Heidenreich: https://github.com/robongrui/duct-tape-doom. Original Doom by id Software. Existing third-party credits and licenses are retained.
