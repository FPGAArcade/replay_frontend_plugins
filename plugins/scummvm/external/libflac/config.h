/* Minimal config.h for libFLAC decoder-only build */

#ifndef FLAC_CONFIG_H
#define FLAC_CONFIG_H

/* Target processor is little endian (most common) */
#define CPU_IS_BIG_ENDIAN 0
#define WORDS_BIGENDIAN 0

/* Set FLAC__BYTES_PER_WORD to 8 on 64-bit systems */
#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
#define ENABLE_64_BIT_WORDS 1
#else
#define ENABLE_64_BIT_WORDS 0
#endif

/* No OGG support */
#define FLAC__HAS_OGG 0

/* Standard headers */
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_LROUND 1

/* Platform-specific */
#if defined(__linux__)
#define FLAC__SYS_LINUX 1
#define HAVE_FSEEKO 1
#define HAVE_UNISTD_H 1
#define HAVE_BYTESWAP_H 1
#endif

#if defined(__APPLE__)
#define FLAC__SYS_DARWIN 1
#define HAVE_FSEEKO 1
#define HAVE_UNISTD_H 1
#endif

#if defined(_WIN32)
#define HAVE_FSEEKO 0
#endif

/* Builtin bswap intrinsics */
#if defined(__GNUC__) || defined(__clang__)
#define HAVE_BSWAP16 1
#define HAVE_BSWAP32 1
#endif

/* x86 SIMD support */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define FLAC__HAS_X86INTRIN 1
#define HAVE_CPUID_H 1
#if defined(__SSE2__) || defined(_M_X64)
/* SSE2 is baseline for x86_64 */
#endif
#endif

/* ARM NEON support */
#if defined(__aarch64__) || defined(_M_ARM64)
#define FLAC__CPU_ARM64 1
#define FLAC__HAS_NEONINTRIN 1
#define FLAC__HAS_A64NEONINTRIN 1
#endif

/* Large file support */
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE 1

/* Version */
#define PACKAGE_VERSION "1.4.3"

/* typeof support */
#if defined(__GNUC__) || defined(__clang__)
#define HAVE_TYPEOF 1
#endif

#endif /* FLAC_CONFIG_H */
