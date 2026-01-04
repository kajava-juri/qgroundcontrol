import QtQuick

// State manager for GridView - controls visibility and layout mode
// Similar to PipState.qml but for grid-based layout
Item {
    id: control
    state: "hidden"  // Start hidden (classic mode active)

    // State names (as properties for external access)
    readonly property string hiddenState:   "hidden"    // Classic mode active, grid not visible
    readonly property string gridState:     "grid"      // Grid visible with multiple cells
    readonly property string focusedState:  "focused"   // Single cell fullscreen

    // Reference to parent GridView (will be set by GridView)
    property var gridView: null

    states: [
        State {
            name: "hidden"
            PropertyChanges {
                target: gridView
                visible: false
            }
        },
        State {
            name: "grid"
            PropertyChanges {
                target: gridView
                visible: true
            }
        },
        State {
            name: "focused"
            PropertyChanges {
                target: gridView
                visible: true
            }
        }
    ]
}