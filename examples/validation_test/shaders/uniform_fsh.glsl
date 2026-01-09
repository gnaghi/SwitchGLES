#version 460

layout(location = 0) out vec4 fragColor;

/* Each uniform in its own block with explicit binding */
layout(std140, binding = 0) uniform ColorBlock {
    vec4 u_color;
};

layout(std140, binding = 1) uniform AlphaBlock {
    float u_alpha;
};

layout(std140, binding = 2) uniform ModeBlock {
    int u_mode;
};

void main() {
    if (u_mode == 1) {
        fragColor = vec4(u_color.rgb, u_alpha);
    } else {
        fragColor = u_color;
    }
}
