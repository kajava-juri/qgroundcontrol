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
    property var    _customVideoManager: QGroundControl.corePlugin.customVideoManager
    property var    dataController: QGroundControl.corePlugin.dataCollectionController

    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    property bool   _rgbActive: false
    property bool   _rgbDecoding: false
    property bool   _thermalActive: false
    property bool   _thermalDecoding: false
    property string _rgbUri: ""
    property string _thermalUri: ""

    function _statusText(active, decoding) {
        if (decoding) {
            return qsTr("Decoding")
        }
        if (active) {
            return qsTr("Connected")
        }
        return qsTr("Offline")
    }

    function _statusColor(active, decoding) {
        if (decoding) {
            return "lime"
        }
        if (active) {
            return "gold"
        }
        return "red"
    }

    QGCPalette { id: qgcPal }

    Connections {
        target: _customVideoManager

        function onStreamStateChanged(streamIndex, active) {
            if (streamIndex === 0) {
                _rgbActive = active
            } else if (streamIndex === 1) {
                _thermalActive = active
            }
        }

        function onStreamDecodingChanged(streamIndex, decoding) {
            if (streamIndex === 0) {
                _rgbDecoding = decoding
            } else if (streamIndex === 1) {
                _thermalDecoding = decoding
            }
        }

        function onStreamUriChanged(streamIndex, uri) {
            if (streamIndex === 0) {
                _rgbUri = uri
            } else if (streamIndex === 1) {
                _thermalUri = uri
            }
        }
    }

    Component.onCompleted: {
        if (_customVideoManager) {
            _rgbActive = _customVideoManager.isStreamActive(0)
            _rgbDecoding = _customVideoManager.isStreamDecoding(0)
            _thermalActive = _customVideoManager.isStreamActive(1)
            _thermalDecoding = _customVideoManager.isStreamDecoding(1)
            _rgbUri = _customVideoManager.getStreamUri(0)
            _thermalUri = _customVideoManager.getStreamUri(1)
        }
    }

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

    Component {
        id: drawerComponent

        ToolIndicatorPage {
            showExpand:         true

            contentComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelHeight

                    QGCLabel {
                        text:               qsTr("Video Stream Status")
                        font.pointSize:     ScreenTools.largeFontPointSize
                        Layout.alignment:   Qt.AlignHCenter
                    }

                    Rectangle {
                        Layout.fillWidth:   true
                        implicitHeight:     streamStatusColumn.implicitHeight + ScreenTools.defaultFontPixelHeight
                        color:              qgcPal.windowShade
                        radius:             ScreenTools.defaultFontPixelWidth * 0.5

                        ColumnLayout {
                            id:                 streamStatusColumn
                            anchors.fill:       parent
                            anchors.margins:    ScreenTools.defaultFontPixelWidth
                            spacing:            ScreenTools.defaultFontPixelWidth * 0.75

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: ScreenTools.defaultFontPixelWidth

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: ScreenTools.defaultFontPixelHeight * 0.65
                                    height: width
                                    radius: width * 0.5
                                    color: _statusColor(_rgbActive, _rgbDecoding)
                                    border.width: 1
                                    border.color: "black"
                                }

                                QGCLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("RGB: ") + _statusText(_rgbActive, _rgbDecoding)
                                }
                            }

                            QGCLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                text: qsTr("RGB URI: ") + (_rgbUri !== "" ? _rgbUri : qsTr("N/A"))
                                font.pointSize: ScreenTools.smallFontPointSize
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: qgcPal.text
                                opacity: 0.2
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: ScreenTools.defaultFontPixelWidth

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: ScreenTools.defaultFontPixelHeight * 0.65
                                    height: width
                                    radius: width * 0.5
                                    color: _statusColor(_thermalActive, _thermalDecoding)
                                    border.width: 1
                                    border.color: "black"
                                }

                                QGCLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("Thermal: ") + _statusText(_thermalActive, _thermalDecoding)
                                }
                            }

                            QGCLabel {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                text: qsTr("Thermal URI: ") + (_thermalUri !== "" ? _thermalUri : qsTr("N/A"))
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 40
                        color: qgcPal.button
                        radius: 4

                        Text {
                            
                            anchors.centerIn: parent
                            text: "Download Replay Data"
                            color: qgcPal.buttonText
                            font.pixelSize: ScreenTools.defaultFontPixelSize
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (dataController) {
                                    dataController.manualDownloadReplayData()
                                }
                            }
                        }
                    }

                    QGCLabel {
                        text: _activeVehicle ? qsTr("Vehicle Connected: ") + _activeVehicle.id : qsTr("No Vehicle")
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
            expandedComponent: expandedPageComponent
        }
    }
}
