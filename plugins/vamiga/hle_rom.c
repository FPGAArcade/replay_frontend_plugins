///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLE (High-Level Emulation) ROM for vAmiga
//
// Minimal native C implementation that handles Amiga boot sequence and exec.library
// functions. Intercepts 68k execution when PC is in ROM address space (0xF80000+).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "hle_rom.h"
#include "tlsf.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ROM address map (absolute addresses, ROM base = 0xF80000)

#define ROM_BASE 0xF80000
#define ROM_SIZE_512K (512 * 1024)

// Entry points
#define HLE_BOOT 0xF80400
#define HLE_WAIT_DISK 0xF80404
#define HLE_VBLANK 0xF80408

// LVO dispatch addresses (exec.library function stubs)
#define HLE_LVO_BASE 0xF81000
#define HLE_ALLOC_MEM 0xF81000
#define HLE_FREE_MEM 0xF81004
#define HLE_OPEN_LIBRARY 0xF81008
#define HLE_CLOSE_LIBRARY 0xF8100C
#define HLE_DISABLE 0xF81010
#define HLE_ENABLE 0xF81014
#define HLE_FORBID 0xF81018
#define HLE_PERMIT 0xF8101C
#define HLE_FIND_RESIDENT 0xF81020
#define HLE_TYPE_OF_MEM 0xF81024
#define HLE_WAIT_PORT 0xF81028
#define HLE_GET_MSG 0xF8102C
#define HLE_REPLY_MSG 0xF81030
#define HLE_ADD_PORT 0xF81034
#define HLE_REM_PORT 0xF81038
#define HLE_PUT_MSG 0xF8103C
#define HLE_DO_IO 0xF81040
#define HLE_SEND_IO 0xF81044
#define HLE_CHECK_IO 0xF81048
#define HLE_WAIT_IO 0xF8104C
#define HLE_ABORT_IO 0xF81050
#define HLE_ADD_TASK 0xF81054
#define HLE_REM_TASK 0xF81058
#define HLE_FIND_TASK 0xF8105C
#define HLE_SET_SIGNAL 0xF81060
#define HLE_SIGNAL 0xF81064
#define HLE_WAIT 0xF81068
#define HLE_ALLOC_SIGNAL 0xF8106C
#define HLE_FREE_SIGNAL 0xF81070
#define HLE_SUPER_STATE 0xF81074
#define HLE_USER_STATE 0xF81078
#define HLE_SET_INT_VECTOR 0xF8107C
#define HLE_OLD_OPEN_LIBRARY 0xF81080
#define HLE_CACHE_CLEAR_U 0xF81084

// Amiga custom chip registers
#define CUSTOM_BASE 0xDFF000
#define DMACONR 0xDFF002
#define INTREQR 0xDFF01E
#define INTENA 0xDFF09A
#define INTREQ 0xDFF09C
#define DMACON 0xDFF096
#define DSKPT 0xDFF020
#define DSKLEN 0xDFF024
#define DSKSYNC 0xDFF07E
#define ADKCON 0xDFF09E

// CIA registers
#define CIAA_PRA 0xBFE001
#define CIAB_PRB 0xBFD100

// Interrupt bits
#define INTF_SETCLR 0x8000
#define INTF_INTEN 0x4000
#define INTF_VERTB 0x0020
#define INTF_DSKBLK 0x0002

// DMACON bits
#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define DMAF_DISK 0x0010

// Memory type flags
#define MEMF_PUBLIC 0x0001
#define MEMF_CHIP 0x0002
#define MEMF_FAST 0x0004
#define MEMF_CLEAR 0x10000

// Slow RAM base address (fixed on all Amigas)
#define SLOW_RAM_BASE 0xC00000

// Memory layout within chip RAM
#define EXEC_BASE_ADDR 0x2000
#define TLSF_POOL_START 0x10000
#define STACK_RESERVE 0x2000

// Dummy task structure address (returned by FindTask)
#define DUMMY_TASK_ADDR 0x3000

// ExecBase LVO offsets (negative offsets from ExecBase)
#define LVO_ALLOC_MEM -198
#define LVO_FREE_MEM -210
#define LVO_OPEN_LIBRARY -552
#define LVO_CLOSE_LIBRARY -414
#define LVO_DISABLE -120
#define LVO_ENABLE -126
#define LVO_FORBID -132
#define LVO_PERMIT -138
#define LVO_FIND_RESIDENT -96
#define LVO_TYPE_OF_MEM -534
#define LVO_WAIT_PORT -384
#define LVO_GET_MSG -372
#define LVO_REPLY_MSG -378
#define LVO_ADD_PORT -354
#define LVO_REM_PORT -360
#define LVO_PUT_MSG -366
#define LVO_DO_IO -456
#define LVO_SEND_IO -462
#define LVO_CHECK_IO -468
#define LVO_WAIT_IO -474
#define LVO_ABORT_IO -480
#define LVO_ADD_TASK -282
#define LVO_REM_TASK -288
#define LVO_FIND_TASK -294
#define LVO_SET_SIGNAL -300
#define LVO_SIGNAL -324
#define LVO_WAIT -318
#define LVO_ALLOC_SIGNAL -330
#define LVO_FREE_SIGNAL -336
#define LVO_SUPER_STATE -150
#define LVO_USER_STATE -156
#define LVO_SET_INT_VECTOR -162
#define LVO_OLD_OPEN_LIBRARY -408
#define LVO_CACHE_CLEAR_U -636

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Big-endian memory helpers for direct ROM/chip RAM buffer access

static void write16(uint8_t* buf, uint32_t offset, uint16_t val) {
    buf[offset] = (uint8_t)(val >> 8);
    buf[offset + 1] = (uint8_t)(val & 0xFF);
}

static void write32(uint8_t* buf, uint32_t offset, uint32_t val) {
    buf[offset] = (uint8_t)(val >> 24);
    buf[offset + 1] = (uint8_t)(val >> 16);
    buf[offset + 2] = (uint8_t)(val >> 8);
    buf[offset + 3] = (uint8_t)(val & 0xFF);
}

static uint16_t read16_buf(const uint8_t* buf, uint32_t offset) {
    return (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
}

static uint32_t read32_buf(const uint8_t* buf, uint32_t offset) {
    return ((uint32_t)buf[offset] << 24) | ((uint32_t)buf[offset + 1] << 16) |
           ((uint32_t)buf[offset + 2] << 8) | (uint32_t)buf[offset + 3];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stack helpers

static void hle_rts(HleAmigaState* state) {
    // Pop return address from stack
    uint32_t sp = state->a[7];
    state->pc = read32_buf(state->chip_ram, sp & (state->chip_ram_size - 1));
    state->a[7] = sp + 4;
}

static void hle_rte(HleAmigaState* state) {
    // Pop SR then PC from stack (for interrupt returns)
    uint32_t sp = state->a[7];
    state->sr = read16_buf(state->chip_ram, sp & (state->chip_ram_size - 1));
    state->pc = read32_buf(state->chip_ram, (sp + 2) & (state->chip_ram_size - 1));
    state->a[7] = sp + 6;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write an LVO stub: a JMP at ExecBase + LVO offset pointing to our HLE handler

static void write_lvo_stub(uint8_t* chip_ram, uint32_t chip_ram_size, uint32_t exec_base,
                           int16_t lvo_offset, uint32_t handler_addr) {
    // LVO entries are at ExecBase + negative_offset
    // We write a JMP instruction there (6 bytes: 4EF9 + 32-bit address)
    uint32_t addr = (uint32_t)(exec_base + lvo_offset);
    uint32_t masked = addr & (chip_ram_size - 1);

    // Ensure we have room for the 6-byte JMP instruction
    if (masked + 5 < chip_ram_size) {
        write16(chip_ram, masked, 0x4EF9);
        write32(chip_ram, masked + 2, handler_addr);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write exception vectors to chip RAM (needed after OVL clear maps RAM at 0x000000)

static void install_exception_vectors(HleAmigaState* state) {
    uint8_t* ram = state->chip_ram;

    // Level 2 autovector (offset 0x68) - unused, point to RTE stub
    // Level 3 autovector (offset 0x6C) -> VBlank handler
    write32(ram, 0x006C, HLE_VBLANK);

    // Level 6 autovector (offset 0x78) - unused, point to a safe RTE
    // Trap vectors (0x80-0xBF) - leave as 0 for now

    // Bus error / Address error -> point to boot entry to restart (crude but functional)
    write32(ram, 0x0008, HLE_BOOT); // Bus error
    write32(ram, 0x000C, HLE_BOOT); // Address error
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize ROM buffer contents

void hle_rom_init(HleAmigaState* state) {
    uint8_t* rom = state->rom;
    uint32_t rom_size = state->rom_size;

    // Clear ROM buffer
    memset(rom, 0, rom_size);

    // Reset vectors at offset 0 (mapped to 0xF80000 by the Amiga memory map)
    // At reset, OVL maps ROM to 0x000000, so the 68000 reads these as the reset vectors.
    // Initial SSP (Supervisor Stack Pointer) - top of chip RAM, long-word aligned
    write32(rom, 0x0000, state->chip_ram_size - 4);
    // Initial PC - our boot entry point
    write32(rom, 0x0004, HLE_BOOT);

    // Exception vectors in ROM (used while OVL is still set, before chip RAM takes over)
    write32(rom, 0x006C, HLE_VBLANK);

    // Write NOP slides at entry points (so prefetch has valid instructions)
    uint32_t entry_points[] = {HLE_BOOT, HLE_WAIT_DISK, HLE_VBLANK};
    for (int i = 0; i < 3; i++) {
        uint32_t offset = entry_points[i] - ROM_BASE;
        if (offset + 3 < rom_size) {
            write16(rom, offset, 0x4E71);     // NOP
            write16(rom, offset + 2, 0x4E71); // NOP
        }
    }

    // Write NOP padding at all LVO handler addresses
    for (uint32_t addr = HLE_LVO_BASE; addr <= HLE_CACHE_CLEAR_U; addr += 4) {
        uint32_t offset = addr - ROM_BASE;
        if (offset + 3 < rom_size) {
            write16(rom, offset, 0x4E71);     // NOP
            write16(rom, offset + 2, 0x4E71); // NOP
        }
    }

    // Set up ExecBase pointer at absolute address 4 in chip RAM
    write32(state->chip_ram, 0x0004, EXEC_BASE_ADDR);

    // Install exception vectors in chip RAM (for after OVL clear)
    install_exception_vectors(state);

    // Write LVO jump stubs at ExecBase + LVO_offset
    // ExecBase is at EXEC_BASE_ADDR, so stubs start at EXEC_BASE_ADDR - 636 = well above boot block
    uint32_t eb = EXEC_BASE_ADDR;
    uint8_t* ram = state->chip_ram;
    uint32_t ram_size = state->chip_ram_size;
    write_lvo_stub(ram, ram_size, eb, LVO_ALLOC_MEM, HLE_ALLOC_MEM);
    write_lvo_stub(ram, ram_size, eb, LVO_FREE_MEM, HLE_FREE_MEM);
    write_lvo_stub(ram, ram_size, eb, LVO_OPEN_LIBRARY, HLE_OPEN_LIBRARY);
    write_lvo_stub(ram, ram_size, eb, LVO_CLOSE_LIBRARY, HLE_CLOSE_LIBRARY);
    write_lvo_stub(ram, ram_size, eb, LVO_DISABLE, HLE_DISABLE);
    write_lvo_stub(ram, ram_size, eb, LVO_ENABLE, HLE_ENABLE);
    write_lvo_stub(ram, ram_size, eb, LVO_FORBID, HLE_FORBID);
    write_lvo_stub(ram, ram_size, eb, LVO_PERMIT, HLE_PERMIT);
    write_lvo_stub(ram, ram_size, eb, LVO_FIND_RESIDENT, HLE_FIND_RESIDENT);
    write_lvo_stub(ram, ram_size, eb, LVO_TYPE_OF_MEM, HLE_TYPE_OF_MEM);
    write_lvo_stub(ram, ram_size, eb, LVO_WAIT_PORT, HLE_WAIT_PORT);
    write_lvo_stub(ram, ram_size, eb, LVO_GET_MSG, HLE_GET_MSG);
    write_lvo_stub(ram, ram_size, eb, LVO_REPLY_MSG, HLE_REPLY_MSG);
    write_lvo_stub(ram, ram_size, eb, LVO_ADD_PORT, HLE_ADD_PORT);
    write_lvo_stub(ram, ram_size, eb, LVO_REM_PORT, HLE_REM_PORT);
    write_lvo_stub(ram, ram_size, eb, LVO_PUT_MSG, HLE_PUT_MSG);
    write_lvo_stub(ram, ram_size, eb, LVO_DO_IO, HLE_DO_IO);
    write_lvo_stub(ram, ram_size, eb, LVO_SEND_IO, HLE_SEND_IO);
    write_lvo_stub(ram, ram_size, eb, LVO_CHECK_IO, HLE_CHECK_IO);
    write_lvo_stub(ram, ram_size, eb, LVO_WAIT_IO, HLE_WAIT_IO);
    write_lvo_stub(ram, ram_size, eb, LVO_ABORT_IO, HLE_ABORT_IO);
    write_lvo_stub(ram, ram_size, eb, LVO_ADD_TASK, HLE_ADD_TASK);
    write_lvo_stub(ram, ram_size, eb, LVO_REM_TASK, HLE_REM_TASK);
    write_lvo_stub(ram, ram_size, eb, LVO_FIND_TASK, HLE_FIND_TASK);
    write_lvo_stub(ram, ram_size, eb, LVO_SET_SIGNAL, HLE_SET_SIGNAL);
    write_lvo_stub(ram, ram_size, eb, LVO_SIGNAL, HLE_SIGNAL);
    write_lvo_stub(ram, ram_size, eb, LVO_WAIT, HLE_WAIT);
    write_lvo_stub(ram, ram_size, eb, LVO_ALLOC_SIGNAL, HLE_ALLOC_SIGNAL);
    write_lvo_stub(ram, ram_size, eb, LVO_FREE_SIGNAL, HLE_FREE_SIGNAL);
    write_lvo_stub(ram, ram_size, eb, LVO_SUPER_STATE, HLE_SUPER_STATE);
    write_lvo_stub(ram, ram_size, eb, LVO_USER_STATE, HLE_USER_STATE);
    write_lvo_stub(ram, ram_size, eb, LVO_SET_INT_VECTOR, HLE_SET_INT_VECTOR);
    write_lvo_stub(ram, ram_size, eb, LVO_OLD_OPEN_LIBRARY, HLE_OLD_OPEN_LIBRARY);
    write_lvo_stub(ram, ram_size, eb, LVO_CACHE_CLEAR_U, HLE_CACHE_CLEAR_U);

    // Initialize TLSF allocator for chip RAM
    // Pool occupies chip RAM from TLSF_POOL_START to (chip_ram_size - STACK_RESERVE)
    uint32_t chip_pool_size = ram_size - TLSF_POOL_START - STACK_RESERVE;
    state->chip_tlsf = tlsf_create_with_pool(ram + TLSF_POOL_START, chip_pool_size);

    // Initialize TLSF allocator for fast RAM (if available)
    if (state->fast_ram && state->fast_ram_size > 0) {
        state->fast_tlsf = tlsf_create_with_pool(state->fast_ram, state->fast_ram_size);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HLE dispatch - called when PC is in ROM address space

bool hle_rom_dispatch(HleAmigaState* state, uint32_t pc) {
    switch (pc) {

    //-----------------------------------------------------------------------------------------------------------------
    // Boot sequence: initialize custom chips, start disk DMA
    case HLE_BOOT: {
        // Disable all interrupts
        state->poke16(state->mem_ctx, INTENA, 0x7FFF);
        // Clear all interrupt requests
        state->poke16(state->mem_ctx, INTREQ, 0x7FFF);
        // Disable all DMA
        state->poke16(state->mem_ctx, DMACON, 0x7FFF);

        // Clear OVL (overlay) bit - CIA-A PRA bit 0
        // This maps chip RAM at 0x000000 instead of ROM
        uint8_t cia_pra = state->peek8(state->mem_ctx, CIAA_PRA);
        state->poke8(state->mem_ctx, CIAA_PRA, cia_pra & 0xFE);

        // Now that chip RAM is mapped at 0x000000, ensure exception vectors are in place
        install_exception_vectors(state);

        // Select DF0 and start motor - CIA-B PRB
        // Bits: SEL0=0 (select), MTR=0 (motor on), SIDE=0 (lower head)
        state->poke8(state->mem_ctx, CIAB_PRB, 0x67); // ~SEL0 & ~MTR, others high

        // Enable disk DMA + master DMA
        state->poke16(state->mem_ctx, DMACON, DMAF_SETCLR | DMAF_MASTER | DMAF_DISK);

        // Set up disk DMA to read boot block (first 1024 bytes = 2 sectors)
        uint32_t boot_dest = 0x000000;
        state->poke16(state->mem_ctx, DSKPT, (uint16_t)(boot_dest >> 16));        // High word
        state->poke16(state->mem_ctx, DSKPT + 2, (uint16_t)(boot_dest & 0xFFFF)); // Low word

        // Set sync word (standard Amiga MFM sync = 0x4489)
        state->poke16(state->mem_ctx, DSKSYNC, 0x4489);

        // ADKCON: enable WORDSYNC for disk reads
        state->poke16(state->mem_ctx, ADKCON, 0x8400);

        // Enable disk block finished interrupt
        state->poke16(state->mem_ctx, INTENA, INTF_SETCLR | INTF_INTEN | INTF_DSKBLK);

        // Enable VBlank interrupt (level 3)
        state->poke16(state->mem_ctx, INTENA, INTF_SETCLR | INTF_VERTB);

        // Start disk DMA: read 2 sectors (512 words = 1024 bytes)
        // DSKLEN bit 15 = 1 (DMA enable), bit 14 = 0 (read), bits 13-0 = word count
        // Must write twice to start (hardware protection)
        state->poke16(state->mem_ctx, DSKLEN, 0x8200); // Enable + 512 words
        state->poke16(state->mem_ctx, DSKLEN, 0x8200); // Write again to confirm

        // Set A6 to ExecBase (many boot blocks expect this)
        state->a[6] = read32_buf(state->chip_ram, 0x0004);

        // Transition to wait state
        state->pc = HLE_WAIT_DISK;
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // Wait for disk DMA to complete
    case HLE_WAIT_DISK: {
        // Poll INTREQR for disk block done (bit 1)
        uint16_t intreq = state->peek16(state->mem_ctx, INTREQR);
        if (!(intreq & INTF_DSKBLK)) {
            // Not done yet - stay in this state (PC unchanged)
            state->pc = HLE_WAIT_DISK;
            return true;
        }

        // Disk read complete - clear the interrupt
        state->poke16(state->mem_ctx, INTREQ, INTF_DSKBLK);

        // Stop disk DMA
        state->poke16(state->mem_ctx, DSKLEN, 0x4000);

        // Boot block was loaded at 0x000000, which overwrites the ExecBase pointer at 0x0004.
        // Restore it so code reading *(uint32_t*)4 still finds ExecBase.
        write32(state->chip_ram, 0x0004, EXEC_BASE_ADDR);

        // Re-install exception vectors (boot block may have overwritten low memory)
        install_exception_vectors(state);

        // Jump to boot block entry point at offset 0x0C
        // Standard Amiga boot blocks have: offset 0 = "DOS\0", offset 0xC = entry code
        state->pc = 0x0C;
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // VBlank interrupt handler
    case HLE_VBLANK: {
        // Acknowledge VBlank interrupt
        state->poke16(state->mem_ctx, INTREQ, INTF_VERTB);
        // RTE - return from exception
        hle_rte(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: AllocMem(byteSize, requirements)
    // D0 = byteSize, D1 = requirements (MEMF_CHIP, MEMF_CLEAR, etc.)
    // Returns: D0 = address or 0
    case HLE_ALLOC_MEM: {
        uint32_t size = state->d[0];
        uint32_t requirements = state->d[1];

        void* ptr = NULL;
        uint32_t amiga_addr = 0;

        if ((requirements & MEMF_CHIP) || !state->fast_tlsf) {
            // Explicit chip RAM request, or no fast RAM available
            ptr = tlsf_malloc(state->chip_tlsf, size);
            if (ptr) {
                amiga_addr = (uint32_t)((uint8_t*)ptr - state->chip_ram);
            }
        } else {
            // Prefer fast RAM (like real AmigaOS) for MEMF_FAST, MEMF_PUBLIC, or no flags
            ptr = tlsf_malloc(state->fast_tlsf, size);
            if (ptr) {
                amiga_addr = state->fast_ram_base + (uint32_t)((uint8_t*)ptr - state->fast_ram);
            } else {
                // Fall back to chip RAM if fast is exhausted
                ptr = tlsf_malloc(state->chip_tlsf, size);
                if (ptr) {
                    amiga_addr = (uint32_t)((uint8_t*)ptr - state->chip_ram);
                }
            }
        }

        if (ptr) {
            if (requirements & MEMF_CLEAR) {
                memset(ptr, 0, size);
            }
            state->d[0] = amiga_addr;
        } else {
            state->d[0] = 0;
        }
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: FreeMem(memoryBlock, byteSize)
    // A1 = memoryBlock, D0 = byteSize
    case HLE_FREE_MEM: {
        uint32_t addr = state->a[1];

        if (state->fast_tlsf && addr >= state->fast_ram_base &&
            addr < state->fast_ram_base + state->fast_ram_size) {
            // Address is in fast RAM
            void* ptr = state->fast_ram + (addr - state->fast_ram_base);
            tlsf_free(state->fast_tlsf, ptr);
        } else if (addr >= TLSF_POOL_START && addr < state->chip_ram_size - STACK_RESERVE) {
            // Address is in chip RAM pool
            void* ptr = state->chip_ram + addr;
            tlsf_free(state->chip_tlsf, ptr);
        }
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: OpenLibrary(libName, version)
    // A1 = library name, D0 = version
    // Returns: D0 = library base or 0
    case HLE_OPEN_LIBRARY:
    case HLE_OLD_OPEN_LIBRARY: {
        // Return ExecBase for any library request (most boot code just needs a non-zero base)
        state->d[0] = read32_buf(state->chip_ram, 0x0004);
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // No-op stubs (CloseLibrary, Disable, Enable, Forbid, Permit, etc.)
    case HLE_CLOSE_LIBRARY:
    case HLE_DISABLE:
    case HLE_ENABLE:
    case HLE_FORBID:
    case HLE_PERMIT:
    case HLE_ADD_PORT:
    case HLE_REM_PORT:
    case HLE_REPLY_MSG:
    case HLE_ADD_TASK:
    case HLE_REM_TASK:
    case HLE_SET_SIGNAL:
    case HLE_SIGNAL:
    case HLE_FREE_SIGNAL:
    case HLE_USER_STATE:
    case HLE_SET_INT_VECTOR:
    case HLE_CACHE_CLEAR_U: {
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: FindResident(name) - return 0 (not found)
    case HLE_FIND_RESIDENT: {
        state->d[0] = 0;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: TypeOfMem(address) - return memory type flags for address
    // A1 = address, Returns: D0 = memory attributes (0 if not in known RAM)
    case HLE_TYPE_OF_MEM: {
        uint32_t addr = state->a[1];
        if (addr < state->chip_ram_size) {
            state->d[0] = MEMF_CHIP | MEMF_PUBLIC;
        } else if (state->slow_ram_size > 0 && addr >= SLOW_RAM_BASE &&
                   addr < SLOW_RAM_BASE + state->slow_ram_size) {
            state->d[0] = MEMF_CHIP | MEMF_PUBLIC; // Slow RAM is DMA-accessible like chip
        } else if (state->fast_ram_size > 0 && addr >= state->fast_ram_base &&
                   addr < state->fast_ram_base + state->fast_ram_size) {
            state->d[0] = MEMF_FAST | MEMF_PUBLIC;
        } else {
            state->d[0] = 0;
        }
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: WaitPort(port) / Wait(signalSet)
    // These just return immediately - not truly correct but sufficient for boot code
    case HLE_WAIT_PORT:
    case HLE_WAIT: {
        state->d[0] = 0;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: GetMsg(port) - return 0 (no message)
    case HLE_GET_MSG: {
        state->d[0] = 0;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: PutMsg / DoIO / SendIO / CheckIO / WaitIO / AbortIO
    // I/O stubs - return success (0)
    case HLE_PUT_MSG:
    case HLE_DO_IO:
    case HLE_SEND_IO:
    case HLE_WAIT_IO:
    case HLE_ABORT_IO: {
        state->d[0] = 0;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: CheckIO - return non-zero (IO complete)
    case HLE_CHECK_IO: {
        state->d[0] = 1;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: FindTask(name) - return a dummy task pointer
    // A1 = name (NULL = find self)
    // Returns: D0 and A0 = task pointer
    case HLE_FIND_TASK: {
        state->d[0] = DUMMY_TASK_ADDR;
        state->a[0] = DUMMY_TASK_ADDR;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: AllocSignal(signalNum) - return signal number
    case HLE_ALLOC_SIGNAL: {
        // Return a valid signal number (bit 0)
        state->d[0] = 0;
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    // exec.library: SuperState() - return previous stack pointer
    case HLE_SUPER_STATE: {
        state->d[0] = state->a[7];
        hle_rts(state);
        return true;
    }

    //-----------------------------------------------------------------------------------------------------------------
    default:
        break;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
