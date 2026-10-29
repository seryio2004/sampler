Copia aqui tus archivos WAV o MP3, o crea subcarpetas con tus kits.
Maximo 30 segundos por archivo. Los originales nunca se modifican.
Tambien puedes iniciar: ./sampler-c --samples /ruta/a/tu/carpeta

Se incluye tr808/: 16 muestras WAV gratuitas (CC0) de Michael Fischer.
Licencia, origen y hashes: tr808/README.md y tr808/manifest.json.
Prueba el kit desde la raiz del proyecto, con los efectos desactivados:
./sampler-c --samples ./samples --state kits/tr808.json --no-save

Se incluye raros/: 12 texturas y sonidos experimentales sintetizados, sin bateria (CC0).
Descripcion, licencia y hashes: raros/README.md y raros/manifest.json.
Kit de nueve sonidos, sin patrones ni efectos:
./sampler-c --samples ./samples --state kits/raros.json --no-save
Anade --arduino /dev/ttyACM0 para tocar tambien con los tres pads superiores.

Se incluye house/: 16 one-shots originales para house (CC0), bateria, bajos y acordes.
Kit con patron a 124 BPM en Do menor: kits/house.json.
Mismos sonidos con patrones vacios: kits/house_live.json.
./sampler-c --samples ./samples --state kits/house.json --no-save --arduino /dev/ttyACM0
Descripcion, notas y hashes: house/README.md y house/manifest.json.
