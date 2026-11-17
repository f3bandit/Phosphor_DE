Phosphor_DE {VERSION} - kernel for the EZ-Flash Omega Definitive Edition - by f3bandit
https://github.com/f3bandit/Phosphor_DE   (full guide, tools, source)

FOR: EZ-Flash Omega Definitive Edition (Rev B tested, Rev A untested).
NOT FOR: the original Omega, the newer blue "Omega DE B" (ezbluekernel.bin), or any other cart.

INSTALL
1. Copy everything inside the "SD card" folder to the ROOT of your microSD card:
     ezkernelnew.bin
     SYSTEM\CHEATS\   (the cheat pack: 1,704 games, 31,168 ready-to-play cheats)
   If the card already has a SYSTEM folder, merge them.
2. Put the card in the cart. HOLD R while turning the console on, until the update screen shows.
3. Let it finish. DO NOT turn the power off while it writes.
4. Done. You can delete ezkernelnew.bin from the SD card afterwards.

ezkernelnew.bin: {SIZE} bytes, md5 {MD5}

SD CARD AFTER INSTALL
  \ezkernelnew.bin        only needed to install/update
  \SYSTEM\CHEATS\         cheats, one file per game code (BPEE.cht = Pokemon Emerald USA)
  \SYSTEM\IMGS\           box art (optional: EZ-Flash's IMGS pack, not included)
  \SYSTEM\SAVER\          your saves (made by the kernel)  <- back this up
  \SYSTEM\RTS\            savestates (made by the kernel)
  \BACKUP\SAVER\          save backups (made by the kernel)
  your .gba games, in any folders

KEYS
  Power-on: hold R = install/update kernel, hold SELECT = EZ-Flash safe mode
  Menu: L/R tabs, A open, B back. NOR tab START = NOR Check. Settings > About START = dump boot chip
  Cheats: A on/off, SELECT all off, L/R page, B done
  In game: L+R+START = in-game menu / reset to menu, L+R+SELECT = sleep (see Settings > Add-ons)

LOADER RECOLOR (optional, makes the bright blue install screen grey)
  loader\patch_loader.py patches your own copy of EZ-Flash's de-recovery.img (4 bytes, checked by md5).
  Steps in the full guide on GitHub.

GOING BACK
  Put EZ-Flash's official ezkernelnew.bin on the SD root and hold R at power-on.

DISCLAIMER
  Unofficial software, use at your own risk. Provided "AS IS", without warranty of any kind.
  The author is not liable for any damage to your cart, console, SD card or saves, or any other loss.
  Back up SYSTEM\SAVER before flashing. Apache License 2.0 (LICENSE); cheat data credits in NOTICE.
