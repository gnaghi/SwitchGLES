#version 460

layout(location = 0) in vec3 a_position;

layout(location = 0) out vec3 v_texcoord;

void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord = a_position;  // Use position as direction for cubemap lookup
}
