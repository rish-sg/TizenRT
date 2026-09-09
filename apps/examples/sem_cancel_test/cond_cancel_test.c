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

static pthread_mutex_t g_test_mutex;
static pthread_cond_t g_test_cond;
static volatile int g_t1_started;
static volatile int g_waiters_before;
static volatile int g_waiters_after;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: t1_cond_waiter

 *
 * Description:
 *   Thread t1 locks mutex, then calls pthread_cond_wait().
 *   It will be canceled while waiting on the condition variable.
 *
 ****************************************************************************/

static void *t1_cond_waiter(void *arg)
{
	int ret;

	printf("[T1] Locking mutex...\n");
	ret = pthread_mutex_lock(&g_test_mutex);
	if (ret != 0) {
		printf("[T1] ERROR: pthread_mutex_lock failed, ret=%d\n", ret);
		return NULL;
	}

	printf("[T1] Calling pthread_cond_wait() (will block)...\n");
	g_t1_started = 1;

	/* This should block forever until canceled */
	ret = pthread_cond_wait(&g_test_cond, &g_test_mutex);

	printf("[T1] pthread_cond_wait() returned %d\n", ret);

	/* If we reach here, we were not canceled */
	pthread_mutex_unlock(&g_test_mutex);
	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_cancel_test_main
 *
 * Description:
 *   Test that pthread_cond_wait() properly restores cond->waiters count
 *   when a thread is canceled while waiting.
 *
 *   Test scenario:
 *   1. Create t1 which calls pthread_cond_wait()
 *   2. Record cond->waiters before cancel
 *   3. Cancel t1 while it's waiting
 *   4. Record cond->waiters after cancel
 *   5. Verify waiters count is consistent (not leaked)
 *
 ****************************************************************************/

int cond_cancel_test_main(int argc, char *argv[])
{
	pthread_t t1;
	pthread_attr_t attr;
	void *t1_result;
	int ret;
	int test_pass = 1;

	printf("\n========================================\n");
	printf("pthread_cond_wait() Cancellation Test\n");
	printf("Waiter Count Consistency Check\n");
	printf("========================================\n\n");

	/* Initialize mutex and condition variable */
	ret = pthread_mutex_init(&g_test_mutex, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_mutex_init failed, ret=%d\n", ret);
		return -1;
	}

	ret = pthread_cond_init(&g_test_cond, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_cond_init failed, ret=%d\n", ret);
		pthread_mutex_destroy(&g_test_mutex);
		return -1;
	}

	g_t1_started = 0;

	/* Set up thread attributes */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, TEST_STACKSIZE);

	/* Create t1 - will block in pthread_cond_wait() */
	printf("[MAIN] Creating t1 (will block in pthread_cond_wait)...\n");
	ret = pthread_create(&t1, &attr, t1_cond_waiter, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_create(t1) failed, ret=%d\n", ret);
		goto cleanup;
	}

	/* Wait for t1 to start and enter pthread_cond_wait() */
	while (!g_t1_started) {
		usleep(1000);
	}

	/* Give t1 time to actually block in pthread_cond_wait() */
	usleep(100000);

	/* Record waiter count before cancel */
	g_waiters_before = g_test_cond.waiters;
	printf("[MAIN] cond->waiters BEFORE cancel: %d\n", g_waiters_before);

	/* Cancel t1 while it's waiting on the condition variable */
	printf("[MAIN] Canceling t1...\n");
	pthread_cancel(t1);

	/* Wait for t1 to finish */
	pthread_join(t1, &t1_result);
	printf("[MAIN] t1 joined, result=%p (PTHREAD_CANCELED=%p)\n",
	       t1_result, PTHREAD_CANCELED);

	/* Give time for any cleanup to run */
	usleep(50000);

	/* Record waiter count after cancel */
	g_waiters_after = g_test_cond.waiters;
	printf("[MAIN] cond->waiters AFTER cancel:  %d\n", g_waiters_after);

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
	if (g_waiters_after == 0) {
		printf("[PASS] cond->waiters restored to 0 (no leak)\n");
	} else {
		printf("[FAIL] cond->waiters LEAKED! before=%d, after=%d\n",
		       g_waiters_before, g_waiters_after);
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
	pthread_cond_destroy(&g_test_cond);
	pthread_mutex_destroy(&g_test_mutex);

	return test_pass ? 0 : -1;
}
