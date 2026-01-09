#version 460

layout(location = 0) in vec4 v_Color;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = v_Color;
}
