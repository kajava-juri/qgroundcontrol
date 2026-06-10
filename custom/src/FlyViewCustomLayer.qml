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
import QtQuick.Layouts
import QtCharts
import org.freedesktop.gstreamer.Qt6GLVideoItem

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.FlightMap

import Custom.Widgets

Item {
    property var parentToolInsets                       // These insets tell you what screen real estate is available for positioning the controls in your overlay
    property var totalToolInsets:   _totalToolInsets    // The insets updated for the custom overlay additions
    property var mapControl:           _mapControl
    property bool gridModeActive:   false               // Whether grid mode is active

    property var dataController: QGroundControl.corePlugin.dataCollectionController
    property bool replayDialogVisible: false 

    readonly property string noGPS:         qsTr("NO GPS")
    readonly property real   indicatorValueWidth:   ScreenTools.defaultFontPixelWidth * 7

    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property real   _indicatorDiameter:     ScreenTools.defaultFontPixelWidth * 18
    property real   _indicatorsHeight:      ScreenTools.defaultFontPixelHeight
    property var    _sepColor:              qgcPal.globalTheme === QGCPalette.Light ? Qt.rgba(0,0,0,0.5) : Qt.rgba(1,1,1,0.5)
    property color  _indicatorsColor:       qgcPal.text
    property bool   _isVehicleGps:          _activeVehicle ? _activeVehicle.gps.count.rawValue > 1 && _activeVehicle.gps.hdop.rawValue < 1.4 : false
    property string _altitude:              _activeVehicle ? (isNaN(_activeVehicle.altitudeRelative.value) ? "0.0" : _activeVehicle.altitudeRelative.value.toFixed(1)) + ' ' + _activeVehicle.altitudeRelative.units : "0.0"
    property string _distanceStr:           isNaN(_distance) ? "0" : _distance.toFixed(0) + ' ' + QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString
    property real   _heading:               _activeVehicle   ? _activeVehicle.heading.rawValue : 0
    property real   _distance:              _activeVehicle ? _activeVehicle.distanceToHome.rawValue : 0
    property string _messageTitle:          ""
    property string _messageText:           ""
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property var    _customVideoManager: QGroundControl.corePlugin.customVideoManager
    property bool   _rgbActive: false
    property bool   _rgbDecoding: false
    property bool   _thermalActive: false
    property bool   _thermalDecoding: false
    property string _rgbUri: ""
    property string _thermalUri: ""

    // Listen to CustomVideoManager signals
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

    // Initialize URIs on startup
    Component.onCompleted: {
        if (_customVideoManager) {
            _rgbUri = _customVideoManager.getStreamUri(0)
            _thermalUri = _customVideoManager.getStreamUri(1)
        }
    }

    function secondsToHHMMSS(timeS) {
        var sec_num = parseInt(timeS, 10);
        var hours   = Math.floor(sec_num / 3600);
        var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
        var seconds = sec_num - (hours * 3600) - (minutes * 60);
        if (hours   < 10) {hours   = "0"+hours;}
        if (minutes < 10) {minutes = "0"+minutes;}
        if (seconds < 10) {seconds = "0"+seconds;}
        return hours+':'+minutes+':'+seconds;
    }

    Item {
        id: flirStreamControl
        
        property Item pipView: _flirPipView
        property Item pipState: flirStreamPipState
        property bool streamActive: false  // Track if stream is active

        PipState {
            id:         flirStreamPipState
            pipView:    flirStreamControl.pipView
            isDark:     true
        }
        
        Loader {
            id: videoLoader1
            anchors.fill: parent
            sourceComponent: GstGLQt6VideoItem {
                id: thermalVideoItem
                objectName: "customThermalVideo"
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: ScreenTools.defaultFontPixelWidth * 0.5
            width: ScreenTools.defaultFontPixelHeight * 0.75
            height: width
            radius: width * 0.5
            color: _rgbDecoding ? "lime" : (_rgbActive ? "gold" : "red")
            border.width: 1
            border.color: "black"
            z: 2
        }

        // Black box overlay when stream is not active
        Rectangle {
            anchors.fill: parent
            color: "black"
            visible: !flirStreamControl.streamActive && !videoControl.inReplayMode 
            z: 1  // Above video but below other UI elements
        }

        // Listen to stream state changes from CustomVideoManager
        Connections {
            target: QGroundControl.corePlugin.customVideoManager

            function onStreamStateChanged(streamIndex, active) {
                if (streamIndex === 0) {  // RGB stream
                    flirStreamControl.streamActive = active
                }
            }
        }

        Component.onCompleted: {
            // Initialize stream state
            if (QGroundControl.corePlugin.customVideoManager) {
                flirStreamControl.streamActive = QGroundControl.corePlugin.customVideoManager.isStreamActive(0)
            }
        }
    }

    // Dummy item for custom stream PipView item2
    Item {
        id: flirStreamDummy
        
        property Item pipState: flirStreamDummyPipState
        
        PipState {
            id:         flirStreamDummyPipState
            pipView:    _flirPipView
            isDark:     false
        }

        Rectangle {
            anchors.fill: parent
            visible: flirStreamDummyPipState.state !== flirStreamDummyPipState.fullState
            color: "black"
            opacity: 0.8
            z: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("click to toggle fullscreen")
                color: "white"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: flirStreamDummyPipState.state = flirStreamDummyPipState.fullState
            }
        }
    }


    PipView {
        id:                     _flirPipView
        anchors.right:          parent.right
        anchors.top:         parent.top
        anchors.margins:        _toolsMargin
        item1IsFullSettingsKey: "FlirStreamIsFullscreen"
        item1IsFullDefault:     false   // Start in fullscreen mode (grid needs easy access)
        resizeCorner:           "bottomLeft"  // Resize from top-left since on right side
        item1:                  flirStreamControl
        item2:                  flirStreamDummy
        show:                   true
        z:                      QGroundControl.zOrderWidgets
        visible:                !_gridModeActive  // Hide when grid mode active
    }


    Item {
        id: customStreamControl
        
        property Item pipView: _pipView2
        property Item pipState: customStreamPipState
        property bool streamActive: false  // Track if stream is active

        PipState {
            id:         customStreamPipState
            pipView:    customStreamControl.pipView
            isDark:     true
        }
        
        Loader {
            id: videoLoader2
            anchors.fill: parent
            sourceComponent: rgbComponent  // Default
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: ScreenTools.defaultFontPixelWidth * 0.5
            width: ScreenTools.defaultFontPixelHeight * 0.75
            height: width
            radius: width * 0.5
            color: _rgbDecoding ? "lime" : (_rgbActive ? "gold" : "red")
            border.width: 1
            border.color: "black"
            z: 2
        }
        
        Component {
            id: rgbComponent
            GstGLQt6VideoItem {
                id: rgbVideoItem
                objectName: "customRgbVideo"
            }
        }
        
        // Component {
        //     id: thermalComponent
        //     GstGLQt6VideoItem {
        //         objectName: "customThermalVideo"
        //     }
        // }

        // Black box overlay when stream is not active
        Rectangle {
            anchors.fill: parent
            color: "black"
            visible: !customStreamControl.streamActive && !videoControl.inReplayMode 
            z: 1  // Above video but below other UI elements
        }

        // Listen to stream state changes from CustomVideoManager
        Connections {
            target: QGroundControl.corePlugin.customVideoManager

            function onStreamStateChanged(streamIndex, active) {
                if (streamIndex === 0) {  // RGB stream
                    customStreamControl.streamActive = active
                }
            }
        }

        Component.onCompleted: {
            // Initialize stream state
            if (QGroundControl.corePlugin.customVideoManager) {
                customStreamControl.streamActive = QGroundControl.corePlugin.customVideoManager.isStreamActive(0)
            }
        }
    }

    // Dummy item for custom stream PipView item2
    Item {
        id: customStreamDummy
        
        property Item pipState: customStreamDummyPipState
        
        PipState {
            id:         customStreamDummyPipState
            pipView:    _pipView2
            isDark:     false
        }

        Rectangle {
            anchors.fill: parent
            visible: customStreamDummyPipState.state !== customStreamDummyPipState.fullState
            color: "black"
            opacity: 0.8
            z: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("click to toggle fullscreen")
                color: "white"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: customStreamDummyPipState.state = customStreamDummyPipState.fullState
            }
        }
    }

    PipView {
        id:                     _pipView2
        anchors.right:          parent.right
        anchors.bottom:         parent.bottom
        anchors.margins:        _toolsMargin
        item1IsFullSettingsKey: "CustomStreamIsFullscreen"
        item1IsFullDefault:     true   // Start in fullscreen mode (grid needs easy access)
        resizeCorner:           "topLeft"  // Resize from top-left since on right side
        item1:                  customStreamControl
        item2:                  customStreamDummy
        show:                   true
        z:                      QGroundControl.zOrderWidgets
        visible:                !_gridModeActive  // Hide when grid mode active
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top        
        z: QGroundControl.zOrderWidgets
        visible: dataController ? dataController.syncInProgress : false

        QGCLabel {
            text: dataController ? dataController.syncStatusText : ""
            Layout.alignment: Qt.AlignHCenter
        }

        ProgressBar {
            id: dcSyncProgressBar
            Layout.fillWidth: true
            visible: dataController ? dataController.syncInProgress : false
            width: Math.min(parent.width * 0.8, ScreenTools.defaultFontPixelWidth * 70)
            height: 64
            from: 0
            to: 100
            value: dataController ? dataController.syncProgressPct : 0

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
            }

            Item {
                width: 16
                height: 16
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: mouseArea.containsMouse || cancelMouseArea.containsMouse

                MouseArea {
                    id: cancelMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: {
                        if (dataController) {
                            dataController.cancelSync()
                        }
                    }
                }

                Rectangle {
                    id: badge
                    anchors.fill: parent
                    radius: width / 2
                    color: "red"
                    antialiasing: true

                    // White X
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.58
                        height: Math.max(3, parent.width * 0.09)
                        radius: height / 2
                        color: "white"
                        rotation: 45
                        antialiasing: true
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.58
                        height: Math.max(3, parent.width * 0.09)
                        radius: height / 2
                        color: "white"
                        rotation: -45
                        antialiasing: true
                    }
                }
            }
        }
    }

    // Replay Mode Dialog
    Rectangle {
        id: replayDialog
        visible: replayDialogVisible
        anchors.centerIn: parent
        width: 400
        height: 250
        color: qgcPal.window
        border.color: qgcPal.text
        border.width: 2
        radius: 4
        z: 1000

        Column {
            anchors.fill: parent
            anchors.margins: ScreenTools.defaultFontPixelWidth
            spacing: ScreenTools.defaultFontPixelHeight

            QGCLabel {
                text: "Enter Replay Mode"
                font.pointSize: ScreenTools.mediumFontPointSize
                font.bold: true
            }

            // RGB Video File
            Row {
                spacing: ScreenTools.defaultFontPixelWidth
                width: parent.width

                QGCLabel {
                    text: "RGB Video:"
                    width: 100
                    anchors.verticalCenter: parent.verticalCenter
                }

                QGCTextField {
                    id: rgbFileInput
                    width: parent.width - 100 - ScreenTools.defaultFontPixelWidth
                    placeholderText: "/path/to/rgb_video.mp4"
                }
            }

            // Thermal Video File
            Row {
                spacing: ScreenTools.defaultFontPixelWidth
                width: parent.width

                QGCLabel {
                    text: "Thermal Video:"
                    width: 100
                    anchors.verticalCenter: parent.verticalCenter
                }

                QGCTextField {
                    id: thermalFileInput
                    width: parent.width - 100 - ScreenTools.defaultFontPixelWidth
                    placeholderText: "/path/to/thermal_video.mp4"
                }
            }

            // Buttons
            Row {
                spacing: ScreenTools.defaultFontPixelWidth
                anchors.horizontalCenter: parent.horizontalCenter

                QGCButton {
                    text: "Start Replay"
                    onClicked: {
                        if (_customVideoManager) {
                            var success = _customVideoManager.enterReplayMode(
                                rgbFileInput.text,
                                thermalFileInput.text
                            )
                            if (success) {
                                replayDialogVisible = false
                            }
                        }
                    }
                }

                QGCButton {
                    text: "Cancel"
                    onClicked: replayDialogVisible = false
                }
            }
        }

        // Click outside to close
        MouseArea {
            anchors.fill: parent
            onClicked: mouse.accepted = true
            z: -1
        }
    }

    // Dark overlay when dialog is open
    Rectangle {
        visible: replayDialogVisible
        anchors.fill: parent
        color: "black"
        opacity: 0.5
        z: 999

        MouseArea {
            anchors.fill: parent
            onClicked: replayDialogVisible = false
        }
    }

    QGCToolInsets {
        id:                     _totalToolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    exampleRectangle.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    Rectangle {
        id: gridToggleButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: toolbar.height + 10
        anchors.rightMargin: 10
        width: 120
        height: 40
        color: _gridModeActive ? "cyan" : "gray"
        border.color: "white"
        border.width: 2
        radius: 4
        z: QGroundControl.zOrderTopMost
        visible: false

        Text {
            anchors.centerIn: parent
            text: _gridModeActive ? "Overlay Mode" : "Grid Mode"
            color: "black"
            font.pixelSize: 14
            font.bold: true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                _gridModeActive = !_gridModeActive
                // debug print
                console.log("Grid Mode Active:", _gridModeActive)
                if (gridView.item) {
                    gridView.item.gridState.state = _gridModeActive ? "grid" : "hidden"
                }

                // Give Loaders time to create/destroy widgets, then reinitialize CustomVideoManager
                Qt.callLater(function() {
                    if (QGroundControl.corePlugin.customVideoManager) {
                        QGroundControl.corePlugin.customVideoManager.reinitializeWidgets(_gridModeActive)
                    }
                })
            }
        }
    }

    // This is an example of how you can use parent tool insets to position an element on the custom fly view layer
    // - we use parent topEdgeLeftInset to position the widget below the toolstrip
    // - we use parent bottomEdgeLeftInset to dodge the virtual joystick if enabled
    // - we use the parent leftEdgeTopInset to size our element to the same width as the ToolStripAction
    // - we export the width of this element as the leftEdgeCenterInset so that the map will recenter if the vehicle flys behind this element
    Rectangle {
        id: exampleRectangle
        visible: false // to see this example, set this to true. To view insets, enable the insets viewer FlyView.qml
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: parentToolInsets.topEdgeLeftInset + _toolsMargin
        anchors.bottomMargin: parentToolInsets.bottomEdgeLeftInset + _toolsMargin
        anchors.leftMargin: _toolsMargin
        width: parentToolInsets.leftEdgeTopInset - _toolsMargin
        color: 'red'

        property real leftEdgeCenterInset: visible ? x + width : 0
    }

    //-------------------------------------------------------------------------
    //-- Heading Indicator
    // Rectangle {
    //     id:                         compassBar
    //     height:                     ScreenTools.defaultFontPixelHeight * 1.5
    //     width:                      ScreenTools.defaultFontPixelWidth  * 50
    //     anchors.bottom:             parent.bottom
    //     anchors.bottomMargin:       _toolsMargin
    //     color:                      "#DEDEDE"
    //     radius:                     2
    //     clip:                       true
    //     anchors.horizontalCenter:   parent.horizontalCenter
    //     Repeater {
    //         model: 720
    //         QGCLabel {
    //             function _normalize(degrees) {
    //                 var a = degrees % 360
    //                 if (a < 0) a += 360
    //                 return a
    //             }
    //             property int _startAngle: modelData + 180 + _heading
    //             property int _angle: _normalize(_startAngle)
    //             anchors.verticalCenter: parent.verticalCenter
    //             x:              visible ? ((modelData * (compassBar.width / 360)) - (width * 0.5)) : 0
    //             visible:        _angle % 45 == 0
    //             color:          "#75505565"
    //             font.pointSize: ScreenTools.smallFontPointSize
    //             text: {
    //                 switch(_angle) {
    //                 case 0:     return "N"
    //                 case 45:    return "NE"
    //                 case 90:    return "E"
    //                 case 135:   return "SE"
    //                 case 180:   return "S"
    //                 case 225:   return "SW"
    //                 case 270:   return "W"
    //                 case 315:   return "NW"
    //                 }
    //                 return ""
    //             }
    //         }
    //     }
    // }
    // Rectangle {
    //     id:                         headingIndicator
    //     height:                     ScreenTools.defaultFontPixelHeight
    //     width:                      ScreenTools.defaultFontPixelWidth * 4
    //     color:                      qgcPal.windowShadeDark
    //     anchors.top:                compassBar.top
    //     anchors.topMargin:          -headingIndicator.height / 2
    //     anchors.horizontalCenter:   parent.horizontalCenter
    //     QGCLabel {
    //         text:                   _heading
    //         color:                  qgcPal.text
    //         font.pointSize:         ScreenTools.smallFontPointSize
    //         anchors.centerIn:       parent
    //     }
    // }
    // Image {
    //     id:                         compassArrowIndicator
    //     height:                     _indicatorsHeight
    //     width:                      height
    //     source:                     "/custom/img/compass_pointer.svg"
    //     fillMode:                   Image.PreserveAspectFit
    //     sourceSize.height:          height
    //     anchors.top:                compassBar.bottom
    //     anchors.topMargin:          -height / 2
    //     anchors.horizontalCenter:   parent.horizontalCenter
    // }
}
