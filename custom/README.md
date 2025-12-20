# QGroundControl Ground Control Station

## Custom Build Example

To build this sample custom version:

* Clean you build directory of any previous build
* Rename the directory from `custom-example` to `custom`
* Change to the `custom` directory
* Build QGC

![Custom Build Screenshot](README.jpg)

More details on what a custom build is and how to create your own can be found in the [QGC Dev Guide](https://dev.qgroundcontrol.com/en/custom_build/custom_build.html).

The main features of this example:

* Assumes an "Off The Shelf" purchased commercial vehicle. This means most vehicle setup is hidden from the user since they should mostly never need to adjust those things. They would be set up correctly by the vehicle producing company prior to sale.
* The above assumption cause the QGC UI to adjust and not show various things. Providing an even simpler experience to the user.
* The full experience continues to be available in "Advanced Mode".
* Brands the build with various custom images and custom color palette which matches corporate branding of the theoretical commercial company this build is for.
* Customizes portions of the interface such as you can see in the above screenshot which shows a custom instrument widget replacing the standard QGC ui.
* It also overrides various QGC Application settings to hide some settings the users shouldn't modify as well as adjusting defaults for others.
* The source code is fully commented to explain what and why it is doing things.

### Custom notes

To build and run:

Run this in the root directory of the repository
``` bash
~/Qt/6.10.0/gcc_64/bin/qt-cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

cmake --build build --config Debug

./build/Debug/Custom-QGroundControl
```

**Test video stream**

``` bash
gst-launch-1.0 -v udpsrc port=5600 address=0.0.0.0 ! "application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! autovideosink
```

### Issues

**QGC steals the stream and sinks into its own corner camera widget**

**Solutions**

* Create a custom message or use the simple existing message type to communicate the stream info, since now the custom plugin is not dependant on qgc setting up the stream, the custom video manager does all that


**Vehicle mode switches to 'unknown'**

Whenever mavlink is connected, the qgc goes crazy and toggles between 'unknown' and the vehivle actual state.
Probably the mavlink script needs to communicate properly.

```
   645.036 Checking stream index 1 name "customThermalVideo" - Custom.DataCollectionController - (DataCollectionController::_onActiveVehicleChanged(Vehicle*)::<lambda(const mavlink_message_t&)>:179)
   645.036 Warning: setStreamUri called for stream 1 URI: "udp://0.0.0.0:5601" - CustomVideoManager - (CustomVideoManager::setStreamUri:321)
   645.036 Warning: Stream 1 URI unchanged, skipping - CustomVideoManager - (CustomVideoManager::setStreamUri:329)
   645.036 Updated stream URI for "customThermalVideo" to "udp://0.0.0.0:5601" - Custom.DataCollectionController - (DataCollectionController::_onActiveVehicleChanged(Vehicle*)::<lambda(const mavlink_message_t&)>:182)
   646.035 Warning: Flight mode group not set - default - (HealthAndArmingCheckReport::update:45)
   647.035 Warning: Flight mode group not set - default - (HealthAndArmingCheckReport::update:45)
   647.035 VIDEO_STREAM_INFORMATION received: Stream Name = "customRgbVideo" URI = "udp://0.0.0.0:5600" - Custom.DataCollectionController - (DataCollectionController::_onActiveVehicleChanged(Vehicle*)::<lambda(const mavlink_message_t&)>:159)
   647.035 CustomVideoManager found, StreamNames size: 2 - Custom.DataCollectionController - (DataCollectionController::_onActiveVehicleChanged(Vehicle*)::<lambda(const mavlink_message_t&)>:170)
   647.035 Checking stream index 0 name "customRgbVideo" - Custom.DataCollectionController - (DataCollectionController::_onActiveVehicleChanged(Vehicle*)::<lambda(const mavlink_message_t&)>:179)
   647.035 Warning: setStreamUri called for stream 0 URI: "udp://0.0.0.0:5600" - CustomVideoManager - (CustomVideoManager::setStreamUri:321)
   647.03
```

Solutions:

* 


**Stream status indicators are not updating**

Whenver the data collection stops, the stream/decoding status does not trigger, why?
