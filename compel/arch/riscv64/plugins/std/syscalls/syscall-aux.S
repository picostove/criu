/**
 * This source contains emulation of syscalls
 * that are not implemented in the AArch64 Linux kernel
 */

ENTRY(sys_open)
	mv a3, a2
	mv a2, a1
	mv a1, a0
	li a0, -100
	j   sys_openat
END(sys_open)


ENTRY(sys_mkdir)
	mv a3, a2
	mv a2, a1
	mv a1, a0
	li a0, -100
	j   sys_mkdirat
END(sys_mkdir)


ENTRY(sys_rmdir)
	li a2, 0x200		// flags = AT_REMOVEDIR
	mv a1, a0
	li a0, -100
	j   sys_unlinkat
END(sys_rmdir)


ENTRY(sys_unlink)
	li a2, 0		// flags = 0
	mv a1, a0
	li a0, -100
	j   sys_unlinkat
END(sys_unlink)
