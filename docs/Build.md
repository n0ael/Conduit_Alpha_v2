# Build, Defines & Test-Workflow — Dossier
> Ausgelagert aus CLAUDE.md v4.7 §13.2/§13.3, Juli 2026. Für
> Build-/CI-Arbeiten verbindlich wie die CLAUDE.md selbst (§1.1).
> Compiler-/Dependency-Regeln (C++20, FetchContent, Werror) und die
> Test-Pflichten stehen weiterhin in CLAUDE.md 13.1/13.4.

## 13.1 Compiler & Abhängigkeiten (Langfassung, Kern in CLAUDE.md 13.1)

- **C++ Standard:** Strikt C++20 — `set(CMAKE_CXX_STANDARD 20)`,
  `set(CMAKE_CXX_STANDARD_REQUIRED ON)`.
- **JUCE:** Minimum JUCE 8.0.0, via CMake `FetchContent` (kein Submodule,
  kein System-Install). **Ableton Link:** via `FetchContent` (header-only).
- **Warnungen als Fehler:** `-Wall -Wextra -Werror` (GCC/Clang), `/W4 /WX`
  (MSVC). Werror gilt per `set_source_files_properties` nur auf
  Conduit-eigenen Quelldateien — JUCE-Sources im selben Target bleiben
  Werror-frei (begründet in den CMakeLists).
- Drittes/viertes Target: `ConduitAirwindows` (Static Lib,
  `add_subdirectory Source/DSP/Airwindows`, linkt nur `juce_audio_basics`;
  Include-Root-Konvention `DSP/Airwindows/...`) + `ConduitAirwindowsTests`.

## C++-Kompilierfallen (ausgelagert aus CLAUDE.md v5.7 §2)

- Verschachtelte `Config`-Structs mit In-Class-Defaults nicht direkt als
  Default-Argument (`Klasse(const Config& = {})`) — Clang lehnt das ab.
  Stattdessen delegierender Default-Ctor in der .cpp:
  `Klasse() : Klasse(Config{}) {}`.
- `juce::Point` braucht `<juce_graphics/juce_graphics.h>` (nicht nur
  `juce_core`) — sonst kaskadierende „Funktion akzeptiert keine 2
  Argumente"-Fehler.

## 13.2 Preprocessor Defines (RT Safety Guardrails)

```cmake
target_compile_definitions(${PROJECT_NAME} PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=0    # verhindert blockierende Modal-Loops im Message Thread
    JUCE_WEB_BROWSER=0              # keine unnötigen Abhängigkeiten
    JUCE_USE_CURL=0
)

# Plattform-conditional — NICHT global setzen:
if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_USE_CORE_AUDIO=1)
elseif(WIN32)
    # ASIO erfordert Steinberg ASIO SDK (separater Download + Lizenz, nicht in JUCE!)
    # SDK-Pfad via JUCE_ASIO_SDK_PATH, erst dann:
    target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_ASIO=1)
endif()
```

## 13.3 Quick Start & Build-Workflow

```bash
# Configure (Windows — auf diesem System ist VS 2026 installiert, kein VS 2022)
cmake -B build -G "Visual Studio 18 2026" -A x64
# Configure (Ninja, alle Plattformen)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Test: Standalone-Target aus build/ ausführen

# Sanitizer-Presets (CLAUDE.md 13.4):
cmake --preset asan && cmake --build --preset asan   # ASan (MSVC) — läuft lokal unter Windows
cmake --preset tsan && cmake --build --preset tsan   # TSan (Clang) — NUR Linux/macOS/WSL,
                                                     # unter Windows nicht verfügbar
# TSan + ASan laufen außerdem automatisch in GitHub Actions (Ubuntu) bei jedem
# Push auf master — .github/workflows/ci.yml ('tsan' + 'asan-linux' Presets)
```

- TSan/ASan-Builds laufen mit Dummy-Audio-Device — kein ASIO nötig.

## 9. Plattform-Matrix (ausgelagert aus CLAUDE.md v5.7 §9)

| Plattform | Priorität | Audio-Backend | Besonderheit |
|---|---|---|---|
| Windows | Primary | ASIO | Dev/DAW |
| macOS | Primary | CoreAudio | Dev/DAW, 32 Samples problemlos |
| Linux Desktop | Secondary | JACK/PipeWire | FireWire-Revival (Altgeräte-Reaktivierung) als Option offengehalten |
| Linux Kiosk (LinkBox) | Secondary | JACK / PipeWire | PREEMPT_RT, Fullscreen |
| iOS | Secondary | CoreAudio Remote I/O | Touch-first; nativer Build validiert 17.07.2026 (iPad Pro A12X: ~1 % idle, 5–6 % Peak); Distribution erfordert Developer-Account + Apple-Silicon-Dev-Hardware |

**Plattform-Scope-Regel** (bleibt app-weit in CLAUDE.md §9): Kein
plattformspezifischer DSP- oder UI-Code. Plattform-spezifisches Setup in
`initAudio()`, im Fenster-/Input-Setup (Fullscreen, Edge-Gesten-
Unterdrückung, Touch-Feedback — ADR 008 Performance-Modus) und in CMake
ist explizit erlaubt.

## 9.1 macOS CoreAudio (ausgelagert aus CLAUDE.md v4.8 §9.1)

- `juce_add_gui_app` mit `BUNDLE_ID` und `JUCE_USE_CORE_AUDIO=1`
- `AudioDeviceManager.setAudioDeviceSetup()` — sampleRate 48000, bufferSize 128
- Tatsächliche Buffer-Size nach Setup abfragen — Hardware kann Minimum erzwingen
- `initAudio()` reagiert defensiv auf abweichende Werte, kein Crash,
  Abweichung in ValueTree-Property `audioSetupWarning` speichern

## 9.2 Linux Kiosk-Mode / LinkBox (ausgelagert aus CLAUDE.md v4.8 §9.2)

- App startet fullscreen/borderless, kein Window Manager nötig
- Cursor ausblenden wenn Touch aktiv
- PREEMPT_RT: keine RT-inkompatiblen Kernel-Calls im Audio Thread
- Touchscreen-Kalibrierung beim Start prüfen (`xinput set-prop`)
