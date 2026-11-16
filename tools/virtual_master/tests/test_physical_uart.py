"""Host-side checks for USB-UART 8E1 and the 4-byte TYPE_2_V profile.

No hardware or PTY is required. This does not validate IO-Link C/Q timing.
"""
import os
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

from virtual_master.crc import checksum6
from virtual_master.master import VirtualMaster
from virtual_master.protocol import MSequenceType
from virtual_master.real_uart import RealUART
import nucleo_master


class PhysicalMasterProfileTest(unittest.TestCase):
    def test_real_uart_even_parity(self):
        with patch("virtual_master.real_uart.serial.Serial") as serial_open:
            RealUART("/dev/ttyUSB0", 230400, parity="even")
            args = serial_open.call_args.kwargs
            self.assertEqual(args["baudrate"], 230400)
            self.assertEqual(args["parity"], "E")
            self.assertEqual(args["bytesize"], 8)
            self.assertEqual(args["stopbits"], 1)

    def test_real_uart_legacy_no_parity(self):
        with patch("virtual_master.real_uart.serial.Serial") as serial_open:
            RealUART("/dev/ttyUSB0")
            self.assertEqual(serial_open.call_args.kwargs["parity"], "N")

    def test_od_width_only_type2v(self):
        uart = Mock()
        with self.assertRaises(ValueError):
            VirtualMaster(uart=uart, m_seq_type=MSequenceType.TYPE_0, od_len=1)
        with self.assertRaises(ValueError):
            VirtualMaster(uart=uart, m_seq_type=MSequenceType.TYPE_2_V, od_len=3)

    def test_type2v_pdin4_pdout0_od1_cycle(self):
        uart = Mock()
        data = b"\x01\x02\x03\x04"
        uart.recv_bytes.return_value = data + bytes([checksum6(data + b"\x00") & 0x3F])
        master = VirtualMaster(uart=uart, m_seq_type=MSequenceType.TYPE_2_V,
                               pd_in_len=4, pd_out_len=0, od_len=1)
        self.assertEqual(master.od_len, 1)
        self.assertEqual(master.generator.od_len, 1)
        response = master.run_cycle()
        frame = uart.send_bytes.call_args.args[0]
        self.assertEqual(len(frame), 3)  # MC, CKT, OD; no PDout
        self.assertEqual(frame[1] & 0xC0, 0x80)
        self.assertEqual(response.pd, data)
        self.assertTrue(response.checksum_ok)

    def test_cli_uses_selected_profile(self):
        argv = ["nucleo_master.py", "/dev/ttyUSB0", "--baud", "230400",
                "--parity", "even", "--m-seq-type", "2_v",
                "--pd-in-len", "4", "--pd-out-len", "0", "--od-len", "1",
                "--cycles", "1", "--cycle-ms", "2", "--no-wakeup"]
        device = Mock()
        device.__enter__ = Mock(return_value=device)
        device.__exit__ = Mock(return_value=False)
        device.run_startup_sequence.return_value = True
        device.go_to_operate.return_value = True
        device.run_cycle.return_value = Mock(valid=True, pd=b"\x01\x02\x03\x04")
        device.run_cycle.return_value.has_event.return_value = False
        with patch.object(sys, "argv", argv), \
             patch.object(nucleo_master, "RealUART") as uart_cls, \
             patch.object(nucleo_master, "VirtualMaster", return_value=device) as master_cls, \
             patch.object(nucleo_master.time, "sleep"):
            self.assertEqual(nucleo_master.main(), 0)
            uart_cls.assert_called_once_with("/dev/ttyUSB0", 230400, parity="even")
            self.assertEqual(master_cls.call_args.kwargs["pd_in_len"], 4)
            self.assertEqual(master_cls.call_args.kwargs["pd_out_len"], 0)
            self.assertEqual(master_cls.call_args.kwargs["od_len"], 1)
            self.assertEqual(master_cls.call_args.kwargs["m_seq_type"], MSequenceType.TYPE_2_V)
            device.run_startup_sequence.assert_called_once_with(send_wakeup=False)


if __name__ == "__main__":
    unittest.main()
