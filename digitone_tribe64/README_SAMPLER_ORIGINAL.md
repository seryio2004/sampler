# Sampler 3×3 en C para Raspberry Pi 3 + gestor WAV/MP3

La [guía de uso del Sampler v5](GUIA_DE_USO.md) reúne los controles, los kits y los pasos para crear patrones y configurar efectos.

La nueva aplicación está en [`src/`](src/) y se compila como `sampler-c`. Ofrece seis pads con tres controles superiores, o nueve pads de sonido cuando Arduino envía `CTRL`, junto con el secuenciador de 16 pasos, el bajo, los efectos, el LCD1602 y la matriz LED con **el mismo cableado BCM** descrito más abajo. Los archivos Python se conservan como referencia y alternativa.

## Instalar y arrancar la versión C

En la Raspberry Pi 3, con Raspberry Pi OS y kernel Linux **5.10 o posterior** (interfaz GPIO v2):

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libasound2-dev libsndfile1-dev libmpg123-dev libjson-c-dev libsamplerate0-dev alsa-utils
cd ~/sampler_3x3_led_v4
make
./sampler-c --samples ./samples
```

Copia tus archivos WAV/MP3 dentro de `samples/`, organizados en subcarpetas si quieres. También puedes elegir otra carpeta, incluida una unidad USB ya montada:

```bash
./sampler-c --samples /media/tu_usuario/USB/kits
```

Ya se incluyen **16 WAV TR-808 gratuitos con licencia CC0** en [`samples/tr808/`](samples/tr808/README.md): bombos, cajas, charles, palmas, toms y percusión. Para probar los primeros seis asignados a los pads, sin efectos y sin cambiar tus patrones guardados, ejecuta desde la raíz del proyecto:

```bash
./sampler-c --samples ./samples --state kits/tr808.json --no-save
```

Añade `--arduino /dev/ttyACM0` si tienes conectado el UNO. Puedes asignar los otros sonidos desde FILES > tr808. El preset usa rutas relativas a la raíz del proyecto. Consulta el [origen, licencia y comprobación del ruido](samples/tr808/README.md) para más detalles. Las muestras nuevas no garantizan resolver un ruido que también se oiga al reproducir archivos fuera del sampler.

También se incluyen **12 samples experimentales sin batería** en [`samples/raros/`](samples/raros/README.md): radio alien, ballena espacial, robot roto, cristales, drones, enjambres y otras texturas. Son síntesis originales distribuidas con CC0. Puedes cargarlos desde FILES > raros o probar los nueve primeros con patrones vacíos:

```bash
./sampler-c --samples ./samples --state kits/raros.json --no-save --arduino /dev/ttyACM0
```

Sin Arduino, omite `--arduino /dev/ttyACM0`. Los seis pads inferiores conservan sus sonidos y la fila superior navega.

El banco [`samples/house/`](samples/house/README.md) añade **16 one-shots originales para house**, con batería, bajos afinados, acordes y efectos. El preset de nueve pads incluye un patrón a **124 BPM en Do menor**:

```bash
./sampler-c --samples ./samples --state kits/house.json --no-save --arduino /dev/ttyACM0
```

Usa `kits/house_live.json` para empezar con los mismos sonidos y los patrones vacíos. Los WAV y ambos presets están incluidos; no requieren recompilar.

`Ctrl+C` cierra el programa y guarda los cambios pendientes. El usuario necesita acceso a audio y `/dev/gpiochip0`; normalmente Raspberry Pi OS lo proporciona mediante los grupos `audio` y `gpio`. No ejecutes simultáneamente las versiones Python y C. Si un GPIO está ocupado por SPI, UART u otro servicio, hay que liberarlo antes de arrancar.

Para elegir la salida, consulta `aplay -L` y pasa su nombre ALSA, por ejemplo:

```bash
./sampler-c --device plughw:CARD=Headphones,DEV=0 --samples ./samples
```

El nombre depende de tu instalación. Se puede cambiar el chip GPIO con `--gpiochip /dev/gpiochipN`; los números de línea deben corresponder a los GPIO BCM del cableado. `./sampler-c --help` muestra las opciones.

Para comprobar el orden y el barrido de la matriz, cierra el sampler y ejecuta:

```bash
./sampler-c --test-leds
```

La prueba dura unos 20 segundos: enciende los LED 1–16 individualmente, después las filas, las columnas y los dos niveles de brillo. El terminal indica la posición esperada: empieza abajo a la izquierda y sube por cada columna. Solo abre los ocho GPIO de la matriz; no inicia audio, LCD, botones, Arduino ni guardado. `Ctrl+C` termina y apaga la matriz. El funcionamiento de los indicadores está descrito en [Matriz LED](#matriz-led-44-y-seguimiento-de-los-16-pasos).

Si aparece ruido en el jack, consulta la [investigación y pruebas de audio](docs/diagnostico_audio.md). Se incluyen `--no-leds` y `--no-lcd` para aislar el montaje, `--latency-ms 100` para comparar interrupciones con un buffer mayor, `--audio-info` para ver la ruta ALSA y `--render salida.wav --seconds 8` para exportar el motor sin abrir ALSA ni GPIO. Al cerrar se muestran `xruns`, pico digital y número de frames recortados. El diagnóstico del sistema y los tonos de referencia se generan con:

```bash
python3 scripts/diagnostico_audio.py --output /tmp/sampler-audio
```

El script guarda `informe.json`, tonos, silencio y una exportación del kit en esa carpeta. No reproduce sonido ni modifica el mezclador o la configuración del sistema. Las hipótesis y sus pruebas están explicadas en el documento; no se ha confirmado todavía la causa del ruido en tu placa.

## Controles v5: Arduino, menús y edición

Con Arduino conectado y enviando `CTRL`, hay **nueve pads de sonido**: los seis inferiores son los slots 1–6; los tres superiores, de izquierda a derecha, los slots 7–9. Sin Arduino, la fila superior vuelve a **IZQ / OK / DER** y quedan seis pads de sonido. Los botones externos del Arduino son **OK en D2** y **FN en D3**.

### Cableado y arranque

Carga [`arduino/sampler_pot/sampler_pot.ino`](arduino/sampler_pot/sampler_pot.ino) en un UNO R3/ELEGOO con Arduino IDE, placa Arduino Uno. No requiere bibliotecas adicionales. La corrección de dirección de NAV está en el sketch: `POT_INVERT[0]=false`; el sampler utiliza el valor recibido sin invertirlo otra vez. Estos cambios de menús y efectos no requieren modificar de nuevo ese sketch si ya tienes cargada la corrección.

| Control | Extremo 1 | Central | Extremo 2 |
|---|---|---|---|
| NAV | GND | A0 | 5V |
| Segundo potenciómetro | GND | A1 | 5V |
| Tercer potenciómetro | GND | A2 | 5V |
| OK | D2 | — | GND |
| FN | D3 | — | GND |

Los botones usan `INPUT_PULLUP`: pulsar conecta a GND. El USB lleva los datos del UNO a la Raspberry; las señales de 5V del Arduino no se conectan a los GPIO de la Raspberry.

```bash
./sampler-c --samples ./samples --arduino /dev/ttyACM0
```

Comprueba el puerto con `ls /dev/ttyACM* /dev/ttyUSB*`. Si falta permiso serie, añade tu usuario a `dialout` con `sudo usermod -aG dialout "$USER"` y vuelve a iniciar sesión. Cierra el monitor serie antes de arrancar el sampler.

### Menús

**Mantén OK 0,65 s para abrir el selector de menú.** NAV elige y OK corto confirma. El menú muestra PATRONES, EFECTOS, TEMPO, BAJO, DIRECTO y SAMPLES; los nombres internos son SEQ, FX, BPM, BASS, DRUM y FILES. Mantener OK dentro del selector lo cierra sin cambiar de modo. Una pulsación mantenida actúa una sola vez y soltarla no confirma nada.

Sin Arduino, IZQ/DER eligen dentro del selector y el pad superior central es OK. Abrir el selector conserva el modo y el audio hasta que confirmas el destino.

### Mapa de los potenciómetros

| Pantalla | NAV / A0 | A1 | A2 | OK corto |
|---|---|---|---|---|
| PATRONES / SEQ | Paso 1–16 | Muestra/pista 1–9 | Nota de bajo 1–6 | Añadir/quitar golpe de la pista elegida |
| BAJO / BASS | Paso 1–16 | Muestra de percusión 1–9 | Nota C, D, E, G, A, C+ | Añadir/quitar la nota elegida |
| EFECTOS / FX | Tipo pendiente de confirmar | Parámetro A confirmado | Parámetro B confirmado | Confirmar selección; después activar/desactivar |
| TEMPO / BPM | 60–180 BPM | — | — | PLAY/STOP |
| DIRECTO / DRUM | Volumen | — | — | Activar/desactivar acompañamiento |
| SAMPLES / FILES | Archivo o carpeta | Pad de destino 1–9 | — | Entrar en carpeta o cargar WAV/MP3 |

Los mandos de selección de pasos, sonidos, menús y archivos se activan **al primer giro deliberado**, con margen contra el ruido. Dejarlos quietos al cambiar de pantalla conserva la selección; ya no hay que buscar su posición anterior para navegar. Si un pad cambia la selección, el mando espera un nuevo giro.

Volumen, tempo y parámetros de efectos mantienen **recogida** para evitar saltos: gira hacia la flecha hasta alcanzar el valor guardado. En los efectos hay resolución de 10 bits, margen de tres unidades ADC y suavizado de unos 5 ms. Cambiar de posición FX exige recoger sus valores independientes. Fuera de FX, A1/A2 no cambian los efectos que siguen sonando.

### Crear patrones

En SEQ, gira A1 para elegir la muestra y NAV para elegir el paso; OK corto añade o quita el golpe. Cambiar de muestra conserva las otras pistas. El encabezado muestra el número de pad, el **nombre real abreviado del sample**, la nota de bajo seleccionada (`B1`–`B6`) y el paso. Los sonidos sintetizados conservan sus nombres de instrumento.

En BASS, gira A2 para elegir la nota y NAV para elegir el paso; OK añade o quita esa nota. **FN + A2 cambia la octava 1–4**; el LCD muestra la octava actual. Al soltar FN, A2 vuelve a seleccionar notas y espera un nuevo giro.

La segunda línea conserva siempre los **16 pasos**: `.` vacío, `X` golpe, `1`–`6` nota, `^` cursor vacío, `*` cursor ocupado y `O`/`:` reproducción. Los avisos de recogida o de carga no sustituyen el patrón. La matriz LED sigue mostrando el cursor y el cabezal con el retardo del audio compensado.

Los pads reproducen y seleccionan sonidos. En BASS, los seis inferiores tocan notas de bajo y los superiores conservan sus tres muestras de percusión.

### Dos efectos simultáneos

La cadena es **mezcla → FX1 → FX2 → efecto momentáneo → volumen/limitador**. Cada posición tiene tipo, activación y dos parámetros independientes. Puedes repetir un tipo en ambas posiciones; el orden influye en el resultado. Los presets antiguos conservan FX1 y dejan FX2 desactivado.

En FX, pulsa **FN + OK corto para alternar entre FX1 y FX2**. NAV elige un tipo pendiente, marcado con `*` y `OK PARA APLICAR`. El efecto anterior sigue sonando hasta pulsar OK corto: entonces se confirma y activa el nuevo (OFF desactiva la posición). Sin una selección pendiente, OK corto activa/desactiva solo esa posición. A1/A2 ajustan los parámetros después de confirmar; mientras hay una elección pendiente no los modifican. `FX1` o `FX2` identifica lo que editas; `+` indica que la otra posición también está activa. Por ejemplo: LOWPASS en FX1 y ECHO en FX2. Salir de FX descarta las selecciones sin confirmar; cambiar entre FX1/FX2 conserva la selección pendiente de cada posición. Solo se guardan los tipos confirmados. Los parámetros de FX2 se guardan como `fx2_choice`, `fx2_enabled`, `fx2_cutoff` y `fx2_resonance`.

Sin Arduino, IZQ/DER cortos ajustan el campo seleccionado. IZQ/DER largos recorren **FX / A / B / SLOT**; en SLOT, una pulsación corta cambia entre las dos posiciones. OK mantiene su función de activación y OK largo abre el menú.

| Índice NAV | Efecto | CUTOFF / A (A1) | RESONANCE / B (A2) |
|---|---|---|---|
| 1 | LOWPASS | Frecuencia 20–18.000 Hz, recorrido logarítmico | Resonancia Q 0,5–8 |
| 2 | HIGHPASS | Frecuencia 20–18.000 Hz, recorrido logarítmico | Resonancia Q 0,5–8 |
| 3 | BITCRUSH | Retención de 1–64 frames | Resolución de 16 a 3 bits |
| 4 | DIST | Ganancia de entrada 1–21 | Mezcla seco/procesado 0–100 % |
| 5 | ECHO | Retardo 40–600 ms | Realimentación 0–75 % |
| 6 | STUTTER | Fragmento 15–200 ms, repetido cuatro veces | Mezcla seco/procesado 0–100 % |
| 7 | TREMOLO | Velocidad 0,5–16 Hz | Profundidad 0–100 % |
| 8 | RING MOD | Portadora 20–2.000 Hz | Mezcla 0–100 % |
| 9 | CHORUS | Velocidad 0,1–5 Hz | Cantidad 0–100 % |
| 10 | FLANGER | Velocidad 0,1–5 Hz | Cantidad 0–100 % |

Con Arduino, **FN + pad inferior 1–6** activa respectivamente repetición 1/8, 1/16, 1/32, reverse, tape stop o glitch. **FN + los tres superiores**, de izquierda a derecha (pads 7–9), activa **ROLL 1/4**, **BEAT SLICER** y **DUB ECHO**. ROLL repite el último pulso a negras; SLICER lo divide en cuatro y reproduce 1–3–2–4 con fundidos breves; DUB ECHO añade repeticiones a negras con realimentación del 65 %, en un buffer independiente. La duración se calcula con el BPM al pulsar. El historial de dos segundos permite capturar un pulso completo incluso a 50 BPM, una vez que haya sonado suficiente audio.

Los nueve actúan **después de las dos posiciones FX**. El último pad mantenido tiene prioridad; al soltarlo vuelve al anterior. Soltar FN, todos los pads o salir de FX libera el efecto con fundido. Sin FN, los tres superiores siguen tocando sus muestras; fuera de FX conservan su función de sonido.

Sin Arduino, los seis pads en FX mantienen los efectos momentáneos sobre las voces originales.

La cadena reserva sus buffers al iniciar y procesa las dos posiciones sin reservar memoria ni acceder a archivos en el hilo de audio. Las pruebas cubren las 121 parejas y la independencia del tamaño de bloque. El rendimiento y la latencia con dos efectos deben comprobarse en la Raspberry física; las mediciones de otro ordenador no equivalen a las de la Pi.

### Explorar, escuchar y asignar samples

1. Abre SAMPLES con el selector de menú.
2. NAV elige carpeta o archivo. OK entra en una carpeta. La entrada **`.. / VOLVER`** regresa al nivel superior; con Arduino también puedes **mantener FN 0,65 s**.
3. Elige un WAV/MP3. La segunda línea muestra su onda tras la lectura en segundo plano.
4. **FN corto inicia o detiene la preescucha** del archivo. No lo asigna ni cambia tus patrones. Se reproduce seco, a nivel reducido, pasando por el volumen maestro y el limitador. Cambiar de selección o salir de FILES detiene la escucha con fundido. Un archivo en proceso de lectura se escucha cuando termina, siempre que siga seleccionado.
5. A1 selecciona el pad de destino. También puedes pulsar cualquiera de los nueve pads: **todos seleccionan destino y reproducen su sonido actual**.
6. OK corto carga el archivo en ese pad. El LCD confirma `Cargado en PAD n`; si falla conserva la muestra anterior.

Sin Arduino, IZQ/DER navegan, IZQ largo vuelve y **DER largo inicia/detiene la preescucha**. También están disponibles `.. / VOLVER` y OK. Volver a entrar en una carpeta actualiza su contenido.

La lectura de la onda y la preescucha usan un trabajo de decodificación en segundo plano. Se conserva un único PCM de preescucha; la carga de un pad tiene su propio trabajo. Los resultados de archivos abandonados se descartan. La onda usa 16 columnas de picos normalizados para visualizarla, sin cambiar el volumen del archivo.

Se admiten WAV PCM/float de 1–8 canales y MP3 mono/estéreo, hasta **30 segundos por muestra**. Se convierten a mono a 44.100 Hz y se añaden fundidos breves. Las muestras silenciosas no se amplifican automáticamente. Los originales no se modifican y la navegación queda dentro de la raíz indicada por `--samples`.

### Conexión y compatibilidad

El sketch envía cada 20 ms, a **115200 baudios, 8N1**:

```text
CTRL 512 665 205 0 0
```

El orden es `CTRL nav a1 a2 ok fn`: tres valores 0–1023 y dos estados 0/1. Hay filtrado ADC, corrección de extremos y antirrebote de 25 ms. El protocolo anterior `POT valor` sigue permitiendo NAV y conserva la fila superior como controles porque no ofrece OK/FN externos.

El LCD avisa **`ARDUINO: 9 PADS`** al recibir controles válidos y **`FILA: CONTROLES`** al perderlos. Tras 300 ms sin datos se cancelan los gestos pendientes y se libera FN; el puerto USB se reintenta cada dos segundos. Una pulsación mantenida durante el cambio de función se debe soltar antes de actuar otra vez. La reconexión recupera recogida para parámetros continuos y espera un giro para los selectores.

Los JSON y kits anteriores de seis muestras siguen cargándose; los guardados actuales contienen nueve pistas/muestras y las dos posiciones de efectos. El estado de conexión y los botones no se guardan.

`make test-core` verifica sketch, serie, selectores y DSP. `make test` añade navegación, selector de menú, gestos de FN, octavas, preescucha y descarte de resultados, asignación uniforme, persistencia, puerto virtual, audio y GPIO simulados. El cableado, el sentido físico y la respuesta del montaje deben comprobarse en la Raspberry y el UNO.

## Cambios para reducir la carga

- Mezcla en C, hasta 32 voces, con buffers y voces preasignados. Los sonidos sintetizados se generan al iniciar, incluidas las cuatro octavas del bajo.
- El reloj de pasos y las repeticiones avanzan por frames de audio, sin depender de los retardos del LCD ni del bucle de botones.
- Hilo ALSA dedicado; no abre archivos, decodifica ni reserva memoria al mezclar. El intercambio de muestra usa una sección crítica breve y retira únicamente las voces del pad sustituido, conservando una cola de fundido sin referencias al PCM anterior antes de liberarlo.
- Barrido LED separado, una fila cada 1 ms, apagando las filas antes de cambiar columnas. Los indicadores se actualizan cada 10 ms, independientemente del refresco del LCD. El cabezal y la actividad de voces siguen la posición reproducida que comunica ALSA, compensando también los 3 ms del limitador; no se usa el estado adelantado de la mezcla. El LCD se actualiza solo donde cambia el contenido.
- Carga y conversión fuera del hilo de audio, con prioridad de CPU reducida para esas tareas en Linux. Se conservan solo las muestras actuales; no hay caché creciente de todas las combinaciones de efectos. Nueve muestras de 30 s ocupan aproximadamente **45,4 MiB** de PCM, además del bajo, dos posiciones FX, un PCM de preescucha y la memoria temporal de carga.

Hay diez efectos generales más OFF y nueve efectos momentáneos con Arduino (seis sin Arduino). Los filtros, la distorsión, el remuestreo y los sonidos sintetizados usan algoritmos adaptados a C y **no producen exactamente el mismo audio que Python**. Los efectos generales se aplican globalmente en directo con y sin CTRL, también a golpes ya iniciados. Con CTRL los momentáneos capturan la mezcla; sin CTRL mantienen las repeticiones y transformaciones de voces originales. El master añade un limitador preventivo y suaviza el volumen. El bajo es monofónico, con fundido breve al cambiar de nota. Las repeticiones comienzan al pulsar y después siguen el intervalo de BPM; no se alinean al origen del compás como en Python.

La salida es estéreo de 16 bits a 44,1 kHz, con el mismo contenido mono en ambos canales. Se solicita a ALSA un **buffer de 60 ms por defecto**, configurable con `--latency-ms`. Los bloques siguen el periodo negociado cuando está entre 64 y 2048 frames; en otros casos usan 512 frames. El programa imprime el buffer/periodo real, el tiempo máximo de render y espera del mutex y las recuperaciones de audio al cerrar. Esto **no mide la latencia física**. Los botones conservan el antirrebote de 25 ms. La latencia, la carga de CPU y la estabilidad del multiplexado deben comprobarse en la Raspberry con tu tarjeta y cableado; aquí se ha verificado el software sin el hardware conectado.

## Protección contra chasquidos y saturación

La revisión de audio elimina la normalización que amplificaba los sonidos sintetizados: ahora solo se atenúan los picos que lo necesitan, una vez al preparar cada sonido. No existe normalización por bloque. Las voces tienen entrada de unos 0,5 ms y salida de 2 ms; las sustituciones, el robo de voces y los cambios del bajo conservan una cola de 3 ms para evitar un salto directo a cero. El volumen master se suaviza durante unos 5 ms.

La suma permanece en float y pasa por un **limitador con unos 3 ms de anticipación**, ataque suavizado y liberación de unos 50 ms, que reduce ganancia solo ante picos altos, sin elevar señales débiles. Un recorte de seguridad permanece al final; se cuenta por separado. Si ALSA notifica una interrupción, se aplica una entrada de 5 ms al reanudar; esto suaviza el borde, pero no reconstruye el audio perdido ni corrige por sí solo la causa de un `xrun`.

Al cerrar se muestran el pico de la mezcla, los frames que excedieron el margen, la actividad del limitador y el recorte final. Un `xrun` identifica una interrupción de entrega, no un problema del normalizador. Consulta el [diagnóstico de audio](docs/diagnostico_audio.md) para separar ambos casos. La normalización visual del LCD solo cambia las barras y no interviene en la reproducción.

## Guardado e importación

Se guardan automáticamente patrones, BPM, octava, efectos, volumen, acompañamiento y rutas de las muestras en **`~/.sampler3x3/patrones_v4.json`**, tras 1,5 s sin cambios y al salir. El guardado usa un archivo temporal y renombrado. Si falla, se informa por terminal y se reintenta.

Si no hay un estado v4 válido, se importan `patrones_v3.json` o, en su defecto, `patrones.json`. Los archivos anteriores no se sobrescriben. Las rutas de muestras son absolutas: mantén el USB en el mismo punto de montaje. Si una muestra no está disponible al iniciar, se usa el sonido sintetizado de ese pad y se informa por terminal; la asignación permanece guardada para intentarlo en el próximo inicio.

Puedes usar otro estado con `--state /ruta/kit.json` (su carpeta debe existir), o evitar escrituras con `--no-save`. Para recuperar sonidos sintetizados, con el programa cerrado deja vacía la entrada correspondiente del array `samples` en el JSON, o usa un estado nuevo.

## Pruebas de C sin Raspberry

```bash
make test-core  # controles, audio, LED, serie y sketch simulado; compiladores C/C++
make test       # ademas WAV/MP3, carpetas, JSON, carga asincrona y ejecutable con ALSA null
./sampler-c --headless --samples ./samples
```

En `--headless` se usan el audio real y una pantalla por terminal sin abrir GPIO. Para una prueba silenciosa de duración limitada:

```bash
./sampler-c --headless --device null --no-save --seconds 2
```

Comandos de terminal (una línea + Enter): `l`, `o`, `r` equivalen a IZQ, OK y DER; `L`, `O`, `R` a sus pulsaciones largas. `O` abre el selector de menú. `mode seq`, `mode fx`, `mode bpm`, `mode bass`, `mode drum` y `mode files` acceden directamente desde terminal; `fxslot 1`/`fxslot 2` eligen posición FX y `preview` alterna la preescucha en FILES. `1`–`9` tocan/seleccionan un slot, `+1`–`+9` mantienen un pad y `-1`–`-9` lo sueltan para FX. Los slots 7–9 corresponden a la fila superior cuando hay CTRL. `ls` lista la carpeta, `slots` muestra las asignaciones y `q` cierra. En terminal, la forma de onda se representa con alturas `1`–`8` y `.` para silencio. La salida ALSA `null` descarta el audio y puede avanzar más rápido que el tiempo real; sirve para pruebas funcionales, no para medir tempo, rendimiento ni latencia.

Las pruebas cubren reloj a nivel de frame independiente del tamaño de bloque, controles, prioridad de efectos mantenidos, reemplazo seguro de voces, conversión WAV estéreo/48 kHz, MP3, archivos dañados y límites, navegación confinada a la raíz, guardado/importación JSON y carga/reapertura desde FILES. También comprueban los picos de la onda, silencio, PCM no válido, la programación de los ocho glifos y el envío del carácter 0 mediante un bus LCD simulado, los cambios entre texto y onda y las vistas previas WAV/MP3 sin asignación. No sustituyen las pruebas físicas de audio, LCD, botones y matriz.

Las pruebas de Arduino incluyen el filtro y la cadencia del sketch mediante funciones simuladas, mensajes fragmentados y dañados, posición absoluta, recogida al cambiar de modo o usar botones, histéresis, pérdida de datos y desconexión. La prueba de integración usa un puerto serie virtual para mover el BPM, desconectar/reconectar el puerto y comprobar el guardado, sin una placa conectada.

---

# Referencia de cableado y versión Python original

Los apartados siguientes documentan la aplicación **Python v3 original**. Para ejecutar la versión C y usar FILES, sigue las instrucciones anteriores. El cableado y los controles de SEQ, FX, BPM, BASS y DRUM se mantienen.

# Sampler 3×3 v3 + matriz LED 4×4 — batería + bajo + efectos Pocket

**Hardware:** Raspberry Pi 3B, 9 interruptores (GND común, pull-up internas), LCD1602 HD44780 por GPIO paralelo en 4 bits y matriz 4×4 de LED. Sin conexión a Internet durante la ejecución. Los sonidos se sintetizan al iniciar o al tocar la nota por primera vez.

**Importante:** los efectos no procesan la salida *master* como lo haría un procesador DSP real. Los efectos globales y Reverse / Tape Stop / Glitch se aplican **a cada nuevo golpe**; las repeticiones vuelven a disparar el último grupo de notas. Al pulsar Reverse/Tape Stop/Glitch se vuelve a disparar una vez el último grupo procesado para dar respuesta inmediata. No modifica el audio que ya está sonando.

## Botones 3×3

```
                SUPERIORES (NAVEGACIÓN)
       ◀ IZQ            OK           DER ▶
       GPIO5          GPIO6          GPIO12

                 INFERIORES (PADS)
       PAD 1          PAD 2          PAD 3
       GPIO13         GPIO16         GPIO19

       PAD 4          PAD 5          PAD 6
       GPIO20         GPIO21         GPIO26
```

Conectar cada switch entre el GPIO indicado y GND común. `GPIO21` corresponde al **pin físico 40**, pad inferior central.

LCD1602: RS=GPIO17, E=GPIO27, D4=GPIO22, D5=GPIO23, D6=GPIO24, D7=GPIO25, RW=GND. Los pines de alimentación, contraste y retroiluminación se mantienen como los hayas montado y comprobado eléctricamente.

## Matriz LED 4×4 y seguimiento de los 16 pasos

**Numeración BCM** (no confundir con numeración física):

| Fila / columna | GPIO BCM | Pin físico |
|---|---:|---:|
| Fila 1 (ánodos) | 7 | 26 |
| Fila 2 (ánodos) | 4 | 7 |
| Fila 3 (ánodos) | 8 | 24 |
| Fila 4 (ánodos) | 9 | 21 |
| Columna 1 (cátodos) | 10 | 19 |
| Columna 2 (cátodos) | 11 | 23 |
| Columna 3 (cátodos) | 18 | 12 |
| Columna 4 (cátodos) | 15 | 10 |

**Distribución física (fila 1 arriba):**

```
             COL 1  COL 2  COL 3  COL 4
Fila 1         04     08     12     16
Fila 2         03     07     11     15
Fila 3         02     06     10     14
Fila 4         01     05     09     13
```

- En modos **SEQ** y **BASS**, los pasos con nota grabada aparecen con menor intensidad.
- El **cabezal de reproducción** se ilumina más fuerte y avanza según el BPM, incluso si ese paso está vacío.
- El **paso seleccionado para editar** parpadea lentamente (2 Hz). Si son dos pasos distintos, se ven ambos simultáneamente.
- El cursor parpadea también cuando contiene una nota: en su fase apagada tiene prioridad sobre la nota y el cabezal. Los demás pasos programados permanecen encendidos a intensidad tenue para distinguir el avance brillante.
- Pulsar **IZQ/DER** cambia el paso que parpadea; pulsar **OK** en SEQ/BASS escribe o elimina la nota, y actualiza la matriz.
- En la versión C, fuera de **SEQ/BASS** (**FX**, **BPM**, **DRUM** y **FILES**), los LED **1–6** indican los seis sonidos de batería y los **7–12** las seis notas de bajo, en el orden de los pads. Los LED 13–15 indican shaker, rim y cowbell (slots 7–9); el LED 16 permanece apagado. Responden tanto al secuenciador como a los golpes manuales y las repeticiones de efectos; siguen el audio, no los botones pulsados. Una actividad breve deja un pulso visible de 40 ms. Un archivo silencioso no enciende su indicador.
- Con STOP, en SEQ/BASS se siguen viendo los pasos grabados y el cursor de edición.

**Cableado eléctrico:** pata larga (+) a la fila y pata corta (-) a la columna.
Usar resistencias limitadoras de corriente, por ejemplo **1 kΩ en serie con cada una de las cuatro columnas** para comenzar pruebas; la intensidad real varía según los colores de LED.
El código activa una sola fila cada vez, sin conexiones directas de columnas a GND.
No uses esos GPIO simultáneamente para SPI u otro periférico.
**Precaución:** GPIO 7 es el **pin físico 26**. Confirma que en el cableado real no esté ocupado por otro circuito. El software no puede verificar el cableado.

**Temporización:** la versión C barre la matriz en un hilo separado y toma una imagen de los cuatro renglones por barrido. Si Linux retrasa el hilo, descarta los plazos vencidos para evitar una ráfaga de filas demasiado cortas. Sigue siendo multiplexado por software: una pausa larga del sistema puede producir un cambio de brillo. La versión Python conserva el barrido en el bucle principal y el comportamiento anterior de los indicadores fuera de edición.

## Navegar entre modos

**Mantener OK 0,65 s** para pasar entre:

`SEQ → FX → BPM → BASS → DRUM → SEQ`

La pulsación corta de OK depende del modo. Una pulsación corta se confirma al **soltar** el botón. Una pulsación larga no cuenta además como corta.

### Modo 1 — SEQ (secuenciador de batería)

Seis pistas independientes, 16 pasos por pista y tempo común:

```
PAD 1: Bombo         PAD 2: Caja       PAD 3: Charles cerrado
PAD 4: Charles abierto PAD 5: Palmas    PAD 6: Tom
```

- Pulsar pad: reproduce y selecciona instrumento.
- IZQ corto / DER corto: paso anterior / siguiente.
- OK corto: activa o desactiva ese instrumento en el paso seleccionado.
- IZQ largo / DER largo: cambia a instrumento anterior / siguiente sin dispararlo.
- La pantalla LCD muestra la pista y una fila de 16 caracteres. `X` = golpe, `.` = silencio, `*` = golpe seleccionado, `^` = silencio seleccionado, `O`/`:` = cabezal de reproducción.

### Modo 2 — FX (efecto general + seis *punch-in*)

Los **efectos generales** se mantienen separados de los seis pads:

- IZQ / DER corto: recorrer `OFF → LOWPASS → HIGHPASS → BITCRUSH → DIST → ECHO → STUTTER`.
- OK corto: activar/desactivar (*bypass*) el efecto general seleccionado.
- El efecto general elegido sigue activo en los otros modos, hasta que lo desactives o cambies.
- PAD 1 a PAD 6: efectos **momentáneos**. Mantén pulsado y suelta para terminar.

```
PAD 1: REP 1/8      PAD 2: REP 1/16      PAD 3: REP 1/32
PAD 4: REVERSE      PAD 5: TAPE STOP     PAD 6: GLITCH
```

- REP 1/8, 1/16 y 1/32: mientras mantienes, el motor deja de lanzar los golpes naturales y vuelve a disparar el último grupo de notas al intervalo sincronizado. El cabezal del secuenciador sigue avanzando.
- REVERSE: los golpes nuevos se reproducen invertidos, con pequeño fundido final.
- TAPE STOP: los golpes nuevos se ralentizan y bajan de tono progresivamente.
- GLITCH: los golpes nuevos se trocean y reorganizan en fragmentos breves.
- Puedes mantener más de un pad; **el último presionado tiene prioridad**. Al soltarlo, vuelve a activarse el anterior si sigue pulsado.
- Los efectos generales se aplican **además** de los efectos momentáneos (también en las repeticiones).
- Las repeticiones están diseñadas para funcionar cuando el secuenciador está reproduciéndose y ya ha sonado algún golpe.
- No hay *latch*: ningún efecto momentáneo queda activado después de soltar todos los pads.

### Modo 3 — BPM (transporte)

- IZQ corto: -5 BPM.
- DER corto: +5 BPM.
- OK corto: PLAY / STOP.
- Rango: 50–220 BPM.
- Los pads inferiores permiten seguir tocando los seis sonidos de batería mientras ajustas el BPM.

### Modo 4 — BASS (bajo en directo + secuenciador)

Seis notas **afinadas** y una melodía monofónica de 16 pasos.

```
PAD 1: C2           PAD 2: D2           PAD 3: E2
PAD 4: G2           PAD 5: A2           PAD 6: C3
```

- Pulsar pad: toca el bajo en directo y selecciona esa nota para escribir.
- IZQ / DER corto: mover el cursor entre los 16 pasos.
- OK corto: escribir la nota seleccionada en el paso; si ya era esa nota, dejar silencio.
- IZQ largo / DER largo: bajar/subir una octava (límites: 1–4).
- Pantalla: los pasos contienen `.`, o `1` a `6` para la nota elegida; `*` / `^` marcan el cursor.
- Batería y bajo comparten BPM y se reproducen simultáneamente mientras `PLAY` está activo.
- El bajo utiliza un generador sencillo tipo sintetizador, no muestras ni MIDI externo.

### Modo 5 — DRUM (batería en directo)

- Seis pads: mismos sonidos de batería que SEQ, pero **sin modificar patrones**.
- Por defecto, el acompañamiento secuenciado no suena en este modo.
- OK corto: activar/desactivar el acompañamiento (la secuencia de batería **y** la del bajo), para improvisar encima.
- IZQ / DER corto: bajar/subir volumen en incrementos del 5 %.
- El resto de los modos conserva su estado y patrones.

## Instalar en Raspberry Pi OS

1. Desde la Raspberry (por SSH o terminal):

```bash
sudo apt update
sudo apt install -y python3-rpi.gpio python3-numpy python3-pygame alsa-utils
```

2. Descomprime `sampler_3x3_led_v4.zip` en Descargas de tu ordenador Ubuntu y **copia la carpeta completa**:

```bash
scp -r ~/Descargas/sampler_3x3_led_v4 sampler:/home/sk/
```

3. Inicia por SSH:

```bash
ssh sampler
cd ~/sampler_3x3_led_v4
python3 sampler.py
```

`Ctrl+C` para salir. No necesitas ejecutar `sudo python3` si tu usuario tiene acceso a GPIO y audio.

Si tu sistema no encuentra audio, inspecciona la salida con `aplay -l`, comprueba el cable/salida de sonido de la Raspberry y vuelve a ejecutar. Si falla con `No module named pygame`/`RPi`, comprueba que usas `/usr/bin/python3` del sistema en lugar de un virtualenv sin los paquetes apt.

## Guardar patrones

- Se escribe automáticamente en `~/.sampler3x3/patrones_v3.json` (tras 1,5 s sin cambios y al cerrar).
- Al arrancar, si existe `patrones_v3.json`, se carga.
- Si no existe, puede importar tus seis pistas antiguas y el BPM desde `~/.sampler3x3/patrones.json` **sin sobrescribirlo**.
- Los patrones nuevos del bajo se almacenan en `bass_pattern`, con valores `-1` para silencio y `0..5` para notas.
- Guarda el archivo o una copia de la microSD si quieres conservar tus secuencias ante una reinstalación.

## Pruebas sin Raspberry (en PC)

Sólo requieren Python + numpy:

```bash
cd sampler_3x3_led_v4
python3 -m unittest discover -s tests -v
python3 -m py_compile *.py
```

Estas pruebas no sustituyen una prueba real de la matriz LED, el audio, el LCD1602 y los nueve cables GPIO.
