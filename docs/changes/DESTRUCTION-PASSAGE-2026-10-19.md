# Destruction prototype — passage and crater fixes

- Removed artificial rectangular frames at breached wall ends while preserving shared wall joins.
- Fixed two-way walking through existing openings, including stepping over fractured sills and crossing exact wall boundaries.
- Removed coincident ceiling sheets from closed breached doors that left a dark strip across the floor.
- Fixed floor-backed door breaches and false collision behind concave walls.
- Added safe recovery for players trapped below closed backing sectors by older crater support calculations.
- Exploding barrels now create nearby wall/floor craters using the rocket blast rules and destruction toggle.
- Toasty chance is now 5%. Close Call requires qualifying enemy damage; firing at nothing or self/environment damage cannot trigger it.
- Includes all three bundled Fluid mods and the existing destruction options, including damage scaling and optional 4,096-unit digging.

Validation: save 5 existing-damage two-way passage and visual inspection; shared-edge geometry; floor/ceiling and liquid checks; stuck-player saves; floor-backed door passage; 4,096-unit passage, projectiles, toggles and save/load.

Run DERP-Destruction.exe and select your own IWAD. Commercial IWADs and personal saves are not included. This remains an experimental destruction build; optional breaches can bypass intended map progression. The stable release remains separate.
