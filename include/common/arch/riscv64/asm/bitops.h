#ifndef __CR_ASM_BITOPS_H__
#define __CR_ASM_BITOPS_H__

#include "common/compiler.h"
#include "common/asm-generic/bitops.h"

static inline int
test_and_set_bit(int nr, volatile unsigned long *p) {
	unsigned long res;
	unsigned long mask = (1UL) << (nr % BITS_PER_LONG);
	unsigned int bit_word = nr / BITS_PER_LONG;
	asm volatile (
		"amoor.d.aqrl %0, %2, %1"
		: "=r" (res), "+A" (p[bit_word])
		: "r" (mask)
		: "memory");
	return (res & mask) != 0;
}

#endif /* __CR_ASM_BITOPS_H__ */
