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
        folder: QGroundControl.corePlugin.customSettings.dataCollectionSaveDirectory
        onAcceptedForLoad: (folderPath) => {
            controller.loadFromMetadataFolder(folderPath)
        }
    }

    Component {
        id: sessionPickerComponent

        QGCPopupDialog {
            title: qsTr("Select Session to Replay")
            buttons: Dialog.Cancel

            ColumnLayout {
                width: ScreenTools.defaultFontPixelWidth * 80
                spacing: ScreenTools.defaultFontPixelHeight / 2

                QGCLabel {
                    text: qsTr("Available Sessions")
                    font.pointSize: ScreenTools.mediumFontPointSize
                    Layout.fillWidth: true
                }
                
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 20
                    
                    ListView {
                        id: sessionListView
                        anchors.fill: parent
                        clip: true
                        spacing: ScreenTools.defaultFontPixelHeight / 4
                        
                        model: controller.sessionMetadata
                        
                        delegate: QGCDelayButton {
                            required property var modelData

                            width: sessionListView.width
                            height: ScreenTools.defaultFontPixelHeight * 2.5
                            delay: 1000  // Hold for 1 second to confirm
                            text: {
                                var streamCount = modelData.videoStreams ? modelData.videoStreams.length : 0
                                var datetimeStr = modelData.timestamp ? Qt.formatDateTime(new Date(modelData.timestamp / 1000), "yyyy-MM-dd HH:mm:ss") : qsTr("Unknown time")
                                return qsTr("Flight ID: %1 | Duration: %2s | Streams: %3 | Tlog: %4\n%5")
                                    .arg(modelData.flightId)
                                    .arg(modelData.dcDurationSecs)
                                    .arg(streamCount > 0 ? streamCount + " video(s)" : qsTr("No video"))
                                    .arg(modelData.hasTlog ? qsTr("Yes") : qsTr("No"))
                                    .arg(datetimeStr)
                            }
                            enabled: modelData.hasTlog
                            
                            onActivated: {
                                if (controller.loadSessionByFlightId(modelData.flightId)) {
                                    close()
                                }
                            }
                        }
                    }
                    
                    QGCLabel {
                        anchors.centerIn: parent
                        text: qsTr("No sessions found")
                        visible: sessionListView.count === 0
                    }
                }
            }
        }
    }

    LogReplayLinkController {
        id: controller

        onPercentCompleteChanged: (percentComplete) => slider.updatePercentComplete(percentComplete)

        onSessionsLoaded: (count) => {
            if (count > 0) {
                sessionPickerComponent.createObject(mainWindow).open()
            } else {
                mainWindow.showMessageDialog(
                    qsTr("No Sessions"), 
                    qsTr("No valid sessions found in the selected folder")
                )
            }
        }
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
            property real totalMs: controller.totalDurationSecs * 1000
            
            background: Item {
                clip: true
                
                Repeater {
                    model: controller.videoReplaySegments
                    
                    Rectangle {
                        // Negative offset means video starts at log time = -start
                        property real segmentStartMs: -modelData.start
                        property real segmentEndMs: segmentStartMs + modelData.duration
                        property real visibleStartMs: Math.max(0, segmentStartMs)
                        property real visibleEndMs: Math.min(segmentEndMs, slider.totalMs)
                        
                        x: slider.leftPadding + (slider.totalMs > 0 ? (visibleStartMs / slider.totalMs) * slider.availableWidth : 0)
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: slider.totalMs > 0 ? ((visibleEndMs - visibleStartMs) / slider.totalMs) * slider.availableWidth : 0
                        height: 4
                        color: modelData.color
                        visible: slider.totalMs > 0 && visibleStartMs < slider.totalMs && visibleEndMs > 0
                    }
                }
            }
            

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
