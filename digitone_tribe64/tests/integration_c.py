"""Exercise the actual ALSA executable, file assignment and persistence without GPIO."""
import json
import os
import re
from pathlib import Path
import select
import struct
import subprocess
import tempfile
import time
import wave

binary = os.environ.get("SAMPLER_BIN", str(Path(__file__).resolve().parents[1] / "sampler-c"))
with tempfile.TemporaryDirectory(prefix="sampler-integration-") as tmp:
    root = Path(tmp)
    sample = root / "a sample.wav"
    with wave.open(str(sample), "wb") as wav:
        wav.setparams((1, 2, 22050, 0, "NONE", "not compressed"))
        wav.writeframes(struct.pack("<h", 12000) * 2205)
    (root / "zbroken.wav").write_bytes(b"invalid audio")
    state = root / "state.json"
    # Seed an old schema to also exercise compatibility with Python patterns.
    state.write_text(json.dumps({"version": 3, "bpm": 135, "bass_pattern": [-1] * 16}))
    process = subprocess.Popen(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(state), "--seconds", "8"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        process.stdin.write(b"mode files\n3\no\n")
        process.stdin.flush()
        output = b""
        deadline = time.monotonic() + 6
        while b"Cargado en PAD 3" not in output and time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.1)[0]:
                block = os.read(process.stdout.fileno(), 8192)
                if not block:
                    break
                output += block
        assert b"Cargado en PAD 3" in output, output.decode(errors="replace")
        process.stdin.write(b"r\no\n")
        process.stdin.flush()
        failed_output = b""
        deadline = time.monotonic() + 3
        while b"Error de carga" not in failed_output and time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.1)[0]:
                block = os.read(process.stdout.fileno(), 8192)
                if not block:
                    break
                failed_output += block
        assert b"Error de carga" in failed_output, failed_output.decode(errors="replace")
        rest, errors = process.communicate(b"q\n", timeout=5)
        assert process.returncode == 0, errors.decode()
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
    saved = json.loads(state.read_text())
    assert saved["version"] == 5 and saved["bpm"] == 135
    assert saved["samples"][2] == str(sample)
    result = subprocess.run(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(state), "--no-save", "--seconds", "0.15"],
        input=b"slots\n", capture_output=True, timeout=5,
    )
    assert result.returncode == 0, result.stderr.decode()
    assert f"PAD 3: {sample}".encode() in result.stdout
    pending = root / "pending.json"
    result = subprocess.run(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(pending), "--seconds", "2"],
        input=b"mode files\n2\no\nq\n", capture_output=True, timeout=5,
    )
    assert result.returncode == 0, result.stderr.decode()
    assert json.loads(pending.read_text())["samples"][1] == str(sample)
    assert not list(root.glob("*.tmp.*"))

# Browsing previews must work before assigning anything, including silent samples.
with tempfile.TemporaryDirectory(prefix="sampler-waveform-") as tmp:
    root = Path(tmp)
    for name, value in (("a preview.wav", 12000), ("b silence.wav", 0)):
        with wave.open(str(root / name), "wb") as wav:
            wav.setparams((1, 2, 22050, 0, "NONE", "not compressed"))
            wav.writeframes(struct.pack("<h", value) * 2205)
    fixture = Path(__file__).parent / "fixtures" / "tone.mp3"
    (root / "c tone.mp3").write_bytes(fixture.read_bytes())
    (root / "zbroken.wav").write_bytes(b"bad audio")
    state = root / "no-save.json"
    process = subprocess.Popen(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(state), "--no-save", "--seconds", "10"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )

    def send_and_wait(commands, expected):
        process.stdin.write(commands)
        process.stdin.flush()
        output = b""
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.1)[0]:
                block = os.read(process.stdout.fileno(), 8192)
                if not block:
                    break
                output += block
                if re.search(expected, output):
                    return output
        raise AssertionError(output.decode(errors="replace"))

    try:
        send_and_wait(b"mode files\n", rb"\[P1 a preview.wav\] \[8{16}\]")
        send_and_wait(b"r\n", rb"\[P1 b silence.wav\] \[\.{16}\]")
        # Change selection twice in one read; no obsolete result may overwrite MP3.
        send_and_wait(b"l\nr\nr\n", rb"\[P1 c tone.mp3 +\] \[[1-8]{16}\]")
        send_and_wait(b"r\n", rb"\[P1 zbroken.wav +\] \[SIN VISTA PREVIA\]")
        output, errors = process.communicate(b"slots\nq\n", timeout=5)
        assert process.returncode == 0, errors.decode()
        assert b"PAD 1: sintetizado" in output  # Browsing never assigns the preview.
        assert not state.exists()
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
print("integration: FILES, vistas WAV/MP3/silencio/error, guardado, reapertura y cierre OK")
