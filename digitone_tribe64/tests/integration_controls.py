"""End-to-end controller V2: three pots, OK and FN and safe release."""
import json
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import threading
import time

binary = os.environ.get("SAMPLER_BIN", str(Path(__file__).resolve().parents[1] / "sampler-c"))
with tempfile.TemporaryDirectory(prefix="sampler-controls-") as tmp:
    state = Path(tmp) / "state.json"
    state.write_text(json.dumps({"bpm": 120}))
    master, slave = pty.openpty()
    process = subprocess.Popen(
        [binary, "--terminal", "--headless", "--device", "null", "--samples", tmp,
         "--state", str(state), "--arduino", os.ttyname(slave), "--seconds", "40"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.close(slave)
    stop = threading.Event()
    lock = threading.Lock()
    values = [0, 665, 205, 0, 0]
    paused = False
    errors = []

    def writer():
        while not stop.wait(.02):
            with lock:
                frame = "CTRL " + " ".join(map(str, values)) + "\n"
                enabled = not paused
            if enabled:
                try:
                    os.write(master, frame.encode())
                except OSError as exc:
                    errors.append(exc)
                    return

    def change(**kwargs):
        names = ["nav", "cutoff", "resonance", "ok", "fn"]
        with lock:
            for key, value in kwargs.items():
                values[names.index(key)] = value
        time.sleep(.08)

    def command(text):
        process.stdin.write((text + "\n").encode())
        process.stdin.flush()

    def wait_for(stream, needle, timeout=4):
        data = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if select.select([stream], [], [], .1)[0]:
                chunk = os.read(stream.fileno(), 8192)
                if not chunk:
                    break
                data += chunk
                if needle in data:
                    return data
        raise AssertionError(f"Missing {needle!r}: {data.decode(errors='replace')}")

    thread = None
    try:
        wait_for(process.stderr, b"Arduino conectado:")
        thread = threading.Thread(target=writer)
        thread.start()
        change(ok=1)
        time.sleep(.7)
        wait_for(process.stdout, b"MENU: PATRONES")
        change(ok=0)
        for _ in range(4):
            command("r")
        command("o")
        wait_for(process.stdout, b"Modo: FX")
        change(ok=0)
        command("r")  # Browsing must not apply an effect before OK.
        wait_for(process.stdout, b"FX1* LOWPASS")
        command("o")
        wait_for(process.stdout, b"FX1  LOWPASS")
        # NAV is still waiting for pickup; it must not hide parameter values.
        change(cutoff=0, resonance=1023)
        wait_for(process.stdout, b"Q8.0")
        # A pending choice freezes its controls; OK applies it and re-arms pickup.
        command("r")
        wait_for(process.stdout, b"FX1* HIGHPASS")
        command("o")
        wait_for(process.stdout, b"FX1  HIGHPASS")
        change(cutoff=0, resonance=1023)
        change(cutoff=400, resonance=500)
        wait_for(process.stdout, b"Q4.2")
        command("o")  # Disable/enable keeps both parameters picked up.
        wait_for(process.stdout, b"FX1  OFF")
        command("o")
        wait_for(process.stdout, b"FX1  HIGHPASS")
        change(cutoff=403, resonance=503)
        command("l")
        wait_for(process.stdout, b"FX1* LOWPASS")
        command("o")
        wait_for(process.stdout, b"FX1  LOWPASS")
        change(cutoff=0, resonance=1023)
        wait_for(process.stdout, b"Q8.0")
        change(fn=1)
        time.sleep(.7)  # FN alone must never behave like long OK / change mode.
        command("+4")
        wait_for(process.stdout, b"REP 1/8")
        change(fn=0)  # Release modifier first, with the pad still held.
        wait_for(process.stdout, b"Q8.0")
        command("-4")
        # D2 alone is OK and toggles the effect. D3 alone never does.
        change(ok=1)
        change(ok=0)
        wait_for(process.stdout, b"FX1  OFF")
        change(ok=1)
        change(ok=0)
        wait_for(process.stdout, b"FX1  LOWPASS")
        # Losing the heartbeat while FN+pad is held must release without OK.
        change(fn=1)
        command("+5")
        wait_for(process.stdout, b"REP 1/16")
        with lock:
            paused = True
        wait_for(process.stdout, b"Q8.0")
        change(fn=0)
        with lock:
            paused = False
        time.sleep(.15)
        command("-5")
        # Losing data with OK down must cancel the pending short/long action.
        change(ok=1)
        with lock:
            paused = True
        time.sleep(.4)
        change(ok=0)
        with lock:
            paused = False
        time.sleep(.15)
        # D2 long changes mode once, and releasing it must not toggle PLAY.
        change(ok=1)
        time.sleep(.7)
        wait_for(process.stdout, b"MENU: EFECTOS")
        time.sleep(.2)
        change(ok=0)
        command("r")
        command("o")
        wait_for(process.stdout, b"Modo: BPM")
        change(nav=512)
        wait_for(process.stdout, b"120 BPM PLAY")
        change(nav=800)
        wait_for(process.stdout, b"154 BPM PLAY")
        # FX knobs are inactive outside the FX editor.
        change(cutoff=403, resonance=503)
        command("mode fx")
        wait_for(process.stdout, b"Modo: FX")
        change(cutoff=0, resonance=1023)
        change(cutoff=403, resonance=503)
        stop.set()
        thread.join()
        output, stderr = process.communicate(b"q\n", timeout=5)
        assert process.returncode == 0, stderr.decode()
        saved = json.loads(state.read_text())
        assert saved["global_choice"] == 1 and saved["global_enabled"] is True
        assert abs(saved["fx_cutoff"]-403/1023)<1e-6
        assert abs(saved["fx_resonance"]-503/1023)<1e-6
        assert saved["bpm"] == 154
        assert not errors, errors
    finally:
        stop.set()
        if thread:
            thread.join()
        os.close(master)
        if process.poll() is None:
            process.kill()
            process.communicate()
print("integration controls: tres mandos, OK corto/largo, FN, pads, timeout sin OK y guardado OK")
