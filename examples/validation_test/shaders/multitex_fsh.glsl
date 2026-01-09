#version 460

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

/* Two texture samplers */
layout(binding = 0) uniform sampler2D u_texture0;
layout(binding = 1) uniform sampler2D u_texture1;

/* Blend factor - UBOs and samplers use separate binding namespaces */
layout(std140, binding = 0) uniform BlendBlock {
    float u_blend;
};

void main() {
    vec4 color0 = texture(u_texture0, v_texcoord);
    vec4 color1 = texture(u_texture1, v_texcoord);

    /* DEBUG: Output u_blend as red channel, texture0.r as green, texture1.b as blue
     * This helps diagnose which components are working:
     * - If red changes (0->255 as blend 0->1): uniform is working
     * - If green is 255: texture0 (red) is being sampled
     * - If blue is 255: texture1 (blue) is being sampled */
    fragColor = vec4(u_blend, color0.r, color1.b, 1.0);
}
