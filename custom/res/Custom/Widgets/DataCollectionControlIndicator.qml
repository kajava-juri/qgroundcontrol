/*
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
*/

/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Item {
    id:                     control
    anchors.top:            parent.top
    anchors.bottom:         parent.bottom
    width:                  indicatorRect.width

    property var dataController: QGroundControl.corePlugin.dataCollectionController 

    QGCPalette { id: qgcPal }

    Rectangle {
        id:     indicatorRect
        width:  ScreenTools.defaultFontPixelWidth * 18
        height: ScreenTools.defaultFontPixelHeight * 2
        color:  dataController.isCollecting ? "#e03131" : "#12b886"
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

}
