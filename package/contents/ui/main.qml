import QtQuick
import QtQuick.Controls
import org.kde.plasma.plasma5support as P5Support
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import "."

WallpaperItem {
    id: root
    width: 1920
    height: 1080
    focus: true

    // Font and size configuration
    property string fontName: (root.configuration && root.configuration.FontName) ? root.configuration.FontName : "Monospace"
    property int fontSize: (root.configuration && root.configuration.FontSize) ? root.configuration.FontSize : 13
    
    // Dynamic panel padding offsets (automatically updated by Screen geometry, overridden by user config)
    property int leftPadding: (root.configuration && root.configuration.PaddingLeft > 0) ? root.configuration.PaddingLeft : (root.Screen.desktopAvailableLeft !== undefined ? root.Screen.desktopAvailableLeft : 0)
    property int topPadding: (root.configuration && root.configuration.PaddingTop > 0) ? root.configuration.PaddingTop : (root.Screen.desktopAvailableTop !== undefined ? root.Screen.desktopAvailableTop : 0)
    property int bottomPadding: (root.configuration && root.configuration.PaddingBottom > 0) ? root.configuration.PaddingBottom : (root.Screen.desktopAvailableTop !== undefined && root.Screen.desktopAvailableHeight !== undefined ? root.height - (root.Screen.desktopAvailableTop + root.Screen.desktopAvailableHeight) : 0)
    property int rightPadding: (root.configuration && root.configuration.PaddingRight > 0) ? root.configuration.PaddingRight : (root.Screen.desktopAvailableLeft !== undefined && root.Screen.desktopAvailableWidth !== undefined ? root.width - (root.Screen.desktopAvailableLeft + root.Screen.desktopAvailableWidth) : 0)

    // System theme variables to watch for changes
    property color systemBgColor: Kirigami.Theme.backgroundColor
    property color systemFgColor: Kirigami.Theme.textColor

    onSystemBgColorChanged: sendTheme()
    onSystemFgColorChanged: sendTheme()

    // Dynamic system-themed background
    Rectangle {
        id: background
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    // Native C++ Terminal is the visual display element
    Terminal {
        id: terminalBackend
        anchors.fill: parent
        anchors.topMargin: root.topPadding
        anchors.bottomMargin: root.bottomPadding
        anchors.leftMargin: root.leftPadding
        anchors.rightMargin: root.rightPadding
        
        fontName: root.fontName
        fontSize: root.fontSize
        terminalFocused: mainScope.activeFocus
        
        Component.onCompleted: {
            console.log("Terminal Wallpaper Debug Geometry:");
            console.log(" - root size: " + root.width + "x" + root.height);
            console.log(" - Screen desktopAvailable: " + root.Screen.desktopAvailableX + "," + root.Screen.desktopAvailableY + " " + root.Screen.desktopAvailableWidth + "x" + root.Screen.desktopAvailableHeight);
            console.log(" - Paddings -> Left: " + root.leftPadding + ", Top: " + root.topPadding + ", Right: " + root.rightPadding + ", Bottom: " + root.bottomPadding);
            sendTheme();
        }
    }

    // Visual Bell Overlay
    Rectangle {
        id: bellOverlay
        anchors.fill: parent
        color: "red"
        opacity: 0
        z: 999
        visible: opacity > 0
    }

    SequentialAnimation {
        id: bellFlashAnimation
        NumberAnimation {
            target: bellOverlay
            property: "opacity"
            from: 0.25
            to: 0
            duration: 150
            easing.type: Easing.OutQuad
        }
    }

    Connections {
        target: terminalBackend
        function onBell() {
            bellFlashAnimation.restart();
        }
    }

    // Main workspace area for key input handling
    FocusScope {
        id: mainScope
        anchors.fill: parent
        focus: true

        // Capture keyboard events and forward to native terminal
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Shift) {
                terminalMouseArea.shiftPressed = true;
            }
            if ((event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_Insert) {
                terminalBackend.pasteFromClipboard();
                event.accepted = true;
                return;
            }
            if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_V) {
                terminalBackend.pasteFromClipboard();
                event.accepted = true;
                return;
            }
            event.accepted = true;
            if (!terminalBackend.isConnected) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    terminalBackend.recreateShell();
                }
                return;
            }
            
            var seq = "";
            if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    if (root.configuration && root.configuration.FontSize !== undefined) {
                        root.configuration.FontSize = Math.min(72, root.configuration.FontSize + 1);
                    } else {
                        root.fontSize = Math.min(72, root.fontSize + 1);
                    }
                    event.accepted = true;
                    return;
                } else if (event.key === Qt.Key_Minus) {
                    if (root.configuration && root.configuration.FontSize !== undefined) {
                        root.configuration.FontSize = Math.max(8, root.configuration.FontSize - 1);
                    } else {
                        root.fontSize = Math.max(8, root.fontSize - 1);
                    }
                    event.accepted = true;
                    return;
                } else if (event.key >= Qt.Key_A && event.key <= Qt.Key_Z) {
                    seq = String.fromCharCode(event.key - Qt.Key_A + 1);
                }
            } else {
                if (event.modifiers & Qt.ShiftModifier) {
                    if (event.key === Qt.Key_PageUp) {
                        terminalBackend.scrollLines(10);
                        event.accepted = true;
                        return;
                    } else if (event.key === Qt.Key_PageDown) {
                        terminalBackend.scrollLines(-10);
                        event.accepted = true;
                        return;
                    }
                }
                switch (event.key) {
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        seq = "\r";
                        break;
                    case Qt.Key_Backspace:
                        seq = "\x7f";
                        break;
                    case Qt.Key_Tab:
                        seq = "\t";
                        break;
                    case Qt.Key_Escape:
                        seq = "\x1b";
                        break;
                    case Qt.Key_Up:
                        seq = "\x1b[A";
                        break;
                    case Qt.Key_Down:
                        seq = "\x1b[B";
                        break;
                    case Qt.Key_Right:
                        seq = "\x1b[C";
                        break;
                    case Qt.Key_Left:
                        seq = "\x1b[D";
                        break;
                    default:
                        if (event.text) {
                            seq = event.text;
                        }
                        break;
                }
            }
            if (seq !== "") {
                terminalBackend.sendInput(seq);
                event.accepted = true;
            }
        }

        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Shift) {
                terminalMouseArea.shiftPressed = false;
            }
        }

        // Mouse click triggers focus, wheel event handles scrolling, selection, and primary clipboard paste
        MouseArea {
            id: terminalMouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            cursorShape: {
                if (terminalBackend.isMouseTrackingActive() && !shiftPressed) {
                    return Qt.ArrowCursor;
                }
                return (terminalMouseArea.containsMouse && terminalBackend.isPositionHoverable(mouseX, mouseY)) ? Qt.IBeamCursor : Qt.ArrowCursor;
            }

            property bool shiftPressed: false

            onPressed: (mouse) => {
                mainScope.forceActiveFocus();
                if (terminalBackend.isMouseTrackingActive() && !(mouse.modifiers & Qt.ShiftModifier)) {
                    var btn = 0;
                    if (mouse.button === Qt.RightButton) btn = 2;
                    else if (mouse.button === Qt.MiddleButton) btn = 1;
                    terminalBackend.sendMouseEvent(0, btn, mouse.x, mouse.y, true);
                    mouse.accepted = true;
                } else {
                    if (mouse.button === Qt.LeftButton) {
                        terminalBackend.startSelection(mouse.x, mouse.y);
                        mouse.accepted = true;
                    } else if (mouse.button === Qt.MiddleButton) {
                        terminalBackend.pasteFromSelection();
                        mouse.accepted = true;
                    } else {
                        mouse.accepted = false;
                    }
                }
            }

            onReleased: (mouse) => {
                if (terminalBackend.isMouseTrackingActive() && !(mouse.modifiers & Qt.ShiftModifier)) {
                    var btn = 0;
                    if (mouse.button === Qt.RightButton) btn = 2;
                    else if (mouse.button === Qt.MiddleButton) btn = 1;
                    terminalBackend.sendMouseEvent(1, btn, mouse.x, mouse.y, false);
                    mouse.accepted = true;
                } else {
                    if (mouse.button === Qt.LeftButton) {
                        terminalBackend.endSelection();
                        mouse.accepted = true;
                    }
                }
            }

            onPositionChanged: (mouse) => {
                if (terminalBackend.isMouseTrackingActive() && !(mouse.modifiers & Qt.ShiftModifier)) {
                    var btn = 0;
                    if (mouse.buttons & Qt.LeftButton) btn = 0;
                    else if (mouse.buttons & Qt.RightButton) btn = 2;
                    else if (mouse.buttons & Qt.MiddleButton) btn = 1;
                    terminalBackend.sendMouseEvent(2, btn, mouse.x, mouse.y, true);
                    mouse.accepted = true;
                } else {
                    if (mouse.buttons & Qt.LeftButton) {
                        terminalBackend.updateSelection(mouse.x, mouse.y);
                        mouse.accepted = true;
                    }
                }
            }

            onWheel: (wheel) => {
                if (wheel.modifiers & Qt.ControlModifier) {
                    if (wheel.angleDelta.y > 0) {
                        if (root.configuration && root.configuration.FontSize !== undefined) {
                            root.configuration.FontSize = Math.min(72, root.configuration.FontSize + 1);
                        } else {
                            root.fontSize = Math.min(72, root.fontSize + 1);
                        }
                    } else if (wheel.angleDelta.y < 0) {
                        if (root.configuration && root.configuration.FontSize !== undefined) {
                            root.configuration.FontSize = Math.max(8, root.configuration.FontSize - 1);
                        } else {
                            root.fontSize = Math.max(8, root.fontSize - 1);
                        }
                    }
                    wheel.accepted = true;
                } else if (terminalBackend.isMouseTrackingActive() && !(wheel.modifiers & Qt.ShiftModifier)) {
                    var btn = (wheel.angleDelta.y > 0) ? 0 : 1;
                    terminalBackend.sendMouseEvent(3, btn, wheel.x, wheel.y, true);
                    wheel.accepted = true;
                } else if (terminalBackend.isAltScreen()) {
                    if (wheel.angleDelta.y > 0) {
                        terminalBackend.sendInput("\x1b[A\x1b[A\x1b[A");
                    } else if (wheel.angleDelta.y < 0) {
                        terminalBackend.sendInput("\x1b[B\x1b[B\x1b[B");
                    }
                    wheel.accepted = true;
                } else {
                    if (wheel.angleDelta.y > 0) {
                        terminalBackend.scrollLines(3);
                    } else if (wheel.angleDelta.y < 0) {
                        terminalBackend.scrollLines(-3);
                    }
                    wheel.accepted = true;
                }
            }
        }
    }



    function sendTheme() {
        var bg = Kirigami.Theme.backgroundColor.toString();
        var fg = Kirigami.Theme.textColor.toString();
        var isDark = (Kirigami.Theme.backgroundColor.r + Kirigami.Theme.backgroundColor.g + Kirigami.Theme.backgroundColor.b) < 1.5;
        terminalBackend.setTheme(bg, fg, isDark);
    }
}
