"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import time
import serial
from typing import Optional

"""
Real UART implementation using pyserial.

Allows the Virtual Master to communicate with a physical Device over a serial port.
"""


class RealUART:
    """Real UART wrapper using pyserial."""

    def __init__(self, port: str, baudrate: int = 38400):
        """
        Initialize Real UART.

        Args:
            port: Serial port path (e.g., '/dev/ttyACM0')
            baudrate: Initial baudrate
        """
        self.port = port
        self.baudrate = baudrate
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS,
            timeout=0,  # Non-blocking read
            write_timeout=0,  # Non-blocking write attempt
        )
        print(f"[RealUART] Opened {self.port} at {self.baudrate} baud")

    def get_device_tty(self) -> str:
        """
        Get the TTY path (for consistency with VirtualUART interface).

        Returns:
            The configured port path.
        """
        return self.port

    def send_byte(self, byte: int) -> None:
        """Send a single byte."""
        self.ser.write(bytes([byte]))

    def send_bytes(self, data: bytes) -> None:
        """Send multiple bytes."""
        self.ser.write(data)

    def recv_byte(self, timeout_ms: int = 1000) -> Optional[int]:
        """Receive a single byte with timeout."""
        deadline = time.time() + (timeout_ms / 1000.0)
        
        while True:
            if self.ser.in_waiting > 0:
                data = self.ser.read(1)
                if data:
                    return data[0]
            
            if time.time() > deadline:
                return None
            time.sleep(0.0001) # Short sleep to yield

    def recv_bytes(self, count: int, timeout_ms: int = 1000) -> Optional[bytes]:
        """Receive multiple bytes with timeout."""
        result = bytearray()
        deadline = time.time() + (timeout_ms / 1000.0)

        while len(result) < count:
            remaining_time = deadline - time.time()
            if remaining_time <= 0:
                return None

            # Read available bytes up to what we need
            available = self.ser.in_waiting
            if available > 0:
                to_read = min(available, count - len(result))
                chunk = self.ser.read(to_read)
                result.extend(chunk)
            else:
                time.sleep(0.0001)

        return bytes(result)

    def set_baudrate(self, baudrate: int) -> None:
        """Change the baudrate."""
        self.baudrate = baudrate
        self.ser.baudrate = baudrate
        print(f"[RealUART] Changed baudrate to {baudrate}")

    def flush(self) -> None:
        """Flush buffers."""
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self) -> None:
        """Close the serial port."""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print(f"[RealUART] Closed {self.port}")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
