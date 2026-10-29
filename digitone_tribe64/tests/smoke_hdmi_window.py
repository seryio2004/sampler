"""Optional Tk test: desktop input is inert; physical Arduino input updates HDMI."""
import argparse
import copy
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import tkinter as tk

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from hdmi_ui.app import Application
from physical_driver import PhysicalController

parser=argparse.ArgumentParser()
parser.add_argument('--screenshot',type=Path)
args=parser.parse_args()
root=tk.Tk()
root.geometry('1280x800+0+0')


def wait_for(predicate,timeout=5):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        root.update()
        if predicate(): return
        time.sleep(.012)
    raise AssertionError('HDMI did not reach expected state')


with tempfile.TemporaryDirectory(prefix='tribe64-passive-tk-') as temp:
    controller=PhysicalController()
    app=Application(root,SimpleNamespace(demo=False,windowed=True),
                    ['--headless','--device','null','--no-save','--arduino',controller.path,
                     '--state',str(Path(temp)/'state.json')])
    try:
        wait_for(lambda:app.link.connected and app.painter is not None and app.state['controller'])
        assert len(app.canvas.find_all())>350 and app.canvas.cget('cursor')=='none'
        keys=('mode','cursor','drum','bpm','muted','pattern','tracks','song')
        before={key:copy.deepcopy(app.state[key]) for key in keys}
        app.canvas.focus_force()
        for x,y in [(200,230),(70,245),(1100,368),(900,100)]:
            app.canvas.event_generate('<Button-1>',x=x,y=y,state=1)
            app.canvas.event_generate('<B1-Motion>',x=x+50,y=y)
            app.canvas.event_generate('<ButtonRelease-1>',x=x+50,y=y)
        for key in ('space','Return','Tab','Right','1','m','F11','Escape'):
            root.event_generate('<KeyPress>',keysym=key)
        root.event_generate('<Control-q>')
        root.tk.call('eval',root.protocol('WM_DELETE_WINDOW'))
        deadline=time.monotonic()+.3
        while time.monotonic()<deadline:
            root.update();time.sleep(.01)
        assert not app.closed and app.link.process.poll() is None
        assert {key:app.state[key] for key in keys}==before
        assert not app.canvas.bind() and not root.bind()
        controller.tap_fn()
        wait_for(lambda:app.state['cursor']//16==1)
        controller.mute_mode()
        wait_for(lambda:app.state['mode']=='MUTE')
        controller.tap_ok()
        wait_for(lambda:app.state['muted'][0])
        controller.mute_mode()
        wait_for(lambda:app.state['mode']=='SEQ')
        wait_for(lambda:any(app.state['scope']))
        if args.screenshot:
            from PIL import ImageGrab
            args.screenshot.parent.mkdir(parents=True,exist_ok=True)
            root.update_idletasks()
            x,y=root.winfo_rootx(),root.winfo_rooty()
            ImageGrab.grab(bbox=(x,y,x+root.winfo_width(),y+root.winfo_height())).save(args.screenshot)
        app.close()  # Administrative lifecycle, never bound to desktop input.
        wait_for(lambda:app.closed)
        assert app.link.process.returncode==0
    finally:
        controller.close()
        if not app.closed:
            app.link.close();root.destroy()
print('HDMI Tk passive: mouse/keys/window close inert, real physical controls, wave display and signal shutdown OK')
