# Kit TR-808 gratuito

16 WAV originales del banco **Roland TR-808 Rhythm Composer Sound Sample Set 1.0.0** de **Michael Fischer / Technopolis**, distribuido por TidalCycles con licencia **CC0 1.0 Universal**.

- Repositorio: https://github.com/tidalcycles/sounds-tr808-fischer
- Revisión descargada: `85fbecf1bec32553395625ea659e2a56dfd7c0e1`
- Licencia completa: [LICENSE-CC0.txt](LICENSE-CC0.txt)
- Origen exacto y SHA-256 de cada archivo: [manifest.json](manifest.json)

Los archivos se han descargado, comprobado contra los hashes Git del repositorio y renombrado. Los bytes de audio no se han modificado. Son WAV PCM de 16 bits, mono, 44.100 Hz; el conjunto ocupa aproximadamente 1,24 MiB. Cada muestra dura entre 0,25 y 2,51 segundos.

| Archivo | Sonido / destino inicial |
|---|---|
| 01_bombo.wav | PAD 1: bombo corto |
| 02_caja.wav | PAD 2: caja |
| 03_hat_cerrado.wav | PAD 3: charles cerrado |
| 04_hat_abierto.wav | PAD 4: charles abierto |
| 05_palmas.wav | PAD 5: palmas |
| 06_tom.wav | PAD 6: tom medio |
| 07_bombo_largo.wav | Bombo largo |
| 08_bombo_grave.wav | Otro bombo |
| 09_caja_seca.wav | Otra caja |
| 10_cencerro.wav | Cencerro |
| 11_rim.wav | Rim shot |
| 12_claves.wav | Claves |
| 13_maracas.wav | Maracas |
| 14_conga.wav | Conga grave |
| 15_tom_alto.wav | Tom agudo |
| 16_platillo.wav | Platillo |

## Escuchar el kit sin efectos

Desde la raíz del proyecto en la Raspberry:

```bash
./sampler-c --samples ./samples --state kits/tr808.json --no-save
```

Añade `--arduino /dev/ttyACM0` si utilizas el mando del UNO. El preset carga los primeros seis WAV, mantiene el patrón de batería inicial, deja el bajo sin notas y desactiva los efectos, con volumen al 60 %. `--no-save` permite probarlo sin modificar el preset ni los patrones habituales. Este preset contiene rutas relativas a la raíz del proyecto: arranca desde esa carpeta.

Los otros diez sonidos se asignan desde FILES > tr808. Para guardar tus cambios, copia primero `kits/tr808.json` a otro archivo y utiliza esa copia con `--state` sin `--no-save`.

## Comprobar el ruido

Estos WAV son grabaciones de una caja de ritmos; la caja, los charles, las palmas y las maracas incluyen componentes de ruido propios del instrumento. Descargar muestras no confirma ni corrige por sí solo una capa de ruido añadida a toda la salida.

Con el sampler cerrado, reproduce un bombo directamente:

```bash
aplay samples/tr808/01_bombo.wav
```

Si también aparece la misma capa de ruido, se reproduce fuera del motor del sampler. Si `aplay` suena limpio, prueba el preset anterior y compara también con `--headless` para ejecutar sin LCD, botones ni barrido de LED. Al cerrar el sampler, anota el número de `xruns` que imprime: indica recuperaciones del dispositivo de audio por interrupciones, no una medida de ruido analógico. Si elegiste una salida concreta en el sampler, usa la misma con `aplay -D NOMBRE_ALSA` para que la comparación sea útil.
