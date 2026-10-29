// SPDX-License-Identifier: MIT
#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float hasContour;
    vec2 viewSize;
    vec4 fadeArea0;
    vec4 fadeArea1;
    vec4 fadeArea2;
    vec4 fadeArea3;
    vec4 fadeRamp0;
    vec4 fadeRamp1;
    vec4 fadeRamp2;
    vec4 fadeRamp3;
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D maskTexture;

float fadeAt(vec2 p, vec4 area, vec4 ramp)
{
    if (p.x < area.x || p.y < area.y || p.x >= area.z || p.y >= area.w) return 1.0;
    return clamp(dot(p, ramp.xy) + ramp.z, 0.0, 1.0);
}

void main()
{
    vec2 p = qt_TexCoord0 * viewSize;
    float alpha = fadeAt(p, fadeArea0, fadeRamp0) * fadeAt(p, fadeArea1, fadeRamp1)
                * fadeAt(p, fadeArea2, fadeRamp2) * fadeAt(p, fadeArea3, fadeRamp3);
    if (hasContour > 0.5) alpha *= texture(maskTexture, qt_TexCoord0).a;
    fragColor = texture(source, qt_TexCoord0) * (qt_Opacity * alpha);
}
