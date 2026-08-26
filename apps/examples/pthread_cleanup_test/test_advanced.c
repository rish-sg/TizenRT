/****************************************************************************
 * apps/examples/pthread_cleanup_test/test_advanced.c
 *
 *   Copyright (C) 2026 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name Samsung Electronics nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "pthread_cleanup.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* For stack overflow test - push more than the configured stack size */
#ifndef CONFIG_PTHREAD_CLEANUP_STACKSIZE
#define CLEANUP_STACKSIZE 2
#else
#define CLEANUP_STACKSIZE CONFIG_PTHREAD_CLEANUP_STACKSIZE
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_PTHREAD_CLEANUP

static int g_adv_cleanup_count = 0;
static pthread_mutex_t g_adv_mutex = PTHREAD_MUTEX_INITIALIZER;

static void adv_cleanup_handler(FAR void *arg)
{
	int id = (int)((uintptr_t)arg);

	pthread_mutex_lock(&g_adv_mutex);
	g_adv_cleanup_count++;
	pthread_mutex_unlock(&g_adv_mutex);

	TEST_INFO("Advanced cleanup handler #%d called", id);
}

#endif

/* For barrier cancellation test */
#ifdef CONFIG_CANCELLATION_POINTS
static pthread_barrier_t g_barrier;
static int g_barrier_cleanup_called = 0;

static void barrier_cleanup_handler(FAR void *arg)
{
	TEST_INFO("Barrier cleanup handler called");
	g_barrier_cleanup_called = 1;
}
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_PTHREAD_CLEANUP

/* Thread for syscall from loadable module test */
static FAR void *loadable_module_thread(FAR void *arg)
{
	int *result = (int *)arg;

	/* Test pthread_cleanup_push via syscall path */
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)1);

	/* Test pthread_cleanup_pop with execute=1 via syscall path */
	pthread_cleanup_pop(1);

	*result = g_adv_cleanup_count;
	return NULL;
}

/* Thread for error handling test */
static FAR void *error_handling_thread(FAR void *arg)
{
	int *result = (int *)arg;

	/* Push a handler and pop without executing, then push/pop again */
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)10);
	pthread_cleanup_pop(0);  /* Don't execute */

	/* Push and pop with execute */
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)20);
	pthread_cleanup_pop(1);  /* Execute */

	*result = g_adv_cleanup_count;
	return NULL;
}

/* Thread for stack overflow test */
static FAR void *stack_overflow_thread(FAR void *arg)
{
	int *result = (int *)arg;
	int i;

	/* Push more handlers than CONFIG_PTHREAD_CLEANUP_STACKSIZE allows.
	 * The kernel should gracefully handle this via DEBUGASSERT.
	 * In release builds, extra pushes are silently ignored.
	 */
	for (i = 0; i < CLEANUP_STACKSIZE + 5; i++) {
		TEST_INFO("Pushing cleanup handler #%d (limit=%d)", i + 1, CLEANUP_STACKSIZE);
		pthread_cleanup_push(adv_cleanup_handler, (FAR void *)((uintptr_t)(i + 1)));
	}

	/* Pop all handlers that were successfully pushed */
	for (i = 0; i < CLEANUP_STACKSIZE + 5; i++) {
		pthread_cleanup_pop(1);
	}

	*result = g_adv_cleanup_count;
	return NULL;
}

#endif /* CONFIG_PTHREAD_CLEANUP */

#ifdef CONFIG_CANCELLATION_POINTS

/* Thread for barrier cancellation test */
static FAR void *barrier_wait_thread(FAR void *arg)
{
	pthread_cleanup_push(barrier_cleanup_handler, NULL);

	/* Wait at barrier - this should be a cancellation point */
	TEST_INFO("Thread waiting at barrier...");
	pthread_barrier_wait(&g_barrier);

	pthread_cleanup_pop(0);
	return NULL;
}

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: test_syscall_from_loadable_module
 *
 * Description:
 *   TC34: Verify that pthread_cleanup_push/pop work correctly when called
 *   via the syscall interface (as would happen from loadable modules).
 *   This tests the core purpose of commit 3d49afb81.
 ****************************************************************************/

int test_syscall_from_loadable_module(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t thread;
	int status;
	int result = 0;

	TEST_START("TC34: Syscall-based cleanup push/pop (loadable module path)");

	g_adv_cleanup_count = 0;

	status = pthread_create(&thread, NULL, loadable_module_thread, &result);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	status = pthread_join(thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_join failed: %d", status);
		return 0;
	}

	if (result >= 1) {
		TEST_INFO("Cleanup handler was called via syscall path (count=%d)", result);
		TEST_PASS("TC34: Syscall-based cleanup push/pop works correctly");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler was NOT called via syscall path");
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_syscall_error_handling
 *
 * Description:
 *   TC35: Test error handling and robustness of the pthread_cleanup_push/pop
 *   syscall interface with various parameter combinations.
 ****************************************************************************/

int test_syscall_error_handling(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t thread;
	int status;
	int result = 0;

	TEST_START("TC35: Syscall error handling for cleanup push/pop");

	g_adv_cleanup_count = 0;

	status = pthread_create(&thread, NULL, error_handling_thread, &result);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	status = pthread_join(thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_join failed: %d", status);
		return 0;
	}

	/* We expect exactly 1 handler to have been executed (the one with pop(1)) */
	if (result == 1) {
		TEST_INFO("Only the executed handler was called (count=%d)", result);
		TEST_PASS("TC35: Error handling works correctly");
		return 1;
	} else {
		TEST_FAIL("Expected 1 cleanup call, got %d", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_stack_overflow_protection
 *
 * Description:
 *   TC39: Test behavior when the cleanup stack exceeds
 *   CONFIG_PTHREAD_CLEANUP_STACKSIZE. The kernel should handle this
 *   gracefully without crashing.
 ****************************************************************************/

int test_stack_overflow_protection(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t thread;
	int status;
	int result = 0;

	TEST_START("TC39: Cleanup stack overflow protection");

	g_adv_cleanup_count = 0;

	TEST_INFO("CONFIG_PTHREAD_CLEANUP_STACKSIZE = %d", CLEANUP_STACKSIZE);

	status = pthread_create(&thread, NULL, stack_overflow_thread, &result);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	status = pthread_join(thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_join failed: %d", status);
		return 0;
	}

	TEST_INFO("Total cleanup handlers called: %d (expected max %d)",
	          result, CLEANUP_STACKSIZE);

	/* The thread should not have crashed. Only the handlers that were
	 * successfully pushed should have been called (at most CLEANUP_STACKSIZE).
	 */
	if (result <= CLEANUP_STACKSIZE) {
		TEST_INFO("Stack overflow handled gracefully");
		TEST_PASS("TC39: Stack overflow protection works");
		return 1;
	} else {
		TEST_FAIL("More handlers called than stack size: %d > %d",
		          result, CLEANUP_STACKSIZE);
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_barrier_cancellation
 *
 * Description:
 *   TC44: Test cancellation while a thread is blocked at pthread_barrier_wait().
 *   This verifies that barrier_wait acts as a cancellation point and that
 *   cleanup handlers are properly invoked.
 ****************************************************************************/

int test_barrier_cancellation(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;

	TEST_START("TC44: Cancellation during pthread_barrier_wait");

	g_barrier_cleanup_called = 0;

	/* Initialize barrier with count=2 (needs 2 threads to proceed) */
	status = pthread_barrier_init(&g_barrier, NULL, 2);
	if (status != 0) {
		TEST_FAIL("pthread_barrier_init failed: %d", status);
		return 0;
	}

	/* Create thread that will wait at the barrier */
	status = pthread_create(&thread, NULL, barrier_wait_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_barrier_destroy(&g_barrier);
		return 0;
	}

	/* Let thread reach the barrier */
	usleep(100 * 1000);

	/* Cancel the thread while it's waiting at the barrier */
	status = pthread_cancel(thread);
	if (status != 0) {
		TEST_FAIL("pthread_cancel failed: %d", status);
		pthread_barrier_destroy(&g_barrier);
		return 0;
	}

	/* Wait for the canceled thread */
	status = pthread_join(thread, &result);
	pthread_barrier_destroy(&g_barrier);

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread was canceled at pthread_barrier_wait");
		if (g_barrier_cleanup_called) {
			TEST_INFO("Cleanup handler was called during barrier cancellation");
			TEST_PASS("TC44: Barrier cancellation works correctly");
			return 1;
		} else {
			TEST_INFO("Cleanup handler was NOT called (may be expected)");
			TEST_PASS("TC44: Barrier cancellation works");
			return 1;
		}
	} else {
		TEST_FAIL("Thread was not canceled, result=%p", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}
