/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_EXAMPLES_SEM_CANCEL_TEST_STACKSIZE
#define TEST_STACKSIZE 8192
#else
#define TEST_STACKSIZE CONFIG_EXAMPLES_SEM_CANCEL_TEST_STACKSIZE
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_async_mutex;
static pthread_cond_t g_async_cond;
static volatile int g_async_t1_started;
static volatile int g_async_waiters_before;
static volatile int g_async_waiters_after;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: t1_async_cond_waiter
 *
 * Description:
 *   Thread t1 sets cancellation type to ASYNCHRONOUS, then calls
 *   pthread_cond_wait(). It will be canceled while waiting.
 *
 *   In async cancellation mode, pthread_cancel() takes the non-deferred
 *   path: it calls pthread_cleanup_popall() and task_terminate()
 *   directly. The thread NEVER returns to pthread_cond_wait().
 *
 *   This means the direct decrement approach in pthread_cond_wait()
 *   will NOT execute, causing a waiter count leak.
 *
 ****************************************************************************/

static void *t1_async_cond_waiter(void *arg)
{
	int ret;

	/* Set cancellation type to ASYNCHRONOUS */
	ret = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	if (ret != 0) {
		printf("[T1] ERROR: pthread_setcanceltype failed, ret=%d\n", ret);
		return NULL;
	}

	printf("[T1] Cancellation type set to ASYNCHRONOUS\n");

	printf("[T1] Locking mutex...\n");
	ret = pthread_mutex_lock(&g_async_mutex);
	if (ret != 0) {
		printf("[T1] ERROR: pthread_mutex_lock failed, ret=%d\n", ret);
		return NULL;
	}

	printf("[T1] Calling pthread_cond_wait() (will block)...\n");
	g_async_t1_started = 1;

	/* This should block forever until canceled.
	 * In async mode, the thread is terminated immediately by
	 * pthread_cancel() and never returns here.
	 */
	ret = pthread_cond_wait(&g_async_cond, &g_async_mutex);

	printf("[T1] pthread_cond_wait() returned %d\n", ret);

	/* If we reach here, we were not canceled */
	pthread_mutex_unlock(&g_async_mutex);
	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: async_cancel_test_main
 *
 * Description:
 *   Test that pthread_cond_wait() properly restores cond->waiters count
 *   when a thread is canceled with ASYNCHRONOUS cancellation type.
 *
 *   Test scenario:
 *   1. Create t1 which sets async cancellation, then calls pthread_cond_wait()
 *   2. Record cond->waiters before cancel
 *   3. Cancel t1 while it's waiting
 *   4. Record cond->waiters after cancel
 *   5. Verify waiters count is consistent (not leaked)
 *
 *   Expected result with direct decrement approach: FAIL
 *     - In async mode, pthread_cancel() terminates the thread immediately
 *     - The thread never returns to pthread_cond_wait() to execute the
 *       direct decrement code
 *     - cond->waiters is NOT decremented → LEAK
 *
 *   Expected result with cleanup handler approach: PASS
 *     - pthread_cancel() calls pthread_cleanup_popall() which runs the
 *       cleanup handler to decrement cond->waiters
 *
 ****************************************************************************/

int async_cancel_test_main(int argc, char *argv[])
{
	pthread_t t1;
	pthread_attr_t attr;
	void *t1_result;
	int ret;
	int test_pass = 1;

	printf("\n========================================\n");
	printf("pthread_cond_wait() Async Cancellation Test\n");
	printf("Waiter Count Consistency Check\n");
	printf("========================================\n\n");

	/* Initialize mutex and condition variable */
	ret = pthread_mutex_init(&g_async_mutex, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_mutex_init failed, ret=%d\n", ret);
		return -1;
	}

	ret = pthread_cond_init(&g_async_cond, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_cond_init failed, ret=%d\n", ret);
		pthread_mutex_destroy(&g_async_mutex);
		return -1;
	}

	g_async_t1_started = 0;

	/* Set up thread attributes */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, TEST_STACKSIZE);

	/* Create t1 - will set async cancel and block in pthread_cond_wait() */
	printf("[MAIN] Creating t1 (async cancel, will block in pthread_cond_wait)...\n");
	ret = pthread_create(&t1, &attr, t1_async_cond_waiter, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_create(t1) failed, ret=%d\n", ret);
		goto cleanup;
	}

	/* Wait for t1 to start and enter pthread_cond_wait() */
	while (!g_async_t1_started) {
		usleep(1000);
	}

	/* Give t1 time to actually block in pthread_cond_wait() */
	usleep(100000);

	/* Record waiter count before cancel */
	g_async_waiters_before = g_async_cond.waiters;
	printf("[MAIN] cond->waiters BEFORE cancel: %d\n", g_async_waiters_before);

	/* Cancel t1 while it's waiting on the condition variable.
	 * In async mode, this takes the non-deferred path in pthread_cancel():
	 * - pthread_cleanup_popall() is called (runs cleanup handlers)
	 * - task_terminate() is called (thread is killed immediately)
	 * The thread NEVER returns to pthread_cond_wait().
	 */
	printf("[MAIN] Canceling t1 (async)...\n");
	pthread_cancel(t1);

	/* Wait for t1 to finish */
	pthread_join(t1, &t1_result);
	printf("[MAIN] t1 joined, result=%p (PTHREAD_CANCELED=%p)\n",
	       t1_result, PTHREAD_CANCELED);

	/* Give time for any cleanup to run */
	usleep(50000);

	/* Record waiter count after cancel */
	g_async_waiters_after = g_async_cond.waiters;
	printf("[MAIN] cond->waiters AFTER cancel:  %d\n", g_async_waiters_after);

	/* Check results */
	printf("\n--- Test Results ---\n");

	/* Check 1: Thread was canceled */
	if (t1_result == PTHREAD_CANCELED) {
		printf("[PASS] t1 was canceled (PTHREAD_CANCELED)\n");
	} else {
		printf("[FAIL] t1 was NOT canceled, result=%p\n", t1_result);
		test_pass = 0;
	}

	/* Check 2: Waiter count was restored */
	if (g_async_waiters_after == 0) {
		printf("[PASS] cond->waiters restored to 0 (no leak)\n");
	} else {
		printf("[FAIL] cond->waiters LEAKED! before=%d, after=%d\n",
		       g_async_waiters_before, g_async_waiters_after);
		printf("[FAIL] This proves direct decrement approach fails for async cancellation.\n");
		printf("[FAIL] The thread was terminated immediately and never returned to\n");
		printf("[FAIL] pthread_cond_wait() to execute the decrement code.\n");
		printf("[FAIL] Use cleanup handler approach for POSIX compliance.\n");
		test_pass = 0;
	}

	/* Summary */
	printf("\n========================================\n");
	if (test_pass) {
		printf("RESULT: PASS\n");
	} else {
		printf("RESULT: FAIL\n");
	}
	printf("========================================\n");

cleanup:
	pthread_attr_destroy(&attr);
	pthread_cond_destroy(&g_async_cond);
	pthread_mutex_destroy(&g_async_mutex);

	return test_pass ? 0 : -1;
}
