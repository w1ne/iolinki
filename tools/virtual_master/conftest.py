"""
Pytest helpers for Virtual Master tests.

Skips PTY-dependent tests when the environment cannot allocate a PTY.
"""

import os
import pty
import pytest
import warnings


def _pty_available() -> bool:
    try:
        master_fd, slave_fd = pty.openpty()
        os.close(master_fd)
        os.close(slave_fd)
        return True
    except OSError:
        return False


def pytest_sessionstart(session):
    available = _pty_available()
    session.config._iolinki_pty_available = available
    if not available:
        warnings.warn(
            "PTY allocation failed; PTY-dependent tests will be skipped. "
            "Run in Docker or a host with PTY support to execute integration tests.",
            RuntimeWarning,
        )


def pytest_collection_modifyitems(config, items):
    available = getattr(config, "_iolinki_pty_available", True)
    if available:
        return

    skip = pytest.mark.skip(reason="PTY not available in this environment")
    for item in items:
        path = str(item.fspath)
        if "tools/virtual_master/tests" in path:
            continue
        if "tools/virtual_master" in path:
            item.add_marker(skip)
