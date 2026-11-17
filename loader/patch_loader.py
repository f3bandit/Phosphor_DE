#!/usr/bin/env python3
"""Phosphor_DE loader recolor - by f3bandit

Makes the cart's loader screens (the "EZ-FLASH UPDATE MODE" / kernel install / "No kernel found"
screens) Phosphor grey instead of bright blue, by changing two colors in EZ-Flash's own
loader recovery image. Nothing else in the image changes: 4 bytes, no code.

  python patch_loader.py de-recovery.img de-recovery-phosphor.img

The input must be EZ-Flash's official de-recovery.img (it is checked by md5 before and after),
so this can't produce anything except the one known-good result.
"""
import hashlib, sys

ORIG_MD5 = 'f8510d97f4da06fd5d413a4074e313d9'     # EZ-Flash de-recovery.img
NEW_MD5 = '557c6e79ad576c3054785bd27fa65340'      # after the patch
PATCHES = [  # offset in the image, old 16-bit color, new 16-bit color (GBA BGR555, little endian)
    (0x1DE8, 0x6208, 0x18A5),   # background: bright blue -> Phosphor grey
    (0x1E04, 0x4525, 0x0842),   # text edge:  blue        -> near black
]

def main(src, dst):
    data = bytearray(open(src, 'rb').read())
    if hashlib.md5(data).hexdigest() != ORIG_MD5:
        sys.exit('Not the official EZ-Flash de-recovery.img (md5 differs). Nothing written.')
    for off, old, new in PATCHES:
        if int.from_bytes(data[off:off + 2], 'little') != old:
            sys.exit('Unexpected bytes at 0x%X. Nothing written.' % off)
        data[off:off + 2] = new.to_bytes(2, 'little')
    if hashlib.md5(data).hexdigest() != NEW_MD5:
        sys.exit('Result check failed. Nothing written.')
    open(dst, 'wb').write(data)
    print('Wrote %s (md5 %s)' % (dst, NEW_MD5))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    main(sys.argv[1], sys.argv[2])
