#!/usr/bin/env python3
"""Generate RCL's deterministic, sample-free sound set.

Only oscillators, deterministic pseudo-random noise, envelopes, filters and
short delay lines are used.  No recorded or third-party audio enters the
output.

The set speaks one language: smooth, and felt as much as heard.  Things hum,
swell, shudder and settle.  Nothing clicks, crackles or hisses; where noise
is used at all (the explosion) it is low, wide and closing up.  Every onset
is soft but immediate, a thousandth of a second or two.

And it says the same thing the same way every time.  There are no takes to
pick from and nothing is detuned at random: one file per thing that happens.

The set is layered by what the game does with it:

  engine     cyclrun (body), cyclhigh (whine, comes in with speed),
             cyclboost (pull, the closer a wall draws the cycle along),
             grind (on top of it, for a wall closer than the cycle is long);
             all loops
  turns      turn_left1..4, turn_right1..4: a note for each way, and the
             overtones of it that a run of turns the same way climbs
  explosion  expl; death for the one whose cycle it was
  interface  ui_hover, ui_adjust, ui_activate, ui_back
  round      count (a number of the countdown), go (its zero)

Everything is written band-limited below 10 kHz: the game's default output
rate is 22.05 kHz and its mixer has no filter of its own.  The loops stay
below about 2 kHz, because they are played above their pitch.
"""

from pathlib import Path
import math
import random
import struct
import wave


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "sound"
SAMPLE_RATE = 48_000
TAU = 2.0 * math.pi

# One key for everything that has a pitch, so the set sounds like one thing.
D3, F3, G3, A3, C4 = 146.83, 174.61, 196.00, 220.00, 261.63
D4, F4, G4, A4, C5 = 293.66, 349.23, 392.00, 440.00, 523.25
D5, F5, G5, A5, C6, D6 = 587.33, 698.46, 783.99, 880.00, 1046.50, 1174.66


# ---------------------------------------------------------------- tools ----

def time_axis(duration):
    return [index / SAMPLE_RATE for index in range(round(duration * SAMPLE_RATE))]


def deterministic_noise(length, seed):
    generator = random.Random(seed)
    return [generator.uniform(-1.0, 1.0) for _ in range(length)]


def biquad(signal, kind, cutoff, q=0.70710678):
    omega = TAU * cutoff / SAMPLE_RATE
    cosine = math.cos(omega)
    sine = math.sin(omega)
    alpha = sine / (2.0 * q)

    if kind == "lowpass":
        b0 = (1.0 - cosine) / 2.0
        b1 = 1.0 - cosine
        b2 = b0
    elif kind == "highpass":
        b0 = (1.0 + cosine) / 2.0
        b1 = -(1.0 + cosine)
        b2 = b0
    else:
        raise ValueError(f"unsupported biquad type: {kind}")

    a0 = 1.0 + alpha
    a1 = -2.0 * cosine
    a2 = 1.0 - alpha
    b0 /= a0
    b1 /= a0
    b2 /= a0
    a1 /= a0
    a2 /= a0

    x1 = x2 = y1 = y2 = 0.0
    output = []
    for value in signal:
        result = b0 * value + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        output.append(result)
        x2, x1 = x1, value
        y2, y1 = y1, result
    return output


def band(signal, low, high):
    """Noise between two frequencies, with a steep top."""
    return biquad(biquad(biquad(signal, "highpass", low), "lowpass", high), "lowpass", high)


def circular_box_filter(signal, radius):
    """A loop-safe moving average: what leaves one end comes in at the other."""
    if radius <= 0:
        return list(signal)
    length = len(signal)
    width = 2 * radius + 1
    extended = signal[-radius:] + signal + signal[:radius]
    running = sum(extended[:width])
    output = [running / width]
    for index in range(1, length):
        running += extended[index + width - 1] - extended[index - 1]
        output.append(running / width)
    return output


def circular_band(signal, fast_radius, slow_radius):
    """Loop-safe band of noise: a short average minus a long one.

    Each average is taken four times over, which makes it all but Gaussian:
    a single moving average leaks a comb of its own far above the band, and
    the game would pitch that up into the top of what it can play.
    """
    fast = signal
    slow = signal
    for _ in range(4):
        fast = circular_box_filter(fast, fast_radius)
        slow = circular_box_filter(slow, slow_radius)
    return normalize_rms([a - b for a, b in zip(fast, slow)])


def normalize_rms(signal):
    rms = math.sqrt(sum(value * value for value in signal) / max(1, len(signal)))
    if rms <= 1.0e-12:
        return list(signal)
    return [value / rms for value in signal]


def remove_dc(signal):
    mean = sum(signal) / max(1, len(signal))
    return [value - mean for value in signal]


def soft_clip(signal, drive):
    scale = math.tanh(drive)
    return [math.tanh(value * drive) / scale for value in signal]


def glide_phase(t, start_frequency, end_frequency, duration, curve=1.0, phase=0.0):
    """Phase of a tone gliding between two pitches; curve > 1 gets there early."""
    # frequency(t) = end + (start - end) * (1 - t / duration) ** curve
    remaining = max(0.0, 1.0 - t / duration)
    swept = (duration / (curve + 1.0)) * (1.0 - remaining ** (curve + 1.0))
    return TAU * (end_frequency * t + (start_frequency - end_frequency) * swept) + phase


def fade_out(t, duration, fade_duration):
    if t <= duration - fade_duration:
        return 1.0
    progress = min(1.0, (t - duration + fade_duration) / fade_duration)
    return math.cos(0.5 * math.pi * progress) ** 2


def onset(t, seconds=0.0012):
    """Soft but immediate: most of the way up within a few thousandths."""
    return 1.0 - math.exp(-t / seconds)


def tone(t, frequency, decay_rate, warmth=0.5, phase=0.0):
    """A round struck tone: a sine with a little of its own overtones, which
    die sooner than it does.  It starts at zero and has no click to it."""
    base = TAU * frequency * t + phase
    return math.exp(-decay_rate * t) * (
        math.sin(base)
        + warmth * 0.40 * math.exp(-decay_rate * 0.9 * t) * math.sin(2.0 * base)
        + warmth * 0.16 * math.exp(-decay_rate * 1.6 * t) * math.sin(3.0 * base)
    )


def shiver(t, rate, depth, settle_rate=30.0):
    """Amplitude that trembles at a steady rate once the sound is under way:
    what makes a tone read as a vibration."""
    return 1.0 - depth * (1.0 - math.exp(-settle_rate * t)) * (0.5 - 0.5 * math.cos(TAU * rate * t))


def room(signal, seed, wet=0.22, size=1.0, damping=0.45):
    """A small space around a sound: a few damped echoes feeding each other.

    Deterministic, short, and quiet enough that the dry onset stays first.
    """
    delays = [round(size * SAMPLE_RATE * d) for d in (0.0131, 0.0177, 0.0229, 0.0297, 0.0371)]
    gains = (0.72, 0.69, 0.66, 0.62, 0.58)
    lines = [[0.0] * delay for delay in delays]
    positions = [0] * len(delays)
    states = [0.0] * len(delays)
    output = []
    for value in signal:
        tapped = 0.0
        for line_index, line in enumerate(lines):
            position = positions[line_index]
            echoed = line[position]
            # each echo loses its top on the way round
            states[line_index] += (echoed - states[line_index]) * (1.0 - damping)
            line[position] = value + states[line_index] * gains[line_index]
            positions[line_index] = (position + 1) % len(line)
            tapped += echoed if line_index % 2 == 0 else -echoed
        output.append(value + wet * tapped / len(lines) * 2.0)
    return output


def extend(signal, seconds):
    return signal + [0.0] * round(seconds * SAMPLE_RATE)


def tail_fade(signal, fade_duration):
    length = len(signal)
    duration = length / SAMPLE_RATE
    return [value * fade_out(index / SAMPLE_RATE, duration, fade_duration)
            for index, value in enumerate(signal)]


def settle(signal, share=0.3, longest=0.05):
    """Brings the end of a dry sound down to nothing before a space is put
    around it; cut off at whatever level it has, it would end in a click."""
    return tail_fade(signal, min(longest, share * len(signal) / SAMPLE_RATE))


# ---------------------------------------------------------------- loops ----

LOOP_SECONDS = 2.0


def periodic(frequency):
    """The nearest pitch that fits the loop a whole number of times."""
    return round(frequency * LOOP_SECONDS) / LOOP_SECONDS


def stack(bases, top, slope, resonance_at, resonance_gain, resonance_width, close_at, seed):
    """Overtones of a few close pitches, each starting at a phase of its own.

    Lined up, close pitches fall in and out of step all at once and the tone
    throbs once per loop; scattered, every overtone beats in its own time.
    """
    phases = random.Random(seed)
    partials = []
    for base, level in bases:
        harmonic = 1
        while base * harmonic <= top:
            frequency = base * harmonic
            resonance = 1.0 + resonance_gain * math.exp(-((frequency - resonance_at) / resonance_width) ** 2)
            closing = 1.0 / math.sqrt(1.0 + (frequency / close_at) ** 4)
            partials.append((frequency, level * resonance * closing / harmonic ** slope,
                             phases.uniform(0.0, TAU)))
            harmonic += 1
    return partials


def engine_body():
    """Two tones a hair apart: every overtone beats at its own slow rate, so
    the hum keeps moving like an engine under load, and it is exactly
    periodic over the loop.

    It is round rather than buzzy: the overtones fall away quickly above the
    body's resonance.  But the weight is still in them and not in the
    fundamental: the game plays this at two thirds of its pitch while cycles
    wait for the start, and a tone that is mostly sub turns to mud there and
    to nothing on small speakers.
    """
    times = time_axis(LOOP_SECONDS)
    length = len(times)
    partials = stack(((periodic(82.5), 1.0), (periodic(83.0), 0.85), (periodic(165.5), 0.35)),
                     1_400.0, 1.0, 360.0, 1.1, 190.0, 800.0, 0x52434C30)

    # a little breath between about 150 Hz and 1.2 kHz, far under the tone
    texture = circular_band(deterministic_noise(length, 0x52434C31), 4, 36)
    sub_octave = periodic(41.5)

    output = []
    for index, t in enumerate(times):
        hum = 0.0
        for frequency, amplitude, phase in partials:
            hum += amplitude * math.sin(TAU * frequency * t + phase)
        sub = 0.16 * math.sin(TAU * sub_octave * t + 0.3)
        # it trembles a little, at whole-number rates so the loop holds
        tremble = 0.93 + 0.05 * math.sin(TAU * 3.0 * t + 0.7) + 0.02 * math.sin(TAU * 27.0 * t)
        output.append(tremble * (0.36 * hum + sub) + 0.018 * texture[index])
    return soft_clip(output, 1.1)


def engine_whine():
    """The upper layer: a motor's tone with sidebands.  It is faint at
    cruising speed, grows with it, and is pitched up further as it does."""
    times = time_axis(LOOP_SECONDS)
    carrier = periodic(440.0)
    second = periodic(660.5)
    order = periodic(55.0)

    output = []
    for t in times:
        # sidebands a whole engine order apart keep it periodic
        wobble = 0.9 + 0.3 * math.sin(TAU * 2.0 * t)
        motor = math.sin(TAU * carrier * t + wobble * math.sin(TAU * order * t))
        upper = math.sin(TAU * second * t + 0.6 * math.sin(TAU * order * 2.0 * t + 0.6))
        pulse = 0.84 + 0.16 * math.sin(TAU * 4.0 * t + 1.1)
        output.append(pulse * (0.52 * motor + 0.22 * upper))
    return soft_clip(output, 1.0)


def engine_pull():
    """A wall drawing the cycle along: a chord that leans forward.  Pairs of
    close tones beating slowly, trembling together.  The game lets it in with
    the cycle's acceleration."""
    times = time_axis(LOOP_SECONDS)
    pairs = (
        (periodic(D4), periodic(D4) + 0.5, 0.50),
        (periodic(A4), periodic(A4) + 1.0, 0.36),
        (periodic(D5), periodic(D5) + 1.5, 0.22),
        (periodic(A5), periodic(A5) + 0.5, 0.09),
    )
    output = []
    for t in times:
        chord = 0.0
        for low, high, level in pairs:
            chord += level * (math.sin(TAU * low * t) + math.sin(TAU * high * t + level * 9.0))
        tremor = 0.80 + 0.12 * math.sin(TAU * 9.0 * t) + 0.08 * math.sin(TAU * 1.0 * t + 0.4)
        output.append(tremor * chord)
    return soft_clip(output, 0.9)


def grind_loop():
    """Grinding along a wall: a current you can feel.  A low buzz that
    flutters at a steady rate, with two pairs of close tones beating over it.

    There is no grit in it and nothing in it starts or stops; the game
    brings it in over the pull when the wall is very close, at this pitch.
    It is on the notes of the pull and of the turns, which it is heard with.
    """
    times = time_axis(LOOP_SECONDS)
    partials = stack(((periodic(D3 * 0.5), 1.0), (periodic(D3 * 0.5) + 0.5, 0.8)),
                     1_900.0, 1.0, 600.0, 2.4, 280.0, 1_300.0, 0x52434C44)
    flutter = 36.0
    low_pair = (periodic(A5), periodic(A5) + 2.0)
    high_pair = (periodic(D6), periodic(D6) + flutter)

    output = []
    for t in times:
        buzz = 0.0
        for frequency, amplitude, phase in partials:
            buzz += amplitude * math.sin(TAU * frequency * t + phase)
        fluttering = 0.66 + 0.34 * math.sin(TAU * flutter * t)
        breathing = 0.92 + 0.08 * math.sin(TAU * 1.0 * t + 0.9)
        ringing = 0.5 * (math.sin(TAU * low_pair[0] * t) + math.sin(TAU * low_pair[1] * t + 1.0))
        shimmer = 0.5 * (math.sin(TAU * high_pair[0] * t) + math.sin(TAU * high_pair[1] * t + 0.5))
        output.append(breathing * (fluttering * 0.34 * buzz + 0.15 * ringing + 0.05 * shimmer))
    return soft_clip(output, 1.05)


# ---------------------------------------------------------------- turns ----

# A turn is a note, and the same turn is always the same note: A below middle
# C for left, the D a fourth above it for right.  A turn the same way soon
# after the last plays the next overtone of its note instead (the octave, the
# fifth over that, the second octave), so a double bind is two notes of one
# chord and a box all four.  The ratios are exact, so notes that ring together
# lock: there is nothing in a chord of them to beat.
TURN_LEFT = A3
TURN_RIGHT = A3 * 4.0 / 3.0
TURN_STEPS = 4


def turn_note(frequency, duration=0.34):
    """One turn: a clean, round note.

    It is all there at once (two thousandths of a second, eased, so it has
    no click), because it is what tells a player the turn was made.  Its
    overtones are exact and die sooner than it does, so it starts clear and
    ends pure.  A short weight an octave down sits under its start, to be
    felt.  Nothing in it glides, trembles or hisses, and it has no room
    around it: whatever else is sounding, it is in tune with itself.
    """
    times = time_axis(duration)
    # higher notes ring a little shorter, as struck things do
    decay = 10.0 + frequency / 80.0
    output = []
    for t in times:
        attack = 0.5 - 0.5 * math.cos(math.pi * min(1.0, t / 0.002))
        base = TAU * frequency * t
        note = math.exp(-decay * t) * (
            math.sin(base)
            + 0.42 * math.exp(-decay * 0.8 * t) * math.sin(2.0 * base)
            + 0.20 * math.exp(-decay * 2.0 * t) * math.sin(3.0 * base)
            + 0.07 * math.exp(-decay * 4.0 * t) * math.sin(4.0 * base)
        )
        weight = 0.45 * math.exp(-34.0 * t) * math.sin(0.5 * base)
        output.append(attack * (note + weight))
    return tail_fade(output, 0.06)


# ----------------------------------------------------------- explosions ----

def explosion_sound(seed, drop_from, drop_to, shudder, duration=1.25):
    """A boom that rolls rather than cracks: a body that falls in pitch, a
    wide low rush that closes up, and a tail that shudders as it dies."""
    times = time_axis(duration)
    length = len(times)
    noise = deterministic_noise(length, seed)
    open_rush = normalize_rms(band(noise, 60.0, 2_200.0))
    closed_rush = normalize_rms(band(noise, 40.0, 520.0))

    dry = []
    for index, t in enumerate(times):
        punch = math.sin(glide_phase(t, drop_from, drop_to, duration, 3.0, 0.2)) * math.exp(-3.0 * t)
        over = math.sin(glide_phase(t, drop_from * 2.0, drop_to * 2.0, duration, 3.0, 0.9)) * math.exp(-6.5 * t)
        sub = math.sin(TAU * drop_to * 1.02 * t + 1.0) * math.exp(-3.8 * t)
        # the shudder comes in after the first blow and fades with the tail
        trembling = shiver(t, shudder, 0.55, 9.0)
        dry.append(
            onset(t, 0.002) * trembling * (
                0.56 * punch
                + 0.20 * over
                + 0.26 * sub
                + 0.22 * open_rush[index] * math.exp(-9.5 * t)
                + 0.30 * closed_rush[index] * math.exp(-3.6 * t)
            )
        )

    # driven a little: the loudest moment is thick, not just tall
    wet = room(extend(settle(soft_clip(dry, 1.5), 0.2, 0.12), 0.24), seed + 3, wet=0.28, size=1.8, damping=0.7)
    return soft_clip(tail_fade(wet, 0.22), 1.15)


def death_sound():
    """For the one whose cycle it was: power going out.  A tone that falls
    and closes up, over one heavy beat."""
    duration = 0.95
    times = time_axis(duration)
    dry = []
    for t in times:
        fall = glide_phase(t, 330.0, 49.0, duration, 1.6, 0.0)
        # fewer and fewer overtones as it falls: a filter closing
        brightness = math.exp(-3.4 * t)
        falling = (
            math.sin(fall)
            + 0.50 * brightness * math.sin(2.0 * fall + 0.4)
            + 0.33 * brightness ** 2 * math.sin(3.0 * fall + 0.9)
            + 0.20 * brightness ** 3 * math.sin(5.0 * fall + 1.3)
        )
        beat = math.sin(TAU * 58.0 * t) * math.exp(-9.0 * t)
        dry.append(onset(t) * (0.44 * math.exp(-2.6 * t) * shiver(t, 14.0, 0.3, 4.0) * falling + 0.40 * beat))
    wet = room(extend(settle(dry, 0.25, 0.14), 0.12), 0x52434C51, wet=0.24, size=1.3, damping=0.6)
    return soft_clip(tail_fade(wet, 0.14), 1.1)


# ------------------------------------------------------------ interface ----

def tick_sound(frequency, duration):
    """The smallest sound of the set: something moved.  One soft note."""
    times = time_axis(duration)
    return tail_fade([onset(t, 0.0008) * tone(t, frequency, 62.0, 0.35) for t in times],
                     min(0.02, duration * 0.4))


def two_notes(seed, first, second, gap, duration, warmth=0.5, decay=14.0):
    """Two soft notes, the second arriving while the first still sounds."""
    times = time_axis(duration)
    dry = []
    for t in times:
        value = 0.50 * onset(t, 0.002) * tone(t, first, decay, warmth)
        if t >= gap:
            # the second note eases in: it has the first to lean on
            later = t - gap
            value += 0.56 * onset(later, 0.005) * tone(later, second, decay * 0.8, warmth)
        dry.append(value)
    return tail_fade(room(extend(settle(dry), 0.05), seed, wet=0.16, size=0.6), 0.06)


# ---------------------------------------------------------------- round ----

def count_sound():
    """A number on the screen: one round, dry beat."""
    duration = 0.18
    times = time_axis(duration)
    output = []
    for t in times:
        output.append(onset(t) * (
            0.62 * tone(t, A4, 24.0, 0.45)
            + 0.24 * math.sin(TAU * D3 * t) * math.exp(-40.0 * t)
        ))
    return tail_fade(output, 0.08)


def go_sound():
    """The start: the count's note opened into a chord that swells for a
    moment and trembles away."""
    duration = 0.66
    times = time_axis(duration)
    dry = []
    for t in times:
        swell = onset(t, 0.004)
        chord = (
            0.42 * tone(t, A4, 5.0, 0.5)
            + 0.34 * tone(t, D5, 5.5, 0.4, 0.3)
            + 0.22 * tone(t, A5, 6.5, 0.3, 0.7)
            + 0.20 * math.sin(TAU * D3 * t) * math.exp(-11.0 * t)
        )
        dry.append(swell * shiver(t, 12.0, 0.22, 6.0) * chord)
    return tail_fade(room(extend(settle(dry, 0.25, 0.12), 0.12), 0x52434C61, wet=0.26, size=1.2), 0.14)


# --------------------------------------------------------------- output ----

def peak_normalize(signal, target_dbfs):
    peak = max(abs(value) for value in signal)
    target = 10.0 ** (target_dbfs / 20.0)
    gain = target / peak
    return [value * gain for value in signal]


def finish(signal, loop):
    """Takes out what a speaker cannot use and a mixer would trip over."""
    if loop:
        # A loop is periodic, so its mean is its DC exactly. It is built below
        # 2 kHz, and a filter would break its seam.
        return remove_dc(signal)

    # A sound that starts and ends must start and end at zero: subtracting a
    # mean would lift its silence. Filter the DC out instead, keep everything
    # below 10 kHz, and close the last few milliseconds.
    signal = biquad(signal, "highpass", 24.0)
    signal = biquad(biquad(signal, "lowpass", 9_000.0), "lowpass", 9_000.0)
    return tail_fade(signal, 0.012)


def write_wav(name, signal, target_dbfs, loop):
    normalized = peak_normalize(finish(signal, loop), target_dbfs)
    samples = [max(-32768, min(32767, round(value * 32767.0))) for value in normalized]
    path = OUTPUT / name
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    return [sample / 32768.0 for sample in samples]


def percentile(values, proportion):
    ordered = sorted(values)
    index = min(len(ordered) - 1, round((len(ordered) - 1) * proportion))
    return ordered[index]


def share_above(signal, frequency):
    """How much of the energy lies above a frequency."""
    high = biquad(biquad(signal, "highpass", frequency), "highpass", frequency)
    total = sum(value * value for value in signal)
    return sum(value * value for value in high) / max(total, 1.0e-12)


def sharpest_step(signal):
    """The largest jump from one sample to the next, against the peak: a
    click shows up here before it shows up anywhere else."""
    peak = max(abs(value) for value in signal)
    step = max(abs(signal[index + 1] - signal[index]) for index in range(len(signal) - 1))
    return step / max(peak, 1.0e-12)


def metrics(name, signal, loop=False, smooth=True):
    peak = max(abs(value) for value in signal)
    rms = math.sqrt(sum(value * value for value in signal) / len(signal))
    dc = sum(signal) / len(signal)
    crest = 20.0 * math.log10(peak / max(rms, 1.0e-12))
    peak_dbfs = 20.0 * math.log10(peak)
    rms_dbfs = 20.0 * math.log10(max(rms, 1.0e-12))

    if peak >= 0.98:
        raise SystemExit(f"{name}: insufficient peak headroom ({peak_dbfs:.2f} dBFS)")
    if abs(dc) >= 5.0e-4:
        raise SystemExit(f"{name}: excessive DC offset ({dc:+.6f})")

    top = share_above(signal, 10_000.0)
    if top > 0.01:
        raise SystemExit(f"{name}: {100 * top:.2f}% of its energy is above 10 kHz")

    # Smooth is the rule of the set: next to nothing above 4 kHz, and no
    # jump between two samples that a click would need.
    bright = share_above(signal, 4_000.0)
    step = sharpest_step(signal)
    if smooth and bright > 0.02:
        raise SystemExit(f"{name}: {100 * bright:.2f}% of its energy is above 4 kHz; that is hiss or a click")
    if step > 0.30:
        raise SystemExit(f"{name}: a jump of {100 * step:.0f}% of the peak between two samples; that is a click")

    first_active = next((index for index, value in enumerate(signal) if abs(value) > 1.0e-4), len(signal))
    if not loop and first_active >= round(0.002 * SAMPLE_RATE):
        raise SystemExit(f"{name}: feedback onset is delayed by {first_active / SAMPLE_RATE:.4f}s")

    detail = ""
    if loop:
        derivatives = [abs(signal[index + 1] - signal[index]) for index in range(len(signal) - 1)]
        seam = abs(signal[0] - signal[-1])
        p99 = percentile(derivatives, 0.99)
        if seam > 1.25 * p99:
            raise SystemExit(f"{name}: loop seam {seam:.6f} exceeds derivative p99 {p99:.6f}")
        detail = f", seam {seam:.5f} (p99 {p99:.5f})"
    else:
        tail_length = max(1, round(0.010 * SAMPLE_RATE))
        tail_rms = math.sqrt(sum(value * value for value in signal[-tail_length:]) / tail_length)
        tail_dbfs = 20.0 * math.log10(max(tail_rms, 1.0e-12))
        if tail_dbfs > -55.0:
            raise SystemExit(f"{name}: tail is too loud ({tail_dbfs:.2f} dBFS)")
        detail = f", onset {1000 * first_active / SAMPLE_RATE:.2f}ms, tail {tail_dbfs:.1f}dBFS"

    print(
        f"{name:16} {len(signal) / SAMPLE_RATE:5.2f}s  peak {peak_dbfs:6.2f} dBFS, RMS {rms_dbfs:6.2f} dBFS, "
        f"crest {crest:5.2f} dB, >4k {100 * bright:4.2f}%, step {100 * step:4.1f}%{detail}"
    )


def turn_metrics(name, signal):
    """A turn has to be heard when it is made: half its peak within three
    thousandths of a second, and the peak itself within the first hundredth."""
    peak = max(abs(value) for value in signal)
    half = next(index for index, value in enumerate(signal) if abs(value) >= 0.5 * peak)
    top = next(index for index, value in enumerate(signal) if abs(value) >= 0.999 * peak)
    if half >= round(0.003 * SAMPLE_RATE):
        raise SystemExit(f"{name}: takes {1000 * half / SAMPLE_RATE:.1f}ms to reach half its level; a turn would be heard late")
    if top >= round(0.010 * SAMPLE_RATE):
        raise SystemExit(f"{name}: peaks after {1000 * top / SAMPLE_RATE:.1f}ms; a turn would be heard late")
    print(f"{'':16} half its level after {1000 * half / SAMPLE_RATE:.2f}ms, peak after {1000 * top / SAMPLE_RATE:.2f}ms")


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sounds = (
        # name, signal, peak in dBFS, loop
        # The loops are dense and never stop, so they sit well under the
        # one-shots: they are the bed the rest is heard on.
        ("cyclrun.wav", engine_body(), -10.0, True),
        ("cyclhigh.wav", engine_whine(), -11.0, True),
        ("cyclboost.wav", engine_pull(), -9.0, True),
        ("grind.wav", grind_loop(), -9.0, True),
    )

    # The turns: a note for each way and its overtones. The higher an
    # overtone, the softer, so a full run is a chord and not a climb in level.
    for side, base in (("left", TURN_LEFT), ("right", TURN_RIGHT)):
        for step in range(1, TURN_STEPS + 1):
            sounds += ((f"turn_{side}{step}.wav", turn_note(base * step), -3.5 - 1.5 * (step - 1), False),)

    sounds += (
        ("expl.wav", explosion_sound(0x52434C39, 118.0, 44.0, 19.0), -2.2, False),
        ("death.wav", death_sound(), -3.0, False),

        ("ui_hover.wav", tick_sound(D6, 0.10), -10.0, False),
        ("ui_adjust.wav", tick_sound(A5, 0.10), -10.0, False),
        ("ui_activate.wav", two_notes(0x52434C3E, D5, A5, 0.055, 0.24), -5.0, False),
        ("ui_back.wav", two_notes(0x52434C3F, A5, D5, 0.055, 0.22, warmth=0.3), -6.5, False),

        ("count.wav", count_sound(), -4.0, False),
        ("go.wav", go_sound(), -3.0, False),
    )

    # what earlier versions of the set had and this one does not
    for name in ("turn.wav", "turn2.wav", "turn3.wav", "turn4.wav", "expl2.wav", "expl3.wav",
                 "notice.wav", "zone.wav"):
        (OUTPUT / name).unlink(missing_ok=True)

    for name, source, target, loop in sounds:
        rendered = write_wav(name, source, target, loop)
        # an explosion is the one place where wide noise belongs
        metrics(name, rendered, loop, smooth=not name.startswith("expl"))
        if name.startswith("turn_"):
            turn_metrics(name, rendered)

    print(f"Generated deterministic RCL procedural audio in {OUTPUT}")


if __name__ == "__main__":
    main()
