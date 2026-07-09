"""/live/device/* command handlers (M3).

device_ref (args[0]) ist die Stable-ID aus der devices-Domain ("dv:3"),
aufgeloest via ctx.resolve_device(). /live/device/set/parameter ist ein
reiner Value-Write und steht auf der FAST_WHITELIST (Timer-Pump).
"""

import logging

from . import _util

logger = logging.getLogger(__name__)


def _parameter(ctx, args):
    if len(args) < 3:
        return
    device = ctx.resolve_device(args[0])
    if device is None:
        logger.debug("device parameter: unknown device %r", args[0])
        return
    params = list(getattr(device, "parameters", []) or [])
    index = _util.as_int(args[1], -1)
    if not (0 <= index < len(params)):
        logger.debug("device parameter: index out of range %r", args[1])
        return
    _util.set_param(params[index], args[2])


def _is_active(ctx, args):
    if len(args) < 2:
        return
    device = ctx.resolve_device(args[0])
    if device is None:
        logger.debug("device is_active: unknown device %r", args[0])
        return
    device.is_active = _util.as_bool(args[1])


def register_all(registry):
    registry.register("/live/device/set/parameter", _parameter)
    registry.register("/live/device/set/is_active", _is_active)
