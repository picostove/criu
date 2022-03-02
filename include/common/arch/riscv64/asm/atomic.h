#ifndef __CR_ATOMIC_H__
#define __CR_ATOMIC_H__

typedef struct {
	int counter;
} atomic_t;

/* Copied from the Linux header arch/riscv/include/asm/barrier.h */

#define nop()		__asm__ __volatile__ ("nop")

#define RISCV_FENCE(p, s) \
	__asm__ __volatile__ ("fence " #p "," #s : : : "memory")

/* These barriers need to enforce ordering on both devices or memory. */
#define mb()		RISCV_FENCE(iorw,iorw)
#define rmb()		RISCV_FENCE(ir,ir)
#define wmb()		RISCV_FENCE(ow,ow)

/* These barriers do not need to enforce ordering on devices, just memory. */
#define __smp_mb()	RISCV_FENCE(rw,rw)
#define __smp_rmb()	RISCV_FENCE(r,r)
#define __smp_wmb()	RISCV_FENCE(w,w)

#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	RISCV_FENCE(rw,w);						\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	RISCV_FENCE(r,rw);						\
	___p1;								\
})

/*
 * This is a very specific barrier: it's currently only used in two places in
 * the kernel, both in the scheduler.  See include/linux/spinlock.h for the two
 * orderings it guarantees, but the "critical section is RCsc" guarantee
 * mandates a barrier on RISC-V.  The sequence looks like:
 *
 *    lr.aq lock
 *    sc    lock <= LOCKED
 *    smp_mb__after_spinlock()
 *    // critical section
 *    lr    lock
 *    sc.rl lock <= UNLOCKED
 *
 * The AQ/RL pair provides a RCpc critical section, but there's not really any
 * way we can take advantage of that here because the ordering is only enforced
 * on that one lock.  Thus, we're just doing a full fence.
 *
 * Since we allow writeX to be called from preemptive regions we need at least
 * an "o" in the predecessor set to ensure device writes are visible before the
 * task is marked as available for scheduling on a new hart.  While I don't see
 * any concrete reason we need a full IO fence, it seems safer to just upgrade
 * this in order to avoid any IO crossing a scheduling boundary.  In both
 * instances the scheduler pairs this with an mb(), so nothing is necessary on
 * the new hart.
 */
#define smp_mb__after_spinlock()	RISCV_FENCE(iorw,iorw)


/* Copied from the Linux kernel header arch/riscv/include/asm/atomic.h */

#define __atomic_acquire_fence()					\
	__asm__ __volatile__(RISCV_ACQUIRE_BARRIER "" ::: "memory")

#define __atomic_release_fence()					\
	__asm__ __volatile__(RISCV_RELEASE_BARRIER "" ::: "memory");

static __always_inline int arch_atomic_read(const atomic_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void arch_atomic_set(atomic_t *v, int i)
{
	WRITE_ONCE(v->counter, i);
}

#ifndef CONFIG_GENERIC_ATOMIC64
#define ATOMIC64_INIT(i) { (i) }
static __always_inline s64 arch_atomic64_read(const atomic64_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void arch_atomic64_set(atomic64_t *v, s64 i)
{
	WRITE_ONCE(v->counter, i);
}
#endif

/*
 * First, the atomic ops that have no ordering constraints and therefor don't
 * have the AQ or RL bits set.  These don't return anything, so there's only
 * one version to worry about.
 */
#define ATOMIC_OP(op, asm_op, I, asm_type, c_type, prefix)		\
static __always_inline							\
void arch_atomic##prefix##_##op(c_type i, atomic##prefix##_t *v)	\
{									\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type " zero, %1, %0"	\
		: "+A" (v->counter)					\
		: "r" (I)						\
		: "memory");						\
}									\

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_OP (op, asm_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_OP (op, asm_op, I, w, int,   )				\
        ATOMIC_OP (op, asm_op, I, d, s64, 64)
#endif

ATOMIC_OPS(add, add,  i)
ATOMIC_OPS(sub, add, -i)
ATOMIC_OPS(and, and,  i)
ATOMIC_OPS( or,  or,  i)
ATOMIC_OPS(xor, xor,  i)

#undef ATOMIC_OP
#undef ATOMIC_OPS

/*
 * Atomic ops that have ordered, relaxed, acquire, and release variants.
 * There's two flavors of these: the arithmatic ops have both fetch and return
 * versions, while the logical ops only have fetch versions.
 */
#define ATOMIC_FETCH_OP(op, asm_op, I, asm_type, c_type, prefix)	\
static __always_inline							\
c_type arch_atomic##prefix##_fetch_##op##_relaxed(c_type i,		\
					     atomic##prefix##_t *v)	\
{									\
	register c_type ret;						\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type " %1, %2, %0"	\
		: "+A" (v->counter), "=r" (ret)				\
		: "r" (I)						\
		: "memory");						\
	return ret;							\
}									\
static __always_inline							\
c_type arch_atomic##prefix##_fetch_##op(c_type i, atomic##prefix##_t *v)	\
{									\
	register c_type ret;						\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type ".aqrl  %1, %2, %0"	\
		: "+A" (v->counter), "=r" (ret)				\
		: "r" (I)						\
		: "memory");						\
	return ret;							\
}

#define ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_type, c_type, prefix)	\
static __always_inline							\
c_type arch_atomic##prefix##_##op##_return_relaxed(c_type i,		\
					      atomic##prefix##_t *v)	\
{									\
        return arch_atomic##prefix##_fetch_##op##_relaxed(i, v) c_op I;	\
}									\
static __always_inline							\
c_type arch_atomic##prefix##_##op##_return(c_type i, atomic##prefix##_t *v)	\
{									\
        return arch_atomic##prefix##_fetch_##op(i, v) c_op I;		\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, c_op, I)					\
        ATOMIC_FETCH_OP( op, asm_op,       I, w, int,   )		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, c_op, I)					\
        ATOMIC_FETCH_OP( op, asm_op,       I, w, int,   )		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, w, int,   )		\
        ATOMIC_FETCH_OP( op, asm_op,       I, d, s64, 64)		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, d, s64, 64)
#endif

ATOMIC_OPS(add, add, +,  i)
ATOMIC_OPS(sub, add, +, -i)

#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_add_return		arch_atomic_add_return
#define arch_atomic_sub_return		arch_atomic_sub_return

#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_add		arch_atomic_fetch_add
#define arch_atomic_fetch_sub		arch_atomic_fetch_sub

#ifndef CONFIG_GENERIC_ATOMIC64
#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_add_return		arch_atomic64_add_return
#define arch_atomic64_sub_return		arch_atomic64_sub_return

#define arch_atomic64_fetch_add_relaxed	arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_sub_relaxed	arch_atomic64_fetch_sub_relaxed
#define arch_atomic64_fetch_add		arch_atomic64_fetch_add
#define arch_atomic64_fetch_sub		arch_atomic64_fetch_sub
#endif

#undef ATOMIC_OPS

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_FETCH_OP(op, asm_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_FETCH_OP(op, asm_op, I, w, int,   )			\
        ATOMIC_FETCH_OP(op, asm_op, I, d, s64, 64)
#endif

ATOMIC_OPS(and, and, i)
ATOMIC_OPS( or,  or, i)
ATOMIC_OPS(xor, xor, i)

#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed
#define arch_atomic_fetch_and		arch_atomic_fetch_and
#define arch_atomic_fetch_or		arch_atomic_fetch_or
#define arch_atomic_fetch_xor		arch_atomic_fetch_xor

#ifndef CONFIG_GENERIC_ATOMIC64
#define arch_atomic64_fetch_and_relaxed	arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_or_relaxed	arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_xor_relaxed	arch_atomic64_fetch_xor_relaxed
#define arch_atomic64_fetch_and		arch_atomic64_fetch_and
#define arch_atomic64_fetch_or		arch_atomic64_fetch_or
#define arch_atomic64_fetch_xor		arch_atomic64_fetch_xor
#endif

#undef ATOMIC_OPS

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN

/* This is required to provide a full barrier on success. */
static __always_inline int arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w     %[p],  %[c]\n"
		"	beq      %[p],  %[u], 1f\n"
		"	add      %[rc], %[p], %[a]\n"
		"	sc.w.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		"	fence    rw, rw\n"
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
       s64 prev;
       long rc;

	__asm__ __volatile__ (
		"0:	lr.d     %[p],  %[c]\n"
		"	beq      %[p],  %[u], 1f\n"
		"	add      %[rc], %[p], %[a]\n"
		"	sc.d.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		"	fence    rw, rw\n"
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless
#endif

/*
 * atomic_{cmp,}xchg is required to have exactly the same ordering semantics as
 * {cmp,}xchg and the operations that return, so they need a full barrier.
 */
#define ATOMIC_OP(c_t, prefix, size)					\
static __always_inline							\
c_t arch_atomic##prefix##_xchg_relaxed(atomic##prefix##_t *v, c_t n)	\
{									\
	return __xchg_relaxed(&(v->counter), n, size);			\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_xchg_acquire(atomic##prefix##_t *v, c_t n)	\
{									\
	return __xchg_acquire(&(v->counter), n, size);			\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_xchg_release(atomic##prefix##_t *v, c_t n)	\
{									\
	return __xchg_release(&(v->counter), n, size);			\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_xchg(atomic##prefix##_t *v, c_t n)		\
{									\
	return __xchg(&(v->counter), n, size);				\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_cmpxchg_relaxed(atomic##prefix##_t *v,	\
				     c_t o, c_t n)			\
{									\
	return __cmpxchg_relaxed(&(v->counter), o, n, size);		\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_cmpxchg_acquire(atomic##prefix##_t *v,	\
				     c_t o, c_t n)			\
{									\
	return __cmpxchg_acquire(&(v->counter), o, n, size);		\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_cmpxchg_release(atomic##prefix##_t *v,	\
				     c_t o, c_t n)			\
{									\
	return __cmpxchg_release(&(v->counter), o, n, size);		\
}									\
static __always_inline							\
c_t arch_atomic##prefix##_cmpxchg(atomic##prefix##_t *v, c_t o, c_t n)	\
{									\
	return __cmpxchg(&(v->counter), o, n, size);			\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS()							\
	ATOMIC_OP(int,   , 4)
#else
#define ATOMIC_OPS()							\
	ATOMIC_OP(int,   , 4)						\
	ATOMIC_OP(s64, 64, 8)
#endif

ATOMIC_OPS()

#define arch_atomic_xchg_relaxed	arch_atomic_xchg_relaxed
#define arch_atomic_xchg_acquire	arch_atomic_xchg_acquire
#define arch_atomic_xchg_release	arch_atomic_xchg_release
#define arch_atomic_xchg		arch_atomic_xchg
#define arch_atomic_cmpxchg_relaxed	arch_atomic_cmpxchg_relaxed
#define arch_atomic_cmpxchg_acquire	arch_atomic_cmpxchg_acquire
#define arch_atomic_cmpxchg_release	arch_atomic_cmpxchg_release
#define arch_atomic_cmpxchg		arch_atomic_cmpxchg

#undef ATOMIC_OPS
#undef ATOMIC_OP

static __always_inline int arch_atomic_sub_if_positive(atomic_t *v, int offset)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w     %[p],  %[c]\n"
		"	sub      %[rc], %[p], %[o]\n"
		"	bltz     %[rc], 1f\n"
		"	sc.w.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		"	fence    rw, rw\n"
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [o]"r" (offset)
		: "memory");
	return prev - offset;
}

#define arch_atomic_dec_if_positive(v)	arch_atomic_sub_if_positive(v, 1)

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline s64 arch_atomic64_sub_if_positive(atomic64_t *v, s64 offset)
{
       s64 prev;
       long rc;

	__asm__ __volatile__ (
		"0:	lr.d     %[p],  %[c]\n"
		"	sub      %[rc], %[p], %[o]\n"
		"	bltz     %[rc], 1f\n"
		"	sc.d.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		"	fence    rw, rw\n"
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [o]"r" (offset)
		: "memory");
	return prev - offset;
}

#define arch_atomic64_dec_if_positive(v)	arch_atomic64_sub_if_positive(v, 1)
#endif

#endif /* __CR_ATOMIC_H__ */
