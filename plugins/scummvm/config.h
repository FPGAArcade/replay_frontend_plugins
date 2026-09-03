// ScummVM config.h for Replay Frontend
// Minimal configuration - most defines are passed via CMake

#ifndef CONFIG_H
#define CONFIG_H

// Endianness - x86_64 and ARM64 are little endian
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define SCUMM_LITTLE_ENDIAN
#else
#error "Unsupported architecture - please add endianness detection"
#endif

// Alignment requirement for safety
#define SCUMM_NEED_ALIGNMENT

#endif // CONFIG_H
