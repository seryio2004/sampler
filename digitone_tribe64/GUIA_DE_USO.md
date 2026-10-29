> Esta es la guía heredada. Para los 64 pasos, páginas y controles OK/FN de este fork consulta [README.md](README.md).

# Guía de uso — Sampler v5

Sampler de nueve pads con Arduino, secuenciador de 16 pasos, bajo sintetizado, dos efectos en serie y carga de WAV/MP3. Esta guía describe la aplicación C (`sampler-c`).

## 1. Arrancar

Ejecuta los comandos desde la raíz del proyecto en la Raspberry:

```bash
cd ~/sampler_v5
./sampler-c --samples ./samples --arduino /dev/ttyACM0
```

Para probar el kit house, con patrón a 124 BPM en Do menor:

```bash
./sampler-c --samples ./samples --state kits/house.json --no-save --arduino /dev/ttyACM0
```

| Preset | Contenido |
|---|---|
| `kits/house.json` | Nueve sonidos house con un patrón de ejemplo |
| `kits/house_live.json` | Los mismos sonidos con los patrones vacíos |
| `kits/raros.json` | Nueve texturas experimentales con los patrones vacíos |
| `kits/tr808.json` | Seis sonidos TR-808 con el patrón inicial de batería |

`--no-save` permite probar sin modificar el preset. Sin Arduino, omite `--arduino /dev/ttyACM0`. `Ctrl+C` cierra el sampler y guarda los cambios cuando el guardado está activo.

El sketch compatible está en `arduino/sampler_pot/sampler_pot.ino`. Si ya lo tienes cargado con el protocolo `CTRL`, no necesitas subirlo de nuevo para actualizar estos controles. El puerto puede ser otro; compruébalo con:

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

Cierra el monitor serie de Arduino antes de arrancar el sampler.

## 2. Pads y botones

### Con Arduino conectado

Los nueve pads tocan sonidos. Los números de pad se conservan entre pantallas:

```text
          IZQUIERDA   CENTRO   DERECHA
Superior      7         8         9
Central       1         2         3
Inferior      4         5         6
```

El botón **OK** está conectado a **D2 del Arduino** y **FN** a **D3**. El pad superior central es el pad 8: no es OK mientras hay conexión Arduino.

| Control del Arduino | Entrada |
|---|---|
| Primer potenciómetro / NAV | A0 |
| Segundo potenciómetro | A1 |
| Tercer potenciómetro | A2 |
| OK | D2, pulsación a GND |
| FN | D3, pulsación a GND |

### Sin Arduino

La fila superior vuelve a ser **IZQ / OK / DER**. Los seis pads inferiores conservan los sonidos 1–6. Las nueve pistas del secuenciador pueden seguir reproduciéndose aunque solo haya seis pads físicos de sonido disponibles.

El LCD avisa `ARDUINO: 9 PADS` al conectar y `FILA: CONTROLES` al perder la conexión. Si estabas manteniendo un botón durante el cambio, suéltalo antes de usar su nueva función.

## 3. Cambiar de menú

1. Mantén **OK durante 0,65 segundos** para abrir el selector.
2. Gira **NAV** para elegir pantalla.
3. Suelta OK y pulsa **OK corto** para entrar.

Con el selector abierto, mantener OK de nuevo lo cierra sin cambiar de pantalla. Una pulsación larga actúa una sola vez; soltarla no confirma una selección.

Sin Arduino, utiliza IZQ/DER para elegir y OK para entrar.

| Menú visible | Nombre interno | Uso |
|---|---|---|
| PATRONES | SEQ | Editar las pistas de muestras |
| EFECTOS | FX | Editar FX1 y FX2 |
| TEMPO | BPM | Ajustar BPM y PLAY/STOP |
| BAJO | BASS | Editar la melodía del bajo sintetizado |
| DIRECTO | DRUM | Tocar pads, ajustar volumen y acompañamiento |
| SAMPLES | FILES | Explorar, escuchar y asignar archivos |

## 4. Qué hace cada potenciómetro

| Pantalla | NAV / A0 | A1 | A2 | OK corto |
|---|---|---|---|---|
| PATRONES | Paso 1–16 | Muestra/pista 1–9 | Nota de bajo 1–6 | Añadir/quitar golpe en el paso |
| BAJO | Paso 1–16 | Muestra de percusión 1–9 | Nota de bajo; FN + A2 cambia octava | Añadir/quitar la nota en el paso |
| EFECTOS | Elegir un efecto pendiente | Parámetro A del efecto confirmado | Parámetro B del efecto confirmado | Confirmar selección o activar/desactivar |
| TEMPO | 60–180 BPM | Sin función | Sin función | PLAY/STOP |
| DIRECTO | Volumen | Sin función | Sin función | Acompañamiento ON/OFF |
| SAMPLES | Archivo o carpeta | Pad de destino 1–9 | Sin función | Entrar o cargar |

Los selectores de pasos, muestras, menús y archivos esperan **un giro deliberado** al entrar: dejarlos quietos conserva la selección anterior.

Los parámetros de efectos, el volumen y el tempo usan **recogida** para evitar saltos. Si aparece una flecha o `POT GIRA IZQ/DER`, gira hacia ese lado hasta alcanzar el valor guardado. No es necesario volver al centro. Fuera de EFECTOS, A1/A2 no modifican los efectos que están sonando.

## 5. Crear un patrón de muestras

1. Abre **PATRONES**.
2. Selecciona la muestra/pista con **A1**, o tócala con su pad.
3. Elige un paso con **NAV**.
4. Pulsa **OK corto** para añadir el golpe. Vuelve a pulsarlo para quitarlo.
5. Cambia de muestra y repite para construir las demás pistas.

Cambiar de pista conserva lo que ya has escrito. El encabezado identifica el pad (`P1`–`P9`), el nombre abreviado del sample, la nota de bajo elegida (`B1`–`B6`) y el paso. Los nombres se obtienen de los archivos asignados.

La segunda línea conserva los 16 pasos:

| Símbolo | Significado |
|---|---|
| `.` | Paso vacío |
| `X` | Golpe escrito |
| `1`–`6` | Nota escrita en BAJO |
| `^` | Cursor en un paso vacío |
| `*` | Cursor en un paso ocupado |
| `:` / `O` | Cabezal de reproducción en un paso vacío/ocupado |

Un patrón completo contiene 16 semicorcheas. Para escuchar o parar el secuenciador, entra en **TEMPO** y pulsa OK. Las muestras que ya han empezado pueden terminar aunque pares el secuenciador.

## 6. Crear el bajo

1. Abre **BAJO**.
2. Elige la nota con **A2**: C, D, E, G, A o C+.
3. Elige el paso con NAV y escribe la nota con OK corto.
4. Para cambiar octava, mantén **FN** y gira **A2**: octavas 1–4.

Al soltar FN, A2 vuelve a seleccionar notas y espera un nuevo giro. La pantalla muestra la nota y su octava. En BAJO, los seis pads inferiores tocan esas notas; los superiores mantienen sus tres muestras de percusión.

El bajo de esta pantalla es sintetizado. Un archivo de bajo cargado en un pad —por ejemplo, el bajo C del kit house— se programa como muestra desde PATRONES. Cambiar el tempo no cambia el tono ni la duración del archivo.

## 7. Efectos: elegir primero y confirmar después

La cadena es:

```text
Muestras y bajo → FX1 → FX2 → efecto momentáneo → volumen → limitador
```

Cada posición tiene su propio tipo, activación y parámetros. Puedes usar dos tipos diferentes o repetir el mismo en ambas posiciones.

### Seleccionar y aplicar

1. Abre **EFECTOS**.
2. Gira NAV para elegir el tipo de efecto.
3. Aparece un asterisco, por ejemplo `FX1* ECHO`, y `OK PARA APLICAR`.
4. Pulsa **OK corto** para confirmar y activar el efecto elegido.
5. Ajusta A1/A2 después de confirmar. Si esperan recogida, sigue sus flechas.

**Girar NAV no activa ni reemplaza el efecto que suena.** Mientras hay una selección pendiente, sigue funcionando el último efecto confirmado y A1/A2 quedan sin aplicar cambios. Esto evita modificar por accidente el efecto anterior mientras eliges el nuevo.

Después de confirmar, OK corto vuelve a servir para activar/desactivar esa posición. Para quitar un efecto mediante la lista, elige **OFF y confirma con OK**; elegir OFF sin confirmar también deja sonando el efecto anterior.

Si vuelves con NAV al tipo ya confirmado, se cancela la selección pendiente. Salir de EFECTOS a otra pantalla descarta las selecciones pendientes, conservando los efectos confirmados. Abrir y cerrar el selector de menú sin salir de EFECTOS mantiene la selección pendiente.

### Editar FX2

Pulsa **FN + OK corto** para alternar entre FX1 y FX2. Esta combinación cambia la posición que editas; no confirma su selección pendiente.

- `FX1` / `FX2`: posición que estás editando.
- `*`: tipo pendiente de confirmar.
- `+`: la otra posición también está activa, cuando no hay una selección pendiente.

Ejemplo de cadena:

1. En FX1, elige LOWPASS y confirma con OK.
2. Ajusta su frecuencia y resonancia.
3. Pulsa FN + OK corto para editar FX2.
4. Elige ECHO, confirma con OK y ajusta tiempo/realimentación.

Los dos efectos continúan activos al volver a PATRONES o tocar en otras pantallas. Solo se guardan los tipos confirmados; las selecciones pendientes no se restauran al reiniciar.

### Tipos disponibles

| Tipo | A1 | A2 |
|---|---|---|
| OFF | — | — |
| LOWPASS | Frecuencia de corte | Resonancia |
| HIGHPASS | Frecuencia de corte | Resonancia |
| BITCRUSH | Retención/remuestreo | Resolución en bits |
| DIST | Ganancia de distorsión | Mezcla |
| ECHO | Tiempo de retardo | Realimentación |
| STUTTER | Duración del fragmento | Mezcla |
| TREMOLO | Velocidad | Profundidad |
| RING MOD | Frecuencia portadora | Mezcla |
| CHORUS | Velocidad | Cantidad |
| FLANGER | Velocidad | Cantidad |

### Efectos momentáneos

Con Arduino, en EFECTOS, mantén FN y pulsa cualquiera de los nueve pads:

| Pad | Acción |
|---|---|
| 7 — superior izquierdo | ROLL 1/4: repetir el último pulso a negras |
| 8 — superior central | BEAT SLICER: dividir un pulso en cuatro y reproducir 1–3–2–4 |
| 9 — superior derecho | DUB ECHO: eco a negras con realimentación del 65 % |
| 1 | Repetición 1/8 |
| 2 | Repetición 1/16 |
| 3 | Repetición 1/32 |
| 4 | Reverse |
| 5 | Tape Stop |
| 6 | Glitch |

Los efectos actúan después de FX1 y FX2. ROLL captura el último pulso y lo repite a su velocidad original. BEAT SLICER captura un pulso, lo divide en cuatro semicorcheas y repite el orden 1–3–2–4, con fundidos breves entre cortes. Ambos necesitan audio reciente; al arrancar utilizan el historial disponible. DUB ECHO trabaja con la mezcla en directo: repite cada negra con una realimentación del 65 %, en un buffer independiente de los dos efectos normales. El BPM se toma al pulsar el pad.

El último pad mantenido tiene prioridad; al soltarlo vuelve al anterior. Se liberan al soltar FN, soltar todos los pads o salir de EFECTOS. **Sin FN, los tres pads superiores siguen tocando sus muestras.** Fuera de EFECTOS también conservan su función de sonido.

### Efectos sin Arduino

IZQ/DER cortos cambian el campo seleccionado. IZQ/DER largos recorren **FX → A → B → SLOT**:

- FX: elegir un tipo pendiente; OK lo confirma.
- A/B: ajustar el parámetro del efecto confirmado.
- SLOT: alternar entre FX1 y FX2.

Mientras hay una selección pendiente, A/B esperan su confirmación. Sin selección pendiente, OK activa/desactiva la posición actual. OK largo abre el menú.

Los seis pads en EFECTOS actúan como efectos momentáneos sobre las voces originales, sin necesitar FN.

## 8. Escuchar y cargar samples

1. Abre **SAMPLES**.
2. NAV selecciona archivo o carpeta; OK entra en una carpeta.
3. Para subir de nivel, selecciona **`.. / VOLVER` y pulsa OK**, o mantén FN durante 0,65 s.
4. Selecciona un WAV/MP3. Tras leerlo, el LCD muestra su forma de onda.
5. Pulsa **FN corto** para iniciar/detener la preescucha.
6. Elige destino con A1 o pulsando un pad. Todos los pads seleccionan destino y reproducen su sonido actual.
7. Pulsa **OK corto** para cargar el archivo elegido. Comprueba `Cargado en PAD n`.

La onda no reproduce audio por sí sola. La preescucha es seca, de nivel reducido, independiente de FX1/FX2, y pasa por el volumen maestro y el limitador. No asigna el archivo ni modifica los patrones. Cambiar de archivo o salir de SAMPLES detiene la escucha con fundido.

Si pulsas FN corto mientras se lee el archivo, se escuchará cuando termine, siempre que siga seleccionado. Si hay un error de carga, el pad conserva su sonido anterior.

Sin Arduino: IZQ/DER navegan, IZQ largo vuelve y **DER largo inicia/detiene la preescucha**. OK carga. La entrada `.. / VOLVER` también está disponible.

Se admiten WAV/MP3 de hasta 30 segundos. Se convierten a mono a 44.100 Hz. La navegación permanece dentro de la carpeta indicada por `--samples`; los originales no se modifican.

## 9. Volumen, acompañamiento y tempo

- **TEMPO:** NAV ajusta 60–180 BPM; OK cambia PLAY/STOP. Los botones IZQ/DER sin Arduino permiten 50–220 BPM en pasos de cinco.
- **DIRECTO:** NAV ajusta el volumen maestro. OK cambia `BG ON/OFF` para permitir o silenciar el acompañamiento en esta pantalla. Los pads siguen disponibles para tocar.

El volumen usa recogida: sigue el aviso de giro hasta alcanzar el valor guardado. Entrar en DIRECTO con BG OFF deja solo los golpes manuales y las colas que estén terminando; al volver a otras pantallas, el patrón vuelve a sonar si PLAY sigue activo.

## 10. Guardar tu trabajo

Para trabajar sobre una copia del kit house:

```bash
cp kits/house.json kits/mi_house.json
./sampler-c --samples ./samples --state kits/mi_house.json --arduino /dev/ttyACM0
```

Sin `--no-save`, se guardan automáticamente los patrones, el tempo, la octava, el volumen, los efectos confirmados, el acompañamiento y las asignaciones. Se guarda tras aproximadamente 1,5 s sin cambios y al cerrar con Ctrl+C.

Sin `--state`, el archivo predeterminado es `~/.sampler3x3/patrones_v4.json`, también en esta versión v5. Las rutas de los presets incluidos son relativas a la raíz del proyecto: arranca desde ella.

## 11. Problemas habituales

| Situación | Qué comprobar |
|---|---|
| Giro NAV y el efecto no cambia de sonido | Si aparece `*`, pulsa OK para aplicar la selección |
| A1/A2 no responden en FX | Confirma primero el tipo pendiente; después recoge el valor siguiendo la flecha |
| No puedo cambiar de menú | Mantén el botón OK de D2 durante 0,65 s; el pad superior central toca un sonido con Arduino |
| El mando no responde al entrar | Haz un giro deliberado; para volumen/tempo/FX puede ser necesaria recogida |
| Se ven ondas pero no se escucha el archivo | Pulsa FN corto para preescuchar; la onda solo es visual |
| No puedo salir de una carpeta | Usa `.. / VOLVER` + OK o FN mantenido |
| La fila superior cambia de función | Comprueba USB, puerto serie y alimentación; el LCD avisa de conexión/pérdida de datos |
| No se guardan los cambios | Comprueba si arrancaste con `--no-save` |
| El archivo no existe al arrancar | Comprueba la ruta del preset y que la unidad con las muestras esté montada |

## 12. Actualizar, compilar y probar

Desde tu ordenador, dentro de la carpeta del proyecto:

```bash
ssh sk@sampler 'mkdir -p ~/sampler_v5'
scp -r . sk@sampler:~/sampler_v5/
```

En la Raspberry:

```bash
cd ~/sampler_v5
sudo apt update
sudo apt install -y build-essential pkg-config python3 alsa-utils \
  libasound2-dev libsndfile1-dev libmpg123-dev \
  libjson-c-dev libsamplerate0-dev
make clean
make -j2
make test
```

Las líneas terminadas en `OK` indican pruebas superadas. Algunas simulan carpetas vacías, pérdidas de conexión y fallos de audio/GPIO para comprobar su manejo. La comprobación física de cableado, sonido y latencia se realiza en el montaje.

## 13. Control desde terminal

Una orden por línea y Enter:

| Orden | Función |
|---|---|
| `l`, `o`, `r` | IZQ, OK, DER cortos |
| `L`, `O`, `R` | Pulsaciones largas; `O` abre/cierra el selector de menú |
| `mode seq` / `mode bass` | Abrir PATRONES / BAJO |
| `mode fx` / `mode bpm` | Abrir EFECTOS / TEMPO |
| `mode drum` / `mode files` | Abrir DIRECTO / SAMPLES |
| `fxslot 1` / `fxslot 2` | Elegir posición de efectos, sin confirmar un tipo pendiente |
| `1`–`9` | Pulsar y soltar un pad |
| `+1`–`+9` / `-1`–`-9` | Mantener / soltar un pad |
| `preview` | Iniciar/detener preescucha en SAMPLES |
| `ls` / `slots` | Listar carpeta / mostrar asignaciones |
| `q` | Salir |
