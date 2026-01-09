#ifndef __khrplatform_h_
#define __khrplatform_h_

/*
** Copyright (c) 2008-2018 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
*/

/* Khronos platform-specific types and definitions.
 *
 * The master copy of khrplatform.h is maintained in the Khronos EGL
 * Registry repository at https://github.com/KhronosGroup/EGL-Registry
 *
 * Adapted for Nintendo Switch homebrew.
 */

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APICALL
 *-------------------------------------------------------------------------
 * This precedes the return type of the function in the function prototype.
 */
#if defined(__SWITCH__)
#   define KHRONOS_APICALL
#elif defined(_WIN32)
#   define KHRONOS_APICALL __declspec(dllimport)
#elif defined(__GNUC__)
#   define KHRONOS_APICALL __attribute__((visibility("default")))
#else
#   define KHRONOS_APICALL
#endif

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APIENTRY
 *-------------------------------------------------------------------------
 * This follows the return type of the function and precedes the function
 * name in the function prototype.
 */
#if defined(_WIN32) && !defined(__SWITCH__)
#   define KHRONOS_APIENTRY __stdcall
#else
#   define KHRONOS_APIENTRY
#endif

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APIATTRIBUTES
 *-------------------------------------------------------------------------
 * This follows the closing parenthesis of the function prototype arguments.
 */
#define KHRONOS_APIATTRIBUTES

/*-------------------------------------------------------------------------
 * Basic type definitions
 *-------------------------------------------------------------------------
 * Khronos uses these types for defining entry point signatures.
 */

#include <stdint.h>
#include <stddef.h>

/*
 * Types that are (so far) the same on all platforms
 */
typedef          float         khronos_float_t;
typedef signed   char          khronos_int8_t;
typedef unsigned char          khronos_uint8_t;
typedef signed   short         khronos_int16_t;
typedef unsigned short         khronos_uint16_t;

/*
 * Types that differ between LLP64 and LP64 architectures
 * - In LLP64 (Windows 64-bit), long is 32-bit, pointer is 64-bit
 * - In LP64 (Linux/macOS 64-bit, Switch), long is 64-bit, pointer is 64-bit
 */
#if defined(__LP64__) || defined(__aarch64__) || defined(__SWITCH__)
typedef signed   int           khronos_int32_t;
typedef unsigned int           khronos_uint32_t;
typedef signed   long          khronos_int64_t;
typedef unsigned long          khronos_uint64_t;
#elif defined(_WIN64)
typedef signed   int           khronos_int32_t;
typedef unsigned int           khronos_uint32_t;
typedef signed   long long     khronos_int64_t;
typedef unsigned long long     khronos_uint64_t;
#else
typedef signed   long          khronos_int32_t;
typedef unsigned long          khronos_uint32_t;
typedef signed   long long     khronos_int64_t;
typedef unsigned long long     khronos_uint64_t;
#endif

/*
 * Types that are (or may be) signed or unsigned depending on platform
 */
typedef khronos_int64_t        khronos_intptr_t;
typedef khronos_uint64_t       khronos_uintptr_t;
typedef khronos_int64_t        khronos_ssize_t;
typedef khronos_uint64_t       khronos_usize_t;

/*
 * Maximum value definitions
 */
#define KHRONOS_MAX_ENUM  0x7FFFFFFF

/*
 * Time types
 *
 * These types can be used to represent a time interval in nanoseconds or
 * an absolute Unadjusted System Time. Unadjusted System Time is the number
 * of nanoseconds since some arbitrary system event (e.g. since the last
 * time the system booted). The Unadjusted System Time is an unsigned
 * 64 bit value that wraps back to 0 every 584 years. Time intervals
 * may be either signed or unsigned.
 */
typedef khronos_uint64_t       khronos_utime_nanoseconds_t;
typedef khronos_int64_t        khronos_stime_nanoseconds_t;

/*
 * Dummy value used to pad enum to 32 bits.
 */
#ifndef KHRONOS_SUPPORT_INT64
#define KHRONOS_SUPPORT_INT64 1
#endif

#ifndef KHRONOS_SUPPORT_FLOAT
#define KHRONOS_SUPPORT_FLOAT 1
#endif

/*
 * Boolean type
 */
typedef enum {
    KHRONOS_FALSE = 0,
    KHRONOS_TRUE  = 1,
    KHRONOS_BOOLEAN_ENUM_FORCE_SIZE = KHRONOS_MAX_ENUM
} khronos_boolean_enum_t;

#endif /* __khrplatform_h_ */
