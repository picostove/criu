#include <string.h>
#include <unistd.h>

#include <linux/elf.h>

#include "types.h"
#include <compel/asm/processor-flags.h>

#include <compel/asm/infect-types.h>
#include "asm/restorer.h"
#include "common/compiler.h"
#include <compel/ptrace.h>
#include "asm/dump.h"
#include "protobuf.h"
#include "images/core.pb-c.h"
#include "images/creds.pb-c.h"
#include "parasite-syscall.h"
#include "log.h"
#include "util.h"
#include "cpu.h"
#include "restorer.h"
#include "compel/infect.h"

#define assign_reg(dst, src, e) dst->e = (__typeof__(dst->e))(src)->e

int save_task_regs(void *x, user_regs_struct_t *regs, user_fpregs_struct_t *fpsimd)
{
	int i;
	CoreEntry *core = x;

	// Save the riscv64 CPU state.
	assign_reg(core->ti_riscv64->gpregs, regs, pc);
	assign_reg(core->ti_riscv64->gpregs, regs, ra);
	assign_reg(core->ti_riscv64->gpregs, regs, sp);
	assign_reg(core->ti_riscv64->gpregs, regs, gp);
	assign_reg(core->ti_riscv64->gpregs, regs, tp);
	assign_reg(core->ti_riscv64->gpregs, regs, t0);
	assign_reg(core->ti_riscv64->gpregs, regs, t1);
	assign_reg(core->ti_riscv64->gpregs, regs, t2);
	assign_reg(core->ti_riscv64->gpregs, regs, s0);
	assign_reg(core->ti_riscv64->gpregs, regs, s1);
	assign_reg(core->ti_riscv64->gpregs, regs, a0);
	assign_reg(core->ti_riscv64->gpregs, regs, a1);
	assign_reg(core->ti_riscv64->gpregs, regs, a2);
	assign_reg(core->ti_riscv64->gpregs, regs, a3);
	assign_reg(core->ti_riscv64->gpregs, regs, a4);
	assign_reg(core->ti_riscv64->gpregs, regs, a5);
	assign_reg(core->ti_riscv64->gpregs, regs, a6);
	assign_reg(core->ti_riscv64->gpregs, regs, a7);
	assign_reg(core->ti_riscv64->gpregs, regs, s2);
	assign_reg(core->ti_riscv64->gpregs, regs, s3);
	assign_reg(core->ti_riscv64->gpregs, regs, s4);
	assign_reg(core->ti_riscv64->gpregs, regs, s5);
	assign_reg(core->ti_riscv64->gpregs, regs, s6);
	assign_reg(core->ti_riscv64->gpregs, regs, s7);
	assign_reg(core->ti_riscv64->gpregs, regs, s8);
	assign_reg(core->ti_riscv64->gpregs, regs, s9);
	assign_reg(core->ti_riscv64->gpregs, regs, s10);
	assign_reg(core->ti_riscv64->gpregs, regs, s11);
	assign_reg(core->ti_riscv64->gpregs, regs, t3);
	assign_reg(core->ti_riscv64->gpregs, regs, t4);
	assign_reg(core->ti_riscv64->gpregs, regs, t5);
	assign_reg(core->ti_riscv64->gpregs, regs, t6);

	// Save the FP/SIMD state
	for (i = 0; i < 64; ++i) {
		core->ti_riscv64->fpsimd->fpregs[i] = fpsimd->q.f[i];
	}
	core->ti_riscv64->fpsimd->fcsr = fpsimd->q.fcsr;

	return 0;
}

int arch_alloc_thread_info(CoreEntry *core)
{
	ThreadInfoRiscv64 *ti_riscv64;
	UserRiscv64RegsEntry *gpregs;
	UserRiscv64FpsimdContextEntry *fpsimd;

	ti_riscv64 = xmalloc(sizeof(*ti_riscv64));
	if (!ti_riscv64)
		goto err;
	thread_info_riscv64__init(ti_riscv64);
	core->ti_riscv64 = ti_riscv64;

	gpregs = xmalloc(sizeof(*gpregs));
	if (!gpregs)
		goto err;
	user_riscv64_regs_entry__init(gpregs);

	ti_riscv64->gpregs = gpregs;

	fpsimd = xmalloc(sizeof(*fpsimd));
	if (!fpsimd)
		goto err;
	user_riscv64_fpsimd_context_entry__init(fpsimd);
	ti_riscv64->fpsimd = fpsimd;
	fpsimd->fpregs = xmalloc(64 * sizeof(fpsimd->fpregs[0]));
	fpsimd->n_fpregs = 64;
	if (!fpsimd->fpregs)
		goto err;

	return 0;
err:
	return -1;
}

void arch_free_thread_info(CoreEntry *core)
{
	if (CORE_THREAD_ARCH_INFO(core)) {
		if (CORE_THREAD_ARCH_INFO(core)->fpsimd) {
			xfree(CORE_THREAD_ARCH_INFO(core)->fpsimd->fpregs);
			xfree(CORE_THREAD_ARCH_INFO(core)->fpsimd);
		}
		xfree(CORE_THREAD_ARCH_INFO(core)->gpregs);
		xfree(CORE_THREAD_ARCH_INFO(core));
		CORE_THREAD_ARCH_INFO(core) = NULL;
	}
}

int restore_fpu(struct rt_sigframe *sigframe, CoreEntry *core)
{
	int i;
	user_fpregs_struct_t *fpsimd = RT_SIGFRAME_FPU(sigframe);

	if (core->ti_riscv64->fpsimd->n_fpregs != 64)
		return 1;

	for (i = 0; i < 64; ++i)
		fpsimd->q.f[i] = core->ti_riscv64->fpsimd->fpregs[i];
	fpsimd->q.fcsr = core->ti_riscv64->fpsimd->fcsr;

	fpsimd->q.reserved[0] = 0;
	fpsimd->q.reserved[1] = 0;
	fpsimd->q.reserved[2] = 0;

	return 0;
}

int restore_gpregs(struct rt_sigframe *f, UserRegsEntry *r)
{
	user_regs_struct_t *gpregs = RT_SIGFRAME_CPU(f);
#define CPREG1(d) gpregs->d = r->d

	CPREG1(pc);
	CPREG1(ra);
	CPREG1(sp);
	CPREG1(gp);
	CPREG1(tp);
	CPREG1(t0);
	CPREG1(t1);
	CPREG1(t2);
	CPREG1(s0);
	CPREG1(s1);
	CPREG1(a0);
	CPREG1(a1);
	CPREG1(a2);
	CPREG1(a3);
	CPREG1(a4);
	CPREG1(a5);
	CPREG1(a6);
	CPREG1(a7);
	CPREG1(s2);
	CPREG1(s3);
	CPREG1(s4);
	CPREG1(s5);
	CPREG1(s6);
	CPREG1(s7);
	CPREG1(s8);
	CPREG1(s9);
	CPREG1(s10);
	CPREG1(s11);
	CPREG1(t3);
	CPREG1(t4);
	CPREG1(t5);
	CPREG1(t6);

#undef CPREG1

	return 0;
}
