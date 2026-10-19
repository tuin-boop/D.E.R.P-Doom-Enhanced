# Destruction prototype — October 19, 2026

Experimental Windows build of D.E.R.P Doom Enhanced, based on Duct-Tape DOOM and the original Doom engine. Includes the existing native FluidWeapons, FluidWeapons SFX and FluidMonsters integration, HUD, AI options and lighting improvements.

## Destructible environments
- Rocket impacts excavate irregular concrete craters in walls, solid floors and ceilings.
- Repeated hits deepen damage; nearby surfaces can receive blast damage when a rocket hits a monster.
- Damage follows moving doors and wraps across supported adjoining surfaces.
- Liquid surfaces do not receive craters.
- Optional through-wall breaches create traversable openings between valid rooms.
- Optional door breaches include locked doors. These can bypass keys and switches and break map progression.
- Wall damage slider: 100–500%, controlling how quickly new wall and door damage accumulates.
- Deeper digging toggle: raises the wall/door depth limit from 256 to 1,024 map units. Off by default; requires wall or door breaching.
- Disabling deeper digging restores normal limits without erasing stored damage.
- Outside-map void remains sealed. Breaches require supported opposing parallel faces and a valid room beyond.
- Through-floor and through-ceiling passages are not implemented.
- Crater caching, local invalidation and floor mesh optimizations reduce growing update costs.

Find the destruction controls under **F4 > Effects > Doors**. This is a prototype: geometry artifacts and map-specific limitations may remain.

## Other additions since the stable release
Optional hit reactions, varied deaths, procedural falls, wound holes, torch embers, shoreline effects and near-miss sounds. Effect cost hints help identify expensive options; shore ripples are off by default. The new glass integration is deferred.

## Running
Download **DERP-Destruction.exe**, run it and select your own compatible IWAD. The three Fluid mod packages and crater material are embedded; no commercial IWAD or user save is included.

This build uses separate destruction-prototype settings and saves. It does not replace the stable release.

## Validation
Release engine and native smoke build passed. Tests covered a full 1,024-unit breach, partial rocket impacts, toggle persistence, sight, hitscan, rocket traversal, save/load and two-way player passage. Locked-door and bundled-feature regression checks passed.

See the accompanying TESTED notes for the earlier surface, performance and damage-slider checks.
