"""BCM GPIO buttons, HD44780 LCD1602, and 4x4 LED matrix."""
import time

BTN_LEFT, BTN_OK, BTN_RIGHT = 5, 6, 12
PADS = (13, 16, 19, 20, 21, 26)
LCD_RS, LCD_E = 17, 27
LCD_DATA = (22, 23, 24, 25)
DEBOUNCE = .025
HOLD_TIME = .65

# LED PCB: fila 1->4 de arriba hacia abajo; cada columna va de LED inferior a superior.
LED_ROWS = (7, 4, 8, 9)       # BCM; filas 1, 2, 3, 4 (anodos, pata larga)
LED_COLUMNS = (10, 11, 18, 15)  # BCM; columnas 1, 2, 3, 4 (catodos, pata corta)



class Button:
    def __init__(self, gpio, pin):
        self.gpio = gpio
        self.pin = pin
        gpio.setup(pin, gpio.IN, pull_up_down=gpio.PUD_UP)
        self.raw = gpio.input(pin) == gpio.LOW
        self.stable = self.raw
        now = time.monotonic()
        self.changed_at = now
        self.down_at = now
        self.long_sent = False

    def poll(self, now):
        events = []
        raw = self.gpio.input(self.pin) == self.gpio.LOW
        if raw != self.raw:
            self.raw = raw
            self.changed_at = now
        if self.raw != self.stable and now - self.changed_at >= DEBOUNCE:
            self.stable = self.raw
            if self.stable:
                self.down_at = now
                self.long_sent = False
                events.append('press')
            else:
                events.append('release')
                if not self.long_sent:
                    events.append('short')
        if self.stable and not self.long_sent and now - self.down_at >= HOLD_TIME:
            self.long_sent = True
            events.append('long')
        return events


class LCD1602:
    def __init__(self, gpio):
        self.gpio = gpio
        self.previous = (None, None)
        for pin in (LCD_RS, LCD_E) + LCD_DATA:
            gpio.setup(pin, gpio.OUT, initial=gpio.LOW)
        time.sleep(.05)
        for command in (0x33, 0x32, 0x28, 0x0C, 0x06, 0x01):
            self.send(command, True)
        time.sleep(.004)

    def _nibble(self, n):
        for index, pin in enumerate(LCD_DATA):
            self.gpio.output(pin, bool(n & (1 << index)))
        self.gpio.output(LCD_E, self.gpio.HIGH)
        time.sleep(.00005)
        self.gpio.output(LCD_E, self.gpio.LOW)
        time.sleep(.00007)

    def send(self, value, command=False):
        self.gpio.output(LCD_RS, self.gpio.LOW if command else self.gpio.HIGH)
        self._nibble((value >> 4) & 15)
        self._nibble(value & 15)

    def show(self, top, bottom):
        content = (top[:16].ljust(16), bottom[:16].ljust(16))
        for row in (0, 1):
            if content[row] == self.previous[row]:
                continue
            self.send(0x80 if row == 0 else 0xC0, True)
            for ch in content[row]:
                self.send(ord(ch))
        self.previous = content


class LEDMatrix:
    """Matriz 4x4: barrido de filas, sin hilos ni retardos bloqueantes.

    Solo una fila se activa en cada instante. Se requiere una resistencia
    limitadora de corriente por columna y conexiones anodo=fila/catodo=columna.
    LED 1=(col 1, fila 4), LED 2=(col 1, fila 3), etc.
    """
    def __init__(self, gpio):
        self.gpio = gpio
        occupied = {BTN_LEFT, BTN_OK, BTN_RIGHT, *PADS, LCD_RS, LCD_E, *LCD_DATA}
        led_pins = LED_ROWS + LED_COLUMNS
        if len(set(led_pins)) != 8 or occupied.intersection(led_pins):
            raise RuntimeError('Conflicto entre GPIO de LED, LCD y botones')

        # Antes de activar filas, configurar cátodos en HIGH (apagados).
        for pin in LED_COLUMNS:
            gpio.setup(pin, gpio.OUT, initial=gpio.HIGH)
        for pin in LED_ROWS:
            gpio.setup(pin, gpio.OUT, initial=gpio.LOW)
        self.row = -1
        self.dim_phase = 0

    @staticmethod
    def coords(step):
        """Paso 0..15 -> fila, columna, ambos base cero."""
        if not 0 <= step < 16:
            raise ValueError('Paso de LED fuera de rango')
        return 3 - (step % 4), step // 4

    def is_lit(self, state, step, now):
        """Capa tenue: notas grabadas. Capa brillante: cursor y reproducción."""
        if state.mode == 0:  # SEQ: mostrar únicamente la pista de batería seleccionada
            programmed = state.drum_patterns[state.selected_drum][step]
        elif state.mode == 3:  # BASS: mostrar los 16 pasos del bajo
            programmed = state.bass_pattern[step] >= 0
        else:
            programmed = False

        # Media intensidad aproximada para los pasos que contienen notas.
        if programmed and self.dim_phase == 0:
            return True

        # La reproducción muestra el cabezal aunque sea un silencio.
        if state.playing and state.playhead == step:
            return True

        # Editando: cursor a ~2 Hz para diferenciarlo del cabezal.
        if state.mode in (0, 3) and state.cursor == step:
            return int(now * 4) % 2 == 0
        return False

    def refresh(self, state, now):
        """Invocar repetidamente desde el bucle principal (aprox. cada 2 ms)."""
        # Apagar primero todas las filas para evitar los LED fantasma.
        for pin in LED_ROWS:
            self.gpio.output(pin, self.gpio.LOW)
        for pin in LED_COLUMNS:
            self.gpio.output(pin, self.gpio.HIGH)

        self.row = (self.row + 1) % 4
        if self.row == 0:
            self.dim_phase ^= 1

        selected = []
        for col, pin in enumerate(LED_COLUMNS):
            # Orden físico: LED 1=col 1/fila 4; LED 16=col 4/fila 1.
            step = col * 4 + (3 - self.row)
            if self.is_lit(state, step, now):
                selected.append(pin)
        if selected:
            for pin in selected:
                self.gpio.output(pin, self.gpio.LOW)
            self.gpio.output(LED_ROWS[self.row], self.gpio.HIGH)

    def off(self):
        for pin in LED_ROWS:
            self.gpio.output(pin, self.gpio.LOW)
        for pin in LED_COLUMNS:
            self.gpio.output(pin, self.gpio.HIGH)
