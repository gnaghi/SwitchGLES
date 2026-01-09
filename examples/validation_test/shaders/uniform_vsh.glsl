#version 460

layout(location = 0) in vec2 a_position;

/* Each uniform in its own block with explicit binding */
layout(std140, binding = 0) uniform MatrixBlock {
    mat4 u_matrix;
};

layout(std140, binding = 1) uniform OffsetBlock {
    vec4 u_offset;
};

void main() {
    gl_Position = u_matrix * vec4(a_position, 0.0, 1.0) + u_offset;
}
