@ Phosphor_DE emulator build: test SD card image (only assembled with SIM=1)
#ifdef SIM_EZ
	.section .rodata
	.align 4
	.global sim_disk, sim_disk_size
sim_disk:
	.incbin SIM_DISK
sim_disk_end:
	.align 2
sim_disk_size:
	.word sim_disk_end - sim_disk
#endif
