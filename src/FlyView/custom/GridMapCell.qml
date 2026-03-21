/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap
import QGroundControl.UTMSP
import QGroundControl.Viewer3D

// Grid cell containing an interactive map (no overlays/widgets)
Rectangle {
    id: _root
    color: "black"
    border.color: "#404040"
    border.width: 2
    clip: true  // Clip map to cell boundaries

    // Property passed from parent (FlyViewGrid)
    property var planController: null

    // Wrapper item to isolate map positioning
    Item {
        id: mapContainer
        anchors.fill: parent
        anchors.margins: 2
        clip: true  // Double-clip for safety

        FlyViewMap {
            id: mapControl
            width: mapContainer.width
            height: mapContainer.height
            x: 0
            y: 0

            planMasterController: _root.planController
            pipMode: false
            toolInsets: Qt.rect(0, 0, 0, 0)
            mapName: "GridMapCellView"
            enabled: true
        }
    }

    // // Zoom controls (+/- buttons and scale)
    // MapScale {
    //     id: mapScale
    //     anchors.margins: 4
    //     anchors.left: parent.left
    //     anchors.top: parent.top
    //     mapControl: mapControl
    //     buttonsOnLeft: true
    //     zoomButtonsVisible: true
    //     z: 100
    // }

    // Label overlay
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        width: label.width + 8
        height: label.height + 4
        color: "#80000000"
        radius: 2
        z: 100

        Text {
            id: label
            anchors.centerIn: parent
            text: "MAP"
            color: "white"
            font.pixelSize: 10
            font.bold: true
        }
    }
}
