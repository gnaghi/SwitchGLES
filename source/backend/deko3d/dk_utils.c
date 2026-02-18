/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Utility/Conversion Functions
 *
 * This module contains helper functions for converting GL enums to deko3d types:
 * - Compare operations (depth, stencil)
 * - Stencil operations
 * - Blend factors and operations
 * - Primitive types
 * - Image formats
 * - Vertex attribute formats
 */

#include "dk_internal.h"
#include <GLES2/gl2ext.h>

/* ============================================================================
 * Compare Operation Conversion
 * ============================================================================ */

DkCompareOp dk_convert_compare_op(GLenum func) {
    switch (func) {
        case GL_NEVER:    return DkCompareOp_Never;
        case GL_LESS:     return DkCompareOp_Less;
        case GL_EQUAL:    return DkCompareOp_Equal;
        case GL_LEQUAL:   return DkCompareOp_Lequal;
        case GL_GREATER:  return DkCompareOp_Greater;
        case GL_NOTEQUAL: return DkCompareOp_NotEqual;
        case GL_GEQUAL:   return DkCompareOp_Gequal;
        case GL_ALWAYS:   return DkCompareOp_Always;
        default:          return DkCompareOp_Always;
    }
}

/* ============================================================================
 * Stencil Operation Conversion
 * ============================================================================ */

DkStencilOp dk_convert_stencil_op(GLenum op) {
    switch (op) {
        case GL_KEEP:      return DkStencilOp_Keep;
        case GL_ZERO:      return DkStencilOp_Zero;
        case GL_REPLACE:   return DkStencilOp_Replace;
        case GL_INCR:      return DkStencilOp_Incr;
        case GL_INCR_WRAP: return DkStencilOp_IncrWrap;
        case GL_DECR:      return DkStencilOp_Decr;
        case GL_DECR_WRAP: return DkStencilOp_DecrWrap;
        case GL_INVERT:    return DkStencilOp_Invert;
        default:           return DkStencilOp_Keep;
    }
}

/* ============================================================================
 * Blend Factor Conversion
 * ============================================================================ */

DkBlendFactor dk_convert_blend_factor(GLenum factor) {
    switch (factor) {
        case GL_ZERO:                     return DkBlendFactor_Zero;
        case GL_ONE:                      return DkBlendFactor_One;
        case GL_SRC_COLOR:                return DkBlendFactor_SrcColor;
        case GL_ONE_MINUS_SRC_COLOR:      return DkBlendFactor_InvSrcColor;
        case GL_DST_COLOR:                return DkBlendFactor_DstColor;
        case GL_ONE_MINUS_DST_COLOR:      return DkBlendFactor_InvDstColor;
        case GL_SRC_ALPHA:                return DkBlendFactor_SrcAlpha;
        case GL_ONE_MINUS_SRC_ALPHA:      return DkBlendFactor_InvSrcAlpha;
        case GL_DST_ALPHA:                return DkBlendFactor_DstAlpha;
        case GL_ONE_MINUS_DST_ALPHA:      return DkBlendFactor_InvDstAlpha;
        case GL_CONSTANT_COLOR:           return DkBlendFactor_ConstColor;
        case GL_ONE_MINUS_CONSTANT_COLOR: return DkBlendFactor_InvConstColor;
        case GL_CONSTANT_ALPHA:           return DkBlendFactor_ConstAlpha;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return DkBlendFactor_InvConstAlpha;
        case GL_SRC_ALPHA_SATURATE:       return DkBlendFactor_SrcAlphaSaturate;
        default:                          return DkBlendFactor_One;
    }
}

/* ============================================================================
 * Blend Operation Conversion
 * ============================================================================ */

DkBlendOp dk_convert_blend_op(GLenum op) {
    switch (op) {
        case GL_FUNC_ADD:              return DkBlendOp_Add;
        case GL_FUNC_SUBTRACT:         return DkBlendOp_Sub;
        case GL_FUNC_REVERSE_SUBTRACT: return DkBlendOp_RevSub;
        default:                       return DkBlendOp_Add;
    }
}

/* ============================================================================
 * Primitive Type Conversion
 * ============================================================================ */

DkPrimitive dk_convert_primitive(GLenum mode) {
    switch (mode) {
        case GL_POINTS:         return DkPrimitive_Points;
        case GL_LINES:          return DkPrimitive_Lines;
        case GL_LINE_LOOP:      return DkPrimitive_LineLoop;
        case GL_LINE_STRIP:     return DkPrimitive_LineStrip;
        case GL_TRIANGLES:      return DkPrimitive_Triangles;
        case GL_TRIANGLE_STRIP: return DkPrimitive_TriangleStrip;
        case GL_TRIANGLE_FAN:   return DkPrimitive_TriangleFan;
        default:                return DkPrimitive_Triangles;
    }
}

/* ============================================================================
 * Image Format Conversion
 * ============================================================================ */

DkImageFormat dk_convert_format(GLenum internalformat, GLenum format, GLenum type) {
    (void)internalformat;
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        return DkImageFormat_RGBA8_Unorm;
    }
    if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
        return DkImageFormat_RGBA8_Unorm;  /* Convert to RGBA */
    }
    if (format == GL_LUMINANCE && type == GL_UNSIGNED_BYTE) {
        return DkImageFormat_R8_Unorm;
    }
    if (format == GL_ALPHA && type == GL_UNSIGNED_BYTE) {
        return DkImageFormat_R8_Unorm;
    }
    if (format == GL_LUMINANCE_ALPHA && type == GL_UNSIGNED_BYTE) {
        return DkImageFormat_RG8_Unorm;
    }
    return DkImageFormat_RGBA8_Unorm;
}

/* ============================================================================
 * Compressed Format Conversion
 * ============================================================================ */

/**
 * Convert GL compressed format enum to DkImageFormat.
 * Returns DkImageFormat_None (0) if format is not supported.
 */
DkImageFormat dk_convert_compressed_format(GLenum internalformat) {
    switch (internalformat) {
        /* ASTC LDR formats */
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:   return DkImageFormat_RGBA_ASTC_4x4;
        case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:   return DkImageFormat_RGBA_ASTC_5x4;
        case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:   return DkImageFormat_RGBA_ASTC_5x5;
        case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:   return DkImageFormat_RGBA_ASTC_6x5;
        case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:   return DkImageFormat_RGBA_ASTC_6x6;
        case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:   return DkImageFormat_RGBA_ASTC_8x5;
        case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:   return DkImageFormat_RGBA_ASTC_8x6;
        case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:   return DkImageFormat_RGBA_ASTC_8x8;
        case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:  return DkImageFormat_RGBA_ASTC_10x5;
        case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:  return DkImageFormat_RGBA_ASTC_10x6;
        case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:  return DkImageFormat_RGBA_ASTC_10x8;
        case GL_COMPRESSED_RGBA_ASTC_10x10_KHR: return DkImageFormat_RGBA_ASTC_10x10;
        case GL_COMPRESSED_RGBA_ASTC_12x10_KHR: return DkImageFormat_RGBA_ASTC_12x10;
        case GL_COMPRESSED_RGBA_ASTC_12x12_KHR: return DkImageFormat_RGBA_ASTC_12x12;

        /* ASTC sRGB formats */
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:   return DkImageFormat_RGBA_ASTC_4x4_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:   return DkImageFormat_RGBA_ASTC_5x4_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:   return DkImageFormat_RGBA_ASTC_5x5_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:   return DkImageFormat_RGBA_ASTC_6x5_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:   return DkImageFormat_RGBA_ASTC_6x6_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:   return DkImageFormat_RGBA_ASTC_8x5_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:   return DkImageFormat_RGBA_ASTC_8x6_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:   return DkImageFormat_RGBA_ASTC_8x8_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:  return DkImageFormat_RGBA_ASTC_10x5_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:  return DkImageFormat_RGBA_ASTC_10x6_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:  return DkImageFormat_RGBA_ASTC_10x8_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR: return DkImageFormat_RGBA_ASTC_10x10_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR: return DkImageFormat_RGBA_ASTC_12x10_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR: return DkImageFormat_RGBA_ASTC_12x12_sRGB;

        /* ETC2/EAC formats */
        case GL_COMPRESSED_RGB8_ETC2:                    return DkImageFormat_RGB_ETC2;
        case GL_COMPRESSED_RGBA8_ETC2_EAC:               return DkImageFormat_RGBA_ETC2;
        case GL_COMPRESSED_SRGB8_ETC2:                   return DkImageFormat_RGB_ETC2_sRGB;
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:        return DkImageFormat_RGBA_ETC2_sRGB;
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2: return DkImageFormat_RGB_PTA_ETC2;
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2: return DkImageFormat_RGB_PTA_ETC2_sRGB;
        case GL_COMPRESSED_R11_EAC:                      return DkImageFormat_R_ETC2_Unorm;
        case GL_COMPRESSED_SIGNED_R11_EAC:               return DkImageFormat_R_ETC2_Snorm;
        case GL_COMPRESSED_RG11_EAC:                     return DkImageFormat_RG_ETC2_Unorm;
        case GL_COMPRESSED_SIGNED_RG11_EAC:              return DkImageFormat_RG_ETC2_Snorm;

        /* ETC1 (legacy, maps to ETC2 RGB which is backward compatible) */
        case GL_ETC1_RGB8_OES:                           return DkImageFormat_RGB_ETC2;

        /* S3TC/DXT (BC1-BC3) */
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:            return DkImageFormat_RGB_BC1;
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:           return DkImageFormat_RGBA_BC1;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:           return DkImageFormat_RGBA_BC2;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:           return DkImageFormat_RGBA_BC3;

        /* RGTC (BC4/BC5) */
        case GL_COMPRESSED_RED_RGTC1_EXT:                return DkImageFormat_R_BC4_Unorm;
        case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:         return DkImageFormat_R_BC4_Snorm;
        case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:          return DkImageFormat_RG_BC5_Unorm;
        case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:   return DkImageFormat_RG_BC5_Snorm;

        /* BPTC (BC6H/BC7) */
        case GL_COMPRESSED_RGBA_BPTC_UNORM_EXT:          return DkImageFormat_RGBA_BC7_Unorm;
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT:    return DkImageFormat_RGBA_BC7_Unorm_sRGB;
        case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT:    return DkImageFormat_RGBA_BC6H_SF16_Float;
        case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT:  return DkImageFormat_RGBA_BC6H_UF16_Float;

        default:
            return (DkImageFormat)0;
    }
}

/* ============================================================================
 * Compressed Format Descriptor Table
 *
 * Single source of truth for block dimensions and byte sizes.
 * Eliminates duplication between block_size and block_bytes functions.
 * ============================================================================ */

typedef struct {
    GLenum format;
    uint8_t blockWidth;
    uint8_t blockHeight;
    uint8_t blockBytes;
} compressed_format_info_t;

static const compressed_format_info_t s_compressed_format_table[] = {
    /* ASTC LDR (all 16 bytes/block, variable block dimensions) */
    { GL_COMPRESSED_RGBA_ASTC_4x4_KHR,   4,  4,  16 },
    { GL_COMPRESSED_RGBA_ASTC_5x4_KHR,   5,  4,  16 },
    { GL_COMPRESSED_RGBA_ASTC_5x5_KHR,   5,  5,  16 },
    { GL_COMPRESSED_RGBA_ASTC_6x5_KHR,   6,  5,  16 },
    { GL_COMPRESSED_RGBA_ASTC_6x6_KHR,   6,  6,  16 },
    { GL_COMPRESSED_RGBA_ASTC_8x5_KHR,   8,  5,  16 },
    { GL_COMPRESSED_RGBA_ASTC_8x6_KHR,   8,  6,  16 },
    { GL_COMPRESSED_RGBA_ASTC_8x8_KHR,   8,  8,  16 },
    { GL_COMPRESSED_RGBA_ASTC_10x5_KHR,  10, 5,  16 },
    { GL_COMPRESSED_RGBA_ASTC_10x6_KHR,  10, 6,  16 },
    { GL_COMPRESSED_RGBA_ASTC_10x8_KHR,  10, 8,  16 },
    { GL_COMPRESSED_RGBA_ASTC_10x10_KHR, 10, 10, 16 },
    { GL_COMPRESSED_RGBA_ASTC_12x10_KHR, 12, 10, 16 },
    { GL_COMPRESSED_RGBA_ASTC_12x12_KHR, 12, 12, 16 },
    /* ASTC sRGB (same dimensions/sizes as LDR) */
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR,   4,  4,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR,   5,  4,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR,   5,  5,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR,   6,  5,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR,   6,  6,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR,   8,  5,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR,   8,  6,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR,   8,  8,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR,  10, 5,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR,  10, 6,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR,  10, 8,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR, 10, 10, 16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR, 12, 10, 16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR, 12, 12, 16 },
    /* ETC2/EAC (all 4x4 blocks) */
    { GL_COMPRESSED_RGB8_ETC2,                     4, 4,  8  },
    { GL_COMPRESSED_SRGB8_ETC2,                    4, 4,  8  },
    { GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, 4, 4,  8  },
    { GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,4, 4,  8  },
    { GL_COMPRESSED_RGBA8_ETC2_EAC,                4, 4,  16 },
    { GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC,         4, 4,  16 },
    { GL_COMPRESSED_R11_EAC,                       4, 4,  8  },
    { GL_COMPRESSED_SIGNED_R11_EAC,                4, 4,  8  },
    { GL_COMPRESSED_RG11_EAC,                      4, 4,  16 },
    { GL_COMPRESSED_SIGNED_RG11_EAC,               4, 4,  16 },
    { GL_ETC1_RGB8_OES,                            4, 4,  8  },
    /* S3TC/DXT (all 4x4 blocks) */
    { GL_COMPRESSED_RGB_S3TC_DXT1_EXT,             4, 4,  8  },
    { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,            4, 4,  8  },
    { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,            4, 4,  16 },
    { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,            4, 4,  16 },
    /* RGTC (BC4/BC5, all 4x4 blocks) */
    { GL_COMPRESSED_RED_RGTC1_EXT,                 4, 4,  8  },
    { GL_COMPRESSED_SIGNED_RED_RGTC1_EXT,          4, 4,  8  },
    { GL_COMPRESSED_RED_GREEN_RGTC2_EXT,           4, 4,  16 },
    { GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT,    4, 4,  16 },
    /* BPTC (BC6H/BC7, all 4x4 blocks, 16 bytes) */
    { GL_COMPRESSED_RGBA_BPTC_UNORM_EXT,           4, 4,  16 },
    { GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT,     4, 4,  16 },
    { GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT,     4, 4,  16 },
    { GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT,   4, 4,  16 },
};
#define NUM_COMPRESSED_FORMATS_TABLE (sizeof(s_compressed_format_table) / sizeof(s_compressed_format_table[0]))

static const compressed_format_info_t *dk_find_compressed_format(GLenum internalformat) {
    for (size_t i = 0; i < NUM_COMPRESSED_FORMATS_TABLE; i++) {
        if (s_compressed_format_table[i].format == internalformat)
            return &s_compressed_format_table[i];
    }
    return NULL;
}

/**
 * Get compressed block dimensions for a given format.
 * Returns block width and height via output parameters.
 * Defaults to 4x4 for unknown formats (matches ETC2/S3TC/BC).
 */
void dk_get_compressed_block_size(GLenum internalformat, int *blockWidth, int *blockHeight) {
    const compressed_format_info_t *info = dk_find_compressed_format(internalformat);
    if (info) {
        *blockWidth = info->blockWidth;
        *blockHeight = info->blockHeight;
    } else {
        *blockWidth = 4;
        *blockHeight = 4;
    }
}

/**
 * Get bytes per block for a given compressed format.
 * Defaults to 16 bytes for unknown formats.
 */
int dk_get_compressed_block_bytes(GLenum internalformat) {
    const compressed_format_info_t *info = dk_find_compressed_format(internalformat);
    return info ? info->blockBytes : 16;
}

/* ============================================================================
 * Vertex Attribute Format Conversion
 * ============================================================================ */

void dk_get_attrib_format(GLenum type, GLint size, GLboolean normalized,
                          DkVtxAttribSize *outSize, DkVtxAttribType *outType) {
    /* Size */
    switch (size) {
        case 1:
            switch (type) {
                case GL_BYTE:           *outSize = DkVtxAttribSize_1x8; break;
                case GL_UNSIGNED_BYTE:  *outSize = DkVtxAttribSize_1x8; break;
                case GL_SHORT:          *outSize = DkVtxAttribSize_1x16; break;
                case GL_UNSIGNED_SHORT: *outSize = DkVtxAttribSize_1x16; break;
                default:                *outSize = DkVtxAttribSize_1x32; break;
            }
            break;
        case 2:
            switch (type) {
                case GL_BYTE:           *outSize = DkVtxAttribSize_2x8; break;
                case GL_UNSIGNED_BYTE:  *outSize = DkVtxAttribSize_2x8; break;
                case GL_SHORT:          *outSize = DkVtxAttribSize_2x16; break;
                case GL_UNSIGNED_SHORT: *outSize = DkVtxAttribSize_2x16; break;
                default:                *outSize = DkVtxAttribSize_2x32; break;
            }
            break;
        case 3:
            switch (type) {
                case GL_BYTE:           *outSize = DkVtxAttribSize_3x8; break;
                case GL_UNSIGNED_BYTE:  *outSize = DkVtxAttribSize_3x8; break;
                case GL_SHORT:          *outSize = DkVtxAttribSize_3x16; break;
                case GL_UNSIGNED_SHORT: *outSize = DkVtxAttribSize_3x16; break;
                default:                *outSize = DkVtxAttribSize_3x32; break;
            }
            break;
        default:
            switch (type) {
                case GL_BYTE:           *outSize = DkVtxAttribSize_4x8; break;
                case GL_UNSIGNED_BYTE:  *outSize = DkVtxAttribSize_4x8; break;
                case GL_SHORT:          *outSize = DkVtxAttribSize_4x16; break;
                case GL_UNSIGNED_SHORT: *outSize = DkVtxAttribSize_4x16; break;
                default:                *outSize = DkVtxAttribSize_4x32; break;
            }
            break;
    }

    /* Type */
    switch (type) {
        case GL_BYTE:
            *outType = normalized ? DkVtxAttribType_Snorm : DkVtxAttribType_Sint;
            break;
        case GL_UNSIGNED_BYTE:
            *outType = normalized ? DkVtxAttribType_Unorm : DkVtxAttribType_Uint;
            break;
        case GL_SHORT:
            *outType = normalized ? DkVtxAttribType_Snorm : DkVtxAttribType_Sint;
            break;
        case GL_UNSIGNED_SHORT:
            *outType = normalized ? DkVtxAttribType_Unorm : DkVtxAttribType_Uint;
            break;
        case GL_FLOAT:
        default:
            *outType = DkVtxAttribType_Float;
            break;
    }
}

/* ============================================================================
 * Type Size Helper
 * ============================================================================ */

GLsizei dk_get_type_size(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_FIXED:
            return 4;
        default:
            return 4;
    }
}
