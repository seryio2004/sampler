"""Serial stand-in for the sampler's three pots and two physical buttons."""
import os
import pty
import threading
import time


class PhysicalController:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.values = [0, 0, 0, 0, 0]
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.writer, daemon=True)
        self.thread.start()

    def writer(self):
        while not self.stop.wait(.02):
            with self.lock:
                line = 'CTRL '+' '.join(map(str, self.values))+'\n'
            try:
                os.write(self.master, line.encode())
            except OSError:
                return

    def change(self, **values):
        keys = ['nav', 'cutoff', 'resonance', 'ok', 'fn']
        with self.lock:
            for key, value in values.items():
                self.values[keys.index(key)] = value
        time.sleep(.09)

    def tap_ok(self):
        self.change(ok=1)
        self.change(ok=0)

    def tap_fn(self):
        self.change(fn=1)
        self.change(fn=0)

    def mute_mode(self):
        self.change(fn=1)
        self.change(ok=1)
        self.change(ok=0)
        self.change(fn=0)

    def open_menu(self):
        self.change(ok=1)
        time.sleep(.68)
        self.change(ok=0)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=1)
        os.close(self.master)
        os.close(self.slave)
