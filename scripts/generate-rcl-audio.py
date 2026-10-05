#!/usr/bin/env python3
"""Generate RCL's deterministic, sample-free sound set.

Only oscillators, deterministic pseudo-random noise, envelopes, filters and
short delay lines are used.  No recorded or third-party audio enters the
output.

The set is layered by what the game does with it:

  engine     cyclrun (body), cyclhigh (whine, comes in with speed),
             cyclboost (rush while a wall pulls the cycle along); all loops
  turns      turn, turn3 (left) and turn2, turn4 (right); one is picked
  scrapes    scrape, scrape2, scrape3; a string of them while grinding
  explosions expl, expl2, expl3; death for the one whose cycle it was
  interface  ui_hover, ui_adjust, ui_activate, ui_back
  round      count (a number on screen), go (the zero), notice (a message),
             zone (a zone appears)

Everything is written band-limited below 10 kHz: the game's default output
rate is 22.05 kHz and its mixer has no filter of its own.  The engine loops
stay below 2 kHz, because they are played at up to four times their pitch.
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
    elif kind == "bandpass":
        b0 = alpha
        b1 = 0.0
        b2 = -alpha
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
    return biquad(biquad(signal, "highpass", low), "lowpass", high)


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


def settle(signal, share=0.3, longest=0.05):
    """Brings the end of a dry sound down to nothing before a space is put
    around it; cut off at whatever level it has, it would end in a click."""
    return tail_fade(signal, min(longest, share * len(signal) / SAMPLE_RATE))


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


def strike(t, attack_rate, decay_rate):
    """An envelope that is there at once and dies away."""
    return (1.0 - math.exp(-attack_rate * t)) * math.exp(-decay_rate * t)


def bell(t, frequency, decay_rate, index=1.6, ratio=2.76, phase=0.0):
    """A struck, glassy tone: a carrier whose modulation dies before it does."""
    modulation = index * math.exp(-decay_rate * 2.2 * t) * math.sin(TAU * frequency * ratio * t)
    return math.sin(TAU * frequency * t + modulation + phase) * math.exp(-decay_rate * t)


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


# --------------------------------------------------------------- engine ----

LOOP_SECONDS = 2.0


def periodic(frequency):
    """The nearest pitch that fits the loop a whole number of times."""
    return round(frequency * LOOP_SECONDS) / LOOP_SECONDS


def engine_body():
    """Two saws a hair apart: every overtone beats at its own slow rate, so
    the tone keeps moving like an engine under load, and it is exactly
    periodic over the loop.

    The weight is in the overtones, not the fundamental: the game plays this
    at two thirds of its pitch while cycles wait for the start, and a tone
    that is mostly sub turns to mud there and to nothing on small speakers.
    """
    times = time_axis(LOOP_SECONDS)
    length = len(times)
    fundamental = periodic(82.5)
    detuned = periodic(83.0)
    octave = periodic(165.5)

    # Partials up to 1.6 kHz, lifted around the body's resonance. Every one
    # starts at a phase of its own: lined up, the two saws would fall in and
    # out of step all at once, and the engine would throb once per loop.
    phases = random.Random(0x52434C30)
    partials = []
    for base, level in ((fundamental, 1.0), (detuned, 0.85), (octave, 0.35)):
        harmonic = 1
        while base * harmonic <= 1_600.0:
            frequency = base * harmonic
            resonance = 1.0 + 1.4 * math.exp(-((frequency - 400.0) / 190.0) ** 2)
            rolloff = 1.0 / math.sqrt(1.0 + (frequency / 1_200.0) ** 4)
            partials.append((frequency, level * resonance * rolloff / harmonic ** 0.75, phases.uniform(0.0, TAU)))
            harmonic += 1

    # a residual between about 150 Hz and 1.2 kHz
    raw_noise = deterministic_noise(length, 0x52434C31)
    texture = circular_band(raw_noise, 4, 36)
    sub_octave = periodic(41.5)

    output = []
    for index, t in enumerate(times):
        saws = 0.0
        for frequency, amplitude, phase in partials:
            saws += amplitude * math.sin(TAU * frequency * t + phase)
        sub = 0.16 * math.sin(TAU * sub_octave * t + 0.3)
        # the residual breathes at whole-number rates, so the loop holds
        breath = 0.045 * (1.0 + 0.3 * math.sin(TAU * 2.0 * t + 0.4))
        motion = 0.94 + 0.05 * math.sin(TAU * 3.0 * t + 0.7)
        output.append(motion * (0.34 * saws + sub) + breath * texture[index])
    return soft_clip(output, 1.15)


def engine_whine():
    """The upper layer: a motor's tone with sidebands, and a narrow hiss.
    It only plays above cruising speed, where it is pitched up further."""
    times = time_axis(LOOP_SECONDS)
    length = len(times)
    carrier = periodic(440.0)
    second = periodic(660.5)
    order = periodic(55.0)

    # a narrow hiss between about 1.2 and 2.2 kHz
    raw_noise = deterministic_noise(length, 0x52434C41)
    hiss = circular_band(raw_noise, 2, 4)

    output = []
    for index, t in enumerate(times):
        # sidebands a whole engine order apart keep it periodic
        wobble = 1.1 + 0.35 * math.sin(TAU * 2.0 * t)
        motor = math.sin(TAU * carrier * t + wobble * math.sin(TAU * order * t))
        upper = math.sin(TAU * second * t + 0.8 * math.sin(TAU * order * 2.0 * t + 0.6))
        pulse = 0.82 + 0.18 * math.sin(TAU * 4.0 * t + 1.1)
        air = 0.20 * (1.0 + 0.4 * math.sin(TAU * 3.0 * t)) * hiss[index]
        output.append(pulse * (0.50 * motor + 0.24 * upper) + air)
    return soft_clip(output, 1.05)


def engine_boost():
    """Air being pushed aside: a band of noise that surges, over a low
    pressure tone.  The game fades it in with the cycle's acceleration."""
    times = time_axis(LOOP_SECONDS)
    length = len(times)
    # two bands of noise: about 0.6 to 2.2 kHz, and 200 to 700 Hz under it
    raw_noise = deterministic_noise(length, 0x52434C42)
    rush = circular_band(raw_noise, 2, 9)
    low_rush = circular_band(deterministic_noise(length, 0x52434C43), 7, 27)
    tone_a = periodic(165.0)
    tone_b = periodic(165.5)

    output = []
    for index, t in enumerate(times):
        surge = 0.72 + 0.20 * math.sin(TAU * 1.0 * t) + 0.08 * math.sin(TAU * 5.0 * t + 0.9)
        pressure = 0.5 * (math.sin(TAU * tone_a * t) + math.sin(TAU * tone_b * t + 1.0))
        output.append(surge * (0.62 * rush[index] + 0.30 * low_rush[index]) + 0.16 * pressure)
    return soft_clip(output, 1.1)


# ---------------------------------------------------------------- turns ----

def turn_sound(seed, top, bottom, thump, ring=None, curve=2.2, duration=0.24):
    """A turn: a click, a zip that falls from one pitch to another, a thump
    for weight, and a short space around it."""
    times = time_axis(duration)
    noise = deterministic_noise(len(times), seed)
    click = normalize_rms(band(noise, 1_900.0, 8_500.0))

    dry = []
    for index, t in enumerate(times):
        zip_envelope = strike(t, 2_600.0, 13.0)
        zip_tone = math.sin(glide_phase(t, top, bottom, duration, curve, 0.2))
        zip_edge = math.sin(glide_phase(t, top * 2.0, bottom * 2.0, duration, curve, 1.1))
        zip_air = math.sin(glide_phase(t, top * 3.0, bottom * 3.0, duration, curve, 0.6))
        body = math.sin(TAU * thump * t + 0.4) * strike(t, 1_500.0, 24.0)
        transient = click[index] * math.exp(-300.0 * t)
        value = (
            0.50 * zip_envelope * zip_tone
            + 0.30 * zip_envelope * math.exp(-10.0 * t) * zip_edge
            + 0.13 * zip_envelope * math.exp(-26.0 * t) * zip_air
            + 0.24 * body
            + 0.26 * transient
        )
        if ring:
            value += 0.14 * bell(t, ring, 18.0, 1.2)
        dry.append(value)

    return tail_fade(room(extend(settle(dry), 0.06), seed + 7, wet=0.20, size=0.55), 0.05)


# -------------------------------------------------------------- scrapes ----

def scrape_sound(seed, partials, chatter_rate, duration=0.20):
    """Metal on a wall of light: grit that chatters, and a few partials that
    ring without being in tune with each other."""
    times = time_axis(duration)
    noise = deterministic_noise(len(times), seed)
    grit = normalize_rms(band(noise, 1_600.0, 7_500.0))
    rub = normalize_rms(band(noise, 220.0, 900.0))

    output = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-1_400.0 * t)
        decay = math.exp(-11.0 * t)
        chatter = 0.6 + 0.4 * max(0.0, math.sin(TAU * chatter_rate * t + 0.3))
        ringing = 0.0
        for frequency, level, rate in partials:
            ringing += level * math.sin(TAU * frequency * t + frequency) * math.exp(-rate * 0.6 * t)
        output.append(
            attack * (
                0.17 * decay * chatter * grit[index]
                + 0.12 * math.exp(-16.0 * t) * rub[index]
                + 0.52 * ringing
            )
        )
    return tail_fade(output, 0.05)


# ----------------------------------------------------------- explosions ----

def explosion_sound(seed, drop_from, drop_to, modes, debris_grains, duration=1.25):
    """A crack that is there at once, a body that falls in pitch, metal that
    rings, debris that keeps arriving, and the space it happens in."""
    times = time_axis(duration)
    length = len(times)
    noise = deterministic_noise(length, seed)
    crack = normalize_rms(band(noise, 1_600.0, 9_500.0))
    body_noise = normalize_rms(band(noise, 50.0, 1_900.0))
    grit = normalize_rms(band(deterministic_noise(length, seed + 1), 900.0, 6_000.0))

    # debris: short bursts scattered over the tail, thinning out
    generator = random.Random(seed + 2)
    debris_envelope = [0.0] * length
    for _ in range(debris_grains):
        at = generator.uniform(0.06, duration * 0.72)
        level = generator.uniform(0.3, 1.0) * math.exp(-2.6 * at)
        width = generator.uniform(0.004, 0.018)
        start = round(at * SAMPLE_RATE)
        for offset in range(round(width * 6 * SAMPLE_RATE)):
            index = start + offset
            if index >= length:
                break
            debris_envelope[index] += level * math.exp(-offset / (width * SAMPLE_RATE))

    dry = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-2_600.0 * t)
        punch = math.sin(glide_phase(t, drop_from, drop_to, duration, 3.0, 0.55)) * math.exp(-3.3 * t)
        sub = math.sin(TAU * drop_to * 1.02 * t + 1.0) * math.exp(-4.4 * t)
        metal = 0.0
        for frequency, level, rate in modes:
            metal += level * math.sin(TAU * frequency * t + frequency * 0.01) * math.exp(-rate * t)
        dry.append(
            attack * (
                0.52 * punch
                + 0.24 * sub
                + 0.15 * metal
                + 0.20 * crack[index] * math.exp(-48.0 * t)
                + 0.30 * body_noise[index] * math.exp(-4.4 * t)
                + 0.18 * grit[index] * debris_envelope[index]
            )
        )

    # driven a little: the loudest moment is thick, not just tall
    wet = room(extend(settle(soft_clip(dry, 1.6), 0.2, 0.12), 0.22), seed + 3, wet=0.30, size=1.7, damping=0.6)
    return soft_clip(tail_fade(wet, 0.20), 1.2)


def death_sound():
    """For the one whose cycle it was: power going out.  A tone that falls
    and closes up, over one heavy beat."""
    duration = 0.95
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C50)
    hiss = normalize_rms(band(noise, 400.0, 3_500.0))

    dry = []
    for index, t in enumerate(times):
        fall = glide_phase(t, 330.0, 49.0, duration, 1.6, 0.1)
        # fewer and fewer overtones as it falls: a filter closing
        brightness = math.exp(-3.4 * t)
        tone = (
            math.sin(fall)
            + 0.50 * brightness * math.sin(2.0 * fall + 0.4)
            + 0.33 * brightness ** 2 * math.sin(3.0 * fall + 0.9)
            + 0.20 * brightness ** 3 * math.sin(5.0 * fall + 1.3)
        )
        beat = math.sin(TAU * 58.0 * t) * strike(t, 2_000.0, 9.0)
        dry.append(
            0.42 * strike(t, 2_400.0, 2.6) * tone
            + 0.40 * beat
            + 0.10 * hiss[index] * math.exp(-11.0 * t)
        )
    wet = room(extend(settle(dry, 0.25, 0.14), 0.12), 0x52434C51, wet=0.24, size=1.3, damping=0.6)
    return soft_clip(tail_fade(wet, 0.14), 1.1)


# ------------------------------------------------------------ interface ----

def tick_sound(seed, frequency, duration, brightness=1.0):
    """The smallest sound of the set: something moved."""
    times = time_axis(duration)
    noise = deterministic_noise(len(times), seed)
    tick = normalize_rms(band(noise, 2_600.0, 9_000.0))
    output = []
    for index, t in enumerate(times):
        tone = bell(t, frequency, 70.0, 0.9 * brightness, 2.0)
        output.append(0.55 * tone + 0.10 * brightness * tick[index] * math.exp(-900.0 * t))
    return tail_fade(output, min(0.02, duration * 0.4))


def two_notes(seed, first, second, gap, duration, brightness=1.0, decay=14.0):
    """Two struck notes, the second arriving while the first still sounds."""
    times = time_axis(duration)
    noise = deterministic_noise(len(times), seed)
    tick = normalize_rms(band(noise, 2_400.0, 8_500.0))
    dry = []
    for index, t in enumerate(times):
        value = 0.50 * bell(t, first, decay, 1.4 * brightness)
        value += 0.07 * brightness * tick[index] * math.exp(-700.0 * t)
        if t >= gap:
            later = t - gap
            value += 0.56 * (1.0 - math.exp(-3_000.0 * later)) * bell(later, second, decay * 0.8, 1.5 * brightness)
        dry.append(value)
    return tail_fade(room(extend(settle(dry), 0.05), seed + 5, wet=0.16, size=0.6), 0.06)


# ---------------------------------------------------------------- round ----

def count_sound():
    """A number on the screen: one clean, dry beat."""
    duration = 0.17
    times = time_axis(duration)
    output = []
    for t in times:
        output.append(
            0.60 * bell(t, A4, 26.0, 0.7, 2.0)
            + 0.22 * math.sin(TAU * A5 * t) * strike(t, 2_500.0, 40.0)
            + 0.20 * math.sin(TAU * 147.0 * t) * strike(t, 1_800.0, 45.0)
        )
    return tail_fade(output, 0.08)


def go_sound():
    """The start: the count's note an octave up, opened into a chord, with
    air rushing in behind it."""
    duration = 0.62
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C60)
    air = normalize_rms(band(noise, 900.0, 7_000.0))
    dry = []
    for index, t in enumerate(times):
        chord = (
            0.44 * bell(t, A5, 6.5, 1.5)
            + 0.30 * bell(t, D6, 7.5, 1.2, 2.0, 0.7)
            + 0.24 * bell(t, D5, 6.0, 1.0, 2.0, 0.3)
            + 0.14 * math.sin(TAU * 110.0 * t) * strike(t, 1_800.0, 16.0)
        )
        whoosh = 0.07 * air[index] * (1.0 - math.exp(-40.0 * t)) * math.exp(-9.0 * t)
        dry.append(chord + whoosh)
    return tail_fade(room(extend(settle(dry, 0.25, 0.12), 0.12), 0x52434C61, wet=0.26, size=1.2), 0.14)


def notice_sound():
    """The game has something to say: two soft notes, a fifth apart."""
    return two_notes(0x52434C62, D5, A5, 0.085, 0.46, brightness=0.7, decay=9.0)


def zone_sound():
    """A zone comes into being: a tone that rises a fifth and shimmers, with
    air opening up around it."""
    duration = 0.85
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C63)
    air = normalize_rms(band(noise, 500.0, 5_500.0))
    dry = []
    for index, t in enumerate(times):
        swell = (1.0 - math.exp(-9.0 * t)) * math.exp(-2.4 * t)
        rise = glide_phase(t, D4 * 0.5, A4 * 0.5, duration, 2.0)
        shimmer = 1.0 + 0.25 * math.sin(TAU * 11.0 * t)
        tone = math.sin(rise) + 0.45 * math.sin(2.0 * rise + 0.5) + 0.22 * math.sin(3.0 * rise + 1.0)
        onset = 0.30 * bell(t, D5, 12.0, 1.0)
        dry.append(0.46 * swell * shimmer * tone + onset + 0.045 * swell * air[index])
    return tail_fade(room(extend(settle(dry, 0.25, 0.14), 0.14), 0x52434C64, wet=0.30, size=1.5), 0.16)


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


def top_octave_share(signal):
    """How much of the energy lies above 10 kHz, where the game's default
    output rate would fold it back."""
    high = biquad(biquad(signal, "highpass", 10_000.0), "highpass", 10_000.0)
    total = sum(value * value for value in signal)
    return sum(value * value for value in high) / max(total, 1.0e-12)


def metrics(name, signal, loop=False):
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

    share = top_octave_share(signal)
    if share > 0.01:
        raise SystemExit(f"{name}: {100 * share:.2f}% of its energy is above 10 kHz")

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
        f"crest {crest:5.2f} dB, >10k {100 * share:4.2f}%{detail}"
    )


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sounds = (
        # name, signal, peak in dBFS, loop
        # The loops are dense and never stop, so they sit well under the
        # one-shots: they are the bed the rest is heard on.
        ("cyclrun.wav", engine_body(), -10.0, True),
        ("cyclhigh.wav", engine_whine(), -11.0, True),
        ("cyclboost.wav", engine_boost(), -8.0, True),

        ("turn.wav", turn_sound(0x52434C32, D6, 620.0, 150.0), -3.5, False),
        ("turn3.wav", turn_sound(0x52434C33, C6, 560.0, 140.0, ring=D5, duration=0.26), -3.5, False),
        ("turn2.wav", turn_sound(0x52434C34, 1_397.0, 740.0, 165.0), -3.5, False),
        ("turn4.wav", turn_sound(0x52434C35, 1_319.0, 700.0, 172.0, ring=A5, curve=2.8), -3.5, False),

        ("scrape.wav", scrape_sound(0x52434C36, ((2_130.0, 0.5, 28.0), (3_410.0, 0.4, 36.0), (5_270.0, 0.25, 48.0)), 137.0), -6.0, False),
        ("scrape2.wav", scrape_sound(0x52434C37, ((1_780.0, 0.5, 30.0), (2_950.0, 0.4, 34.0), (4_610.0, 0.3, 44.0)), 163.0, duration=0.18), -6.0, False),
        ("scrape3.wav", scrape_sound(0x52434C38, ((2_480.0, 0.45, 26.0), (3_890.0, 0.4, 40.0), (6_050.0, 0.2, 52.0)), 119.0, duration=0.22), -6.0, False),

        ("expl.wav", explosion_sound(0x52434C39, 118.0, 44.0, ((173.0, 1.0, 8.2), (287.0, 0.54, 10.5), (431.0, 0.3, 13.0)), 26), -2.2, False),
        ("expl2.wav", explosion_sound(0x52434C3A, 132.0, 39.0, ((157.0, 1.0, 7.4), (263.0, 0.6, 9.5), (397.0, 0.35, 12.0), (611.0, 0.2, 16.0)), 34, duration=1.40), -2.2, False),
        ("expl3.wav", explosion_sound(0x52434C3B, 104.0, 48.0, ((191.0, 1.0, 9.0), (311.0, 0.5, 11.5), (503.0, 0.28, 15.0)), 20, duration=1.10), -2.2, False),
        ("death.wav", death_sound(), -3.0, False),

        ("ui_hover.wav", tick_sound(0x52434C3C, D6, 0.07), -10.0, False),
        ("ui_adjust.wav", tick_sound(0x52434C3D, A5, 0.06, 0.7), -10.0, False),
        ("ui_activate.wav", two_notes(0x52434C3E, D5, A5, 0.055, 0.24), -5.0, False),
        ("ui_back.wav", two_notes(0x52434C3F, A5, D5, 0.055, 0.22, brightness=0.6), -6.5, False),

        ("count.wav", count_sound(), -4.0, False),
        ("go.wav", go_sound(), -3.0, False),
        ("notice.wav", notice_sound(), -6.0, False),
        ("zone.wav", zone_sound(), -5.0, False),
    )

    for name, source, target, loop in sounds:
        rendered = write_wav(name, source, target, loop)
        metrics(name, rendered, loop)

    print(f"Generated deterministic RCL procedural audio in {OUTPUT}")


if __name__ == "__main__":
    main()
