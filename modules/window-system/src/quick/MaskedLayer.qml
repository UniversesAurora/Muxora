// SPDX-License-Identifier: MIT
import QtQuick

Item {
    id: root
    property Item contourItem
    property bool hasContour: false
    property bool hasFades: false
    property bool maskValid: true
    property vector4d fadeArea0
    property vector4d fadeArea1
    property vector4d fadeArea2
    property vector4d fadeArea3
    property vector4d fadeRamp0
    property vector4d fadeRamp1
    property vector4d fadeRamp2
    property vector4d fadeRamp3
    property alias contentItem: content

    ShaderEffectSource {
        id: contour
        sourceItem: root.maskValid && root.hasContour ? root.contourItem : null
        hideSource: true
        visible: false
        samples: 4
    }
    Item {
        id: content
        visible: root.maskValid
        anchors.fill: parent
        layer.enabled: root.maskValid && (root.hasFades || root.hasContour)
        layer.samples: 4
        layer.effect: ShaderEffect {
            required property Item source
            property Item maskTexture: root.hasContour ? contour : source
            property real hasContour: root.hasContour ? 1 : 0
            property vector2d viewSize: Qt.vector2d(root.width, root.height)
            property vector4d fadeArea0: root.fadeArea0
            property vector4d fadeArea1: root.fadeArea1
            property vector4d fadeArea2: root.fadeArea2
            property vector4d fadeArea3: root.fadeArea3
            property vector4d fadeRamp0: root.fadeRamp0
            property vector4d fadeRamp1: root.fadeRamp1
            property vector4d fadeRamp2: root.fadeRamp2
            property vector4d fadeRamp3: root.fadeRamp3
            fragmentShader: "qrc:/window-system/shaders/alpha_mask.frag.qsb"
        }
    }
}
