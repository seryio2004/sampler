# Sampler autónomo ESP32-S3: componentes y arquitectura

Fecha: 29 de octubre de 2026. Propuesta de diseño; no es firmware implementado ni un esquema eléctrico definitivo.

## 1. Decisión y viabilidad

El objetivo es sustituir **Raspberry + Arduino por un ESP32-S3 con módulos**, manteniendo un secuenciador de 64 pasos en cuatro páginas de 16, muteos, canción, lanzamiento de patrones y modificación de sonidos. No hará falta Linux, entorno gráfico ni HDMI para utilizarlo.

**Es viable construir este instrumento con un ESP32-S3 con PSRAM. No es viable trasladar sin cambios el programa actual ni garantizar de antemano sus 32 voces y todos sus efectos.** El reto es el motor de audio y su memoria, no leer los controles. La síntesis FM comparable a un Digitone completo queda fuera de esta propuesta: el punto de partida son muestras y el bajo sintetizado del proyecto.

Placa recomendada: **ESP32-S3 DevKitC con módulo WROOM-1 N16R8**, 16 MB de flash y 8 MB de PSRAM. Comprobar la inscripción del módulo y la memoria detectada por firmware; algunas ofertas mezclan variantes. La flash almacena programa/datos y la PSRAM sirve como RAM adicional: no son intercambiables. Espressif documenta CPU de dos núcleos de hasta 240 MHz, 512 KB de SRAM interna y las interfaces necesarias en la [ficha del S3](https://www.espressif.com/en/products/socs/esp32-s3).

| Función | Valoración para este diseño |
|---|---|
| 16 pads digitales, 8 potenciómetros, OK/FN y 4 botones adicionales | Viable con expansores y ADC externo |
| 16 LEDs de pasos y 4 de página | Viable con controlador LED |
| Secuencias 64 pasos, mute, escenas, canción y lanzamiento | Viable; preservar lógica y rehacer integración |
| Pantalla con onda global, onda de pista y forma del sample | Viable con dibujo parcial y resolución reducida |
| Audio estéreo 44,1 kHz, muestras PCM, pitch, envolvente y filtro | Objetivo razonable; requiere implementación y mediciones |
| 16 pistas asignables | Viable como estructura; no equivale a 16 voces simultáneas garantizadas |
| 8 voces simultáneas con efectos sencillos | Objetivo inicial de prototipo, todavía sin medir |
| 16–32 voces, dos efectos complejos y samples largos simultáneos | No comprometido; depende del coste del DSP y acceso a memoria/SD |
| MP3 en directo y equivalencia exacta con las librerías de Linux | Posponer; convertir muestras a WAV PCM antes de copiarlas |
| Entrada de audio y grabación de samples | Requiere ADC/codec de audio adicional; no incluida en la lista base |

Recomendación: fabricar primero **un prototipo con salida de línea, 8 voces objetivo y muestras cortas precargadas**. Medirlo antes de diseñar una PCB o encargar la caja definitiva.

## 2. Lista de compra en AliExpress

Los enlaces de esta lista son búsquedas por referencia. El apartado 10 añade anuncios identificados mediante índices de terceros y un presupuesto desglosado. No ha sido posible comprobar el carrito de AliExpress: stock, variante, IVA y envío deben confirmarse antes de comprar. Selecciona las características de la tabla, no solo la foto.

### Electrónica principal

| Cantidad | Componente | Especificación para comprar | Enlace |
|---|---|---|---|
| 1 | ESP32-S3 DevKitC N16R8 | WROOM-1, 16 MB flash + 8 MB PSRAM, USB nativo accesible y cabeceras completas; evitar C3/S2 o S3 sin PSRAM | [ESP32-S3 N16R8](https://www.aliexpress.com/w/wholesale-esp32-s3-n16r8.html) |
| 1 | ADC MCP3208 | Preferible módulo SPI de 8 canales, 12 bits, utilizable a 3,3 V. Si compras MCP3208-CI/P suelto, añadir zócalo DIP16 y placa | [MCP3208 módulo](https://www.aliexpress.com/w/wholesale-mcp3208-module.html) |
| 2 | Expansor MCP23017 | Módulos I²C de 16 GPIO con direcciones configurables y RESET accesible, alimentación/lógica a 3,3 V | [MCP23017 módulo](https://www.aliexpress.com/w/wholesale-mcp23017-module.html) |
| 1 | Driver TLC5947 | Módulo de 24 salidas de corriente constante, PWM, lógica a 3,3 V; resistencia de corriente modificable | [TLC5947 módulo](https://www.aliexpress.com/w/wholesale-tlc5947-module.html) |
| 1 | OLED monocroma de 0,96 pulgadas | 128×64, SSD1306, I²C de cuatro pines, azul sobre negro, sin táctil; alternativa 1,3 pulgadas SH1106 | [OLED azul SSD1306](https://www.aliexpress.com/w/wholesale-0.96-oled-ssd1306-blue-i2c.html) |
| 1 | DAC PCM5102A | Módulo estéreo I²S, salida de línea L/R; comprobar configuración de mute y formato I²S | [PCM5102A I2S](https://www.aliexpress.com/w/wholesale-pcm5102a-i2s.html) |
| 1 | Lector microSD SPI | Señales nativas de 3,3 V; evitar módulos para Arduino de 5 V con divisores lentos | [microSD SPI 3,3 V](https://www.aliexpress.com/w/wholesale-microsd-spi-3.3v-module.html) |
| 1 | Tarjeta microSD | 16–32 GB de marca, FAT32; comprar en canal fiable si no se puede verificar autenticidad | [microSD 32 GB](https://www.aliexpress.com/w/wholesale-microsd-32gb.html) |

El MCP3208 tiene ocho entradas multiplexadas internamente: basta **un ADC para los ocho potenciómetros**, sin CD74HC4067. Su tasa admisible depende de la alimentación; trabajar inicialmente con SPI de hasta 1 MHz y respetar tiempos del fabricante a 3,3 V. Fuente: [MCP3208 y ficha técnica](https://www.microchip.com/en-us/product/mcp.3208).

El MCP23017 aporta 16 GPIO por módulo; usar dos evita matriz de teclas y permite pulsaciones simultáneas sin ghosting. Fuente: [Microchip MCP23017](https://www.microchip.com/en-us/product/mcp23017).

### Panel y conexiones

| Cantidad total | Componente | Especificación y reutilización | Enlace |
|---|---|---|---|
| 16 | Pads/pulsadores de interpretación | Versión económica: 16 pulsadores NO de 6×6 del pack de 50, con actuadores; alternativa: reutilizar nueve actuales y añadir siete | [Pulsadores momentáneos de panel](https://www.aliexpress.com/w/wholesale-momentary-push-button-12mm.html) |
| 8 | Potenciómetros | B10K lineales de una vuelta, tres terminales; comprar pack de 10 iguales o reutilizar los compatibles | [B10K lineal](https://www.aliexpress.com/w/wholesale-b10k-linear-potentiometer.html) |
| 8 | Perillas | Diámetro y eje compatibles con el potenciómetro elegido | [Perillas eje 6 mm](https://www.aliexpress.com/w/wholesale-potentiometer-knob-6mm.html) |
| 6 | Botones de función | OK, FN y cuatro funciones; seis unidades del mismo pack de 50, o reutilizar existentes | [Botones de panel momentáneos](https://www.aliexpress.com/w/wholesale-momentary-panel-button.html) |
| 16 | LEDs de pasos/pads | Rojos difusos de 3 mm; reutilizar los actuales o sacar 16 del pack de 100 | [LED difuso 3 mm](https://www.aliexpress.com/w/wholesale-3mm-diffused-led.html) |
| 4 | LEDs de página | Rojos del mismo pack; etiquetar 1–16, 17–32, 33–48, 49–64 | [LED difuso 5 mm](https://www.aliexpress.com/w/wholesale-5mm-diffused-led.html) |
| 1 | Interruptor ON/OFF mantenido | SPST o SPDT pequeño, usado como solicitud lógica de encendido/apagado | [Interruptor SPDT panel](https://www.aliexpress.com/w/wholesale-spdt-mini-toggle-switch.html) |
| 2 o 1 | Conectores de salida | Dos jacks mono L/R o un jack TRS estéreo de línea; no confundir TRS estéreo con salida mono balanceada | [Jack 6,35 mm panel](https://www.aliexpress.com/w/wholesale-6.35mm-panel-jack.html) |
| 1 | Cable USB de datos | Compatible con la placa, para programación y servicio; no solo carga | [Cable USB C datos](https://www.aliexpress.com/w/wholesale-usb-type-c-data-cable.html) |
| 1 lote | Conectores y cable | JST-XH o bornes, cabeceras, cable flexible, separadores y termorretráctil | [Kit JST-XH](https://www.aliexpress.com/w/wholesale-jst-xh-2.54-kit.html) |
| 1 | Placa de montaje | Placa perforada para prototipo; PCB propia después de validar | [Placa perforada](https://www.aliexpress.com/w/wholesale-prototype-perfboard.html) |
| 1 | Caja | Comprar tras fijar pantalla, distancias de pads, jacks y placas | [Caja proyecto electrónica](https://www.aliexpress.com/w/wholesale-electronic-project-enclosure.html) |

Estos pads son interruptores: **no detectan velocidad o presión**. Si se quieren pads piezoeléctricos/FSR, cambian la compra, los acondicionadores analógicos y el firmware; el ADC de los potenciómetros no resuelve por sí solo 16 pads sensibles.

### Alimentación y componentes auxiliares

| Cantidad | Componente | Uso | Enlace |
|---|---|---|---|
| 1 | Fuente regulada 5 V, 2 A | Margen de prototipo; sumar consumos reales y comprobar regulador de placa antes de cerrar diseño | [Fuente 5 V 2 A](https://www.aliexpress.com/w/wholesale-5v-2a-power-supply.html) |
| 1 | Regulador 3,3 V externo | Módulo regulado de al menos 500 mA para periféricos si la placa no ofrece margen; salida verificada con multímetro | [Regulador 3,3 V](https://www.aliexpress.com/w/wholesale-3.3v-regulator-module.html) |
| 1 lote | Condensadores | 100 nF cerámicos por integrado, 10 µF por módulo y 100–470 µF en entrada; ajustar al regulador elegido | [Condensadores surtidos](https://www.aliexpress.com/w/wholesale-capacitor-assortment.html) |
| 1 lote | Resistencias | 1 kΩ para filtros de pots, 4,7–10 kΩ para polarizaciones/I²C según módulos y resistencia IREF del driver | [Resistencias surtidas](https://www.aliexpress.com/w/wholesale-resistor-assortment.html) |
| 1 conjunto | Corte electrónico de alimentación | Conmutador de carga high-side 5 V, ≥2 A, EN compatible con 3,3 V y estado apagado definido; módulo con esquema disponible | [Load switch 5 V EN](https://www.aliexpress.com/w/wholesale-5v-load-switch-enable-module.html) |
| 1 conjunto | Lógica de enclavamiento del encendido | Diodos, resistencias y transistor/MOSFET adecuados al EN del módulo; diseñar para combinar interruptor y señal HOLD del ESP32 | [MOSFET y transistores](https://www.aliexpress.com/w/wholesale-mosfet-transistor-kit.html) |

**El corte electrónico necesita esquema específico antes de comprar ese módulo.** No cualquier placa MOSFET sirve: muchas cortan masa o necesitan 5 V en EN. Para el primer prototipo puede mantenerse alimentación permanente y usar el interruptor para guardar, silenciar y entrar en reposo; esto es apagado lógico y sigue consumiendo. El corte total seguro se añade cuando se valida el enclavamiento descrito en el apartado 7.

Opcional: amplificador de auriculares estéreo si se quieren cascos. El PCM5102A entrega **nivel de línea**, no está dimensionado para auriculares de 32 Ω ni altavoz. Si se quiere grabar audio externo, añadir un codec estéreo con ADC/DAC e I²S, por ejemplo un módulo basado en ES8388, y revisar los GPIO de control y la alimentación. No comprar ambos como requisito de reproducción.

El presupuesto consolidado del apartado 10 sustituye las reservas anteriores: incluye OLED, packs de controles, batería, cuatro canales de audio y CLOCK IN, sin sumar dos veces componentes compartidos.

La lista anterior corresponde al montaje base. Para batería, cuatro canales de salida y sincronización, añadir los componentes del apartado 9; el total de DACs pasa a dos.

## 3. Arquitectura de conexiones

```text
16 pads ────────── MCP23017 A ──┐
OK + FN + 4 funciones ─ MCP23017 B ─ I²C ─┐
                                         │
8 potenciómetros ─ MCP3208 ─ SPI controles ┤
                                         │
16 LEDs + 4 LEDs página ─ TLC5947 ─ serie ─┤
                                         ├─ ESP32-S3 N16R8
OLED SSD1306 ──── I²C pantalla    ────────┤
microSD ───────── SPI almacenamiento ────────┤
                                         │
Salida L/R ◀─ PCM5102A ◀─ I²S con DMA ────┤
Interruptor + circuito HOLD/corte ────────┘
```

No enviar audio por el ADC de potenciómetros. El PCM5102A recibe audio digital I²S; la SD contiene los samples. Usar buses SPI generales distintos para ADC y SD. La OLED utiliza un segundo bus I²C dedicado, separado de los MCP23017, para que su refresco no retrase la lectura de pads. Precargar los samples reduce la dependencia de lecturas continuas desde SD.

### Asignación provisional de GPIO

Para una DevKitC N16R8 con estos pines expuestos. **Verificar esquema y revisión de la placa adquirida antes de cablear.** Se usa todo a lógica de 3,3 V; no aplicar 5 V a GPIO.

| Bus o señal | GPIO propuestos |
|---|---|
| I²C SDA / SCL | 8 / 9 |
| SPI ADC SCK / MOSI / MISO / CS | 4 / 5 / 6 / 7 |
| SPI SD SCK / MOSI / MISO | 12 / 11 / 13 |
| I²C OLED SDA / SCL, bus dedicado | 14 / 15 |
| GPIO liberado al eliminar TFT | 10 |
| SD CS | 21 |
| TLC5947 DATA / CLOCK / LATCH / BLANK | 16 / 17 / 18 / 47 |
| I²S BCLK / LRCLK / DATA OUT | 38 / 39 / 40 |
| Interruptor de solicitud | 41 |
| HOLD de alimentación | 42 |
| USB nativo D− / D+ | 19 / 20, reservados |

No usar 35/36/37 en la variante con PSRAM octal; evitar 0/3/45/46 por sus funciones de arranque y 48 por posibles LEDs integrados. La revisión de la DevKit puede llevar LED en 38: si ocurre, hay que deshabilitarlo/desconectarlo o reasignar I²S. Fuente: [guía oficial DevKitC](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-dev-kits-en-master-esp32s3.pdf).

Los MCP23017 pueden leerse por sondeo, sin pines INT. Direcciones propuestas 0x20 y 0x21; fijar A0–A2 y RESET, sin dejarlos flotantes. Primer módulo: 16 pads. Segundo: OK, FN y cuatro funciones; diez GPIO libres. Pull-ups I²C a 3,3 V, teniendo en cuenta las ya instaladas en los módulos.

## 4. Potenciómetros, pads y LEDs

Potenciómetros B10K entre 3,3 V y GND, cursor a CH0–CH7 del MCP3208 mediante resistencia de 1 kΩ y 100 nF a masa junto a la entrada. VDD y VREF a 3,3 V limpia y desacoplada; AGND/DGND con retorno común bien trazado. La medida es ratiométrica si pots y referencia comparten alimentación. Calibrar extremos y usar suavizado, histéresis y recogida del valor guardado para evitar saltos.

Objetivos de firmware: 100–200 lecturas por segundo **por potenciómetro**, sondeo de botones cada 1–2 ms y antirrebote ajustable de 5–10 ms tras probar los contactos. Son objetivos de diseño, no rendimiento medido. Los 22 botones van a GND con pull-up, cada uno a su entrada; permite FN + varios pads.

El código existente ya maneja **16 LEDs en matriz 4×4**, aunque solo tenga nueve pads. Conservar los LEDs no garantiza conservar su cableado: el TLC5947 requiere cátodos independientes hacia sus salidas y ánodos a la alimentación correspondiente. Si la matriz está en una PCB con pistas comunes, habrá que separar conexiones o elegir un driver de matriz compatible después de inspeccionarla. No conectar la matriz actual directamente al TLC5947.

Asignación TLC5947: salidas 0–15 para pasos/pads, 16–19 para páginas; quedan cuatro libres. Comenzar con 2–5 mA por LED y ajustar IREF según ficha, potencia y brillo. No dejar el ajuste habitual de algunos módulos a 30 mA sin verificar. BLANK debe mantener LEDs apagados durante el arranque con polarización externa. Fuente: [TI TLC5947](https://www.ti.com/product/TLC5947).

## 5. Pantalla y manejo

Recomendada para abaratar: **OLED de 0,96 pulgadas, 128×64, SSD1306, I²C, azul sobre negro, sin táctil**. Reservar 2–4 € para un módulo genérico, sin considerar ese rango un precio confirmado de carrito. Es pequeña: el área activa ronda 22×11 mm. La alternativa **OLED 1,3 pulgadas SH1106 azul**, con los mismos 128×64 píxeles, mejora la lectura por unos 2 € adicionales de reserva, pero no añade detalle.

También existen paneles de emisión amarilla sobre negro. Elegir explícitamente «Yellow» si se quiere amarillo en toda la superficie: «Yellow/Blue» suele tener una franja amarilla superior y el resto azul, fijadas físicamente, sin poder escoger color por software. La opción azul uniforme evita esa confusión. No comprar LCD1602/2004: son de caracteres y no sustituyen una pantalla gráfica de ondas.

Referencias técnicas: [Joy-IT OLED azul SSD1306](https://joy-it.net/en/products/SBC-OLED01V2), [Waveshare OLED 0,96 con variantes azul y amarillo/azul](https://www.waveshare.com/product/displays/0.96inch-oled-module-c.htm) y [Waveshare OLED azul 1,3 SH1106](https://www.waveshare.com/product/ai/displays/oled/1.3inch-oled-b.htm). La Waveshare de 0,96 actual lleva SSD1315: comprobar el controlador de cada anuncio y usar un driver compatible; no asumir que todo módulo de igual tamaño es SSD1306.

La pantalla sigue siendo **exclusivamente visual**. El visor Tkinter se sustituye por un renderizador monocromo con driver SSD1306 o SH1106 según compra. No habrá táctil, ratón ni interacción musical mediante teclado. El estilo pasa a fondo negro, líneas de un píxel y textos mínimos en el color de emisión del panel; se elimina la paleta RGB del diseño anterior.

Contenido en vistas seleccionadas con los controles físicos:

- Cabecera de una línea: BPM, patrón y página.
- Vista de reproducción: onda global real, más los 16 pasos en una fila inferior.
- Vista de sonido: forma del sample o señal de la pista seleccionada y un parámetro activo.
- Vista de comparación: dos ondas pequeñas, global y pista. Las demás pistas se consultan seleccionándolas; mostrar 16 ondas a la vez en 128×64 no sería legible.

Usar reducción min/max de audio real y refresco objetivo de 10–15 fps. El framebuffer de un bit ocupa **1.024 bytes**; dos, 2.048 bytes. Un envío completo por I²C a 400 kHz necesita como mínimo unos 23 ms contando ACK, más comandos y software. El bus dedicado en GPIO14/15 evita bloquear los expansores de botones en GPIO8/9. El audio funciona en otra tarea; no espera a la pantalla. Limitar refresco y contraste cuando no haga falta para reducir consumo y desgaste de contenido estático.

La sustitución libera GPIO10 y elimina el controlador táctil y la retroiluminación. La autonomía de la batería sigue pendiente de medir; no se transforma el ahorro de pantalla en una promesa de horas adicionales.

### Mapa de controles inicial

Se conservan OK y FN y se **añaden cuatro botones**: PLAY/STOP, REC, PAGE y MUTE. Los 16 pads introducen pasos en SEQ y disparan sonidos en directo. En MUTE activan/desactivan pistas; FN + pad permite solo temporal, con restauración al soltar. PAGE recorre las cuatro páginas; FN + PAGE recorre en sentido contrario. La función REC y las combinaciones son propuestas pendientes de implementación.

Los cuatro LEDs indican **la página que se está editando**: uno fijo; si se reproduce otra página, su LED parpadea suavemente. Así se distinguen edición y reproducción. El cursor dentro de la página se representa en los 16 LEDs de pasos y en pantalla. La fórmula es `paso_absoluto = pagina * 16 + pad`, con página 0–3 y pad 0–15.

Propuesta de los ocho pots en SONIDO: pitch, inicio, longitud/final, ataque, decay/release, cutoff, resonancia y envío FX. Cambian de asignación según pantalla y conservan recogida. Inicio, envolvente y envío por pista requieren ampliar el modelo de sonido actual; no están todos implementados hoy.

## 6. Audio, memoria y cambios de software

### Situación del repositorio

`src/core.h` del fork define hoy 9 pads, 10 pistas contando el bajo, 32 voces, 44.100 Hz y muestras de hasta 30 segundos. `src/arduino.c` interpreta tres potenciómetros y OK/FN. `src/hardware.c` controla GPIO Linux, LCD de caracteres y la matriz de LEDs. El Makefile enlaza ALSA, libsndfile, mpg123, json-c y libsamplerate; el visor es Python/Tkinter. **Nada de eso se convierte en firmware ESP32 ejecutando make con otra placa.**

### Arquitectura nueva

Crear un proyecto separado, por ejemplo `firmware_esp32/`, con ESP-IDF y FreeRTOS; conservar el fork Linux como referencia y banco de comparación.

| Área | Cambio necesario |
|---|---|
| Audio | Sustituir ALSA por I²S TX con DMA hacia PCM5102A |
| Motor | Separar mezcla/DSP portable de threads, dispositivos y archivos Linux; asignaciones de memoria estáticas o realizadas fuera del audio |
| Temporización | Secuenciador basado en contador de frames de audio; no en refresco de pantalla ni en `delay()` |
| Archivos | FatFS/microSD y lector WAV PCM sencillo; convertir MP3 y sample rates en el ordenador |
| Estado | Formato versionado, guardado con archivo temporal, validación y recuperación; compatibilidad/migración de los presets de nueve pistas |
| Controles | Sustituir protocolo Arduino por lectura local MCP3208/MCP23017; array de ocho pots y eventos de 22 botones |
| LEDs | Sustituir barrido GPIO Linux por TLC5947; estado calculado a partir del patrón y reproducción reales |
| Pantalla | Sustituir Tkinter por renderizador OLED pasivo monocromo; distribuir la información en vistas de 128×64 |
| Pistas | Ampliar a 16 pistas de sample y decidir mantener el bajo como pista 17; revisar máscaras, arrays, bucles y serialización |
| Verificación | Portar pruebas puras del secuenciador/DSP y añadir mediciones con hardware real |

**Hay máscaras `uint16_t` de actividad de pistas en el código actual**: con 16 samples y bajo se necesitan al menos 17 bits, por tanto migrarlas a `uint32_t`. Una máscara de los 16 pasos de una página sí puede seguir usando 16 bits. No cambiar simplemente `PADS` y asumir que todo queda compatible.

Separar tareas: audio de máxima prioridad, controles/eventos, almacenamiento y pantalla de menor prioridad. Colas acotadas, sin asignar memoria, imprimir logs, acceder a SD ni esperar al bus de pantalla desde el procesamiento de audio. Reservar buffers DMA adecuados al periférico; para el primer diseño situarlos en SRAM interna y dejar samples/ondas grandes en PSRAM. La concurrencia entre núcleos debe usar snapshots/colas, sin modificar estructuras que el audio esté leyendo.

El S3 dispone de I²S y transferencia DMA documentados por [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html). El DAC elegido admite I²S de tres señales y salida estéreo de línea, según [TI PCM5102A](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf). Cablear alimentación, XSMT y selección de formato según el esquema del módulo concreto; no confundir la tensión admitida por su placa con la del chip.

### Presupuesto de memoria y alcance inicial

A 44,1 kHz/16 bits, un segundo mono ocupa 88.200 bytes y uno estéreo 176.400 bytes. Ocho MiB equivalen teóricamente a unos 95 segundos mono o 47,5 estéreo **si toda la PSRAM fuera audio**, cosa que no sucede. El límite real del kit debe reservar memoria para efectos, pantalla, buffers y firmware.

Propuesta de primera versión: limitar PCM precargado a **4 MiB por kit**, unos 47,5 segundos mono o 23,8 estéreo agregados. Dieciséis muestras mono de dos segundos ocupan unos 2,69 MiB y caben en ese presupuesto. Mantener samples PCM16 en memoria y convertir durante la mezcla; convertirlos todos a float32 duplica su ocupación. Un delay estéreo de un segundo en float32 ya consume 352.800 bytes.

Conservar 16 pistas de secuencia, comenzar con ocho voces activas y voice stealing con rampa para evitar clicks. Pitch con interpolación, envolvente y filtro sencillo primero; un delay y saturación después. Optimizar o sustituir efectos caros antes de intentar dos bloques completos. El objetivo de bloque 128 frames a 44,1 kHz da unos 2,90 ms por bloque, **no equivale a latencia total**, que incluye cola DMA, escaneo, antirrebote y DAC.

Desactivar Wi-Fi/Bluetooth durante interpretación en la primera versión para reducir carga e interferencias. Un futuro modo de transferencia por USB o Wi-Fi puede funcionar fuera de reproducción. No se presupone disponer de SCP/SSH en el dispositivo autónomo; actualizar firmware por USB y copiar samples a microSD.

## 7. Interruptor de encendido y apagado

El interruptor mantenido debe expresar una petición. Al pasar a OFF: detener nuevas notas, aplicar rampa de salida, guardar el estado, cerrar/sincronizar archivos y liberar HOLD para cortar alimentación. Un interruptor directamente en serie con 5 V interrumpe ese proceso y puede perder cambios o corromper la SD.

Diseño de corte propuesto: la posición ON habilita el load switch; al arrancar, el ESP32 activa HOLD. OFF retira la petición del interruptor, pero HOLD mantiene la alimentación hasta acabar el guardado. El circuito combina ambas señales sin retroalimentación a GPIO y con polarizaciones definidas. Al liberar HOLD cae la alimentación; volver a ON permite arrancar. Añadir límite temporal de apagado y recuperación si falla la SD. El circuito debe poder detectar OFF incluso mientras HOLD sigue activo.

Este enclavamiento necesita **un esquema y pruebas**, no basta conectar el interruptor al pin EN de la DevKit. Durante programación, el USB puede alimentar la placa aunque el interruptor esté en OFF: prever aislamiento de VBUS o una entrada de servicio para evitar alimentación inversa. No unir salidas de dos reguladores de 3,3 V ni dos fuentes de 5 V directamente. La ampliación con batería, cargador y protección se detalla en el apartado 9; debe integrarse con este circuito de apagado.

## 8. Orden de construcción y criterios de aceptación

1. Comprar placa N16R8, DAC, SD y pantalla. Verificar flash/PSRAM reales y reproducir WAV sin interfaz pesada.
2. Medir ocho voces, pitch y filtro con salida I²S estable; después activar pantalla y carga de controles. Registrar carga máxima y margen por bloque.
3. Añadir MCP3208, los dos MCP23017 y TLC5947. Verificar las 22 pulsaciones simultáneas necesarias, extremos/recogida de los ocho pots y los 20 LEDs.
4. Portar secuencia, mute/solo, páginas, escenas, canción y lanzamiento. Probar cambios de página durante reproducción y límites exactos del patrón de 64 pasos.
5. Añadir efectos progresivamente. Prueba sostenida de al menos 30 minutos con máxima carga acordada, sin underruns ni bloqueos y con los controles operativos.
6. Probar guardado, pérdida de SD, apagado y arranque antes de diseñar alimentación definitiva y PCB. Asegurar que cambios guardados se recuperan tras apagado normal y que un archivo incompleto no impide arrancar.
7. Fijar polifonía, tamaños máximos de kit y efectos según mediciones; después cerrar PCB, caja y compra completa.

**Compra inicial recomendada:** S3 N16R8 + PCM5102A + microSD + OLED, y después el resto del panel cuando el audio esté validado. El hardware propuesto permite el instrumento solicitado; la equivalencia de audio con la versión Raspberry dependerá del portado y las mediciones.


## 9. Ampliación: batería, salidas independientes y sincronización

### Configuración propuesta

Añadir **batería LiPo 1S protegida de unos 2.000 mAh, carga USB-C con power-path, cuatro canales de salida de línea y una entrada CLOCK IN protegida**. Se mantiene un solo ESP32-S3. Todo este apartado es propuesta pendiente de esquema, firmware y validación. Se elige minijack TS de 3,5 mm como entrada principal de sync, por preferencia del usuario; MIDI queda como ampliación opcional. Las compatibilidades eléctricas y de reloj se cerrarán al conocer los modelos de los otros sintes.

### Batería y carga durante el uso

Una LiPo de una celda ocupa poco espacio: nominal 3,7 V, carga completa 4,2 V. Comprar una celda protegida, con fabricante/capacidad trazables y dimensiones que quepan en la caja; para la batería es preferible un distribuidor fiable si no se puede comprobar la oferta. Verificar polaridad real del conector: dos conectores JST iguales pueden tener polaridad invertida.

Arquitectura de potencia:

```text
USB-C 5 V ── cargador con power-path ── SYS ── boost 5 V ── corte HOLD ── equipo
                         │
                    batería 1S protegida
                         │
                    medida de batería
```

Propuesta de cargador: módulo basado en **BQ24074** con conexión independiente BAT/SYS y control de temperatura de batería. Su power-path permite alimentar el equipo y cargar gestionando la corriente disponible; su salida SYS no es un regulador fijo de 5 V, por eso se añade un boost. Fuente: [TI BQ24074](https://www.ti.com/product/BQ24074).

El boost debe admitir aproximadamente 3–4,4 V en entrada y entregar 5 V estables con la carga máxima medida; reservar al menos 1 A continuo de salida y margen transitorio. Un módulo basado en TPS61088 es una opción, ajustado y comprobado a 5 V: la cifra «10 A» del chip corresponde al interruptor interno y no garantiza 10 A de salida del módulo. Fuente: [TI TPS61088](https://www.ti.com/product/TPS61088).

El cargador queda antes del corte del equipo: se puede cargar con el sampler apagado. El interruptor y HOLD cortan los consumidores, incluyendo boost si se utiliza su EN con aislamiento correcto o un corte adecuado de entrada. El diagrama funcional no garantiza consumo cero del boost si solo se corta después de él: medir reposo y elegir desconexión real si hace falta. Conectar la batería únicamente al cargador/protección diseñado para 1S, nunca a los pines 3V3 del ESP32.

No utilizar un TP4056 sencillo como sustituto directo del módulo con power-path para tocar mientras carga: protección de celda y reparto de alimentación son funciones diferentes. Ajustar corriente de carga al límite de la batería y a la disipación del módulo, no al máximo anunciado. La entrada USB-C de alimentación debe implementar CC correctamente para funcionar con cables C–C; una entrada básica de 5 V no implica USB-PD. Aislar la alimentación USB de programación para evitar retornos a VBUS.

Estimación orientativa, **sin consumo del prototipo medido**: una celda de 2 Ah a 3,7 V almacena nominalmente 7,4 Wh. Con un 85 % global útil y un consumo de 2–3 W, resultan aproximadamente **2,1–3,1 horas**. Una batería de 1 Ah da aproximadamente la mitad. Brillo, LEDs, auriculares, radio y reguladores cambian esta autonomía. La batería/protección debe soportar los picos de entrada del boost; no dimensionarla con la corriente de 5 V de salida únicamente.

Para indicador en pantalla, se propone un módulo MAX17048 para celda 1S en el mismo I²C (dirección habitual 0x36, verificar). Su porcentaje requiere configuración/validación con la celda elegida. Fuente: [Analog Devices MAX17048](https://www.analog.com/en/products/max17048.html). Alternativa económica: divisor resistivo hacia un ADC libre, calculado para 4,2 V máximo y sin alimentación inversa cuando el equipo está apagado. No conectar directamente la batería al MCP3208 a 3,3 V.

### Cuatro canales de audio reales

Configuración sugerida: **MAIN L, MAIN R, AUX 1 y AUX 2**, cuatro conectores mono de línea. AUX 1/2 pueden usarse como dos salidas mono independientes o un segundo par estéreo. Permite separar bombo, bajo o percusiones para procesarlos con equipo externo. Duplicar el jack del primer DAC solo duplicaría la misma mezcla.

Para esta escala, añadir **un segundo PCM5102A**, total dos DACs estéreo. Cada uno recibe su flujo estéreo por un periférico I²S distinto. El S3 tiene dos I²S con modos master/slave, documentados por [Espressif](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html). No conectar ambos DACs a una misma línea DATA esperando obtener canales distintos, ni intentar que un PCM5102A decodifique cuatro slots TDM.

Propuesta de sincronía digital: I²S0 master genera BCLK/LRCLK para ambos DACs; I²S1 TX slave recibe físicamente esos mismos relojes en otros GPIO y genera DATA del segundo DAC. Usar iguales formato y longitud de slot, buffers coordinados y arranque con silencio. Validar alineación y recuperación de ambos DMA con osciloscopio/analizador y una señal común antes de comprometer salida multicanal. Los relojes compartidos evitan deriva, pero no aseguran por sí solos alineación de los buffers.

| Señal añadida | GPIO provisional | Conexión |
|---|---|---|
| I²S1 BCLK de entrada | 1 | Puente desde BCLK master GPIO38 |
| I²S1 LRCLK de entrada | 2 | Puente desde LRCLK master GPIO39 |
| I²S1 DATA OUT | 43 | DIN del segundo PCM5102A |
| CLOCK IN ya acondicionado a 3,3 V | 44 | Interrupción/captura de pulsos |

GPIO43/44 dejan de estar disponibles para consola UART; se conserva el USB nativo de 19/20 para servicio. Confirmar si el puente USB-UART de la DevKit conduce GPIO44: aislarlo físicamente o elegir una placa sin ese conflicto antes de usarlo para CLOCK IN. No conectar la salida del puente y el circuito de sync a la misma entrada. Revisar también el LED integrado de GPIO38 mencionado en el apartado 3. No se añaden pines para el medidor de batería I²C.

El mezclador necesita buses MAIN y AUX, asignación por pista, ganancia y limitación por bus. Propuesta inicial: cada pista se enruta a MAIN, AUX1, AUX2 o AUX estéreo; sacarla de MAIN al asignarla exclusivamente a AUX. Hacer explícito si los efectos compartidos vuelven al MAIN; no prometer efectos independientes por cada salida sin medir la carga. Guardar el routing con el kit y mostrarlo de forma mínima en pantalla.

Cuatro canales PCM16 a 44,1 kHz son 352.800 bytes/s de muestras de salida, más colas y conversiones; con slots de 32 bits aumenta el tráfico de bus. Esto añade procesamiento y buffers, no voces gratis. Las salidas siguen siendo de línea no balanceada y no alimentan directamente auriculares/altavoces.

Si se desean seis u ocho canales más adelante, reconsiderar un DAC TDM multicanal y su etapa analógica en lugar de seguir sumando DACs estéreo: ya se ocupan ambos I²S. Un PCM1690 admite ocho canales/TDM, pero exige alimentación y salidas diferenciales con acondicionamiento, por lo que no es la opción sencilla para esta compra. Fuente: [TI PCM1690](https://www.ti.com/lit/ds/symlink/pcm1690.pdf).

### Entrada de sincronización entre sintes

Añadir **CLOCK IN por minijack TS de 3,5 mm**, tip señal y sleeve masa, para reloj por pulsos. Objetivo eléctrico inicial: pulsos positivos nominales de 3,3/5 V, después de acondicionamiento. No es entrada de audio, MIDI TRS ni DIN Sync y no se presupone compatible directamente con señales bipolares o niveles de Eurorack.

Entrada propuesta: jack → resistencia limitadora/divisor y protección frente a picos/polaridad negativa → comparador con histéresis → salida digital de 3,3 V → GPIO44. Un LMV331 permite salida de colector abierto con pull-up a 3,3 V; dimensionar la entrada para respetar su rango de modo común y límites absolutos. Su salida abierta por sí sola **no hace segura la entrada de 5/12 V**. Fuente: [TI LMV331](https://www.ti.com/lit/ds/symlink/lmv331.pdf).

El módulo/circuito debe tener esquema comprobable y declarar rango admitido, umbrales y protecciones. No comprar un comparador genérico con pull-up de salida a 5 V y conectarlo directamente al ESP32. Para varios instrumentos unidos a masas distintas, estudiar aislamiento; la propuesta de comparador comparte masa y no proporciona aislamiento galvánico.

Firmware necesario:

- Elegir reloj interno o externo y configurar pulsos por negra, inicialmente 2/4/24 PPQN y polaridad/flanco seleccionable como opciones de diseño. El valor se ajusta al aparato concreto: no existe un ajuste único para todos los sintes.
- Capturar flancos con marca temporal en una ISR mínima; filtrar rebotes/glitches sin perder pulsos legítimos y estimar tempo/fase en otra tarea.
- Sincronizar el contador musical con el reloj externo sin cambiar bruscamente la tasa I²S ni producir clicks. Interpolar subdivisiones cuando hay menos pulsos que pasos.
- Calcular `BPM = 60 / (intervalo_segundos * PPQN)`. A 120 BPM y 2 PPQN se reciben cuatro pulsos por segundo, no dos.
- Definir cuándo se considera perdida la señal y cómo se para o continúa; mostrar estado EXT/LOCK. Incluir pruebas de jitter, cambio de tempo y cable desconectado.
- Mantener PLAY/STOP local: un cable de reloj simple no comunica necesariamente inicio, parada ni posición de canción. Un RESET adicional o MIDI Clock con mensajes de transporte son ampliaciones distintas.

Una sola entrada permite que el sampler siga a un maestro; no distribuye reloj a los demás. Para sincronizar varios equipos, utilizar las salidas/thru del maestro o un distribuidor activo compatible; no asumir que un cable en Y sirve para cualquier carga. Si el sampler debe ser maestro, añadir CLOCK OUT con driver de nivel definido y otro GPIO: queda pendiente revisar pines o trasladar señales lentas al expansor. **No comprar cable MIDI TRS como si fuera cable TS de sync**; confirmar interfaz de cada equipo.

### Componentes adicionales de esta ampliación

Las cantidades de esta tabla se suman a la lista base. Son enlaces de búsqueda; precios, compatibilidad de módulos y vendedores siguen sin verificarse.

| Cantidad adicional | Componente | Qué seleccionar | Enlace AliExpress |
|---|---|---|---|
| 1 | Batería LiPo 1S | 3,7 V nominal, aproximadamente 2.000 mAh, protegida, dimensiones y corriente especificadas, preferible NTC | [LiPo 1S protegida](https://www.aliexpress.com/w/wholesale-3.7v-2000mah-protected-lipo.html) |
| 1 | Cargador con power-path | Módulo BQ24074 con BAT y SYS separados, temperatura y corriente configurables; entrada USB-C o breakout CC apropiado | [BQ24074 power-path](https://www.aliexpress.com/w/wholesale-bq24074-module.html) |
| 1 | Boost regulado a 5 V | Módulo TPS61088 o equivalente documentado, rango de entrada adecuado, corriente continua/eficiencia verificadas y EN accesible | [TPS61088 módulo](https://www.aliexpress.com/w/wholesale-tps61088-module.html) |
| 1 opcional | Medidor de batería | MAX17048 compatible 1S, lógica I²C 3,3 V | [MAX17048 módulo](https://www.aliexpress.com/w/wholesale-max17048-module.html) |
| 1 | Segundo DAC PCM5102A | Mismo modelo/configuración del primero; total dos | [PCM5102A I2S](https://www.aliexpress.com/w/wholesale-pcm5102a-i2s.html) |
| 2 | Jacks de audio adicionales | Mono 6,35 mm para AUX1/AUX2; total cuatro si MAIN usa dos mono | [Jack mono de panel](https://www.aliexpress.com/w/wholesale-6.35mm-mono-panel-jack.html) |
| 1 | Jack CLOCK IN | TS 3,5 mm de panel, con identificación clara | [Jack TS 3,5 mm](https://www.aliexpress.com/w/wholesale-3.5mm-mono-panel-jack.html) |
| 1 | Acondicionador de sync | LMV331 en breakout/PCB, con protección, divisor, histéresis y pull-up a 3,3 V; requiere circuito calculado | [LMV331](https://www.aliexpress.com/w/wholesale-lmv331.html) |
| 1 lote | Pasivos de sync/alimentación | Resistencias, condensadores, diodos de protección y conectores según esquema definitivo | [Componentes electrónicos](https://www.aliexpress.com/w/wholesale-electronic-components-kit.html) |
| 1 | Cable sync | TS 3,5 mm macho-macho si coincide con el aparato elegido | [Cable mono TS 3,5 mm](https://www.aliexpress.com/w/wholesale-3.5mm-ts-mono-cable.html) |

Prever montaje aislado para la batería, sujeción sin aplastarla, acceso de servicio y separación física entre boost/cargador y rutas de audio. El coste de esta ampliación ya se integra en el presupuesto consolidado del apartado 10; no añadir otra reserva de 40–90 € encima del total.

### Validación adicional antes de cerrar la PCB

Comprobar reproducción y cuatro salidas simultáneas durante carga, desconexión de USB sin reinicio, autonomía real, temperatura de carga y ruido del boost en silencio. Probar apagado con batería baja antes de que actúe su protección. Verificar nivel de cada jack, alineación entre DACs y routing/mutes. Probar sync con cada modelo real de sinte y documentar cable, tensión, polaridad y PPQN aceptados.


## 10. Compra económica por packs y presupuesto consolidado

Revisión del 29 de octubre de 2026. Configuración presupuestada: **OLED azul 0,96, 16 pads digitales hechos con pulsadores simples, seis botones de función, ocho potenciómetros, 20 LEDs rojos, batería 1S, cuatro canales de línea y CLOCK IN**. Los packs incluyen repuestos. No se compra la TFT anterior ni otro pack para cada tipo de botón/LED.

### Pantalla y packs localizados

Se han encontrado referencias de anuncios en índices de terceros. Los enlaces «AliExpress» apuntan al identificador publicado, pero **no se ha podido comprobar la ficha ni el precio final en AliExpress**. Las cantidades/variantes deben revisarse al abrirlas; no se recomienda un vendedor por aparecer en esta tabla. Los precios indexados están en USD, son orientativos y pueden corresponder a otra variante; no se han convertido a EUR para simular un precio de compra.

| Compra | Uso y variante exacta | Referencia de compra | Evidencia y precio observado fuera del carrito |
|---|---|---|---|
| 1 OLED 0,96 | 128×64, azul uniforme, sin táctil, preferible I²C de 4 pines; seleccionar controlador SSD1306 compatible | [AliExpress, anuncio 1005006195241193](https://www.aliexpress.com/item/1005006195241193.html) | [Pricearchive](https://www.pricearchive.org/aliexpress.com/item/1005006195241193): título con variantes I²C/SPI y colores; 1,15 USD registrado el 09/09/2026, no precio confirmado de la variante azul |
| 1 pack de 10 potenciómetros | WH148, **B10K lineal, tres terminales**, eje 6 mm, misma longitud; ocho en uso y dos de repuesto | [AliExpress, anuncio 1005010736369918](https://www.aliexpress.com/item/1005010736369918.html) | [Pricearchive](https://ms.pricearchive.org/aliexpress.com/item/1005010736369918): título de pack 10 con variantes de 3/6 pines y valores; precio indexado 1,51 USD, variante sin confirmar |
| 1 pack de 50 pulsadores | Momentáneos NO, **6×6 mm**, altura igual en todo el lote; 22 en uso y 28 de repuesto | [AliExpress, anuncio 32842271522](https://www.aliexpress.com/item/32842271522.html) | [Alitools](https://alitools.io/en/showcase/50pcs-6-6x4-3mm-6x6x5mm-dip-tactile-tact-mini-push-button-switch-micro-switch-momentary-dip-4-6x6x6-6x6x7-6x6x8-6x6x9-6x6x10-32842271522): identifica lote de 50 y variantes de altura; registro antiguo, no usar su precio como oferta vigente |
| 1 pack de 100 LEDs | **Rojos, 3 mm, difusos, de dos patas, sin parpadeo**; 20 en uso y 80 de repuesto | [Buscar pack de 100 rojos en AliExpress](https://www.aliexpress.com/w/wholesale-100pcs-3mm-red-diffused-led.html) | [Índice de Pricearchive](https://www.pricearchive.org/search/aliexpress.com/3mm-red-led-lot/1): encuentra lote de 100 multivariante de ENLINCA a 0,75 USD, con datos del 27/10/2026; rojo/difuso y precio de esa variante no confirmados |
| 1 pack de 10 perillas | Eje 6 mm y fijación compatible; ocho en uso y dos de repuesto | [Buscar 10 perillas](https://www.aliexpress.com/w/wholesale-10pcs-potentiometer-knob-6mm.html) | Solo búsqueda y reserva de presupuesto, sin anuncio/precio verificado |
| 1 pack de 25 capuchones o actuadores | Compatibles con el pulsador de 6×6 elegido; 22 en uso | [Buscar capuchones compatibles](https://www.aliexpress.com/w/wholesale-6x6-tactile-switch-cap.html) | Solo búsqueda; no todos los switches de 6×6 admiten capuchón, confirmar ajuste o imprimir actuadores |

**Selección práctica:** reservar 4–7 € para los diez potenciómetros, 2–4 € para los 50 pulsadores y 1–3 € para los 100 LEDs; son asignaciones propias en euros, no ofertas verificadas. No comprar surtido de resistencias de potenciómetro si no contiene ocho B10K. No seleccionar A10K logarítmicos, encoders ni trimmers de ajuste como sustituto de estas ocho perillas.

Los pulsadores 6×6 son baratos, pero pequeños y de clic marcado. Sirven para probar el secuenciador, no ofrecen el tacto de un pad de goma ni sensibilidad a velocidad. Fijarlos a placa y accionar mediante tapas/actuadores, sin cargar los terminales con esfuerzo del panel. Sus cuatro patas suelen ser dos pares unidos internamente: comprobar con multímetro antes de cablear. Si se prioriza tocar percusión con los dedos, presupuestar después botones más grandes; los capuchones no convierten estos contactos en pads sensibles.

La pantalla de 1,3 pulgadas puede buscarse como [SH1106 azul 128×64 sin táctil](https://www.aliexpress.com/w/wholesale-1.3-oled-sh1106-blue-i2c.html). Reservar 4–6 € para genérica; comprobar variante azul porque abundan las blancas. La referencia oficial [Waveshare 1,3 azul](https://www.waveshare.com/product/ai/displays/oled/1.3inch-oled-b.htm) muestra 7,49 USD y la [Waveshare 0,96](https://www.waveshare.com/product/displays/0.96inch-oled-module-c.htm) 3,49 USD en sus páginas, sin portes ni validación de carrito. Esas cifras son del fabricante y **no precios de AliExpress**.

### Presupuesto por partidas en euros

**Estimación de planificación**, sin carrito verificado. Cada rango es el coste total de la fila, con las cantidades indicadas; en particular, potenciómetros, botones y LEDs se cobran por pack completo. Las reservas amplias para módulos incluyen la posibilidad de elegir un breakout documentado; no presuponen que el chip suelto sirva directamente. Las partidas del sistema base y ampliación se cuentan una sola vez.

| Partida | Cantidad comprada | Reserva mínima € | Reserva máxima € |
|---|---:|---:|---:|
| ESP32-S3 N16R8 | 1 placa | 8 | 12 |
| MCP3208 | 1 módulo o IC + adaptador | 4 | 7 |
| MCP23017 | 2 módulos | 4 | 8 |
| TLC5947 | 1 módulo | 5 | 10 |
| OLED azul 0,96 | 1 módulo | 2 | 4 |
| PCM5102A | 2 módulos, cuatro canales | 6 | 10 |
| Lector microSD SPI | 1 | 1 | 3 |
| Tarjeta microSD | 1 | 5 | 8 |
| Potenciómetros B10K | 1 pack de 10 | 4 | 7 |
| Perillas | 1 pack de 10 | 2 | 4 |
| Pulsadores simples | 1 pack de 50 | 2 | 4 |
| Capuchones/actuadores | 1 pack de al menos 25 | 2 | 5 |
| LEDs rojos difusos | 1 pack de 100 | 1 | 3 |
| Interruptor mantenido | 1 | 1 | 2 |
| Jacks de audio + CLOCK IN | 4 mono de línea + 1 TS de sync | 4 | 7 |
| Acondicionamiento CLOCK IN | 1 circuito | 2 | 4 |
| LiPo protegida 2.000 mAh | 1 | 8 | 14 |
| Cargador con power-path | 1 módulo BQ24074 | 5 | 10 |
| Boost 5 V | 1 módulo | 3 | 6 |
| Regulación 3,3 V | 1 módulo | 2 | 4 |
| Corte seguro y HOLD | 1 conjunto | 4 | 8 |
| Medidor de batería MAX17048 | 1 módulo opcional, incluido aquí | 2 | 4 |
| Pasivos y desacoplos adicionales | 1 lote | 4 | 7 |
| Placa perforada, cable, conectores y fijaciones | 1 lote | 7 | 12 |
| Caja de prototipo | 1 | 8 | 15 |
| Alimentador 5 V y cable USB | 1 conjunto | 5 | 9 |
| **Subtotal de componentes** | | **101** | **187** |
| Reserva de portes y ajustes de impuestos al pagar | | 10 | 20 |
| Margen para imprevistos/adaptadores | | 10 | 20 |
| **Reserva total de planificación** | | **121** | **227** |

Como cifra de trabajo, apartaría **unos 170 €**, con un techo inicial de unos **230 €**, comprando todo nuevo. El importe entregado con IVA solo se puede fijar revisando el carrito, origen y condiciones: la reserva de 10–20 € no sustituye ese cálculo. No se aplican cupones de bienvenida ni precios promocionales a este presupuesto.

Con la OLED de 1,3 pulgadas, el rango de componentes sería 103–189 € y la reserva total 123–229 €. Omitir el medidor opcional resta 2–4 €. Reutilizar componentes reduce las filas correspondientes, pero no descontar a la vez un pack completo si sigue siendo necesario comprarlo para cubrir las unidades restantes.

No incluye herramientas, soldadura profesional, PCB propia, envío de fabricación, auriculares/amplificador, MIDI, grabación de audio ni mano de obra de desarrollo. El presupuesto sigue sujeto a validar los módulos de alimentación, corte y sync antes de comprar: esos circuitos necesitan esquema, no solo un título comercial.

## 11. Alternativa: matriz 4×4 con un LED dentro de cada botón

La referencia aportada por el usuario, [AliExpress 1005009899149476](https://es.aliexpress.com/item/1005009899149476.html), aparece en [Pricearchive](https://uk.pricearchive.org/aliexpress.com/item/1005009899149476) como matriz de 16 pulsadores. No se ha podido abrir la ficha de AliExpress para confirmar fotos, esquema o variantes.

### Opción localizada: Trellis monocroma 4×4

**Corrección:** el anuncio de AliExpress recomendado anteriormente no está disponible según la comprobación del usuario. Se retira como opción de compra. Procedía de un índice de terceros y no debía presentarse como enlace de compra vigente. No se ha encontrado un sustituto verificable en AliExpress para este conjunto.

Fichas accesibles consultadas:

- [Placa Trellis 1616 en Adafruit](https://www.adafruit.com/product/1616): ficha oficial, pero figura sin stock en la consulta.
- [Placa Trellis 1616 en Opencircuit España](https://opencircuit.es/producto/adafruit-trellis-monochrome-driver-pcb-4x4): ficha accesible de distribuidor; confirmar precio, disponibilidad y plazo al pedir.
- [Botonera de silicona 1611 en Adafruit](https://www.adafruit.com/product/1611): figura a 4,95 USD y con stock en la consulta; no incluye placa ni LEDs. Portes e impuestos aparte.

Estos enlaces no son de AliExpress y no equivalen a un kit completo.

El sistema de referencia [Adafruit Trellis 1616](https://www.adafruit.com/trellis) ofrece 16 posiciones de pulsación y 16 posiciones para LEDs de 3 mm, una bajo cada botón, con control individual de encendido/apagado por I²C. Usa una cubierta de silicona translúcida 4×4; se pueden montar LEDs rojos. La placa original se vende sin silicona ni LEDs y permite varias pulsaciones sin ghosting. Estas características describen el original; cualquier alternativa compatible necesita su propia comprobación.

Para tener el panel completo hacen falta:

- Una placa controladora Trellis monocroma 1616 o compatible con esquema documentado.
- Una cubierta de silicona 4×4 tipo 1611, mecánicamente compatible.
- Dieciséis LEDs rojos difusos de 3 mm, soldadura y fijación mecánica.
- Adaptación de nivel I²C si corresponde a la placa elegida. La [conexión original documentada](https://learn.adafruit.com/adafruit-trellis-diy-open-source-led-keypad/connecting) alimenta Trellis a 5 V: revisar sus pull-ups y añadir conversión bidireccional 3,3↔5 V si el bus queda elevado a 5 V. No conectar esas señales directamente al ESP32/Pico.

Búsqueda alternativa: [Trellis 4×4 monocroma con silicona](https://www.aliexpress.com/w/wholesale-trellis-4x4-keypad-led.html). Evitar confundirla con NeoTrellis RGB si el objetivo es mantener LEDs rojos y coste reducido.

**Presupuesto de reserva del panel completo: 20–30 €**, incluyendo placa, silicona, LEDs y adaptación de nivel sencilla; estimación propia, no oferta de compra verificada. No sumar esta reserva sin retirar las piezas que sustituya. El presupuesto del apartado 10 sigue correspondiendo a pulsadores simples hasta confirmar la compra de esta alternativa.

### Integración en el sampler

Trellis puede encargarse de los 16 pads y sus 16 LEDs sin una Pico adicional. Sustituye el MCP23017 dedicado a pads y las 16 salidas del TLC5947 usadas para ellos. Los seis botones de función y los cuatro LEDs de página siguen necesitando conexiones: se puede conservar el segundo MCP23017 y estudiar controlar esos cuatro LEDs con resistencias, verificando corriente por pin y total; después podría retirarse el TLC5947. No eliminar ambos módulos del presupuesto sin rediseñar esa parte.

Los LEDs deben seguir el patrón, mute o posición ordenados por el ESP32, independientemente de si el botón está pulsado. Para distinguir cursor y pasos, usar parpadeo; no asumir brillo individual PWM en la Trellis monocroma. Los botones de silicona no proporcionan sensibilidad a velocidad.

### Opciones que no equivalen a lo solicitado

Se han encontrado [matrices anunciadas con 16 teclas y ocho LEDs](https://es.pricearchive.org/aliexpress.com/item/1005012161446649): no ofrecen un LED por tecla. Tampoco basta que un módulo diga «with LED» si solo indica alimentación. Comprobar 16 LEDs, control individual y ubicación bajo cada botón antes de elegir.

La alternativa de menor coste puede seguir siendo una PCB propia con pulsadores simples y un LED rojo al lado de cada uno. Para iluminación dentro de la tecla, necesita botones translúcidos o pulsadores iluminados con terminales independientes de contacto y LED; no se ha verificado un kit 4×4 de ese tipo a precio comparable a la matriz básica del enlace.
