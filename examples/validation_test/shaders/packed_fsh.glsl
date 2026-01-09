#version 460

layout(location = 0) out vec4 fragColor;

/* Single packed UBO: vec4 color (16 bytes) + vec4 params (16 bytes) = 32 bytes total */
layout(std140, binding = 0) uniform PackedFragBlock {
    vec4 u_color;     /* offset 0 */
    vec4 u_params;    /* offset 16: .x = alpha multiplier */
};

void main() {
    fragColor = vec4(u_color.rgb, u_color.a * u_params.x);
}
