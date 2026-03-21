import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

import Custom.Widgets

QGCPopupDialog {
    title: qsTr("Data Collection Settings")
    buttons: Dialog.Close

    property var _customSettings: null

    SettingsGroupLayout {
        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("HTTP URL")
            fact: _customSettings ? _customSettings.httpUrl : null
            visible: fact !== null
        }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("Folder Name")
            fact: _customSettings ? _customSettings.folderName : null
            visible: fact !== null
        }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("Timeout (seconds)")
            fact: _customSettings ? _customSettings.timeout : null
            visible: fact !== null
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth * 2
            visible: _customSettings && _customSettings.noTimeout

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("No Timeout (Run Indefinitely)")
            }

            FactCheckBox {
                fact: _customSettings ? _customSettings.noTimeout : null
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth * 2
            visible: _customSettings && _customSettings.enableVoxlLogging

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Enable VOXL Logging")
            }

            FactCheckBox {
                fact: _customSettings ? _customSettings.enableVoxlLogging : null
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth * 2
            visible: _customSettings && _customSettings.enableRtkLogging

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Enable RTK Logging")
            }

            FactCheckBox {
                fact: _customSettings ? _customSettings.enableRtkLogging : null
            }
        }

        // RowLayout {
        //     Layout.fillWidth: true
        //     spacing: ScreenTools.defaultFontPixelWidth * 2
        //     visible: _customSettings && _customSettings.enableQgcStreaming

        //     QGCLabel {
        //         Layout.fillWidth: true
        //         text: qsTr("Enable QGC Streaming")
        //     }

        //     FactCheckBox {
        //         fact: _customSettings ? _customSettings.enableQgcStreaming : null
        //     }
        // }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("QGC IP Address")
            fact: _customSettings ? _customSettings.qgcIp : null
            visible: fact !== null
        }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("QGC Port")
            fact: _customSettings ? _customSettings.qgcPort : null
            visible: fact !== null
        }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("Remote User Name")
            fact: _customSettings ? _customSettings.remoteUserName : null
            visible: fact !== null  
        }

        // RowLayout {
        //     Layout.fillWidth: true
        //     spacing: ScreenTools.defaultFontPixelWidth * 2
        //     visible: _customSettings && _customSettings.useHardwareEncoding

        //     QGCLabel {
        //         Layout.fillWidth: true
        //         text: qsTr("Use Hardware Encoding")
        //     }

        //     FactCheckBox {
        //         fact: _customSettings ? _customSettings.useHardwareEncoding : null
        //     }
        // }
    }
}