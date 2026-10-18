Reference: bundled original FluidWeapons.pk3, SHA256 47ee9e7be5d8118e39b601f5da3be0735e3029baf2af31cd5245c718be315e52
Captured with UZDoom 5.0.3 at the original 35-Hz state rate.
The test EventHandler runs the actual Select and Deselect states and logs
PSprite transforms after every tick. Four ordinary samples per weapon are
compiled, retaining sprite changes, separate hands and independent layers.
Gloved fists have separate captures. Ambient chainsaw smoke and the invisible
base control overlay are excluded. Pistol mirroring matches our right-handed
presentation. Firing interrupts visual settling; Doom owns weapon timing.

Regenerate from the repository root:
python tools/compile_switch_capture.py tools/switch-reference/bare.txt.gz platform/weapon_switch_data.h tools/switch-reference/gloved.txt.gz

Captures cover ordinary switching. Mid-fire special lower/reload branches and
optional chainsaw starter-cord mode are not included in these clips.
