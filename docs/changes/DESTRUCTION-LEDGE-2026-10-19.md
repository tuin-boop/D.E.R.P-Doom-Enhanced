# Destruction prototype — ledge movement fix

Fixes getting stuck after dropping close to a damaged ledge, reproduced with STUCK10. Players whose collision box already overlaps the edge can retreat into the room; moving farther into the wall remains blocked. This is an engine-wide collision fix, with no noclip or teleport workaround.

Includes the previous passage/seam, barrel crater, door, recovery and notice fixes, plus all three bundled Fluid mods.

Validation: STUCK10 outward retreat and subsequent movement passed; deeper wall penetration stayed blocked. STUCKWEIRD, UNDERLVL, save 5 two-way passage, DOORWONTOPEN and shared-edge regression checks passed. Original saves were not changed.

Download DERP-Destruction.exe and select your own IWAD. This remains the separate experimental destruction prerelease; optional breaches can bypass map progression. Commercial IWADs and personal saves are not included.
