# VICE (C64)

The emulator is the libretro port of VICE, carried in-tree under `upstream/`.

## The ROMs are deliberately absent

`upstream/vice/data/` arrived from the frontend carrying 33 `.bin` files: Commodore's KERNAL,
BASIC and CHARGEN images, plus the 1541 drive and printer ROMs. They were deleted before this
plugin was first committed and must not come back.

They are Commodore's copyrighted code. This project has no right to redistribute them, and a
published plugin that contained them would be distributing them to every user. Because git history
is permanent, a single commit carrying them would be unfixable without rewriting a published
repository, so the deletion has to happen before the copy lands rather than afterwards.

What remains in `data/` is VICE's own material -- palettes, keymaps, and its build files -- which
is GPL like the rest of the emulator.

The consequence is that the plugin cannot boot on its own. Its config template declares
`requires_bios`, and the frontend supplies the ROMs the user has provided. A smoke for this plugin
needs an open replacement set rather than a dump.

**If you re-sync `upstream/` from vice-libretro, delete `upstream/vice/data/**/*.bin` again as part
of that sync**; upstream ships them and every update will bring them back.
