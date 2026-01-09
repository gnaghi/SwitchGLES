#version 460

layout(location = 0) in vec3 v_texCoord;

layout(binding = 0) uniform samplerCube u_cubemap;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(u_cubemap, v_texCoord);
}
