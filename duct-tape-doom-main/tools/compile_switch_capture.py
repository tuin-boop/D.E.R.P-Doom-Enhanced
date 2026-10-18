"""Compile per-tic Fluid Weapons PSprite captures to native animation data.

Input is produced by the reference-engine SwitchCapture EventHandler. Frames
0..31 are Select, 40..54 are Deselect; the following weapon is excluded.
No interpreter or reference engine is required by the shipped game.
"""
import sys
import gzip
from pathlib import Path

def read_capture(path):
    frames = {}
    text = gzip.open(path, 'rt').read() if str(path).endswith('.gz') else Path(path).read_text()
    for line in text.splitlines():
        p = line.split()
        if p and p[0] == 'SF':
            sample, weapon, tic = map(int, p[1:4])
            base = list(map(float, p[4:6]))
            frame = frames.setdefault(sample, {})[tic] = []
        elif p and p[0] == 'SL':
            ident, sprite = int(p[1]), p[2]
            # The base weapon is a hidden control state, not artwork. Smoke is
            # an independent ambient effect, not part of the switch animation.
            if ident == 1 or sprite.startswith(('NSHS', 'PLY')):
                continue
            x,y,sx,sy,rot,px,py,alpha = map(float,p[3:11])
            add,flip,mirror,interp = map(int,p[11:15])
            if add:
                x += base[0]
                y += base[1]
            # Our pistol uses the right-handed presentation throughout gameplay.
            if weapon == 1:
                flip = mirror = 0
            additive = sprite.startswith('CH2B')
            flags = flip | (mirror<<1) | (interp<<2) | (additive<<3)
            frame.append((ident,sprite,x,y,sx,sy,rot,px,py,alpha,flags))
    return frames

frames = read_capture(sys.argv[1])

poses, ranges, clips = [], [], []
def num(v):
    s = f'{v:.6f}'.rstrip('0').rstrip('.')
    return s + ('.0f' if '.' not in s else 'f')
for weapon in range(9):
    # Discard the first round: initial inventory and glove preference changes
    # are deliberately not representative of ordinary weapon switching.
    samples = [s for s in sorted(frames) if s>=9 and s%9==weapon and 79 in frames[s]]
    if len(samples)<4:
        raise ValueError(f'weapon {weapon}: need four complete reference samples')
    for sample in samples[:4]:
        for ticks in (range(32), range(40,55)):
            clips.append((len(ranges),len(ticks)))
            for tic in ticks:
                layer = frames[sample][tic]
                ranges.append((len(poses),len(layer)))
                poses.extend(layer)

# Gloves use their own source Select/Deselect, not a recolored glove-strip.
gloved = read_capture(sys.argv[3])
for sample in range(1,5):
    for ticks in (range(32), range(40,55)):
        clips.append((len(ranges),len(ticks)))
        for tic in ticks:
            layer = gloved[sample][tic]
            ranges.append((len(poses),len(layer)))
            poses.extend(layer)

out=['// Generated from original FluidWeapons.pk3 PSprite states at 35 Hz.',
     '// See tools/compile_switch_capture.py. Do not edit individual poses.',
     'inline constexpr SwitchPose switchPoses[] = {']
for ident,sprite,x,y,sx,sy,rot,px,py,alpha,flags in poses:
    out.append('{%d,"%s",{%s},%s,%d},' % (ident,sprite,','.join(map(num,(x,y,sx,sy,rot,px,py))),num(alpha),flags))
out += ['};','inline constexpr SwitchFrame switchFrames[] = {']
out += ['{%d,%d},'%r for r in ranges]
out += ['};','inline constexpr SwitchClip switchClips[] = {']
out += ['{%d,%d},'%r for r in clips]
out += ['};','']
Path(sys.argv[2]).write_text('\n'.join(out))
print(f'{len(clips)} clips, {len(ranges)} frames, {len(poses)} layer poses')
