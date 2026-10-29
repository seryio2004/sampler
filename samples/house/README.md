# House: batería, bajo y acordes

Banco original de **16 one-shots sintetizados**, con un kit de nueve pads pensado para house y deep house. No utiliza grabaciones ni samples descargados. Se distribuye con [CC0 1.0](LICENSE-CC0.txt).

WAV PCM de 16 bits, mono, 44.100 Hz; pico máximo de 86 % (aprox. −1,3 dBFS), eliminación de continua y fundidos en los extremos. Las muestras son golpes individuales y stabs: el patrón lo ejecuta el sampler.

El preset de ejemplo está a **124 BPM, en Do menor**, con bombo a negras, palmas en los tiempos 2 y 4, charles a contratiempo, bajo sincopado y acordes Cm7. El bajo del ejemplo es la muestra del pad 7; el patrón del bajo sintetizado del sampler queda vacío.

| Pad inicial | Archivo | Duración | Sonido |
|---|---|---|---|
| 1 | 01_kick_house.wav | 0.560 s | Bombo redondo con subgrave y ataque corto |
| 2 | 02_clap_house.wav | 0.300 s | Palma de varias capas con cola filtrada |
| 3 | 03_hat_cerrado.wav | 0.105 s | Charles cerrado brillante |
| 4 | 04_hat_abierto.wav | 0.320 s | Charles abierto para contratiempos |
| 5 | 05_shaker.wav | 0.140 s | Shaker fino para semicorcheas |
| 6 | 06_rim_house.wav | 0.130 s | Rim corto y seco para sincopas |
| 7 | 07_bajo_C.wav | 0.290 s | Bajo sintetico redondo en Do (C2) |
| 8 | 08_acorde_Cm7.wav | 0.700 s | Acorde menor septima calido (C3 Eb3 G3 Bb3) |
| 9 | 09_fx_sweep.wav | 0.480 s | Barrido de ruido ascendente corto |
| FILES | 10_caja_house.wav | 0.280 s | Caja con cuerpo tonal y ruido |
| FILES | 11_ride.wav | 0.800 s | Ride metalico con cola suave |
| FILES | 12_organo_Fm.wav | 0.340 s | Stab de organo en Fa menor (F3 Ab3 C4) |
| FILES | 13_bajo_G.wav | 0.290 s | Bajo sintetico en Sol (G2) |
| FILES | 14_acorde_Abmaj7.wav | 0.700 s | Acorde mayor septima para variar la armonia (Ab3 C4 Eb4 G4) |
| FILES | 15_fx_caida.wav | 0.850 s | Caida de ruido filtrado para transiciones |
| FILES | 16_vocal_sintetica.wav | 0.420 s | Vocal sintetica tipo ah; sin voz grabada (C4) |

## Reproducir el patrón de ejemplo

Desde la raíz del proyecto en la Raspberry:

```bash
./sampler-c --samples ./samples --state kits/house.json --no-save --arduino /dev/ttyACM0
```

El patrón comienza al arrancar. Los efectos están desactivados para escuchar el banco limpio. Los pads inferiores son 1–6; la fila superior, de izquierda a derecha, es bajo C, acorde Cm7 y sweep. Las siete muestras adicionales se cargan desde SAMPLES / FILES > house.

Sin Arduino, omite `--arduino /dev/ttyACM0`: las nueve pistas del patrón siguen sonando, mientras la fila superior vuelve a controlar la navegación y los seis pads inferiores tocan sus sonidos.

Para tocar desde cero con los mismos sonidos y los patrones vacíos:

```bash
./sampler-c --samples ./samples --state kits/house_live.json --no-save --arduino /dev/ttyACM0
```

`--no-save` protege ambos presets. Para guardar tu versión:

```bash
cp kits/house.json kits/mi_house.json
./sampler-c --samples ./samples --state kits/mi_house.json --arduino /dev/ttyACM0
```

Las rutas de las muestras son relativas a la raíz del proyecto. En DIRECTO, activa el acompañamiento con OK si quieres que continúe el patrón mientras tocas; en TEMPO, OK cambia PLAY/STOP. A1 elige la pista que editas en PATRONES; NAV elige el paso y OK lo añade/quita.

Para variar la armonía, prueba bajo G, órgano Fm o acorde Abmaj7 desde FILES. Son muestras afinadas; cambiar el BPM modifica los disparos, no su tono ni duración. La función BAJO del sampler sigue utilizando su sintetizador independiente.

## Origen y regeneración

[manifest.json](manifest.json) documenta notas, duraciones y SHA-256 de cada archivo. El generador solo necesita Python 3 y su biblioteca estándar:

```bash
python3 scripts/generar_house.py
```

Los WAV ya están incluidos: no hace falta regenerarlos ni recompilar el sampler para utilizar este kit. También se admite `--output /otra/carpeta`. Las semillas son fijas; pueden existir pequeñas diferencias de redondeo entre plataformas.
