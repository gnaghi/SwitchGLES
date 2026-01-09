#version 460

layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform FragUniforms {
    vec4 u_color;
};

void main() {
    fragColor = u_color;
}
