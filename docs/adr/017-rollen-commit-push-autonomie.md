# ADR 017 — Rollen-Modell & Commit-/Push-Autonomie

Datum: 21.07.2026 · Status: beschlossen (User-Entscheidung im
Rahmen der CLAUDE.md-Kompression v5.8)

Kontext: Der User (Leon) reviewt Produkt-VERHALTEN, nicht Code —
Berichte, Smoke-Anleitungen und Entscheidungsvorlagen sind sein
Gate, nicht der Diff. Das bis dahin geltende pauschale Commit-Verbot
(„lokale Commits nur auf Ansage") erzeugte am 20.07.2026 wiederholt
Vergess-Fehler: fertige Meilensteine blieben uncommitted liegen, der
Arbeitsbaum sammelte gemischte Stände, und ein pathspec-Stash setzte
gestagte Dateien zurück (Rettung nur über den Stash-Index-Commit).

Entscheidung:
- Claude Code committet selbstständig EINEN Commit pro abgeschlossenem
  Meilenstein — kein Warten auf eine separate Commit-Ansage mehr.
- Push bleibt am User-Gate: Freigabe erst nach der Push-Checkliste
  (Bericht gelesen · DoD vollständig · eigener Smoke bestanden ·
  CI grün).
- Rollen: Leon = Produktvision/Design-/Verhaltensentscheidungen,
  Feldtests/Smoke, Freigaben auf Berichtsebene (liest keinen Code).
  Claude (Chat) = Architektur, Entscheidungsvorlagen mit Tradeoffs,
  Auftrags-Autorschaft, Review von STOPP-Berichten/Fix-Vorschlägen.
  Claude Code = Implementierung, Tests, Dossier-/STATUS-Pflege, Commit.
- Mechanische Gates (unabhängig vom User): /WX-Build, Catch2
  (Regressionstests vorher-rot), ASan/TSan, RT-Audit, CI.
- Permission-Regeln: Deny-Regeln für push/merge/gh-pr bleiben; eine
  eigenständige Commit-Sperre existiert in den Settings nicht (weder
  lokal noch global ein `deny`-Block — die frühere Commit-Zurückhaltung
  war Harness-Default-Verhalten, kein Regel-Eintrag).

Konsequenz:
- Jedes Auftrags-Template trägt in der DoD einen „Dein Smoke" in
  Produktsprache — der Bericht endet damit.
- Externe/globale Skills und Tools sind diesem Regelwerk nachrangig;
  bei Konflikt gilt die CLAUDE.md (Präzedenz: ponytail-Ausschluss).
