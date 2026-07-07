### ADR: SpscQueue als einziger Inter-Thread-Queue-Baustein + Repo-Rename-Klarstellung

- **(a) AbstractFifo-Restbestand entfernt:** §3.1 nannte noch
  `juce::AbstractFifo` als Option für Parameter-Updates — ein Restbestand
  aus frühen Dokumentversionen. Die Codebase nutzt durchgängig `SpscQueue`
  (`Source/Util/SpscQueue.h`, Catch2-getestet, TSan-abgedeckt); es gibt
  0 AbstractFifo-Vorkommen im Code. §3.1 schreibt SpscQueue jetzt als
  einzigen Inter-Thread-Queue-Baustein vor.
- **(b) Repo-Umbenennung dokumentiert:** `Conduit_Alpha_v2` →
  `github.com/n0ael/Conduit` (Juli 2026). Der Hinweisblock unter dem
  Dokumenttitel verhindert Verwechslung alter Referenzen (Commits,
  Kommentare, externe Notizen) mit einem anderen Projekt; Roadmap-Angaben
  „v2.x" bleiben Feature-Meilensteine, keine Repo-Namen.
- Kein Code betroffen — reine Dokumentationsentscheidung.
