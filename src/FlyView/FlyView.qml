/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap
import QGroundControl.UTMSP
import QGroundControl.Viewer3D

import org.freedesktop.gstreamer.Qt6GLVideoItem

Item {
    id: _root

    // These should only be used by MainRootWindow
    property var planController:    _planController
    property var guidedController:  _guidedController

    // Properties of UTM adapter
    property bool utmspSendActTrigger: false

    PlanMasterController {
        id:                     _planController
        flyView:                true
        Component.onCompleted:  start()
    }

    property bool   _mainWindowIsMap:       mapControl.pipState.state === mapControl.pipState.fullState
    property bool   _isFullWindowItemDark:  _mainWindowIsMap ? mapControl.isSatelliteMap : true
    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _missionController:     _planController.missionController
    property var    _geoFenceController:    _planController.geoFenceController
    property var    _rallyPointController:  _planController.rallyPointController
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property var    _guidedController:      guidedActionsController
    property var    _guidedValueSlider:     guidedValueSlider
    property var    _widgetLayer:           widgetLayer
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property rect   _centerViewport:        Qt.rect(0, 0, width, height)
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30
    property var    _mapControl:            mapControl
    property real   _widgetMargin:          ScreenTools.defaultFontPixelWidth * 0.75

    property bool   _rgbActive:            false
    property bool   _rgbDecoding:           false
    property var    _rgbUri:                ""
    property real   _fullItemZorder:    0
    property real   _pipItemZorder:     QGroundControl.zOrderWidgets
    property bool   _gridModeActive:    false  // Toggle between classic and grid mode
    property var    _gridView:          null   // Reference to grid view component

    function _calcCenterViewPort() {
        var newToolInset = Qt.rect(0, 0, width, height)
        toolstrip.adjustToolInset(newToolInset)
    }

    function dropMainStatusIndicatorTool() {
        toolbar.dropMainStatusIndicatorTool();
    }

    QGCToolInsets {
        id:                     _toolInsets
        topEdgeLeftInset:       toolbar.height
        topEdgeCenterInset:     topEdgeLeftInset
        topEdgeRightInset:      topEdgeLeftInset
        leftEdgeBottomInset:    _pipView.leftEdgeBottomInset
        bottomEdgeLeftInset:    _pipView.bottomEdgeLeftInset
    }

    Connections {
        target: QGroundControl.corePlugin.customVideoManager

        function onReplayModeChanged(active) {
            videoControl.inReplayMode = active
            droneReplayControl.visible = active
        }

        function onStreamStateChanged(streamIndex, active) {
            if (streamIndex === 0) {
                _rgbActive = active
            }
        }

        function onStreamDecodingChanged(streamIndex, decoding) {
            if (streamIndex === 0) {
                _rgbDecoding = decoding
            }
        }

        function onStreamUriChanged(streamIndex, uri) {
            if (streamIndex === 0) {
                _rgbUri = uri
            }
        }
    
    }

    Item {
        id:                 mapHolder
        anchors.fill:       parent

        FlyViewMap {
            id:                     mapControl
            planMasterController:   _planController
            rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
            pipView:                _pipView
            pipMode:                !_mainWindowIsMap
            toolInsets:             customOverlay.totalToolInsets
            toolbarHeight:          toolbar.height
            mapName:                "FlightDisplayView"
            enabled:                !viewer3DWindow.isOpen && !_gridModeActive
            visible:                !_gridModeActive  // Hide main map in grid mode
        }

        FlyViewVideo {
            id:             videoControl
            pipView:        _pipView
            inReplayMode:   videoControl.inReplayMode
        }

        Item {
            id: droneReplayControl
            anchors.fill: parent

            property Item pipView: _pipView3
            property Item pipState: droneReplayPipState

            PipState {
                id:         droneReplayPipState
                pipView:    droneReplayControl.pipView
                isDark:     true
            }

            Loader {
                id: droneReplayLoader
                anchors.fill: parent
                sourceComponent: droneReplayComponent
            }

            Component {
                id: droneReplayComponent
                GstGLQt6VideoItem {
                    id: droneReplayVideoItem
                    anchors.fill: parent
                    objectName: "customDroneReplayVideo"
                }
            }
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
                id: videoLoader
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

        PipView {
            id:                     _pipView
            anchors.left:           parent.left
            anchors.bottom:         parent.bottom
            anchors.margins:        _toolsMargin
            item1IsFullSettingsKey: "MainFlyWindowIsMap"
            item1:                  mapControl
            item2:                  droneReplayControl.visible ? droneReplayControl : (QGroundControl.videoManager.hasVideo ? videoControl : null)
            show:                   (droneReplayControl.visible || QGroundControl.videoManager.hasVideo) && !QGroundControl.videoManager.fullScreen &&
                                        ((droneReplayControl.visible && droneReplayControl.pipState.state === droneReplayControl.pipState.pipState) || (!droneReplayControl.visible && videoControl.pipState.state === videoControl.pipState.pipState) || mapControl.pipState.state === mapControl.pipState.pipState)
            z:                      QGroundControl.zOrderWidgets
            visible:                !_gridModeActive  // Hide when grid mode active

            property real leftEdgeBottomInset: visible ? width + anchors.margins : 0
            property real bottomEdgeLeftInset: visible ? height + anchors.margins : 0
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
        }

        Item {
            id: customStreamDummy2
            
            property Item pipState: customStreamDummyPipState2
            
            PipState {
                id:         customStreamDummyPipState2
                pipView:    _pipView3
                isDark:     false
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

        PipView {
            id:                     _pipView3
            anchors.left:           parent.left
            anchors.bottom:         parent.bottom
            item1IsFullSettingsKey: "DroneReplayIsFullscreen"
            item1IsFullDefault:     true
            item1:                  droneReplayControl
            item2:                  customStreamDummy
            show:                   true
            z:                      QGroundControl.zOrderWidgets
            visible:                !_gridModeActive &&  videoControl.inReplayMode // Hide when grid mode active

        }

        // New Grid View
        Loader {
            id:             gridView
            objectName:     "gridView"
            anchors.fill:   parent
            anchors.top: parent.top
            anchors.topMargin: toolbar.height
            sourceComponent: FlyViewGrid {
                z:              QGroundControl.zOrderWidgets
                visible:        _gridModeActive
                planMasterController:  _planController
            }

        }
        

        FlyViewWidgetLayer {
            id:                     widgetLayer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            anchors.margins:        _widgetMargin
            anchors.topMargin:      toolbar.height + _widgetMargin
            z:                      _fullItemZorder + 2 // we need to add one extra layer for map 3d viewer (normally was 1)
            parentToolInsets:       _toolInsets
            mapControl:             _mapControl
            visible:                !QGroundControl.videoManager.fullScreen && !_gridModeActive  // Hide in grid mode
            isViewer3DOpen:         viewer3DWindow.isOpen
        }

        FlyViewCustomLayer {
            id:                 customOverlay
            anchors.fill:       widgetLayer
            z:                  _fullItemZorder + 2
            parentToolInsets:   widgetLayer.totalToolInsets
            mapControl:         _mapControl
            gridModeActive:     _gridModeActive  // Pass grid mode state
            visible:            !QGroundControl.videoManager.fullScreen && !_gridModeActive  // Hide in grid mode
        }
        

        // Development tool for visualizing the insets for a paticular layer, show if needed
        FlyViewInsetViewer {
            id:                     widgetLayerInsetViewer
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            z:                      widgetLayer.z + 1
            insetsToView:           widgetLayer.totalToolInsets
            visible:                false
        }

        GuidedActionsController {
            id:                 guidedActionsController
            missionController:  _missionController
            guidedValueSlider:     _guidedValueSlider
        }

        //-- Guided value slider (e.g. altitude)
        GuidedValueSlider {
            id:                 guidedValueSlider
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            anchors.topMargin:  toolbar.height
            z:                  QGroundControl.zOrderTopMost
            visible:            false
        }

        Viewer3D {
            id: viewer3DWindow
            anchors.fill: parent
        }
    }

    UTMSPActivationStatusBar {
        activationStartTimestamp:   UTMSPStateStorage.startTimeStamp
        activationApproval:         UTMSPStateStorage.showActivationTab && QGroundControl.utmspManager.utmspVehicle.vehicleActivation
        flightID:                   UTMSPStateStorage.flightID
        anchors.fill:               parent

        function onActivationTriggered(value) {
            _root.utmspSendActTrigger = value
        }
    }

    FlyViewToolBar {
        id:                 toolbar
        guidedValueSlider:  _guidedValueSlider
        utmspSliderTrigger: utmspSendActTrigger
        visible:            !QGroundControl.videoManager.fullScreen
    }

    // Simple toggle button (temporary - will move to toolbar later)
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
}
