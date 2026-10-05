# RCL procedural audio

The WAV files in this directory are original procedural RCL assets. They are
generated entirely from oscillators, envelopes, filters, and synthetic noise;
they contain no sampled or copyrighted source recordings.

Regenerate them with:

```sh
bash scripts/generate-rcl-audio.sh
```

All generated files are mono, 48 kHz, signed 16-bit PCM WAVs. The generator
uses a fixed pseudo-random seed for every noise layer, normalizes peak level
with headroom, removes DC, and validates onset, quiet tails, the loops' seams
and how much energy lies above 10 kHz. That last check is there because the
game's default output rate is 22.05 kHz and its mixer has no filter of its
own: whatever a file has up there would fold back as noise.

The set speaks one language: smooth, and felt as much as heard. Things hum,
swell, shudder and settle. Nothing clicks, crackles or hisses; where noise is
used at all (the explosions) it is low, wide and closing up. The generator
enforces it: outside the explosions, a file with more than 2% of its energy
above 4 kHz, or with a jump between two samples that a click would need, is
refused. (The first version of this set had grit, zips and a grinding made
of separate scrapes. Jamie's verdict was "too particle-ish, everything should
be smooth and vibration like".)

Everything with a pitch is in one key (D), so the set sounds like one thing.

Engine, four loops the game swells and settles per cycle and pitches with its
speed. Nothing in a cycle's sound starts or stops abruptly but a turn:

- `cyclrun.wav`: the body. Two tones a hair apart, so every overtone beats at
  its own slow rate. Round rather than buzzy, but the weight is still in the
  overtones and not the fundamental, because cycles idle at two thirds of its
  pitch and small speakers have no sub. Exactly periodic over its two seconds.
- `cyclhigh.wav`: the whine, a motor tone with sidebands. Faint at cruising
  speed, it grows with it.
- `cyclboost.wav`: the pull, a chord of close tones beating slowly. The game
  fades it in with the cycle's acceleration, which is what a wall gives.
- `grind.wav`: the grind, a low buzz that flutters at a steady rate under two
  pairs of beating tones. The game brings it up quickly while sparks fly and
  lets it go slowly, so it is one hum and not a sound per spark.

One-shots, of which the game picks a take and varies the pitch by a few
percent every time, so a burst of them is not one sample stuttering:

- `turn.wav`, `turn3.wav` (left) and `turn2.wav`, `turn4.wav` (right): a
  short round thrum that sags in pitch and shivers as it dies, with a soft
  note two octaves up so it carries. Left turns are on the two lower notes
  (G, A), right turns on the two upper (C, D).
- `expl.wav`, `expl2.wav`, `expl3.wav`: a boom that rolls rather than cracks:
  a body that falls in pitch, a wide low rush that closes up, a tail that
  shudders as it dies, a room.
- `death.wav`: for the one whose cycle it was, on top of the explosion: a
  tone that falls and closes up over one heavy beat.

Round and interface, which used to be silent:

- `count.wav`: a number shown in the centre of the screen (3, 2, 1).
- `go.wav`: the zero a countdown ends on.
- `notice.wav`: any other centre message, at most every 1.5 seconds.
- `zone.wav`: a zone comes into being.
- `ui_hover.wav`, `ui_adjust.wav`, `ui_activate.wav`, `ui_back.wav`: the
  selection moves, a value is stepped, a row is entered, a menu is left.

A moviepack's own `moviesounds/` still take precedence where it has them;
the extra engine layers and the takes are only used with this set.

To check a mix without a speaker, start the client with the environment
variable `RCL_AUDIO_DUMP` set to a file name: what the mixer would play is
written there (16-bit stereo at the device's rate, no header) and the device
gets silence.

## Design references

The synthesis approach follows primary and official sources rather than a
third-party sample library:

- Tsai, Wang, and Su model timbre as deterministic sinusoids plus a filtered
  stochastic residual in *GPU-Based Spectral Model Synthesis for Real-Time
  Sound Rendering* (DAFx-10):
  https://www.dafx.de/paper-archive/2010/DAFx10/TsaiWangSu_DAFx10_P28.pdf
- Freed found perceived impact hardness predicted by the attack's spectral
  level and spectral-centroid behavior in *Auditory correlates of perceived
  mallet hardness* (JASA 87): https://doi.org/10.1121/1.399298
- Hjortkjaer and McAdams found both spectral distribution and temporal-envelope
  energy contribute to identifying impact material and action (JASA 140):
  https://doi.org/10.1121/1.4955181
- Kim et al. relate powerful, pleasant vehicle sound to engine-order spectra,
  harmonic arrangement, level envelope, rumble, and booming (SAE 2017-01-1756):
  https://doi.org/10.4271/2017-01-1756
- The official GDC session *Making a Car Sound Like a Car* emphasizes vehicle
  audio as multiple components under shared simulation control rather than one
  undifferentiated loop:
  https://www.gdcvault.com/play/1012692/Making-a-Car-Sound-Like
- Jack et al. found zero-latency action feedback rated higher quality than
  jittered 10 ms and 20 ms feedback, motivating immediate cue onsets:
  https://doi.org/10.1525/mp.2018.36.1.109
