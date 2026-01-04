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

// Simple grid container - shows a basic 2x2 grid of colored rectangles for testing
Item {
    id: root
    visible: false  // Start hidden

    // Expose the grid state to external access
    property alias gridState: _gridState

    // Grid state manager
    GridState {
        id: _gridState
        gridView: root  // Set reference to this GridView
    }

    // Simple test grid - 4 cells
    GridLayout {
        id: gridContainer
        anchors.fill: parent
        anchors.margins: 10
        columns: 2  // 2 columns = 2x2 grid
        columnSpacing: 4
        rowSpacing: 4

        // Cell 0: Red
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "red"
            border.color: "white"
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: "Cell 0\n(Click me)"
                color: "white"
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    console.log("Clicked Cell 0")
                }
            }
        }

        // Cell 1: Green
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "green"
            border.color: "white"
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: "Cell 1\n(Click me)"
                color: "white"
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    console.log("Clicked Cell 1")
                }
            }
        }

        // Cell 2: Blue
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "blue"
            border.color: "white"
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: "Cell 2\n(Click me)"
                color: "white"
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    console.log("Clicked Cell 2")
                }
            }
        }

        // Cell 3: Yellow
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "yellow"
            border.color: "black"
            border.width: 2

            Text {
                anchors.centerIn: parent
                text: "Cell 3\n(Click me)"
                color: "black"
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    console.log("Clicked Cell 3")
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