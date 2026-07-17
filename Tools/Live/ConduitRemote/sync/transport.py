"""Transport domain: playback, tempo, metronome, session record, time sig."""

from .base import Domain


class TransportDomain(Domain):
    NAME = "transport"

    def __init__(self, live_song, sender):
        Domain.__init__(self, live_song, sender)
        self._bound = False

    # -- snapshot -------------------------------------------------------------

    def collect(self):
        song = self.song
        state = {
            "is_playing": bool(song.is_playing),
            "tempo": float(song.tempo),
            "metronome": bool(song.metronome),
            "session_record": bool(song.session_record),
            "sig_num": int(song.signature_numerator),
            "sig_den": int(song.signature_denominator),
        }

        # Song-Position (Live-Remote-Bridge, 07/2026): bewusst auf ganze
        # bar/beat quantisiert -- der current_song_time-Listener feuert
        # waehrend des Playbacks sehr oft, aber compute_diff() sendet nur,
        # wenn sich der KEY-WERT aendert. Quantisierung = Diff-Drossel.
        # LOM-Guard: fehlende Faehigkeit -> Keys weglassen (Rule touchlive).
        try:
            bt = song.get_current_beats_song_time()
            state["bar"] = int(bt.bars)
            state["beat"] = int(bt.beats)
        except Exception:
            pass

        return state

    # -- listeners --------------------------------------------------------------

    def _on_change(self):
        self.mark_dirty()

    def on_attach(self):
        if self._bound:
            return
        song = self.song
        song.add_is_playing_listener(self._on_change)
        song.add_tempo_listener(self._on_change)
        song.add_metronome_listener(self._on_change)
        song.add_session_record_listener(self._on_change)
        # Song-Position: Listener-Bind kann pro Live-Version werfen
        # (Rule touchlive) -- einzeln absichern, der Poll-Fallback der
        # Domain greift NICHT (die anderen Binds gelangen), deshalb ist
        # die Position ohne diesen Listener schlicht standbild-frei erst
        # beim naechsten anderen Event. Praktisch existiert der Listener
        # seit Live 1.x.
        try:
            song.add_current_song_time_listener(self._on_change)
            self._time_bound = True
        except Exception:
            self._time_bound = False
        self._bound = True

    def on_detach(self):
        if not self._bound:
            return
        song = self.song
        song.remove_is_playing_listener(self._on_change)
        song.remove_tempo_listener(self._on_change)
        song.remove_metronome_listener(self._on_change)
        song.remove_session_record_listener(self._on_change)
        if getattr(self, "_time_bound", False):
            try:
                song.remove_current_song_time_listener(self._on_change)
            except Exception:
                pass
            self._time_bound = False
        self._bound = False
