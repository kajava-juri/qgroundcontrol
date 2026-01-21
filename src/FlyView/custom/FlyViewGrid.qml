/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

import Custom.Widgets

// Simple grid container - shows a basic 2x2 grid of colored rectangles for testing
Item {
    id: root
    visible: false  // Start hidden

    // Properties passed from parent (FlyView)
    property var planMasterController: null  // Required for map cell

    // Expose the grid state to external access
    property alias gridState: _gridState

    property var dataController: null
    property var _customVideoManager: QGroundControl.corePlugin.customVideoManager
    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle 
    
    // Stream status properties
    property bool   _rgbActive: false
    property bool   _rgbDecoding: false
    property bool   _thermalActive: false
    property bool   _thermalDecoding: false
    property string _rgbUri: ""
    property string _thermalUri: ""

    // Listen to CustomVideoManager signals
    Connections {
        target: _customVideoManager

        function onStreamStateChanged(streamIndex, active) {
            if (streamIndex === 0) {
                _rgbActive = active
            } else if (streamIndex === 1) {
                _thermalActive = active
            }
        }

        function onStreamDecodingChanged(streamIndex, decoding) {
            if (streamIndex === 0) {
                _rgbDecoding = decoding
            } else if (streamIndex === 1) {
                _thermalDecoding = decoding
            }
        }

        function onStreamUriChanged(streamIndex, uri) {
            if (streamIndex === 0) {
                _rgbUri = uri
            } else if (streamIndex === 1) {
                _thermalUri = uri
            }
        }
    }

    // Initialize URIs on startup
    Component.onCompleted: {
        if (_customVideoManager) {
            _rgbUri = _customVideoManager.getStreamUri(0)
            _thermalUri = _customVideoManager.getStreamUri(1)
        }
    }

    // Grid state manager
    GridState {
        id: _gridState
        gridView: root
    }

    GridLayout {
        id: gridContainer
        anchors.fill: parent
        anchors.top: parent.top
        columns: 3  // 3 columns = 3x2 grid
        columnSpacing: 4
        rowSpacing: 4

        // Row 1: Map, Attitude, Telemetry
        GridMapCell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            border.color: "cyan"
            border.width: 2
            planController: root.planMasterController
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1e1e1e"
            border.color: "white"
            border.width: 2

            property real _heading: _activeVehicle ? _activeVehicle.heading.rawValue : 0

            CustomAttitudeWidget {
                anchors.centerIn: parent
                size: Math.min(parent.width, parent.height) * 0.75
                vehicle: _activeVehicle
                showHeading:        false
            }

            // Compass needle overlay
            Image {
                id: headingNeedle
                anchors.centerIn: parent
                height: Math.min(parent.width, parent.height) * 0.5
                width: height
                source: "/custom/img/compass_needle.svg"
                fillMode: Image.PreserveAspectFit
                sourceSize.height: height
                opacity: 0.7
                transform: Rotation {
                    origin.x: headingNeedle.width / 2
                    origin.y: headingNeedle.height / 2
                    angle: _heading
                }
            }

            // Heading label in top-right corner
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                width: headingLabel.contentWidth + ScreenTools.defaultFontPixelWidth
                height: headingLabel.contentHeight + ScreenTools.defaultFontPixelHeight * 0.5
                radius: 4
                color: qgcPal.windowShade
                border.color: "cyan"
                border.width: 1

                QGCLabel {
                    id: headingLabel
                    anchors.centerIn: parent
                    text: _heading.toFixed(0) + "°"
                    color: "cyan"
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.bold: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2b2d30"
            border.color: "white"
            border.width: 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelHeight * 0.5
                spacing: ScreenTools.defaultFontPixelHeight * 0.3

                QGCLabel {
                    text: "Flight Data"
                    font.bold: true
                    color: "cyan"
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "gray" }

                // Altitude
                Row {
                    spacing: ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        text: "ALT:"
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                    }
                    QGCLabel {
                        text: _activeVehicle ? _activeVehicle.altitudeRelative.valueString + " " + _activeVehicle.altitudeRelative.units : "---"
                        color: "lime"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.bold: true
                    }
                }

                // Ground speed
                Row {
                    spacing: ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        text: "GS:"
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                    }
                    QGCLabel {
                        text: _activeVehicle ? _activeVehicle.groundSpeed.valueString + " " + _activeVehicle.groundSpeed.units : "---"
                        color: "lime"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.bold: true
                    }
                }

                // Heading
                Row {
                    spacing: ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        text: "HDG:"
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                    }
                    QGCLabel {
                        text: _activeVehicle ? _activeVehicle.heading.valueString + "°" : "---"
                        color: "lime"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.bold: true
                    }
                }

                // GPS
                Row {
                    spacing: ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        text: "GPS:"
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                    }
                    QGCLabel {
                        text: _activeVehicle && _activeVehicle.gps.count.rawValue > 0 ? 
                              _activeVehicle.gps.count.valueString + " sats" : "No GPS"
                        color: _activeVehicle && _activeVehicle.gps.count.rawValue >= 6 ? "lime" : "red"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        font.bold: true
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Row 2: RGB Camera, Thermal Camera, Controls
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.visible
            sourceComponent: CustomVideoStream {
                streamObjectName: "customRgbVideo"
                streamLabel: "RGB Camera"
                borderColor: "green"
            }
        }

        // Cell 3: Thermal Video Stream
        // Only create when grid is visible to avoid conflicts with CustomLayer
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.visible
            sourceComponent: CustomVideoStream {
                streamObjectName: "customThermalVideo"
                streamLabel: "Thermal Camera"
                borderColor: "red"
            }
        }

        // Cell 4: Data Collection Controls
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2b2d30"
            border.color: "black"
            border.width: 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelHeight * 0.5
                spacing: ScreenTools.defaultFontPixelHeight * 0.5

                // Button row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight * 0.5

                    QGCButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: parent.width / 2
                        text: qsTr("Settings")
                        onClicked: {
                            var dialog = dataCollectionDialogComponent.createObject(mainWindow, {
                                "_customSettings": QGroundControl.corePlugin.customSettings
                            })
                            dialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: parent.width / 2
                        Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2
                        color: dataController.isCollecting ? "#e03131" : "#12b886"
                        radius: 4

                        Text {
                            anchors.centerIn: parent
                            text: dataController.isCollecting ? "■ Stop" : "● Start"
                            color: "white"
                            font.pixelSize: ScreenTools.defaultFontPixelHeight
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: dataController.toggleRecording()
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                QGCLabel {
                    text: "Stream Status"
                    color: "cyan"
                    font.bold: true
                }

                QGCLabel {
                    text: "Manager: " + (_customVideoManager ? "✓" : "✗")
                    color: _customVideoManager ? "lime" : "red"
                    font.pixelSize: ScreenTools.smallFontPointSize
                }

                Rectangle { 
                    Layout.fillWidth: true
                    height: 1
                    color: "gray"
                }

                // RGB stream details
                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    QGCLabel {
                        text: "RGB (0):"
                        color: "white"
                        font.bold: true
                    }
                    QGCLabel {
                        text: "  URI: " + (_rgbUri || "N/A")
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        elide: Text.ElideMiddle
                        width: parent.width
                    }
                    Row {
                        spacing: 8
                        QGCLabel {
                            text: "  Active:"
                            color: "white"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: _rgbActive ? "✓" : "✗"
                            color: _rgbActive ? "lime" : "red"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: "Decoding:"
                            color: "white"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: _rgbDecoding ? "✓" : "✗"
                            color: _rgbDecoding ? "lime" : "red"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                    }
                }

                Rectangle { 
                    Layout.fillWidth: true
                    height: 1
                    color: "gray"
                }

                // Thermal stream details
                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    QGCLabel {
                        text: "Thermal (1):"
                        color: "white"
                        font.bold: true
                    }
                    QGCLabel {
                        text: "  URI: " + (_thermalUri || "N/A")
                        color: "white"
                        font.pixelSize: ScreenTools.smallFontPointSize
                        elide: Text.ElideMiddle
                        width: parent.width
                    }
                    Row {
                        spacing: 8
                        QGCLabel {
                            text: "  Active:"
                            color: "white"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: _thermalActive ? "✓" : "✗"
                            color: _thermalActive ? "lime" : "red"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: "Decoding:"
                            color: "white"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                        QGCLabel {
                            text: _thermalDecoding ? "✓" : "✗"
                            color: _thermalDecoding ? "lime" : "red"
                            font.pixelSize: ScreenTools.smallFontPointSize
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            Component {
                id: dataCollectionDialogComponent
                DataCollectionDialog {
                    // _customSettings passed via createObject properties
                }
            }
        }
    }

    // Debug label
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10
        width: debugLabel.width + 20
        height: debugLabel.height + 10
        color: "black"
        opacity: 0.8
        z: 100

        Text {
            id: debugLabel
            anchors.centerIn: parent
            text: "GRID MODE\nState: " + _gridState.state
            color: "cyan"
            font.pixelSize: 14
        }
    }
}