#!/usr/bin/env python3
"""Sampler 3x3 v3: 6 drum tracks, bass melody, global + momentary effects.

Hardware: Raspberry Pi 3B, LCD1602 parallel, LED 4x4, GPIO numbering BCM.
Mode loop: SEQ -> FX -> BPM -> BASS -> DRUM -> SEQ (OK hold 0.65s).
See README.md for full mappings.
"""
import json
import math
import os
import sys
import time
from pathlib import Path

from engine import Sequencer, PUNCH_REPEATS
from hardware import BTN_LEFT, BTN_OK, BTN_RIGHT, PADS, Button, LCD1602, LEDMatrix

STORE = Path.home() / '.sampler3x3' / 'patrones_v3.json'
OLD_STORE = Path.home() / '.sampler3x3' / 'patrones.json'
PIN_KEYS = {BTN_LEFT: 'left', BTN_OK: 'ok', BTN_RIGHT: 'right'}


class Sampler:
    def __init__(self):
        try:
            import RPi.GPIO as GPIO
        except ImportError as exc:
            raise RuntimeError('Instala python3-rpi.gpio con apt') from exc
        from audio import AudioEngine
        self.gpio = GPIO
        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)
        self.buttons = {pin: Button(GPIO, pin) for pin in PIN_KEYS}
        self.buttons.update({pin: Button(GPIO, pin) for pin in PADS})
        self.lcd = LCD1602(GPIO)
        self.leds = LEDMatrix(GPIO)
        self.state = Sequencer()
        loaded = self.state.load(STORE, OLD_STORE)
        print('Patrones:', loaded if loaded else 'nuevos')
        self.audio = AudioEngine()
        now = time.monotonic()
        self.next_step_at = now + .2
        self.clock_origin = self.next_step_at
        self.last_chord = ()
        self.repeat_chord = ()
        self.repeat_next_at = None
        self.saved_at = None
        self.last_screen_at = 0
        self.lcd.show(*self.state.screen())

    def save(self):
        tmp = STORE.with_suffix('.tmp')
        try:
            STORE.parent.mkdir(parents=True, exist_ok=True)
            tmp.write_text(json.dumps(self.state.serialize(), indent=2) + '\n', encoding='utf-8')
            os.replace(tmp, STORE)
        except OSError as exc:
            print('Error al guardar:', exc, file=sys.stderr)
        self.saved_at = None

    def note(self, voice, punch=None):
        kind, pad = voice
        s = self.state
        self.audio.play(kind, pad, s.bass_octave, s.global_effect, punch,
                        s.master_volume)

    def play_voices(self, voices, punch=None, remember=True):
        for voice in voices:
            self.note(voice, punch=punch)
        if voices and remember:
            self.last_chord = tuple(voices)

    def fx_changed(self, now, old):
        """Re-sync repeat pulse on effect transitions, with instant audio feedback."""
        new = self.state.punch_effect
        if old == new:
            return
        self.repeat_next_at = None
        self.repeat_chord = ()
        if new is None:
            return
        if new in (0, 1, 2):
            self.repeat_chord = self.last_chord
            if self.repeat_chord:
                self.play_voices(self.repeat_chord, remember=False)
                interval = 60. / self.state.bpm / PUNCH_REPEATS[new]
                beat_grid = math.floor((now - self.clock_origin) / interval) + 1
                self.repeat_next_at = max(now + .02,
                    self.clock_origin + beat_grid * interval)
        elif self.last_chord:
            self.play_voices(self.last_chord, punch=new, remember=False)

    def on_button(self, pin, event, now):
        s = self.state
        if pin in PADS:
            index = PADS.index(pin)
            if event == 'press':
                old = s.punch_effect
                voices = s.pad_press(index)
                if s.mode == 1:
                    self.fx_changed(now, old)
                else:
                    self.play_voices(voices, punch=None)
            elif event == 'release':
                old = s.punch_effect
                s.pad_release(index)
                self.fx_changed(now, old)
            return

        if event not in ('short', 'long'):
            return
        was_mode = s.mode
        was_playing = s.playing
        was_bpm = s.bpm
        old_fx = s.punch_effect
        result = s.nav(PIN_KEYS[pin], event)
        if result == 'save':
            self.saved_at = now
        if result == 'mode':
            self.fx_changed(now, old_fx)
            print('Modo:', s.mode_name)
        if was_playing != s.playing:
            self.repeat_next_at = None
            if s.playing:
                self.next_step_at = now + .04
                self.clock_origin = self.next_step_at
                print('PLAY')
            else:
                print('STOP')
        elif s.bpm != was_bpm:
            self.next_step_at = now + 60. / (s.bpm * 4)
            self.clock_origin = self.next_step_at
            if s.punch_effect in (0, 1, 2):
                self.fx_changed(now, None)

    def tick(self, now):
        s = self.state
        if not s.playing:
            return
        period = 60. / s.bpm / 4
        repeat = s.punch_effect in (0, 1, 2)
        processed = 0
        while now >= self.next_step_at and processed < 4:
            s.playhead = (s.playhead + 1) % 16
            chord = s.step_voices(s.playhead)
            if chord and not repeat:
                self.play_voices(chord, punch=s.punch_effect)
            self.next_step_at += period
            processed += 1
        if processed >= 4 and self.next_step_at <= now:
            self.next_step_at = now + period
            self.clock_origin = self.next_step_at - (s.playhead+1)*period
        if repeat and self.repeat_next_at is not None and self.repeat_chord:
            interval = 60. / s.bpm / PUNCH_REPEATS[s.punch_effect]
            processed = 0
            while now >= self.repeat_next_at and processed < 4:
                self.play_voices(self.repeat_chord, remember=False)
                self.repeat_next_at += interval
                processed += 1
            if processed >= 4 and now >= self.repeat_next_at:
                self.repeat_next_at = now + interval

    def run(self):
        print('SAMPLER 3X3 V3 + LED 4x4 | Ctrl+C para salir')
        print('LED: pasos grabados tenues; reproduccion brillante; cursor parpadeante')
        print('OK largo: SEQ -> FX -> BPM -> BASS -> DRUM')
        print('SEQ: 6 pads = baterias; izq/der = paso; OK = on/off')
        print('FX: 6 pads = 1/8,1/16,1/32,reverse,tapestop,glitch')
        print('FX: izq/der = elegir efecto GLOBAL; OK = activar/bypass')
        print('BPM: izq/der = +/-5; OK = stop/play')
        print('BASS: 6 pads = notas; izq/der = paso; OK = escribir/borrar nota')
        print('BASS: izq/der LARGO = octava -/+')
        print('DRUM: 6 pads = bateria en directo; izq/der = volumen; OK = backing')
        try:
            while True:
                now = time.monotonic()
                for pin, button in self.buttons.items():
                    for event in button.poll(now):
                        self.on_button(pin, event, now)
                self.tick(now)
                self.leds.refresh(self.state, now)
                if now - self.last_screen_at > .05:
                    self.lcd.show(*self.state.screen())
                    self.last_screen_at = now
                if self.saved_at is not None and now-self.saved_at > 1.5:
                    self.save()
                time.sleep(.002)
        except KeyboardInterrupt:
            print('\nSaliendo...')

    def close(self):
        if hasattr(self, 'leds'):
            self.leds.off()
        if self.saved_at is not None:
            self.save()
        try:
            self.lcd.show('SAMPLER DETENIDO', 'HASTA LUEGO')
        except Exception:
            pass
        if hasattr(self, 'audio'):
            self.audio.close()
        self.gpio.cleanup()


def main():
    sampler = None
    try:
        sampler = Sampler()
        sampler.run()
    except Exception as exc:
        print('Fallo al iniciar/ejecutar:', exc, file=sys.stderr)
        raise SystemExit(1) from exc
    finally:
        if sampler is not None:
            sampler.close()
        else:
            # If LCD or mixer setup failed halfway through, release GPIOs.
            try:
                import RPi.GPIO as GPIO
                GPIO.cleanup()
            except Exception:
                pass


if __name__ == '__main__':
    main()
