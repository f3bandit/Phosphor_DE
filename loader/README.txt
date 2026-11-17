Phosphor_DE loader recolor - by f3bandit

de-recovery-phosphor.img is EZ-Flash's Omega Definitive Edition loader recovery image
(de-recovery.img, md5 f8510d97f4da06fd5d413a4074e313d9) with exactly 4 bytes changed:
two colors in the loader's palette setup. No code is changed.

  image offset   was                        now
  0x1DE8         0x6208 (blue background)   0x18A5 (Phosphor grey)
  0x1E04         0x4525 (blue text edge)    0x0842 (near black)

md5 of de-recovery-phosphor.img: 557c6e79ad576c3054785bd27fa65340
Check it before writing it to a card.

It turns the bright blue loader screens ("EZ-FLASH UPDATE MODE", the kernel install screen,
"No kernel found") Phosphor grey. It's optional: Phosphor_DE works the same either way.

ONLY for the EZ-Flash Omega Definitive Edition. Never use it on the original Omega or the newer
blue "Omega DE B".

INSTALL
1. Write de-recovery-phosphor.img to a SPARE microSD card with Win32 Disk Imager
   (this wipes that card; Rufus may refuse the image).
2. Put that card in the cart, HOLD SELECT while turning the console on (safe mode),
   choose "Update Loader" and wait for 100%. Don't turn the power off while it writes.
3. Put your normal SD card back. Done.

UNDO
Do the same with EZ-Flash's original de-recovery.img. Safe mode (the SELECT screen) is separate
from the loader and isn't changed by this, so the way back always stays available.

MAKE IT YOURSELF
patch_loader.py makes the same file from your own copy of EZ-Flash's de-recovery.img and checks
the md5 before and after:
  python patch_loader.py de-recovery.img de-recovery-phosphor.img
