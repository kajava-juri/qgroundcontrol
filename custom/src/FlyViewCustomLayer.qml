/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

import Custom.Widgets

Item {
    property var parentToolInsets                       // These insets tell you what screen real estate is available for positioning the controls in your overlay
    property var totalToolInsets:   _totalToolInsets    // The insets updated for the custom overlay additions
    property var mapControl

    readonly property string noGPS:         qsTr("NO GPS")
    readonly property real   indicatorValueWidth:   ScreenTools.defaultFontPixelWidth * 7

    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property real   _indicatorDiameter:     ScreenTools.defaultFontPixelWidth * 18
    property real   _indicatorsHeight:      ScreenTools.defaultFontPixelHeight
    property var    _sepColor:              qgcPal.globalTheme === QGCPalette.Light ? Qt.rgba(0,0,0,0.5) : Qt.rgba(1,1,1,0.5)
    property color  _indicatorsColor:       qgcPal.text
    property bool   _isVehicleGps:          _activeVehicle ? _activeVehicle.gps.count.rawValue > 1 && _activeVehicle.gps.hdop.rawValue < 1.4 : false
    property string _altitude:              _activeVehicle ? (isNaN(_activeVehicle.altitudeRelative.value) ? "0.0" : _activeVehicle.altitudeRelative.value.toFixed(1)) + ' ' + _activeVehicle.altitudeRelative.units : "0.0"
    property string _distanceStr:           isNaN(_distance) ? "0" : _distance.toFixed(0) + ' ' + QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
    property real   _heading:               _activeVehicle   ? _activeVehicle.heading.rawValue : 0
    property real   _distance:              _activeVehicle ? _activeVehicle.distanceToHome.rawValue : 0
    property string _messageTitle:          ""
    property string _messageText:           ""
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property var    _customSettings: QGroundControl.corePlugin.customSettings
    property var    _customVideoManager: QGroundControl.corePlugin.customVideoManager

    function secondsToHHMMSS(timeS) {
        var sec_num = parseInt(timeS, 10);
        var hours   = Math.floor(sec_num / 3600);
        var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
        var seconds = sec_num - (hours * 3600) - (minutes * 60);
        if (hours   < 10) {hours   = "0"+hours;}
        if (minutes < 10) {minutes = "0"+minutes;}
        if (seconds < 10) {seconds = "0"+seconds;}
        return hours+':'+minutes+':'+seconds;
    }

    DataCollectionController {
        id: dataController
    }

    // Dual video streams for data collection
    Rectangle {
        id: dualVideoWidget
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: ScreenTools.defaultFontPixelWidth
        width: 640
        height: 240
        color: "black"
        border.color: "white"
        border.width: 2

        Component.onCompleted: {
            console.log("DualVideoWidget: Initializing...")
            // Get CustomVideoManager from corePlugin
            // if (QGroundControl.corePlugin && _customVideoManager) {
            //     // Initialize streams with our video items
            //     // _customVideoManager.initializeStreams(rgbVideoItem, thermalVideoItem)

            //     // Start both streams
            //     _customVideoManager.startStream(0)  // RGB stream
            //     _customVideoManager.startStream(1)  // Thermal stream

            //     console.log("DualVideoWidget: Initialized and started streams")
            // } else {
            //     console.warn("DualVideoWidget: CustomVideoManager not available!")
            // }
        }

        Row {
            anchors.fill: parent
            spacing: 2

            // RGB Video
            Rectangle {
                width: parent.width / 2 - 1
                height: parent.height
                color: "black"
                border.color: "green"
                border.width: 1

                Item {
                    id: rgbVideoItem
                    objectName: "customRgbVideo"
                    anchors.fill: parent

                    Text {
                        anchors.centerIn: parent
                        text: "RGB Camera\n(Port 5600)"
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Thermal Video
            Rectangle {
                width: parent.width / 2 - 1
                height: parent.height
                color: "black"
                border.color: "red"
                border.width: 1

                Item {
                    id: thermalVideoItem
                    objectName: "customThermalVideo"
                    anchors.fill: parent

                    Text {
                        anchors.centerIn: parent
                        text: "Thermal Camera\n(Port 5601)"
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // Detailed Status Panel (for debugging)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 258
        width: debugColumn.width + 16
        height: debugColumn.height + 16
        color: "black"
        opacity: 0.85
        radius: 4
        border.color: "white"
        border.width: 1

        Column {
            id: debugColumn
            anchors.centerIn: parent
            spacing: 4

            QGCLabel {
                text: "Stream Status"
                color: "cyan"
                font.bold: true
            }

            QGCLabel {
                text: "Manager: " + (_customVideoManager ? "✓" : "✗")
                color: _customVideoManager ? "lime" : "red"
            }

            Rectangle { width: 150; height: 1; color: "gray" }

            QGCLabel {
                text: "RGB (0):"
                color: "white"
                font.bold: true
            }
            QGCLabel {
                text: "  URI: " + (_customVideoManager ? _customVideoManager.getStreamUri(0) : "N/A")
                color: "white"
                font.pixelSize: ScreenTools.smallFontPointSize
            }
            QGCLabel {
                text: "  Active: " + (_customVideoManager && _customVideoManager.isStreamActive(0) ? "✓" : "✗")
                color: (_customVideoManager && _customVideoManager.isStreamActive(0)) ? "lime" : "red"
            }
            QGCLabel {
                text: "  Decoding: " + (_customVideoManager && _customVideoManager.isStreamDecoding(0) ? "✓" : "✗")
                color: (_customVideoManager && _customVideoManager.isStreamDecoding(0)) ? "lime" : "red"
            }

            Rectangle { width: 150; height: 1; color: "gray" }

            QGCLabel {
                text: "Thermal (1):"
                color: "white"
                font.bold: true
            }
            QGCLabel {
                text: "  URI: " + (_customVideoManager ? _customVideoManager.getStreamUri(1) : "N/A")
                color: "white"
                font.pixelSize: ScreenTools.smallFontPointSize
            }
            QGCLabel {
                text: "  Active: " + (_customVideoManager && _customVideoManager.isStreamActive(1) ? "✓" : "✗")
                color: (_customVideoManager && _customVideoManager.isStreamActive(1)) ? "lime" : "red"
            }
            QGCLabel {
                text: "  Decoding: " + (_customVideoManager && _customVideoManager.isStreamDecoding(1) ? "✓" : "✗")
                color: (_customVideoManager && _customVideoManager.isStreamDecoding(1)) ? "lime" : "red"
            }
        }

        // Click to hide (optional)
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: parent.visible = false
        }
    }
    }

    QGCToolInsets {
        id:                     _totalToolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    exampleRectangle.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parent.width - compassBackground.x
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     compassArrowIndicator.y + compassArrowIndicator.height
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parent.height - attitudeIndicator.y
    }

    // Row {
    //     anchors.right: parent.right
    //     anchors.top: parent.top
    //     anchors.topMargin: ScreenTools.defaultFontPixelWidth * 4
    //     anchors.margins: ScreenTools.defaultFontPixelWidth
    //     spacing: ScreenTools.defaultFontPixelWidth / 2
        


    //     // Text {
    //     //     anchors.verticalCenter: parent.verticalCenter
    //     //     text: dataController.recordingTime
    //     //     color: "white"
    //     //     visible: dataController.isCollecting
    //     // }
    // }

    Rectangle {
        width: 300
        height: 240
        color: qgcPal.window

        ValueAxis {
            id: axisX
            min: 0
            max: 100
        }

        ValueAxis {
            id: axisY
            min: -20
            max: 20
        }

        LineSeries {
            name: "Accel X"
            axisX: axisX
            axisY: axisY
            color: "#e03131"
        }
        
        LineSeries {
            name: "Accel Y"
            axisX: axisX
            axisY: axisY
            color: "#12b886"
        }

        LineSeries {
            name: "Accel Z"
            axisX: axisX
            axisY: axisY
            color: "#228be6"
        }

        Connections {
            target: dataController
            function onImuDataChanged(x, y, z) {
                // Update series

            }
        }
    }

    Rectangle {
        id: rgbCameraView
        width: 300
        height: 240
        color: "black"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 248

        property var _cameraManager: _activeVehicle ? _activeVehicle.cameraManager : null
        property var _currentStream: _cameraManager ? _cameraManager.currentStreamInstance : null
        property string _streamUri: _currentStream ? _currentStream.uri : ""

        // CustomVideoStream {
        //     id: mjpegStream
        //     anchors.fill: parent
        //     streamUri: dataController.videoStreamUri
        // }

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: (rgbCameraView._currentStream && rgbCameraView._currentStream.isActive && mjpegStream.playing && mjpegStream.status !== AnimatedImage.Error) ? "#12b886" : "#e03131"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 5
        }
    }


    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 64
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelHeight

        QGCButton {
            text: qsTr("Data Collection Settings")
            onClicked: {
                dataCollectionDialogComponent.createObject(mainWindow).open()
            }
        }

        Rectangle {
            width: ScreenTools.defaultFontPixelWidth * 18
            height: ScreenTools.defaultFontPixelHeight * 2
            color: dataController.isCollecting ? "#e03131" : "#12b886"
            radius: 4
            
            Text {
                anchors.centerIn: parent
                text: dataController.isCollecting ? "Stop Recording" : "Start Recording"
                color: "white"
            }
            
            MouseArea {
                anchors.fill: parent
                onClicked: dataController.toggleRecording()
            }
        }

        Text {
            text: "Test Value: " + dataController.testValue
            color: "white"
            font.pixelSize: 24
        }
        

        Component {
            id: dataCollectionDialogComponent
            QGCPopupDialog {
                title: qsTr("Data Collection Settings")
                buttons: Dialog.Close

                SettingsGroupLayout {
                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("HTTP URL")
                        fact: _customSettings ? _customSettings.httpUrl : null
                        visible: fact !== null
                    }

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("WebSocket URL")
                        fact: _customSettings ? _customSettings.webSocketUrl : null
                        visible: fact !== null
                    }
                }
            }
        }
    }

    // This is an example of how you can use parent tool insets to position an element on the custom fly view layer
    // - we use parent topEdgeLeftInset to position the widget below the toolstrip
    // - we use parent bottomEdgeLeftInset to dodge the virtual joystick if enabled
    // - we use the parent leftEdgeTopInset to size our element to the same width as the ToolStripAction
    // - we export the width of this element as the leftEdgeCenterInset so that the map will recenter if the vehicle flys behind this element
    Rectangle {
        id: exampleRectangle
        visible: false // to see this example, set this to true. To view insets, enable the insets viewer FlyView.qml
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: parentToolInsets.topEdgeLeftInset + _toolsMargin
        anchors.bottomMargin: parentToolInsets.bottomEdgeLeftInset + _toolsMargin
        anchors.leftMargin: _toolsMargin
        width: parentToolInsets.leftEdgeTopInset - _toolsMargin
        color: 'red'

        property real leftEdgeCenterInset: visible ? x + width : 0
    }

    //-------------------------------------------------------------------------
    //-- Heading Indicator
    Rectangle {
        id:                         compassBar
        height:                     ScreenTools.defaultFontPixelHeight * 1.5
        width:                      ScreenTools.defaultFontPixelWidth  * 50
        anchors.bottom:             parent.bottom
        anchors.bottomMargin:       _toolsMargin
        color:                      "#DEDEDE"
        radius:                     2
        clip:                       true
        anchors.horizontalCenter:   parent.horizontalCenter
        Repeater {
            model: 720
            QGCLabel {
                function _normalize(degrees) {
                    var a = degrees % 360
                    if (a < 0) a += 360
                    return a
                }
                property int _startAngle: modelData + 180 + _heading
                property int _angle: _normalize(_startAngle)
                anchors.verticalCenter: parent.verticalCenter
                x:              visible ? ((modelData * (compassBar.width / 360)) - (width * 0.5)) : 0
                visible:        _angle % 45 == 0
                color:          "#75505565"
                font.pointSize: ScreenTools.smallFontPointSize
                text: {
                    switch(_angle) {
                    case 0:     return "N"
                    case 45:    return "NE"
                    case 90:    return "E"
                    case 135:   return "SE"
                    case 180:   return "S"
                    case 225:   return "SW"
                    case 270:   return "W"
                    case 315:   return "NW"
                    }
                    return ""
                }
            }
        }
    }
    Rectangle {
        id:                         headingIndicator
        height:                     ScreenTools.defaultFontPixelHeight
        width:                      ScreenTools.defaultFontPixelWidth * 4
        color:                      qgcPal.windowShadeDark
        anchors.top:                compassBar.top
        anchors.topMargin:          -headingIndicator.height / 2
        anchors.horizontalCenter:   parent.horizontalCenter
        QGCLabel {
            text:                   _heading
            color:                  qgcPal.text
            font.pointSize:         ScreenTools.smallFontPointSize
            anchors.centerIn:       parent
        }
    }
    Image {
        id:                         compassArrowIndicator
        height:                     _indicatorsHeight
        width:                      height
        source:                     "/custom/img/compass_pointer.svg"
        fillMode:                   Image.PreserveAspectFit
        sourceSize.height:          height
        anchors.top:                compassBar.bottom
        anchors.topMargin:          -height / 2
        anchors.horizontalCenter:   parent.horizontalCenter
    }

    Rectangle {
        id:                     compassBackground
        anchors.bottom:         attitudeIndicator.bottom
        anchors.right:          attitudeIndicator.left
        anchors.rightMargin:    -attitudeIndicator.width / 2
        width:                  -anchors.rightMargin + compassBezel.width + (_toolsMargin * 2)
        height:                 attitudeIndicator.height * 0.75
        radius:                 2
        color:                  qgcPal.window

        Rectangle {
            id:                     compassBezel
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin:     _toolsMargin
            anchors.left:           parent.left
            width:                  height
            height:                 parent.height - (northLabelBackground.height / 2) - (headingLabelBackground.height / 2)
            radius:                 height / 2
            border.color:           qgcPal.text
            border.width:           1
            color:                  Qt.rgba(0,0,0,0)
        }

        Rectangle {
            id:                         northLabelBackground
            anchors.top:                compassBezel.top
            anchors.topMargin:          -height / 2
            anchors.horizontalCenter:   compassBezel.horizontalCenter
            width:                      northLabel.contentWidth * 1.5
            height:                     northLabel.contentHeight * 1.5
            radius:                     ScreenTools.defaultFontPixelWidth  * 0.25
            color:                      qgcPal.windowShade

            QGCLabel {
                id:                 northLabel
                anchors.centerIn:   parent
                text:               "N"
                color:              qgcPal.text
                font.pointSize:     ScreenTools.smallFontPointSize
            }
        }

        Image {
            id:                 headingNeedle
            anchors.centerIn:   compassBezel
            height:             compassBezel.height * 0.75
            width:              height
            source:             "/custom/img/compass_needle.svg"
            fillMode:           Image.PreserveAspectFit
            sourceSize.height:  height
            transform: [
                Rotation {
                    origin.x:   headingNeedle.width  / 2
                    origin.y:   headingNeedle.height / 2
                    angle:      _heading
                }]
        }

        Rectangle {
            id:                         headingLabelBackground
            anchors.top:                compassBezel.bottom
            anchors.topMargin:          -height / 2
            anchors.horizontalCenter:   compassBezel.horizontalCenter
            width:                      headingLabel.contentWidth * 1.5
            height:                     headingLabel.contentHeight * 1.5
            radius:                     ScreenTools.defaultFontPixelWidth  * 0.25
            color:                      qgcPal.windowShade

            QGCLabel {
                id:                 headingLabel
                anchors.centerIn:   parent
                text:               _heading
                color:              qgcPal.text
                font.pointSize:     ScreenTools.smallFontPointSize
            }
        }
    }

    Rectangle {
        id:                     attitudeIndicator
        anchors.bottomMargin:   _toolsMargin + parentToolInsets.bottomEdgeRightInset
        anchors.rightMargin:    _toolsMargin
        anchors.bottom:         parent.bottom
        anchors.right:          parent.right
        height:                 ScreenTools.defaultFontPixelHeight * 6
        width:                  height
        radius:                 height * 0.5
        color:                  qgcPal.windowShade

        CustomAttitudeWidget {
            size:               parent.height * 0.95
            vehicle:            _activeVehicle
            showHeading:        false
            anchors.centerIn:   parent
        }
    }
}
