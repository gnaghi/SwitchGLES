#version 460

layout(location = 0) in vec3 a_position;

layout(std140, binding = 0) uniform MVPBlock {
    mat4 u_mvp;
};

layout(location = 0) out vec3 v_texCoord;

void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_texCoord = a_position;  // Use position as cubemap direction
}
