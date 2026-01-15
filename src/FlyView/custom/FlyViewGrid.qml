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

    // Access to CustomVideoManager - adjust based on how it's exposed in your CustomPlugin
    property var _customVideoManager: QGroundControl.corePlugin.customVideoManager
    
    // Stream properties - bound to CustomVideoManager
    property string _rgbUri: _customVideoManager ? _customVideoManager.getStreamUri(0) : ""
    property bool _rgbActive: _customVideoManager ? _customVideoManager.isStreamActive(0) : false
    property bool _rgbDecoding: _customVideoManager ? _customVideoManager.isStreamDecoding(0) : false
    
    property string _thermalUri: _customVideoManager ? _customVideoManager.getStreamUri(1) : ""
    property bool _thermalActive: _customVideoManager ? _customVideoManager.isStreamActive(1) : false
    property bool _thermalDecoding: _customVideoManager ? _customVideoManager.isStreamDecoding(1) : false 

    // Grid state manager
    GridState {
        id: _gridState
        gridView: root  // Set reference to this GridView
    }

    // Connect to CustomVideoManager signals for live updates
    Connections {
        target: _customVideoManager
        
        function onStreamUriChanged(streamIndex, uri) {
            if (streamIndex === 0) _rgbUri = uri
            else if (streamIndex === 1) _thermalUri = uri
        }
        
        function onStreamStateChanged(streamIndex, active) {
            if (streamIndex === 0) _rgbActive = active
            else if (streamIndex === 1) _thermalActive = active
        }
        
        function onStreamDecodingChanged(streamIndex, decoding) {
            if (streamIndex === 0) _rgbDecoding = decoding
            else if (streamIndex === 1) _thermalDecoding = decoding
        }
    }

    // Simple test grid - 4 cells
    GridLayout {
        id: gridContainer
        anchors.fill: parent
        anchors.top: parent.top
        columns: 3  // 3 columns = 3x2 grid
        columnSpacing: 4
        rowSpacing: 4

        // Cell 0: Red
        GridMapCell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "red"
            border.color: "white"
            border.width: 2
            planController: root.planMasterController

            // MouseArea {
            //     anchors.fill: parent
            //     onClicked: {
            //         console.log("Clicked Cell 0")
            //     }
            // }
        }

        // Cell 1: Main Drone Camera (spans 2 columns)
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.columnSpan: 2  // Span both columns for prominent display
            active: root.visible
            sourceComponent: CustomVideoStream {
                streamObjectName: "customMainVideo"
                streamLabel: "Main Camera"
                borderColor: "yellow"
            }
        }

        // Cell 2: RGB Video Stream
        // Only create when grid is visible to avoid conflicts with CustomLayer
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

                // Data source status header
                QGCLabel {
                    text: "Stream Status" + (_customVideoManager ? " ✓" : " ✗")
                    color: _customVideoManager ? "cyan" : "red"
                    font.bold: true
                }

                // Scrollable data source status area
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    
                    ColumnLayout {
                        width: parent.width
                        spacing: ScreenTools.defaultFontPixelHeight * 0.5

                        // RGB Stream
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 3
                            color: "#1e1e1e"
                            radius: 4
                            border.color: _rgbDecoding ? "green" : "gray"
                            border.width: 2

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 0

                                QGCLabel {
                                    text: "RGB"
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: ScreenTools.smallFontPointSize
                                }
                                
                                QGCLabel {
                                    Layout.fillWidth: true
                                    text: _rgbUri ? _rgbUri : "(no stream)"
                                    color: "gray"
                                    font.pixelSize: ScreenTools.smallFontPointSize * 0.7
                                    elide: Text.ElideMiddle
                                    wrapMode: Text.NoWrap
                                }

                                Row {
                                    spacing: 4
                                    QGCLabel {
                                        text: "A:" + (_rgbActive ? "✓" : "✗")
                                        color: _rgbActive ? "lime" : "red"
                                        font.pixelSize: ScreenTools.smallFontPointSize * 0.9
                                    }
                                    QGCLabel {
                                        text: "D:" + (_rgbDecoding ? "✓" : "✗")
                                        color: _rgbDecoding ? "lime" : "red"
                                        font.pixelSize: ScreenTools.smallFontPointSize * 0.9
                                    }
                                }
                            }
                        }

                        // Thermal Stream
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 3
                            color: "#1e1e1e"
                            radius: 4
                            border.color: _thermalDecoding ? "#e03131" : "gray"
                            border.width: 2

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 2

                                QGCLabel {
                                    text: "Thermal"
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: ScreenTools.smallFontPointSize
                                }
                                
                                QGCLabel {
                                    Layout.fillWidth: true
                                    text: _thermalUri ? _thermalUri : "(no stream)"
                                    color: "gray"
                                    font.pixelSize: ScreenTools.smallFontPointSize * 0.7
                                    elide: Text.ElideMiddle
                                    wrapMode: Text.NoWrap
                                }

                                Row {
                                    spacing: 4
                                    QGCLabel {
                                        text: "A:" + (_thermalActive ? "✓" : "✗")
                                        color: _thermalActive ? "lime" : "red"
                                        font.pixelSize: ScreenTools.smallFontPointSize * 0.9
                                    }
                                    QGCLabel {
                                        text: "D:" + (_thermalDecoding ? "✓" : "✗")
                                        color: _thermalDecoding ? "lime" : "red"
                                        font.pixelSize: ScreenTools.smallFontPointSize * 0.9
                                    }
                                }
                            }
                        }
                    }
                }
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