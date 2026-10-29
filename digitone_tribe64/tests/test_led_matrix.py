import unittest

from engine import Sequencer
from hardware import LED_ROWS, LED_COLUMNS, LCD_DATA, LCD_RS, LCD_E, BTN_LEFT, BTN_OK, BTN_RIGHT, PADS, LEDMatrix


class FakeGPIO:
    OUT, IN, HIGH, LOW = 'out', 'in', 1, 0

    def __init__(self):
        self.pins = {}
        self.writes = []

    def setup(self, pin, direction, initial=None, **kwargs):
        self.pins[pin] = initial

    def output(self, pin, value):
        self.pins[pin] = value
        self.writes.append((pin, value))

    def active_leds(self):
        active_rows = [index for index, pin in enumerate(LED_ROWS) if self.pins[pin] == self.HIGH]
        active_cols = [index for index, pin in enumerate(LED_COLUMNS) if self.pins[pin] == self.LOW]
        return {c * 4 + (3 - r) for r in active_rows for c in active_cols}


class LEDMatrixTests(unittest.TestCase):
    def setUp(self):
        self.gpio = FakeGPIO()
        self.leds = LEDMatrix(self.gpio)
        self.state = Sequencer()

    def test_mapping_matches_physical_board(self):
        self.assertEqual(LED_ROWS, (7, 4, 8, 9))
        self.assertEqual(LED_COLUMNS, (10, 11, 18, 15))
        self.assertEqual(LEDMatrix.coords(0), (3, 0))
        self.assertEqual(LEDMatrix.coords(1), (2, 0))
        self.assertEqual(LEDMatrix.coords(3), (0, 0))
        self.assertEqual(LEDMatrix.coords(4), (3, 1))
        self.assertEqual(LEDMatrix.coords(15), (0, 3))
        occupied = {BTN_LEFT, BTN_OK, BTN_RIGHT, *PADS, LCD_RS, LCD_E, *LCD_DATA}
        self.assertFalse(occupied.intersection(LED_ROWS + LED_COLUMNS))

    def test_all_16_playhead_positions(self):
        self.state.mode = 2   # BPM: sin patron de fondo ni cursor
        self.state.playing = True
        for step in range(16):
            self.state.playhead = step
            seen = set()
            for i in range(4):
                self.leds.refresh(self.state, now=1.0 + i * .002)
                seen |= self.gpio.active_leds()
            self.assertEqual(seen, {step}, f'paso {step+1}')

    def test_cursor_moves_in_edit_mode(self):
        self.state.playing = False
        self.state.mode = 0
        self.state.drum_patterns = [[False] * 16 for _ in range(6)]
        self.state.cursor = 5
        lit = set()
        for _ in range(4):
            self.leds.refresh(self.state, now=.1)
            lit |= self.gpio.active_leds()
        self.assertEqual(lit, {5})
        dark = set()
        for _ in range(4):
            self.leds.refresh(self.state, now=.4)
            dark |= self.gpio.active_leds()
        self.assertEqual(dark, set())

    def test_editing_writes_same_step_shown_by_led(self):
        self.state.playing = False
        self.state.drum_patterns = [[False] * 16 for _ in range(6)]
        self.state.cursor = 10
        self.state.nav('ok', 'short')
        self.assertTrue(self.state.drum_patterns[0][10])
        self.leds.dim_phase = 0
        self.assertTrue(self.leds.is_lit(self.state, 10, now=.4))
        self.state.nav('ok', 'short')
        self.assertFalse(self.state.drum_patterns[0][10])
        self.assertFalse(self.leds.is_lit(self.state, 10, now=.4))

    def test_bass_pattern_indication(self):
        self.state.mode = 3
        self.state.playing = False
        self.state.cursor = 0
        self.state.bass_pattern[14] = 2
        self.leds.dim_phase = 0
        self.assertTrue(self.leds.is_lit(self.state, 14, now=.4))
        self.assertFalse(self.leds.is_lit(self.state, 13, now=.4))

    def test_playhead_and_cursor_simultaneously(self):
        self.state.mode = 0
        self.state.drum_patterns = [[False] * 16 for _ in range(6)]
        self.state.cursor = 6
        self.state.playhead = 14
        self.state.playing = True
        seen = set()
        for _ in range(4):
            self.leds.refresh(self.state, now=.1)
            seen |= self.gpio.active_leds()
        self.assertEqual(seen, {6, 14})

    def test_leds_off_after_cleanup(self):
        self.state.playhead = 0
        self.leds.refresh(self.state, now=.1)
        self.leds.off()
        self.assertTrue(all(self.gpio.pins[p] == self.gpio.LOW for p in LED_ROWS))
        self.assertTrue(all(self.gpio.pins[p] == self.gpio.HIGH for p in LED_COLUMNS))


if __name__ == '__main__':
    unittest.main()
