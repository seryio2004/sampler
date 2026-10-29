"""Launch one audio engine and render its private, nonblocking HDMI telemetry."""
import argparse
from collections import deque
import json
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

from .build import BuildError, ensure_engine
from .display import DisplayError, open_local_display
from .painter import ImagePainter, TkPainter
from .view import BG, MODES, View, demo_state

PROJECT = Path(__file__).resolve().parents[1]


class EngineLink:
    def __init__(self, arguments, state=None):
        engine=ensure_engine(PROJECT)
        self.parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.parent.setblocking(False)
        try:
            self.parent.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 262144)
        except OSError:
            pass  # Buffer enlargement is optional on restricted hosts.
        self.latest = None
        self.received = 0.
        self.errors = deque(maxlen=12)
        argv = [str(engine), '--samples', str(PROJECT/'samples')]
        if state is not None:
            argv += ['--state', str(state)]
        argv += list(arguments)+['--ui-fd', str(child.fileno())]
        try:
            self.process = subprocess.Popen(argv, cwd=PROJECT, stdin=subprocess.DEVNULL,
                                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                            pass_fds=(child.fileno(),))
        except BaseException:
            self.parent.close()
            raise
        finally:
            child.close()
        threading.Thread(target=self.read_errors, daemon=True).start()

    def read_errors(self):
        for line in self.process.stderr:
            text = line.decode(errors='replace').strip()
            self.errors.append(text)
            print(text, file=sys.stderr)

    def poll(self):
        payload = None
        for _ in range(64):
            try:
                payload = self.parent.recv(65536)
            except BlockingIOError:
                break
        if payload:
            try:
                value = json.loads(payload)
                if value.get('protocol')==1 and len(value['pattern'])==9 and len(value['tracks'])==9 and len(value['scope'])==256 and len(value.get('track_scopes', []))==10:
                    self.latest, self.received = value, time.monotonic()
            except (ValueError, TypeError, KeyError):
                pass
        return self.latest

    @property
    def connected(self):
        return self.process.poll() is None and time.monotonic()-self.received<1.5

    def close(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=3)
        self.parent.close()
        self.process.stderr.close()


def configure_window(root, windowed):
    if windowed:
        root.geometry('1280x800')
        root.minsize(960, 600)
    else:
        # xinit can run the viewer without a window manager. In that case the
        # fullscreen hint is ignored, so request the actual screen geometry too.
        root.geometry(f'{root.winfo_screenwidth()}x{root.winfo_screenheight()}+0+0')
    root.attributes('-fullscreen', not windowed)


class Application:
    """Passive display: there are no musical input handlers or command channel."""
    def __init__(self, root, options, engine_arguments):
        import tkinter as tk
        self.root, self.options = root, options
        self.closed = False
        self.closing_at = None
        self.link = None
        self.state = demo_state()
        self.state['demo'] = bool(options.demo)
        self.state['mode'] = getattr(options, 'view', 'SEQ') if options.demo else 'SEQ'
        if not options.demo:
            self.state.update(playing=0, controller=0, scope=[0]*256,
                              track_scopes=[[0]*128 for _ in range(10)], waveform=[[0, 0] for _ in range(128)])
        self.view = View()
        self.canvas = tk.Canvas(root, bg=BG, highlightthickness=0, cursor='none', takefocus=0)
        self.canvas.pack(fill='both', expand=True)
        self.painter = None
        root.title('TRIBE64 / VISOR HDMI')
        configure_window(root, options.windowed)
        root.configure(cursor='none')
        # Closing the decoration must not become a mouse control for the sampler.
        root.protocol('WM_DELETE_WINDOW', lambda: None)
        if not options.demo:
            state = PROJECT/'tribe64_state.json'
            custom_state = '--state' in engine_arguments
            if not custom_state and not state.exists():
                shutil.copyfile(PROJECT/'kits/tribe64.json', state)
            self.link = EngineLink(engine_arguments, None if custom_state else state)
        root.after(20, self.tick)

    def tick(self):
        if self.closed or self.closing_at is not None:
            return
        if self.link:
            latest = self.link.poll()
            if latest:
                self.state = latest
            if self.link.process.poll() is not None:
                self.state['message'] = self.link.errors[-1] if self.link.errors else 'Motor cerrado'
        elif self.state['playing']:
            self.state['playhead'] = int(time.monotonic()*self.state['bpm']/15)%64
            self.state['audio_frame'] += 2940
        self.painter = TkPainter(self.canvas)
        self.view.draw(self.painter, self.state, not self.link or self.link.connected)
        self.root.after(67, self.tick)

    def close(self):
        """Administrative shutdown, invoked by process signals, never GUI input."""
        if self.closing_at is not None:
            return
        self.closing_at = time.monotonic()
        if self.link and self.link.process.poll() is None:
            self.link.process.send_signal(signal.SIGINT)
        self.root.after(50, self.finish_close)

    def finish_close(self):
        if self.closed:
            return
        if self.link and self.link.process.poll() is None and time.monotonic()-self.closing_at<2:
            self.link.poll()
            self.root.after(50, self.finish_close)
            return
        if self.link:
            self.link.close()
        self.closed = True
        self.root.destroy()


def main(argv=None):
    parser = argparse.ArgumentParser(description='Visor HDMI pasivo de Tribe64. Solo los controles fisicos manejan el instrumento.')
    parser.add_argument('--windowed', action='store_true', help='Abrir en ventana en lugar de pantalla completa')
    parser.add_argument('--display', help='Pantalla local concreta; por defecto se detecta automáticamente')
    parser.add_argument('--check-display', action='store_true', help='Comprobar el HDMI local desde SSH sin arrancar audio')
    parser.add_argument('--demo', action='store_true', help='Vista de demostracion sin audio ni GPIO')
    parser.add_argument('--preview', type=Path, help='Exportar una vista PNG sin abrir ventana ni motor (requiere Pillow)')
    parser.add_argument('--view', default='SEQ', choices=MODES, help='Pantalla para --preview')
    options, engine_arguments = parser.parse_known_args(argv)
    if options.preview:
        state = demo_state()
        state['mode'] = options.view
        painter = ImagePainter()
        View().draw(painter, state)
        options.preview.parent.mkdir(parents=True, exist_ok=True)
        painter.image.save(options.preview)
        print(options.preview)
        return 0
    try:
        if not options.demo and not options.check_display:
            ensure_engine(PROJECT)
        import tkinter as tk
        root=open_local_display(tk,options.display)
    except ImportError:
        print('HDMI: falta Tkinter. Instala: sudo apt install python3-tk',file=sys.stderr)
        return 1
    except (BuildError,DisplayError,OSError) as exc:
        print(f'HDMI: {exc}',file=sys.stderr)
        return 1
    if options.check_display:
        root.destroy()
        return 0
    app = None
    try:
        app = Application(root, options, engine_arguments)
        root.after_idle(root.deiconify)
        signal.signal(signal.SIGINT, lambda signum, frame: app.close())
        signal.signal(signal.SIGTERM, lambda signum, frame: app.close())
        root.mainloop()
    except (BuildError, OSError, ValueError) as exc:
        print(f'HDMI: {exc}', file=sys.stderr)
        return 1
    finally:
        if app and app.link and not app.closed:
            app.link.close()
        try:
            root.destroy()
        except tk.TclError:
            pass
    return 0
