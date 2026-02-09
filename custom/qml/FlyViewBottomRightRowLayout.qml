/****************************************************************************
 *
 * Custom override: Move instrument panel to bottom center
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView

Item {
    anchors.fill: parent

    property real spacing:              0  // Required by parent but not used in Item layout
    property real bottomEdgeCenterInset:    instrumentPanel.height + instrumentPanel.anchors.bottomMargin
    property real bottomEdgeRightInset:     0
    property real rightEdgeBottomInset:     0

    TelemetryValuesBar {
        id:                     telemetryBar
        anchors.bottom:         parent.bottom
        anchors.bottomMargin:   ScreenTools.defaultFontPixelWidth * 5.25
        anchors.right:          instrumentPanel.left
        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 0.75
        extraWidth:             instrumentPanel.extraValuesWidth
        settingsGroup:          factValueGrid.telemetryBarSettingsGroup
        specificVehicleForCard: null // Tracks active vehicle
    }

    FlyViewInstrumentPanel {
        id:                     instrumentPanel
        anchors.bottom:         parent.bottom
        anchors.bottomMargin:   ScreenTools.defaultFontPixelWidth * 6.25
        anchors.horizontalCenter: parent.horizontalCenter
        visible:                QGroundControl.corePlugin.options.flyView.showInstrumentPanel && _showSingleVehicleUI
    }
}
