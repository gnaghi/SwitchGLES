/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Shared GL Types
 */

#ifndef SGL_GL_TYPES_H
#define SGL_GL_TYPES_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdint.h>

/* Configuration */
#define SGL_FB_NUM              3       /* Triple buffering */
#define SGL_FB_WIDTH            1280
#define SGL_FB_HEIGHT           720

#define SGL_CODE_MEM_SIZE       (16 * 1024 * 1024)  /* 16MB for precompiled shaders */
#define SGL_CMD_MEM_SIZE        (4 * 1024 * 1024)  /* 4MB - large for flush_finish calibration loops */
#define SGL_DATA_MEM_SIZE       (256 * 1024 * 1024)  /* 256MB (VBOs + client arrays + uniforms) */
#define SGL_UNIFORM_BUF_SIZE    (1024 * 1024)  /* 1MB (spearmint needs ~30 uniforms × many draws) */
#define SGL_UNIFORM_ALIGNMENT   0x100   /* DK_UNIFORM_BUF_ALIGNMENT */
#define SGL_CODE_ALIGNMENT      0x100   /* Shader code alignment (256 bytes) */
#define SGL_PAGE_ALIGNMENT      0x1000  /* Memory block page alignment (4KB) */
#define SGL_TEXTURE_MEM_SIZE    (128 * 1024 * 1024) /* 128MB for textures */
#define SGL_DESCRIPTOR_MEM_SIZE (SGL_MAX_TEXTURES * 64)  /* 64 = sizeof(DkImageDescriptor) + sizeof(DkSamplerDescriptor) */

/* Alignment helper */
#define SGL_ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

/* Packed uniform location encoding.
 * CRITICAL: bit 31 must be 0 so GLint locations are always >= 0.
 * GLES spec: glGetUniformLocation returns -1 for inactive uniforms,
 * and apps check "loc < 0" to skip inactive uniforms. Using bit 31
 * as a flag makes all packed locations negative, causing apps (including
 * dEQP) to skip glUniform* calls entirely. */
#define SGL_LOC_PACKED_FLAG     (1u << 30)
#define SGL_LOC_SAMPLER_FLAG    (1u << 29)  /* sampler: lower bits = sampler index in program */
#define SGL_LOC_STAGE_SHIFT     24
#define SGL_LOC_STAGE_MASK      0x1F        /* 5 bits (24-28), avoids flag bits 29-30 */
#define SGL_LOC_BINDING_SHIFT   16
#define SGL_LOC_BINDING_MASK    0xFF
#define SGL_LOC_OFFSET_MASK     0xFFFF

/* Maximum resources */
#define SGL_MAX_SURFACES        4
#define SGL_MAX_CONTEXTS        4
#define SGL_MAX_BUFFERS         1024
#define SGL_MAX_SHADERS         2048
#define SGL_MAX_PROGRAMS        512
#define SGL_MAX_TEXTURES        1024
#define SGL_MAX_FRAMEBUFFERS    64
#define SGL_MAX_RENDERBUFFERS   64
#define SGL_MAX_ATTRIBS         32  /* Matches Mesa pc->MaxAttribs=32 and DK_MAX_VERTEX_ATTRIBS=32. Hardware has 16 native slots (MaxNativeAttribs); extra slots support aliased-inactive attributes per GLES2 spec. */
#define SGL_MAX_ATTRIB_BINDINGS 32  /* Capacity for attrib_bindings[] */
#define SGL_MAX_UNIFORMS        16
#define SGL_MAX_TEXTURE_UNITS   16

/* Packed UBO configuration */
#define SGL_MAX_PACKED_UBO_SIZE  8192  /* Max bytes per packed UBO (supports 128 bones) */
#define SGL_MAX_PACKED_UBOS      2     /* Per stage: 0=main, 1=bones */
#define SGL_ATTRIB_NAME_MAX      64    /* Max attribute name length */

/* Packed UBO shadow buffer (CPU-side, flushed to GPU at draw time) */
typedef struct sgl_packed_ubo {
    uint8_t data[SGL_MAX_PACKED_UBO_SIZE];  /* CPU shadow buffer */
    uint32_t size;       /* Total used size (set at registration time) */
    bool dirty;          /* Any uniform written since last bind? */
    bool valid;          /* Has been configured? */
} sgl_packed_ubo_t;

/* Buffer object */
typedef struct sgl_buffer {
    bool used;
    GLenum target;
    GLsizeiptr size;
    GLenum usage;
    uint32_t backend_handle;
    uint32_t data_offset;
} sgl_buffer_t;

/* Mesa direct compilation metadata (for ES 1.00 shaders compiled without transpiler) */
#define SGL_MESA_MAX_UNIFORMS 128
#define SGL_MESA_MAX_SAMPLERS 16
#define SGL_MESA_MAX_INPUTS   32

typedef struct sgl_mesa_uniform {
    char name[SGL_ATTRIB_NAME_MAX];
    uint32_t offset;        /* Byte offset in constbuf */
    uint32_t size_bytes;    /* Total size in bytes */
    GLenum gl_type;         /* GL_FLOAT, GL_FLOAT_VEC4, GL_FLOAT_MAT4, etc. */
    uint32_t array_elements; /* 0 for non-array, N for array[N] */
} sgl_mesa_uniform_t;

typedef struct sgl_mesa_sampler {
    char name[SGL_ATTRIB_NAME_MAX];
    int binding;            /* Texture descriptor binding (0, 1, 2...) */
    GLenum gl_type;         /* GL_SAMPLER_2D or GL_SAMPLER_CUBE */
} sgl_mesa_sampler_t;

typedef struct sgl_mesa_input {
    char name[SGL_ATTRIB_NAME_MAX];
    int location;           /* Generic attribute location (0-based) */
    GLenum gl_type;         /* GL_FLOAT, GL_FLOAT_VEC4, GL_FLOAT_MAT4, etc. */
} sgl_mesa_input_t;

typedef struct sgl_mesa_metadata {
    int num_uniforms;
    sgl_mesa_uniform_t uniforms[SGL_MESA_MAX_UNIFORMS];
    uint32_t constbuf_size;
    int num_samplers;
    sgl_mesa_sampler_t samplers[SGL_MESA_MAX_SAMPLERS];
    int num_inputs;
    sgl_mesa_input_t inputs[SGL_MESA_MAX_INPUTS];
    uint8_t *initial_data;      /* Heap-allocated copy of Mesa constbuf initial data, or NULL */
    uint32_t initial_data_size; /* Size in bytes of initial_data */
    int depth_range_offset;     /* Byte offset of gl_DepthRange in constbuf, -1 = not used */
} sgl_mesa_metadata_t;

/* Shader object */
typedef struct sgl_shader {
    bool used;
    GLenum type;
    bool compiled;
    bool needs_transpile;   /* true if source is GLSL ES 1.00 (deferred to link time) */
    bool compiled_via_mesa; /* true if compiled directly by Mesa (not transpiler) */
    bool delete_pending;    /* glDeleteShader called; defer actual free until detached */
    int  attach_count;      /* number of programs this shader is attached to */
    uint32_t backend_handle;
    uint32_t code_offset;
    uint32_t code_size;
    char *source;       /* GLSL source (from glShaderSource), NULL if precompiled */
    char *info_log;     /* Compilation info/error log */
    sgl_mesa_metadata_t *mesa_meta; /* Heap-allocated, NULL if not compiled via Mesa */
} sgl_shader_t;

/* Uniform binding info */
typedef struct sgl_uniform_binding {
    bool valid;
    uint32_t offset;
    uint32_t size;        /* Aligned size (256 bytes for deko3d) */
    uint32_t data_size;   /* Actual data size (e.g., 64 for mat4) */
    bool dirty;
    /* Shadow copy for glGetUniformfv/iv readback (max mat4 = 64 bytes) */
    uint8_t shadow[64];
    uint32_t shadow_size;
    uint32_t shadow_components; /* 1-4 for vec, 4/9/16 for mat */
    GLenum shadow_type;         /* GL_FLOAT or GL_INT */
} sgl_uniform_binding_t;

/* Attribute binding (from glBindAttribLocation or built-in defaults) */
typedef struct sgl_attrib_binding {
    char name[SGL_ATTRIB_NAME_MAX];
    GLuint index;           /* Pending binding (set by glBindAttribLocation, read by linker) */
    GLint linked_location;  /* Location from last link (-1 if not yet linked/inactive) */
    GLenum gl_type;   /* GL_FLOAT, GL_FLOAT_VEC2, ..., GL_FLOAT_MAT4 (from transpiler) */
    bool used;
    bool user_bound;  /* true if from glBindAttribLocation, false if linker-added */
    bool in_shader;   /* true if attribute exists in the compiled shader (active) */
} sgl_attrib_binding_t;

/* Per-program uniform location entry (from transpiler reflection) */
#define SGL_MAX_PROGRAM_UNIFORMS 128
#define SGL_MAX_PROGRAM_SAMPLERS 16
typedef struct sgl_program_uniform_loc {
    char name[SGL_ATTRIB_NAME_MAX];       /* Flattened name (e.g. "u_var_m0") */
    char gles_name[SGL_ATTRIB_NAME_MAX];  /* GLES API name (e.g. "u_var.m0") */
    GLint location;  /* packed encoded: SGL_LOC_PACKED_FLAG | stage | binding | offset */
    GLenum gl_type;  /* GL_FLOAT, GL_FLOAT_VEC2, ..., GL_FLOAT_MAT4, GL_INT_VEC4, etc. */
    GLint array_size; /* 0 or 1 = scalar, >1 = array[N] */
    uint16_t element_stride; /* Byte stride between array elements (Mesa: actual, transpiler: std140) */
    bool used;
} sgl_program_uniform_loc_t;

/* Active uniform info (populated when glGetUniformLocation succeeds) */
typedef struct sgl_active_uniform_info {
    char name[SGL_ATTRIB_NAME_MAX];
    GLint location;
    GLenum type;        /* GL_FLOAT, GL_FLOAT_VEC2, ..., GL_FLOAT_MAT4, GL_INT, etc. */
    GLint size;         /* 1 for non-arrays */
    uint16_t element_stride; /* Byte stride between array elements (0 = non-array or std140) */
    bool active;
} sgl_active_uniform_info_t;

/* Per-program sampler info (from transpiler) */
typedef struct sgl_program_sampler {
    char name[SGL_ATTRIB_NAME_MAX];       /* Flattened name (e.g. "s_0") */
    char gles_name[SGL_ATTRIB_NAME_MAX];  /* GLES API name (e.g. "s[0]") */
    int shader_binding;  /* layout(binding=N) for FS (or VS if FS-only absent) */
    int vs_shader_binding; /* layout(binding=N) for VS when sampler in both stages (-1 = use shader_binding) */
    int tex_unit;        /* GL texture unit assigned via glUniform1i (default: same as binding) */
    int array_index;     /* index within sampler array (-1 if not array) */
    int array_total;     /* total elements in sampler array (0 if not array) */
    GLenum gl_type;      /* GL_SAMPLER_2D or GL_SAMPLER_CUBE */
    bool used;
} sgl_program_sampler_t;

/* Program object */
typedef struct sgl_program {
    bool used;
    bool linked;
    bool validated;
    bool shaders_initialized;
    bool delete_pending;
    char *info_log;         /* Link info/error log (NULL = empty) */
    GLuint vertex_shader;
    GLuint fragment_shader;
    uint32_t backend_handle;
    sgl_uniform_binding_t vertex_uniforms[SGL_MAX_UNIFORMS];
    sgl_uniform_binding_t fragment_uniforms[SGL_MAX_UNIFORMS];
    /* Packed UBO shadow buffers (per-stage, per-binding) */
    sgl_packed_ubo_t packed_vertex[SGL_MAX_PACKED_UBOS];
    sgl_packed_ubo_t packed_fragment[SGL_MAX_PACKED_UBOS];
    /* Attribute bindings (from glBindAttribLocation) */
    sgl_attrib_binding_t attrib_bindings[SGL_MAX_ATTRIB_BINDINGS];
    int num_attrib_bindings;
    int num_active_attribs;  /* Count of attributes actually in linked shader (for GL_ACTIVE_ATTRIBUTES) */
    /* Active uniform tracking (populated by glGetUniformLocation) */
    sgl_active_uniform_info_t active_uniforms[SGL_MAX_UNIFORMS * 2]; /* VS + FS */
    int num_active_uniforms;
    /* Per-program uniform locations (from transpiler reflection at link time) */
    sgl_program_uniform_loc_t program_uniforms[SGL_MAX_PROGRAM_UNIFORMS];
    int num_program_uniforms;
    /* Per-program packed UBO sizes [stage][binding] (from transpiler) */
    int packed_ubo_sizes[2][SGL_MAX_PACKED_UBOS];
    /* Per-program sampler info (from transpiler — maps sampler names to bindings) */
    sgl_program_sampler_t samplers[SGL_MAX_PROGRAM_SAMPLERS];
    int num_samplers;
    /* Packed UBO dual-stage mirrors: when a uniform exists in both VS and FS UBOs,
     * glGetUniformLocation returns the VS location as primary; the FS location is
     * stored here so set_*_uniform can write to both packed UBOs. */
#define SGL_MAX_PACKED_MIRRORS 16
    struct { GLint primary; GLint mirror; } packed_mirrors[SGL_MAX_PACKED_MIRRORS];
    int num_packed_mirrors;
    /* gl_DepthRange built-in: packed locations for sgl_dr_near/far/diff (0 = not used) */
    GLint depth_range_loc[3]; /* [0]=near, [1]=far, [2]=diff */
    GLint depth_range_loc_fs[3]; /* Mirror for FS when both VS+FS use gl_DepthRange */
    bool has_depth_range;
} sgl_program_t;

/* Texture object */
typedef struct sgl_texture {
    bool used;
    GLenum target;
    GLsizei width, height;
    GLenum internal_format;
    uint32_t backend_handle;
    /* Sampler parameters */
    GLenum min_filter;
    GLenum mag_filter;
    GLenum wrap_s;
    GLenum wrap_t;
    /* Cubemap completeness: set true when a face is uploaded with
     * dimensions different from level-0. Cleared on glTexImage2D level 0
     * if the first face re-establishes a consistent size. */
    bool cubemap_incomplete;
    /* Deferred deletion: GPU data kept alive while attached to non-current FBOs */
    bool delete_pending;    /* glDeleteTextures called, but still referenced by FBO(s) */
    int fbo_ref_count;      /* Number of FBO attachment points referencing this texture */
} sgl_texture_t;

/* Framebuffer object */
typedef struct sgl_framebuffer {
    bool used;
    bool bound;                 /* Has been bound at least once (for glIsFramebuffer) */
    GLuint color_attachment;    /* Texture or Renderbuffer ID */
    GLuint depth_attachment;    /* Texture or Renderbuffer ID or 0 */
    GLuint stencil_attachment;  /* Renderbuffer ID or 0 */
    uint32_t backend_handle;
    bool is_complete;
    bool color_is_renderbuffer;   /* true if color_attachment is a renderbuffer */
    bool depth_is_renderbuffer;   /* true if depth_attachment is a renderbuffer */
    bool stencil_is_renderbuffer; /* true if stencil_attachment is a renderbuffer */
    GLenum color_textarget;       /* textarget used in glFramebufferTexture2D (for cubemap face query) */
    GLenum depth_textarget;       /* textarget for depth attachment (cubemap face query) */
    GLenum stencil_textarget;     /* textarget for stencil attachment (cubemap face query) */
} sgl_framebuffer_t;

/* Renderbuffer object */
typedef struct sgl_renderbuffer {
    bool used;
    bool bound;                 /* Has been bound at least once (for glIsRenderbuffer) */
    GLenum internal_format;
    GLsizei width, height;
    uint32_t backend_handle;
    bool delete_pending;    /* glDeleteRenderbuffers called, but still referenced by FBO(s) */
    int fbo_ref_count;      /* Number of FBO attachment points referencing this RB */
} sgl_renderbuffer_t;

/* Vertex attribute state */
typedef struct sgl_vertex_attrib {
    bool enabled;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    const void *pointer;
    GLuint buffer;  /* Bound VBO or 0 for client array */
    uint32_t buffer_offset;  /* GPU buffer offset (computed before draw) */
    uint32_t buffer_data_size; /* Total VBO data size (for correct buffer extents) */
    GLfloat current_value[4]; /* Constant value when array is disabled (default: 0,0,0,1) */
} sgl_vertex_attrib_t;

#endif /* SGL_GL_TYPES_H */
