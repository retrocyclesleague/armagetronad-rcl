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
used at all (the explosion) it is low, wide and closing up. The generator
enforces it: outside the explosion, a file with more than 2% of its energy
above 4 kHz, or with a jump between two samples that a click would need, is
refused. (The first version of this set had grit, zips and a grinding made
of separate scrapes. Jamie's verdict was "too particle-ish, everything should
be smooth and vibration like".)

And it says the same thing the same way every time. There is one file per
thing that happens, the game never picks between takes and never detunes
anything at random, and nothing is heard for some of its occasions and not
for others. (The second version picked one of two takes per turn and shifted
each by up to 4%, swelled the grind in over a fifth of a second and rang for
some centre messages and zones but not all. Jamie's verdict: "inconsistent in
a way that isn't intuitive", "clean binds should sound clean and harmonic".)

Everything with a pitch is on D, A and E, so whatever sounds together is a
fifth, a fourth or an octave apart.

Engine: four loops per cycle, which follow what the cycle does. The first two
are pitched with its speed, the other two keep their pitch:

- `cyclrun.wav`: the body. Two tones a hair apart, so every overtone beats at
  its own slow rate. Round rather than buzzy, but the weight is still in the
  overtones and not the fundamental, because cycles idle at two thirds of its
  pitch and small speakers have no sub. Exactly periodic over its two seconds.
- `cyclhigh.wav`: the whine, a motor tone with sidebands. Faint at cruising
  speed, it grows with it.
- `cyclboost.wav`: the pull, a chord of close tones beating slowly. Its level
  is how hard walls draw the cycle along, which is how close they are: none
  beyond the reach of wall acceleration, all of it at no distance. Not the
  cycle's acceleration, which is large off the start line and nothing at the
  top speed of a long grind.
- `grind.wav`: the grind, a low buzz that flutters at a steady rate under two
  pairs of beating tones. It joins the pull for a wall closer than the cycle
  is long. It does not depend on sparks being shown.

Turns, which are notes:

- `turn_left1.wav` is a left turn (A below middle C) and `turn_right1.wav` a
  right turn (the D a fourth above). Always.
- `turn_left2.wav` to `turn_left4.wav` and `turn_right2.wav` to
  `turn_right4.wav` are the next overtones of those notes: the octave, the
  fifth above it, the second octave. A turn the same way within 0.22 seconds
  of the last plays the next one up instead of the first, and each rings on
  while the next starts. So a double bind is two notes of one chord, a box
  all four, and a run played tightly sounds tight. Any other turn starts over
  on the first note of its side.
- A note is all there within two thousandths of a second (the generator
  refuses one that takes three to reach half its level): it is what tells a
  player the turn was made. Its overtones are exact multiples and the notes
  are exact ratios of each other, so nothing in a chord of them beats.

The rest:

- `expl.wav`: a boom that rolls rather than cracks: a body that falls in
  pitch, a wide low rush that closes up, a tail that shudders as it dies, a
  room.
- `death.wav`: for the one whose cycle it was, on top of the explosion: a
  tone that falls and closes up over one heavy beat.
- `count.wav`: a number of the countdown in the centre of the screen.
- `go.wav`: the zero it ends on. No other centre message makes a sound.
- `ui_hover.wav`, `ui_adjust.wav`, `ui_activate.wav`, `ui_back.wav`: the
  selection moves, a value is stepped, a row is entered, a menu is left.

A moviepack's own `moviesounds/` still take precedence where it has them;
the extra engine layers and the turn notes are only used with this set.

When a sound starts: the mixer fills a piece of the output at a time and a
sound starts with the next piece. That piece is 12 ms on Windows (Buffer
Length in the sound menu; it was 46 ms). Measured in the mixer, a turn note
waits 13 ms on average for its piece, where it waited 26. Smaller pieces
would not shorten that: under sdl12-compat the device takes about 23 ms at a
time and the pieces are filled in pairs. Asking for a sound never waits for
the mixer.

To check a mix without a speaker, start the client with the environment
variable `RCL_AUDIO_DUMP` set to a file name: what the mixer would play is
written there (16-bit stereo at the device's rate, no header) and the device
gets silence. Next to it, in a file of the same name with `.txt` added, are
the device's rate, the size of the mixer's pieces and how long turn notes
waited for the mixer.

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
