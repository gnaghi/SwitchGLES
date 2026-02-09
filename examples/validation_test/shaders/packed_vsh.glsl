#version 460

layout(location = 0) in vec2 a_position;

/* Single packed UBO: mat4 (64 bytes) + vec4 (16 bytes) = 80 bytes total */
layout(std140, binding = 0) uniform PackedVertexBlock {
    mat4 u_matrix;    /* offset 0 */
    vec4 u_offset;    /* offset 64 */
};

void main() {
    gl_Position = u_matrix * vec4(a_position, 0.0, 1.0) + u_offset;
}
