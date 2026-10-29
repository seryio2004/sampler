# Arranque desde SSH

Los lanzadores detectan la sesión gráfica local y recompilan el motor para la arquitectura del ordenador donde se ejecutan. Un `sampler-c` compilado en x86 no sirve directamente en una Raspberry ARM; ya no se ejecuta ese archivo sin comprobarlo.

## Copiar el proyecto

Desde la carpeta que contiene el fork en tu ordenador:

```bash
scp -r digitone_tribe64 sk@raspberrypi:~/
```

Copia también `Makefile`, `src/`, `hdmi_ui/` y `scripts/`. Si mantienes un `tribe64_state.json` en el ordenador, evita sobrescribir con él los cambios de tu Raspberry. Puedes transferir solo las fuentes con rsync:

```bash
rsync -av --exclude sampler-c --exclude build/ --exclude tribe64_state.json --exclude .sampler-build.lock digitone_tribe64/ sk@raspberrypi:~/digitone_tribe64/
```

## Instalar dependencias una vez

En la Raspberry, desde SSH:

```bash
cd ~/digitone_tribe64
sudo apt install build-essential pkg-config libasound2-dev libsndfile1-dev libmpg123-dev libjson-c-dev libsamplerate0-dev python3-tk fonts-dejavu-core
```

No ejecutes los lanzadores con sudo. Usa el mismo usuario que inicia el escritorio del HDMI. Detén antes el servicio del sampler antiguo si utiliza el mismo audio o GPIO.

## Comprobar y arrancar

```bash
cd ~/digitone_tribe64
./iniciar_hdmi.sh --check-display
./iniciar_hdmi.sh --arduino /dev/ttyACM0
```

La comprobación solo busca y abre brevemente la conexión gráfica, sin audio, GPIO ni cambios de patrones. Debe informar de una pantalla local, por ejemplo `HDMI: pantalla local :0 detectada`.

El arranque normal recompila automáticamente cuando el motor falta, pertenece a otra arquitectura o hay fuentes más recientes. Una compilación fallida conserva el binario anterior. Los lanzadores usan su propia carpeta, aunque se invoquen desde otra ubicación:

```bash
~/digitone_tribe64/iniciar_hdmi.sh --arduino /dev/ttyACM0
```

Para arrancar solo el motor, con sus controles físicos y sin visor:

```bash
~/digitone_tribe64/iniciar_tribe64.sh --arduino /dev/ttyACM0
```

## HDMI mínimo sin escritorio

El visor puede ejecutarse como única aplicación de Xorg, sin instalar un escritorio ni un gestor de ventanas. Instala desde SSH:

```bash
sudo apt update
sudo apt install --no-install-recommends xserver-xorg-core xserver-xorg-input-libinput xinit xauth python3-tk fonts-dejavu-core
```

Después, desde la consola del teclado conectado a la Raspberry, inicia sesión como `sk` y ejecuta, sin sudo:

```bash
cd ~/digitone_tribe64
./iniciar_kiosco.sh --arduino /dev/ttyACM0
```

El lanzador abre Xorg con el visor a la resolución del HDMI. Desactiva el apagado por inactividad porque los controles físicos no generan actividad de teclado o ratón. El visor sigue siendo exclusivamente visual. Al terminar el visor, xinit cierra esa sesión gráfica. Este comando no configura el arranque automático.

La primera sesión Xorg se inicia desde la consola local por los permisos de acceso a la pantalla. Una vez iniciada, el detector de `iniciar_hdmi.sh` permite conectarse a esa sesión desde SSH con el mismo usuario. Si ya hay un escritorio activo, ejecuta directamente `iniciar_hdmi.sh`; en escritorios Wayland instala también `xwayland` si falta. No necesitas cambiar el escritorio ni la configuración de arranque.

Debian documenta [Xorg](https://wiki.debian.org/Xorg) y [xinit](https://packages.debian.org/trixie/xinit), que permite iniciar una sesión con una sola aplicación.

La selección manual es opcional:

```bash
./iniciar_hdmi.sh --check-display --display :1
./iniciar_hdmi.sh --display :1 --arduino /dev/ttyACM0
```

## Qué significaban los errores

| Mensaje | Causa y solución |
|---|---|
| `No makefile found` / `No rule to make target clean` | `make` se ejecutó fuera de la carpeta del proyecto, o falta el Makefile. Usa `cd ~/digitone_tribe64` y copia el fork completo. |
| `./iniciar_hdmi.sh: No such file or directory` | El comando se ejecutó desde otra carpeta. Usa la ruta completa al lanzador. |
| `Exec format error` | El ejecutable copiado tiene una arquitectura incompatible. El nuevo lanzador lo detecta y recompila en la Raspberry. |
| `no display name and no $DISPLAY` | La terminal SSH no heredó el entorno gráfico. El nuevo visor busca la sesión local y su autorización. |
| `No se encontró ninguna sesión gráfica local` | No hay un escritorio X11/XWayland accesible. Inicia/configura la sesión gráfica local. |
| `Falta .../Makefile` | Se han copiado archivos incompletos; vuelve a copiar el fork, no solo los ejecutables. |

La detección consulta el entorno de la sesión de usuario, los procesos gráficos del mismo usuario, las sesiones locales y los sockets X11. Prueba las autorizaciones disponibles sin imprimir sus datos. La documentación de [Labwc](https://labwc.github.io/labwc.1.html) explica cómo comparte las variables gráficas con systemd; [X.Org](https://www.x.org/guide/communication/) describe la autorización Xauthority; Raspberry Pi documenta [Wayland y la configuración del escritorio](https://www.raspberrypi.com/documentation/computers/configuration.html).

## Verificación local

```bash
python3 tests/test_startup.py
python3 tests/smoke_ssh_startup.py --xvfb /ruta/a/Xvfb
```

La segunda prueba es opcional y necesita Xvfb y xauth. Crea una sesión virtual protegida, elimina el DISPLAY heredado o simula uno reenviado por SSH y verifica la detección y autorización reales. No cambia la configuración del escritorio ni utiliza GPIO.
