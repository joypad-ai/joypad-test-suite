# GameCube Homebrew Bootloader

`gbi.hdr` is the **GameCube Homebrew Bootloader / open-source apploader**
originally released by bushing (Ben Byer) on the HackMii blog in
2008, based on the GC-Linux project's apploader. It's the legally-
redistributable replacement for Nintendo's proprietary apploader,
designed specifically so homebrew projects can ship bootable
iso9660 + El Torito GameCube ISOs.

## What it does

It's a small disc-image-prefix binary (32 KB) containing:

- A GameCube disc header that tells the GameCube how to boot the disc.
- A minimal apploader that runs at boot, locates the file named
  `bootldr.dol` inside the iso9660 file tree, loads it into memory,
  and jumps to its entry point.

The build step composes this header with the rest of the disc tree
(our `.dol` renamed to `bootldr.dol`, plus `opening.bnr`) via
`mkisofs` to produce a bootable `.iso`.

## Provenance + licence

Vendored verbatim from
[ArtemioUrbina/240pTestSuite](https://github.com/ArtemioUrbina/240pTestSuite/blob/master/240psuite/Wii/240pSuite/homebrew_disc/gbi.hdr),
which uses the same file. The apploader's code lineage goes back to
the GPL-licensed GC-Linux apploader; bushing's release inherits that
licence even though the binary blob itself doesn't carry the licence
text. Treat this file as GPL — the rest of `joypad-tester/` is
MIT-licensed, so this `apploader/` subdirectory is the only
GPL-licensed footprint in the repo.

If you redistribute a `.iso` built using this blob, your distribution
is subject to GPL terms for the bootloader segment (no additional
restrictions; source = this same `gbi.hdr` binary as far as anyone
can practically obtain).

Credits:

- **bushing (Ben Byer)** — packaged the homebrew apploader release
  ([HackMii blog post, 2008-08](https://hackmii.com/2008/08/open-source-apploader-iso-template/))
- **GC-Linux project** — original apploader implementation
- **Artemio Urbina (240pTestSuite)** — kept the binary distributable
  alongside an actively-maintained homebrew project we could pull
  from in 2026
