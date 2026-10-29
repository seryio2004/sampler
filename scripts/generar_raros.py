#!/usr/bin/env python3
"""Generate original experimental WAVs using only the Python standard library.

SPDX-License-Identifier: CC0-1.0
No recordings, downloaded samples or external dependencies.
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
ROOT = Path(__file__).resolve().parents[1]
TAU = 2 * math.pi
SOUNDS = [
    ("01_radio_alien", 3.8, "Radio alien: barrido FM y balizas que se cruzan."),
    ("02_ballena_espacial", 5.4, "Ballena espacial: lamento grave que cae y vuelve a subir."),
    ("03_robot_roto", 3.2, "Robot roto: vocales digitales y cambios de tono escalonados."),
    ("04_cristales", 4.6, "Cristales suspendidos: parciales inarmonicos que flotan."),
    ("05_nave_abandonada", 6.4, "Nave abandonada: drone grave con batidos y respiracion."),
    ("06_enjambre", 4.2, "Enjambre: nubes de microtonos que orbitan."),
    ("07_laser_derretido", 3.7, "Laser derretido: caida continua y modulacion viscosa."),
    ("08_fantasmas_cinta", 5.6, "Fantasmas de cinta: coro sintetico con desafinacion lenta."),
    ("09_portal_metal", 4.8, "Portal metalico: resonancias que se abren y se retuercen."),
    ("10_sueno_maquina", 4.4, "Sueno de maquina: arpegio espectral difuso."),
    ("11_agua_digital", 4.9, "Agua digital: burbujas tonales y corriente filtrada."),
    ("12_voz_inversa", 5.2, "Voz inversa: formantes sinteticos que crecen hacia atras."),
]


def synth(index, seconds):
    rng = random.Random(3070 + index)
    frames = round(seconds * RATE)
    output = array.array("f")
    phase = 0.0
    low = 0.0
    held = 0.0
    previous_step = -1
    for i in range(frames):
        t = i / RATE
        u = t / seconds
        noise = rng.uniform(-1, 1)
        low += .015 * (noise - low)
        if index == 0:
            hz = 260 + 170 * math.sin(TAU * .37 * t) + 45 * math.sin(TAU * 1.3 * t)
            phase += hz / RATE
            x = math.sin(TAU * phase + 3.7 * math.sin(TAU * 79 * t)) * (.55 + .35 * math.sin(TAU * 2.1 * t)) + .12 * low
        elif index == 1:
            hz = 85 + 210 * (2 * u - 1) ** 2 + 9 * math.sin(TAU * 3 * t)
            phase += hz / RATE
            x = math.sin(TAU * phase + 1.2 * math.sin(TAU * phase * .501)) + .18 * math.sin(TAU * phase * 2.01)
        elif index == 2:
            step = int(t * 9)
            if step != previous_step:
                held = rng.choice([83, 117, 156, 209, 278, 351])
                previous_step = step
            phase += held / RATE
            carrier = math.sin(TAU * phase)
            voice = math.sin(TAU * 710 * t + 2 * carrier) + .5 * math.sin(TAU * 1230 * t + 3 * carrier)
            x = voice * (.45 + .4 * math.sin(TAU * 1.7 * t))
        elif index == 3:
            x = sum(math.sin(TAU * hz * t + .6 * math.sin(TAU * (.11 + n * .04) * t)) * (.5 + .5 * math.sin(TAU * .13 * t + n)) / (n + 2)
                    for n, hz in enumerate([293.7, 467.3, 731.1, 1129.7, 1813.1, 2609.3]))
        elif index == 4:
            x = (.7 * math.sin(TAU * 55 * t) + .5 * math.sin(TAU * 55.8 * t)
                 + .24 * math.sin(TAU * 82.7 * t + 2 * math.sin(TAU * .21 * t)) + low) * (.6 + .3 * math.sin(TAU * .31 * t))
        elif index == 5:
            x = sum(math.sin(TAU * (170 + n * 37.9) * t + (2 + n * .2) * math.sin(TAU * (.4 + n * .17) * t)) / 9 for n in range(9))
        elif index == 6:
            phase += (65 + 2300 * math.exp(-1.7 * t)) / RATE
            x = math.sin(TAU * phase + 4 * math.sin(TAU * phase * .127)) * (.7 + .2 * math.sin(TAU * 5.3 * t))
        elif index == 7:
            wobble = .012 * math.sin(TAU * .7 * t) + .002 * math.sin(TAU * 5.1 * t)
            x = sum(math.sin(TAU * hz * (t + wobble) + .6 * math.sin(TAU * .19 * t + n)) / (n + 2)
                    for n, hz in enumerate([130.81, 155.56, 196.0, 261.1]))
        elif index == 8:
            phase += (73 + 62 * u) / RATE
            x = sum(math.sin(TAU * phase * ratio + (1 + 5 * u) * math.sin(TAU * 17.3 * t)) / (n + 2)
                    for n, ratio in enumerate([1, 2.731, 4.193, 6.791, 9.37]))
        elif index == 9:
            # Overlapping soft notes, with no percussive attack.
            x = 0
            for n, hz in enumerate([164.81, 246.94, 311.13, 466.16, 659.25, 739.99]):
                window = max(0, math.sin(math.pi * (t / 2.8 - n * .14))) ** 2
                x += .23 * window * math.sin(TAU * hz * t + .7 * math.sin(TAU * hz * 1.501 * t))
        elif index == 10:
            phase += (230 + 150 * math.sin(TAU * .67 * t) + 65 * math.sin(TAU * 2.3 * t)) / RATE
            x = .55 * math.sin(TAU * phase + 2.4 * math.sin(TAU * 6.7 * t)) * (.55 + .4 * math.sin(TAU * 3.1 * t)) + 1.6 * low
        else:
            phase += (105 + 42 * u) / RATE
            pulse = math.sin(TAU * phase)
            swell = (.5 - .5 * math.cos(TAU * 1.1 * t)) ** 2
            x = swell * (math.sin(TAU * (420 + 180 * u) * t + 2.5 * pulse)
                         + .5 * math.sin(TAU * (1300 - 370 * u) * t + pulse))
        output.append(x)
    # Remove DC before a slow entrance and release; keep intentional noise textures.
    dc = math.fsum(output) / frames
    for i in range(frames):
        fade = min(1, i / (RATE * .08), (frames - 1 - i) / (RATE * .4))
        output[i] = (output[i] - dc) * math.sin(math.pi * .5 * max(0, fade)) ** 2
    gain = .78 / max(abs(x) for x in output)
    pcm = array.array("h", (round(x * gain * 32767) for x in output))
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "samples" / "raros")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"origin": "Sintesis original del proyecto; sin grabaciones externas",
                "license": "CC0-1.0", "generator": "scripts/generar_raros.py", "samples": []}
    for index, (name, duration, description) in enumerate(SOUNDS):
        pcm = synth(index, duration)
        path = args.output / (name + ".wav")
        with wave.open(str(path), "wb") as wav:
            wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            wav.writeframes(pcm.tobytes())
        manifest["samples"].append({"file": path.name, "seconds": duration, "description": description,
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        print(f"{path.name}: {duration:.1f} s")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
