// conduit_announce.js — Announce + Fernbedienung des nativen Conduit-LFO
// (CLAUDE.md 7.4). Das Device macht KEIN Audio: der LFO läuft in Conduit,
// die Dials senden nur OSC über die rename-feste Alias-Adresse.
autowatch = 1;
inlets = 1;
outlets = 2; // 0: OSC-Messages → [udpsend] | 1: "set <seed>" → [live.numbox]

var FACTORY_KEY = "lfo";
var REANNOUNCE_MS = 30000;

var seed = 0;          // persistente remoteId-Basis (live.numbox, Stored Only)
var apiReady = false;  // erst nach dem live.thisdevice-Bang ist die Live API da
var announceTask = new Task(announce, this);

// live.thisdevice → bang: Live API bereit (NICHT loadbang — der feuert zu früh)
function bang()
{
    apiReady = true;

    if (seed === 0)
    {
        // Erst-Anlage: einmalige Zufalls-ID, wandert via Outlet 1 in die
        // live.numbox und wird damit im Live-Set gespeichert
        seed = Math.floor(1 + Math.random() * 2147483000);
        outlet(1, "set", seed);
    }

    announce();
    announceTask.interval = REANNOUNCE_MS;
    announceTask.repeat(); // Re-Announce-Heartbeat (Conduit-Neustart, Node geloescht)
}

// live.numbox beim Laden des Live-Sets: gespeicherte ID wiederherstellen
function seedvalue(v)
{
    seed = Math.floor(v);

    if (apiReady && seed !== 0)
        announce();
}

function remoteId()
{
    return "m4l-" + seed;
}

function announce()
{
    if (!apiReady || seed === 0)
        return;

    var track = new LiveAPI("this_device canonical_parent");
    var name = String(track.get("name"));
    var colour = parseInt(track.get("color"), 10) || 0; // 0x00RRGGBB

    outlet(0, "/conduit/announce", remoteId(), FACTORY_KEY, name, colour);
}

// Dial-Werte → /conduit/remote/{remoteId}/{paramId} (Alias, rename-fest)
function rate(v)
{
    if (seed !== 0)
        outlet(0, "/conduit/remote/" + remoteId() + "/rate", v);
}

function depth(v)
{
    if (seed !== 0)
        outlet(0, "/conduit/remote/" + remoteId() + "/depth", v);
}

function notifydeleted()
{
    announceTask.cancel();
}
