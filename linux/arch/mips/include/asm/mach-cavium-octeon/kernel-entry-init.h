/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2008 Cavium Networks, Inc
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H
#define __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H


#define CP0_CYCLE_COUNTER $9, 6
#define CP0_CVMCTL_REG $9, 7
#define CP0_CVMMEMCTL_REG $11,7
#define CP0_PRID_REG $15, 0
#define CP0_PRID_OCTEON_PASS1 0x000d0000
#define CP0_PRID_OCTEON_CN30XX 0x000d0200

.macro  kernel_entry_setup
	# Registers set by bootloader:
	# (only 32 bits set by bootloader, all addresses are physical
	# addresses, and need to have the appropriate memory region set
	# by the kernel
	# a0 = argc
	# a1 = argv (kseg0 compat addr)
	# a2 = 1 if init core, zero otherwise
	# a3 = address of boot descriptor block
	.set push
	.set arch=octeon
#ifdef CONFIG_HOTPLUG_CPU
	b	7f

FEXPORT(octeon_hotplug_entry)
	move	a0, zero
	move	a1, zero
	move	a2, zero
	move	a3, zero
7:
#endif
	mfc0	v0, CP0_STATUS
	/* Force 64-bit addressing enabled */
	ori	v0, v0, (ST0_UX | ST0_SX | ST0_KX)
	mtc0	v0, CP0_STATUS

	# Clear the TLB.
	mfc0	v0, $16, 1	# Config1
	dsrl	v0, v0, 25
	andi	v0, v0, 0x3f
	mfc0	v1, $16, 3	# Config3
	bgez	v1, 1f
	mfc0	v1, $16, 4	# Config4
	andi	v1, 0x7f
	dsll	v1, 6
	or	v0, v0, v1
1:				# Number of TLBs in v0

	dmtc0	zero, $2, 0	# EntryLo0
	dmtc0	zero, $3, 0	# EntryLo1
	dmtc0	zero, $5, 0	# PageMask
	dla	t0, 0xffffffff90000000
10:
	dmtc0	t0, $10, 0	# EntryHi
	tlbp
	mfc0	t1, $0, 0	# Index
	bltz	t1, 1f
	tlbr
	dmtc0	zero, $2, 0	# EntryLo0
	dmtc0	zero, $3, 0	# EntryLo1
	dmtc0	zero, $5, 0	# PageMask
	tlbwi			# Make it a 'normal' sized page
	daddiu	t0, t0, 8192
	b	10b
1:
	mtc0	v0, $0, 0	# Index
	tlbwi
	.set	noreorder
	bne	v0, zero, 10b
	 addiu	v0, v0, -1
	.set	reorder

	mtc0	zero, $0, 0	# Index
	dmtc0	zero, $10, 0	# EntryHi

#ifdef CONFIG_MAPPED_KERNEL
	# Set up the TLB index 0 for wired access to kernel.
	# Assume we were loaded with sufficient alignment so that we
	# can cover the image with two pages.
	dla	v0, _end
	dla	s0, _text
	dsubu	v0, v0, s0	# size of image
	move	v1, zero
	li	t1, -1		# shift count.
1:	dsrl	v0, v0, 1	# mask into v1
	dsll	v1, v1, 1
	daddiu	t1, t1, 1
	ori	v1, v1, 1
	bne	v0, zero, 1b
	daddiu	t2, t1, -6
	mtc0	v1, $5, 0	# PageMask
	dla	t3, 0xffffffffc0000000 # kernel address
	dmtc0	t3, $10, 0	# EntryHi
	.set push
	.set noreorder
	.set nomacro
	bal	1f
	nop
1:
	.set pop

	dsra	v0, ra, 31
	daddiu	v0, v0, 1	# if it were a ckseg0 address v0 will be zero.
	beqz	v0, 3f
	dli	v0, 0x07ffffffffffffff	# Otherwise assume xkphys.
	b	2f
3:
	dli	v0, 0x7fffffff

2:	and	ra, ra, v0	# physical address of pc in ra
	dla	v0, 1b
	dsubu	v0, v0, s0	# distance from _text to 1: in v0
	dsubu	ra, ra, v0	# ra is physical address of _text
	dsrl	v1, v1, 1
	nor	v1, v1, zero
	and	ra, ra, v1	# mask it with the page mask
	dsubu	v1, t3, ra	# virtual to physical offset into v1
	dsrlv	v0, ra, t1
	dsllv	v0, v0, t2
	ori	v0, v0, 0x1f
	dmtc0	v0, $2, 0  # EntryLo1
	dsrlv	v0, ra, t1
	daddiu	v0, v0, 1
	dsllv	v0, v0, t2
	ori	v0, v0, 0x1f
	dmtc0	v0, $3, 0  # EntryLo2
	mtc0	$0, $0, 0  # Set index to zero
	tlbwi
	li	v0, 1
	mtc0	v0, $6, 0  # Wired
	dla	v0, phys_to_kernel_offset
	sd	v1, 0(v0)
	dla	v0, kernel_image_end
	li	v1, 2
	dsllv	v1, v1, t1
	daddu	v1, v1, t3
	sd	v1, 0(v0)
	dla	v0, continue_in_mapped_space
	jr	v0

continue_in_mapped_space:
#endif
	# Read the cavium mem control register
	dmfc0   v0, CP0_CVMMEMCTL_REG
	# Clear the lower 6 bits, the CVMSEG size
	dins    v0, $0, 0, 6
	ori     v0, CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE
	dmtc0   v0, CP0_CVMMEMCTL_REG	# Write the cavium mem control register
	dmfc0   v0, CP0_CVMCTL_REG	# Read the cavium control register
#ifdef CONFIG_CAVIUM_OCTEON_HW_FIX_UNALIGNED
	# Disable unaligned load/store support but leave HW fixup enabled
	or  v0, v0, 0x5001
	xor v0, v0, 0x1001
#else
	# Disable unaligned load/store and HW fixup support
	or  v0, v0, 0x5001
	xor v0, v0, 0x5001
#endif
	# First clear off CvmCtl[IPPCI] bit and move the performance
	# counters interrupt to IRQ 6
	li	v1, ~(7 << 7)
	and	v0, v0, v1
	or	v0, v0, 0x300		# (6 << 7)
	# Write the cavium control register
	dmtc0   v0, CP0_CVMCTL_REG
	sync
	# Flush dcache after config change
	cache   9, 0($0)
	# Get my core id
	rdhwr   v0, $0
	# Jump the master to kernel_entry
	bne     a2, zero, octeon_main_processor
	nop

#ifdef CONFIG_SMP

	#
	# All cores other than the master need to wait here for SMP bootstrap
	# to begin
	#

	# This is the variable where the next core to boot os stored
	PTR_LA  t0, octeon_processor_boot
octeon_spin_wait_boot:
	# Get the core id of the next to be booted
	LONG_L  t1, (t0)
	# Keep looping if it isn't me
	bne t1, v0, octeon_spin_wait_boot
	nop
	# Get my GP from the global variable
	PTR_LA  t0, octeon_processor_gp
	LONG_L  gp, (t0)
	# Get my SP from the global variable
	PTR_LA  t0, octeon_processor_sp
	LONG_L  sp, (t0)
	# Set the SP global variable to zero so the master knows we've started
	LONG_S  zero, (t0)
#ifdef __OCTEON__
	syncw
	syncw
#else
	sync
#endif
	# Jump to the normal Linux SMP entry point
	j   smp_bootstrap
	nop
#else /* CONFIG_SMP */

	#
	# Someone tried to boot SMP with a non SMP kernel. All extra cores
	# will halt here.
	#
octeon_wait_forever:
	wait
	b   octeon_wait_forever
	nop

#endif /* CONFIG_SMP */
octeon_main_processor:
	.set pop
.endm

/*
 * Do SMP slave processor setup necessary before we can savely execute C code.
 */
	.macro  smp_slave_setup
	.endm

#endif /* __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H */
