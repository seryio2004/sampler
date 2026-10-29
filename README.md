# Sampler 3×3

Mi idea con este proyecto es tener una pequeña máquina para hacer ritmos con las manos: pulsar un pad, escribir unos pasos, añadir un bajo y jugar con los efectos. Una Raspberry Pi, botones, potenciómetros y una pantalla pequeña. El ordenador sirve para preparar y arrancar el programa; después, el montaje tiene sus propios controles para tocar.

Aquí están tanto la primera demo como la versión en C y una variante con secuencias más largas. Para empezar con el sampler de 16 pasos, lo que hay que compilar y ejecutar es **`sampler-c`**, desde la raíz de este repositorio.

## De la primera demo a la versión actual

**Los archivos `sampler.py`, `audio.py`, `engine.py` y `hardware.py` de la raíz son de la primera demo en Python.** Los conservo como parte del recorrido del proyecto: ahí está la primera forma de probar los pads, el secuenciador, el bajo y los LED. Sus controles y sus funciones corresponden a aquella demo.

El programa actual está en [`src/`](src/) y está escrito en C. Las instrucciones de este README se refieren a él. Para usarlo no hace falta arrancar `sampler.py`.

Hay otros Python con tareas distintas: los de `scripts/` sirven para generar sonidos o hacer diagnósticos y los de `tests/` comprueban el funcionamiento. En [`digitone_tribe64/`](digitone_tribe64/README.md) está Tribe64, una variante con patrones de 64 pasos, escenas, canción y un visor HDMI que también usa Python. Esa carpeta tiene sus propios lanzadores y su propia guía.

## Qué se puede hacer

El sampler combina nueve pistas de muestras, un secuenciador de 16 pasos y un bajo sintetizado. Permite cargar WAV y MP3, escucharlos antes de asignarlos y tocar encima de un patrón. Los sonidos de batería sintetizados permiten empezar aunque todavía no hayas preparado tus propios archivos.

Hay dos posiciones de efectos en serie, con filtros, distorsión, bitcrush, eco, stutter, tremolo, ring mod, chorus y flanger. También hay efectos momentáneos para intervenir mientras suena el ritmo: repeticiones, reverse, tape stop, glitch, roll, slicer y dub echo.

Con Arduino conectado, los nueve botones de la cuadrícula son pads de sonido. Sin él, los tres de arriba sirven para navegar y quedan seis pads para tocar. El LCD1602 muestra lo que estás editando y la matriz de 16 LED permite seguir los pasos sin mirar una pantalla grande.

## El montaje

El montaje de referencia usa una Raspberry Pi 3B con Raspberry Pi OS, nueve pulsadores, un LCD1602 compatible con HD44780 conectado en paralelo y una matriz de LED de 4×4. Para los tres potenciómetros y los botones OK y FN utilizo un Arduino UNO R3 o compatible, conectado por USB a la Raspberry.

Una vez instaladas las dependencias y copiados los sonidos, el sampler puede funcionar sin conexión a Internet.

### Los nueve botones

Todos los números de GPIO de este documento usan **numeración BCM**. Cada pulsador va entre su GPIO y GND común; el programa utiliza las resistencias pull-up internas.

| Posición | Izquierda | Centro | Derecha |
|---|---|---|---|
| Superior | GPIO 5 · pad 7 | GPIO 6 · pad 8 | GPIO 12 · pad 9 |
| Central | GPIO 13 · pad 1 | GPIO 16 · pad 2 | GPIO 19 · pad 3 |
| Inferior | GPIO 20 · pad 4 | GPIO 21 · pad 5 | GPIO 26 · pad 6 |

Sin Arduino, la fila superior pasa a ser **IZQ / OK / DER**, en ese orden. GPIO 21 es el pin físico 40 de la Raspberry.

### Pantalla y LED

El LCD usa **RS=17, E=27, D4=22, D5=23, D6=24 y D7=25**, todos GPIO BCM. RW va a GND. La alimentación, el contraste y la retroiluminación deben ajustarse al módulo que montes; los GPIO de la Raspberry trabajan a 3,3 V.

| Matriz LED | GPIO BCM | Pin físico |
|---|---:|---:|
| Fila 1, ánodos | 7 | 26 |
| Fila 2, ánodos | 4 | 7 |
| Fila 3, ánodos | 8 | 24 |
| Fila 4, ánodos | 9 | 21 |
| Columna 1, cátodos | 10 | 19 |
| Columna 2, cátodos | 11 | 23 |
| Columna 3, cátodos | 18 | 12 |
| Columna 4, cátodos | 15 | 10 |

Las filas se cuentan desde arriba. Los pasos empiezan abajo a la izquierda y suben por columnas:

```text
 04  08  12  16
 03  07  11  15
 02  06  10  14
 01  05  09  13
```

Los ánodos van a las filas y los cátodos a las columnas, con resistencias limitadoras de corriente. Como punto de partida para las pruebas, el montaje contempla 1 kΩ en serie con cada columna. El barrido activa una fila cada vez. Estos GPIO deben estar libres de otros usos, como SPI o UART.

En PATRONES y BAJO, los LED distinguen los pasos escritos, el cursor que parpadea y la posición de reproducción. En las demás pantallas muestran actividad de las voces.

### Arduino y potenciómetros

Carga el sketch [`arduino/sampler_pot/sampler_pot.ino`](arduino/sampler_pot/sampler_pot.ino) desde Arduino IDE, seleccionando Arduino Uno. No necesita bibliotecas adicionales.

| Control | Conexión |
|---|---|
| NAV | Terminal central del potenciómetro a A0 |
| Segundo potenciómetro | Terminal central a A1 |
| Tercer potenciómetro | Terminal central a A2 |
| OK | Pulsador entre D2 y GND |
| FN | Pulsador entre D3 y GND |

Los extremos de cada potenciómetro van a GND y 5 V del Arduino. OK y FN usan `INPUT_PULLUP`. La comunicación con la Raspberry se hace por USB: **no conectes las señales de 5 V del Arduino a los GPIO de la Raspberry**. Cierra el monitor serie de Arduino antes de arrancar el sampler.

## Compilar y arrancar

En la Raspberry, con Raspberry Pi OS y kernel Linux 5.10 o posterior para la interfaz GPIO v2, instala las dependencias:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config alsa-utils \
  libasound2-dev libsndfile1-dev libmpg123-dev \
  libjson-c-dev libsamplerate0-dev
```

Desde la carpeta del proyecto:

```bash
cd ~/sampler_3x3_led_v4
make
./sampler-c --samples ./samples --arduino /dev/ttyACM0
```

Si no usas Arduino, quita `--arduino /dev/ttyACM0`. El puerto puede variar; puedes localizarlo con `ls /dev/ttyACM* /dev/ttyUSB*`. Tu usuario necesita acceso al audio, a `/dev/gpiochip0` y, si lo usas, al puerto serie. En Raspberry Pi OS suelen intervenir los grupos `audio`, `gpio` y `dialout`.

Para salir, pulsa **Ctrl+C**. No ejecutes a la vez el sampler en C y la primera demo en Python: comparten el hardware.

## La primera vuelta

Para escuchar algo ya preparado, el kit house incluye nueve sonidos y un patrón a 124 BPM en Do menor:

```bash
./sampler-c --samples ./samples --state kits/house.json --no-save --arduino /dev/ttyACM0
```

`--no-save` permite probar sin cambiar el preset. Si quieres empezar a tocar sin un patrón escrito, usa `kits/house_live.json`.

Para hacer tu propio ritmo:

1. Mantén **OK unos 0,65 segundos** para abrir el menú. Gira NAV hasta **PATRONES**, suelta OK y pulsa corto para entrar.
2. Elige el sonido con A1 y el paso con NAV. OK corto pone o quita un golpe.
3. Prueba un bombo en los pasos **1, 5, 9 y 13**. Cambia a una caja y escribe golpes en **5 y 13**. Lo que hayas puesto en la otra pista se conserva.
4. Entra en **TEMPO** desde el menú. OK corto cambia entre reproducción y parada; NAV ajusta el tempo.
5. Vuelve a PATRONES para seguir editando mientras escuchas, o entra en DIRECTO para tocar encima con el acompañamiento activado.

Sin Arduino, IZQ/DER permiten navegar y el botón superior central hace de OK. La [guía de uso](GUIA_DE_USO.md) explica también las pulsaciones largas y la edición sin potenciómetros.

## Moverse por el instrumento

OK largo abre el selector de menú; NAV elige y OK corto entra. Mantener OK otra vez dentro del selector lo cierra.

| Pantalla | NAV / A0 | A1 | A2 | OK corto |
|---|---|---|---|---|
| PATRONES | Paso 1–16 | Pista de muestra | Nota de bajo | Poner o quitar golpe |
| BAJO | Paso 1–16 | Pista de muestra | Nota de bajo | Poner o quitar nota |
| EFECTOS | Elegir tipo | Parámetro A | Parámetro B | Confirmar tipo o activar/desactivar |
| TEMPO | Tempo | — | — | PLAY/STOP |
| DIRECTO | Volumen | — | — | Acompañamiento ON/OFF |
| SAMPLES | Archivo o carpeta | Pad de destino | — | Entrar o cargar |

Los selectores esperan un giro deliberado al cambiar de pantalla. Para volumen, tempo y parámetros de efectos hay recogida: si aparece una flecha, gira hacia ese lado hasta alcanzar el valor guardado. Así el sonido no pega un salto por la posición en la que se haya quedado el mando.

En BAJO, A2 elige entre C, D, E, G, A y C de la octava siguiente. **FN + A2** cambia la octava entre 1 y 4. Los seis pads inferiores permiten tocar esas notas y el secuenciador las reproduce junto a las muestras.

### Cargar tus sonidos

Copia WAV o MP3 dentro de `samples/`, en subcarpetas si te resulta más cómodo. También puedes indicar otra carpeta con `--samples`, incluida una unidad USB ya montada.

En **SAMPLES**, NAV elige un archivo y **FN corto** inicia o detiene la preescucha. A1 elige el pad de destino; también puedes pulsar ese pad para seleccionarlo y escuchar el sonido que ya tiene. **OK corto carga el archivo**. Para volver a la carpeta anterior, elige `.. / VOLVER` o mantén FN.

Sin Arduino, IZQ/DER navegan, IZQ largo vuelve y DER largo controla la preescucha.

Se admiten muestras de hasta 30 segundos, convertidas internamente a mono a 44.100 Hz. Los archivos originales se conservan.

Además del [kit house](samples/house/README.md), están el [banco TR-808](samples/tr808/README.md), con 16 WAV distribuidos con licencia CC0, y los [sonidos raros](samples/raros/README.md), con 12 texturas experimentales. Sus presets están en `kits/`.

### Jugar con los efectos

La mezcla pasa por **FX1 → FX2 → efecto momentáneo → volumen y limitador**. En EFECTOS, NAV elige un tipo, pero hay que pulsar OK para aplicarlo. Mientras aparece el asterisco de selección pendiente sigue sonando el efecto anterior. Después de confirmar, A1 y A2 ajustan sus parámetros y OK permite activarlo o desactivarlo.

**FN + OK corto** alterna entre FX1 y FX2. Puedes dejar, por ejemplo, un filtro en la primera posición y un eco en la segunda; ambos siguen actuando cuando vuelves a editar el patrón.

Con Arduino, dentro de EFECTOS, mantén FN y un pad para aplicar un efecto momentáneo:

| Pad | Efecto |
|---|---|
| 1 / 2 / 3 | Repetición 1/8, 1/16 y 1/32 |
| 4 / 5 / 6 | Reverse, tape stop y glitch |
| 7 / 8 / 9 | Roll 1/4, beat slicer y dub echo |

Al soltar FN o todos los pads, el efecto momentáneo se libera. Sin Arduino, los seis pads inferiores tienen sus efectos momentáneos directamente en esta pantalla.

## Guardar lo que vas haciendo

Para trabajar a partir de un kit, prefiero hacer una copia y usarla como archivo de trabajo:

```bash
cp kits/house.json kits/mi_house.json
./sampler-c --samples ./samples --state kits/mi_house.json --arduino /dev/ttyACM0
```

Sin `--no-save`, el programa guarda automáticamente los patrones, las asignaciones de muestras, el tempo, el volumen, la octava, el acompañamiento y los efectos confirmados. También guarda al cerrar con Ctrl+C.

Si no indicas `--state`, utiliza `~/.sampler3x3/patrones_v4.json`. Los presets incluidos usan rutas relativas a la raíz del proyecto, así que arranca desde esa carpeta. Guarda una copia de tus JSON y de tus muestras si quieres conservar un tema completo.

## Si algo no responde

Para elegir salida de audio, consulta `aplay -L` y pasa el nombre con `--device`. Por ejemplo, si tu sistema ofrece esa salida:

```bash
./sampler-c --samples ./samples --device plughw:CARD=Headphones,DEV=0
```

Para comprobar el orden de los LED, cierra el sampler y ejecuta `./sampler-c --test-leds`. Para aislar problemas del montaje puedes arrancar con `--no-leds`, `--no-lcd` o `--headless`; este último funciona sin GPIO y conserva el audio.

Si un efecto no cambia al girar NAV, comprueba si falta confirmarlo con OK. Si un mando no responde al entrar, haz un giro deliberado o sigue la flecha de recogida. Si la fila superior vuelve a navegar, revisa la conexión USB y el puerto del Arduino.

El ruido de audio tiene su propio [documento de diagnóstico](docs/diagnostico_audio.md), con pruebas para comparar la salida del sistema y la del motor. El resto de opciones se consulta con `./sampler-c --help`.

## Para seguir mirando por dentro

- [`GUIA_DE_USO.md`](GUIA_DE_USO.md): controles completos, preescucha, efectos y comandos de terminal.
- [`src/`](src/): motor de audio, secuenciador, archivos, controles y hardware de la versión C.
- [`arduino/`](arduino/): sketch de los potenciómetros y botones externos.
- [`samples/`](samples/) y [`kits/`](kits/): bancos de sonidos y presets para empezar.
- [`tests/`](tests/): pruebas del motor y de los controles; `make test` ejecuta la batería y requiere Python 3 para las pruebas de integración.
- [`digitone_tribe64/README.md`](digitone_tribe64/README.md): instrucciones de la variante de 64 pasos y del visor HDMI.

Las pruebas con hardware simulado ayudan a revisar el programa. El sonido, el cableado y la respuesta al tocar se comprueban en el montaje físico.
