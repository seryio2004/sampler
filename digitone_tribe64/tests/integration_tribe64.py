"""Exercise legacy scene migration, step 64 and persistent mutes in the real app."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
binary = os.environ.get("SAMPLER_BIN", str(project / "sampler-c"))


def run(state, commands):
    result = subprocess.run([binary, "--terminal", "--headless", "--device", "null", "--samples",
                             str(project / "samples"), "--state", str(state)],
                            input=commands.encode(), capture_output=True, timeout=5)
    assert result.returncode == 0, result.stderr.decode()
    return json.loads(state.read_text())


with tempfile.TemporaryDirectory(prefix="tribe64-migration-") as tmp:
    state = Path(tmp) / "legacy.json"
    scenes = [dict(name=f"OLD {i+1}", patterns=[[False]*16 for _ in range(9)],
                   bass_pattern=[-1]*16) for i in range(9)]
    scenes[8]["patterns"][3][15] = True
    scenes[8]["bass_pattern"][7] = 4
    state.write_text(json.dumps(dict(version=5, scenes=scenes, edit_scene=9)))
    saved = run(state, "page 4\nl\no\nmute 9\nmute 10\nsound 3 -12 0.4 0.25\nq\n")
    ninth = saved["scenes"][8]
    assert ninth["name"] == "OLD 9" and ninth["patterns"][3][15]
    assert len(ninth["patterns"][0]) == 64 and ninth["patterns"][0][63]
    assert ninth["bass_pattern"][7] == 4 and ninth["bass_pattern"][16:] == [-1]*48
    assert ninth["muted"][8:] == [True, True]
    assert saved["pad_sound"][2] == dict(pitch=-12.0, cutoff=saved["pad_sound"][2]["cutoff"], length=.25)
    assert abs(saved["pad_sound"][2]["cutoff"]-.4)<1e-6
    assert not saved["scenes"][0]["muted"][8]
    saved = run(state, "mute 9\nq\n")
    assert saved["scenes"][8]["muted"][8:] == [False, True]
    assert saved["scenes"][8]["patterns"][0][63]
    assert saved["pad_sound"][2]["pitch"] == -12
    state.write_text(json.dumps(dict(patterns=[False]*9)))
    saved = run(state, "scene 1\nq\n")
    assert len(saved["scenes"][0]["patterns"][0]) == 64
print("tribe64 integration: legacy scenes, step 64, per-pattern mutes, sound parameters, reopen and malformed rows OK")
