from virtual_master.crc import calculate_checksum_type0

print(f"CRC(0x00, 0x00) = 0x{calculate_checksum_type0(0x00, 0x00):02X}")
print(f"CRC(0x04, 0x00) = 0x{calculate_checksum_type0(0x04, 0x00):02X}")
print(f"CRC(0x02, 0x00) = 0x{calculate_checksum_type0(0x02, 0x00):02X}")
print(f"CRC(0x01, 0x00) = 0x{calculate_checksum_type0(0x01, 0x00):02X}")

for s in range(256):
    if calculate_checksum_type0(s, 0) == 0x26:
        print(f"Status 0x{s:02X} yields 0x26")
