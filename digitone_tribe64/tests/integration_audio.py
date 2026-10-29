"""Verify offline PCM and actual ALSA writes using the file/null plugin, no board."""
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import wave

project = Path(__file__).resolve().parents[1]
binary = os.environ.get("SAMPLER_BIN", str(project / "sampler-c"))
with tempfile.TemporaryDirectory(prefix="sampler-audio-test-") as tmp:
    root = Path(tmp)
    rendered = root / "motor.wav"
    raw = root / "alsa.raw"
    config = root / "alsa.conf"
    config.write_text('pcm.test_null { type null }\n'
                      'pcm.test_capture { type file slave.pcm "test_null" '
                      f'file "{raw}" format "raw" }}\n')
    common = [binary, "--terminal", "--samples", str(project / "samples"), "--state", str(project / "kits/tr808.json")]
    result = subprocess.run(common + ["--render", str(rendered), "--seconds", "0.75"],
                            capture_output=True, timeout=10, cwd=project)
    assert result.returncode == 0, result.stderr.decode()
    assert b"frames recortados 0/" in result.stderr
    with wave.open(str(rendered), "rb") as wav:
        rate = wav.getframerate()
        assert rate == 44100 and wav.getnchannels() == 2 and wav.getsampwidth() == 2
        assert wav.getnframes() == int(.75 * rate)
        expected = wav.readframes(wav.getnframes())
    assert any(expected) and expected[:int(.2*rate)*4] == bytes(int(.2*rate)*4)
    env = dict(os.environ, ALSA_CONFIG_PATH=str(config))
    result = subprocess.run(common + ["--headless", "--device", "test_capture", "--no-save",
                                      "--latency-ms", "100", "--audio-info", "--seconds", "0.75"],
                            input=b"", capture_output=True, timeout=10, cwd=project, env=env)
    assert result.returncode == 0, result.stderr.decode()
    captured = raw.read_bytes()
    assert len(captured) >= len(expected), (len(captured), len(expected))
    assert captured[:len(expected)] == expected  # Transport doesn't add any PCM noise.
    assert b"xruns): 0" in result.stderr and b"frames recortados 0/" in result.stderr
    assert b"solicitud 100 ms" in result.stdout
    assert re.search(rb"pico antes del recorte 0\.[0-9]+", result.stderr)
    injected_env = dict(env)
    preload = [str(project / "build/test-alsa.so")]
    # Keep the sanitizer runtime first when the executable is instrumented.
    linked = subprocess.check_output(["ldd", binary], text=True)
    for line in linked.splitlines():
        if "libasan.so" in line and "=>" in line:
            preload.insert(0, line.split("=>", 1)[1].split()[0])
    injected_env["LD_PRELOAD"] = ":".join(preload)
    result = subprocess.run(common + ["--headless", "--device", "test_capture", "--no-save",
                                      "--seconds", "0.75"], input=b"", capture_output=True,
                            timeout=10, cwd=project, env=injected_env)
    assert result.returncode == 0, result.stderr.decode()
    assert b"xruns): 1" in result.stderr, result.stderr.decode()
    position = int(re.search(rb"TEST_XRUN_AT=(\d+)", result.stderr)[1])
    recovered = raw.read_bytes()
    assert recovered[:position*4] == expected[:position*4]
    fade = rate // 200
    for i in range(fade):
        original = struct.unpack_from("<hh", expected, (position+i)*4)
        actual = struct.unpack_from("<hh", recovered, (position+i)*4)
        gain = i / fade
        assert all(abs(actual[c]-int(original[c]*gain)) <= 1 for c in (0, 1))
    assert recovered[(position+fade)*4:len(expected)] == expected[(position+fade)*4:]
    for invalid in ("0", "9", "501", "no", "100ms"):
        bad = subprocess.run([binary, "--terminal", "--latency-ms", invalid], capture_output=True, timeout=3)
        assert bad.returncode != 0
    # The collection script is harmless on a PC too and creates real reference files.
    output = root / "report"
    result = subprocess.run(["python3", str(project / "scripts/diagnostico_audio.py"),
                             "--output", str(output)], capture_output=True, timeout=45)
    assert result.returncode == 0, result.stderr.decode()
    report = json.loads((output / "informe.json").read_text())
    assert report["render"]["returncode"] == 0, report["render"]
    with wave.open(str(output / "silencio_22050.wav"), "rb") as wav:
        assert not any(wav.readframes(wav.getnframes()))
    for rate in (22050, 44100, 48000):
        with wave.open(str(output / f"tono_{rate}.wav"), "rb") as wav:
            assert wav.getframerate() == rate and wav.getnframes() == rate * 4
print("audio: WAV/ALSA identicos, escrituras parciales, EAGAIN/EINTR y recuperacion de xrun con fundido OK")
