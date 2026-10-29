"""Drive actual sampler navigation through a USB serial stand-in (PTY)."""
import json
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import time

binary = os.environ.get("SAMPLER_BIN", str(Path(__file__).resolve().parents[1] / "sampler-c"))
with tempfile.TemporaryDirectory(prefix="sampler-arduino-") as tmp:
    root = Path(tmp)
    state = root / "state.json"
    state.write_text(json.dumps({"bpm": 120}))
    link = root / "arduino"
    master, slave = pty.openpty()
    link.symlink_to(os.ttyname(slave))
    process = subprocess.Popen(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(state), "--arduino", str(link), "--seconds", "20"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    os.close(slave)

    def wait_for(stream, needle, timeout=4):
        data = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if select.select([stream], [], [], 0.1)[0]:
                chunk = os.read(stream.fileno(), 8192)
                if not chunk:
                    break
                data += chunk
                if needle in data:
                    return data
        raise AssertionError(data.decode(errors="replace"))

    def send(value, duration=.15):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            os.write(master, f"POT {value}\n".encode())
            time.sleep(.02)

    try:
        wait_for(process.stderr, b"Arduino conectado:")
        process.stdin.write(b"mode bpm\n")
        process.stdin.flush()
        wait_for(process.stdout, b"120 BPM PLAY")
        send(1023)  # Far from 120: wait for pickup.
        wait_for(process.stdout, b"POT GIRA IZQ")
        send(512)   # Pick up 120 BPM.
        send(800)
        wait_for(process.stdout, b"154 BPM PLAY")
        send(800, .5)  # Holding the position must never repeat.
        # Buttons release pickup even while the knob keeps sending.
        process.stdin.write(b"r\n")
        process.stdin.flush()
        send(800)
        wait_for(process.stdout, b"POT GIRA DER")
        send(840)  # Pick up button-selected 159 BPM.
        send(0)
        wait_for(process.stdout, b"60 BPM PLAY")
        time.sleep(.4)
        send(1023)  # No jump after lost heartbeat.
        wait_for(process.stdout, b"POT GIRA IZQ")
        send(0)
        os.close(master)
        master = -1
        wait_for(process.stderr, b"reintento en 2 s")
        master, slave = pty.openpty()
        link.unlink()
        link.symlink_to(os.ttyname(slave))
        wait_for(process.stderr, b"Arduino conectado:", 5)
        os.close(slave)
        send(1023)
        wait_for(process.stdout, b"POT GIRA IZQ")
        send(0)
        send(800)
        wait_for(process.stdout, b"154 BPM PLAY")
        process.stdin.write(b"mode drum\n")  # DRUM: an independent volume pickup.
        process.stdin.flush()
        wait_for(process.stdout, b"P1 BOMBO")
        send(0)
        wait_for(process.stdout, b"POT GIRA DER")
        send(765)
        send(0)
        wait_for(process.stdout, b"VOL 0%")
        send(1023)
        wait_for(process.stdout, b"VOL 100%")
        send(0)
        wait_for(process.stdout, b"VOL 0%")
        output, errors = process.communicate(b"q\n", timeout=5)
        assert process.returncode == 0, errors.decode()
        saved = json.loads(state.read_text())
        assert saved["bpm"] == 154
        assert saved["master_volume"] == 0
    finally:
        if master >= 0:
            os.close(master)
        if process.poll() is None:
            process.kill()
            process.communicate()
print("integration arduino: posicion absoluta, recogida, timeout, reconexion y guardado OK")
