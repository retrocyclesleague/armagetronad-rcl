#!/usr/bin/env python3
"""Generate RCL's deterministic, sample-free gameplay sound set.

Only oscillators, deterministic pseudo-random noise, envelopes and biquad
filters are used.  No recorded or third-party audio enters the output.
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


def circular_box_filter(signal, radius):
    """A loop-safe moving average used for the engine's stochastic layer."""
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


def linear_chirp_phase(t, start_frequency, end_frequency, duration, phase=0.0):
    slope = (end_frequency - start_frequency) / duration
    return TAU * (start_frequency * t + 0.5 * slope * t * t) + phase


def equal_power_tail(t, duration, fade_duration):
    if t <= duration - fade_duration:
        return 1.0
    progress = min(1.0, (t - duration + fade_duration) / fade_duration)
    return math.cos(0.5 * math.pi * progress) ** 2


def engine_loop():
    duration = 2.0
    times = time_axis(duration)
    length = len(times)

    # Integer engine orders and integer-rate modulation make the waveform
    # exactly periodic over two seconds. Half-orders add a lightcycle-like
    # mechanical growl without losing the original low electronic identity.
    fundamental = 52.0
    orders = (
        (0.5, 0.075, 0.30),
        (1.0, 0.36, 0.00),
        (1.5, 0.105, 1.05),
        (2.0, 0.19, 0.42),
        (3.0, 0.115, 1.70),
        (4.0, 0.072, 0.85),
        (6.0, 0.046, 2.20),
        (8.0, 0.030, 0.55),
        (12.0, 0.018, 1.30),
    )

    raw_noise = deterministic_noise(length, 0x52434C31)
    fast_noise = circular_box_filter(raw_noise, 2)
    slow_noise = circular_box_filter(raw_noise, 30)
    texture = normalize_rms([fast - slow for fast, slow in zip(fast_noise, slow_noise)])

    output = []
    for index, t in enumerate(times):
        phase_wobble = 0.10 * math.sin(TAU * 2.0 * t) + 0.035 * math.sin(TAU * 3.0 * t)
        harmonic = 0.0
        for order, amplitude, phase in orders:
            harmonic += amplitude * math.sin(TAU * fundamental * order * t + phase + order * phase_wobble)

        # A small, periodic induction/mechanical residual prevents the motor
        # from reading as a static organ chord when the mixer pitch-shifts it.
        residual_level = 0.030 * (1.0 + 0.22 * math.sin(TAU * 2.0 * t + 0.4))
        amplitude_motion = 0.95 + 0.045 * math.sin(TAU * 3.0 * t + 0.7)
        output.append(amplitude_motion * harmonic + residual_level * texture[index])

    return soft_clip(output, 1.18)


def turn_sound():
    duration = 0.20
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C32)
    click = biquad(biquad(noise, "highpass", 1_300.0), "lowpass", 9_000.0)

    output = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-1_800.0 * t)
        body_envelope = attack * math.exp(-15.5 * t)
        primary = math.sin(linear_chirp_phase(t, 1_180.0, 560.0, duration, 0.15))
        edge = math.sin(linear_chirp_phase(t, 2_050.0, 920.0, duration, 1.10))
        mechanism = math.sin(TAU * 238.0 * t + 0.4) * math.exp(-26.0 * t)
        transient = click[index] * math.exp(-72.0 * t)
        tail = equal_power_tail(t, duration, 0.045)
        output.append(
            tail * (
                0.48 * body_envelope * primary
                + 0.19 * body_envelope * edge
                + 0.13 * attack * mechanism
                + 0.30 * transient
            )
        )
    return output


def scrape_sound():
    # The legacy mixer plays this asset at 4x. Authoring it low gives the
    # in-game scrape useful 1-10 kHz energy instead of ultrasonic hiss.
    duration = 0.64
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C33)
    grit = biquad(biquad(noise, "highpass", 240.0), "lowpass", 2_350.0)
    grit = normalize_rms(grit)

    output = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-2_200.0 * t)
        sustain_decay = math.exp(-2.9 * t)
        chatter = 0.72 + 0.28 * max(0.0, math.sin(TAU * 137.0 * t + 0.3))
        friction_phase = linear_chirp_phase(t, 285.0, 410.0, duration, 0.6)
        friction = math.sin(friction_phase) + 0.34 * math.sin(2.03 * friction_phase + 0.8)
        tail = equal_power_tail(t, duration, 0.16)
        output.append(
            tail * attack * sustain_decay * (
                0.205 * chatter * grit[index]
                + 0.075 * math.exp(-3.8 * t) * friction
            )
        )
    return output


def explosion_sound():
    duration = 1.10
    times = time_axis(duration)
    noise = deterministic_noise(len(times), 0x52434C34)
    crack = biquad(biquad(noise, "highpass", 1_500.0), "lowpass", 10_500.0)
    body_noise = biquad(biquad(noise, "highpass", 45.0), "lowpass", 2_100.0)
    crack = normalize_rms(crack)
    body_noise = normalize_rms(body_noise)

    output = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-2_400.0 * t)
        low_phase = linear_chirp_phase(t, 112.0, 47.0, duration, 0.55)
        low_punch = math.sin(low_phase) * math.exp(-4.6 * t)
        sub = math.sin(TAU * 52.0 * t + 1.0) * math.exp(-5.8 * t)
        metal = (
            math.sin(TAU * 173.0 * t + 0.2) * math.exp(-8.2 * t)
            + 0.54 * math.sin(TAU * 287.0 * t + 1.4) * math.exp(-10.5 * t)
        )
        bright_transient = crack[index] * math.exp(-55.0 * t)
        debris = body_noise[index] * math.exp(-6.0 * t)
        tail = equal_power_tail(t, duration, 0.14)
        output.append(
            tail * attack * (
                0.48 * low_punch
                + 0.19 * sub
                + 0.13 * metal
                + 0.255 * bright_transient
                + 0.245 * debris
            )
        )
    return soft_clip(output, 1.12)


def ui_chirp(duration, start_frequency, end_frequency, seed, descending=False):
    times = time_axis(duration)
    noise = deterministic_noise(len(times), seed)
    tick = biquad(noise, "highpass", 2_400.0)
    output = []
    for index, t in enumerate(times):
        attack = 1.0 - math.exp(-2_600.0 * t)
        envelope = attack * math.exp(-20.0 * t)
        phase = linear_chirp_phase(t, start_frequency, end_frequency, duration, 0.2)
        harmonic_phase = linear_chirp_phase(t, start_frequency * 1.5, end_frequency * 1.5, duration, 1.0)
        transient = 0.06 * tick[index] * math.exp(-95.0 * t)
        tail = equal_power_tail(t, duration, min(0.07, duration * 0.60))
        weight = 0.20 if descending else 0.16
        output.append(
            tail * (
                0.31 * envelope * math.sin(phase)
                + weight * envelope * math.sin(harmonic_phase)
                + transient
            )
        )
    return output


def peak_normalize(signal, target_dbfs):
    signal = remove_dc(signal)
    peak = max(abs(value) for value in signal)
    target = 10.0 ** (target_dbfs / 20.0)
    gain = target / peak
    return [value * gain for value in signal]


def write_wav(name, signal, target_dbfs):
    normalized = peak_normalize(signal, target_dbfs)
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
        f"{name:16} peak {peak_dbfs:6.2f} dBFS, RMS {rms_dbfs:6.2f} dBFS, "
        f"crest {crest:5.2f} dB, DC {dc:+.6f}{detail}"
    )


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sounds = (
        ("cyclrun.wav", engine_loop(), -5.0, True),
        ("turn.wav", turn_sound(), -3.5, False),
        ("scrape.wav", scrape_sound(), -5.0, False),
        ("expl.wav", explosion_sound(), -2.2, False),
        ("ui_hover.wav", ui_chirp(0.085, 1_450.0, 1_180.0, 0x52434C35), -9.0, False),
        ("ui_activate.wav", ui_chirp(0.20, 690.0, 1_320.0, 0x52434C36), -5.0, False),
        ("ui_back.wav", ui_chirp(0.20, 1_080.0, 510.0, 0x52434C37, True), -6.0, False),
    )

    for name, source, target, loop in sounds:
        rendered = write_wav(name, source, target)
        metrics(name, rendered, loop)

    print(f"Generated deterministic RCL procedural audio in {OUTPUT}")


if __name__ == "__main__":
    main()
