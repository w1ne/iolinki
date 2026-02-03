#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Basic example of using the Virtual IO-Link Master.

This demonstrates how to:
1. Create a Virtual Master
2. Get the Device TTY path
3. Run startup sequence
4. Perform basic communication
"""

import sys
import time
from virtual_master.master import VirtualMaster


def main():
    print("=== Virtual IO-Link Master - Basic Example ===
")
    
    # Create Virtual Master
    with VirtualMaster() as master:
        print(f"Device should connect to: {master.get_device_tty()}
")
        print("Start your Device application with:")
        print(f"  ./build/examples/host_demo/host_demo {master.get_device_tty()}
")
        print("Waiting 5 seconds for Device to start...")
        time.sleep(5)
        
        # Run startup sequence
        if master.run_startup_sequence():
            print("
[SUCCESS] Startup complete!
")
            
            # Run a few communication cycles
            print("Running 10 communication cycles...")
            for i in range(10):
                response = master.run_cycle()
                
                if response.has_event():
                    print(f"  Cycle {i}: Device has pending event!")
                    event_code = master.request_event()
                    if event_code:
                        print(f"  Event code: 0x{event_code:04X}")
                
                time.sleep(0.01)  # 10ms cycle time
            
            print("
[DONE] Communication test complete")
        else:
            print("
[FAILED] Startup sequence failed")
            return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
