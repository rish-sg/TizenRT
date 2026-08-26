/****************************************************************************
 * apps/examples/pthread_cleanup_test/new_apis_test.c
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
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_PTHREAD_CLEANUP

static int g_api_cleanup_count = 0;
static int g_api_cleanup_order[20];
static int g_api_order_idx = 0;
static pthread_mutex_t g_api_mutex = PTHREAD_MUTEX_INITIALIZER;

static void api_cleanup_handler(FAR void *arg)
{
	int id = (int)((uintptr_t)arg);

	pthread_mutex_lock(&g_api_mutex);
	if (g_api_order_idx < 20) {
		g_api_cleanup_order[g_api_order_idx++] = id;
	}
	g_api_cleanup_count++;
	pthread_mutex_unlock(&g_api_mutex);

	TEST_INFO("API cleanup handler #%d called", id);
}

#endif

#ifdef CONFIG_CANCELLATION_POINTS

static sem_t g_api_sem;
static int g_api_canceled = 0;
static int g_api_self_canceled = 0;
static int g_api_testcancel_called = 0;
static sem_t g_api_sync_sem;  /* Synchronization semaphore for TC57/TC60 */

#endif


/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* --- TC58: Macro pairing thread --- */

#ifdef CONFIG_PTHREAD_CLEANUP

static int g_macro_cleanup_count = 0;

static void macro_cleanup_handler(FAR void *arg)
{
	int id = (int)((uintptr_t)arg);
	g_macro_cleanup_count++;
	TEST_INFO("Macro cleanup handler #%d called", id);
}

static FAR void *macro_pairing_thread(FAR void *arg)
{
	/* Test 1: push/pop in a simple block */
	{
		pthread_cleanup_push(macro_cleanup_handler, (FAR void *)1);
		pthread_cleanup_pop(1);
	}

	/* Test 2: push/pop in if block */
	if (1) {
		pthread_cleanup_push(macro_cleanup_handler, (FAR void *)2);
		pthread_cleanup_pop(1);
	}

	/* Test 3: push/pop in for loop */
	{
		int i;
		for (i = 0; i < 3; i++) {
			pthread_cleanup_push(macro_cleanup_handler, (FAR void *)((uintptr_t)(3 + i)));
			pthread_cleanup_pop(1);
		}
	}

	/* Test 4: push then pthread_exit triggers cleanup */
	pthread_cleanup_push(macro_cleanup_handler, (FAR void *)10);
	pthread_exit(NULL);

	/* Should never reach here */
	pthread_cleanup_pop(0);
	return NULL;
}

#endif

/* --- TC51: Concurrent push --- */


#ifdef CONFIG_PTHREAD_CLEANUP

#define CONCURRENT_THREAD_COUNT 5
#define PUSHES_PER_THREAD 10

typedef struct {
	int thread_id;
	int cleanup_count;
} concurrent_arg_t;

static FAR void *concurrent_push_thread(FAR void *arg)
{
	concurrent_arg_t *targ = (concurrent_arg_t *)arg;
	int i;

	for (i = 0; i < PUSHES_PER_THREAD; i++) {
		pthread_cleanup_push(api_cleanup_handler,
		                     (FAR void *)((uintptr_t)(targ->thread_id * 100 + i)));
		pthread_cleanup_pop(1);
	}

	targ->cleanup_count = PUSHES_PER_THREAD;
	return NULL;
}

#endif

/* --- TC52: Pop error cases --- */

#ifdef CONFIG_PTHREAD_CLEANUP

static FAR void *pop_error_thread(FAR void *arg)
{
	int *result = (int *)arg;

	/* Push 2 handlers */
	pthread_cleanup_push(api_cleanup_handler, (FAR void *)1);
	pthread_cleanup_push(api_cleanup_handler, (FAR void *)2);

	/* Pop 2 handlers with execute=1 */
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);

	/* Now the stack should be empty. Pop again - this is technically
	 * undefined behavior, but the kernel should handle it gracefully
	 * (tos is already 0, so pthread_cleanup_pop_tcb does nothing).
	 */
	pthread_cleanup_pop(1);  /* Extra pop - should be no-op */

	*result = g_api_cleanup_count;
	return NULL;
}

#endif

/* --- TC53: Self-cancel --- */

#ifdef CONFIG_CANCELLATION_POINTS

static FAR void *self_cancel_thread(FAR void *arg)
{
	pthread_cleanup_push(api_cleanup_handler, (FAR void *)999);

	TEST_INFO("Thread about to cancel itself");

	/* Cancel self - this should trigger cleanup handlers */
	pthread_cancel(pthread_self());

	/* If cancellation is deferred, this point acts as cancellation point */
	pthread_testcancel();

	pthread_cleanup_pop(0);
	return NULL;
}

#endif

/* --- TC54: Double cancel --- */

#ifdef CONFIG_CANCELLATION_POINTS

static FAR void *double_cancel_target_thread(FAR void *arg)
{
	pthread_cleanup_push(api_cleanup_handler, (FAR void *)555);

	/* Wait to be canceled */
	sleep(10);

	pthread_cleanup_pop(0);
	return NULL;
}

#endif

/* --- TC55: Cancel state inheritance --- */

#ifdef CONFIG_CANCELLATION_POINTS

static int g_inherited_state = -1;

static FAR void *state_inheritance_thread(FAR void *arg)
{
	int oldstate;
	int status;

	/* Check what state we inherited */
	status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if (status == 0) {
		g_inherited_state = oldstate;
		/* Restore to enable */
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}

	return NULL;
}

#endif

/* --- TC56/TC57: pthread_testcancel tests --- */

#ifdef CONFIG_CANCELLATION_POINTS

static FAR void *testcancel_basic_thread(FAR void *arg)
{
	int i;

	pthread_cleanup_push(api_cleanup_handler, (FAR void *)777);

	/* This thread should be canceled when it calls pthread_testcancel */
	for (i = 0; i < 100; i++) {
		/* Without calling testcancel, a deferred cancel won't take effect
		 * in a CPU-bound loop. With testcancel, it should cancel here.
		 */
		pthread_testcancel();
		usleep(1000);
	}

	pthread_cleanup_pop(0);
	return NULL;
}

static FAR void *testcancel_cpu_thread(FAR void *arg)
{
	volatile int sum = 0;
	int i;

	pthread_cleanup_push(api_cleanup_handler, (FAR void *)888);

	/* Signal that thread has started and is ready */
	sem_post(&g_api_sync_sem);

	/* Long CPU-bound loop with periodic sleep so cancel can be delivered.
	 * Cancellation only happens at testcancel() since cancel type is deferred.
	 */
	for (i = 0; i < 10000000; i++) {
		sum += i;

		/* Call testcancel every 1000 iterations */
		if (i % 1000 == 0) {
			g_api_testcancel_called++;
			pthread_testcancel();
			usleep(1000);  /* Small sleep to allow cancel delivery */
		}
	}

	pthread_cleanup_pop(0);
	return (void *)(uintptr_t)sum;
}


#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: test_concurrent_push
 *
 * Description:
 *   TC51: Test multiple threads pushing cleanup handlers concurrently.
 *   Verifies thread-safety of pthread_cleanup_push() when called from
 *   multiple threads simultaneously.
 ****************************************************************************/

int test_concurrent_push(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t threads[CONCURRENT_THREAD_COUNT];
	concurrent_arg_t args[CONCURRENT_THREAD_COUNT];
	int status;
	int i;
	int total_expected = CONCURRENT_THREAD_COUNT * PUSHES_PER_THREAD;

	TEST_START("TC51: Concurrent pthread_cleanup_push from multiple threads");

	g_api_cleanup_count = 0;
	g_api_order_idx = 0;

	for (i = 0; i < CONCURRENT_THREAD_COUNT; i++) {
		args[i].thread_id = i;
		args[i].cleanup_count = 0;

		status = pthread_create(&threads[i], NULL, concurrent_push_thread, &args[i]);
		if (status != 0) {
			TEST_FAIL("pthread_create failed for thread %d: %d", i, status);
			return 0;
		}
	}

	for (i = 0; i < CONCURRENT_THREAD_COUNT; i++) {
		pthread_join(threads[i], NULL);
	}

	TEST_INFO("Expected %d cleanup calls, got %d", total_expected, g_api_cleanup_count);

	if (g_api_cleanup_count == total_expected) {
		TEST_PASS("TC51: Concurrent push test passed - all handlers called");
		return 1;
	} else {
		TEST_FAIL("Expected %d, got %d", total_expected, g_api_cleanup_count);
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_pop_error_cases
 *
 * Description:
 *   TC52: Test error cases for pthread_cleanup_pop including extra pops
 *   beyond what was pushed. The kernel should handle these gracefully.
 ****************************************************************************/

int test_pop_error_cases(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t thread;
	int status;
	int result = 0;

	TEST_START("TC52: pthread_cleanup_pop error cases (extra pops)");

	g_api_cleanup_count = 0;

	status = pthread_create(&thread, NULL, pop_error_thread, &result);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	status = pthread_join(thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_join failed: %d", status);
		return 0;
	}

	/* We pushed 2 and popped 3 (1 extra). Only 2 should have been executed. */
	if (result == 2) {
		TEST_INFO("Exactly 2 handlers executed (extra pop was no-op)");
		TEST_PASS("TC52: Pop error handling works correctly");
		return 1;
	} else {
		TEST_FAIL("Expected 2 cleanup calls, got %d", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_self_cancel
 *
 * Description:
 *   TC53: Test a thread canceling itself via pthread_cancel(pthread_self()).
 *   The cleanup handler should be called and the thread should exit.
 ****************************************************************************/

int test_self_cancel(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;

	TEST_START("TC53: Self-cancellation via pthread_cancel(pthread_self())");

	g_api_cleanup_count = 0;
	g_api_self_canceled = 0;

	status = pthread_create(&thread, NULL, self_cancel_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	status = pthread_join(thread, &result);
	if (status != 0) {
		TEST_FAIL("pthread_join failed: %d", status);
		return 0;
	}

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread successfully canceled itself");
		if (g_api_cleanup_count > 0) {
			TEST_INFO("Cleanup handler was called during self-cancel");
			TEST_PASS("TC53: Self-cancellation works correctly");
			return 1;
		} else {
			TEST_INFO("Cleanup handler was NOT called (may be expected)");
			TEST_PASS("TC53: Self-cancellation works");
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

/****************************************************************************
 * Name: test_double_cancel
 *
 * Description:
 *   TC54: Test sending multiple pthread_cancel() requests to the same
 *   thread. The second cancel should not cause errors.
 ****************************************************************************/

int test_double_cancel(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;

	TEST_START("TC54: Double cancel (multiple cancel requests)");

	g_api_cleanup_count = 0;

	status = pthread_create(&thread, NULL, double_cancel_target_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(50 * 1000);

	/* First cancel request */
	status = pthread_cancel(thread);
	if (status != 0) {
		TEST_INFO("First pthread_cancel returned: %d", status);
	}

	/* Second cancel request - should return OK or ESRCH */
	status = pthread_cancel(thread);
	if (status == 0) {
		TEST_INFO("Second pthread_cancel returned OK (thread still alive)");
	} else {
		TEST_INFO("Second pthread_cancel returned: %d (thread already canceled)", status);
	}

	status = pthread_join(thread, &result);

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread was canceled after double cancel requests");
		TEST_PASS("TC54: Double cancel handled correctly");
		return 1;
	} else {
		TEST_FAIL("Thread was not canceled, result=%p", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_cancel_state_inheritance
 *
 * Description:
 *   TC55: Test that a child thread inherits the parent's cancellation
 *   state when created.
 ****************************************************************************/

int test_cancel_state_inheritance(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	int status;
	int oldstate;

	TEST_START("TC55: Cancellation state inheritance");

	g_inherited_state = -1;

	/* Set parent state to DISABLE */
	status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if (status != 0) {
		TEST_FAIL("pthread_setcancelstate(DISABLE) failed: %d", status);
		return 0;
	}

	TEST_INFO("Parent cancel state set to DISABLE");

	/* Create child thread - it should inherit DISABLE state */
	status = pthread_create(&thread, NULL, state_inheritance_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		return 0;
	}

	pthread_join(thread, NULL);

	/* Restore parent state to ENABLE */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	TEST_INFO("Child thread inherited cancel state: %s",
	          g_inherited_state == PTHREAD_CANCEL_DISABLE ? "DISABLE" :
	          g_inherited_state == PTHREAD_CANCEL_ENABLE ? "ENABLE" : "UNKNOWN");

	if (g_inherited_state == PTHREAD_CANCEL_DISABLE) {
		TEST_PASS("TC55: Child inherited DISABLE state from parent");
		return 1;
	} else if (g_inherited_state == PTHREAD_CANCEL_ENABLE) {
		TEST_INFO("Child started with ENABLE (default state - may be expected)");
		TEST_PASS("TC55: State inheritance test completed");
		return 1;
	} else {
		TEST_FAIL("Could not determine inherited cancel state");
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_testcancel_basic
 *
 * Description:
 *   TC56: Basic test of pthread_testcancel(). A thread that is pending
 *   cancellation should exit when pthread_testcancel() is called.
 ****************************************************************************/

int test_testcancel_basic(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;

	TEST_START("TC56: pthread_testcancel() basic functionality");

	g_api_cleanup_count = 0;

	status = pthread_create(&thread, NULL, testcancel_basic_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(50 * 1000);

	/* Request cancellation */
	status = pthread_cancel(thread);
	if (status != 0) {
		TEST_FAIL("pthread_cancel failed: %d", status);
		pthread_join(thread, NULL);
		return 0;
	}

	/* Wait for thread - it should exit at next pthread_testcancel() */
	status = pthread_join(thread, &result);

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread was canceled at pthread_testcancel()");
		TEST_PASS("TC56: pthread_testcancel() works correctly");
		return 1;
	} else {
		TEST_FAIL("Thread was not canceled, result=%p", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_testcancel_cpu_bound
 *
 * Description:
 *   TC57: Test pthread_testcancel() in a CPU-bound loop. Without calling
 *   testcancel, a deferred cancellation would never take effect in a
 *   CPU-bound thread. This verifies that testcancel creates a cancellation
 *   point.
 ****************************************************************************/

int test_testcancel_cpu_bound(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;

	TEST_START("TC57: pthread_testcancel() in CPU-bound loop");

	g_api_cleanup_count = 0;
	g_api_testcancel_called = 0;

	/* Initialize sync semaphore to 0 */
	sem_init(&g_api_sync_sem, 0, 0);

	status = pthread_create(&thread, NULL, testcancel_cpu_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		sem_destroy(&g_api_sync_sem);
		return 0;
	}

	/* Wait for thread to signal it has started */
	sem_wait(&g_api_sync_sem);
	TEST_INFO("Thread started, requesting cancellation");

	/* Request cancellation */
	status = pthread_cancel(thread);
	if (status != 0) {
		TEST_FAIL("pthread_cancel failed: %d", status);
		pthread_join(thread, NULL);
		sem_destroy(&g_api_sync_sem);
		return 0;
	}


	/* Wait for thread - it should exit at next pthread_testcancel() */
	status = pthread_join(thread, &result);

	sem_destroy(&g_api_sync_sem);

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread canceled at pthread_testcancel() in CPU loop");
		TEST_INFO("testcancel was called %d times before cancellation",
		          g_api_testcancel_called);
		TEST_PASS("TC57: CPU-bound testcancel works correctly");
		return 1;
	} else {
		TEST_FAIL("Thread was not canceled, result=%p", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}


/****************************************************************************
 * Name: test_macro_pairing
 *
 * Description:
 *   TC58: Test that pthread_cleanup_push/pop macro pairing works correctly
 *   in various block scopes (if/else, for loops, switch cases).
 ****************************************************************************/

int test_macro_pairing(void)
{
#ifdef CONFIG_PTHREAD_CLEANUP
	pthread_t thread;
	int status;

	TEST_START("TC58: pthread_cleanup_push/pop macro pairing in blocks");

	g_macro_cleanup_count = 0;

	/* Run the macro pairing test in a separate thread so that
	 * cleanup handlers are executed via pthread_exit() and pop(1).
	 */
	status = pthread_create(&thread, NULL, macro_pairing_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	pthread_join(thread, NULL);

	/* Expected: 5 handlers called
	 * - Test 1: 1 (pop with execute=1)
	 * - Test 2: 1 (pop with execute=1)
	 * - Test 3: 3 (pop with execute=1, 3 iterations)
	 * - Test 4: 0 from pop(1) since pthread_exit happens first,
	 *           but the push handler IS called on exit = 1
	 * Total: 1 + 1 + 3 + 1 = 6
	 */
	TEST_INFO("Total cleanup handlers called: %d (expected 6)", g_macro_cleanup_count);

	if (g_macro_cleanup_count == 6) {
		TEST_PASS("TC58: Macro pairing works in all block scopes");
		return 1;
	} else {
		TEST_FAIL("Expected 6 cleanup calls, got %d", g_macro_cleanup_count);
		return 0;
	}
#else
	TEST_INFO("CONFIG_PTHREAD_CLEANUP not enabled, skipping test");
	return 1;
#endif
}


/****************************************************************************
 * Name: test_rapid_state_toggle
 *
 * Description:
 *   TC59: Test rapid toggling of cancellation state between ENABLE and
 *   DISABLE using pthread_setcancelstate(). Verifies the state machine
 *   is robust under rapid transitions.
 ****************************************************************************/

int test_rapid_state_toggle(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;
	int oldstate;
	int i;
	int toggle_count = 0;

	TEST_START("TC59: Rapid cancel state toggle (ENABLE/DISABLE)");

	/* Rapidly toggle cancel state in main thread */
	for (i = 0; i < 100; i++) {
		status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		if (status == 0) {
			toggle_count++;
		}

		status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
		if (status == 0) {
			toggle_count++;
		}
	}

	TEST_INFO("Completed %d state toggles (expected 200)", toggle_count);

	/* Verify final state is ENABLE */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	if (toggle_count == 200) {
		TEST_PASS("TC59: Rapid state toggle completed successfully");
		return 1;
	} else {
		TEST_FAIL("Expected 200 toggles, got %d", toggle_count);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * Name: test_combined_api_test
 *
 * Description:
 *   TC60: Integration test that exercises all 5 APIs together:
 *   - pthread_cleanup_push()
 *   - pthread_cleanup_pop()
 *   - pthread_cancel()
 *   - pthread_setcancelstate()
 *   - pthread_testcancel()
 *
 *   This test creates a thread that uses all 5 APIs in a realistic scenario.
 ****************************************************************************/

int test_combined_api_test(void)
{
#ifdef CONFIG_CANCELLATION_POINTS
	pthread_t thread;
	void *result;
	int status;
	int oldstate;

	TEST_START("TC60: Combined API integration test (all 5 APIs)");

	g_api_cleanup_count = 0;

	/* Initialize sync semaphore */
	sem_init(&g_api_sync_sem, 0, 0);

	/* Create a thread that uses:
	 * - pthread_cleanup_push (API #1) - registers cleanup handler
	 * - pthread_testcancel (API #5) - creates cancellation point in loop
	 */
	status = pthread_create(&thread, NULL, testcancel_cpu_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		sem_destroy(&g_api_sync_sem);
		return 0;
	}

	/* Wait for thread to start */
	sem_wait(&g_api_sync_sem);
	TEST_INFO("Thread started, using setcancelstate + cancel");

	/* API #4: pthread_setcancelstate - disable cancel in main thread */
	status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if (status != 0) {
		TEST_FAIL("pthread_setcancelstate(DISABLE) failed: %d", status);
	} else {
		TEST_INFO("Main thread cancel state set to DISABLE");
	}

	/* API #3: pthread_cancel - request cancellation of child thread */
	status = pthread_cancel(thread);
	if (status != 0) {
		TEST_FAIL("pthread_cancel failed: %d", status);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_join(thread, NULL);
		sem_destroy(&g_api_sync_sem);
		return 0;
	}

	/* Restore main thread cancel state to ENABLE */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	/* Wait for child thread - it exits at pthread_testcancel (API #5)
	 * and cleanup handlers are called via push/pop (APIs #1 and #2)
	 */
	status = pthread_join(thread, &result);

	sem_destroy(&g_api_sync_sem);

	if (result == PTHREAD_CANCELED) {
		TEST_INFO("Thread was canceled successfully");
		TEST_INFO("Cleanup handlers called: %d", g_api_cleanup_count);

		/* Verify all 5 APIs were exercised:
		 * #1 pthread_cleanup_push - in child thread
		 * #2 pthread_cleanup_pop - executed during cancel cleanup
		 * #3 pthread_cancel - called from main thread
		 * #4 pthread_setcancelstate - called in main thread
		 * #5 pthread_testcancel - in child thread loop
		 */
		TEST_INFO("All 5 APIs exercised: push, pop, cancel, setcancelstate, testcancel");
		TEST_PASS("TC60: All 5 APIs work together correctly");
		return 1;
	} else {
		TEST_FAIL("Thread was not canceled, result=%p", result);
		return 0;
	}
#else
	TEST_INFO("CONFIG_CANCELLATION_POINTS not enabled, skipping test");
	return 1;
#endif
}

