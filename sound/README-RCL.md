# RCL procedural audio

The WAV files in this directory are original procedural RCL assets. They are
generated entirely from oscillators, envelopes, filters, and synthetic noise;
they contain no sampled or copyrighted source recordings.

Regenerate them with:

```sh
bash scripts/generate-rcl-audio.sh
```

All generated files are mono, 48 kHz, signed 16-bit PCM WAVs. `cyclrun.wav` is
periodic and designed to loop without a boundary click. The generator uses a
fixed pseudo-random seed for every noise layer, normalizes peak level with
headroom, removes DC, and validates onset, quiet tails, and the engine seam.

The gameplay set is intentionally layered by function:

- `cyclrun.wav`: harmonic and half-order motor body plus a small periodic
  filtered-noise residual. Runtime pitch continues to follow cycle speed.
- `turn.wav`: immediate bright transient, short descending servo chirp, and
  lower mechanical body so a turn reads through the engine loop.
- `scrape.wav`: deterministic filtered friction noise, chatter, and resonant
  body. It is authored low because the legacy mixer plays it at four times
  source speed.
- `expl.wav`: a zero-latency crack, modal metallic body, low descending punch,
  and a longer debris tail, with headroom for the existing positional mix.

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
