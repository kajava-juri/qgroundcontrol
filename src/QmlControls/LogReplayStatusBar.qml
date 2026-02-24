import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QGroundControl
import QGroundControl.Controls

Rectangle {
    height: visible ? (rowLayout.height + (_margins * 2)) : 0
    color: qgcPal.window

    property real _margins: ScreenTools.defaultFontPixelHeight / 4
    property var _logReplayLink: null

    function pickLogFile() {
        // NOTE: Allowing replay with active connections to support external data collectors
        // (e.g., component 25) that need to synchronize with replay for sensor data streaming.
        // The replay link creates a separate vehicle instance for the log data.
        if (globals.activeVehicle) {
            mainWindow.showMessageDialog(qsTr("Log Replay"), qsTr("You must close all connections prior to replaying a log."))
            return
        }

        filePicker.openForLoad()
    }

    QGCPalette { id: qgcPal }

    QGCFileDialog {
        id: filePicker
        title: qsTr("Select Telemetery Log")
        nameFilters: [ qsTr("Telemetry Logs (*.%1)").arg(_logFileExtension), qsTr("All Files (*)") ]
        folder: QGroundControl.settingsManager.appSettings.telemetrySavePath
        onAcceptedForLoad: (file) => {
            controller.link = QGroundControl.linkManager.startLogReplay(file)
            close()
        }

        property string _logFileExtension: QGroundControl.settingsManager.appSettings.telemetryFileExtension
    }

    QGCFileDialog {
        id: folderPicker
        title: qsTr("Select Metadata Folder")
        selectFolder: true
        folder: QGroundControl.settingsManager.appSettings.telemetrySavePath
        onAcceptedForLoad: (folderPath) => {
            controller.loadFromMetadataFolder(folderPath)
        }
    }

    LogReplayLinkController {
        id: controller

        onPercentCompleteChanged: (percentComplete) => slider.updatePercentComplete(percentComplete)
    }

    RowLayout {
        id: rowLayout
        anchors {
            margins: _margins
            top: parent.top
            left: parent.left
            right: parent.right
        }

        // Status indicator for replay data availability
        Rectangle {
            Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 0.8
            Layout.preferredHeight: width
            radius: width / 2
            visible: controller.link && controller.replayDataStatus !== LogReplayLinkController.NotRequired
            color: {
                switch (controller.replayDataStatus) {
                    case LogReplayLinkController.Checking:
                        return qgcPal.colorYellow
                    case LogReplayLinkController.Ready:
                        return qgcPal.colorGreen
                    case LogReplayLinkController.Unavailable:
                        return qgcPal.colorRed
                    default:
                        return qgcPal.text
                }
            }
        }

        QGCLabel {
            text: controller.statusMessage
            visible: controller.link && controller.statusMessage !== ""
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 30
            elide: Text.ElideRight
        }

        QGCButton {
            text: "Load Metadata Folder"
            onClicked: folderPicker.openForLoad()
        }

        QGCButton {
            enabled: controller.link && 
                     (controller.replayDataStatus === LogReplayLinkController.Ready || 
                      controller.replayDataStatus === LogReplayLinkController.NotRequired)
            text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
            onClicked: controller.isPlaying = !controller.isPlaying
        }

        QGCComboBox {
            textRole: "text"
            currentIndex: 3

            model: ListModel {
                ListElement { text: "0.1";  value: 0.1 }
                ListElement { text: "0.25"; value: 0.25 }
                ListElement { text: "0.5";  value: 0.5 }
                ListElement { text: "1x";   value: 1 }
                ListElement { text: "2x";   value: 2 }
                ListElement { text: "5x";   value: 5 }
                ListElement { text: "10x";  value: 10 }
            }

            onActivated: (index) => { controller.playbackSpeed = model.get(currentIndex).value }
        }

        QGCLabel { text: controller.playheadTime }

        Slider {
            id: slider
            Layout.fillWidth: true
            from: 0
            to: 100
            enabled: controller.link

            property bool manualUpdate: false

            function updatePercentComplete(percentComplete) {
                manualUpdate = true
                value = percentComplete
                manualUpdate = false
            }

            onValueChanged: {
                if (!manualUpdate) {
                    controller.percentComplete = value
                }
            }
        }

        QGCLabel { text: controller.totalTime }

        QGCButton {
            text: qsTr("Load Telemetry Log")
            onClicked: pickLogFile()
            visible: !controller.link
        }

        QGCButton {
            text: qsTr("Close")
            onClicked: {
                var activeVehicle = QGroundControl.multiVehicleManager.activeVehicle
                if (activeVehicle) {
                    activeVehicle.closeVehicle()
                }
                QGroundControl.settingsManager.flyViewSettings.showLogReplayStatusBar.rawValue = false
            }
        }
    }
}
