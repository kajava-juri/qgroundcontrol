#!/usr/bin/env python3
"""
MAVLink camera stream sender - announces camera to QGC with video stream URL
"""

from pymavlink import mavutil
from pymavlink.dialects.v20 import ardupilotmega as mavlink2
import time

# Configuration
STREAM_URL = "http://127.0.0.1:5000/video_feed"  # Your MJPEG stream URL
CAMERA_COMPONENT_ID = 100  # MAV_COMP_ID_CAMERA (100-105 are camera components)

# Create connection - manually use MAVLink 2.0 ardupilotmega dialect
mav = mavutil.mavlink_connection('udpout:127.0.0.1:14550', dialect='ardupilotmega', source_system=1)

# Replace the MAVLink instance with v20 version
mav.mav = mavlink2.MAVLink(mav, srcSystem=1, srcComponent=1)

# Target is QGC (255 = broadcast is fine for GCS)
mav.target_system = 255
mav.target_component = 0

print("Sending to QGC on UDP port 14550 as vehicle system ID 1")
print(f"MAVLink version: {mav.WIRE_PROTOCOL_VERSION}")
print(f"Camera stream URL: {STREAM_URL}")
print("\nIn QGC: Go to Application Settings → Comm Links")
print("  1. Add new UDP link")
print("  2. Set 'Listening Port' to 14550")
print("  3. Click Connect")
print("\nWaiting for QGC connection...")
print("Press Ctrl+C to stop\n")
counter = 0
boot_time = int(time.time() * 1000)
camera_info_sent = False
stream_info_sent = False
print(f"MAVLink version: {mav.WIRE_PROTOCOL_VERSION}")

try:
    while True:
        current_time_ms = int(time.time() * 1000) - boot_time
        
        # 1. Send heartbeat from autopilot component (required for vehicle connection)
        mav.mav.srcComponent = 1
        # 1. Send heartbeat from autopilot component (required for vehicle connection)
        mav.mav.srcComponent = 1
        mav.mav.heartbeat_send(
            mavlink2.MAV_TYPE_QUADROTOR,
            mavlink2.MAV_AUTOPILOT_GENERIC,
            0, 0, 0
        )
        # 2. Send heartbeat from camera component (so QGC knows camera exists)
        mav.mav.srcComponent = CAMERA_COMPONENT_ID
        mav.mav.heartbeat_send(
            mavlink2.MAV_TYPE_CAMERA,
            mavlink2.MAV_AUTOPILOT_INVALID,
            0, 0, 0
        ) 3. Send CAMERA_INFORMATION (announces camera capabilities)
        if not camera_info_sent:
            mav.mav.srcComponent = CAMERA_COMPONENT_ID
            mav.mav.camera_information_send(
                time_boot_ms=current_time_ms,
                vendor_name=b'CustomCam\x00' + b'\x00' * 22,  # 32 bytes total
                model_name=b'DataCollection\x00' + b'\x00' * 18,  # 32 bytes total
                firmware_version=0,
                focal_length=0.0,
                sensor_size_h=0.0,
                sensor_size_v=0.0,
                resolution_h=640,
                resolution_v=480,
                lens_id=0,
                flags=1,  # CAMERA_CAP_FLAGS_HAS_VIDEO_STREAM
                cam_definition_version=0,
                cam_definition_uri=b'',
                gimbal_device_id=0
            )
            camera_info_sent = True
            print("Sent CAMERA_INFORMATION")
        
        # 4. Send VIDEO_STREAM_INFORMATION (tells QGC where to find the stream)
        if not stream_info_sent:
            mav.mav.srcComponent = CAMERA_COMPONENT_ID
            # Encode URL as bytes (max 160 chars)
            url_bytes = STREAM_URL.encode('utf-8')[:160]
            url_bytes += b'\x00' * (160 - len(url_bytes))  # Pad to 160 bytes
            
            mav.mav.video_stream_information_send(
                stream_id=1,
                count=1,  # Total number of streams
                type=1,  # VIDEO_STREAM_TYPE_RTSP (QGC will handle HTTP/MJPEG too)
                flags=1,  # VIDEO_STREAM_STATUS_FLAGS_RUNNING
                framerate=30.0,
                resolution_h=640,
                resolution_v=480,
                bitrate=0,
                rotation=0,
                hfov=0,
                name=b'Data Stream\x00' + b'\x00' * 20,  # 32 bytes
                uri=url_bytes
            )
            stream_info_sent = True
            print(f"Sent VIDEO_STREAM_INFORMATION with URL: {STREAM_URL}")
        
        # 5. Send telemetry data
        mav.mav.srcComponent = 1
        mav.mav.named_value_float_send(
            time_boot_ms=current_time_ms,
            name=b'test_count',
            value=float(counter)
        )
        
        if counter % 5 == 0:  # Print every 5 seconds to reduce spam
            print(f"Heartbeats + telemetry sent (counter={counter})")
        
        counter += 1
        time.sleep(1)  # Send once per second
except KeyboardInterrupt:

    print("\nStopped")

