import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.AppSettings

SettingsPage {
    property var _customSettings: QGroundControl.corePlugin.customSettings

    QGCLabel {
        text: "Data Collection Settings"
        font.pointSize: ScreenTools.largeFontPointSize
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Server Configuration")
        visible: _customSettings !== null

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("HTTP URL")
            fact: _customSettings ? _customSettings.httpUrl : null
            visible: fact !== null
        }

        LabelledFactTextField {
            Layout.fillWidth: true
            label: qsTr("WebSocket URL")
            fact: _customSettings ? _customSettings.webSocketUrl : null
            visible: fact !== null
        }
    }

    QGCLabel {
        visible: _customSettings === null
        text: "Error: Custom settings not initialized"
        color: "red"
    }
}