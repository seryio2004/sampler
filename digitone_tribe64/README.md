# Tribe64 — fork del sampler 3×3

Propuesta de hardware autónomo: [ESP32-S3, componentes y cambios de arquitectura](docs/ESP32_COMPONENTES_Y_ARQUITECTURA.md).

Secuenciador para Raspberry Pi + Arduino, inspirado en la edición por páginas y los mutes de Elektron Digitone. Cada patrón tiene **64 semicorcheas**, repartidas en **4 páginas de 16**: cuatro compases en 4/4. Conserva nueve pads, bajo melódico, nueve escenas, canción, efectos, LCD1602 y el cableado original.

Este fork adapta ese flujo al montaje existente; no implementa el motor completo de cuatro operadores, los parameter locks ni todas las funciones del Digitone. Las voces FM son síntesis original precalculada en el motor del sampler. No contiene sonidos ni firmware de Elektron.

## Compilar y tocar

En Raspberry Pi OS, instala las dependencias del proyecto y compila en esta carpeta:

```bash
sudo apt install build-essential pkg-config libasound2-dev libsndfile1-dev libmpg123-dev libjson-c-dev libsamplerate0-dev
make clean
make
./iniciar_tribe64.sh --arduino /dev/ttyACM0
```

El lanzador crea `tribe64_state.json` desde el preset la primera vez; en los siguientes arranques conserva tus cambios. `Ctrl+C` guarda y cierra. El sketch del Arduino sigue siendo `arduino/sampler_pot/sampler_pot.ino`: OK en D2, FN en D3, ambos a GND con INPUT_PULLUP. No cambia el cableado.

Para probar el preset sin guardarlo:

```bash
./sampler-c --samples ./samples --state kits/tribe64.json --no-save --arduino /dev/ttyACM0
```

Sin Arduino, omite `--arduino`; la fila superior conserva IZQ/OK/DER y hay seis pads físicos de sonido. El menú y los potenciómetros permiten seleccionar las demás pistas.

## Visor HDMI: solo visual, controles físicos

`pantalla_hdmi.py` es un monitor pasivo con fondo gris verdoso, tipografía sans serif, datos monoespaciados, líneas finas y un acento turquesa, inspirado en la [referencia visual de TypeSafe](https://typesafe.ai/blog/introducing-system-one-models-and-jev). Los textos se limitan a nombres, valores y estados; no hay eslóganes ni instrucciones permanentes. **Todo el instrumento se maneja con los pads, OK, FN y las perillas del sampler.** La pantalla sigue automáticamente el modo, pista, página y menú elegidos físicamente.

No hay botones gráficos, edición con ratón, controles táctiles, arrastre, rueda, atajos ni entrada de teclado. El cursor está oculto. El cierre de la decoración de ventana tampoco controla el sampler. La entrada de comandos de terminal está desactivada por defecto y siempre queda bloqueada cuando se usa HDMI, incluso si se solicita expresamente.

El visor también puede arrancarse desde **SSH**: detecta la sesión gráfica local y su autorización. Ambos lanzadores comprueban la arquitectura del ejecutable y recompilan si se ha copiado un binario de otro ordenador o han cambiado las fuentes.

Después de copiar el fork completo, instala las dependencias en la Raspberry:

```bash
sudo apt install build-essential pkg-config libasound2-dev libsndfile1-dev libmpg123-dev libjson-c-dev libsamplerate0-dev python3-tk fonts-dejavu-core xwayland
./iniciar_hdmi.sh --arduino /dev/ttyACM0
```

Puedes ejecutar `cd ~/digitone_tribe64` y `./iniciar_hdmi.sh --arduino /dev/ttyACM0` directamente por SSH, con el mismo usuario del escritorio. El visor busca la sesión X11 o XWayland del HDMI; no utiliza el display reenviado al ordenador de la conexión SSH. No necesitas exportar `DISPLAY` ni `XAUTHORITY` a mano.

Comprueba la conexión gráfica, sin arrancar audio, con `./iniciar_hdmi.sh --check-display`. Si necesitas seleccionar una pantalla concreta, puedes usar `--display :1`. Si la Raspberry arranca en consola, puedes instalar Xorg + xinit y ejecutar `./iniciar_kiosco.sh --arduino /dev/ttyACM0` desde el teclado local: abre únicamente el visor, sin escritorio. Paquetes y pasos en [Arranque por SSH](docs/ARRANQUE_SSH.md#hdmi-mínimo-sin-escritorio).

Este lanzador sustituye a `iniciar_tribe64.sh` e inicia una única instancia del motor y el visor, usando el mismo `tribe64_state.json`. Si el sampler está ejecutándose como servicio, páralo antes de iniciar esta instancia. El cierre administrativo del proceso o servicio termina el motor y guarda los cambios; no existe un comando de cierre desde el visor.

El monitor representa los modos físicos:

| Vista | Información mostrada |
|---|---|
| PATRÓN / BAJO | Diez pistas, página de 16 pasos, cursor de edición, cabezal de reproducción y resumen de los 64 pasos |
| MUTE | Nombre y estado de las pistas, solo y señal de voz individual; los pads físicos hacen los mutes |
| SONIDO / PADS | Forma completa del sample, afinación, filtro, longitud y onda de la pista seleccionada |
| SAMPLES | Carpeta y archivo elegidos con NAV, pad destino, preview y estado de carga |
| EFECTOS | Los dos slots, efecto activo, selección pendiente y valores de las perillas |
| TEMPO | BPM, transporte y volumen |
| CANCIÓN | Bloques, patrón, repeticiones, bloque actual, bucle y edición pendiente |
| LANZAR / ESCENAS | Escena que suena, escena seleccionada y siguiente; acciones físicas de gestión |

Las ondas son datos de audio, no adornos animados:

- **MASTER OUT:** mezcla global real después de efectos, volumen y limitador; ventana de unos 46 ms.
- **VOICES[01..10]:** diez osciloscopios de las voces individuales, incluyendo el bajo, antes de los efectos globales; ventanas de unos 23 ms. Cada onda se identifica por número y nombre; el turquesa destaca la pista seleccionada y la línea queda plana cuando no hay señal. Las colas de los efectos compartidos aparecen en MASTER OUT.
- **VOICE seleccionada:** ampliación de la pista que estás manejando físicamente.
- **SOURCE:** forma completa del sample seleccionado, tramo de longitud elegido y posición de reproducción. Es un resumen gráfico del archivo, distinto de la señal en directo.
- **Nivel y envolvente:** estimación RMS de la ventana de mezcla y evolución reciente del nivel.

El visor se actualiza unas 15 veces por segundo y se adapta a la resolución. La comunicación con el motor es solo de lectura mediante un socket privado heredado, sin servidor de red ni puerto abierto. La ventana no tiene canal de comandos y el motor no espera al dibujo.

Para una demostración gráfica pasiva, sin motor ni guardado:

```bash
python3 pantalla_hdmi.py --demo --windowed
```

Para generar capturas sin escritorio, instala opcionalmente `python3-pil` y usa `python3 pantalla_hdmi.py --preview docs/hdmi_patron.png --view SEQ`. La marca **DEMO** distingue los ejemplos de las señales conectadas. [Patrón y ondas](docs/hdmi_patron.png), [mutes](docs/hdmi_mute.png), [canción](docs/hdmi_song.png) y [ventana conectada durante la prueba](docs/hdmi_en_vivo.png).

`make test` incluye pruebas con el Arduino simulado por puerto serie: las entradas físicas cambian el motor y su telemetría. `python3 tests/smoke_hdmi_window.py` comprueba en una sesión gráfica que ratón, teclado y cierre de ventana no modifican el instrumento. La comprobación física del HDMI, audio y GPIO en la Raspberry queda pendiente.

## Menú simplificado

**OK largo** abre el menú principal: **PATRONES → MUTE → SONIDO → SAMPLES → EFECTOS → TEMPO → MÁS**. NAV elige y OK corto entra. OK largo vuelve a cerrar el menú sin cambiar de pantalla.

Dentro de **MÁS** están **BAJO, PADS, CANCIÓN, LANZAR y ESCENAS**. La opción **VOLVER** regresa al menú principal. Si abres el menú desde una función avanzada, se muestra directamente esa lista. Ninguna función anterior se ha eliminado; los controles de uso diario quedan en la primera lista.

## Edición y mutes con OK y FN

| Control | Acción |
|---|---|
| OK corto en PATRONES | Pone o quita un golpe del sonido y paso seleccionados |
| NAV A0 | Selecciona el paso 1–16 dentro de la página visible |
| Potenciómetro A1 | Selecciona el sonido 1–9 |
| FN corto en PATRONES o BAJO | Cambia de página: 1–16, 17–32, 33–48, 49–64 |
| Mantener FN y girar NAV | Selecciona directamente una de las cuatro páginas |
| Mantener FN y pulsar OK corto | Entra en MUTE desde PATRONES, BAJO, SONIDO o PADS; repetir vuelve a la pantalla anterior |
| NAV en MUTE | Selecciona una pista: 1–9 sonidos; 10 bajo melódico |
| OK corto en MUTE | Silencia o recupera la pista seleccionada |
| Pads en MUTE | Silencian o recuperan directamente cada sonido, sin dispararlo |
| FN + pad en PATRONES, BAJO, SONIDO o PADS | Mute rápido sin cambiar de pantalla ni disparar el sample |
| FN + pad en MUTE | Solo de esa pista; repetir el mismo gesto recupera la mezcla anterior |
| FN corto en MUTE | Quita el solo; si no hay solo, vuelve a la pantalla anterior |
| OK largo, unos 0,65 s | Abre o cierra el menú; NAV elige, OK corto entra |
| Menú TEMPO, OK corto | Inicia o detiene la reproducción |
| MÁS → ESCENAS | Selecciona, copia, vacía o renombra patrones |

En MUTE, la primera línea muestra el patrón y el nombre de la pista seleccionada. `ON` significa activa, `OFF` silenciada y `SOLO` escucha aislada. La segunda línea muestra `123456789B`: cada número corresponde a un pad, `B` es el bajo melódico y un guion indica silencio. NAV selecciona las diez pistas y OK conmuta su mute. Los LED reproducen la disposición física de los nueve pads; el bajo ocupa el primer LED de la cuarta fila. Una pista silenciada queda apagada; las activas se iluminan suavemente y brillan al sonar. La pista en solo luce brillante.

Durante la reproducción, todos los controles de mute actúan sobre **el patrón que está sonando**, aunque estés editando otro. Con el transporte parado, actúan sobre el patrón seleccionado para editar. Los mutes se guardan por patrón y se copian con él. Las colas y los efectos ya sonando terminan de forma natural.

El SOLO es una escucha temporal: no modifica los mutes guardados. Puedes cambiar de solo con FN + otro pad y salir repitiendo FN + el mismo pad, pulsando FN corto o abandonando MUTE. Al salir se recupera exactamente la combinación de mutes anterior. Un mute normal cancela primero el solo y cambia la pista pulsada. Cambiar de patrón también cancela el solo; no se guarda al cerrar el programa.

En edición, el LCD indica la página `1/4`…`4/4` y el número absoluto del paso. Los 16 LED y los 16 caracteres muestran esa página; el cabezal solo aparece cuando pasa por ella. La página visible se controla manualmente y no salta mientras editas. En BAJO, la segunda línea muestra las notas de la página y el paso absoluto aparece arriba. Un cambio de escena o un bloque de canción espera al final de los **64 pasos**. Las notas editadas durante la reproducción se incorporan en la siguiente vuelta del patrón, como en el motor de escenas original; los mutes se aplican inmediatamente a los próximos disparos.

## Qué es el modo PATRONES

Un patrón es la partitura de la máquina: determina en qué pasos toca cada sonido. Cada pista tiene sus propios 64 pasos y todas se reproducen juntas. Un paso es una semicorchea; 16 pasos forman un compás y el patrón completo tiene cuatro compases en 4/4. Las cuatro páginas son partes del mismo patrón. Las escenas E1…E9 son patrones diferentes.

Para hacer un ritmo: entra en PATRONES desde el menú, elige el bombo con A1, elige pasos con A0 y pulsa OK para activarlos. Por ejemplo, activa 1, 5, 9 y 13 en la primera página. Después selecciona la caja y activa 5 y 13: el bombo sigue programado. FN corto pasa a la siguiente página para completar los 64 pasos. El menú TEMPO permite iniciar o parar con OK. Si quieres quitar el bombo temporalmente, usa MUTE: conserva su programación para recuperarlo después.

## Modificar cada sonido con las perillas

Abre el menú con **OK largo**, selecciona **SONIDO** con NAV y entra con OK corto. Pulsa un pad para seleccionarlo y escucharlo. En esta pantalla las tres perillas controlan su sample:

| Perilla | Parámetro | Recorrido |
|---|---|---|
| A0 / NAV | T — afinación | −24 a +24 semitonos; centro = tono original |
| A1 | F — filtro paso bajo de dos polos | 0 = oscuro, 100 = abierto / sin filtro |
| A2 | L — longitud reproducida | 1–100 % de la duración del sample |

**OK corto** vuelve a disparar el sonido seleccionado. También puedes dejar el patrón reproduciéndose y girar las perillas: la afinación y el filtro afectan a las voces que están sonando; la longitud se aplica al siguiente golpe. **FN + NAV** selecciona otro pad sin dispararlo. Sin Arduino, IZQ/DER cambian la afinación, IZQ/DER largos cambian de pad y OK lo reproduce; los otros parámetros se pueden ajustar por terminal.

Cada pad conserva sus tres valores, tanto con WAV/MP3 cargados como con las voces sintetizadas. Los ajustes se guardan en el proyecto y se comparten entre todos sus patrones, igual que la asignación de samples. No se modifica el archivo WAV/MP3 original. La escucha previa en SAMPLES sigue siendo una escucha limpia del archivo.

Al cambiar de pad o entrar en SONIDO, las perillas esperan a alcanzar el valor guardado antes de actuar. Si una no responde, llévala al valor que aparece en la pantalla; después podrás modificarlo sin saltos. El ajuste de afinación cambia la velocidad de reproducción: al subir una octava el sample dura la mitad, al bajarla dura el doble. No es un algoritmo de estiramiento temporal.

En el diagnóstico optativo `--terminal` sin HDMI, `mode sound` abre esta pantalla y `sound 3 -12 0.4 0.25` configura el pad 3 con −12 semitonos, filtro al 40 % y longitud del 25 %. Los límites son pad 1–9, afinación −24…24, filtro 0…1 y longitud 0.01…1. Los proyectos anteriores se abren con tono original, filtro abierto y longitud completa. Estos controles cubren edición básica por sample inspirada en [Digitakt](https://elektron.se/wp-content/uploads/2024/09/Digitakt_User_Manual_ENG_OS1.51_231108.pdf); no incluyen automatización por paso ni todas sus páginas de síntesis.

## Sonidos techno / tribe

El preset original `kits/tribe64.json` funciona sin cargar WAV externos: kick saturado con caída de tono, caja, charles metálico, abierto, palmas, tom, percusión FM tribal, golpe metálico FM y bajo FM. Además conserva el bajo melódico de seis notas. Tempo de ejemplo: **145 BPM**.

Contiene tres escenas: `TRIBE MAIN` con variaciones en los cuatro compases, `BREAK MUTES` con bombo y caja silenciados para recuperarlos en directo, y `TRIBE FILL` con relleno final. Las seis escenas restantes están vacías. Se puede regenerar el preset con `python3 scripts/generar_tribe64.py`.

## Diagnóstico y pruebas sin placa

Solo para diagnóstico automatizado fuera del visor, `sampler-c --terminal` permite los comandos originales y `page 1`…`page 4`, `mode mute`, `mute 1`…`mute 10`, `fn on` y `fn off`. `l`/`r` recorren los 16 pasos de la página, `o` equivale a OK corto y `O` abre el menú. En MUTE, `l`/`r` seleccionan pista y `o` conmuta su mute.

```bash
./sampler-c --terminal --headless --device null --samples ./samples --state kits/tribe64.json --no-save
./sampler-c --samples ./samples --state kits/tribe64.json --render demo_tribe64.wav --seconds 14
make test
```

El arranque directo de `sampler-c` usa un estado independiente en `~/.tribe64/patrones_64.json`. El lanzador usa su archivo local `tribe64_state.json`. Los estados antiguos de 16 pasos se importan en la primera página; el resto queda vacío. Los guardados del fork contienen 64 pasos y diez mutes por escena y deben abrirse con este fork.

El código Python se conserva como referencia del sampler original; el motor de 64 pasos es la versión C. Consulta [README_SAMPLER_ORIGINAL.md](README_SAMPLER_ORIGINAL.md) para cableado y funciones heredadas. La [guía original](GUIA_DE_USO.md) describe controles anteriores; para las nuevas combinaciones usa esta página.

Referencia de interacción: [manual oficial de Elektron Digitone, OS 1.41](https://www.elektron.se/wp-content/uploads/2024/09/Digitone_User_Manual_ENG_OS1.41_231108.pdf), apartados de secuenciación y MUTE.
