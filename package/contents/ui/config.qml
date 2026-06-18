import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    
    property alias cfg_FontName: fontField.text
    property alias cfg_FontSize: sizeSpin.value
    property alias cfg_PaddingLeft: leftSpin.value
    property alias cfg_PaddingRight: rightSpin.value
    property alias cfg_PaddingTop: topSpin.value
    property alias cfg_PaddingBottom: bottomSpin.value

    Kirigami.FormLayout {
        anchors.fill: parent
        
        TextField {
            id: fontField
            Kirigami.FormData.label: "Font Family:"
            placeholderText: "Monospace"
        }
        
        SpinBox {
            id: sizeSpin
            from: 8
            to: 72
            Kirigami.FormData.label: "Font Size:"
        }

        SpinBox {
            id: leftSpin
            from: 0
            to: 500
            Kirigami.FormData.label: "Padding Left (px):"
        }

        SpinBox {
            id: topSpin
            from: 0
            to: 500
            Kirigami.FormData.label: "Padding Top (px):"
        }

        SpinBox {
            id: rightSpin
            from: 0
            to: 500
            Kirigami.FormData.label: "Padding Right (px):"
        }

        SpinBox {
            id: bottomSpin
            from: 0
            to: 500
            Kirigami.FormData.label: "Padding Bottom (px):"
        }
    }
}
