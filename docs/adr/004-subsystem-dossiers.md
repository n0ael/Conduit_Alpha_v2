### ADR 004: Subsystem-Dossiers unter docs/
Status: Akzeptiert — Juli 2026
Kontext: Die CLAUDE.md war auf > 1000 Zeilen gewachsen.
Governance-Invarianten (jede Session nötig) und Feature-Gedächtnis
abgeschlossener Subsysteme (nur bei Arbeit am Subsystem nötig)
waren vermischt; das Wachstum war linear mit jedem Feature.
Entscheidung: Subsystem-Spezifikationen und -Lektionen leben in
docs/{Subsystem}.md, ADRs in docs/adr/ (append-only). Die CLAUDE.md
behält pro Subsystem einen Invarianten-Stub mit Pflicht-Verweis
(§1.1). Arbeitsaufträge nennen die relevanten Dossiers; Phase 1
bestätigt deren Lektüre. §7.2 (LinkAudio: Send + Clock implementiert)
ist nach docs/LinkAudio.md ausgelagert; der offene Receive-Meilenstein
arbeitet gegen dieses Dossier als Pflichtlektüre.
Descope in derselben Version: die 10-Finger-Panic-Geste (§10.1)
ist aus der Spezifikation gestrichen; All-Notes-Off bleibt über
Release-All (Grid-Page) und modulare Mechanismen abgedeckt.
Konsequenzen: + Kernprompt um ~30 % reduziert (1023 → 714 Zeilen); künftige Feature-Doku landet
in Dossiers, nicht im Prompt. − Risiko übersprungener
Dossier-Lektüre → mitigiert durch §1.1 und den
Phase-1-Pflichtnachweis in jedem Auftrag.
