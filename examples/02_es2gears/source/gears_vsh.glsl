#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 v_Color;

layout(std140, binding = 0) uniform MVPBlock {
    mat4 ModelViewProjectionMatrix;
};

layout(std140, binding = 1) uniform NormalBlock {
    mat4 NormalMatrix;
};

layout(std140, binding = 2) uniform LightBlock {
    vec4 LightSourcePosition;
};

layout(std140, binding = 3) uniform ColorBlock {
    vec4 MaterialColor;
};

void main() {
    // Transform the normal to eye coordinates
    // Use w=0.0 because normals are directions, not positions (no translation)
    vec3 N = normalize(vec3(NormalMatrix * vec4(normal, 0.0)));

    // The LightSourcePosition is actually its direction for directional light
    vec3 L = normalize(LightSourcePosition.xyz);

    // Multiply the diffuse value by the vertex color
    float diffuse = max(dot(N, L), 0.0);
    v_Color = diffuse * MaterialColor;

    // Transform the position to clip coordinates
    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);
}
