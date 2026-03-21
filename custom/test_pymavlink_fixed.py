#!/usr/bin/env python3
"""
FIXED: MAVLink dual stream sender - sends VIDEO_STREAM_INFORMATION from correct component
"""

from pymavlink import mavutil
from pymavlink.dialects.v20 import ardupilotmega as mavlink2
import time
import threading
import cv2
import subprocess
import sys
import signal

# Configuration
RGB_STREAM_URL = "udp://0.0.0.0:5600"
THERMAL_STREAM_URL = "udp://0.0.0.0:5601"
CAMERA_COMPONENT_ID = 100

# Video streaming configuration
VIDEO_WIDTH = 640
VIDEO_HEIGHT = 480
VIDEO_FPS = 30
BITRATE = "2M"
UDP_HOST = "127.0.0.1"
RGB_PORT = 5600
THERMAL_PORT = 5601

# Create connection - manually use MAVLink 2.0 ardupilotmega dialect
mav = mavutil.mavlink_connection('udpout:127.0.0.1:14550', dialect='ardupilotmega', source_system=1)

# Replace the MAVLink instance with v20 version
mav.mav = mavlink2.MAVLink(mav, srcSystem=1, srcComponent=1)

# Target is QGC (255 = broadcast is fine for GCS)
mav.target_system = 255
mav.target_component = 0

print("FIXED MAVLink dual stream sender")
print("Sending to QGC on UDP port 14550 as vehicle system ID 1")
print(f"MAVLink version: {mav.WIRE_PROTOCOL_VERSION}")
print(f"RGB stream URL: {RGB_STREAM_URL}")
print(f"Thermal stream URL: {THERMAL_STREAM_URL}")
print("\nPress Ctrl+C to stop\n")

counter = 0
boot_time = int(time.time() * 1000)

class VideoStreamer:
    """Streams RGB and grayscale video from camera via FFmpeg to UDP"""
    def __init__(self):
        self.cap = None
        self.ffmpeg_rgb = None
        self.ffmpeg_thermal = None
        self.running = False
        self.thread = None

    def start(self):
        """Initialize camera and FFmpeg processes"""
        # Open camera
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            print("Error: Cannot open camera")
            return False

        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT)
        self.cap.set(cv2.CAP_PROP_FPS, VIDEO_FPS)

        # FFmpeg command for RGB stream (H.264 RTP/UDP)
        ffmpeg_rgb_cmd = [
            'ffmpeg', '-y', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'bgr24',
            '-s', f'{VIDEO_WIDTH}x{VIDEO_HEIGHT}', '-r', str(VIDEO_FPS), '-i', '-',
            '-c:v', 'libx264', '-preset', 'ultrafast', '-tune', 'zerolatency',
            '-b:v', BITRATE, '-maxrate', BITRATE, '-bufsize', '1M',
            '-g', str(VIDEO_FPS), '-keyint_min', str(VIDEO_FPS),
            '-sc_threshold', '0', '-bf', '0', '-f', 'rtp', f'rtp://{UDP_HOST}:{RGB_PORT}'
        ]

        # FFmpeg command for thermal/grayscale stream
        ffmpeg_thermal_cmd = [
            'ffmpeg', '-y', '-f', 'rawvideo', '-vcodec', 'rawvideo', '-pix_fmt', 'gray',
            '-s', f'{VIDEO_WIDTH}x{VIDEO_HEIGHT}', '-r', str(VIDEO_FPS), '-i', '-',
            '-c:v', 'libx264', '-preset', 'ultrafast', '-tune', 'zerolatency',
            '-b:v', BITRATE, '-maxrate', BITRATE, '-bufsize', '1M',
            '-g', str(VIDEO_FPS), '-keyint_min', str(VIDEO_FPS),
            '-sc_threshold', '0', '-bf', '0', '-f', 'rtp', f'rtp://{UDP_HOST}:{THERMAL_PORT}'
        ]

        try:
            self.ffmpeg_rgb = subprocess.Popen(ffmpeg_rgb_cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
            self.ffmpeg_thermal = subprocess.Popen(ffmpeg_thermal_cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
        except FileNotFoundError:
            print("Error: FFmpeg not found. Install with: sudo apt install ffmpeg")
            return False

        self.running = True
        print(f"Video streaming started:")
        print(f"  RGB stream: rtp://{UDP_HOST}:{RGB_PORT}")
        print(f"  Thermal stream: rtp://{UDP_HOST}:{THERMAL_PORT}")

        # Start streaming thread
        self.thread = threading.Thread(target=self._stream_loop, daemon=True)
        self.thread.start()
        return True

    def _stream_loop(self):
        """Main streaming loop (runs in separate thread)"""
        frame_count = 0
        while self.running:
            ret, frame = self.cap.read()
            if not ret:
                print("Error: Cannot read frame from camera")
                break

            # Convert to grayscale for thermal stream
            gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

            try:
                self.ffmpeg_rgb.stdin.write(frame.tobytes())
                self.ffmpeg_thermal.stdin.write(gray_frame.tobytes())
                frame_count += 1

                if frame_count % (VIDEO_FPS * 5) == 0:
                    print(f"  Streamed {frame_count} frames")
            except (BrokenPipeError, Exception) as e:
                print(f"Streaming error: {e}")
                break

    def stop(self):
        """Cleanup resources"""
        print("Stopping video streamer...")
        self.running = False

        if self.thread:
            self.thread.join(timeout=2)

        for proc in [self.ffmpeg_rgb, self.ffmpeg_thermal]:
            if proc:
                try:
                    proc.stdin.close()
                    proc.terminate()
                    proc.wait(timeout=2)
                except:
                    pass

        if self.cap:
            self.cap.release()
        print("Video streaming stopped")

# Global video streamer instance
video_streamer = VideoStreamer()

def handle_command(msg):
    """Handle incoming MAVLink commands and send ACK"""
    if msg.get_type() == 'COMMAND_LONG':
        cmd_id = msg.command
        ack_component = msg.target_component if msg.target_component != 0 else 1

        if cmd_id == mavlink2.MAV_CMD_REQUEST_MESSAGE:
            requested_msg_id = int(msg.param1)

            if requested_msg_id == mavlink2.MAVLINK_MSG_ID_AUTOPILOT_VERSION:
                result = mavlink2.MAV_RESULT_ACCEPTED
                mav.mav.srcComponent = 1  # Autopilot version from autopilot
                mav.mav.autopilot_version_send(
                    capabilities=(mavlink2.MAV_PROTOCOL_CAPABILITY_MAVLINK2 |
                                  mavlink2.MAV_PROTOCOL_CAPABILITY_MISSION_FLOAT |
                                  mavlink2.MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT |
                                  mavlink2.MAV_PROTOCOL_CAPABILITY_COMMAND_INT),
                    flight_sw_version=0x01000000,
                    middleware_sw_version=0, os_sw_version=0, board_version=0,
                    flight_custom_version=b'\x00' * 8,
                    middleware_custom_version=b'\x00' * 8,
                    os_custom_version=b'\x00' * 8,
                    vendor_id=0, product_id=0, uid=0, uid2=b'\x00' * 18
                )
                print(f"Sent AUTOPILOT_VERSION")

            elif requested_msg_id == 269:  # VIDEO_STREAM_INFORMATION
                result = mavlink2.MAV_RESULT_ACCEPTED

                # ✅ CRITICAL FIX: Send from CAMERA component, not autopilot!
                mav.mav.srcComponent = CAMERA_COMPONENT_ID

                # Stream 1: RGB
                mav.mav.video_stream_information_send(
                    stream_id=1, count=2, type=mavlink2.VIDEO_STREAM_TYPE_RTPUDP,
                    flags=mavlink2.VIDEO_STREAM_STATUS_FLAGS_RUNNING,
                    framerate=30.0, resolution_h=640, resolution_v=480, bitrate=2000000,
                    rotation=0, hfov=90,
                    name="RGB Camera".encode('utf-8').ljust(32, b'\x00'),
                    uri=RGB_STREAM_URL.encode('utf-8').ljust(160, b'\x00')
                )

                # Stream 2: Thermal
                mav.mav.video_stream_information_send(
                    stream_id=2, count=2, type=mavlink2.VIDEO_STREAM_TYPE_RTPUDP,
                    flags=mavlink2.VIDEO_STREAM_STATUS_FLAGS_RUNNING | mavlink2.VIDEO_STREAM_STATUS_FLAGS_THERMAL,
                    framerate=30.0, resolution_h=640, resolution_v=480, bitrate=2000000,
                    rotation=0, hfov=90,
                    name="Thermal Camera".encode('utf-8').ljust(32, b'\x00'),
                    uri=THERMAL_STREAM_URL.encode('utf-8').ljust(160, b'\x00')
                )
                print(f"Sent VIDEO_STREAM_INFORMATION from component {CAMERA_COMPONENT_ID}")

            elif requested_msg_id == 259:  # CAMERA_INFORMATION
                result = mavlink2.MAV_RESULT_ACCEPTED

                # CRITICAL: Must send from camera component!
                current_time_ms = int(time.time() * 1000) - boot_time

                # Create a fresh MAVLink message with correct source component
                camera_info_msg = mav.mav.camera_information_encode(
                    time_boot_ms=current_time_ms,
                    vendor_name='CustomCam'.encode('utf-8').ljust(32, b'\x00'),
                    model_name='DualStream'.encode('utf-8').ljust(32, b'\x00'),
                    firmware_version=0, focal_length=0.0,
                    sensor_size_h=0.0, sensor_size_v=0.0,
                    resolution_h=640, resolution_v=480, lens_id=0,
                    flags=mavlink2.CAMERA_CAP_FLAGS_HAS_VIDEO_STREAM,
                    cam_definition_version=0, cam_definition_uri=b'', gimbal_device_id=0
                )
                # Override the source component
                camera_info_msg.pack(mav.mav, mav.target_system, CAMERA_COMPONENT_ID)
                mav.write(camera_info_msg.get_msgbuf())
                print(f"Sent CAMERA_INFORMATION from component {CAMERA_COMPONENT_ID}")
            else:
                result = mavlink2.MAV_RESULT_UNSUPPORTED

        elif cmd_id == 521:  # MAV_CMD_SET_MESSAGE_INTERVAL
            result = mavlink2.MAV_RESULT_ACCEPTED
        else:
            result = mavlink2.MAV_RESULT_UNSUPPORTED

        # Send ACK from the component that was targeted
        mav.mav.srcComponent = ack_component
        mav.mav.command_ack_send(cmd_id, result, 0, 0, msg.target_system, msg.target_component)

# Start video streaming
if not video_streamer.start():
    print("Failed to start video streaming")
    sys.exit(1)

try:
    while True:
        current_time_ms = int(time.time() * 1000) - boot_time

        msg = mav.recv_match(blocking=False)
        if msg:
            handle_command(msg)

        # 1. Send heartbeat from BOTH autopilot and camera
        mav.mav.srcComponent = 1
        mav.mav.heartbeat_send(mavlink2.MAV_TYPE_GENERIC, mavlink2.MAV_AUTOPILOT_GENERIC,
                               0, 0, mavlink2.MAV_STATE_ACTIVE)

        mav.mav.srcComponent = CAMERA_COMPONENT_ID
        mav.mav.heartbeat_send(mavlink2.MAV_TYPE_CAMERA, mavlink2.MAV_AUTOPILOT_INVALID,
                               0, 0, mavlink2.MAV_STATE_ACTIVE)

        # 2. ✅ CRITICAL FIX: Send VIDEO_STREAM_INFORMATION from CAMERA component!
        if counter % 2 == 0:
            mav.mav.srcComponent = CAMERA_COMPONENT_ID  # Not component 1!

            # Stream 1: RGB
            mav.mav.video_stream_information_send(
                stream_id=1, count=2, type=mavlink2.VIDEO_STREAM_TYPE_RTPUDP,
                flags=mavlink2.VIDEO_STREAM_STATUS_FLAGS_RUNNING,
                framerate=30.0, resolution_h=640, resolution_v=480, bitrate=2000000,
                rotation=0, hfov=90,
                name="RGB Camera".encode('utf-8').ljust(32, b'\x00'),
                uri=RGB_STREAM_URL.encode('utf-8').ljust(160, b'\x00')
            )

            # Stream 2: Thermal
            mav.mav.video_stream_information_send(
                stream_id=2, count=2, type=mavlink2.VIDEO_STREAM_TYPE_RTPUDP,
                flags=mavlink2.VIDEO_STREAM_STATUS_FLAGS_RUNNING | mavlink2.VIDEO_STREAM_STATUS_FLAGS_THERMAL,
                framerate=30.0, resolution_h=640, resolution_v=480, bitrate=2000000,
                rotation=0, hfov=90,
                name="Thermal Camera".encode('utf-8').ljust(32, b'\x00'),
                uri=THERMAL_STREAM_URL.encode('utf-8').ljust(160, b'\x00')
            )

        # 3. Send telemetry data
        mav.mav.srcComponent = 1
        mav.mav.named_value_float_send(
            time_boot_ms=current_time_ms,
            name=b'test_count',
            value=float(counter)
        )

        if counter % 5 == 0:
            print(f"Heartbeats sent (counter={counter})")

        counter += 1
        time.sleep(1)

except KeyboardInterrupt:
    print("\nStopping...")
finally:
    video_streamer.stop()
    print("Stopped")
