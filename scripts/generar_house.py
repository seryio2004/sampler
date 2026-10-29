#!/usr/bin/env python3
"""Generate an original house one-shot bank with Python's standard library.

SPDX-License-Identifier: CC0-1.0
No recordings, external samples or additional dependencies.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import wave

RATE = 44100
TAU = 2 * math.pi
ROOT = Path(__file__).resolve().parents[1]
SOUNDS = [
    ("01_kick_house", .56, "Bombo redondo con subgrave y ataque corto", None),
    ("02_clap_house", .30, "Palma de varias capas con cola filtrada", None),
    ("03_hat_cerrado", .105, "Charles cerrado brillante", None),
    ("04_hat_abierto", .32, "Charles abierto para contratiempos", None),
    ("05_shaker", .14, "Shaker fino para semicorcheas", None),
    ("06_rim_house", .13, "Rim corto y seco para sincopas", None),
    ("07_bajo_C", .29, "Bajo sintetico redondo en Do", "C2"),
    ("08_acorde_Cm7", .70, "Acorde menor septima calido", "C3 Eb3 G3 Bb3"),
    ("09_fx_sweep", .48, "Barrido de ruido ascendente corto", None),
    ("10_caja_house", .28, "Caja con cuerpo tonal y ruido", None),
    ("11_ride", .80, "Ride metalico con cola suave", None),
    ("12_organo_Fm", .34, "Stab de organo en Fa menor", "F3 Ab3 C4"),
    ("13_bajo_G", .29, "Bajo sintetico en Sol", "G2"),
    ("14_acorde_Abmaj7", .70, "Acorde mayor septima para variar la armonia", "Ab3 C4 Eb4 G4"),
    ("15_fx_caida", .85, "Caida de ruido filtrado para transiciones", None),
    ("16_vocal_sintetica", .42, "Vocal sintetica tipo ah; sin voz grabada", "C4"),
]


def hz(midi):
    return 440 * 2 ** ((midi - 69) / 12)


def synth(index, duration):
    rng = random.Random(52000 + index)
    frames = round(duration * RATE)
    samples = array.array("f")
    phase = 0.0
    previous_noise = 0.0
    low = 0.0
    high_low = 0.0
    for i in range(frames):
        t = i / RATE
        u = t / duration
        noise = rng.uniform(-1, 1)
        high = (noise - previous_noise) * .5
        previous_noise = noise
        if index == 0:
            phase += (49 + 125 * math.exp(-t * 42)) / RATE
            body = math.sin(TAU * phase) * math.exp(-t * 9)
            click = high * math.exp(-t * 240)
            x = math.tanh(1.5 * body) * .8 + .16 * click
        elif index in (1, 9):
            # High-passed noise with a controlled low-pass gives a softer clap.
            low += .14 * (noise - low)
            band = noise - low
            high_low += .55 * (band - high_low)
            if index == 1:
                bursts = sum(math.exp(-(t - onset) * 150) if t >= onset else 0
                             for onset in (0, .012, .025, .039))
                x = high_low * (bursts * .6 + .28 * math.exp(-t * 16))
            else:
                x = .7 * high_low * math.exp(-t * 21) + .45 * math.sin(TAU * 185 * t) * math.exp(-t * 32)
        elif index in (2, 3, 10):
            metal = sum(math.sin(TAU * freq * t) for freq in (4139, 5683, 7397, 9761, 12347, 14821)) / 6
            if index == 10:
                x = (.65 * metal + .35 * high) * math.exp(-t * 6) + .25 * math.sin(TAU * 2713 * t) * math.exp(-t * 32)
            else:
                decay = 55 if index == 2 else 13
                x = (.45 * metal + .55 * high) * math.exp(-t * decay)
        elif index == 4:
            x = high * math.exp(-t * 32) * (.65 + .35 * math.sin(TAU * 49 * t) ** 2)
        elif index == 5:
            x = (.6 * math.sin(TAU * 820 * t) + .32 * math.sin(TAU * 1733 * t)
                 + .14 * high) * math.exp(-t * 52)
        elif index in (6, 12):
            phase += hz(36 if index == 6 else 43) / RATE
            saw = sum(math.sin(TAU * phase * n) / n for n in range(1, 13)) * .45
            cutoff = 180 + 1900 * math.exp(-t * 22)
            coefficient = 1 - math.exp(-TAU * cutoff / RATE)
            low += coefficient * (saw - low)
            x = math.tanh(1.8 * (.55 * math.sin(TAU * phase) + .65 * low)) * math.exp(-t * 8)
        elif index in (7, 13):
            notes = (48, 51, 55, 58) if index == 7 else (56, 60, 63, 67)
            chord = 0.0
            for note in notes:
                freq = hz(note)
                for n in range(1, 7):
                    chord += (math.sin(TAU * freq * n * t) + .35 * math.sin(TAU * freq * 1.0017 * n * t)) / (n * n)
            coefficient = 1 - math.exp(-TAU * (650 + 2800 * math.exp(-t * 8)) / RATE)
            low += coefficient * (chord / len(notes) - low)
            x = math.tanh(low * 1.5) * math.exp(-t * 5)
        elif index in (8, 14):
            progress = u if index == 8 else 1 - u
            cutoff = 250 + 12000 * progress * progress
            low += (1 - math.exp(-TAU * cutoff / RATE)) * (noise - low)
            x = low * ((math.sin(math.pi * .5 * u) ** 2) if index == 8 else math.exp(-t * 5))
        elif index == 11:
            x = sum((math.sin(TAU * hz(note) * t) + .5 * math.sin(TAU * 2 * hz(note) * t)
                     + .24 * math.sin(TAU * 3 * hz(note) * t)) / 3 for note in (53, 56, 60)) * math.exp(-t * 12)
        else:
            phase += hz(60) / RATE
            # Harmonic amplitudes around two formants; no speech recordings.
            x = sum(math.sin(TAU * phase * n) * (math.exp(-((hz(60) * n - 750) / 300) ** 2)
                    + .4 * math.exp(-((hz(60) * n - 1200) / 220) ** 2)) / n for n in range(1, 13))
            x *= math.sin(math.pi * min(1, t / .1) * .5) * math.exp(-t * 7)
        samples.append(x)
    dc = math.fsum(samples) / frames
    attack = .001 if index in (0, 1, 2, 3, 4, 5, 9, 10) else .005
    release = .008 if index in (0, 1, 2, 3, 4, 5, 9) else .035
    for i in range(frames):
        fade = min(1, i / (attack * RATE), (frames - 1 - i) / (release * RATE))
        samples[i] = (samples[i] - dc) * max(0, fade)
    gain = .86 / max(abs(value) for value in samples)
    pcm = array.array("h", (round(value * gain * 32767) for value in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "samples" / "house")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"origin": "Sintesis original del proyecto; sin grabaciones externas",
                "license": "CC0-1.0", "generator": "scripts/generar_house.py",
                "demo_bpm": 124, "demo_key": "C minor", "samples": []}
    for index, (name, duration, description, note) in enumerate(SOUNDS):
        path = args.output / (name + ".wav")
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            wav.writeframes(synth(index, duration).tobytes())
        manifest["samples"].append({"file": path.name, "seconds": duration,
                                    "description": description, "notes": note,
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        print(path.name)
    (args.output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
