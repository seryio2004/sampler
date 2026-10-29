# Investigación del ruido en la Raspberry Pi 3

El síntoma comunicado es una capa de ruido sobre todos los sonidos, usando el **jack de 3,5 mm**. El usuario ha confirmado que **con `aplay` el ruido no aparece** y que al probar sin GPIO hay menos ruido, pero quedan chasquidos. Esto indica que hay que separar actividad del montaje y continuidad del audio. No se ha podido entrar desde este entorno por SSH: el nombre `sampler` no se resuelve. No hay todavía una única causa confirmada para todo el síntoma.

## Revisión implementada para limpiar el audio

El antiguo normalizador solo ajustaba los sonidos sintetizados una vez al arrancar; no había un normalizador por bloques en la reproducción. Sin embargo, sí existían cortes instantáneos de voces y recorte brusco al superar el nivel máximo, que pueden causar clics o distorsión. Se han corregido ambos mecanismos:

- Frecuencia interna y salida a **44.100 Hz**. Los WAV del kit conservan su frecuencia; para otras frecuencias se usa libsamplerate con `SRC_SINC_BEST_QUALITY`, fuera del hilo de audio. MP3 se decodifica a float a su frecuencia nativa antes de convertirlo.
- Ninguna fuente se amplifica automáticamente. Los sintetizadores solo atenúan si exceden su margen; un archivo con picos fuera de rango recibe una atenuación fija, no un recorte de sus muestras ni normalización por bloques.
- Entrada de voces de unos 0,5 ms, salida de 2 ms y colas de 3 ms al sustituir o robar una voz, incluido el bajo monofónico. El volumen cambia con una rampa suave de unos 5 ms.
- Limitador de mezcla con 132 frames de anticipación (2,99 ms), ataque suavizado y liberación de unos 50 ms. No aumenta señales débiles. El recorte final queda como protección y se cuenta por separado de la reducción de ganancia.
- Buffer ALSA de **60 ms por defecto**, bloques acordes al periodo negociado y fundido de 5 ms tras una recuperación de `xrun`. Un fundido no recupera los frames perdidos por el dispositivo.
- Menos consultas de la vista previa bajo el mutex de audio, mutex con herencia de prioridad cuando está disponible y menor prioridad de CPU para los hilos de carga. Ninguna de estas medidas garantiza tiempo real en un sistema sobrecargado.

Los efectos BITCRUSH, DIST y GLITCH conservan su distorsión deliberada. Usa el preset con los efectos apagados para evaluar limpieza de la reproducción.

Tras actualizar el código, instala la dependencia nueva y prueba el preset sin GPIO:

```bash
cd ~/sampler_3x3_led_v4
sudo apt install libsamplerate0-dev
make clean
make
./sampler-c --state kits/tr808.json --no-save --headless --seconds 10
```

Si suena limpio, compara el mismo preset normal y con `--no-leds`/`--no-lcd` por separado. Si conserva el ruido, compara el buffer de 100 ms y la exportación digital reproducida con `aplay`. Un error `./sampler-c: No such file or directory` no constituye una prueba de audio: hay que comprobar la carpeta actual y compilar el ejecutable en la Raspberry.

## Qué se ha comprobado en el código

| Prueba | Resultado |
|---|---|
| Motor sin voces, y después de terminar una nota | Todas las muestras PCM valen exactamente cero |
| Seno de 1 kHz pasando por una voz sin FX | Diferencia de como máximo 1 unidad de PCM de 16 bits respecto a la ganancia esperada |
| Render de 20 segundos del preset TR-808 a 44,1 kHz | Pico de mezcla: 0,385969; 0 frames recortados de 882.000; sin reducción del limitador |
| Exportación WAV frente a reproducción con ALSA `file/null` | Los bytes de audio coinciden exactamente durante el intervalo comparado |
| 32 voces artificialmente superpuestas | Se detecta el exceso y actúa el limitador, sin recorte final |
| Sustitución de una voz constante y cambios de volumen | Las pruebas limitan el salto entre muestras, frente a la interrupción instantánea anterior |
| Escrituras ALSA parciales e inyección de EAGAIN, EINTR y EPIPE | Continúa la reproducción; se verifica el fundido exacto de recuperación y se conserva el resto del PCM |
| Remuestreo de un tono float de 1 kHz, 48 → 44,1 kHz | SNR calculada de 145,5 dB en el tramo central del ensayo |
| Tono de 23 kHz a 48 kHz, convertido a 44,1 kHz | Residuo RMS cercano a 0,00000001 en el ensayo de rechazo de alias |

Las cifras de remuestreo describen señales float de prueba y no la relación señal/ruido del jack ni de los WAV de 16 bits. El ensayo de la voz de 1 kHz incluye los nuevos fundidos y el retraso del limitador; la señal fuera de esos bordes conserva el nivel esperado dentro de la cuantización de 16 bits.

Estas pruebas reducen la sospecha de ruido añadido por la mezcla, un error de formato/intercalado o memoria sin inicializar en la ruta comprobada. **No validan** el driver físico de la Pi, el comportamiento bajo carga real, el firmware de audio ni la etapa analógica. Una prueba con ALSA `null` no mide latencia ni estabilidad en una Raspberry.

La salida del programa es PCM estéreo S16_LE, **44.100 Hz**, con la misma señal en ambos canales. El código no configura PWM por hardware: solicita líneas GPIO digitales mediante la API del kernel. Tener GPIO18 entre las columnas de la matriz no demuestra por sí solo un conflicto con el generador PWM de audio.

## Hipótesis y cómo separarlas

| Posible causa | Qué encaja con ella | Prueba que la diferencia |
|---|---|---|
| Ruido de la salida analógica, incluido dither | Siseo sobre sonidos de cualquier procedencia; más evidente con niveles bajos y amplificación posterior | Tono y silencio reproducidos con `aplay`, sin el sampler ejecutándose |
| Acoplamiento de alimentación/masa por LED, LCD u otros circuitos | Zumbido que cambia con los indicadores o con la actividad de la placa | Comparar el mismo preset normal, con `--no-leds`, con `--no-lcd` y con `--headless` |
| Interrupciones del flujo ALSA bajo carga | Chasquidos, cortes y contador de `xruns` mayor que cero | Comparar el valor actual de 60 ms con `--latency-ms 100`, sin cambiar las otras condiciones |
| Ruta ALSA, mezclador o conversión de frecuencia | Diferencias entre aplicaciones o frecuencias | Usar el mismo dispositivo explícito; comparar tonos a 22.050, 44.100 y 48.000 Hz |
| Recorte de la mezcla | Distorsión ligada a golpes simultáneos o repeticiones intensas | Consultar el pico digital y el contador de frames recortados al cerrar |
| Overlay PWM/I²S o audio remapeado | Conflictos de pines o periféricos activos | Consultar el informe de GPIO y los overlays efectivos/configurados |

La matriz actual conmuta una fila cada 1 ms: barrido completo a 250 Hz y alternancia de la capa tenue a 125 Hz. Estas frecuencias hacen razonable investigar acoplamiento eléctrico si el ruido cambia al quitar el barrido. Es una hipótesis basada en el circuito y el código, no una medición de ruido.

Los pines usados por los botones, el LCD y los LED también incluyen GPIO18–21. Un DAC I²S requiere revisar el cableado y liberar sus líneas; no debe recomendarse como sustitución directa conservando esos GPIO ocupados. Para el jack, hay que comprobar si existen overlays como `audremap`, `pwm` o `pwm-2chan` antes de atribuir el problema a un conflicto de PWM.

## Pruebas en la Raspberry

Actualiza el proyecto, cierra cualquier sampler anterior y compila:

```bash
cd ~/sampler_3x3_led_v4
make clean
make
```

### 1. Separar el programa de la salida

Con el sampler cerrado:

```bash
aplay samples/tr808/01_bombo.wav
```

Si también aparece el mismo ruido, el problema ya se reproduce fuera del programa. Si suena limpio, continúa comparando las condiciones siguientes: la diferencia de frecuencia o ruta ALSA también puede influir, de modo que no basta para acusar o descartar todo el hardware.

Genera referencias de tono, silencio y una exportación del motor, junto con un informe del sistema:

```bash
python3 scripts/diagnostico_audio.py --output /tmp/sampler-audio
```

Este comando no reproduce audio, no modifica el mezclador ni los archivos de arranque y no cambia modos GPIO. Crea:

- `informe.json`: modelo, kernel, dispositivos ALSA, niveles del mezclador, configuración relevante, estado GPIO, temperatura e indicadores de throttling, cuando las herramientas estén disponibles.
- `silencio_22050.wav` y `silencio_44100.wav`: cuatro segundos de ceros PCM, a la frecuencia anterior y a la actual.
- `tono_22050.wav`, `tono_44100.wav`, `tono_48000.wav`: tonos estéreo de 1 kHz a −18 dBFS, con fundidos de entrada y salida.
- `tr808_motor.wav`: ocho segundos del motor C, exportados sin abrir ALSA, Arduino ni GPIO.

Escucha los archivos uno a uno, siempre por la misma salida:

```bash
aplay /tmp/sampler-audio/tono_44100.wav
aplay /tmp/sampler-audio/silencio_44100.wav
aplay /tmp/sampler-audio/tono_22050.wav
aplay /tmp/sampler-audio/tr808_motor.wav
```

Si solo el tono de 22.050 Hz se ve afectado, investiga el driver/conversión de frecuencia. Si todos los tonos tienen la misma capa de ruido, la sospecha se desplaza hacia la ruta de audio y la salida analógica. El silencio exacto permite buscar ruido de fondo, aunque el firmware puede silenciar automáticamente ciertos intervalos y en ese caso un silencio limpio no descarta ruido durante los tonos.

`aplay -L` enumera los nombres ALSA. Si utilizas un dispositivo explícito, aplica el mismo nombre a ambos programas, por ejemplo `aplay -D NOMBRE_ALSA archivo.wav` y `sampler-c --device NOMBRE_ALSA`. No presupongas que tu tarjeta se llama `Headphones`: compruébalo primero.

### 2. Aislar GPIO y buffer sin cambiar el sonido

Ejecuta las variantes por separado, cerrando con Ctrl+C entre ellas. Usa el mismo preset y la misma salida en todos los casos:

```bash
# Referencia actual: LCD y LED activos; buffer de 60 ms.
./sampler-c --state kits/tr808.json --no-save --seconds 15

# Solo quitar la matriz LED; LCD y botones siguen activos.
./sampler-c --state kits/tr808.json --no-save --seconds 15 --no-leds

# Solo quitar las escrituras al LCD; LED y botones siguen activos.
./sampler-c --state kits/tr808.json --no-save --seconds 15 --no-lcd

# No solicitar ningún GPIO.
./sampler-c --state kits/tr808.json --no-save --seconds 15 --headless

# Solo aumentar el buffer, conservando LCD y LED.
./sampler-c --state kits/tr808.json --no-save --seconds 15 --latency-ms 100 --audio-info
```

No conectes Arduino mediante `--arduino` durante esta primera comparación. Si sospechas alimentación o masa compartida, prueba también con el USB del UNO desconectado físicamente, con el programa cerrado antes de hacerlo. Desactivar software no corta la alimentación de los circuitos externos.

Al cerrar, anota las dos líneas nuevas:

```text
Recuperaciones ALSA (xruns): ...
Audio digital: pico antes del recorte ...; frames recortados .../...
```

Un `xrun` indica una interrupción del flujo; no mide siseo analógico. Un contador de recortes en cero tampoco demuestra que la salida analógica esté limpia. Se muestran también los frames con reducción del limitador, el exceso de nivel anterior a la protección y los tiempos máximos de render y espera del mutex. Si mejora al aumentar el buffer y disminuyen los `xruns`, el siguiente trabajo es ajustar planificación/carga. El valor por defecto es ahora 60 ms; `--latency-ms` admite 10–500 ms. Para comparar con el buffer anterior puedes indicar `--latency-ms 23`, pero el nuevo motor sigue trabajando a 44,1 kHz.

### 3. Si el ruido aparece también con aplay por el jack

Este apartado no es la primera intervención en el caso actual: el usuario ha confirmado que `aplay` suena limpio. Primero se deben comparar el motor revisado, los GPIO y la continuidad ALSA.

La documentación oficial describe siseo por dither en determinadas condiciones, especialmente con volumen ALSA bajo. Primero inspecciona los niveles con `alsamixer`; antes de subirlos, baja la ganancia del amplificador o altavoz externo. Una señal digital/ALSA muy atenuada y mucha ganancia posterior hacen más visible el ruido de la salida.

Si las pruebas apuntan a ese siseo, una prueba reversible es establecer `disable_audio_dither=1` en el archivo de arranque que utiliza tu sistema (`/boot/firmware/config.txt` en sistemas actuales, `/boot/config.txt` en otros). Guarda una copia antes, revisa la sección aplicable y evita opciones duplicadas contradictorias. Requiere reiniciar. Puede reducir el siseo de dither y cambiar el comportamiento de cuantización a niveles bajos; no es una corrección general de zumbidos ni de interrupciones ALSA.

`audio_pwm_mode=2` ya es el modo de calidad predeterminado según la documentación actual. Solo tendría sentido corregir un modo legado si el informe muestra que se ha forzado `audio_pwm_mode=1`. No reduzcas `pwm_sample_bits` ni actives overlays PWM como supuesto arreglo del ruido.

Si la prueba sin LED mejora el audio, revisa alimentación, retornos de masa y desacoplo del montaje. Si no mejora, el resultado evita cambiar a ciegas la matriz o su frecuencia. El informe incluye `vcgencmd get_throttled`; sus indicadores históricos no demuestran que la alimentación esté fallando justo durante la reproducción.

## Fuentes técnicas

- [Raspberry Pi: audio_pwm_mode y disable_audio_dither](https://www.raspberrypi.com/documentation/computers/config_txt.html#audio_pwm_mode).
- [Raspberry Pi: overlays audremap, pwm y pwm-2chan](https://github.com/raspberrypi/firmware/blob/master/boot/overlays/README).
- [ALSA: flujo PCM, formatos, parámetros y underruns](https://alsa-project.org/alsa-doc/alsa-lib/pcm.html).
- [libsamplerate: conversión completa en memoria](https://libsndfile.github.io/libsamplerate/api_simple.html) y [conversores sinc](https://libsndfile.github.io/libsamplerate/api_misc.html#converters).
- [Raspberry Pi: explicación del PWM y del acoplamiento a la salida analógica por un ingeniero de Raspberry Pi](https://forums.raspberrypi.com/viewtopic.php?t=73062). Es una explicación histórica del mecanismo; no una medición de este montaje.

Para continuar con los chasquidos que pudieran quedar, hacen falta el resultado del motor actualizado, la variante GPIO con la que cambia el ruido, los nuevos contadores al cerrar y el `informe.json` generado en la Pi.
