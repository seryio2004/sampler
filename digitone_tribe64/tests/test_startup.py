"""Native engine bootstrap and SSH display discovery, without touching host sessions."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from hdmi_ui import build, display

PROJECT=Path(__file__).resolve().parents[1]


def elf(identity):
    header=bytearray(20);header[:4]=b'\x7fELF';header[4],header[5]=identity[:2]
    header[18:20]=identity[2].to_bytes(2,'little' if identity[1]==1 else 'big')
    return bytes(header)


class NativeBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='tribe64-build-test-')
        self.root=Path(self.temp.name)
        (self.root/'src').mkdir()
        (self.root/'src/main.c').write_text('/* source */')
        (self.root/'Makefile').write_text('# fixture')
        self.target=self.root/'sampler-c'

    def tearDown(self): self.temp.cleanup()

    def current(self):
        self.target.write_bytes(elf(build.native_identity()))
        self.target.chmod(0o700)
        stamp=max(p.stat().st_mtime_ns for p in self.root.glob('**/*') if p.is_file())+1_000_000_000
        os.utime(self.target,ns=(stamp,stamp))

    def fake_compile(self,*args,**kwargs):
        self.assertEqual(args[0],['make','-B','sampler-c'])
        self.assertEqual(kwargs['cwd'],self.root)
        self.current()
        return SimpleNamespace(returncode=0)

    def test_matching_binary_is_not_executed_or_rebuilt(self):
        self.current()
        with patch.object(build.subprocess,'run') as compile:
            self.assertEqual(build.ensure_engine(self.root),self.target)
            compile.assert_not_called()

    def test_x86_copy_on_arm_requires_native_rebuild(self):
        self.target.write_bytes(elf((2,1,62)))
        def compile(*args,**kwargs):
            self.target.write_bytes(elf((2,1,183)))
            return SimpleNamespace(returncode=0)
        with patch.object(build,'native_identity',return_value=(2,1,183)),patch.object(build.subprocess,'run',side_effect=compile) as called:
            build.ensure_engine(self.root)
            self.assertEqual(called.call_count,1)
        self.assertEqual(build.elf_identity(self.target),(2,1,183))

    def test_32_bit_userspace_is_distinct_from_arm64(self):
        self.assertNotEqual(elf((1,1,40)),elf((2,1,183)))
        self.target.write_bytes(elf((1,1,40)))
        self.assertEqual(build.elf_identity(self.target),(1,1,40))
        self.assertIsNone(build.elf_identity(self.root/'missing'))
        self.target.write_bytes(b'not ELF')
        self.assertIsNone(build.elf_identity(self.target))

    def test_stale_sources_rebuild_without_working_directory_assumptions(self):
        self.current()
        stamp=self.target.stat().st_mtime_ns+1_000_000_000
        os.utime(self.root/'src/main.c',ns=(stamp,stamp))
        with patch.object(build.subprocess,'run',side_effect=self.fake_compile) as called:
            build.ensure_engine(self.root)
            called.assert_called_once()

    def test_missing_makefile_has_actionable_error(self):
        (self.root/'Makefile').unlink()
        with self.assertRaisesRegex(build.BuildError,'fork completo'):
            build.ensure_engine(self.root)

    def test_failed_build_does_not_attempt_foreign_execution(self):
        self.target.write_bytes(b'foreign')
        with patch.object(build.subprocess,'run',return_value=SimpleNamespace(returncode=2)):
            with self.assertRaisesRegex(build.BuildError,'dependencias'):
                build.ensure_engine(self.root)
        self.assertEqual(self.target.read_bytes(),b'foreign')

    def test_launcher_runs_from_an_unrelated_directory(self):
        if not (PROJECT/'sampler-c').exists():
            self.skipTest('Compila sampler-c para comprobar el lanzador completo')
        self.current()
        shutil.copy(PROJECT/'Makefile',self.root/'Makefile')
        shutil.copytree(PROJECT/'src',self.root/'src',dirs_exist_ok=True)
        shutil.copy(PROJECT/'sampler-c',self.target)
        (self.root/'samples').mkdir()
        (self.root/'kits').mkdir()
        shutil.copy(PROJECT/'kits/tribe64.json',self.root/'kits/tribe64.json')
        (self.root/'scripts').mkdir()
        shutil.copy(PROJECT/'scripts/asegurar_motor.py',self.root/'scripts/asegurar_motor.py')
        (self.root/'hdmi_ui').mkdir()
        shutil.copy(PROJECT/'hdmi_ui/__init__.py',self.root/'hdmi_ui/__init__.py')
        shutil.copy(PROJECT/'hdmi_ui/build.py',self.root/'hdmi_ui/build.py')
        launcher=self.root/'iniciar_tribe64.sh'
        shutil.copy(PROJECT/'iniciar_tribe64.sh',launcher)
        stamp=max(p.stat().st_mtime_ns for p in self.root.glob('**/*') if p.is_file())+1_000_000_000
        os.utime(self.target,ns=(stamp,stamp))
        outside=self.root/'unrelated';outside.mkdir()
        result=subprocess.run([str(launcher),'--headless','--device','null','--no-save','--seconds','.1'],
                              cwd=outside,capture_output=True,timeout=8)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertIn(b'Sampler C',result.stdout)

    def test_failed_link_preserves_existing_engine_atomically(self):
        shutil.copy(PROJECT/'Makefile',self.root/'Makefile')
        shutil.copytree(PROJECT/'src',self.root/'src',dirs_exist_ok=True)
        compiler=self.root/'fake-cc'
        compiler.write_text('#!/usr/bin/env python3\nimport sys\nfrom pathlib import Path\nPath(sys.argv[sys.argv.index("-o")+1]).write_bytes(b"partial")\nraise SystemExit(1)\n')
        compiler.chmod(0o700)
        self.target.write_bytes(b'existing-working-engine')
        result=subprocess.run(['make','-B','-o','check-deps','sampler-c',f'CC={compiler}'],cwd=self.root,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.target.read_bytes(),b'existing-working-engine')
        self.assertEqual(list(self.root.glob('sampler-c.tmp.*')),[])


class DisplayDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='tribe64-display-test-')
        self.root=Path(self.temp.name)
        self.home=self.root/'home';self.home.mkdir()
        self.proc=self.root/'proc';self.proc.mkdir()
        self.sockets=self.root/'x11';self.sockets.mkdir()
        self.runtime_root=self.root/'run'
        self.runtime=self.runtime_root/str(os.getuid());self.runtime.mkdir(parents=True)
        self.env={'HOME':str(self.home),'SSH_CONNECTION':'remote ssh session','DISPLAY':'localhost:10.0',
                  'XDG_RUNTIME_DIR':str(self.runtime)}

    def tearDown(self): self.temp.cleanup()

    def discover(self,run=None,**kwargs):
        return display.discover_displays(self.env,proc_root=self.proc,x11_dir=self.sockets,
                                        runtime_root=self.runtime_root,home=self.home,
                                        run=run or (lambda args,env:''),**kwargs)

    def process(self,pid,name,environment,arguments=()):
        path=self.proc/str(pid);path.mkdir()
        (path/'comm').write_text(name)
        (path/'environ').write_bytes(b'\0'.join(f'{k}={v}'.encode() for k,v in environment.items()))
        (path/'cmdline').write_bytes(b'\0'.join(str(a).encode() for a in arguments))

    def test_ssh_forwarding_is_not_selected_and_secrets_are_not_imported(self):
        auth=self.runtime/'.Xwaylandauth.test';auth.write_bytes(b'cookie-not-printed')
        self.process(101,'wf-panel-pi',{'DISPLAY':':2','XAUTHORITY':str(auth),'API_SECRET':'not-a-display-variable'})
        result=self.discover()
        self.assertTrue(result)
        self.assertTrue(all(candidate.display==':2' for candidate in result))
        self.assertTrue(any(candidate.environment.get('XAUTHORITY')==str(auth) for candidate in result))
        for candidate in result:
            self.assertNotIn('API_SECRET',candidate.environment)

    def test_labwc_systemd_environment(self):
        def run(args,env):
            return 'DISPLAY=:1\nWAYLAND_DISPLAY=wayland-1\nPASSWORD=secret\n' if args[0]=='systemctl' else ''
        result=self.discover(run)
        self.assertEqual(result[0].display,':1')
        self.assertEqual(result[0].environment['WAYLAND_DISPLAY'],'wayland-1')
        self.assertNotIn('PASSWORD',result[0].environment)

    def test_xwayland_auth_from_server_arguments(self):
        auth=self.runtime/'server-auth';auth.write_bytes(b'cookie')
        self.process(101,'Xwayland',{},['Xwayland',':3','-rootless','-auth',auth])
        result=self.discover()
        self.assertTrue(any(c.display==':3' and c.environment.get('XAUTHORITY')==str(auth) for c in result))

    def test_socket_and_runtime_cookie_fallback(self):
        (self.sockets/'X7').touch()
        auth=self.runtime/'.mutter-Xwaylandauth.test';auth.touch()
        result=self.discover()
        self.assertTrue(any(c.display==':7' and c.environment.get('XAUTHORITY')==str(auth) for c in result))

    def test_logind_uses_only_local_sessions_of_this_user(self):
        self.process(101,'session',{'DISPLAY':':4'})
        def run(args,env):
            if args[:2]==['loginctl','list-sessions']:
                return f'c1 {os.getuid()} user seat0 tty1\nc2 {os.getuid()} user - pts/0\nc3 999999 other seat0 tty2\n'
            if args[:2]==['loginctl','show-session']:
                self.assertEqual(args[2],'c1')
                return 'Display=\nLeader=101\nType=wayland\nRemote=no\n'
            return ''
        self.assertEqual(self.discover(run)[0].display,':4')

    def test_no_display_does_not_guess_zero(self):
        self.assertEqual(self.discover(),[])
        with self.assertRaisesRegex(display.DisplayError,'raspi-config'):
            display.open_local_display(None,discover=lambda:[],check=lambda c:False)

    def test_invalid_authority_path_does_not_crash_discovery(self):
        self.env.update(DISPLAY=':0',XAUTHORITY=str(self.root/'missing'/'auth'))
        result=self.discover()
        self.assertTrue(result)
        self.assertNotIn('XAUTHORITY',result[0].environment)

    def test_tries_authorizations_and_commits_only_a_working_display(self):
        class Root:
            def withdraw(self): pass
        class TK:
            TclError=RuntimeError
            def Tk(self,screenName): return Root()
        candidates=[display.Candidate(':0',{'DISPLAY':':0','XAUTHORITY':'wrong'},'test'),
                    display.Candidate(':1',{'DISPLAY':':1','XAUTHORITY':'good'},'test')]
        with patch.dict(os.environ,{'DISPLAY':'localhost:10.0'},clear=True),contextlib.redirect_stderr(io.StringIO()) as messages:
            display.open_local_display(TK(),discover=lambda:candidates,check=lambda c:c.display==':1')
            self.assertEqual(os.environ['DISPLAY'],':1')
            self.assertEqual(os.environ['XAUTHORITY'],'good')
            self.assertNotIn('good',messages.getvalue())

    def test_explicit_local_display_and_names(self):
        self.assertEqual(display.local_name('unix/:3'),':3')
        self.assertEqual(display.local_name(':3.1'),':3.1')
        self.assertIsNone(display.local_name('localhost:10.0'))
        self.assertIsNone(display.local_name(':0;bad'))
        with self.assertRaises(display.DisplayError):
            display.open_local_display(None,'localhost:10',discover=lambda:[])



class KioskTests(unittest.TestCase):
    def test_fullscreen_uses_hdmi_dimensions_without_window_manager(self):
        from hdmi_ui.app import configure_window
        from unittest.mock import Mock
        for width,height in ((1920,1080),(800,480)):
            root=Mock()
            root.winfo_screenwidth.return_value=width
            root.winfo_screenheight.return_value=height
            configure_window(root,False)
            root.geometry.assert_called_once_with(f'{width}x{height}+0+0')
            root.attributes.assert_called_once_with('-fullscreen',True)
            root.minsize.assert_not_called()

    def test_kiosk_launch_routes_and_console_requirement(self):
        with tempfile.TemporaryDirectory(prefix='tribe64-kiosk-') as temp:
            root=Path(temp)
            launcher=root/'iniciar_kiosco.sh'
            shutil.copy(PROJECT/'iniciar_kiosco.sh',launcher)
            scripts={
                'iniciar_hdmi.sh':'printf "visor\\n"; printf "%s\\n" "$@"',
                'startx':'printf "startx\\n"; printf "%s\\n" "$@"',
                'tty':'printf "%s\\n" "$TEST_TTY"',
            }
            for name,body in scripts.items():
                target=root/name
                target.write_text('#!/bin/sh\n'+body+'\n')
                target.chmod(0o755)
            env=dict(os.environ,PATH=str(root)+':'+os.environ['PATH'],DISPLAY=':0',TEST_TTY='/dev/pts/1')
            def run():
                return subprocess.run([str(launcher),'--arduino','/dev/ttyACM0'],env=env,cwd='/tmp',capture_output=True,text=True)
            result=run()
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(result.stdout.splitlines(),['visor','--arduino','/dev/ttyACM0'])
            env.update(DISPLAY='localhost:10.0',TEST_TTY='/dev/tty1')
            result=run()
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(result.stdout.splitlines(),['startx',str(root/'iniciar_hdmi.sh'),'--arduino','/dev/ttyACM0','--','-nolisten','tcp','-s','0','-dpms'])
            env.update(DISPLAY='',TEST_TTY='/dev/pts/1')
            result=run()
            self.assertNotEqual(result.returncode,0)
            self.assertIn('consola local',result.stderr)


if __name__=='__main__': unittest.main()
