#!/usr/bin/env python3
"""Collect read-only Pi/ALSA evidence and produce deterministic listening tests.

Never changes mixer, boot settings, GPIO mode, services, or package configuration.
Nothing is played automatically. Standard library only.
"""
import argparse
import array
import datetime
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import wave

PROJECT = Path(__file__).resolve().parents[1]


def run(args, timeout=8, cwd=None):
    if not shutil.which(str(args[0])):
        return {"command": args, "error": "Comando no instalado"}
    try:
        p = subprocess.run(args, capture_output=True, text=True, errors="replace",
                           timeout=timeout, cwd=cwd)
        return {"command": args, "returncode": p.returncode,
                "stdout": p.stdout, "stderr": p.stderr}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"command": args, "error": str(exc)}


def read(path):
    try:
        return Path(path).read_text(errors="replace").replace("\x00", "").strip()
    except OSError as exc:
        return str(exc)


def reference(path, rate, tone):
    frames = rate * 4
    samples = array.array("h")
    amplitude = 32767 * 10 ** (-18 / 20)  # Quiet reference: -18 dBFS.
    for i in range(frames):
        fade = min(1., i / (rate * .02), (frames - 1 - i) / (rate * .02))
        value = round(amplitude * math.sin(2 * math.pi * 1000 * i / rate) * fade) if tone else 0
        samples.extend((value, value))
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 2, rate, 0, "NONE", "not compressed"))
        wav.writeframes(samples.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Carpeta para el informe y los WAV")
    args = parser.parse_args()
    output = args.output or Path(tempfile.mkdtemp(prefix="sampler-audio-"))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"date": datetime.datetime.now().astimezone().isoformat(),
              "model": read("/proc/device-tree/model"), "commands": []}
    commands = [
        ["uname", "-a"], ["aplay", "-l"], ["aplay", "-L"],
        ["vcgencmd", "get_throttled"], ["vcgencmd", "measure_temp"],
        ["vcgencmd", "get_config", "int"], ["amixer", "scontents"],
    ]
    for command in commands:
        result = run(command)
        if command[-2:] == ["get_config", "int"] and "stdout" in result:
            result["stdout"] = "\n".join(line for line in result["stdout"].splitlines()
                                          if re.search(r"audio|pwm|dither", line, re.I))
        report["commands"].append(result)
    pin_tool = "pinctrl" if shutil.which("pinctrl") else "raspi-gpio"
    report["commands"].append(run([pin_tool, "get"]))
    report["alsa"] = {path: read(path) for path in
                      ("/proc/asound/cards", "/proc/asound/pcm", "/proc/asound/version")}
    cards = re.findall(r"^\s*(\d+)\s+\[", report["alsa"]["/proc/asound/cards"], re.M)
    for card in cards:
        report["commands"].append(run(["amixer", "-c", card, "scontents"]))
    report["boot_audio_lines"] = {}
    for name in ("/boot/firmware/config.txt", "/boot/config.txt"):
        if Path(name).is_file():
            report["boot_audio_lines"][name] = [
                line for line in read(name).splitlines()
                if re.search(r"audio|pwm|dither|dtoverlay|dtparam|gpio|include|^\s*\[", line, re.I)]
    for rate in (22050, 44100, 48000):
        reference(output / f"tono_{rate}.wav", rate, True)
    for rate in (22050, 44100):
        reference(output / f"silencio_{rate}.wav", rate, False)
    binary = PROJECT / "sampler-c"
    if binary.is_file():
        report["render"] = run([str(binary), "--samples", str(PROJECT / "samples"),
                                "--state", str(PROJECT / "kits/tr808.json"),
                                "--render", str(output / "tr808_motor.wav"),
                                "--seconds", "8"], timeout=30, cwd=PROJECT)
    else:
        report["render"] = {"error": "Compila con make para exportar tambien el motor C"}
    if (output / "tr808_motor.wav").is_file():
        with wave.open(str(output / "tr808_motor.wav"), "rb") as wav:
            report["render_audio"] = {"sample_rate": wav.getframerate(), "channels": wav.getnchannels(),
                                      "bits": wav.getsampwidth()*8, "frames": wav.getnframes()}
    (output / "informe.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"Informe y pruebas WAV: {output}")
    print("No se ha reproducido audio ni cambiado la configuracion de la Raspberry.")
    print("Con el sampler cerrado, reproduce cada WAV con aplay y la MISMA salida ALSA.")
    print(f"  aplay '{output / 'tono_44100.wav'}'")
    print(f"  aplay '{output / 'silencio_44100.wav'}'")
    if (output / "tr808_motor.wav").is_file():
        print(f"  aplay '{output / 'tr808_motor.wav'}'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
