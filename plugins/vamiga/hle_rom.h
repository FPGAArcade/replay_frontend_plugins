///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLE (High-Level Emulation) ROM for vAmiga
//
// Provides a minimal native C "ROM" that intercepts 68k execution in ROM address space
// and handles Amiga boot sequence + exec.library functions directly in C.
// Only activates when no real Kickstart ROM is found.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    // CPU registers (read/write by dispatch handlers)
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;

    // Direct memory buffer access (big-endian byte order)
    uint8_t* chip_ram;
    uint32_t chip_ram_size;
    uint8_t* fast_ram;
    uint32_t fast_ram_size;
    uint32_t fast_ram_base;  // Amiga address where fast RAM starts (e.g. 0x200000)
    uint8_t* slow_ram;
    uint32_t slow_ram_size;
    uint8_t* rom;
    uint32_t rom_size;

    // Side-effect memory access (for custom chip/CIA writes)
    void (*poke8)(void* ctx, uint32_t addr, uint8_t val);
    void (*poke16)(void* ctx, uint32_t addr, uint16_t val);
    uint8_t (*peek8)(void* ctx, uint32_t addr);
    uint16_t (*peek16)(void* ctx, uint32_t addr);
    void* mem_ctx;

    // TLSF allocator pools (chip RAM for DMA-accessible, fast RAM for everything else)
    void* chip_tlsf;
    void* fast_tlsf;
} HleAmigaState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize ROM buffer contents (reset vectors, LVO stubs)

void hle_rom_init(HleAmigaState* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dispatch handler - called when PC is in ROM space
// Returns true if handled

bool hle_rom_dispatch(HleAmigaState* state, uint32_t pc);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
