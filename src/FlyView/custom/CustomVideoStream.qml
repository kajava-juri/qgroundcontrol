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
import org.freedesktop.gstreamer.Qt6GLVideoItem

import QGroundControl
import QGroundControl.Controls

// Reusable custom video stream component
// Works with CustomVideoManager which finds streams by objectName
Rectangle {
    id: root
    color: "black"

    // Required properties
    required property string streamObjectName
    required property string streamLabel
    required property color borderColor

    border.color: borderColor
    border.width: 1

    // Video item - CustomVideoManager finds this by objectName and manages it
    GstGLQt6VideoItem {
        id: videoItem
        objectName: root.streamObjectName
        anchors.fill: parent
    }

    // Stream label overlay (always visible for identification)
    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        text: streamLabel
        color: "white"
        font.pixelSize: ScreenTools.defaultFontPixelHeight
        horizontalAlignment: Text.AlignHCenter
        z: 1
    }
}