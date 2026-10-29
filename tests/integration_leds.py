"""Standalone LED diagnostic opens only eight GPIOs and always blanks on exit."""
import os
from pathlib import Path
import subprocess

project = Path(__file__).resolve().parents[1]
binary = os.environ.get("SAMPLER_BIN", str(project / "sampler-c"))
preload = [str(project / "build/test-gpio.so")]
for line in subprocess.check_output(["ldd", binary], text=True).splitlines():
    if "libasan.so" in line and "=>" in line:
        preload.insert(0, line.split("=>", 1)[1].split()[0])
env = dict(os.environ, LD_PRELOAD=":".join(preload), ALSA_CONFIG_PATH="/does/not/exist")
command = [binary, "--test-leds", "--gpiochip", "/dev/null", "--seconds", "0.05",
           "--samples", "/does/not/exist", "--state", "/does/not/exist/state.json",
           "--arduino", "/does/not/exist", "--device", "does_not_exist"]
result = subprocess.run(command, env=env, capture_output=True, timeout=5)
assert result.returncode == 0, result.stderr.decode()
assert b"LED 01: fila 1, columna 1" in result.stdout
assert result.stderr.count(b"TEST_GPIO_MATRIX_ONLY") == 1
assert result.stderr.rstrip().endswith(b"TEST_GPIO_OFF")
failed = subprocess.run(command, env=dict(env, SAMPLER_TEST_GPIO_FAIL="1"),
                        capture_output=True, timeout=5)
assert failed.returncode != 0
assert failed.stderr.rstrip().endswith(b"TEST_GPIO_OFF")
for conflict in ("--headless", "--no-leds", "--render"):
    flags = [conflict] + (["/tmp/unused-led-test.wav"] if conflict == "--render" else [])
    bad = subprocess.run([binary, "--test-leds"] + flags, capture_output=True, timeout=3)
    assert bad.returncode != 0 and b"requiere GPIO" in bad.stderr
print("LED: diagnostico independiente, solo ocho GPIO, conflictos y apagado tras fallo OK")
