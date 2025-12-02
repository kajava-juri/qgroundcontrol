#!/usr/bin/env python3
"""
Simple MAVLink test sender - sends "hello world" as custom telemetry to QGC
"""

from pymavlink import mavutil
import time

# Create connection - use udpin so we can receive QGC's replies
mav = mavutil.mavlink_connection('udpin:0.0.0.0:14551')

# Wait a moment for socket to bind
time.sleep(0.5)

# Set target to QGC's address
mav.target_system = 255  # Broadcast
mav.target_component = 0

print("Listening on UDP port 14551, will send to QGC")
print("In QGC: Go to Application Settings → Comm Links")
print("  1. Add new UDP link")
print("  2. Set 'Listening Port' to 14550")
print("  3. Add 'Target Host' 127.0.0.1:14551")
print("  4. Click Connect")
print("\nPress Ctrl+C to stop\n")

counter = 0
boot_time = int(time.time() * 1000)

try:
    while True:
        # Send heartbeat to establish vehicle connection
        mav.mav.heartbeat_send(
            mavutil.mavlink.MAV_TYPE_QUADROTOR,
            mavutil.mavlink.MAV_AUTOPILOT_GENERIC,
            0, 0, 0
        )
        
        # Send a simple counter value
        mav.mav.named_value_float_send(
            time_boot_ms=int(time.time() * 1000) - boot_time,
            name=b'test_count',
            value=float(counter)
        )
        
        print(f"Sent: heartbeat + test_count = {counter}")
        
        counter += 1
        time.sleep(1)  # Send once per second
        
except KeyboardInterrupt:
    print("\nStopped")
