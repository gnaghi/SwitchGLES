/*
 * SwitchGLES - Textured Vertex Shader (FFP)
 *
 * This shader supports textured rendering with position, color, and texcoord.
 *
 * To compile with UAM:
 *   uam -s vert ffp_tex_vsh.glsl -o ffp_tex_vsh.dksh
 */

#version 460

/* Uniform buffer - matrices */
layout(std140, binding = 0) uniform FFPUniforms {
    mat4 mvp;           /* ModelViewProjection matrix */
    mat4 modelview;     /* ModelView matrix */
    mat4 projection;    /* Projection matrix */
    mat4 normal_mat;    /* Normal matrix */
    vec4 default_color; /* Default color */
};

/* Vertex inputs */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;      /* RGBA color */
layout(location = 2) in vec2 in_texcoord;   /* Texture coordinates */
layout(location = 3) in vec3 in_normal;     /* Normal (for future lighting) */

/* Outputs to fragment shader */
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_texcoord;
layout(location = 2) out vec3 out_normal;

void main()
{
    /* Transform vertex position */
    gl_Position = mvp * vec4(in_position, 1.0);

    /* Pass color */
    out_color = in_color;

    /* Pass texture coordinates */
    out_texcoord = in_texcoord;

    /* Transform normal (simplified - just pass through for now) */
    out_normal = mat3(normal_mat) * in_normal;
}
