"""Passive HDMI receives real PCM and physical serial controls; no GUI commands."""
import copy
import json
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hdmi_ui.app import EngineLink, PROJECT
from hdmi_ui.view import View, demo_state, MODES
from physical_driver import PhysicalController


def wait(link, predicate, timeout=5):
    deadline = time.monotonic()+timeout
    latest = None
    while time.monotonic()<deadline:
        latest = link.poll()
        if latest and predicate(latest):
            return latest
        assert link.process.poll() is None, list(link.errors)
        time.sleep(.015)
    fields = ('mode','playing','controller','cursor','drum','menu_open','menu_selected','bpm')
    raise AssertionError({key: latest.get(key) for key in fields} if latest else None)


class Recorder:
    def __init__(self): self.texts=[]
    def rect(self, *args): pass
    def line(self, *args): pass
    def oval(self, *args): pass
    def text(self, x, y, text, *args, **kwargs): self.texts.append(str(text))


def renderer_is_passive():
    view = View()
    state = demo_state()
    assert not hasattr(view, 'send') and not hasattr(view, 'click')
    for mode in MODES:
        state['mode'] = mode
        original = copy.deepcopy(state)
        recorder = Recorder()
        view.draw(recorder, state)
        assert state==original
        words=' '.join(recorder.texts).upper()
        for forbidden in ('RATON', 'CLIC', 'SHIFT', 'TECLADO', 'F11', 'ESPACIO:', 'A0:', 'FN+', 'INPUT =', 'PATTERN[', 'VOICES['):
            assert forbidden not in words


def stdin_is_disabled(directory):
    # Even explicitly requested diagnostics cannot control a connected HDMI motor.
    receiver, sender = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    receiver.settimeout(3)
    try:
        process = subprocess.Popen([str(PROJECT/'sampler-c'),'--terminal','--headless','--device','null',
                                    '--samples',str(PROJECT/'samples'),'--state',str(directory/'ignored.json'),
                                    '--no-save','--seconds','.3','--ui-fd',str(sender.fileno())],
                                   stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   pass_fds=(sender.fileno(),))
        sender.close()
        _, errors = process.communicate(b'mode mute\nsound 1 24 0 0.1\nq\n', timeout=5)
        assert process.returncode==0, errors.decode()
        packet=json.loads(receiver.recv(65536))
        assert packet['mode']=='SEQ' and packet['tracks'][0]['pitch']==0
    finally:
        receiver.close(); sender.close()
    result = subprocess.run([str(PROJECT/'sampler-c'),'--headless','--device','null','--no-save',
                             '--samples',str(PROJECT/'samples'),'--state',str(directory/'ignored.json'),'--seconds','.1'],
                            input=b'mode mute\nq\n',capture_output=True,timeout=4)
    assert result.returncode==0 and b'Modo: MUTE' not in result.stdout


renderer_is_passive()
with tempfile.TemporaryDirectory(prefix='tribe64-passive-') as tmp:
    directory=Path(tmp)
    controller=PhysicalController()
    link=EngineLink(['--headless','--device','null','--arduino',controller.path],directory/'state.json')
    try:
        assert not hasattr(link,'send') and link.process.stdin is None
        first=wait(link,lambda s:s['controller'] and any(s['scope']))
        assert len(first['track_scopes'])==10 and all(len(row)==128 for row in first['track_scopes'])
        assert any(first['track_scopes'][0]) and not any(first['track_scopes'][8])
        assert any(pair[0] or pair[1] for pair in first['waveform'])
        for page in (1,2,3):
            controller.tap_fn()
            wait(link,lambda s,n=page:s['cursor']//16==n)
        controller.change(nav=1023)
        wait(link,lambda s:s['cursor']==63)
        controller.tap_ok()
        wait(link,lambda s:s['pattern'][0][63])
        controller.mute_mode()
        wait(link,lambda s:s['mode']=='MUTE')
        controller.tap_ok()
        wait(link,lambda s:s['stored_muted'][0])
        controller.mute_mode()
        wait(link,lambda s:s['mode']=='SEQ')
        controller.open_menu()
        wait(link,lambda s:s['menu_open'])
        controller.change(nav=365)
        wait(link,lambda s:s['menu_selected']==10)
        controller.tap_ok()
        wait(link,lambda s:s['mode']=='SOUND' and not s['menu_open'])
        controller.change(nav=512,cutoff=1023,resonance=1023)
        controller.change(nav=800,cutoff=300,resonance=500)
        changed=wait(link,lambda s:s['tracks'][0]['pitch']>10 and s['tracks'][0]['cutoff']<.4)
        assert changed['tracks'][0]['length']<.6
        # Stop reading snapshots for a while; real physical controls still work.
        time.sleep(1.2)
        controller.mute_mode()
        wait(link,lambda s:s['mode']=='MUTE')
        controller.mute_mode()
        wait(link,lambda s:s['mode']=='SOUND')
        link.process.send_signal(signal.SIGINT)
        assert link.process.wait(timeout=4)==0
        saved=json.loads((directory/'state.json').read_text())
        assert saved['patterns'][0][63] and saved['scenes'][0]['muted'][0]
        assert saved['pad_sound'][0]['pitch']>10
    finally:
        controller.close()
        link.close()
    stdin_is_disabled(directory)
print('HDMI passive: real global/track waves, physical controls, no GUI input/channel, ignored stdin, save and slow display OK')
