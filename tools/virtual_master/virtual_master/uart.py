"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import os
import pty
import select
import time
import termios
from typing import Optional

"""
Virtual UART implementation using pseudo-terminals (pty).

Provides a virtual serial connection between the Master and Device
without requiring physical hardware.
"""


class VirtualUART:
    """Virtual UART using pty pairs for Master-Device communication."""

    def __init__(self):
        """Create a pty pair for virtual UART communication."""
        self.master_fd, self.device_fd = pty.openpty()
        self.device_tty = os.ttyname(self.device_fd)

        for fd in [self.master_fd, self.device_fd]:
            try:
                attrs = termios.tcgetattr(fd)
                attrs[3] = (
                    attrs[3]
                    & ~termios.ECHO
                    & ~termios.ICANON
                    & ~termios.IEXTEN
                    & ~termios.ISIG
                )
                attrs[1] = attrs[1] & ~termios.ONLCR  # Disable NL to CR-NL conversion
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
            except termios.error:
                pass

    def get_device_tty(self) -> str:
        """
        Get the TTY path for the Device side.

        Returns:
            TTY device path (e.g., '/dev/pts/5')
        """
        return self.device_tty

    def send_byte(self, byte: int) -> None:
        """
        Send a single byte from Master to Device.

        Args:
            byte: Byte value (0-255)
        """
        os.write(self.master_fd, bytes([byte]))

    def send_bytes(self, data: bytes) -> None:
        """
        Send multiple bytes from Master to Device.

        Args:
            data: Bytes to send
        """
        os.write(self.master_fd, data)

    def recv_byte(self, timeout_ms: int = 1000) -> Optional[int]:
        """
        Receive a single byte from Device (non-blocking with timeout).

        Args:
            timeout_ms: Timeout in milliseconds

        Returns:
            Received byte value or None if timeout
        """
        timeout_sec = timeout_ms / 1000.0
        ready, _, _ = select.select([self.master_fd], [], [], timeout_sec)

        if ready:
            data = os.read(self.master_fd, 1)
            return data[0] if data else None
        return None

    def recv_bytes(self, count: int, timeout_ms: int = 1000) -> Optional[bytes]:
        """
        Receive multiple bytes from Device.

        Args:
            count: Number of bytes to receive
            timeout_ms: Timeout in milliseconds

        Returns:
            Received bytes or None if timeout
        """
        result = bytearray()
        deadline = time.time() + (timeout_ms / 1000.0)

        while len(result) < count:
            remaining_ms = int((deadline - time.time()) * 1000)
            if remaining_ms <= 0:
                return None

            byte = self.recv_byte(remaining_ms)
            if byte is None:
                return None
            result.append(byte)

        return bytes(result)

    def flush(self) -> None:
        """Flush any pending data in the UART buffers."""
        while True:
            ready, _, _ = select.select([self.master_fd], [], [], 0)
            if not ready:
                break
            os.read(self.master_fd, 1024)

    def close(self) -> None:
        """Close the virtual UART."""
        os.close(self.master_fd)
        os.close(self.device_fd)

    def __enter__(self):
        """Context manager entry."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
