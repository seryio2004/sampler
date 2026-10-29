CC ?= cc
CXX ?= c++
CXXFLAGS ?= -O2
PKGS = alsa sndfile libmpg123 json-c samplerate
CPPFLAGS += $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
CFLAGS ?= -O3
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -pthread
LDLIBS += $(shell pkg-config --libs $(PKGS) 2>/dev/null) -lm -pthread
SOURCES = src/main.c src/core.c src/effects.c src/files.c src/hardware.c src/arduino.c

.PHONY: all clean test check-deps test-core test-startup
all: sampler-c
check-deps:
	@pkg-config --exists $(PKGS) || { echo "Instala: sudo apt install build-essential pkg-config libasound2-dev libsndfile1-dev libmpg123-dev libjson-c-dev libsamplerate0-dev"; exit 1; }
sampler-c: Makefile $(SOURCES) src/core.h src/files.h src/hardware.h src/arduino.h src/effects.h | check-deps
	@set -e; target_tmp="$@.tmp.$$$$"; trap 'rm -f "$$target_tmp"' EXIT HUP INT TERM; \
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$$target_tmp" $(SOURCES) $(LDLIBS); \
	mv -f "$$target_tmp" "$@"
build/test-scenes: tests/test_scenes.c src/core.c src/effects.c src/core.h src/effects.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_scenes.c src/core.c src/effects.c -lm
build/test-core: tests/test_core.c src/core.c src/effects.c src/effects.h src/core.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_core.c src/core.c src/effects.c -lm
build/test-hardware: tests/test_hardware.c src/hardware.c src/hardware.h src/core.c src/effects.c src/effects.h src/core.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_hardware.c src/hardware.c src/core.c src/effects.c -lm -pthread
build/test-arduino: tests/test_arduino.c src/arduino.c src/arduino.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_arduino.c src/arduino.c
build/test-sketch: tests/test_sketch.cpp tests/arduino_mock/Arduino.h arduino/sampler_pot/sampler_pot.ino
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++11 -Wall -Wextra -Wpedantic $(LDFLAGS) -Itests/arduino_mock -o $@ tests/test_sketch.cpp
build/test-effects: tests/test_effects.c src/effects.c src/effects.h src/core.c src/core.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_effects.c src/core.c src/effects.c -lm
build/test-tribe64: tests/test_tribe64.c src/core.c src/effects.c src/hardware.c src/core.h src/hardware.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_tribe64.c src/core.c src/effects.c src/hardware.c -lm -pthread
build/test-sound: tests/test_sound.c src/core.c src/effects.c src/core.h src/effects.h
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_sound.c src/core.c src/effects.c -lm
test-core: build/test-sound build/test-tribe64 build/test-scenes build/test-effects build/test-core build/test-hardware build/test-arduino build/test-sketch
	./build/test-sound
	./build/test-tribe64
	./build/test-scenes
	./build/test-core
	./build/test-effects
	./build/test-hardware
	./build/test-arduino
	./build/test-sketch
build/test-files: tests/test_files.c src/files.c src/core.c src/effects.c src/effects.h src/files.h src/core.h | check-deps
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_files.c src/files.c src/core.c src/effects.c $(LDLIBS)
build/test-alsa.so: tests/fake_alsa.c src/core.h | check-deps
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -fPIC -shared -o $@ tests/fake_alsa.c -ldl
build/test-gpio.so: tests/fake_gpio.c
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o $@ tests/fake_gpio.c
build/test-controls: tests/test_controls.c $(SOURCES) src/core.h src/effects.h src/arduino.h | check-deps
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test_controls.c src/core.c src/effects.c src/files.c src/hardware.c src/arduino.c $(LDLIBS)
test-startup:
	python3 tests/test_startup.py
test: test-startup test-core build/test-controls build/test-files build/test-alsa.so build/test-gpio.so sampler-c
	./build/test-files
	./build/test-controls
	python3 tests/integration_hdmi.py
	python3 tests/integration_tribe64.py
	python3 tests/integration_c.py
	python3 tests/integration_arduino.py
	python3 tests/integration_controls.py
	python3 tests/integration_audio.py
	python3 tests/integration_leds.py
clean:
	rm -f sampler-c
	rm -rf build
