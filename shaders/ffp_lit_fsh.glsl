/*
 * SwitchGLES - Lit Fragment Shader (FFP)
 *
 * Applies fog to lit fragments
 *
 * To compile with UAM:
 *   uam -s frag ffp_lit_fsh.glsl -o ffp_lit_fsh.dksh
 */

#version 460

/* Light structure - must match vertex shader
 * NOTE: No float _pad[3] - std140 arrays have 16-byte element alignment!
 */
struct Light {
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
    vec4 position;
    vec3 spot_direction;
    float spot_exponent;
    float spot_cutoff;
    float constant_attenuation;
    float linear_attenuation;
    float quadratic_attenuation;
    int enabled;
};

/* Uniform buffer - lighting (for fog color) */
layout(std140, binding = 1) uniform LightingUniforms {
    Light lights[8];  /* Must match vertex shader layout */
    vec4 light_model_ambient;
    vec4 material_ambient;
    vec4 material_diffuse;
    vec4 material_specular;
    vec4 material_emission;
    float material_shininess;
    int lighting_enabled;
    int fog_enabled;
    int fog_mode;
    vec4 fog_color;
    float fog_density;
    float fog_start;
    float fog_end;
};

/* Inputs from vertex shader */
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in float in_fog_factor;

/* Output color */
layout(location = 0) out vec4 out_color;

void main()
{
    vec4 color = in_color;

    /* Apply fog */
    if (fog_enabled != 0) {
        color.rgb = mix(fog_color.rgb, color.rgb, in_fog_factor);
    }

    out_color = color;
}
