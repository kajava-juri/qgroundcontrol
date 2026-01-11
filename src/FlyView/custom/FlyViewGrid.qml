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

    // Grid state manager
    GridState {
        id: _gridState
        gridView: root  // Set reference to this GridView
    }

    // Simple test grid - 4 cells
    GridLayout {
        id: gridContainer
        anchors.fill: parent
        anchors.top: parent.top
        columns: 2  // 2 columns = 2x2 grid
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

        // Cell 1: RGB Video Stream
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

        // Cell 2: Thermal Video Stream
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

        // Cell 3: Data Collection Controls
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2b2d30"  // Dark background instead of yellow
            border.color: "black"
            border.width: 2

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.9, ScreenTools.defaultFontPixelWidth * 32)
                spacing: ScreenTools.defaultFontPixelHeight * 0.75

                QGCButton {
                    width: parent.width
                    text: qsTr("Data Collection Settings")
                    onClicked: {
                        var dialog = dataCollectionDialogComponent.createObject(mainWindow, {
                            "_customSettings": QGroundControl.corePlugin.customSettings
                        })
                        dialog.open()
                    }
                }

                Rectangle {
                    width: parent.width
                    height: ScreenTools.defaultFontPixelHeight * 3
                    color: dataController.isCollecting ? "#e03131" : "#12b886"
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: dataController.isCollecting ? "Stop Recording" : "Start Recording"
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

                Text {
                    width: parent.width
                    text: "Test Value: " + dataController.testValue
                    color: "white"
                    font.pixelSize: ScreenTools.defaultFontPixelHeight * 1.2
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
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