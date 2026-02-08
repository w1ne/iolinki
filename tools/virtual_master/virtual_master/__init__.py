"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

from .master import VirtualMaster
from .protocol import MSequenceType

__all__ = ["VirtualMaster", "MSequenceType"]

"""
Python Virtual IO-Link Master

A software-based IO-Link Master simulator for automated testing
of IO-Link Device implementations without requiring physical hardware.
"""

__version__ = "0.1.0"
