#!/usr/bin/env python3
"""Shared native-engine bootstrap for both launchers."""
from pathlib import Path
import sys

PROJECT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PROJECT))
from hdmi_ui.build import BuildError, ensure_engine

try:
    ensure_engine(PROJECT)
except (BuildError,OSError) as exc:
    print(f'Motor: {exc}',file=sys.stderr)
    raise SystemExit(1)
