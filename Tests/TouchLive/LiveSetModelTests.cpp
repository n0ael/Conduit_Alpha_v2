#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveSetModel.h"

using Catch::Approx;

namespace
{

[[nodiscard]] juce::var parse (const char* json)
{
    auto result = juce::JSON::parse (juce::String::fromUTF8 (json));
    REQUIRE (result.getDynamicObject() != nullptr);
    return result;
}

/** Zählt alle Tree-Events — der Flacker-Nachweis für die Reconnect-Regel. */
struct TreeEventCounter final : juce::ValueTree::Listener
{
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { ++propertyChanges; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override            { ++childAdds; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override     { ++childRemoves; }

    [[nodiscard]] int total() const noexcept { return propertyChanges + childAdds + childRemoves; }

    int propertyChanges = 0;
    int childAdds = 0;
    int childRemoves = 0;
};

} // namespace

//==============================================================================
TEST_CASE ("LiveSetModel: Snapshot füllt Skalar- und Objekt-Keys korrekt", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("transport",
        parse (R"({"is_playing":true,"tempo":120.5,"metronome":false,"sig_num":4,"sig_den":4})"));

    auto transport = model.getDomain ("transport");
    REQUIRE (static_cast<bool> (transport.getProperty ("is_playing")));
    REQUIRE (static_cast<double> (transport.getProperty ("tempo")) == Approx (120.5));
    REQUIRE_FALSE (static_cast<bool> (transport.getProperty ("metronome")));
    REQUIRE (static_cast<int> (transport.getProperty ("sig_num")) == 4);

    model.applySnapshot ("tracks",
        parse (R"({"tr:0":{"name":"Drums","color":16711680,"kind":"audio","index":0},)"
               R"("tr:1":{"name":"Bass","color":255,"kind":"midi","index":1}})"));

    auto drums = model.findItem ("tracks", "tr:0");
    REQUIRE (drums.isValid());
    REQUIRE (drums.getProperty ("name").toString() == "Drums");
    REQUIRE (static_cast<int> (drums.getProperty ("color")) == 16711680);
    REQUIRE (drums.getProperty ("kind").toString() == "audio");

    REQUIRE (model.findItem ("tracks", "tr:1").isValid());
    REQUIRE (model.getDomain ("tracks").getNumChildren() == 2);
}

TEST_CASE ("LiveSetModel: Diff ändert nur betroffene Properties, null entfernt Kinder", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("mixer",
        parse (R"({"tr:0":{"volume":0.85,"pan":0.0,"mute":false},)"
               R"("tr:1":{"volume":0.5,"pan":-0.2,"mute":true}})"));

    // Diff: tr:0 trägt den KOMPLETTEN neuen Wert, tr:1 wird entfernt
    model.applyDiff ("mixer",
        parse (R"({"tr:0":{"volume":0.7,"pan":0.0,"mute":false},"tr:1":null})"));

    auto first = model.findItem ("mixer", "tr:0");
    REQUIRE (static_cast<double> (first.getProperty ("volume")) == Approx (0.7));
    REQUIRE (static_cast<double> (first.getProperty ("pan")) == Approx (0.0));
    REQUIRE_FALSE (model.findItem ("mixer", "tr:1").isValid());
    REQUIRE (model.getDomain ("mixer").getNumChildren() == 1);

    // Skalar-Diff: nur tempo ändert sich, is_playing bleibt
    model.applySnapshot ("transport", parse (R"({"is_playing":true,"tempo":120.0})"));
    model.applyDiff ("transport", parse (R"({"tempo":128.0})"));

    auto transport = model.getDomain ("transport");
    REQUIRE (static_cast<double> (transport.getProperty ("tempo")) == Approx (128.0));
    REQUIRE (static_cast<bool> (transport.getProperty ("is_playing")));

    // Skalar-null entfernt die Property
    model.applyDiff ("transport", parse (R"({"is_playing":null})"));
    REQUIRE_FALSE (transport.hasProperty ("is_playing"));
}

TEST_CASE ("LiveSetModel: Objekt-Wert ersetzt Item komplett (fehlende Felder verschwinden)", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    // Muster der Gegenseite: der Master verliert nie Felder, aber ein
    // Clip-Slot wechselt z. B. von belegt zu leer → Wert ohne alte Felder
    model.applySnapshot ("mixer", parse (R"({"tr:0":{"volume":0.85,"pan":0.1,"mute":false}})"));
    model.applyDiff ("mixer", parse (R"({"tr:0":{"volume":0.85,"pan":0.1}})"));

    auto item = model.findItem ("mixer", "tr:0");
    REQUIRE (item.hasProperty ("volume"));
    REQUIRE_FALSE (item.hasProperty ("mute"));
}

TEST_CASE ("LiveSetModel: Snapshot als Tree-Diff entfernt verschwundene Keys", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("session",
        parse (R"({"scene:sc:0":{"name":"Intro","index":0},"grid:tr:0":[null,{"name":"A","state":"stopped"}]})"));
    model.applySnapshot ("session",
        parse (R"({"scene:sc:0":{"name":"Intro","index":0}})"));

    auto session = model.getDomain ("session");
    REQUIRE (model.findItem ("session", "scene:sc:0").isValid());
    REQUIRE_FALSE (session.hasProperty ("grid:tr:0"));
}

TEST_CASE ("LiveSetModel: Reconnect-Snapshot — unveränderte Properties feuern keine Listener", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    // Voller Mix aus Skalaren, Objekten und Arrays MIT verschachtelten
    // Objekten (Session-Grid) — genau da liegt die var-Pointer-Falle
    const auto* sessionJson =
        R"({"scene:sc:0":{"name":"Intro","color":255,"index":0},)"
        R"("grid:tr:0":[null,{"name":"A","color":1,"state":"playing"},null],)"
        R"("grid:tr:1":[{"name":"B","color":2,"state":"stopped"},null,null]})";

    model.applySnapshot ("session", parse (sessionJson));
    model.applySnapshot ("transport", parse (R"({"is_playing":false,"tempo":120.0})"));

    // Handle lokal halten: ValueTree-Listener hängen an der Instanz,
    // nicht am SharedObject — ein Temporary würde sie sofort verlieren
    auto state = model.getState();
    TreeEventCounter counter;
    state.addListener (&counter);

    // Identischer Snapshot (frisch geparst → neue DynamicObjects) → NICHTS feuert
    model.applySnapshot ("session", parse (sessionJson));
    model.applySnapshot ("transport", parse (R"({"is_playing":false,"tempo":120.0})"));
    REQUIRE (counter.total() == 0);

    // Ein geändertes Feld → genau EIN Property-Event
    model.applySnapshot ("transport", parse (R"({"is_playing":false,"tempo":124.0})"));
    REQUIRE (counter.propertyChanges == 1);
    REQUIRE (counter.childAdds == 0);
    REQUIRE (counter.childRemoves == 0);

    // Eine geänderte Grid-Zeile → genau EIN Property-Event (die Zeile)
    model.applySnapshot ("session",
        parse (R"({"scene:sc:0":{"name":"Intro","color":255,"index":0},)"
               R"("grid:tr:0":[null,{"name":"A","color":1,"state":"stopped"},null],)"
               R"("grid:tr:1":[{"name":"B","color":2,"state":"stopped"},null,null]})"));
    REQUIRE (counter.propertyChanges == 2);
    REQUIRE (counter.childAdds == 0);
    REQUIRE (counter.childRemoves == 0);

    state.removeListener (&counter);
}

TEST_CASE ("LiveSetModel: Echo-Suppression-Prädikat lässt berührte Felder unangetastet", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("mixer", parse (R"({"tr:0":{"volume":0.5,"pan":0.0}})"));

    const auto suppressVolume = [] (const juce::String& domainName, const juce::String& key,
                                    const juce::String& field)
    {
        return domainName == "mixer" && key == "tr:0" && field == "volume";
    };

    model.applyDiff ("mixer", parse (R"({"tr:0":{"volume":0.9,"pan":0.25}})"), suppressVolume);

    auto item = model.findItem ("mixer", "tr:0");
    REQUIRE (static_cast<double> (item.getProperty ("volume")) == Approx (0.5));
    REQUIRE (static_cast<double> (item.getProperty ("pan")) == Approx (0.25));
}

//==============================================================================
TEST_CASE ("LiveSetModel: setItemField schreibt optimistisch und feuert Listener", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("mixer", parse (R"({"tr:0":{"vol":0.5,"pan":0.0,"mute":false}})"));

    TreeEventCounter counter;
    auto state = model.getState();   // Handle halten: Listener leben pro Instanz
    state.addListener (&counter);

    model.setItemField ("mixer", "tr:0", "vol", 0.8);
    auto item = model.findItem ("mixer", "tr:0");
    REQUIRE (static_cast<double> (item.getProperty ("vol")) == Approx (0.8));
    REQUIRE (counter.propertyChanges == 1);

    // Bool-Feld (mute/solo/arm)
    model.setItemField ("mixer", "tr:0", "mute", true);
    REQUIRE (static_cast<bool> (item.getProperty ("mute")));

    // Gleicher Wert -> kein zweites Event (Flacker-Schutz)
    const auto before = counter.total();
    model.setItemField ("mixer", "tr:0", "vol", 0.8);
    REQUIRE (counter.total() == before);

    // Fehlendes Item -> No-op
    model.setItemField ("mixer", "tr:99", "vol", 0.3);
    REQUIRE (counter.total() == before);

    state.removeListener (&counter);
}

TEST_CASE ("LiveSetModel: setItemArrayElement ersetzt ein Send und benachrichtigt", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LiveSetModel model;

    model.applySnapshot ("mixer", parse (R"({"tr:0":{"vol":0.5,"sends":[0.1,0.2,0.3]}})"));

    TreeEventCounter counter;
    auto state = model.getState();   // Handle halten: Listener leben pro Instanz
    state.addListener (&counter);

    model.setItemArrayElement ("mixer", "tr:0", "sends", 1, 0.75);
    auto item = model.findItem ("mixer", "tr:0");
    const auto* sends = item.getProperty ("sends").getArray();
    REQUIRE (sends != nullptr);
    REQUIRE (static_cast<double> (sends->getReference (0)) == Approx (0.1));
    REQUIRE (static_cast<double> (sends->getReference (1)) == Approx (0.75));
    REQUIRE (static_cast<double> (sends->getReference (2)) == Approx (0.3));
    REQUIRE (counter.propertyChanges == 1);   // neuer Array-Pointer -> genau ein Event

    // Index ausserhalb / gleicher Wert -> No-op
    const auto before = counter.total();
    model.setItemArrayElement ("mixer", "tr:0", "sends", 9, 0.5);
    model.setItemArrayElement ("mixer", "tr:0", "sends", 1, 0.75);
    REQUIRE (counter.total() == before);

    state.removeListener (&counter);
}
