#!/bin/sh
set -eu
sampler_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$sampler_root"
case "${DISPLAY:-}" in
    :[0-9]*|unix:[0-9]*|unix/:[0-9]*)
        exec "$sampler_root/iniciar_hdmi.sh" "$@"
        ;;
esac
if ! command -v startx >/dev/null 2>&1; then
    echo 'Falta startx. Instala: sudo apt install --no-install-recommends xserver-xorg-core xserver-xorg-input-libinput xinit xauth python3-tk fonts-dejavu-core' >&2
    exit 1
fi
sampler_tty=$(tty 2>/dev/null || true)
case "$sampler_tty" in
    /dev/tty[0-9]*) ;;
    *)
        echo 'Para crear la sesión HDMI mínima, ejecuta iniciar_kiosco.sh desde la consola local con el teclado de la Raspberry.' >&2
        echo 'Con una sesión gráfica ya arrancada puedes usar iniciar_hdmi.sh desde SSH.' >&2
        exit 1
        ;;
esac
# A single client: no desktop, panel, terminal or window manager.
# GPIO activity does not reset X11 idle time, so disable blanking and DPMS.
unset DISPLAY
exec startx "$sampler_root/iniciar_hdmi.sh" "$@" -- -nolisten tcp -s 0 -dpms
