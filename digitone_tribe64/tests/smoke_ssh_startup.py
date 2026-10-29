"""Optional acceptance test: discover and authenticate a local desktop from SSH."""
import argparse
import contextlib
import io
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from hdmi_ui.display import Candidate,discover_displays,open_local_display,probe

parser=argparse.ArgumentParser()
parser.add_argument('--xvfb',default=shutil.which('Xvfb'))
args=parser.parse_args()
if not args.xvfb or not shutil.which('xauth'):
    raise SystemExit('Esta prueba opcional requiere Xvfb y xauth.')

with tempfile.TemporaryDirectory(prefix='tribe64-ssh-session-') as temp:
    directory=Path(temp)
    number=next(n for n in range(197,230) if not Path(f'/tmp/.X11-unix/X{n}').exists() and not Path(f'/tmp/.X{n}-lock').exists())
    name=f':{number}'
    authority=directory/'Xauthority'
    result=subprocess.run(['xauth','-f',str(authority),'add',name,'MIT-MAGIC-COOKIE-1',secrets.token_hex(16)],capture_output=True)
    assert result.returncode==0,'No se pudo crear la autorización de prueba'
    server=subprocess.Popen([args.xvfb,name,'-screen','0','1280x800x24','-nolisten','tcp','-auth',str(authority)],
                            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        until=time.monotonic()+5
        while not Path(f'/tmp/.X11-unix/X{number}').exists() and time.monotonic()<until:
            assert server.poll() is None,'No se pudo iniciar el servidor gráfico de prueba'
            time.sleep(.03)
        assert Path(f'/tmp/.X11-unix/X{number}').exists()
        clean=dict(os.environ)
        for key in ('DISPLAY','XAUTHORITY','WAYLAND_DISPLAY','XDG_RUNTIME_DIR','DBUS_SESSION_BUS_ADDRESS'):
            clean.pop(key,None)
        clean['HOME']=str(directory)
        clean['SSH_CONNECTION']='192.0.2.1 1234 192.0.2.2 22'
        clean['DISPLAY']='localhost:10.0'  # The forwarded computer must not be selected.
        candidate=Candidate(name,{'DISPLAY':name,'XAUTHORITY':str(authority)},'test')
        assert probe(candidate,clean)
        assert not probe(Candidate(name,{'DISPLAY':name},'test'),clean),'El servidor de prueba debe requerir autenticación'
        # Isolate discovery fixtures; the final connection uses the real X server/cookie.
        proc=directory/'proc';process=proc/'101';process.mkdir(parents=True)
        (process/'comm').write_text('Xwayland')
        (process/'environ').write_bytes(b'WAYLAND_DISPLAY=wayland-1\0')
        (process/'cmdline').write_bytes(b'\0'.join(v.encode() for v in ('Xwayland',name,'-auth',str(authority))))
        runtime=directory/'run'/str(os.getuid());runtime.mkdir(parents=True)
        sockets=directory/'sockets';sockets.mkdir();(sockets/f'X{number}').touch()
        def discovery():
            return discover_displays(clean,proc_root=proc,x11_dir=sockets,runtime_root=runtime.parent,
                                     home=directory,run=lambda args,env:'')
        import tkinter as tk
        with patch.dict(os.environ,clean,clear=True),contextlib.redirect_stderr(io.StringIO()) as log:
            root=open_local_display(tk,discover=discovery)
            assert os.environ['DISPLAY']==name
            assert os.environ['XAUTHORITY']==str(authority)
            root.destroy()
        assert str(authority) not in log.getvalue(),'No se deben imprimir rutas de autorización'
        # Also exercise the actual CLI on the host process/session discovery path.
        command=[sys.executable,str(Path(__file__).resolve().parents[1]/'pantalla_hdmi.py'),'--check-display','--display',name]
        result=subprocess.run(command,env=clean,capture_output=True,text=True,timeout=15)
        assert result.returncode==0,result.stderr
        assert name in result.stderr
        for inherited in (None,'localhost:10.0'):
            automatic=dict(clean)
            if inherited is None:
                automatic.pop('DISPLAY',None)
            else:
                automatic['DISPLAY']=inherited
            result=subprocess.run(command[:-2],env=automatic,capture_output=True,text=True,timeout=20)
            assert result.returncode==0,result.stderr
            assert 'HDMI: pantalla local :' in result.stderr,result.stderr
            assert 'localhost:' not in result.stderr
    finally:
        server.terminate()
        server.wait(timeout=5)
        server.stderr.close()
print('SSH HDMI: no DISPLAY, forwarded DISPLAY ignored, real Xauthority, local session discovery and CLI OK')
