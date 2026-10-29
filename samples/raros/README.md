# Raros: sonidos experimentales

12 muestras sintetizadas para este proyecto, sin grabaciones externas ni sonidos de batería. Texturas, drones, voces artificiales y señales extrañas; algunas contienen ruido y disonancia intencionados.

WAV PCM de 16 bits, mono, 44.100 Hz. Duraciones entre 3,2 y 6,4 segundos. Pico limitado al 78 % (aprox. −2,2 dBFS), eliminación de continua y fundidos de entrada/salida. Se distribuyen con [CC0 1.0](LICENSE-CC0.txt); puedes usarlas y modificarlas. El [manifest](manifest.json) incluye descripción y SHA-256 de cada WAV.

| Archivo | Duración | Sonido |
|---|---|---|
| 01_radio_alien.wav | 3.8 s | Radio alien: barrido FM y balizas que se cruzan. |
| 02_ballena_espacial.wav | 5.4 s | Ballena espacial: lamento grave que cae y vuelve a subir. |
| 03_robot_roto.wav | 3.2 s | Robot roto: vocales digitales y cambios de tono escalonados. |
| 04_cristales.wav | 4.6 s | Cristales suspendidos: parciales inarmonicos que flotan. |
| 05_nave_abandonada.wav | 6.4 s | Nave abandonada: drone grave con batidos y respiracion. |
| 06_enjambre.wav | 4.2 s | Enjambre: nubes de microtonos que orbitan. |
| 07_laser_derretido.wav | 3.7 s | Laser derretido: caida continua y modulacion viscosa. |
| 08_fantasmas_cinta.wav | 5.6 s | Fantasmas de cinta: coro sintetico con desafinacion lenta. |
| 09_portal_metal.wav | 4.8 s | Portal metalico: resonancias que se abren y se retuercen. |
| 10_sueno_maquina.wav | 4.4 s | Sueno de maquina: arpegio espectral difuso. |
| 11_agua_digital.wav | 4.9 s | Agua digital: burbujas tonales y corriente filtrada. |
| 12_voz_inversa.wav | 5.2 s | Voz inversa: formantes sinteticos que crecen hacia atras. |

## Probar los nueve primeros sonidos

Desde la raíz del proyecto:

```bash
./sampler-c --samples ./samples --state kits/raros.json --no-save --arduino /dev/ttyACM0
```

Los seis pads inferiores usan las muestras 1–6; los tres superiores, de izquierda a derecha, 7–9. El preset desactiva efectos y deja vacíos los patrones de batería y bajo para tocar las texturas sin acompañamiento. `--no-save` conserva el preset. Las rutas son relativas a la raíz del proyecto.

Sin Arduino, omite `--arduino /dev/ttyACM0`: los seis inferiores tocan las muestras 1–6 y la fila superior navega. Para cambiar cualquier muestra, entra en FILES, abre `raros`, elige el pad de destino y confirma el WAV con OK. Las muestras 10–12 están disponibles desde FILES.

Con el programa cerrado puedes escuchar una muestra directamente:

```bash
aplay samples/raros/02_ballena_espacial.wav
```

Para conservar tus asignaciones, copia `kits/raros.json` a otro archivo y usa esa copia con `--state` sin `--no-save`.

## Regenerar

No hace falta regenerarlas para usar el kit: los WAV ya están incluidos. El generador usa solo la biblioteca estándar de Python 3:

```bash
python3 scripts/generar_raros.py
```

También admite `--output /ruta/a/otra/carpeta`. Las semillas aleatorias son fijas; los hashes pueden variar ligeramente entre plataformas por sus implementaciones matemáticas.
