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
        /* ASTC formats (most common on mobile/Switch) */
        case 0x93B0: return DkImageFormat_RGBA_ASTC_4x4;    /* GL_COMPRESSED_RGBA_ASTC_4x4_KHR */
        case 0x93B1: return DkImageFormat_RGBA_ASTC_5x4;    /* GL_COMPRESSED_RGBA_ASTC_5x4_KHR */
        case 0x93B2: return DkImageFormat_RGBA_ASTC_5x5;    /* GL_COMPRESSED_RGBA_ASTC_5x5_KHR */
        case 0x93B3: return DkImageFormat_RGBA_ASTC_6x5;    /* GL_COMPRESSED_RGBA_ASTC_6x5_KHR */
        case 0x93B4: return DkImageFormat_RGBA_ASTC_6x6;    /* GL_COMPRESSED_RGBA_ASTC_6x6_KHR */
        case 0x93B5: return DkImageFormat_RGBA_ASTC_8x5;    /* GL_COMPRESSED_RGBA_ASTC_8x5_KHR */
        case 0x93B6: return DkImageFormat_RGBA_ASTC_8x6;    /* GL_COMPRESSED_RGBA_ASTC_8x6_KHR */
        case 0x93B7: return DkImageFormat_RGBA_ASTC_8x8;    /* GL_COMPRESSED_RGBA_ASTC_8x8_KHR */
        case 0x93B8: return DkImageFormat_RGBA_ASTC_10x5;   /* GL_COMPRESSED_RGBA_ASTC_10x5_KHR */
        case 0x93B9: return DkImageFormat_RGBA_ASTC_10x6;   /* GL_COMPRESSED_RGBA_ASTC_10x6_KHR */
        case 0x93BA: return DkImageFormat_RGBA_ASTC_10x8;   /* GL_COMPRESSED_RGBA_ASTC_10x8_KHR */
        case 0x93BB: return DkImageFormat_RGBA_ASTC_10x10;  /* GL_COMPRESSED_RGBA_ASTC_10x10_KHR */
        case 0x93BC: return DkImageFormat_RGBA_ASTC_12x10;  /* GL_COMPRESSED_RGBA_ASTC_12x10_KHR */
        case 0x93BD: return DkImageFormat_RGBA_ASTC_12x12;  /* GL_COMPRESSED_RGBA_ASTC_12x12_KHR */

        /* ASTC sRGB formats */
        case 0x93D0: return DkImageFormat_RGBA_ASTC_4x4_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR */
        case 0x93D1: return DkImageFormat_RGBA_ASTC_5x4_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR */
        case 0x93D2: return DkImageFormat_RGBA_ASTC_5x5_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR */
        case 0x93D3: return DkImageFormat_RGBA_ASTC_6x5_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR */
        case 0x93D4: return DkImageFormat_RGBA_ASTC_6x6_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR */
        case 0x93D5: return DkImageFormat_RGBA_ASTC_8x5_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR */
        case 0x93D6: return DkImageFormat_RGBA_ASTC_8x6_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR */
        case 0x93D7: return DkImageFormat_RGBA_ASTC_8x8_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR */
        case 0x93D8: return DkImageFormat_RGBA_ASTC_10x5_sRGB;  /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR */
        case 0x93D9: return DkImageFormat_RGBA_ASTC_10x6_sRGB;  /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR */
        case 0x93DA: return DkImageFormat_RGBA_ASTC_10x8_sRGB;  /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR */
        case 0x93DB: return DkImageFormat_RGBA_ASTC_10x10_sRGB; /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR */
        case 0x93DC: return DkImageFormat_RGBA_ASTC_12x10_sRGB; /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR */
        case 0x93DD: return DkImageFormat_RGBA_ASTC_12x12_sRGB; /* GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR */

        /* ETC2 formats */
        case 0x9274: return DkImageFormat_RGB_ETC2;         /* GL_COMPRESSED_RGB8_ETC2 */
        case 0x9278: return DkImageFormat_RGBA_ETC2;        /* GL_COMPRESSED_RGBA8_ETC2_EAC */
        case 0x9275: return DkImageFormat_RGB_ETC2_sRGB;    /* GL_COMPRESSED_SRGB8_ETC2 */
        case 0x9279: return DkImageFormat_RGBA_ETC2_sRGB;   /* GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC */
        case 0x9276: return DkImageFormat_RGB_PTA_ETC2;     /* GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 */
        case 0x9277: return DkImageFormat_RGB_PTA_ETC2_sRGB;/* GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 */
        case 0x9270: return DkImageFormat_R_ETC2_Unorm;     /* GL_COMPRESSED_R11_EAC */
        case 0x9271: return DkImageFormat_R_ETC2_Snorm;     /* GL_COMPRESSED_SIGNED_R11_EAC */
        case 0x9272: return DkImageFormat_RG_ETC2_Unorm;    /* GL_COMPRESSED_RG11_EAC */
        case 0x9273: return DkImageFormat_RG_ETC2_Snorm;    /* GL_COMPRESSED_SIGNED_RG11_EAC */

        /* ETC1 (legacy, maps to ETC2 RGB which is compatible) */
        case 0x8D64: return DkImageFormat_RGB_ETC2;         /* GL_ETC1_RGB8_OES */

        /* S3TC/BC formats (Desktop, also supported by Tegra) */
        case 0x83F0: return DkImageFormat_RGB_BC1;          /* GL_COMPRESSED_RGB_S3TC_DXT1_EXT */
        case 0x83F1: return DkImageFormat_RGBA_BC1;         /* GL_COMPRESSED_RGBA_S3TC_DXT1_EXT */
        case 0x83F2: return DkImageFormat_RGBA_BC2;         /* GL_COMPRESSED_RGBA_S3TC_DXT3_EXT */
        case 0x83F3: return DkImageFormat_RGBA_BC3;         /* GL_COMPRESSED_RGBA_S3TC_DXT5_EXT */

        /* BC4/BC5 (RGTC) */
        case 0x8DBB: return DkImageFormat_R_BC4_Unorm;      /* GL_COMPRESSED_RED_RGTC1 */
        case 0x8DBC: return DkImageFormat_R_BC4_Snorm;      /* GL_COMPRESSED_SIGNED_RED_RGTC1 */
        case 0x8DBD: return DkImageFormat_RG_BC5_Unorm;     /* GL_COMPRESSED_RG_RGTC2 */
        case 0x8DBE: return DkImageFormat_RG_BC5_Snorm;     /* GL_COMPRESSED_SIGNED_RG_RGTC2 */

        /* BC6H/BC7 (BPTC) */
        case 0x8E8C: return DkImageFormat_RGBA_BC7_Unorm;       /* GL_COMPRESSED_RGBA_BPTC_UNORM */
        case 0x8E8D: return DkImageFormat_RGBA_BC7_Unorm_sRGB;  /* GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM */
        case 0x8E8E: return DkImageFormat_RGBA_BC6H_SF16_Float; /* GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT */
        case 0x8E8F: return DkImageFormat_RGBA_BC6H_UF16_Float; /* GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT */

        default:
            return (DkImageFormat)0;  /* Unsupported format */
    }
}

/**
 * Get compressed block dimensions for a given format.
 * Returns block width and height via output parameters.
 */
void dk_get_compressed_block_size(GLenum internalformat, int *blockWidth, int *blockHeight) {
    switch (internalformat) {
        /* ASTC formats - block size is encoded in the format name */
        case 0x93B0: case 0x93D0: *blockWidth = 4;  *blockHeight = 4;  break;
        case 0x93B1: case 0x93D1: *blockWidth = 5;  *blockHeight = 4;  break;
        case 0x93B2: case 0x93D2: *blockWidth = 5;  *blockHeight = 5;  break;
        case 0x93B3: case 0x93D3: *blockWidth = 6;  *blockHeight = 5;  break;
        case 0x93B4: case 0x93D4: *blockWidth = 6;  *blockHeight = 6;  break;
        case 0x93B5: case 0x93D5: *blockWidth = 8;  *blockHeight = 5;  break;
        case 0x93B6: case 0x93D6: *blockWidth = 8;  *blockHeight = 6;  break;
        case 0x93B7: case 0x93D7: *blockWidth = 8;  *blockHeight = 8;  break;
        case 0x93B8: case 0x93D8: *blockWidth = 10; *blockHeight = 5;  break;
        case 0x93B9: case 0x93D9: *blockWidth = 10; *blockHeight = 6;  break;
        case 0x93BA: case 0x93DA: *blockWidth = 10; *blockHeight = 8;  break;
        case 0x93BB: case 0x93DB: *blockWidth = 10; *blockHeight = 10; break;
        case 0x93BC: case 0x93DC: *blockWidth = 12; *blockHeight = 10; break;
        case 0x93BD: case 0x93DD: *blockWidth = 12; *blockHeight = 12; break;

        /* ETC2/EAC and S3TC/BC - all use 4x4 blocks */
        default:
            *blockWidth = 4;
            *blockHeight = 4;
            break;
    }
}

/**
 * Get bytes per block for a given compressed format.
 */
int dk_get_compressed_block_bytes(GLenum internalformat) {
    switch (internalformat) {
        /* ASTC - all block sizes use 16 bytes per block */
        case 0x93B0: case 0x93B1: case 0x93B2: case 0x93B3:
        case 0x93B4: case 0x93B5: case 0x93B6: case 0x93B7:
        case 0x93B8: case 0x93B9: case 0x93BA: case 0x93BB:
        case 0x93BC: case 0x93BD:
        case 0x93D0: case 0x93D1: case 0x93D2: case 0x93D3:
        case 0x93D4: case 0x93D5: case 0x93D6: case 0x93D7:
        case 0x93D8: case 0x93D9: case 0x93DA: case 0x93DB:
        case 0x93DC: case 0x93DD:
            return 16;

        /* ETC2 RGBA and RG11 - 16 bytes */
        case 0x9278: case 0x9279:  /* RGBA8_ETC2 */
        case 0x9272: case 0x9273:  /* RG11_EAC */
            return 16;

        /* ETC2 RGB, R11, and punch-through alpha - 8 bytes */
        case 0x9274: case 0x9275:  /* RGB8_ETC2 */
        case 0x9276: case 0x9277:  /* RGB8_PTA_ETC2 */
        case 0x9270: case 0x9271:  /* R11_EAC */
        case 0x8D64:               /* ETC1 */
            return 8;

        /* S3TC/BC1 (DXT1) - 8 bytes */
        case 0x83F0: case 0x83F1:
            return 8;

        /* S3TC/BC2-3 (DXT3/5) - 16 bytes */
        case 0x83F2: case 0x83F3:
            return 16;

        /* BC4 - 8 bytes */
        case 0x8DBB: case 0x8DBC:
            return 8;

        /* BC5, BC6H, BC7 - 16 bytes */
        case 0x8DBD: case 0x8DBE:
        case 0x8E8C: case 0x8E8D: case 0x8E8E: case 0x8E8F:
            return 16;

        default:
            return 16;  /* Default to 16 bytes */
    }
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
