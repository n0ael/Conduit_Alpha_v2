#pragma once

namespace conduit
{

//==============================================================================
/**
    Mixin für Module, die einen Hardware-Audio-Anker des EngineProcessor
    spiegeln (ADR 009): Der GraphManager verbindet sie bei der
    Materialisierung implizit mit dem registrierten AudioGraphIOProcessor
    (Anker-Kabel stehen NICHT im Tree — sie sind Infrastruktur, kein
    Patch-Zustand; syncConnections rührt sie nicht an, weil nur Kabel
    zwischen tree-verwalteten Nodes abgeglichen werden).

    Thread-Ownership: beide Methoden Message Thread (Materialisierung,
    VOR prepareForGraph).
*/
class IExternalAudioEndpoint
{
public:
    virtual ~IExternalAudioEndpoint() = default;

    /** true = Hardware-EINGANG (Anker liefert Kanäle an dieses Modul);
        false = Hardware-AUSGANG (dieses Modul speist den Anker). */
    [[nodiscard]] virtual bool isInputEndpoint() const noexcept = 0;

    /** Kanalzahl aus dem Tree (Hardware-Kanäle) — VOR prepareForGraph,
        bestimmt die Bus-Konfiguration des Pass-Through. */
    virtual void setEndpointChannels (int numChannels) = 0;

    /** Aktuelle Kanalzahl (für die Anker-Verkabelung des GraphManager). */
    [[nodiscard]] virtual int getEndpointChannels() const noexcept = 0;
};

} // namespace conduit
