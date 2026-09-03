# VICE C64 Emulator - CMake build for C64 target only
#
# Based on vice-libretro (https://github.com/libretro/vice-libretro)
# This builds VICE as a static library for embedding in the Replay Frontend

# Note: We don't use project() here as this is included as a subdirectory
# The parent project has already set up the compiler

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# Directories
set(EMU ${CMAKE_CURRENT_SOURCE_DIR}/vice/src)
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(RETRODEP ${CMAKE_CURRENT_SOURCE_DIR}/retrodep)
set(LIBRETRO_COMM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libretro-common)

# C64-specific define
add_compile_definitions(__X64__)

# Required defines for VICE libretro build
add_compile_definitions(__LIBRETRO__)
add_compile_definitions(HAVE_CONFIG_H)

# Core name for x64 (C64)
add_compile_definitions(CORE_NAME="x64")

# Use libretro VFS abstraction
add_compile_definitions(USE_LIBRETRO_VFS)

# Suppress warnings for external code
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/embedded
    ${EMU}/core/rtc
    ${EMU}/lib
    ${EMU}/printerdrv
    ${RETRODEP}
    ${LIBRETRO_COMM_DIR}/include
    ${LIBRETRO_COMM_DIR}/include/compat/zlib
    # C64-specific includes from Makefile.x64
    ${EMU}/sid
    ${EMU}
    ${EMU}/arch/shared
    ${EMU}/c64
    ${EMU}/c64/cart
    ${EMU}/c64dtv
    ${EMU}/core
    ${EMU}/datasette
    ${EMU}/diag
    ${EMU}/drive
    ${EMU}/drive/iec
    ${EMU}/drive/iec/c64exp
    ${EMU}/drive/ieee
    ${EMU}/drive/tcbm
    ${EMU}/fsdevice
    ${EMU}/imagecontents
    ${EMU}/joyport
    ${EMU}/lib/p64
    ${EMU}/monitor
    ${EMU}/raster
    ${EMU}/resid
    ${EMU}/residfp
    ${EMU}/residfp/builders/residfp-builder/residfp
    ${EMU}/rs232drv
    ${EMU}/rtc
    ${EMU}/samplerdrv
    ${EMU}/socketdrv
    ${EMU}/tape
    ${EMU}/tapeport
    ${EMU}/userport
    ${EMU}/vdrive
    ${EMU}/vicii
    ${EMU}/video
)

# ============================================================================
# Libretro integration sources
# ============================================================================
set(SOURCES_LIBRETRO
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-mapper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-dc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-vkbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro/libretro-graph.c
)

# ============================================================================
# Common VICE sources (from Makefile.common SOURCES_C_COMMON)
# ============================================================================
set(SOURCES_COMMON
    ${EMU}/alarm.c
    ${EMU}/attach.c
    ${EMU}/autostart-prg.c
    ${EMU}/autostart.c
    ${EMU}/cbmdos.c
    ${EMU}/cbmimage.c
    ${EMU}/charset.c
    ${EMU}/clipboard.c
    ${EMU}/cmdline.c
    ${EMU}/color.c
    ${EMU}/core/ata.c
    ${EMU}/core/ciacore.c
    ${EMU}/core/ciatimer.c
    ${EMU}/core/cs8900.c
    ${EMU}/core/flash040core.c
    ${EMU}/core/fmopl.c
    ${EMU}/core/i8255a.c
    ${EMU}/core/m93c86.c
    ${EMU}/core/mc6821core.c
    ${EMU}/core/riotcore.c
    ${EMU}/core/scsi.c
    ${EMU}/core/ser-eeprom.c
    ${EMU}/core/spi-flash.c
    ${EMU}/core/spi-sdcard.c
    ${EMU}/core/t6721.c
    ${EMU}/core/tpicore.c
    ${EMU}/core/viacore.c
    ${EMU}/core/rtc/bq4830y.c
    ${EMU}/core/rtc/ds12c887.c
    ${EMU}/core/rtc/ds1202_1302.c
    ${EMU}/core/rtc/ds1216e.c
    ${EMU}/core/rtc/ds1307.c
    ${EMU}/core/rtc/ds1602.c
    ${EMU}/core/rtc/pcf8583.c
    ${EMU}/core/rtc/rtc-58321a.c
    ${EMU}/core/rtc/rtc-72421.c
    ${EMU}/core/rtc/rtc.c
    ${EMU}/crc32.c
    ${EMU}/crt.c
    ${EMU}/debug.c
    ${EMU}/diskimage/diskimage.c
    ${EMU}/diskimage/fsimage-check.c
    ${EMU}/diskimage/fsimage-create.c
    ${EMU}/diskimage/fsimage-dxx.c
    ${EMU}/diskimage/fsimage-gcr.c
    ${EMU}/diskimage/fsimage-p64.c
    ${EMU}/diskimage/fsimage-probe.c
    ${EMU}/diskimage/fsimage.c
    ${EMU}/diskimage/realimage.c
    ${EMU}/drive/ieee/fdc.c
    ${EMU}/drive/ieee/ieee-cmdline-options.c
    ${EMU}/drive/ieee/ieee-resources.c
    ${EMU}/drive/ieee/ieee.c
    ${EMU}/drive/ieee/ieeerom.c
    ${EMU}/drive/ieee/memieee.c
    ${EMU}/drive/ieee/riot1d.c
    ${EMU}/drive/ieee/riot2d.c
    ${EMU}/drive/ieee/via1d2031.c
    ${EMU}/drive/rotation.c
    ${EMU}/drive/tcbm/glue1551.c
    ${EMU}/drive/tcbm/mem1551.c
    ${EMU}/drive/tcbm/tcbm-cmdline-options.c
    ${EMU}/drive/tcbm/tcbm-resources.c
    ${EMU}/drive/tcbm/tcbm.c
    ${EMU}/drive/tcbm/tcbmrom.c
    ${EMU}/drive/tcbm/tpid.c
    ${EMU}/dma.c
    ${EMU}/event.c
    ${EMU}/fileio/cbmfile.c
    ${EMU}/fileio/fileio.c
    ${EMU}/fileio/p00.c
    ${EMU}/findpath.c
    ${EMU}/fliplist.c
    ${EMU}/fsdevice/fsdevice-close.c
    ${EMU}/fsdevice/fsdevice-cmdline-options.c
    ${EMU}/fsdevice/fsdevice-filename.c
    ${EMU}/fsdevice/fsdevice-flush.c
    ${EMU}/fsdevice/fsdevice-open.c
    ${EMU}/fsdevice/fsdevice-read.c
    ${EMU}/fsdevice/fsdevice-resources.c
    ${EMU}/fsdevice/fsdevice-write.c
    ${EMU}/fsdevice/fsdevice.c
    ${EMU}/gcr.c
    ${EMU}/iecbus/iecbus.c
    ${EMU}/imagecontents/diskcontents-block.c
    ${EMU}/imagecontents/diskcontents-iec.c
    ${EMU}/imagecontents/diskcontents.c
    ${EMU}/imagecontents/imagecontents.c
    ${EMU}/imagecontents/tapecontents.c
    ${EMU}/init.c
    ${EMU}/initcmdline.c
    ${EMU}/interrupt.c
    ${EMU}/kbdbuf.c
    ${EMU}/keyboard.c
    ${EMU}/keymap.c
    ${EMU}/lib.c
    ${EMU}/lib/p64/p64.c
    ${EMU}/log.c
    ${EMU}/machine-bus.c
    ${EMU}/machine.c
    ${EMU}/main.c
    ${EMU}/midi.c
    ${EMU}/network.c
    ${EMU}/opencbmlib.c
    ${EMU}/palette.c
    ${EMU}/parallel/parallel-trap.c
    ${EMU}/parallel/parallel.c
    ${EMU}/profiler.c
    ${EMU}/ram.c
    ${EMU}/raster/raster-cache.c
    ${EMU}/raster/raster-canvas.c
    ${EMU}/raster/raster-changes.c
    ${EMU}/raster/raster-cmdline-options.c
    ${EMU}/raster/raster-line-changes-sprite.c
    ${EMU}/raster/raster-line-changes.c
    ${EMU}/raster/raster-line.c
    ${EMU}/raster/raster-modes.c
    ${EMU}/raster/raster-resources.c
    ${EMU}/raster/raster-snapshot.c
    ${EMU}/raster/raster-sprite-cache.c
    ${EMU}/raster/raster-sprite-status.c
    ${EMU}/raster/raster-sprite.c
    ${EMU}/raster/raster.c
    ${EMU}/rawfile.c
    ${EMU}/rawnet.c
    ${EMU}/resources.c
    ${EMU}/romset.c
    ${EMU}/rs232drv/rs232.c
    ${EMU}/rs232drv/rs232drv.c
    ${EMU}/rs232drv/rs232net.c
    ${EMU}/rs232drv/rsuser.c
    ${EMU}/screenshot.c
    ${EMU}/snapshot.c
    ${EMU}/socket.c
    ${EMU}/sound.c
    ${EMU}/sysfile.c
    ${EMU}/traps.c
    ${EMU}/util.c
    ${EMU}/vicefeatures.c
    ${EMU}/vdrive/vdrive-bam.c
    ${EMU}/vdrive/vdrive-command.c
    ${EMU}/vdrive/vdrive-dir.c
    ${EMU}/vdrive/vdrive-iec.c
    ${EMU}/vdrive/vdrive-internal.c
    ${EMU}/vdrive/vdrive-rel.c
    ${EMU}/vdrive/vdrive-snapshot.c
    ${EMU}/vdrive/vdrive.c
    ${EMU}/video/render1x1.c
    ${EMU}/video/render1x1ntsc.c
    ${EMU}/video/render1x1pal.c
    ${EMU}/video/render1x1rgbi.c
    ${EMU}/video/video-canvas.c
    ${EMU}/video/video-cmdline-options.c
    ${EMU}/video/video-color.c
    ${EMU}/video/video-render.c
    ${EMU}/video/video-render-crtmono.c
    ${EMU}/video/video-render-palntsc.c
    ${EMU}/video/video-render-rgbi.c
    ${EMU}/video/video-resources.c
    ${EMU}/video/video-sound.c
    ${EMU}/video/video-viewport.c
    ${EMU}/vsync.c
    ${EMU}/zfile.c
    ${EMU}/zipcode.c
)

# ============================================================================
# Joyport sources
# ============================================================================
set(SOURCES_JOYPORT
    ${EMU}/joyport/bbrtc.c
    ${EMU}/joyport/cardkey.c
    ${EMU}/joyport/coplin_keypad.c
    ${EMU}/joyport/cx21.c
    ${EMU}/joyport/cx85.c
    ${EMU}/joyport/inception.c
    ${EMU}/joyport/joyport.c
    ${EMU}/joyport/joyport_io_sim.c
    ${EMU}/joyport/joystick.c
    ${EMU}/joyport/lightpen.c
    ${EMU}/joyport/mouse.c
    ${EMU}/joyport/mouse_1351.c
    ${EMU}/joyport/mouse_neos.c
    ${EMU}/joyport/mouse_paddle.c
    ${EMU}/joyport/mouse_quadrature.c
    ${EMU}/joyport/multijoy.c
    ${EMU}/joyport/ninja_snespad.c
    ${EMU}/joyport/paperclip2.c
    ${EMU}/joyport/paperclip64.c
    ${EMU}/joyport/paperclip64e.c
    ${EMU}/joyport/paperclip64sc.c
    ${EMU}/joyport/powerpad.c
    ${EMU}/joyport/protopad.c
    ${EMU}/joyport/rushware_keypad.c
    ${EMU}/joyport/sampler2bit.c
    ${EMU}/joyport/sampler4bit.c
    ${EMU}/joyport/script64_dongle.c
    ${EMU}/joyport/spaceballs.c
    ${EMU}/joyport/trapthem_snespad.c
    ${EMU}/joyport/vizawrite64_dongle.c
    ${EMU}/joyport/waasoft_dongle.c
)

# ============================================================================
# Userport sources
# ============================================================================
set(SOURCES_USERPORT
    ${EMU}/userport/userport.c
    ${EMU}/userport/userport_4bit_sampler.c
    ${EMU}/userport/userport_8bss.c
    ${EMU}/userport/userport_dac.c
    ${EMU}/userport/userport_diag_586220_harness.c
    ${EMU}/userport/userport_diag_pin.c
    ${EMU}/userport/userport_digimax.c
    ${EMU}/userport/userport_hks_joystick.c
    ${EMU}/userport/userport_hummer_joystick.c
    ${EMU}/userport/userport_io_sim.c
    ${EMU}/userport/userport_joystick.c
    ${EMU}/userport/userport_petscii_snespad.c
    ${EMU}/userport/userport_rtc_58321a.c
    ${EMU}/userport/userport_rtc_ds1307.c
    ${EMU}/userport/userport_spt_joystick.c
    ${EMU}/userport/userport_superpad64.c
    ${EMU}/userport/userport_synergy_joystick.c
    ${EMU}/userport/userport_wic64.c
    ${EMU}/userport/userport_woj_joystick.c
)

# ============================================================================
# C64 cartridge sources
# ============================================================================
set(SOURCES_C64_CART
    ${EMU}/c64/cart/actionreplay.c
    ${EMU}/c64/cart/actionreplay2.c
    ${EMU}/c64/cart/actionreplay3.c
    ${EMU}/c64/cart/actionreplay4.c
    ${EMU}/c64/cart/atomicpower.c
    ${EMU}/c64/cart/bisplus.c
    ${EMU}/c64/cart/blackbox3.c
    ${EMU}/c64/cart/blackbox4.c
    ${EMU}/c64/cart/blackbox8.c
    ${EMU}/c64/cart/blackbox9.c
    ${EMU}/c64/cart/bmpdataturbo.c
    ${EMU}/c64/cart/c64-generic.c
    ${EMU}/c64/cart/c64-midi.c
    ${EMU}/c64/cart/c64acia1.c
    ${EMU}/c64/cart/c64cart.c
    ${EMU}/c64/cart/c64carthooks.c
    ${EMU}/c64/cart/c64cartmem.c
    ${EMU}/c64/cart/c64tpi.c
    ${EMU}/c64/cart/capture.c
    ${EMU}/c64/cart/clockport-mp3at64.c
    ${EMU}/c64/cart/clockport-rrnet.c
    ${EMU}/c64/cart/clockport.c
    ${EMU}/c64/cart/comal80.c
    ${EMU}/c64/cart/cs8900io.c
    ${EMU}/c64/cart/daa.c
    ${EMU}/c64/cart/debugcart.c
    ${EMU}/c64/cart/delaep7x8.c
    ${EMU}/c64/cart/delaep64.c
    ${EMU}/c64/cart/delaep256.c
    ${EMU}/c64/cart/diashowmaker.c
    ${EMU}/c64/cart/digimax.c
    ${EMU}/c64/cart/dinamic.c
    ${EMU}/c64/cart/drean.c
    ${EMU}/c64/cart/dqbb.c
    ${EMU}/c64/cart/ds12c887rtc.c
    ${EMU}/c64/cart/easycalc.c
    ${EMU}/c64/cart/easyflash.c
    ${EMU}/c64/cart/epyxfastload.c
    ${EMU}/c64/cart/ethernetcart.c
    ${EMU}/c64/cart/exos.c
    ${EMU}/c64/cart/expert.c
    ${EMU}/c64/cart/final.c
    ${EMU}/c64/cart/final3.c
    ${EMU}/c64/cart/finalplus.c
    ${EMU}/c64/cart/formel64.c
    ${EMU}/c64/cart/freezeframe.c
    ${EMU}/c64/cart/freezeframe2.c
    ${EMU}/c64/cart/freezemachine.c
    ${EMU}/c64/cart/funplay.c
    ${EMU}/c64/cart/gamekiller.c
    ${EMU}/c64/cart/georam.c
    ${EMU}/c64/cart/gmod2.c
    ${EMU}/c64/cart/gmod3.c
    ${EMU}/c64/cart/gs.c
    ${EMU}/c64/cart/hyperbasic.c
    ${EMU}/c64/cart/ide64.c
    ${EMU}/c64/cart/ieeeflash64.c
    ${EMU}/c64/cart/isepic.c
    ${EMU}/c64/cart/kcs.c
    ${EMU}/c64/cart/kingsoft.c
    ${EMU}/c64/cart/ltkernal.c
    ${EMU}/c64/cart/mach5.c
    ${EMU}/c64/cart/magicdesk.c
    ${EMU}/c64/cart/magicdesk16.c
    ${EMU}/c64/cart/magicformel.c
    ${EMU}/c64/cart/magicvoice.c
    ${EMU}/c64/cart/maxbasic.c
    ${EMU}/c64/cart/mikroass.c
    ${EMU}/c64/cart/multimax.c
    ${EMU}/c64/cart/mmc64.c
    ${EMU}/c64/cart/mmcreplay.c
    ${EMU}/c64/cart/ocean.c
    ${EMU}/c64/cart/pagefox.c
    ${EMU}/c64/cart/partner64.c
    ${EMU}/c64/cart/prophet64.c
    ${EMU}/c64/cart/ramcart.c
    ${EMU}/c64/cart/ramlink.c
    ${EMU}/c64/cart/retroreplay.c
    ${EMU}/c64/cart/reu.c
    ${EMU}/c64/cart/rexep256.c
    ${EMU}/c64/cart/rexutility.c
    ${EMU}/c64/cart/rgcd.c
    ${EMU}/c64/cart/ross.c
    ${EMU}/c64/cart/rrnetmk3.c
    ${EMU}/c64/cart/sdbox.c
    ${EMU}/c64/cart/sfx_soundexpander.c
    ${EMU}/c64/cart/sfx_soundsampler.c
    ${EMU}/c64/cart/shortbus.c
    ${EMU}/c64/cart/shortbus_digimax.c
    ${EMU}/c64/cart/shortbus_etfe.c
    ${EMU}/c64/cart/silverrock128.c
    ${EMU}/c64/cart/simonsbasic.c
    ${EMU}/c64/cart/snapshot64.c
    ${EMU}/c64/cart/stardos.c
    ${EMU}/c64/cart/stb.c
    ${EMU}/c64/cart/superexplode5.c
    ${EMU}/c64/cart/supergames.c
    ${EMU}/c64/cart/supersnapshot.c
    ${EMU}/c64/cart/supersnapshot4.c
    ${EMU}/c64/cart/turtlegraphics.c
    ${EMU}/c64/cart/uc1.c
    ${EMU}/c64/cart/uc2.c
    ${EMU}/c64/cart/rexramfloppy.c
    ${EMU}/c64/cart/warpspeed.c
    ${EMU}/c64/cart/westermann.c
    ${EMU}/c64/cart/zaxxon.c
    ${EMU}/c64/cart/zippcode48.c
)

# ============================================================================
# C64-specific sources (from Makefile.x64)
# ============================================================================
set(SOURCES_C64
    ${EMU}/c64/c64-cmdline-options.c
    ${EMU}/c64/c64-memory-hacks.c
    ${EMU}/c64/c64-resources.c
    ${EMU}/c64/c64.c
    ${EMU}/c64/c64bus.c
    ${EMU}/c64/c64cia1.c
    ${EMU}/c64/c64cia2.c
    ${EMU}/c64/c64cpu.c
    ${EMU}/c64/c64datasette.c
    ${EMU}/c64/c64drive.c
    ${EMU}/c64/c64export.c
    ${EMU}/c64/c64fastiec.c
    ${EMU}/c64/c64gluelogic.c
    ${EMU}/c64/c64iec.c
    ${EMU}/c64/c64io.c
    ${EMU}/c64/c64keyboard.c
    ${EMU}/c64/c64mem.c
    ${EMU}/c64/c64meminit.c
    ${EMU}/c64/c64memlimit.c
    ${EMU}/c64/c64memrom.c
    ${EMU}/c64/c64memsnapshot.c
    ${EMU}/c64/c64model.c
    ${EMU}/c64/c64parallel.c
    ${EMU}/c64/c64pla.c
    ${EMU}/c64/c64printer.c
    ${EMU}/c64/c64rom.c
    ${EMU}/c64/c64romset.c
    ${EMU}/c64/c64rsuser.c
    ${EMU}/c64/c64sound.c
    ${EMU}/c64/c64video.c
    ${EMU}/c64/c64_256k.c
    ${EMU}/c64/plus60k.c
    ${EMU}/c64/plus256k.c
    ${EMU}/c64/reloc65.c
    # Datasette
    ${EMU}/datasette/datasette.c
    ${EMU}/datasette/datasette-sound.c
    # Diagnostics
    ${EMU}/diag/c64_diag_586220_harness.c
    # Drive
    ${EMU}/drive/drive-check.c
    ${EMU}/drive/drive-cmdline-options.c
    ${EMU}/drive/drive-resources.c
    ${EMU}/drive/drive-snapshot.c
    ${EMU}/drive/drive-sound.c
    ${EMU}/drive/drive-writeprotect.c
    ${EMU}/drive/drive.c
    ${EMU}/drive/drivecpu.c
    ${EMU}/drive/drivecpu65c02.c
    ${EMU}/drive/driveimage.c
    ${EMU}/drive/drivemem.c
    ${EMU}/drive/driverom.c
    ${EMU}/drive/drivesync.c
    # IEC drive
    ${EMU}/drive/iec/c64exp/c64exp-cmdline-options.c
    ${EMU}/drive/iec/c64exp/c64exp-resources.c
    ${EMU}/drive/iec/c64exp/dolphindos3.c
    ${EMU}/drive/iec/c64exp/iec-c64exp.c
    ${EMU}/drive/iec/c64exp/profdos.c
    ${EMU}/drive/iec/c64exp/supercard.c
    ${EMU}/drive/iec/c64exp/stardos-exp.c
    ${EMU}/drive/iec/cia1571d.c
    ${EMU}/drive/iec/cia1581d.c
    ${EMU}/drive/iec/cmdhd.c
    ${EMU}/drive/iec/fdd.c
    ${EMU}/drive/iec/glue1571.c
    ${EMU}/drive/iec/iec-cmdline-options.c
    ${EMU}/drive/iec/iec-resources.c
    ${EMU}/drive/iec/iec.c
    ${EMU}/drive/iec/iecrom.c
    ${EMU}/drive/iec/memiec.c
    ${EMU}/drive/iec/pc8477.c
    ${EMU}/drive/iec/via1d1541.c
    ${EMU}/drive/iec/via4000.c
    ${EMU}/drive/iec/wd1770.c
    ${EMU}/drive/iecieee/iecieee.c
    ${EMU}/drive/iecieee/via2d.c
    # Printer
    ${EMU}/printerdrv/driver-select.c
    ${EMU}/printerdrv/drv-ascii.c
    ${EMU}/printerdrv/drv-raw.c
    ${EMU}/printerdrv/interface-serial.c
    ${EMU}/printerdrv/interface-userport.c
    ${EMU}/printerdrv/output-graphics.c
    ${EMU}/printerdrv/output-select.c
    ${EMU}/printerdrv/output-text.c
    ${EMU}/printerdrv/printer-serial.c
    ${EMU}/printerdrv/printer-userport.c
    ${EMU}/printerdrv/printer.c
    # Serial
    ${EMU}/serial/fsdrive.c
    ${EMU}/serial/realdevice.c
    ${EMU}/serial/serial-device.c
    ${EMU}/serial/serial-iec-bus.c
    ${EMU}/serial/serial-iec-device.c
    ${EMU}/serial/serial-iec-lib.c
    ${EMU}/serial/serial-iec.c
    ${EMU}/serial/serial-realdevice.c
    ${EMU}/serial/serial-trap.c
    ${EMU}/serial/serial.c
    # SID
    ${EMU}/sid/catweaselmkiii.c
    ${EMU}/sid/fastsid.c
    ${EMU}/sid/hardsid.c
    ${EMU}/sid/parsid.c
    ${EMU}/sid/sid-cmdline-options.c
    ${EMU}/sid/sid-resources.c
    ${EMU}/sid/sid-snapshot.c
    ${EMU}/sid/sid.c
    ${EMU}/sid/ssi2001.c
    # Tape
    ${EMU}/tape/t64.c
    ${EMU}/tape/tap.c
    ${EMU}/tape/tape-internal.c
    ${EMU}/tape/tape-snapshot.c
    ${EMU}/tape/tape.c
    ${EMU}/tape/tapeimage.c
    # Tapeport
    ${EMU}/tapeport/cp-clockf83.c
    ${EMU}/tapeport/dtl-basic-dongle.c
    ${EMU}/tapeport/sense-dongle.c
    ${EMU}/tapeport/tapecart.c
    ${EMU}/tapeport/tapeport.c
    ${EMU}/tapeport/tape_diag_586220_harness.c
    # VIC-II
    ${EMU}/vicii/vicii-badline.c
    ${EMU}/vicii/vicii-cmdline-options.c
    ${EMU}/vicii/vicii-color.c
    ${EMU}/vicii/vicii-draw.c
    ${EMU}/vicii/vicii-fetch.c
    ${EMU}/vicii/vicii-irq.c
    ${EMU}/vicii/vicii-mem.c
    ${EMU}/vicii/vicii-phi1.c
    ${EMU}/vicii/vicii-resources.c
    ${EMU}/vicii/vicii-snapshot.c
    ${EMU}/vicii/vicii-sprites.c
    ${EMU}/vicii/vicii-stubs.c
    ${EMU}/vicii/vicii-timing.c
    ${EMU}/vicii/vicii.c
)

# ============================================================================
# ReSID (C++ SID emulation)
# ============================================================================
set(SOURCES_RESID_CXX
    ${EMU}/resid/dac.cc
    ${EMU}/resid/envelope.cc
    ${EMU}/resid/extfilt.cc
    ${EMU}/resid/filter8580new.cc
    ${EMU}/resid/pot.cc
    ${EMU}/resid/sid.cc
    ${EMU}/resid/version.cc
    ${EMU}/resid/voice.cc
    ${EMU}/resid/wave.cc
    ${EMU}/sid/resid.cc
    ${EMU}/sid/resid-fp.cc
)

# ============================================================================
# ReSIDfp (improved SID emulation)
# ============================================================================
set(SOURCES_RESIDFP_CXX
    ${EMU}/residfp/builders/residfp-builder/residfp/version.cc
    ${EMU}/residfp/builders/residfp-builder/residfp/Dac.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/EnvelopeGenerator.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/ExternalFilter.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Filter.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Filter6581.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Filter8580.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/FilterModelConfig.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/FilterModelConfig8580.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Integrator.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Integrator8580.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/OpAmp.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/resample/SincResampler.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/SID.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/Spline.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/WaveformCalculator.cpp
    ${EMU}/residfp/builders/residfp-builder/residfp/WaveformGenerator.cpp
)

# ============================================================================
# Arch/shared sources
# ============================================================================
set(SOURCES_ARCH
    ${EMU}/arch/shared/socketdrv/socketdrv.c
)

# ============================================================================
# Retrodep sources (embedded data and stubs)
# ============================================================================
set(SOURCES_RETRODEP
    ${RETRODEP}/embedded.c
    ${RETRODEP}/embedded/c64embedded.c
    ${RETRODEP}/archdep.c
    ${RETRODEP}/console.c
    ${RETRODEP}/joy.c
    ${RETRODEP}/lightpendrv.c
    ${RETRODEP}/kbd.c
    ${RETRODEP}/mousedrv.c
    ${RETRODEP}/signals.c
    ${RETRODEP}/soundretro.c
    ${RETRODEP}/ui.c
    ${RETRODEP}/uimon.c
    ${RETRODEP}/uistatusbar.c
    ${RETRODEP}/video.c
    ${RETRODEP}/vsyncarch.c
    # Stubs
    ${RETRODEP}/info.c
    ${RETRODEP}/cart/cpmcart.c
    ${RETRODEP}/gfxoutputdrv/gfxoutput.c
    ${RETRODEP}/monitor/asm6502.c
    ${RETRODEP}/monitor/asmR65C02.c
    ${RETRODEP}/monitor/asmz80.c
    ${RETRODEP}/monitor/monitor.c
    ${RETRODEP}/monitor/monitor_network.c
    ${RETRODEP}/monitor/mon_util.c
    ${RETRODEP}/printerdrv/drv-1520.c
    ${RETRODEP}/printerdrv/drv-mps803.c
    ${RETRODEP}/printerdrv/drv-nl10.c
    ${RETRODEP}/samplerdrv/file_drv.c
    ${RETRODEP}/samplerdrv/sampler.c
    ${RETRODEP}/video/renderscale2x.c
    ${RETRODEP}/video/video-render-2x2.c
)

# ============================================================================
# Dependency sources (zlib, 7zip)
# ============================================================================
set(SOURCES_DEPS
    ${DEPS_DIR}/libz/unzip.c
    ${DEPS_DIR}/libz/ioapi.c
    ${DEPS_DIR}/libz/adler32.c
    ${DEPS_DIR}/libz/crc32.c
    ${DEPS_DIR}/libz/deflate.c
    ${DEPS_DIR}/libz/gzclose.c
    ${DEPS_DIR}/libz/gzlib.c
    ${DEPS_DIR}/libz/gzread.c
    ${DEPS_DIR}/libz/gzwrite.c
    ${DEPS_DIR}/libz/inffast.c
    ${DEPS_DIR}/libz/inflate.c
    ${DEPS_DIR}/libz/inftrees.c
    ${DEPS_DIR}/libz/trees.c
    ${DEPS_DIR}/libz/zutil.c
    ${DEPS_DIR}/7zip/7zArcIn.c
    ${DEPS_DIR}/7zip/7zBuf.c
    ${DEPS_DIR}/7zip/7zCrc.c
    ${DEPS_DIR}/7zip/7zCrcOpt.c
    ${DEPS_DIR}/7zip/7zDec.c
    ${DEPS_DIR}/7zip/7zFile.c
    ${DEPS_DIR}/7zip/7zStream.c
    ${DEPS_DIR}/7zip/Bcj2.c
    ${DEPS_DIR}/7zip/Bra.c
    ${DEPS_DIR}/7zip/Bra86.c
    ${DEPS_DIR}/7zip/BraIA64.c
    ${DEPS_DIR}/7zip/CpuArch.c
    ${DEPS_DIR}/7zip/Delta.c
    ${DEPS_DIR}/7zip/Lzma2Dec.c
    ${DEPS_DIR}/7zip/LzmaDec.c
)

# ============================================================================
# Libretro-common sources
# ============================================================================
set(SOURCES_LIBRETRO_COMMON
    ${LIBRETRO_COMM_DIR}/compat/compat_strl.c
    ${LIBRETRO_COMM_DIR}/compat/compat_strcasestr.c
    ${LIBRETRO_COMM_DIR}/compat/fopen_utf8.c
    ${LIBRETRO_COMM_DIR}/encodings/encoding_utf.c
    ${LIBRETRO_COMM_DIR}/file/file_path.c
    ${LIBRETRO_COMM_DIR}/file/file_path_io.c
    ${LIBRETRO_COMM_DIR}/streams/file_stream.c
    ${LIBRETRO_COMM_DIR}/streams/file_stream_transforms.c
    ${LIBRETRO_COMM_DIR}/string/stdstring.c
    ${LIBRETRO_COMM_DIR}/time/rtime.c
    ${LIBRETRO_COMM_DIR}/vfs/vfs_implementation.c
)

# ============================================================================
# Build static library
# ============================================================================
add_library(vice_c64_lib STATIC
    ${SOURCES_LIBRETRO}
    ${SOURCES_COMMON}
    ${SOURCES_JOYPORT}
    ${SOURCES_USERPORT}
    ${SOURCES_C64_CART}
    ${SOURCES_C64}
    ${SOURCES_RESID_CXX}
    ${SOURCES_RESIDFP_CXX}
    ${SOURCES_ARCH}
    ${SOURCES_RETRODEP}
    ${SOURCES_DEPS}
    ${SOURCES_LIBRETRO_COMMON}
)

# Ensure PIC for shared library linking
set_target_properties(vice_c64_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Remove parent project's include directories to avoid header conflicts
# The parent CMakeLists.txt has include_directories(src/base external) which
# would cause VICE's log.h, etc. to conflict with the frontend's versions
get_target_property(VICE_INCLUDES vice_c64_lib INCLUDE_DIRECTORIES)
if(VICE_INCLUDES)
    list(FILTER VICE_INCLUDES EXCLUDE REGEX ".*/src/base$")
    list(FILTER VICE_INCLUDES EXCLUDE REGEX "^external$")
    set_target_properties(vice_c64_lib PROPERTIES INCLUDE_DIRECTORIES "${VICE_INCLUDES}")
endif()

# Export include directories for consumers
target_include_directories(vice_c64_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EMU}
)
