#!/usr/bin/env python3
"""Preset original: nueve voces sintetizadas y bajo; cuatro compases de 16 pasos."""
import json
from pathlib import Path


def scene(index):
    patterns = [[False] * 64 for _ in range(9)]
    bass = [-1] * 64
    for step in range(64):
        bar, pos = divmod(step, 16)
        patterns[0][step] = pos % 4 == 0
        patterns[1][step] = pos in (4, 12) or (bar == 3 and pos in (14, 15))
        patterns[2][step] = pos % 2 == 0
        patterns[3][step] = pos in (2, 6, 10, 14)
        patterns[4][step] = pos == 12 and bar % 2 == 1
        patterns[5][step] = pos in ((3, 11, 15) if bar == 3 else (7, 15))
        patterns[6][step] = pos in (3, 6, 11, 14) if bar % 2 == 0 else pos in (1, 7, 10, 15)
        patterns[7][step] = (pos + bar * 3) % 7 == 0
        patterns[8][step] = pos in (1, 7, 10) and bar in (1, 3)
        if pos in (2, 5, 8, 11, 14):
            bass[step] = (0, 0, 3, 1)[bar]
    muted = [False] * 10
    if index == 1:  # Breakdown: el bombo se recupera desde MUTE.
        muted[0] = True
        muted[1] = True
    if index == 2:
        for step in range(48, 64):
            patterns[5][step] = step % 2 == 1
            patterns[7][step] = step % 3 == 0
    if index >= 3:
        patterns = [[False] * 64 for _ in range(9)]
        bass = [-1] * 64
    return dict(name=("TRIBE MAIN", "BREAK MUTES", "TRIBE FILL")[index] if index < 3 else f"VACIO {index + 1}",
                patterns=patterns, bass_pattern=bass, muted=muted)


def main():
    scenes = [scene(i) for i in range(9)]
    preset = dict(version=5, tribe64_version=1, bpm=145, bass_octave=2,
                  master_volume=.68, global_choice=0, global_enabled=False,
                  fx2_choice=0, fx2_enabled=False, samples=[""] * 9,
                  edit_scene=1, scenes=scenes, patterns=scenes[0]["patterns"],
                  bass_pattern=scenes[0]["bass_pattern"])
    path = Path(__file__).resolve().parents[1] / "kits/tribe64.json"
    path.write_text(json.dumps(preset, indent=2) + "\n")
    print(path)


if __name__ == "__main__":
    main()
