#!/bin/bash
# Script to fix USB permissions for J-Link and ST-Link on Linux

echo "=== Fixing USB Permissions for Zephyr Development ==="

# 1. Add user to plugdev group
echo "Adding $USER to plugdev group..."
sudo usermod -aG plugdev $USER

# 2. Create udev rules for ST-Link (Nucleo)
echo "Installing ST-Link udev rules..."
cat <<EOF | sudo tee /etc/udev/rules.d/60-stlink.rules > /dev/null
# ST-LINK/V2-1
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="660", GROUP="plugdev", TAG+="uaccess"
# ST-LINK/V3
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374e", MODE="660", GROUP="plugdev", TAG+="uaccess"
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374f", MODE="660", GROUP="plugdev", TAG+="uaccess"
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3753", MODE="660", GROUP="plugdev", TAG+="uaccess"
EOF

# 3. Create udev rules for J-Link
echo "Installing J-Link udev rules..."
cat <<EOF | sudo tee /etc/udev/rules.d/99-jlink.rules > /dev/null
# Segger J-Link
ATTRS{idVendor}=="1366", MODE="660", GROUP="plugdev", TAG+="uaccess"
EOF

# 4. Reload udev rules
echo "Reloading udev rules..."
sudo udevadm control --reload-rules
sudo udevadm trigger

echo ""
echo "=== Success! ==="
echo "IMPORTANT: You MUST log out and log back in (or reboot) for group changes to take effect."
echo "Alternatively, you can run 'newgrp plugdev' in your current terminal to apply it immediately for that session."
