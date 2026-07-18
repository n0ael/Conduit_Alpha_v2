#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "Modules/ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Verwaltet den Pages-Zweig des Root-ValueTrees (ADR 008 M1): Seiten sind
    eine reine View-Schicht über EINEM AudioProcessorGraph — der Audio-Thread
    kennt keine Seiten. Alle Methoden laufen auf dem Message Thread und
    arbeiten AUSSCHLIESSLICH auf dem ValueTree.

    Schema (6.2-Erweiterung):
      Pages[]
        └── Page
             ├── pageUuid     (persistent, Referenzanker — Node-Property zeigt hierauf)
             ├── gridX/gridY  (Seiten-Koordinaten, mutierbar)
             ├── name         (optional)
             └── viewOffsetX / viewOffsetY / viewZoom   (Viewport pro Seite)

    Regeln (ADR 008):
      - Seitenwechsel eines Nodes ist ein setProperty (EIN Undo-Step),
        NIE removeChild/addChild — sonst feuern Delete-Pfad und
        OSC-Deregistrierung fälschlich.
      - Seiten-Löschen nur wenn leer (Regel a).
      - Migration (Bestandspatches ohne Pages-Zweig): undo-frei, EINE Seite
        (0,0), alle Nodes erhalten deren pageUuid, rootStateVersion wird
        gesetzt. Idempotent — Nodes ohne pageUuid (z. B. von
        ensureIONodeStates nach dem Laden ergänzte I/O-Endpunkte) werden
        bei jedem Aufruf nachgezogen.
*/
class PageManager final
{
public:
    /** Root-Version, ab der der Pages-Zweig existiert (Bestand ohne
        rootStateVersion-Property gilt als Version 1). Die ADR-009-
        I/O-Wandlung bumpt SEPARAT in M2 (Sequenz-Korrektur 18.07.2026). */
    static constexpr int pagesRootVersion = 2;

    PageManager (juce::ValueTree rootTree, juce::UndoManager& undoManagerToUse);

    //==========================================================================
    /** Legt die Default-Seite (0,0) an, wenn der Pages-Zweig leer ist bzw.
        fehlt (undo-frei — Grundausstattung wie ensureIONodeStates).
        Gibt die pageUuid der Seite (0,0) zurück (bzw. der ersten Seite,
        falls (0,0) nicht existiert, aber andere Seiten vorhanden sind). */
    juce::String ensureDefaultPage();

    /** Aktive Seite (M3b): Root-Property `activePage` (View-State, kein
        Undo, persistiert mit der Session). Zeigt die Property auf eine
        gelöschte/unbekannte Seite, fällt der Getter auf die Default-Seite
        zurück und repariert die Property. Neue Nodes landen hier. */
    [[nodiscard]] juce::String getActivePageUuid();

    /** Aktive Seite wechseln — false, wenn die Uuid keine Seite ist.
        View-State (kein Undo, Muster Viewport). */
    bool setActivePage (const juce::String& uuid);

    /** Nachbarseite im Grid (gridX+dx, gridY+dy) relativ zu einer Seite;
        invalid, wenn dort keine liegt. */
    [[nodiscard]] juce::ValueTree neighbourPage (const juce::String& uuid,
                                                 int dx, int dy) const;

    //==========================================================================
    /** Legt eine Seite an (gridX, gridY) an — undo-fähig, eine Transaktion.
        Existiert dort bereits eine Seite, wird deren pageUuid zurückgegeben
        (kein Duplikat). */
    juce::String createPage (int gridX, int gridY);

    /** Page-Subtree an (gridX, gridY); invalid wenn keine Seite dort liegt. */
    [[nodiscard]] juce::ValueTree findPage (int gridX, int gridY) const;

    /** Page-Subtree zur pageUuid; invalid wenn unbekannt. */
    [[nodiscard]] juce::ValueTree findPageByUuid (const juce::String& uuid) const;

    /** pageUuid des Node-Subtrees (leer, wenn der Node keine trägt). */
    [[nodiscard]] static juce::String pageOf (const juce::ValueTree& nodeTree);

    /** Verschiebt einen Node auf eine andere Seite — reines setProperty
        (EIN Undo-Step), undo-fähig. false wenn Node oder Seite unbekannt. */
    bool setNodePage (const juce::String& nodeUuid, const juce::String& targetPageUuid);

    /** Regel a: true nur, wenn die Seite existiert und KEIN Node ihre
        pageUuid trägt. (Löschen der letzten Seite ist erlaubt —
        ensureDefaultPage() legt (0,0) bei Bedarf neu an; ob die UI das
        zulässt, entscheidet M3b.) */
    [[nodiscard]] bool canDeletePage (const juce::String& uuid) const;

    /** Löscht eine leere Seite (canDeletePage) — undo-fähig, eine
        Transaktion. false wenn verweigert. */
    bool deletePage (const juce::String& uuid);

    //==========================================================================
    /** Migration + Reparatur nach jedem Laden (und einmal im Ctor-Pfad des
        EngineProcessor): fehlt der Pages-Zweig oder ist rootStateVersion <
        pagesRootVersion → Pages-Zweig + Seite (0,0) anlegen, allen Nodes
        ohne pageUuid die Default-Seite setzen, rootStateVersion heben.
        Komplett undo-frei (erscheint NICHT in der Undo-History), idempotent. */
    void migrateAndRepair();

private:
    //==========================================================================
    [[nodiscard]] juce::ValueTree pagesContainer() const;
    [[nodiscard]] juce::ValueTree makePage (int gridX, int gridY) const;

    juce::ValueTree rootState;  // ref-counted Handle
    juce::UndoManager& undoManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PageManager)
};

} // namespace conduit
