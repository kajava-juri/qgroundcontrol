/****************************************************************************
 *
 * Dual Video Widget - Shows RGB and Thermal streams side-by-side
 * Independent of QGC's camera system
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools as ScreenTools

Rectangle {
    id: root
    color: "black"

    property var customVideoManager: null

    // Layout: Side-by-side or stacked
    property bool sideBySide: width > height

    Component.onCompleted: {
        console.log("DualVideoWidget: Initializing...")

        // Get CustomVideoManager from corePlugin
        if (QGroundControl.corePlugin && QGroundControl.corePlugin.customVideoManager) {
            customVideoManager = QGroundControl.corePlugin.customVideoManager

            // Initialize streams with our video items
            customVideoManager.initializeStreams(rgbVideoItem, thermalVideoItem)

            console.log("DualVideoWidget: Initialized with CustomVideoManager")
        } else {
            console.warn("DualVideoWidget: CustomVideoManager not available!")
        }
    }

    // Layout container
    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        // RGB Video
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            border.color: rgbVideoItem.videoRunning ? "green" : "red"
            border.width: 2

            Item {
                id: rgbVideoItem
                objectName: "customRgbVideo"
                anchors.fill: parent
                anchors.margins: 2

                property bool videoRunning: customVideoManager ? customVideoManager.isStreamDecoding(0) : false

                // No video placeholder
                Rectangle {
                    anchors.centerIn: parent
                    width: noVideoLabelRgb.contentWidth + ScreenTools.defaultFontPixelHeight
                    height: noVideoLabelRgb.contentHeight + ScreenTools.defaultFontPixelHeight
                    radius: ScreenTools.defaultFontPixelWidth / 2
                    color: "black"
                    opacity: 0.7
                    visible: !rgbVideoItem.videoRunning

                    QGCLabel {
                        id: noVideoLabelRgb
                        text: "RGB Camera\n(Port 5600)"
                        font.bold: true
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        anchors.centerIn: parent
                    }
                }
            }

            // Stream label
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 4
                width: labelRgb.contentWidth + 8
                height: labelRgb.contentHeight + 4
                color: "black"
                opacity: 0.7
                radius: 2

                QGCLabel {
                    id: labelRgb
                    text: "RGB"
                    color: "white"
                    font.bold: true
                    anchors.centerIn: parent
                    font.pixelSize: ScreenTools.smallFontPointSize
                }
            }

            // Restart button
            QGCButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                text: "↻"
                width: ScreenTools.defaultFontPixelHeight * 2
                height: ScreenTools.defaultFontPixelHeight * 1.5
                onClicked: {
                    if (customVideoManager) {
                        console.log("Restarting RGB stream...")
                        customVideoManager.restartStream(0)
                    }
                }
            }
        }

        // Thermal Video
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            border.color: thermalVideoItem.videoRunning ? "green" : "red"
            border.width: 2

            Item {
                id: thermalVideoItem
                objectName: "customThermalVideo"
                anchors.fill: parent
                anchors.margins: 2

                property bool videoRunning: customVideoManager ? customVideoManager.isStreamDecoding(1) : false

                // No video placeholder
                Rectangle {
                    anchors.centerIn: parent
                    width: noVideoLabelThermal.contentWidth + ScreenTools.defaultFontPixelHeight
                    height: noVideoLabelThermal.contentHeight + ScreenTools.defaultFontPixelHeight
                    radius: ScreenTools.defaultFontPixelWidth / 2
                    color: "black"
                    opacity: 0.7
                    visible: !thermalVideoItem.videoRunning

                    QGCLabel {
                        id: noVideoLabelThermal
                        text: "Thermal Camera\n(Port 5601)"
                        font.bold: true
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        anchors.centerIn: parent
                    }
                }
            }

            // Stream label
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 4
                width: labelThermal.contentWidth + 8
                height: labelThermal.contentHeight + 4
                color: "black"
                opacity: 0.7
                radius: 2

                QGCLabel {
                    id: labelThermal
                    text: "THERMAL"
                    color: "orange"
                    font.bold: true
                    anchors.centerIn: parent
                    font.pixelSize: ScreenTools.smallFontPointSize
                }
            }

            // Restart button
            QGCButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                text: "↻"
                width: ScreenTools.defaultFontPixelHeight * 2
                height: ScreenTools.defaultFontPixelHeight * 1.5
                onClicked: {
                    if (customVideoManager) {
                        console.log("Restarting thermal stream...")
                        customVideoManager.restartStream(1)
                    }
                }
            }
        }
    }

    // Detailed Status Panel (for debugging)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 8
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
                text: "Manager: " + (customVideoManager ? "✓" : "✗")
                color: customVideoManager ? "lime" : "red"
            }

            Rectangle { width: 150; height: 1; color: "gray" }

            QGCLabel {
                text: "RGB (0):"
                color: "white"
                font.bold: true
            }
            QGCLabel {
                text: "  URI: " + (customVideoManager ? customVideoManager.getStreamUri(0) : "N/A")
                color: "white"
                font.pixelSize: ScreenTools.smallFontPointSize
            }
            QGCLabel {
                text: "  Active: " + (customVideoManager && customVideoManager.isStreamActive(0) ? "✓" : "✗")
                color: (customVideoManager && customVideoManager.isStreamActive(0)) ? "lime" : "red"
            }
            QGCLabel {
                text: "  Decoding: " + (customVideoManager && customVideoManager.isStreamDecoding(0) ? "✓" : "✗")
                color: (customVideoManager && customVideoManager.isStreamDecoding(0)) ? "lime" : "red"
            }

            Rectangle { width: 150; height: 1; color: "gray" }

            QGCLabel {
                text: "Thermal (1):"
                color: "white"
                font.bold: true
            }
            QGCLabel {
                text: "  URI: " + (customVideoManager ? customVideoManager.getStreamUri(1) : "N/A")
                color: "white"
                font.pixelSize: ScreenTools.smallFontPointSize
            }
            QGCLabel {
                text: "  Active: " + (customVideoManager && customVideoManager.isStreamActive(1) ? "✓" : "✗")
                color: (customVideoManager && customVideoManager.isStreamActive(1)) ? "lime" : "red"
            }
            QGCLabel {
                text: "  Decoding: " + (customVideoManager && customVideoManager.isStreamDecoding(1) ? "✓" : "✗")
                color: (customVideoManager && customVideoManager.isStreamDecoding(1)) ? "lime" : "red"
            }
        }

        // Click to hide (optional)
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: parent.visible = false
        }
    }

    // Status indicator
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: 4
        width: statusLabel.contentWidth + 16
        height: statusLabel.contentHeight + 8
        color: "black"
        opacity: 0.8
        radius: 4

        QGCLabel {
            id: statusLabel
            anchors.centerIn: parent
            text: {
                if (!customVideoManager) return "No Manager"

                var rgb = customVideoManager.isStreamDecoding(0)
                var thermal = customVideoManager.isStreamDecoding(1)

                if (rgb && thermal) return "Both Streams Active"
                if (rgb) return "RGB Only"
                if (thermal) return "Thermal Only"
                return "No Streams"
            }
            color: {
                if (!customVideoManager) return "red"
                var rgb = customVideoManager.isStreamDecoding(0)
                var thermal = customVideoManager.isStreamDecoding(1)
                return (rgb && thermal) ? "lime" : (rgb || thermal) ? "yellow" : "red"
            }
            font.bold: true
            font.pixelSize: ScreenTools.smallFontPointSize
        }
    }
}
