#version 460

layout(location = 0) in vec2 a_position;

/* Test all vertex uniform types - using unique names to avoid conflicts */

/* Binding 0: float - scale factor */
layout(std140, binding = 0) uniform ScaleBlock {
    float u_testScale;
};

/* Binding 1: vec2 - offset */
layout(std140, binding = 1) uniform OffsetBlock {
    vec2 u_testOffset2;
};

/* Binding 2: vec3 - additional offset (z ignored for 2D) */
layout(std140, binding = 2) uniform Offset3Block {
    vec3 u_testOffset3;
};

/* Binding 3: mat2 - 2D rotation/scale */
layout(std140, binding = 3) uniform Mat2Block {
    mat2 u_testMat2;
};

/* Binding 4: mat3 - 2D transform with translation */
layout(std140, binding = 4) uniform Mat3Block {
    mat3 u_testMat3;
};

void main() {
    /* Apply scale */
    vec2 scaled = a_position * u_testScale;

    /* Apply mat2 rotation */
    vec2 rotated = u_testMat2 * scaled;

    /* Apply offsets */
    vec2 final = rotated + u_testOffset2.xy + u_testOffset3.xy;

    gl_Position = vec4(final, 0.0, 1.0);
}
