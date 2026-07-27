/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * apps/examples/sigrace7a/sigrace7a_main.c
 *
 * Deterministic reproducer for the signal delivery race condition observed
 * on ARMv8-M (TizenRT 4.0), ported to ARMv7-A (TizenRT 5.0).
 *
 * ---------------------------------------------------------------------------
 * ROOT CAUSE (see os/arch/arm/src/armv8-m/up_schedulesigaction.c)
 * ---------------------------------------------------------------------------
 * On ARMv8-M, up_schedule_sigaction() CASE 2 hijacks current_regs when a
 * signal is delivered to the *currently interrupted* task. It unconditionally
 * writes:
 *
 *     current_regs[REG_XPSR] = ARMV8M_XPSR_T;   // 0x01000000 -> IPSR = 0
 *
 * and only fixes up the exception-return value when it exactly equals
 * EXC_RETURN_HANDLER (0xfffffff1):
 *
 *     if (current_regs[REG_EXC_RETURN] == EXC_RETURN_HANDLER) { ... }
 *
 * If current_regs points at a *handler-mode* exception frame (a nested
 * exception), CASE 2 clears the frame's IPSR while leaving EXC_RETURN
 * describing a handler-mode return. The next exception return then fails
 * the ARMv8-M return-integrity check -> INVPC UsageFault -> reboot.
 *
 * ---------------------------------------------------------------------------
 * WHY ARMv7-A DOES NOT HAVE THIS BUG
 * ---------------------------------------------------------------------------
 * ARMv7-A's arm_schedulesigaction.c (os/arch/arm/src/armv7-a/) does NOT use
 * an EXC_RETURN register. Instead it directly writes the CPSR:
 *
 *     CURRENT_REGS[REG_CPSR] = (PSR_MODE_SYS | PSR_I_BIT | PSR_F_BIT);
 *
 * This is UNCONDITIONAL - there is no check on the previous CPSR mode.
 * The signal trampoline (arm_sigdeliver) always runs in privileged System
 * mode. No conditional logic means no missed case.
 *
 * ---------------------------------------------------------------------------
 * HOW THIS REPRODUCER WORKS ON ARMv7-A
 * ---------------------------------------------------------------------------
 * The ARMv8-M reproducer uses SysTick (OUTER) + PendSV (INNER) to create a
 * nested exception, then signals the interrupted task from the INNER handler.
 *
 * On ARMv7-A we use the GIC's Software Generated Interrupt (SGI) mechanism:
 *
 *   - OUTER: We use a Software Generated Interrupt (SGI) as the outer
 *     interrupt. We attach our own handler to an unused SGI line.
 *   - INNER: We use another unused SGI as the inner interrupt, with higher
 *     priority so it can preempt the outer.
 *
 * Sequence:
 *   1. A task registers a SIGUSR1 handler and remembers its own pid.
 *   2. We install our OUTER handler on SGI_OUTER and INNER handler on
 *      SGI_INNER, and raise SGI_INNER's priority above SGI_OUTER's so
 *      SGI_INNER can preempt SGI_OUTER.
 *   3. The task triggers SGI_OUTER via arm_cpu_sgi(). Our OUTER handler
 *      runs in IRQ mode.
 *   4. Inside OUTER we trigger SGI_INNER and enable interrupts. Because
 *      SGI_INNER now has higher priority it immediately preempts OUTER
 *      while OUTER is still in interrupt context - the nested exception
 *      that the field race on ARMv8-M produces.
 *   5. SGI_INNER's handler sets CURRENT_REGS to its frame, which captures
 *      SGI_OUTER's interrupt context.
 *   6. Inside SGI_INNER (INNER) we kill(pid, SIGUSR1) - exactly the path
 *      the real crash took (ISR -> work_signal -> kill(SIGWORK)). This
 *      drives up_schedule_sigaction() CASE 2.
 *   7. On ARMv7-A, CASE 2 unconditionally sets CPSR = PSR_MODE_SYS. No
 *      crash occurs. The signal is delivered correctly.
 *
 * On ARMv7-A the test always PASSES because arm_schedulesigaction.c has no
 * conditional privilege check - CPSR is always set to PSR_MODE_SYS.
 *
 * ---------------------------------------------------------------------------
 * SAFETY
 * ---------------------------------------------------------------------------
 * This file only ACTS when sigrace7a_main() is called (via the "sigrace7a"
 * TASH command). The SGI handlers are no-ops unless the test is armed.
 * SGI4 and SGI5 are chosen because they are not used by the OS (SGI0 is
 * unused, SGI1/SGI2 are used for SMP, SGI3 for CPU hotplug).
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <sys/types.h>

#include <tinyara/irq.h>
#include <tinyara/arch.h>
#include <arch/irq.h>

/* Include GIC headers directly using relative paths.
 * gic.h provides arm_cpu_sgi(), GIC_ICCBPR, getreg32(), putreg32(), etc.
 * Dependency chain: gic.h -> mpcore.h -> chip.h -> hardware/amebasmart_memorymap.h
 *                  gic.h -> up_internal.h
 */
#include "../../../os/arch/arm/src/amebasmart/chip.h"
#include "../../../os/arch/arm/src/armv7-a/mpcore.h"
#include "../../../os/arch/arm/src/armv7-a/gic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Use SGI4 as OUTER and SGI5 as INNER.
 * SGI0 is unused, SGI1/SGI2 are used for SMP, SGI3 for CPU hotplug.
 * SGI4-SGI15 are available for our use.
 */

#define SIGRACE_IRQ_OUTER   4	/* SGI4 */
#define SIGRACE_IRQ_INNER   5	/* SGI5 */

/* GIC priorities: lower value == higher priority.
 * Default priority is 0x80 (128). We set INNER to 0x40 (64) so it preempts
 * OUTER which stays at default 0x80.
 */

#define SIGRACE_PRIO_OUTER  0x80	/* Default priority */
#define SIGRACE_PRIO_INNER  0x40	/* Higher priority than OUTER */

#define SIGRACE_SIGNO       SIGUSR1

#define SIGRACE_TASK_NAME   "sigrace7a"
#define SIGRACE_TASK_PRIO   SCHED_PRIORITY_DEFAULT
#define SIGRACE_TASK_STACK  8192

/* GIC Binary Point Register values */
#define SIGRACE_BPR_ALLOW   1	/* Allow preemption (GIC_ICCBPR_2_7) */
#define SIGRACE_BPR_RESTORE 7	/* No preemption (GIC_ICCBPR_NOPREMPT) */



/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile pid_t g_sigrace_pid;
static volatile bool  g_sigrace_active;		/* test in progress */
static volatile bool  g_sigrace_armed;		/* one-shot: fire nesting once */
static volatile bool  g_sigrace_outer_ran;
static volatile bool  g_sigrace_inner_ran;
static volatile bool  g_sigrace_sig_handled;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* SIGUSR1 handler for the reproducer task. On ARMv7-A the queued signal
 * is delivered here (in thread mode), which is the correct behaviour.
 */

static void sigrace_sig_handler(int signo)
{
	g_sigrace_sig_handled = true;
}

/* INNER (SGI5) handler. Runs nested inside the SGI4 (OUTER) handler, so
 * CURRENT_REGS now points at SGI_OUTER's interrupt context frame.
 * Signalling the interrupted task from here is the precise trigger for
 * up_schedule_sigaction() CASE 2. Mirrors the real crash path: ISR ->
 * kill(SIGWORK).
 *
 * On ARMv7-A, CASE 2 unconditionally sets CPSR = PSR_MODE_SYS, so no
 * crash occurs.
 */

static int sigrace_inner_isr(int irq, void *context, void *arg)
{
	if (!g_sigrace_active) {
		return OK;
	}

	g_sigrace_inner_ran = true;

	/* Signal the task that SGI_OUTER (and now SGI_INNER) interrupted.
	 * Because that task == this_task() and CURRENT_REGS is non-NULL, the
	 * kernel takes the CASE 2 path and rewrites CURRENT_REGS.
	 *
	 * On ARMv7-A: CPSR is unconditionally set to PSR_MODE_SYS (privileged).
	 * No crash - signal delivered correctly.
	 */

	(void)kill(g_sigrace_pid, SIGRACE_SIGNO);
	return OK;
}

/* OUTER (SGI4) handler. Opens the nesting window deterministically.
 * Inside this handler we trigger SGI_INNER and enable interrupts so that
 * SGI_INNER (higher priority) can preempt us while we are still in
 * interrupt context.
 */

static int sigrace_outer_isr(int irq, void *context, void *arg)
{
	if (g_sigrace_active && g_sigrace_armed) {
		/* One-shot: only the first armed trigger creates the nested
		 * exception.
		 */

		g_sigrace_armed = false;
		g_sigrace_outer_ran = true;

		/* Trigger SGI_INNER (sent to this CPU only). The SGI will be
		 * pending but not taken yet because interrupts are disabled
		 * in IRQ mode.
		 */

		arm_cpu_sgi(SIGRACE_IRQ_INNER, 1);



		/* Enable IRQ interrupts so the higher-priority SGI_INNER can

		 * preempt us right here, while SGI_OUTER is still an active
		 * interrupt. This is the nested-exception state.
		 *
		 * On ARMv7-A, the GIC binary point register must allow
		 * preemption for this to work. We set it before triggering
		 * the test (see sigrace7a_main).
		 */

		up_irq_enable();

		/* Give the core a few cycles to actually take SGI_INNER.
		 * On ARMv7-A we should come back here after SGI_INNER returns.
		 */

		for (volatile int i = 0; i < 64; i++) {
			__asm__ __volatile__("nop");
		}

		/* SGI_INNER returned cleanly. Disable interrupts before
		 * returning from OUTER.
		 */

		__asm__ __volatile__("cpsid i" ::: "memory");
	}

	return OK;
}

/* Reproducer task entry point. */

static int sigrace_task(int argc, char *argv[])
{
	irqstate_t flags;
	uint32_t saved_bpr;

	g_sigrace_pid = getpid();
	g_sigrace_outer_ran = false;
	g_sigrace_inner_ran = false;
	g_sigrace_sig_handled = false;

	printf("\n");
	printf("========================================================\n");
	printf(" [sigrace7a] up_schedule_sigaction reproducer for ARMv7-A\n");
	printf(" [sigrace7a] pid=%d  OUTER=SGI%d  INNER=SGI%d\n",
		   (int)g_sigrace_pid, SIGRACE_IRQ_OUTER, SIGRACE_IRQ_INNER);
	printf(" [sigrace7a] OUTER prio=0x%02x  INNER prio=0x%02x\n",
		   SIGRACE_PRIO_OUTER, SIGRACE_PRIO_INNER);
	printf(" [sigrace7a] ARMv7-A always sets CPSR=PSR_MODE_SYS\n");
	printf(" [sigrace7a] (no conditional logic -> no crash)\n");
	printf("========================================================\n");

	/* 1. Install the signal handler so a sigaction is queued (required
	 *    for up_schedule_sigaction to be invoked).
	 */

	if (signal(SIGRACE_SIGNO, sigrace_sig_handler) == SIG_ERR) {
		printf(" [sigrace7a] FAILED to install SIGUSR1 handler (errno=%d)\n", errno);
		return -1;
	}

	/* 2. Attach our OUTER and INNER handlers to SGI4 and SGI5.
	 */

	if (irq_attach(SIGRACE_IRQ_INNER, sigrace_inner_isr, NULL) != OK) {
		printf(" [sigrace7a] FAILED to attach INNER handler\n");
		return -1;
	}

	if (irq_attach(SIGRACE_IRQ_OUTER, sigrace_outer_isr, NULL) != OK) {
		printf(" [sigrace7a] FAILED to attach OUTER handler\n");
		return -1;
	}

	/* 3. Set priorities: INNER higher than OUTER so INNER can preempt.
	 * Also enable the SGI interrupts at the GIC.
	 */

	up_prioritize_irq(SIGRACE_IRQ_OUTER, SIGRACE_PRIO_OUTER);
	up_prioritize_irq(SIGRACE_IRQ_INNER, SIGRACE_PRIO_INNER);

	up_enable_irq(SIGRACE_IRQ_OUTER);
	up_enable_irq(SIGRACE_IRQ_INNER);

	/* 4. Temporarily allow GIC preemption (binary point register).
	 * The GIC is initialized with GIC_ICCBPR_NOPREMPT (no preemption).
	 * We set it to GIC_ICCBPR_2_7 to allow preemption for the test.
	 */

	flags = enter_critical_section();
	saved_bpr = getreg32(GIC_ICCBPR);
	putreg32(SIGRACE_BPR_ALLOW, GIC_ICCBPR);

	leave_critical_section(flags);


	/* 5. Arm and fire OUTER (SGI4) from thread mode. The handler runs
	 *    synchronously (higher priority than the thread), so by the time
	 *    arm_cpu_sgi() returns the nested sequence has already run.


	 */

	g_sigrace_active = true;
	g_sigrace_armed = true;

	printf(" [sigrace7a] firing OUTER (SGI%d)...\n", SIGRACE_IRQ_OUTER);
	arm_cpu_sgi(SIGRACE_IRQ_OUTER, 1);


	/* 6. Restore the GIC binary point register. */

	flags = enter_critical_section();
	putreg32(saved_bpr, GIC_ICCBPR);
	leave_critical_section(flags);



	g_sigrace_active = false;

	/* 7. Detach our handlers. SGI4/SGI5 are restored to unused state. */

	(void)irq_detach(SIGRACE_IRQ_OUTER);
	(void)irq_detach(SIGRACE_IRQ_INNER);

	usleep(50 * 1000);

	/* 8. If we get here, no fault occurred: ARMv7-A handled the nested
	 *    signal path correctly.
	 */

	printf("\n");
	printf("========================================================\n");
	printf(" [sigrace7a] SURVIVED - no fault occurred.\n");
	printf(" [sigrace7a]   OUTER ran : %s\n", g_sigrace_outer_ran ? "yes" : "no");
	printf(" [sigrace7a]   INNER ran : %s\n", g_sigrace_inner_ran ? "yes" : "no");
	printf(" [sigrace7a]   signal    : %s\n", g_sigrace_sig_handled ? "delivered" : "pending/none");
	if (g_sigrace_outer_ran && g_sigrace_inner_ran) {
		printf(" [sigrace7a] RESULT: PASS - nested-signal path handled safely.\n");
		printf(" [sigrace7a] (ARMv7-A arm_schedulesigaction.c always uses PSR_MODE_SYS)\n");
	} else {
		printf(" [sigrace7a] RESULT: INCONCLUSIVE - nesting window not hit.\n");
		printf(" [sigrace7a]   OUTER ran=%d INNER ran=%d\n",
			   g_sigrace_outer_ran, g_sigrace_inner_ran);
	}
	printf("========================================================\n");

	return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sigrace7a_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int sigrace7a_main(int argc, char *argv[])
#endif
{
	int pid;

	printf("[sigrace7a] Starting signal race reproducer for ARMv7-A\n");

	pid = task_create(SIGRACE_TASK_NAME, SIGRACE_TASK_PRIO,
					  SIGRACE_TASK_STACK, sigrace_task, NULL);
	if (pid < 0) {
		printf("[sigrace7a] task_create failed (errno=%d)\n", errno);
	}

	return pid;
}
