"""Pure control/sequencer state machine for the Sampler 3x3 v3.
No GPIO or sound hardware is imported here; this module is unit-testable.
"""

import json
from pathlib import Path

MODES = ("SEQ", "FX", "BPM", "BASS", "DRUM")
DRUMS = ("BOMBO", "CAJA", "HAT CER", "HAT ABI", "PALMAS", "TOM")
BASS_NOTES = ("C", "D", "E", "G", "A", "C+")
GLOBAL_FX = ("OFF", "LOWPASS", "HIGHPASS", "BITCRUSH", "DIST", "ECHO", "STUTTER")
PUNCH_FX = ("REP 1/8", "REP 1/16", "REP 1/32", "REVERSE", "TAPE STOP", "GLITCH")
PUNCH_REPEATS = (2, 4, 8)  # number of triggers per quarter-note
BPM_MIN, BPM_MAX = 50, 220


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


def default_drums():
    grid = [[False] * 16 for _ in range(6)]
    grid[0] = [i in (0, 4, 8, 12) for i in range(16)]
    grid[1] = [i in (4, 12) for i in range(16)]
    grid[2] = [i % 2 == 0 for i in range(16)]
    return grid


class Sequencer:
    def __init__(self):
        self.mode = 0
        self.bpm = 120
        self.playing = True
        self.drum_patterns = default_drums()
        self.bass_pattern = [-1] * 16  # monophonic: -1=rest, 0..5=note
        self.selected_drum = 0
        self.selected_bass = 0
        self.cursor = 0
        self.playhead = -1
        self.bass_octave = 2  # C2,D2,E2,G2,A2,C3; range 1..4
        self.global_choice = 0
        self.global_enabled = True
        self.punch_held = []
        self.last_punch = None
        self.live_backing = False
        self.master_volume = 0.75

    @property
    def mode_name(self):
        return MODES[self.mode]

    @property
    def global_effect(self):
        return self.global_choice if self.global_enabled else 0

    @property
    def punch_effect(self):
        return self.punch_held[-1] if self.mode == 1 and self.punch_held else None

    def step_voices(self, step):
        """Voices to play at this step. Live-drums mode can silence backing."""
        if not 0 <= step < 16:
            raise ValueError('Invalid step')
        if self.mode == 4 and not self.live_backing:
            return ()
        voices = [("drum", i) for i, row in enumerate(self.drum_patterns) if row[step]]
        note = self.bass_pattern[step]
        if note >= 0:
            voices.append(("bass", note))
        return tuple(voices)

    def pad_press(self, pad):
        assert 0 <= pad < 6
        if self.mode == 1:
            if pad in self.punch_held:
                self.punch_held.remove(pad)
            self.punch_held.append(pad)
            self.last_punch = pad
            return ()
        if self.mode == 3:
            self.selected_bass = pad
            return (("bass", pad),)
        self.selected_drum = pad
        return (("drum", pad),)

    def pad_release(self, pad):
        if pad in self.punch_held:
            self.punch_held.remove(pad)

    def nav(self, key, kind):
        """Dispatch top-row left/ok/right, short/long. Returns status string."""
        assert key in ("left", "ok", "right")
        assert kind in ("short", "long")
        if kind == 'long' and key == 'ok':
            self.punch_held.clear()
            self.mode = (self.mode + 1) % len(MODES)
            return 'mode'
        if kind == 'long':
            if self.mode == 0:
                self.selected_drum = (self.selected_drum + (-1 if key == 'left' else 1)) % 6
            elif self.mode == 3:
                self.bass_octave = clamp(self.bass_octave + (-1 if key == 'left' else 1), 1, 4)
            return 'edit'
        if self.mode in (0, 3):
            if key != 'ok':
                self.cursor = (self.cursor + (-1 if key == 'left' else 1)) % 16
                return 'cursor'
            if self.mode == 0:
                row = self.drum_patterns[self.selected_drum]
                row[self.cursor] = not row[self.cursor]
            else:
                self.bass_pattern[self.cursor] = (-1 if self.bass_pattern[self.cursor] == self.selected_bass
                                                  else self.selected_bass)
            return 'save'
        if self.mode == 1:
            if key == 'left':
                self.global_choice = (self.global_choice - 1) % len(GLOBAL_FX)
                self.global_enabled = True
            elif key == 'right':
                self.global_choice = (self.global_choice + 1) % len(GLOBAL_FX)
                self.global_enabled = True
            else:
                self.global_enabled = not self.global_enabled
            return 'save'
        if self.mode == 2:
            if key == 'left':
                self.bpm = clamp(self.bpm - 5, BPM_MIN, BPM_MAX)
                return 'save'
            if key == 'right':
                self.bpm = clamp(self.bpm + 5, BPM_MIN, BPM_MAX)
                return 'save'
            self.playing = not self.playing
            return 'transport'
        if self.mode == 4:
            if key == 'left':
                self.master_volume = round(clamp(self.master_volume - 0.05, 0.1, 1.0), 2)
            elif key == 'right':
                self.master_volume = round(clamp(self.master_volume + 0.05, 0.1, 1.0), 2)
            else:
                self.live_backing = not self.live_backing
            return 'save'
        return 'none'

    def bass_label(self, note):
        if note < 0:
            return '--'
        return f'{BASS_NOTES[note]}{self.bass_octave + (1 if note == 5 else 0)}'

    def screen(self):
        """Exactly two LCD lines, 16 characters each, plain HD44780 ASCII."""
        if self.mode in (0, 3):
            if self.mode == 0:
                title = f'D{self.selected_drum + 1}:{DRUMS[self.selected_drum]}'
                pattern = self.drum_patterns[self.selected_drum]
                cells = ['X' if v else '.' for v in pattern]
                active = pattern[self.cursor]
            else:
                current = self.bass_pattern[self.cursor]
                title = f'BASS {self.bass_label(self.selected_bass)}'
                cells = [str(v + 1) if v >= 0 else '.' for v in self.bass_pattern]
                active = current >= 0
            top = f'{title:<11}{self.bpm:>3}{">" if self.playing else "|"}'
            if self.playhead >= 0 and self.playhead != self.cursor:
                cells[self.playhead] = 'O' if self.mode == 0 and pattern[self.playhead] else ':'
            cells[self.cursor] = '*' if active else '^'
            bottom = ''.join(cells)
        elif self.mode == 1:
            global_label = GLOBAL_FX[self.global_effect]
            top = f'FX {global_label:<10}'
            punch = self.punch_effect
            bottom = f'PAD {PUNCH_FX[punch] if punch is not None else "SIN EFECTO"}'
        elif self.mode == 2:
            top = 'BPM  IZQ / DER'
            bottom = f'{self.bpm:3d} BPM   {"PLAY" if self.playing else "STOP"} OK'
        else:
            top = 'DRUM KIT DIRECTO'
            bottom = f'VOL {int(self.master_volume * 100):3d}% BG {"ON" if self.live_backing else "OFF"}'
        return top[:16].ljust(16), bottom[:16].ljust(16)

    def serialize(self):
        return {
            'version': 3, 'bpm': self.bpm, 'patterns': self.drum_patterns,
            'bass_pattern': self.bass_pattern, 'bass_octave': self.bass_octave,
            'global_choice': self.global_choice, 'global_enabled': self.global_enabled,
            'master_volume': self.master_volume, 'live_backing': self.live_backing,
        }

    def restore(self, source):
        """Read v1/v2/v3 safely: do not let corrupted JSON change the controls."""
        if not isinstance(source, dict):
            return
        rows = source.get('patterns')
        if (isinstance(rows, list) and len(rows) == 6 and
                all(isinstance(row, list) and len(row) == 16 for row in rows)):
            self.drum_patterns = [[bool(cell) for cell in row] for row in rows]
        bass = source.get('bass_pattern')
        if (isinstance(bass, list) and len(bass) == 16 and
                all(type(v) is int and -1 <= v <= 5 for v in bass)):
            self.bass_pattern = bass[:]
        def number(name, fallback, lo, hi):
            try:
                return clamp(int(source.get(name, fallback)), lo, hi)
            except (ValueError, TypeError, OverflowError):
                return fallback
        self.bpm = number('bpm', self.bpm, BPM_MIN, BPM_MAX)
        self.bass_octave = number('bass_octave', self.bass_octave, 1, 4)
        self.global_choice = number('global_choice', self.global_choice, 0, len(GLOBAL_FX) - 1)
        self.global_enabled = (source.get('global_enabled', True) is not False)
        self.live_backing = (source.get('live_backing', False) is True)
        try:
            volume = float(source.get('master_volume', self.master_volume))
            if not (0.1 <= volume <= 1.0):
                volume = self.master_volume
            self.master_volume = volume
        except (ValueError, TypeError, OverflowError):
            pass

    def load(self, path, legacy=None):
        for name in (path, legacy):
            if name is None or not Path(name).is_file():
                continue
            try:
                with open(name, encoding='utf-8') as f:
                    data = json.load(f)
                self.restore(data)
                return str(name)
            except (OSError, ValueError, TypeError):
                continue
        return None
