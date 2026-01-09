#version 460

layout(location = 0) in vec3 a_position;

void main() {
    gl_Position = vec4(a_position, 1.0);
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
}
