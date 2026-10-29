"""Find the user's local X11/XWayland session when launching over SSH."""
from dataclasses import dataclass
import os
from pathlib import Path
import pwd
import re
import subprocess
import sys

KEYS=('DISPLAY','XAUTHORITY','XDG_RUNTIME_DIR','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS')
LOCAL=re.compile(r'^(?:unix/?)?:(\d+)(?:\.(\d+))?$')
DESKTOP_NAMES=('labwc','wayfire','Xwayland','Xorg','Xvfb','lxsession','lxpanel','wf-panel','pcmanfm',
               'openbox','gnome-shell','gnome-session','plasmashell','kwin','xfce4-session','xfce4-panel',
               'lxqt-session','mate-session','weston','sway','cage','lxterminal','lightdm')


class DisplayError(RuntimeError):
    pass


@dataclass
class Candidate:
    display: str
    environment: dict
    source: str


def local_name(value):
    match=LOCAL.fullmatch(value or '')
    if not match:
        return None
    return ':'+match[1]+('.'+match[2] if match[2] and match[2]!='0' else '')


def filtered_environment(data, separator='\n'):
    if isinstance(data,bytes):
        data=data.decode(errors='replace')
    result={}
    for line in data.split(separator):
        key,equals,value=line.partition('=')
        if equals and key in KEYS and value and '\0' not in value:
            result[key]=value
    return result


def capture(arguments, env):
    try:
        result=subprocess.run(arguments,env=env,capture_output=True,text=True,timeout=1.2,check=False)
        return result.stdout if result.returncode==0 else ''
    except (OSError,subprocess.TimeoutExpired):
        return ''


def readable_file(value):
    try:
        path=Path(value).expanduser()
        return path if path.is_file() and os.access(path,os.R_OK) else None
    except OSError:
        return None


def discover_displays(environ=None, *, proc_root=Path('/proc'), x11_dir=Path('/tmp/.X11-unix'),
                      runtime_root=Path('/run/user'), home=None, uid=None, run=capture):
    """Only inspect our own desktop processes. Never export complete environments."""
    env=dict(os.environ if environ is None else environ)
    uid=os.getuid() if uid is None else uid
    home=Path.home() if home is None else Path(home)
    proc_root,x11_dir,runtime_root=map(Path,(proc_root,x11_dir,runtime_root))
    runtimes=[]
    for value in (env.get('XDG_RUNTIME_DIR'),str(runtime_root/str(uid))):
        if not value:
            continue
        path=Path(value)
        try:
            if path.is_dir() and path.stat().st_uid==uid and path not in runtimes:
                runtimes.append(path)
        except OSError:
            pass
    base={key:env[key] for key in KEYS if env.get(key) and key not in ('DISPLAY','XDG_RUNTIME_DIR')}
    if runtimes:
        base['XDG_RUNTIME_DIR']=str(runtimes[0])
        if (runtimes[0]/'bus').exists():
            base['DBUS_SESSION_BUS_ADDRESS']='unix:path='+str(runtimes[0]/'bus')
    command_env={**env,**base}
    seeds=[]
    def add(values,source):
        display=local_name(values.get('DISPLAY'))
        if display:
            seeds.append((display,{**base,**values,'DISPLAY':display},source))
    add(filtered_environment('\n'.join(f'{k}={v}' for k,v in env.items() if k in KEYS)),'entorno')
    add(filtered_environment(run(['systemctl','--user','show-environment'],command_env)),'sesión de usuario')

    def process_values(pid):
        path=proc_root/str(pid)
        try:
            if path.stat().st_uid!=uid:
                return None
            with (path/'environ').open('rb') as stream:
                return filtered_environment(stream.read(131072),'\0')
        except OSError:
            return None

    # Labwc panels/children inherit DISPLAY, even if the SSH shell does not.
    try:
        processes=sorted((p for p in proc_root.iterdir() if p.name.isdigit()),key=lambda p:int(p.name))
    except OSError:
        processes=[]
    for path in processes:
        try:
            if path.stat().st_uid!=uid:
                continue
            name=(path/'comm').read_text().strip()
            if not name.startswith(DESKTOP_NAMES):
                continue
        except OSError:
            continue
        values=process_values(path.name) or {}
        add(values,'proceso gráfico')
        if name.startswith(('Xorg','Xwayland','Xvfb')):
            try:
                arguments=(path/'cmdline').read_bytes().decode(errors='replace').split('\0')
            except OSError:
                arguments=[]
            display=next((local_name(a) for a in arguments if local_name(a)),None)
            if display:
                server=dict(values,DISPLAY=display)
                if '-auth' in arguments:
                    index=arguments.index('-auth')+1
                    if index<len(arguments) and arguments[index]:
                        server['XAUTHORITY']=arguments[index]
                add(server,'servidor gráfico')

    sessions=run(['loginctl','list-sessions','--no-legend','--no-pager'],command_env)
    for line in sessions.splitlines():
        fields=line.split()
        if len(fields)<4 or fields[1]!=str(uid) or not fields[3].startswith('seat'):
            continue
        properties=run(['loginctl','show-session',fields[0],'--no-pager','--property=Display',
                        '--property=Leader','--property=Type','--property=Remote'],command_env)
        info=dict(line.split('=',1) for line in properties.splitlines() if '=' in line)
        if info.get('Remote')=='yes' or info.get('Type') not in ('x11','wayland'):
            continue
        leader=process_values(info.get('Leader','')) or {}
        if info.get('Display'):
            leader['DISPLAY']=info['Display']
        add(leader,'sesión local')

    for path in sorted(x11_dir.glob('X[0-9]*'),key=lambda p:(len(p.name),p.name)):
        if path.name[1:].isdigit():
            add({'DISPLAY':':'+path.name[1:]},'socket local')

    authorities=[]
    for value in [env.get('XAUTHORITY'),str(home/'.Xauthority'),
                  *[str(v.get('XAUTHORITY','')) for _,v,_ in seeds]]:
        if value and value not in authorities:
            authorities.append(value)
    for runtime in runtimes:
        for pattern in ('.Xauthority','Xauthority','.Xwaylandauth.*','.mutter-Xwaylandauth.*','xauth_*'):
            for path in sorted(runtime.glob(pattern)):
                if str(path) not in authorities:
                    authorities.append(str(path))
    authorities=[str(path) for a in authorities if (path:=readable_file(a)) is not None]
    candidates=[];seen=set()
    for display,values,source in seeds:
        for authority in [values.get('XAUTHORITY'),*authorities,None]:
            proposed=dict(values)
            if authority:
                path=readable_file(authority)
                if path is None:
                    continue
                proposed['XAUTHORITY']=str(path)
            else:
                proposed.pop('XAUTHORITY',None)
            key=(display,proposed.get('XAUTHORITY'),proposed.get('XDG_RUNTIME_DIR'))
            if key not in seen:
                seen.add(key)
                candidates.append(Candidate(display,proposed,source))
    return candidates


def probe(candidate, environ=None):
    env=dict(os.environ if environ is None else environ)
    env.pop('XAUTHORITY',None)
    env.update(candidate.environment)
    # A subprocess bounds connection time and keeps failed Tcl instances out of the viewer.
    code='import sys, tkinter as tk; r=tk.Tk(screenName=sys.argv[1]); r.withdraw(); r.update_idletasks(); r.destroy()'
    try:
        result=subprocess.run([sys.executable,'-c',code,candidate.display],env=env,
                              stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=2,check=False)
        return result.returncode==0
    except (OSError,subprocess.TimeoutExpired):
        return False


def open_local_display(tk, requested=None, *, discover=discover_displays, check=probe):
    candidates=discover()
    if requested:
        display=local_name(requested)
        if not display:
            raise DisplayError('--display debe identificar una pantalla local, por ejemplo :0 o :1.')
        candidates=[c for c in candidates if c.display==display]
        if not candidates:
            env={key:os.environ[key] for key in KEYS if key in os.environ}
            env['DISPLAY']=display
            candidates=[Candidate(display,env,'selección explícita')]
    for candidate in candidates:
        if not check(candidate):
            continue
        old={key:os.environ.get(key) for key in KEYS}
        os.environ.pop('XAUTHORITY',None)
        os.environ.update(candidate.environment)
        root=None
        try:
            root=tk.Tk(screenName=candidate.display)
            root.withdraw()
        except tk.TclError:
            if root is not None:
                root.destroy()
            for key,value in old.items():
                if value is None:
                    os.environ.pop(key,None)
                else:
                    os.environ[key]=value
            continue
        print(f'HDMI: pantalla local {candidate.display} detectada ({candidate.source}).',file=sys.stderr)
        return root
    user=pwd.getpwuid(os.getuid()).pw_name
    if candidates:
        raise DisplayError(f'No se pudo abrir una pantalla local con el usuario {user}. '
                           'Usa el mismo usuario del escritorio, sin sudo, y comprueba que su sesión esté abierta. '
                           'En Wayland debe estar disponible XWayland. Si solo hay consola, habilita Escritorio '
                           'con inicio automático en sudo raspi-config.')
    raise DisplayError('No se encontró ninguna sesión gráfica local X11/XWayland. '
                       'Si la Raspberry arranca en consola, habilita Escritorio con inicio automático '
                       'en sudo raspi-config y reinicia. En Raspberry Pi OS Lite es necesario instalar '
                       'un escritorio y XWayland; conectar HDMI por sí solo no crea una sesión gráfica.')
