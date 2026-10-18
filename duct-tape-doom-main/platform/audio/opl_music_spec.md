# OPL music: behaviour spec

What the OPL music player does, written only from public documents:

- Yamaha YMF262 (OPL3) register manual: register map, operator layout,
  F-number/block pitch, 0.75 dB total-level steps.
- Doom Wiki, GENMIDI: lump layout, flags, fine tuning, base note offset.
- Doom Wiki, MUS (and the ModdingWiki MUS page it links to): events,
  controllers, pitch bend range, mono/poly.

It does not follow any Doom port's source code. Where the documents leave
something open, the choice below is our own and is marked *ours*.

## Files

| File | Job |
|---|---|
| `music_score.c` | MUS and MIDI loaders (kept from before): one event list in MUS channel numbers, timed in output frames. Shared with the SoundFont synth |
| `music_player.c` | Plays a score through a synth: events on their frame, looping. Shared |
| `opl_genmidi.c` | Reads the GENMIDI lump |
| `opl_chip.c` | Nuked OPL3, chip startup, operator offsets, pitch formula, volume curve |
| `opl_voices.c` | 16 score channels, 18 chip voices, allocation and stealing |
| `mixer_music.c` | The game's side: picks the OPL or SoundFont synth, output gain |
| `tests/music_render.c` | `doom-music-render`: writes a WAD's songs to WAV files without the game |

## Chip

- OPL3 mode, two banks of nine 2-operator channels: 18 voices. Bank 1
  registers are the bank 0 address plus 0x100.
- Operator offsets are calculated, not stored in a table. For channel
  `ch` (0-8) the first (modulator) operator is at `ch % 3 + 8 * (ch / 3)`
  and the second (carrier) operator is 3 further on.
- Startup, after the emulator reset:
  1. `0x105 = 0x01`: NEW. Turns on OPL3 mode: the second bank, stereo
     output bits and all eight waveforms.
  2. `0x104 = 0x00`: every channel is a 2-operator channel.
  3. `0x01 = 0x00`, `0x08 = 0x00`: test register cleared, no CSM, NTS 0.
  4. `0xBD = 0x00`: melodic mode, shallow tremolo (1 dB) and vibrato
     (7 cents). *Ours*: these are the chip's own defaults.
  5. On each of the 18 channels: key off, both output sides on, and both
     operators at full attenuation (TL 63) with the fastest release.

  Only registers that exist on the YMF262 are written.
- Note writes go through Nuked's write queue, two chip samples apart
  (about 40 µs, roughly the pace real cards needed). This also lets a
  key-off reach the envelope before the next key-on, so a stolen voice
  restarts its attack.

## Instruments (GENMIDI)

- `#OPL_II#`, then 175 records of 36 bytes: 128 melodic programs, then 47
  percussion sounds for keys 35-81.
- Record: flags (u16), fine tuning (u8), fixed note (u8), two 16-byte
  voices. Flag 0x0001 means fixed pitch, 0x0004 means double voice, and
  0x0002 is ignored.
- A patch is kept as the lump's 16 bytes. They map straight to registers: 0 to 0x20, 1 to 0x60, 2 to 0x80,
  3 to 0xE0, 4|5 to 0x40 (KSL is stored already shifted, level 0-63), 6 to
  0xC0 (feedback and connection). Bytes 7-12 are the same for the carrier.
  Bytes 14-15 are the base note offset, a signed number of semitones.
- Fine tuning applies to the second voice only. 128 means no change, and
  each step is 1/32 semitone (per the wiki: 0x82 equals a MIDI bend of +256).
- Double voice: both layers sound. *Ours*: each layer keeps its own carrier
  level, so the second layer isn't forced to the first layer's volume.

## Pitch

The pitch of a voice is counted in 1/32 semitones:

    p = 32 * (note + base_offset + 12) + bend + detune

The extra octave comes from the instruments themselves. In Doom's GENMIDI,
78 of the 128 melodic patches run their carrier at half the channel
frequency (MULT 0) or carry a -12 base note, and only 25 sound at the
channel frequency. The bank was voiced for a chip set an octave above the
note, which brings most instruments to their General MIDI pitch. Played at
concert pitch without it, E1M1's bass (program 34, MULT 0) sounds at
20-41 Hz instead of 41-82 Hz. (The first version missed this; it was
caught by comparing with a Sound Blaster recording.)

`note` is the key, or the fixed note when the instrument has fixed pitch.
`bend` is the channel's bend, from -64 to +63 (two semitones each way).
`detune` is `fine_tuning - 128` on the second layer and 0 on the first.
Fixed-pitch instruments ignore bend.

Concert pitch (MIDI 69 = 440 Hz), turned into chip units with the YMF262
formula `f = fnum * 49716 / 2^(20 - block)`:

    x = 440 * 2^((p/32 - 69) / 12) * 2^20 / 49716   (F-number at block 0)
    raise block while x >= 1023.5 (halving x each time), up to block 7
    fnum = min(1023, round(x))

So F-numbers fall between 512 and 1023 wherever the chip allows. Rounding
then costs at most 1.7 cents; the lowest notes (below MIDI 21, where block
0 runs out) reach 2.5 cents. Below block 0 the F-number just gets
smaller. Above block 7 the pitch clamps at the top instead of folding back
an octave. There is no table and no overflow entry.

## Volume

Each voice's loudness comes from three 7-bit values: note velocity (v),
channel volume (c) and channel expression (e). *Ours*: the curve is
square-law, 40 dB per decade for each of them:

    loss_dB = 40 * log10(127/v) + 40 * log10(127/c) + 40 * log10(127/e)
    steps   = round(loss_dB / 0.75)          (the chip's TL step)

`steps` is added to the carrier's GENMIDI level. With connection = 1
(additive, so the modulator is heard directly) it is added to the
modulator's level too. The result is clamped to 63. A zero in any of the
three values makes the note silent. The game's music slider scales the
mixed output linearly and does not touch the chip. The output gain is 1.0:
the loudest score (MAP01) peaks at 0.83 on the chip, so it stays under
clipping at full music volume.

## Channels

- 16 channels in MUS numbering, with 15 as percussion. MIDI files arrive
  already remapped by the loader.
- A channel is only its last 7-bit values (program, volume, expression,
  pan, bend) plus the mono flag. Chip values are worked out from them when
  a register is written.
- Defaults on song start: program 0, volume 100, expression 127, pan
  centre, bend centre, poly mode.
- Pan, 0-127: below 32 is left only, above 95 is right only, and anything
  else plays on both sides. This writes the C0 output bits (0x10 left,
  0x20 right). *Ours*: the OPL3 can't do anything between those three.
- Percussion (channel 15): key k in 35-81 plays instrument 128 + k - 35.
  Other keys are ignored, and program changes on that channel are ignored.
- Controllers acted on: 7 volume, 10 pan, 11 expression, 120 all sounds off
  (immediate silence), 121 reset controllers, 123 all notes off (release
  normally), 126 mono, 127 poly. Every other controller is ignored.
- Reset controllers re-centres bend and sets expression to 127. It keeps
  volume, pan and program, as the General MIDI recommended practice says.
- Mono mode: a note-on first releases every note still held on that
  channel.
- A velocity of 0 on note-on counts as a note-off.
- Volume, expression and pan changes reach notes that are already
  sounding. Bend changes retune them.

## Voices

- 18 fixed voices: bank 0 channels 0-8, then bank 1 channels 0-8.
- A global age counter goes up by one on every note-on and every release.
  Each voice records the counter's value at its last change.
- Note-on: the first layer always gets a voice. The second layer, if the
  instrument is double, only takes a voice that is free. It never steals.
- Picking a free voice: the one released longest ago, so recent release
  tails can ring out. A free voice that already holds the note's patch goes
  first, since reloading a patch costs eight register writes.
- Register writes that wouldn't change a register are skipped.
- Stealing, when no voice is free. *Ours*:
  1. Release the oldest voice playing a second layer.
  2. Otherwise, release the oldest note on the highest-numbered channel
     that has a note held. This is MUS numbering, so drums (channel 15) go
     first.
  Stealing is rare in the IWADs (3 times across all 64 Doom and Doom II
  scores, all in D_INTRO) but common in PWAD MIDI music: Scythe 2 has 589
  steals, and part A of Back to Saturn X E1 has 22,198. Sparing drums
  instead (`doom-music-render -steal spare`) did worse there. In BTSX's
  D_ROMERO it doubled the steals and the song came out much quieter:
  MIDI drum notes are often released late or never, so spared drums hold
  voices the melody needs.
- A voice reloads its instrument registers only when its instrument or
  layer changes.
- Note-off releases every voice on that channel holding that key: key-off
  only, so the envelope's release plays out.

## Song timing

The loaders turn MUS ticks (140 Hz) and MIDI ticks (tempo map) into output
frames. The render loop dispatches every event at or before the current
frame, then clocks the chip once per frame. At the end, a looping song
resets its channels and voices and starts again. Otherwise playback stops.
