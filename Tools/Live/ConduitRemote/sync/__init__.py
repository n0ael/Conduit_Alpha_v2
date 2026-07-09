"""Sync layer: domains, stable IDs, delivery."""


def build_domains(song, sender):
    """Construct all sync domains. Returns dict name -> Domain instance."""
    from .transport import TransportDomain
    from .tracks import TracksDomain
    from .mixer import MixerDomain
    from .session import SessionDomain
    from .devices import DevicesDomain
    domains = {}
    for cls in (TransportDomain, TracksDomain, MixerDomain, SessionDomain,
                DevicesDomain):
        domain = cls(song, sender)
        domains[domain.NAME] = domain
    return domains
