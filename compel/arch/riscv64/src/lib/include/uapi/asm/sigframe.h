#ifndef UAPI_COMPEL_ASM_SIGFRAME_H__
#define UAPI_COMPEL_ASM_SIGFRAME_H__

#include <asm/ptrace.h>
#include <sys/ptrace.h>

#include <signal.h>

#include <stdint.h>

/*
 * Copied from sigcontext structure in arch/riscv/include/uapi/asm/sigcontext.h
 */
struct rt_sigcontext {
	struct user_regs_struct sc_regs;
	union __riscv_fp_state sc_fpregs;
};

#include <compel/sigframe-common.h>

/* Copied from the kernel source arch/riscv/kernel/signal.c */

struct rt_sigframe {
        siginfo_t info;
        ucontext_t uc;
        uint32_t sigreturn_code[2]; 
};

/* clang-format off */
#define ARCH_RT_SIGRETURN(new_sp, rt_sigframe)					\
	asm volatile(								\
			"mv sp, %0					\n"	\
			"li a7, "__stringify(__NR_rt_sigreturn)"	\n"	\
			"ecall						\n"	\
			:							\
			: "r"(new_sp)						\
			: "a7", "memory")
/* clang-format on */

#define RT_SIGFRAME_UC(rt_sigframe)	     (&(rt_sigframe)->uc)

static inline struct rt_sigcontext* RT_SIGFRAME_SIGCONTEXT(struct rt_sigframe* sigframe) {
	return (struct rt_sigcontext *)&sigframe->uc.uc_mcontext;
}

static inline long unsigned int RT_SIGFRAME_REGIP(struct rt_sigframe* sigframe) {
	struct rt_sigcontext* sigctx = RT_SIGFRAME_SIGCONTEXT(sigframe);
	return (long unsigned int)sigctx->sc_regs.pc;
}

static inline struct user_regs_struct* RT_SIGFRAME_CPU(struct rt_sigframe* sigframe) {
	struct rt_sigcontext* sigctx = RT_SIGFRAME_SIGCONTEXT(sigframe);
	return (struct user_regs_struct *)&sigctx->sc_regs;
}

static inline union __riscv_fp_state* RT_SIGFRAME_FPU(struct rt_sigframe* sigframe) {
	struct rt_sigcontext* sigctx = RT_SIGFRAME_SIGCONTEXT(sigframe);
	return (union __riscv_fp_state *)&sigctx->sc_fpregs;
}

#define RT_SIGFRAME_HAS_FPU(rt_sigframe)     (1)
#define RT_SIGFRAME_OFFSET(rt_sigframe)	     0

#define rt_sigframe_erase_sigset(sigframe)	memset(&sigframe->uc.uc_sigmask, 0, sizeof(k_rtsigset_t))
#define rt_sigframe_copy_sigset(sigframe, from) memcpy(&sigframe->uc.uc_sigmask, from, sizeof(k_rtsigset_t))

#endif /* UAPI_COMPEL_ASM_SIGFRAME_H__ */
