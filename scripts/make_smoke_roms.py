#!/usr/bin/env python3
"""Writes the smoke fixtures for the systems that have no redistributable homebrew.

  make_smoke_roms.py

Each console ROM is hand-assembled here: it turns the display on and fills the screen with one tile
whose left half is colour 0 and right half colour 1, then spins. That is all a smoke needs, since the
host fails a frame whose pixels all carry the same value. Encodings are in the comments.
"""

import struct
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent


class Asm:
    def __init__(self, origin):
        self.origin = origin
        self.code = bytearray()

    def __call__(self, *data):
        self.code += bytes(data)

    def here(self):
        return self.origin + len(self.code)

    def rel8(self, opcode, target):
        self(opcode, (target - (self.here() + 2)) & 0xFF)


def gba():
    # ARM, entered at 0x08000000 with the BIOS skipped. Mode 3, BG2: the top 80 lines go red.
    rom = bytearray(0x100)
    rom[0:4] = struct.pack("<I", 0xEA00002E)  # b 0xC0, past the header
    rom[0xA0:0xAC] = b"REPLAYSMOKE\0"
    rom[0xB2] = 0x96  # fixed value
    rom[0xBD] = (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF
    body = [
        0xE3A00301,  # mov    r0, #0x04000000
        0xE3A01B01,  # mov    r1, #0x400
        0xE2811003,  # add    r1, r1, #3         DISPCNT: mode 3, BG2 on
        0xE1C010B0,  # strh   r1, [r0]
        0xE3A00406,  # mov    r0, #0x06000000
        0xE3A0101F,  # mov    r1, #0x1F          red
        0xE3A02C4B,  # mov    r2, #0x4B00        240 * 80 pixels
        0xE0C010B2,  # loop:  strh r1, [r0], #2
        0xE2522001,  # subs   r2, r2, #1
        0x1AFFFFFC,  # bne    loop
        0xEAFFFFFE,  # b      .
    ]
    rom[0xC0:] = b"".join(struct.pack("<I", word) for word in body)
    return bytes(rom)


def pce():
    # HuC6280, reset through the vector at 0xFFFE with bank 0 mapped at 0xE000.
    a = Asm(0xE000)
    a(0x78, 0xD8)  # sei; cld
    a(0xA9, 0xFF, 0x53, 0x01)  # lda #$FF; tam #1     I/O page at 0x0000
    a(0x9C, 0x00, 0x04)  # stz $0400                   VCE: 5 MHz dot clock

    def vdc(reg, value):
        a(0x03, reg, 0x13, value & 0xFF, 0x23, value >> 8)  # st0 #reg; st1 #lo; st2 #hi

    vdc(0x05, 0x0000)  # CR: display off
    vdc(0x09, 0x0000)  # MWR: 32x32 map
    vdc(0x0A, 0x0202)  # HSR
    vdc(0x0B, 0x031F)  # HDR: 256 pixels
    vdc(0x0C, 0x0F02)  # VSR
    vdc(0x0D, 0x00EF)  # VDW: 240 lines
    vdc(0x0E, 0x0003)  # VCR
    vdc(0x00, 0x0000)  # MAWR: the map
    a(0x03, 0x02)  # st0 #2                             VWR
    a(0xA2, 0x00, 0xA0, 0x04)  # ldx #0; ldy #4        1024 entries of tile 0x100
    loop = a.here()
    a(0x13, 0x00, 0x23, 0x01)  # st1 #0; st2 #1
    a(0xE8)  # inx
    a.rel8(0xD0, loop)  # bne
    a(0x88)  # dey
    a.rel8(0xD0, loop)  # bne
    vdc(0x00, 0x1000)  # MAWR: tile 0x100
    a(0x03, 0x02, 0xA2, 0x08)  # st0 #2; ldx #8
    loop = a.here()
    a(0x13, 0x0F, 0x23, 0x00)  # st1 #$0F; st2 #0     planes 0-1: right half colour 1
    a(0xCA)  # dex
    a.rel8(0xD0, loop)  # bne
    a(0xA2, 0x08)  # ldx #8
    loop = a.here()
    a(0x13, 0x00, 0x23, 0x00)  # st1 #0; st2 #0       planes 2-3
    a(0xCA)  # dex
    a.rel8(0xD0, loop)  # bne
    a(0x9C, 0x02, 0x04, 0x9C, 0x03, 0x04)  # stz $0402; stz $0403    colour 0
    a(0x9C, 0x04, 0x04, 0x9C, 0x05, 0x04)  # stz $0404; stz $0405    black
    a(0xA9, 0xFF, 0x8D, 0x04, 0x04)  # lda #$FF; sta $0404            colour 1 white
    a(0xA9, 0x01, 0x8D, 0x05, 0x04)  # lda #1; sta $0405
    vdc(0x05, 0x0080)  # CR: background on
    a.rel8(0x80, a.here())  # bra .
    rom = bytearray(0x8000)
    rom[: len(a.code)] = a.code
    rom[0x1FF6:0x2000] = struct.pack("<5H", *[a.origin] * 5)
    return bytes(rom)


def sms():
    # Z80 from 0x0000, VDP mode 4.
    a = Asm(0x0000)
    a(0xF3)  # di
    a(0x0E, 0xBF)  # ld c, $BF                         VDP control

    def out_c(*values):
        for value in values:
            a(0x3E, value, 0xED, 0x79)  # ld a, value; out (c), a

    out_c(0x04, 0x80, 0x80, 0x81, 0xFF, 0x82, 0xFF, 0x85, 0xFB, 0x86, 0x00, 0x87, 0x00, 0x88, 0x00, 0x89)
    out_c(0x00, 0x40)  # VRAM write at 0x0000: tile 0
    a(0x0E, 0xBE, 0x16, 0x08)  # ld c, $BE; ld d, 8
    loop = a.here()
    a(0x3E, 0x0F, 0xED, 0x79, 0xAF)  # ld a, $0F; out (c), a; xor a    right half colour 1
    a(0xED, 0x79, 0xED, 0x79, 0xED, 0x79)  # out (c), a x3
    a(0x15)  # dec d
    a.rel8(0x20, loop)  # jr nz
    a(0x0E, 0xBF)  # ld c, $BF
    out_c(0x00, 0x78)  # VRAM write at 0x3800: the name table
    a(0x0E, 0xBE, 0xAF, 0x16, 0x07)  # ld c, $BE; xor a; ld d, 7       7 * 256 bytes
    outer = a.here()
    a(0x06, 0x00)  # ld b, 0
    inner = a.here()
    a(0xED, 0x79)  # out (c), a
    a.rel8(0x10, inner)  # djnz
    a(0x15)  # dec d
    a.rel8(0x20, outer)  # jr nz
    a(0x0E, 0xBF)  # ld c, $BF
    out_c(0x00, 0xC0)  # CRAM write at 0
    a(0x0E, 0xBE)  # ld c, $BE
    out_c(0x00, 0x3F)  # black, white
    a(0x0E, 0xBF)  # ld c, $BF
    out_c(0xC0, 0x81)  # display on
    a.rel8(0x18, a.here())  # jr .
    rom = bytearray(0x8000)
    rom[: len(a.code)] = a.code
    return bytes(rom)


def snes():
    # 65816 in emulation mode, LoROM, reset at 0x8000. Mode 0, BG1.
    a = Asm(0x8000)

    def sta(addr):
        a(0x8D, addr & 0xFF, addr >> 8)

    def stz(addr):
        a(0x9C, addr & 0xFF, addr >> 8)

    a(0x78)  # sei
    a(0xA9, 0x80)  # lda #$80
    sta(0x2100)  # forced blank
    sta(0x2115)  # VMAIN: step after the high byte
    stz(0x2105)  # BGMODE 0
    stz(0x2107)  # BG1 map at word 0
    a(0xA9, 0x01)  # lda #1
    sta(0x210B)  # BG1 tiles at word 0x1000
    sta(0x212C)  # BG1 on the main screen
    stz(0x2116)
    stz(0x2117)
    a(0xA2, 0x00, 0xA0, 0x04)  # ldx #0; ldy #4       1024 map words
    loop = a.here()
    stz(0x2118)
    stz(0x2119)
    a(0xE8)  # inx
    a.rel8(0xD0, loop)  # bne
    a(0x88)  # dey
    a.rel8(0xD0, loop)  # bne
    a(0xA9, 0x10)  # lda #$10
    sta(0x2117)  # VRAM word 0x1000
    a(0xA2, 0x08, 0xA9, 0x0F)  # ldx #8; lda #$0F
    loop = a.here()
    sta(0x2118)  # right half colour 1
    stz(0x2119)
    a(0xCA)  # dex
    a.rel8(0xD0, loop)  # bne
    stz(0x2121)
    stz(0x2122)
    stz(0x2122)  # colour 0 black
    a(0xA9, 0xFF)  # lda #$FF
    sta(0x2122)
    a(0xA9, 0x7F)  # lda #$7F
    sta(0x2122)  # colour 1 white
    a(0xA9, 0x0F)  # lda #$0F
    sta(0x2100)  # display on
    a.rel8(0x80, a.here())  # bra .
    rom = bytearray(0x8000)
    rom[: len(a.code)] = a.code
    rom[0x7FC0:0x7FD5] = b"REPLAY SMOKE".ljust(21)
    rom[0x7FD5:0x7FDA] = bytes((0x20, 0x00, 0x05, 0x00, 0x01))  # LoROM, ROM only, 32 KB, no RAM, NA
    rom[0x7FDC:0x7FE0] = b"\xff\xff\x00\x00"
    rom[0x7FFC:0x7FFE] = struct.pack("<H", a.origin)  # emulation-mode reset
    checksum = sum(rom) & 0xFFFF
    rom[0x7FDC:0x7FE0] = struct.pack("<HH", checksum ^ 0xFFFF, checksum)
    return bytes(rom)


def ws():
    # V30 (8086), monochrome, entered at FFFF:0000 with the boot ROM skipped.
    a = Asm(0x0000)
    a(0xFA, 0xFC)  # cli; cld
    a(0x31, 0xC0, 0x8E, 0xC0)  # xor ax, ax; mov es, ax
    a(0xBF, 0x00, 0x18, 0xB9, 0x00, 0x04)  # mov di, $1800; mov cx, $400   the map
    a(0xF3, 0xAB)  # rep stosw
    a(0xBF, 0x00, 0x20, 0xB8, 0x0F, 0x00)  # mov di, $2000; mov ax, $000F  tile 0
    a(0xB1, 0x08, 0xF3, 0xAB)  # mov cl, 8; rep stosw                       right half colour 1
    a(0xB0, 0x03, 0xE6, 0x07)  # mov al, 3; out $07, al                     map at 0x1800
    a(0xB0, 0x70, 0xE6, 0x20)  # mov al, $70; out $20, al                   palette 0: shades 0, 7
    a(0xB0, 0x01, 0xE6, 0x00)  # mov al, 1; out $00, al                     screen 1 on
    a.rel8(0xEB, a.here())  # jmp .
    rom = bytearray(0x10000)
    rom[: len(a.code)] = a.code
    rom[0xFFF0:0xFFF5] = bytes((0xEA, 0x00, 0x00, 0x00, 0xF0))  # jmp far F000:0000
    rom[0xFFFE:] = struct.pack("<H", sum(rom[:0xFFFE]) & 0xFFFF)
    return bytes(rom)


BASIC_TOKENS = {"IF": 0x8B, "THEN": 0xA7, "SYS": 0x9E, "POKE": 0x97, "PRINT": 0x99, "CHR$": 0xC7,
                "FOR": 0x81, "TO": 0xA4, "NEXT": 0x82, "LOAD": 0x93, "=": 0xB2}


def basic(lines):
    # Tokenised C64 BASIC at 0x0801. Keywords are tokens everywhere but inside quotes.
    prg = bytearray()
    address = 0x0801
    for number, text in lines.items():
        body = bytearray(number.to_bytes(2, "little"))
        quoted = False
        i = 0
        while i < len(text):
            keyword = None if quoted else next((k for k in BASIC_TOKENS if text.startswith(k, i)), None)
            if keyword:
                body.append(BASIC_TOKENS[keyword])
                i += len(keyword)
            else:
                quoted ^= text[i] == '"'
                body += text[i].encode()
                i += 1
        body.append(0)
        address += 2 + len(body)
        prg += address.to_bytes(2, "little") + body
    return bytes(prg + b"\0\0")


def c64():
    # A 1541 disk. SMOKE blacks out the screen, waits a second, by which time the plugin has turned
    # true drive emulation back on, and loads PATTERN through the emulated drive. LOAD in a program
    # reruns it from the top, where A is now set, so it calls PATTERN, which fills the screen with
    # colour stripes. Everything else, an error message included, is black on black.
    smoke = basic({
        10: "IF A THEN SYS 49152",
        20: "A=1:POKE 53280,0:POKE 53281,0:POKE 646,0:PRINT CHR$(147):FOR I=1 TO 1000:NEXT",
        30: 'LOAD "PATTERN",8,1',
    })
    a = Asm(0xC000)
    a(0xA2, 0x00)  # ldx #0
    loop = a.here()
    a(0xA9, 0xA0)  # lda #$A0                           a solid block
    for page in range(0x04, 0x08):
        a(0x9D, 0x00, page)  # sta $0400,x ... $0700,x   screen
    a(0x8A)  # txa                                       colour x & 15
    for page in range(0xD8, 0xDC):
        a(0x9D, 0x00, page)  # sta $D800,x ... $DB00,x   colour RAM
    a(0xE8)  # inx
    a.rel8(0xD0, loop)  # bne
    a(0x4C, *a.here().to_bytes(2, "little"))  # jmp .

    sectors = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5
    disk = bytearray(sum(sectors) * 256)

    def sector(track, index):
        offset = (sum(sectors[: track - 1]) + index) * 256
        return memoryview(disk)[offset : offset + 256]

    files = [("SMOKE", 0x0801, smoke, (17, 0)), ("PATTERN", 0xC000, bytes(a.code), (17, 1))]
    directory = sector(18, 1)
    directory[0:2] = bytes((0, 0xFF))  # the only directory sector
    for i, (name, load, data, (track, index)) in enumerate(files):
        payload = load.to_bytes(2, "little") + data
        assert len(payload) <= 254, name
        block = sector(track, index)
        block[0:2] = bytes((0, len(payload) + 1))  # last block: offset of its last byte
        block[2 : 2 + len(payload)] = payload
        entry = directory[32 * i : 32 * i + 32]
        entry[2:5] = bytes((0x82, track, index))  # closed PRG
        entry[5:21] = name.encode().ljust(16, b"\xa0")
        entry[30:32] = (1).to_bytes(2, "little")  # one block

    bam = sector(18, 0)
    bam[0:4] = bytes((18, 1, 0x41, 0))  # directory at 18/1, DOS format 'A'
    used = {(18, 0), (18, 1)} | {ts for *_, ts in files}
    for track in range(1, 36):
        free = [s for s in range(sectors[track - 1]) if (track, s) not in used]
        bits = sum(1 << s for s in free)
        bam[4 * track : 4 * track + 4] = bytes((len(free),)) + bits.to_bytes(3, "little")
    bam[0x90:0xAB] = b"REPLAY SMOKE".ljust(16, b"\xa0") + b"\xa0\xa0RS\xa02A" + b"\xa0" * 4
    return bytes(disk)


ROMS = {
    "mesen2_gba/smoke/smoke.gba": gba,
    "mesen2_pce/smoke/smoke.pce": pce,
    "mesen2_sms/smoke/smoke.sms": sms,
    "mesen2_snes/smoke/smoke.sfc": snes,
    "mesen2_ws/smoke/smoke.ws": ws,
    "vice_c64/smoke/smoke.d64": c64,
}

if __name__ == "__main__":
    for path, build in ROMS.items():
        out = REPO_DIR / "plugins" / path
        out.parent.mkdir(exist_ok=True)
        out.write_bytes(build())
        print(f"wrote plugins/{path}")
