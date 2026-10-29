"""Monophonic PCM sound generation and per-trigger effects for pygame.mixer.
These are discrete-note effects, not sample-accurate master-bus DSP.
"""
import math
import numpy as np

RATE = 22050
DRUM_GAINS = (0.76, 0.51, 0.34, 0.31, 0.44, 0.53)
BASS_GAIN = 0.48


def normalize(x):
    x = np.asarray(x, dtype=np.float32)
    peak = float(np.max(np.abs(x))) if x.size else 0
    return (x * (0.86 / peak)).astype(np.float32) if peak > 0 else x


def _smooth_noise(rng, size, window):
    x = rng.standard_normal(size).astype(np.float32)
    kernel = np.ones(window, dtype=np.float32) / window
    return (x - np.convolve(x, kernel, 'same')).astype(np.float32)


def make_drums():
    rng = np.random.default_rng(2030)
    axis = lambda seconds: np.arange(round(seconds * RATE), dtype=np.float32) / RATE
    t = axis(.48)
    hz = 49 + 120 * np.exp(-32 * t)
    kick = np.sin(2 * np.pi * np.cumsum(hz) / RATE) * np.exp(-11 * t)
    kick += .14 * _smooth_noise(rng, len(t), 7) * np.exp(-120 * t)
    t = axis(.30)
    snare = (.72 * _smooth_noise(rng, len(t), 7) * np.exp(-22 * t)
             + .37 * np.sin(2 * np.pi * 175 * t) * np.exp(-28 * t))
    t = axis(.12)
    hh = _smooth_noise(rng, len(t), 5) * np.exp(-65 * t)
    t = axis(.46)
    ho = _smooth_noise(rng, len(t), 5) * np.exp(-9 * t)
    t = axis(.34)
    env = (np.exp(-110 * t) + .75 * np.exp(-130 * np.maximum(0, t-.019)) * (t>=.019)
           + .55 * np.exp(-110 * np.maximum(0,t-.039)) * (t>=.039)
           + .15 * np.exp(-10 * t))
    clap = _smooth_noise(rng, len(t), 7) * env
    t = axis(.44)
    hz = 105 + 145 * np.exp(-20 * t)
    tom = np.sin(2 * np.pi * np.cumsum(hz) / RATE) * np.exp(-12 * t)
    tom += .05 * _smooth_noise(rng, len(t), 7) * np.exp(-80 * t)
    return [normalize(x) for x in (kick, snare, hh, ho, clap, tom)]


def make_bass(pad, octave):
    """Six pitched bass keys C, D, E, G, A, upper C."""
    midi = (36, 38, 40, 43, 45, 48)[pad] + (octave - 2) * 12
    hz = 440. * 2 ** ((midi - 69) / 12.)
    t = np.arange(round(.68 * RATE), dtype=np.float32) / RATE
    # Soft analog-style sub bass: triangle-like square + sub, brief pitch scoop.
    freq = hz * (1. + .045 * np.exp(-30 * t))
    phase = np.cumsum(freq) / RATE
    tri = 2. * np.abs(2. * (phase % 1.) - 1.) - 1.
    sub = np.sin(2. * np.pi * phase)
    signal = (.49 * tri + .66 * sub + .095 * np.sin(4*np.pi*phase))
    attack = np.minimum(t / .009, 1.)
    env = attack * np.exp(-3.45 * t)
    signal = signal * env
    return normalize(signal)


def global_transform(x, effect):
    """Global preset effect on each newly triggered note (7 choices)."""
    if effect == 0:
        return x
    if effect == 1:
        return np.convolve(x, np.ones(43, np.float32)/43, 'same') * 1.48
    if effect == 2:
        avg = np.convolve(x, np.ones(33, np.float32)/33, 'same')
        return (x - avg) * 1.35
    if effect == 3:
        block = 7
        quant = np.round(x[::block] * 13) / 13
        return np.repeat(quant, block)[:len(x)]
    if effect == 4:
        return np.tanh(x * 5.) / np.tanh(5.)
    if effect == 5:
        # Three echoes, spaced in 16ths at approx 120 BPM.
        out = np.zeros(len(x) + round(.39 * RATE), dtype=np.float32)
        out[:len(x)] = x
        for sec, g in ((.13,.40),(.26,.25),(.39,.12)):
            offset = round(sec * RATE)
            out[offset:offset+len(x)] += x*g
        return out
    if effect == 6:
        n = min(len(x), round(.07*RATE))
        grain = x[:n].copy()
        grain[-min(60,n):] *= np.linspace(1, 0, min(60,n))
        return np.concatenate([grain*a for a in (1.,.8,.6,.45)])
    return x


def punch_transform(x, effect):
    """Last three FX pads: 3 reverse, 4 tapestop, 5 glitch."""
    if effect is None or effect < 3:
        return x
    if effect == 3:  # reverse hit, short fade-out instead of abrupt click
        y = x[::-1].copy()
        fade = min(len(y)//4, round(.025*RATE))
        if fade:
            y[-fade:] *= np.linspace(1, 0, fade)
        return y
    if effect == 4:  # continuous downward pitch/speed sweep across this hit
        length = min(round(1.0*RATE), max(len(x), round(.48*RATE)))
        i = np.arange(length, dtype=np.float32)
        speed = 1.30 * np.maximum(0, 1. - i/length)**2 + .04
        source_pos = np.cumsum(speed)
        y = np.interp(source_pos, np.arange(len(x)), x, left=0, right=0)
        y *= np.maximum(0, 1. - i/length)**.72
        return y.astype(np.float32)
    if effect == 5:  # glitch: shuffled deterministic 20 ms grains and gating
        size = max(32, round(.022*RATE))
        out = np.zeros(len(x), dtype=np.float32)
        total = (len(x)+size-1)//size
        for k in range(total):
            first = k*size
            end = min(first+size, len(x))
            source_k = (k * 5 + (k//3)*7) % max(1,total)
            chunk = x[source_k*size:source_k*size + (end-first)]
            if len(chunk) < end-first:
                chunk = np.pad(chunk, (0,end-first-len(chunk)))
            gate = (.0 if k % 7 == 0 else (.42 if k % 3 == 0 else .9))
            out[first:end] = chunk * gate
        return out
    return x


def render(source, global_effect=0, punch_effect=None):
    output = global_transform(source, global_effect)
    output = punch_transform(output, punch_effect)
    return np.clip(output, -.94, .94).astype(np.float32)


class AudioEngine:
    def __init__(self):
        try:
            import pygame
        except ImportError as exc:
            raise RuntimeError('Instala: sudo apt install python3-pygame') from exc
        self.pygame = pygame
        try:
            pygame.mixer.init(frequency=RATE, size=-16, channels=1, buffer=256)
            pygame.mixer.set_num_channels(32)
        except pygame.error as exc:
            raise RuntimeError('No se pudo abrir audio ALSA/pygame: '+str(exc)) from exc
        self.drums = make_drums()
        self.sources = {}
        self.cached = {}

    def play(self, kind, index, octave, global_effect, punch_effect, master_volume):
        if not 0 <= index < 6:
            return
        source_id = (kind, index, octave if kind == 'bass' else 0)
        if source_id not in self.sources:
            self.sources[source_id] = (self.drums[index] if kind == 'drum'
                                       else make_bass(index, octave))
        # Only cache transformed sound when first used, including combinations
        key = (source_id, global_effect, punch_effect if punch_effect in (3,4,5) else None)
        if key not in self.cached:
            y = render(self.sources[source_id], global_effect, key[2])
            pcm = np.ascontiguousarray((y*32767).astype(np.int16))
            self.cached[key] = self.pygame.sndarray.make_sound(pcm)
        channel = self.cached[key].play()
        if channel is not None:
            gain = DRUM_GAINS[index] if kind == 'drum' else BASS_GAIN
            channel.set_volume(float(master_volume) * gain)

    def close(self):
        self.pygame.mixer.quit()
