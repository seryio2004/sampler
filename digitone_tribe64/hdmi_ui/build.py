"""Validate the engine against the local userspace and rebuild when required."""
import fcntl
import os
from pathlib import Path
import platform
import shutil
import stat
import struct
import subprocess
import sys


class BuildError(RuntimeError):
    pass


def elf_identity(path):
    """(ELF class, byte order, machine), without executing a foreign binary."""
    try:
        with Path(path).open('rb') as stream:
            header = stream.read(20)
    except OSError:
        return None
    if len(header)<20 or header[:4]!=b'\x7fELF' or header[4] not in (1,2) or header[5] not in (1,2):
        return None
    endian='little' if header[5]==1 else 'big'
    return header[4],header[5],int.from_bytes(header[18:20],endian)


def native_identity():
    # Handles 32-bit Raspberry userspace on a 64-bit kernel as well.
    identity=elf_identity('/proc/self/exe')
    if identity:
        return identity
    bits=struct.calcsize('P')*8
    arch=platform.machine().lower()
    machine=183 if arch in ('aarch64','arm64') and bits==64 else 40 if arch.startswith(('arm','aarch')) else 62 if arch in ('x86_64','amd64') and bits==64 else 3 if arch in ('i386','i686','x86_64','amd64') else 243 if arch.startswith('riscv') else None
    if machine is None:
        raise BuildError('Arquitectura no reconocida. Compila el motor localmente con make.')
    return 2 if bits==64 else 1,1 if sys.byteorder=='little' else 2,machine


def build_reason(project, target):
    actual=elf_identity(target)
    if actual!=native_identity():
        return 'binario ausente o de otra arquitectura'
    sources=[project/'Makefile',*project.glob('src/*.[ch]')]
    if any(source.stat().st_mtime_ns>target.stat().st_mtime_ns for source in sources):
        return 'código actualizado'
    return None


def ensure_engine(project):
    project=Path(project).resolve()
    target=project/'sampler-c'
    if not (project/'Makefile').is_file():
        raise BuildError(f'Falta {project}/Makefile. Copia el fork completo, incluyendo Makefile y src/.')
    if not (project/'src/main.c').is_file():
        raise BuildError(f'Faltan las fuentes en {project}/src. Copia el fork completo para poder compilar en la Raspberry.')
    # A second launcher waits for the first build, then checks the new binary again.
    with (project/'.sampler-build.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        reason=build_reason(project,target)
        if reason:
            if not shutil.which('make'):
                raise BuildError('Falta make. Instala las dependencias de compilación indicadas en README.md.')
            print(f'Motor: {reason}; compilando para {platform.machine()}…',file=sys.stderr)
            result=subprocess.run(['make','-B','sampler-c'],cwd=project,check=False)
            if result.returncode:
                raise BuildError(f'No se pudo compilar el motor en {project}. Revisa las dependencias que indica make.')
            if elf_identity(target)!=native_identity():
                raise BuildError('El compilador generó un binario incompatible. Revisa CC y usa el compilador nativo de la Raspberry.')
        if not os.access(target,os.X_OK):
            try:
                target.chmod(target.stat().st_mode|stat.S_IXUSR)
            except OSError as exc:
                raise BuildError(f'No se puede dar permiso de ejecución a {target.name}: {exc}') from exc
    return target
