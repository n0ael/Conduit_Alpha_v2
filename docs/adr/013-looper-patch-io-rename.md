# ADR 013 — Looper patch IN/OUT: Umbenennung + Entfall des Mini-Out

Status: Akzeptiert (19.07.2026) · Amendment zu ADR 010/012.

## Kontext

Seit ADR 012 existierten ZWEI Looper-Ausgangsmodule nebeneinander:
`looper_big_out` („Looper Out", Auto-Follow) und das kompakte
`looper_out` („Looper Out Mini", manuelle Abgriffe Pre/Post × Mono-Modi).
User-Feedback 19.07.2026: zwei Out-Module sind verwirrend, und die
Doppelbelegung erzwang den unglücklichen Namen „Looper Out Mini".

## Entscheidungen

1. **`looper_out` („Looper Out Mini") entfällt ERSATZLOS** — Modul,
   Panel, GraphManager-Slot-API (`addLooperOutSlot`/`removeLooperOutSlot`/
   `setLooperOutSlotPre`) und der nur vom Mini genutzte
   `outputPre`/`outputTarget`/`outputMode`-Property-Listener-Zweig sind
   gelöscht. Bewusster Funktionsverlust: Pre-Fader-Einzelabgriffe und
   Mono-Modi (sum/left/right) — die Send-Busse (Pre/Post pro Track)
   decken den Hauptfall.
2. **Umbenennung inkl. Code** (User-Entscheidung: Code übernimmt die
   UI-Namen): `looper_in` → **`looper_patch_in`** („Looper patch IN"),
   `looper_big_out` → **`looper_patch_out`** („Looper patch OUT") — in
   Browser, Modul-Header, factoryIds, Klassen- und Dateinamen
   (`LooperPatchInModule`/`LooperPatchOutModule`,
   `LooperPatchInPanel`/`LooperPatchOutPanel`) und der
   GraphManager-API (`syncLooperPatchOutConfigs`,
   `hasLooperPatchOutCables`, `collectAndRemovePatchOutCables`,
   `restorePatchOutCables`, `PatchOutCableRef`).
3. **Migration alter Patches:** `GraphManager::normalizeNode` mappt die
   Alt-Schlüssel `looper_in`/`looper_big_out` auf die neuen factoryIds;
   `normalizeLoadedNodes()` läuft in setStateInformation/loadPreset
   DIREKT nach dem Tree-Copy — VOR `syncLooperPatchOutConfigs`, das
   sonst über die alten Schlüssel hinwegliefe. moduleIds (und damit
   tap:-Quell-Keys) bleiben unverändert. `looper_out`-Nodes alter
   Patches laufen in den definierten nodeError-Pfad („Unbekanntes
   Modul: looper_out") und sind normal löschbar — kein stiller Drop,
   die Kabel des Users bleiben sichtbar referenziert.

## Konsequenzen

- Browser zeigt genau EIN Looper-Ausgangsmodul; Namenskollision und
  „Mini"-Krücke sind weg.
- Descriptor-Zähltest: 69 → 68 Module.
- OSC-Pfade neuer Instanzen tragen die neuen moduleIds
  (`looper_patch_in_1` …); bestehende Patches behalten ihre Pfade.
