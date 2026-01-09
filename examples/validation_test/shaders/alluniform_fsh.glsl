#version 460

layout(location = 0) out vec4 fragColor;

/* Test all fragment uniform types - using unique names to avoid conflicts */

/* Binding 0: float - alpha multiplier */
layout(std140, binding = 0) uniform AlphaBlock {
    float u_testAlpha;
};

/* Binding 1: vec2 - color RG components */
layout(std140, binding = 1) uniform Vec2Block {
    vec2 u_testVec2;
};

/* Binding 2: vec3 - color RGB */
layout(std140, binding = 2) uniform Vec3Block {
    vec3 u_testVec3;
};

/* Binding 3: vec4 - full color */
layout(std140, binding = 3) uniform Vec4Block {
    vec4 u_testVec4;
};

/* Binding 4: int - mode selector */
layout(std140, binding = 4) uniform IntBlock {
    int u_testMode;
};

/* Binding 5: ivec2 - integer pair */
layout(std140, binding = 5) uniform Ivec2Block {
    ivec2 u_testIvec2;
};

/* Binding 6: ivec3 - integer triple */
layout(std140, binding = 6) uniform Ivec3Block {
    ivec3 u_testIvec3;
};

/* Binding 7: ivec4 - integer quad */
layout(std140, binding = 7) uniform Ivec4Block {
    ivec4 u_testIvec4;
};

void main() {
    /* Mode determines which uniform to use for color */
    if (u_testMode == 1) {
        /* Use float alpha with white */
        fragColor = vec4(1.0, 1.0, 1.0, u_testAlpha);
    } else if (u_testMode == 2) {
        /* Use vec2 for RG, blue=0, alpha=1 */
        fragColor = vec4(u_testVec2.x, u_testVec2.y, 0.0, 1.0);
    } else if (u_testMode == 3) {
        /* Use vec3 for RGB, alpha=1 */
        fragColor = vec4(u_testVec3, 1.0);
    } else if (u_testMode == 4) {
        /* Use vec4 directly */
        fragColor = u_testVec4;
    } else if (u_testMode == 5) {
        /* Use ivec2 (convert to float, expecting 0-255 range) */
        fragColor = vec4(float(u_testIvec2.x) / 255.0, float(u_testIvec2.y) / 255.0, 0.0, 1.0);
    } else if (u_testMode == 6) {
        /* Use ivec3 */
        fragColor = vec4(float(u_testIvec3.x) / 255.0, float(u_testIvec3.y) / 255.0, float(u_testIvec3.z) / 255.0, 1.0);
    } else if (u_testMode == 7) {
        /* Use ivec4 */
        fragColor = vec4(float(u_testIvec4.x) / 255.0, float(u_testIvec4.y) / 255.0,
                         float(u_testIvec4.z) / 255.0, float(u_testIvec4.w) / 255.0);
    } else {
        /* Default: red */
        fragColor = vec4(1.0, 0.0, 0.0, 1.0);
    }
}
