/*
 * SwitchGLES - Lit Vertex Shader (FFP)
 *
 * Implements per-vertex lighting with up to 8 lights
 *
 * To compile with UAM:
 *   uam -s vert ffp_lit_vsh.glsl -o ffp_lit_vsh.dksh
 */

#version 460

/* Uniform buffer - matrices */
layout(std140, binding = 0) uniform FFPUniforms {
    mat4 mvp;           /* ModelViewProjection matrix */
    mat4 modelview;     /* ModelView matrix */
    mat4 projection;    /* Projection matrix */
    mat4 normal_mat;    /* Normal matrix (inverse transpose of modelview) */
    vec4 default_color; /* Default color */
};

/* Light structure - must match sgl_light
 * NOTE: Do NOT add float _pad[3] here! In std140, arrays have 16-byte element
 * alignment, so float[3] would be 48 bytes instead of 12. The natural std140
 * struct padding rounds the struct to 112 bytes, matching the C struct.
 */
struct Light {
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
    vec4 position;      /* In eye coordinates */
    vec3 spot_direction;
    float spot_exponent;
    float spot_cutoff;  /* In degrees, 180 = no spotlight */
    float constant_attenuation;
    float linear_attenuation;
    float quadratic_attenuation;
    int enabled;
    /* No _pad here - std140 naturally pads struct to 112 bytes */
};

/* Uniform buffer - lighting */
layout(std140, binding = 1) uniform LightingUniforms {
    Light lights[8];
    vec4 light_model_ambient;
    vec4 material_ambient;
    vec4 material_diffuse;
    vec4 material_specular;
    vec4 material_emission;
    float material_shininess;
    int lighting_enabled;
    int fog_enabled;
    int fog_mode;      /* 0=LINEAR, 1=EXP, 2=EXP2 */
    vec4 fog_color;
    float fog_density;
    float fog_start;
    float fog_end;
};

/* Vertex inputs */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_texcoord;
layout(location = 3) in vec3 in_normal;

/* Outputs to fragment shader */
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_texcoord;
layout(location = 2) out float out_fog_factor;

void main()
{
    /* Transform vertex position */
    gl_Position = mvp * vec4(in_position, 1.0);

    /* Pass texture coordinates */
    out_texcoord = in_texcoord;

    /* Default fog factor (1 = no fog) */
    out_fog_factor = 1.0;

    if (lighting_enabled == 0) {
        /* No lighting - just pass vertex color */
        out_color = in_color;
    } else {
        /* Transform position and normal to eye coordinates */
        vec3 eye_pos = (modelview * vec4(in_position, 1.0)).xyz;
        vec3 eye_normal = normalize(mat3(normal_mat) * in_normal);

        /* Start with emission and ambient */
        vec4 color = material_emission;
        color += material_ambient * light_model_ambient;

        /* Add contribution from each enabled light */
        for (int i = 0; i < 8; i++) {
            if (lights[i].enabled == 0) continue;

            vec3 light_dir;
            float attenuation = 1.0;

            if (lights[i].position.w == 0.0) {
                /* Directional light */
                light_dir = normalize(lights[i].position.xyz);
            } else {
                /* Point/spot light */
                vec3 light_vec = lights[i].position.xyz - eye_pos;
                float dist = length(light_vec);
                light_dir = light_vec / dist;

                /* Attenuation */
                attenuation = 1.0 / (lights[i].constant_attenuation +
                                     lights[i].linear_attenuation * dist +
                                     lights[i].quadratic_attenuation * dist * dist);

                /* Spotlight */
                if (lights[i].spot_cutoff < 180.0) {
                    float spot_cos = dot(-light_dir, normalize(lights[i].spot_direction));
                    float cutoff_cos = cos(radians(lights[i].spot_cutoff));
                    if (spot_cos < cutoff_cos) {
                        attenuation = 0.0;
                    } else {
                        attenuation *= pow(spot_cos, lights[i].spot_exponent);
                    }
                }
            }

            /* Ambient contribution */
            color += material_ambient * lights[i].ambient * attenuation;

            /* Diffuse contribution */
            float n_dot_l = max(dot(eye_normal, light_dir), 0.0);
            color += material_diffuse * lights[i].diffuse * n_dot_l * attenuation;

            /* Specular contribution */
            if (n_dot_l > 0.0 && material_shininess > 0.0) {
                vec3 view_dir = normalize(-eye_pos);
                vec3 half_vec = normalize(light_dir + view_dir);
                float n_dot_h = max(dot(eye_normal, half_vec), 0.0);
                float spec = pow(n_dot_h, material_shininess);
                color += material_specular * lights[i].specular * spec * attenuation;
            }
        }

        /* Clamp and preserve alpha from diffuse material */
        out_color = vec4(clamp(color.rgb, 0.0, 1.0), material_diffuse.a);
    }

    /* Calculate fog factor if enabled */
    if (fog_enabled != 0) {
        vec3 eye_pos = (modelview * vec4(in_position, 1.0)).xyz;
        float eye_dist = length(eye_pos);

        if (fog_mode == 0) {
            /* GL_LINEAR */
            out_fog_factor = clamp((fog_end - eye_dist) / (fog_end - fog_start), 0.0, 1.0);
        } else if (fog_mode == 1) {
            /* GL_EXP */
            out_fog_factor = exp(-fog_density * eye_dist);
        } else {
            /* GL_EXP2 */
            float f = fog_density * eye_dist;
            out_fog_factor = exp(-f * f);
        }
    }
}
