# Conduit

Modular audio/CV instrument — free node-based patching, comparable to a hardware
modular synthesizer. Built with C++20 and JUCE 8.

> **Status: Alpha** (current: `v3.0-alpha`) — under active development.
> APIs, patch format and UI are subject to change without notice.

## Features

- **Node-based patch canvas** — modules are freely placed and cabled;
  glitch-free graph swaps (fade-out → topology change → fade-in), full
  undo/redo for every patchable action
- **Ableton Link** — tempo sync, start/stop sync, launch quantization,
  Link Audio send (stream module outputs to Link peers)
- **OSC integration** — bidirectional parameter control
  (`/conduit/{type}/{id}/{param}`), snapshot-diff feedback to clients,
  Max for Live announce protocol for remote modules
- **FX chassis** — every audio effect gets I/O gain stages, level meters,
  one CV input per DSP parameter, control linking and per-parameter
  response curves for free
- **Retro looper** — Endlesss-style always-on capture with bar-accurate,
  phase-locked commit and playback
- **CV hardware support** — DC-coupled interface calibration
  (Expert Sleepers ES series), 1V/oct tuning, per-channel latency trim
- **Touch-first UI** — Push-3-inspired design system, fully mouse/keyboard
  compatible

## Tech stack

C++20 · JUCE 8 (native components, no ImGui) · CMake · Ableton Link SDK ·
Catch2 v3

Real-time audio path is lock-free and allocation-free; ThreadSanitizer and
AddressSanitizer runs are part of CI.

## Building

```bash
# Configure (Windows / Visual Studio)
cmake -B build -G "Visual Studio 18 2026" -A x64

# Configure (Ninja, all platforms)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug
```

Dependencies (JUCE, Ableton Link, Catch2) are fetched automatically via CMake
`FetchContent`. On Windows, ASIO support requires the Steinberg ASIO SDK
(set `JUCE_ASIO_SDK_PATH`); without it the build falls back to
WASAPI/DirectSound.

### Tests

```bash
cmake --build build --config Debug --target ConduitTests
build/ConduitTests_artefacts/Debug/ConduitTests
```

Sanitizer presets: `cmake --preset asan` (Windows/MSVC) and
`cmake --preset tsan` (Clang, Linux/macOS only).

## Platforms

| Platform | Backend | Status |
|---|---|---|
| Windows | ASIO | primary |
| macOS | CoreAudio | primary |
| Linux (desktop / kiosk) | JACK / PipeWire | secondary |
