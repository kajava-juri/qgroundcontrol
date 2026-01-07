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
    width:                  mainLayout.width

    property bool showIndicator: true
    property var    _customSettings: QGroundControl.corePlugin.customSettings

    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    QGCPalette { id: qgcPal }

    RowLayout {
        id:                     mainLayout
        anchors.verticalCenter: parent.verticalCenter
        spacing:                ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            Layout.preferredWidth:  ScreenTools.defaultFontPixelHeight
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight
            fillMode:               Image.PreserveAspectFit
            mipmap:                 true
            color:                  qgcPal.windowTransparentText
            source:                 "/qmlimages/DatabaseIcon.svg"
        }

        QGCLabel {
            text:               "Data"
            color:              qgcPal.windowTransparentText
            font.pointSize:     ScreenTools.largeFontPointSize
        }
    }

    MouseArea {
        anchors.fill:   mainLayout
        onClicked:      mainWindow.showIndicatorDrawer(drawerComponent, control)
    }

    Component {
        id: expandedPageComponent

        SettingsGroupLayout {
                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("HTTP URL")
                    fact: _customSettings ? _customSettings.httpUrl : null
                    visible: fact !== null
                }

                // LabelledFactTextField {
                //     Layout.fillWidth: true
                //     label: qsTr("WebSocket URL")
                //     fact: _customSettings ? _customSettings.webSocketUrl : null
                //     visible: fact !== null
                // }

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

    Component {
        id: drawerComponent

        ToolIndicatorPage {
            showExpand:         true

            contentComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight

                    QGCLabel {
                        text:               "Hello World"
                        font.pointSize:     ScreenTools.largeFontPointSize
                        Layout.alignment:   Qt.AlignHCenter
                    }

                    QGCLabel {
                        text:               "Data Collection Indicator"
                        Layout.alignment:   Qt.AlignHCenter
                    }

                    QGCLabel {
                        text:               _activeVehicle ? "Vehicle Connected: " + _activeVehicle.id : "No Vehicle"
                        Layout.alignment:   Qt.AlignHCenter
                    }
                }
            }
            expandedComponent: expandedPageComponent
        }
    }
}
