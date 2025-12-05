import QtQuick
import QtMultimedia
import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    // Property to receive the video stream URI from parent
    property string streamUri: ""

    // Access the global VideoManager
    property var videoManager: QGroundControl.videoManager
    property var videoSettings: QGroundControl.settingsManager.videoSettings
    property var mavlinkManager: QGroundControl.mavlinkManager
    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var videoReceiver: videoManager ? videoManager.videoReceiver : null

    onStreamUriChanged: {
        // console.log("CustomVideoStream: Stream URI changed to:", streamUri);
        // You can handle the URI change here, e.g., start/stop video stream
    }

    VideoOutput {
        anchors.fill:   parent

        // Optional overlay showing stream info
        Text {
            text: root.streamUri !== "" ?
                  "Stream URI: " + root.streamUri :
                  ((root.videoReceiver && root.videoReceiver.videoRunning) ?
                   "Streaming: " + root.videoReceiver.videoSource :
                   "No Video")
            color: "white"
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            padding: 10
        }

        Component.onCompleted: {
            console.log("CustomVideoStream component loaded");
            console.log("VideoManager:", root.videoManager);
            console.log("VideoReceiver:", root.videoReceiver);
        }
    }

    Connections {
        target: root.mavlinkManager

        function onMessageReceived(message) {
            if (message.name === "VIDEO_STREAM_INFORMATION") {
                console.log("Received VIDEO_STREAM_INFORMATION message:", message);
            }
        }
    }
}