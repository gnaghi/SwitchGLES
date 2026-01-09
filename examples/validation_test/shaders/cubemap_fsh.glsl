#version 460

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec3 v_texcoord;

layout(binding = 0) uniform samplerCube u_cubemap;

void main() {
    fragColor = texture(u_cubemap, v_texcoord);
}
