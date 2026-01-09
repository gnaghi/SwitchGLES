/*
 * SwitchGLES - Fixed Function Pipeline Basic Vertex Shader
 *
 * This shader handles basic vertex transformation with:
 * - MVP matrix transformation
 * - Vertex colors (pass-through)
 * - Texture coordinates (pass-through)
 *
 * To compile with UAM:
 *   uam -s vert ffp_basic.vert.glsl -o ffp_basic.vert.dksh
 */

#version 450

/* Uniform buffer - matrices and state */
layout(binding = 0) uniform FFPUniforms {
    mat4 mvp;           /* ModelViewProjection matrix */
    mat4 modelview;     /* ModelView matrix (for future lighting) */
    mat4 projection;    /* Projection matrix */
    mat4 normal_mat;    /* Normal matrix (for future lighting) */
    vec4 default_color; /* Default color if no vertex colors */
};

/* Vertex inputs */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_normal;

/* Outputs to fragment shader */
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_texcoord;
layout(location = 2) out vec3 out_normal;

void main()
{
    /* Transform vertex position */
    gl_Position = mvp * vec4(in_position, 1.0);

    /* Pass vertex color (or use default if all zeros - simple heuristic) */
    out_color = in_color;

    /* Pass texture coordinates */
    out_texcoord = in_texcoord;

    /* Transform normal (for future lighting) */
    out_normal = mat3(normal_mat) * in_normal;
}
