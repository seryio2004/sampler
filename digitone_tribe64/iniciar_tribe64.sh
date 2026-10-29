#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
python3 ./scripts/asegurar_motor.py
if [ ! -e ./tribe64_state.json ]; then cp kits/tribe64.json tribe64_state.json; fi
exec ./sampler-c --samples ./samples --state ./tribe64_state.json "$@"
