#!/bin/sh
# Builds sim/sim_disk.img (the test SD card baked into the emulator build) from sim/root.
# Needs mtools and dosfstools. Then: make SIM=1  -> phosphor_de_sim.gba (mGBA only, not for the cart)
cd "$(dirname "$0")/.." || exit 1
rm -f sim/sim_disk.img
truncate -s 16M sim/sim_disk.img
mkfs.vfat -F16 -n SIMSD sim/sim_disk.img >/dev/null
LC_ALL=C.UTF-8 mcopy -s -i sim/sim_disk.img sim/root/* ::/
