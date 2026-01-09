#version 460

layout(location = 0) out vec4 fragColor;

/* Simple blend uniform test - no textures */
layout(std140, binding = 0) uniform BlendBlock {
    float u_blend;
};

void main() {
    /* Output blend value as grayscale
     * blend=0 -> black
     * blend=0.5 -> gray
     * blend=1 -> white */
    fragColor = vec4(u_blend, u_blend, u_blend, 1.0);
}
