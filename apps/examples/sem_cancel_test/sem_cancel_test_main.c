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
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_EXAMPLES_SEM_CANCEL_TEST_STACKSIZE
#define TEST_STACKSIZE 8192
#else
#define TEST_STACKSIZE CONFIG_EXAMPLES_SEM_CANCEL_TEST_STACKSIZE
#endif

/* Forward declaration for cond cancellation test */
int cond_cancel_test_main(int argc, char *argv[]);

/* Forward declaration for async cancellation test */
int async_cancel_test_main(int argc, char *argv[]);


/****************************************************************************
 * Private Data
 ****************************************************************************/

static sem_t g_test_sem;

static volatile int g_cleanup_called;
static volatile int g_errno_seen;
static volatile int g_t1_started;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cleanup_handler
 *
 * Description:
 *   Cleanup handler registered by t1. Called when t1 is canceled.
 *
 ****************************************************************************/

static void cleanup_handler(void *arg)
{
	printf("[CLEANUP] Cleanup handler called for t1!\n");
	g_cleanup_called = 1;
}

/****************************************************************************
 * Name: t1_thread
 *
 * Description:
 *   Thread t1 blocks on sem_wait(). When canceled by t2, it should:
 *   - NOT see ECANCELED as errno (POSIX: sem_wait returns EINTR)
 *   - Have cleanup handler called
 *   - Exit via pthread_exit(PTHREAD_CANCELED) in leave_cancellation_point()
 *
 ****************************************************************************/

static void *t1_thread(void *arg)
{
	int ret;

	/* Register cleanup handler */
	pthread_cleanup_push(cleanup_handler, NULL);

	/* Signal that t1 has started */
	g_t1_started = 1;

	printf("[T1] Calling sem_wait() (will block)...\n");

	/* This should block forever until canceled */
	ret = sem_wait(&g_test_sem);

	/* If we reach here, sem_wait returned an error */
	g_errno_seen = errno;
	printf("[T1] sem_wait() returned %d, errno=%d (%s)\n",
	       ret, g_errno_seen,
	       g_errno_seen == EINTR ? "EINTR" :
	       g_errno_seen == ECANCELED ? "ECANCELED" :
	       g_errno_seen == ETIMEDOUT ? "ETIMEDOUT" : "OTHER");

	/* DEBUGASSERT: If we get ECANCELED, the fix is not applied.
	 * Per POSIX, sem_wait() should return EINTR when interrupted by
	 * a signal (cancellation is treated as signal interruption).
	 * ECANCELED is NOT a valid errno for sem_wait().
	 */
	DEBUGASSERT(g_errno_seen != ECANCELED);

	/* Pop cleanup handler (won't execute since we're here, not canceled) */
	pthread_cleanup_pop(0);

	return NULL;
}

/****************************************************************************
 * Name: t2_thread
 *
 * Description:
 *   Thread t2 waits for t1 to start, then cancels it.
 *
 ****************************************************************************/

static void *t2_thread(void *arg)
{
	pthread_t t1 = *(pthread_t *)arg;

	/* Wait for t1 to start and block in sem_wait() */
	while (!g_t1_started) {
		usleep(1000);
	}

	/* Give t1 time to actually block in sem_wait() */
	usleep(50000);

	printf("[T2] Canceling t1...\n");
	pthread_cancel(t1);

	printf("[T2] pthread_cancel() returned\n");
	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sem_cancel_test_main
 *
 * Description:
 *   Main entry point for the semaphore cancellation test.
 *
 *   Test scenario:
 *   1. Create t1 which blocks on sem_wait()
 *   2. Create t2 which cancels t1
 *   3. t1 should:
 *      - Have cleanup handler called
 *      - NOT see ECANCELED as errno (should be EINTR per POSIX)
 *      - Exit with PTHREAD_CANCELED
 *
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int sem_cancel_test_main(int argc, char *argv[])
#endif
{
	pthread_t t1;
	pthread_t t2;
	pthread_attr_t attr;
	void *t1_result;
	int ret;
	int test_pass = 1;

	printf("\n========================================\n");
	printf("Semaphore Cancellation Test\n");
	printf("========================================\n\n");

	/* Initialize semaphore at 0 so sem_wait() blocks */
	if (sem_init(&g_test_sem, 0, 0) != 0) {
		printf("[FAIL] sem_init() failed, errno=%d\n", errno);
		return -1;
	}

	g_cleanup_called = 0;
	g_errno_seen = -1;
	g_t1_started = 0;

	/* Set up thread attributes */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, TEST_STACKSIZE);

	/* Create t1 - will block on sem_wait() */
	printf("[MAIN] Creating t1 (will block on sem_wait)...\n");
	ret = pthread_create(&t1, &attr, t1_thread, NULL);
	if (ret != 0) {
		printf("[FAIL] pthread_create(t1) failed, ret=%d\n", ret);
		sem_destroy(&g_test_sem);
		return -1;
	}

	/* Create t2 - will cancel t1 */
	printf("[MAIN] Creating t2 (will cancel t1)...\n");
	ret = pthread_create(&t2, &attr, t2_thread, &t1);
	if (ret != 0) {
		printf("[FAIL] pthread_create(t2) failed, ret=%d\n", ret);
		pthread_cancel(t1);
		pthread_join(t1, NULL);
		sem_destroy(&g_test_sem);
		return -1;
	}

	/* Wait for t2 to finish */
	pthread_join(t2, NULL);
	printf("[MAIN] t2 joined\n");

	/* Wait for t1 to finish (should be canceled) */
	pthread_join(t1, &t1_result);
	printf("[MAIN] t1 joined, result=%p (PTHREAD_CANCELED=%p)\n",
	       t1_result, PTHREAD_CANCELED);

	/* Check results */
	printf("\n--- Test Results ---\n");

	/* Check 1: Cleanup handler was called */
	if (g_cleanup_called) {
		printf("[PASS] Cleanup handler was called\n");
	} else {
		printf("[FAIL] Cleanup handler was NOT called\n");
		test_pass = 0;
	}

	/* Check 2: Thread was canceled (exited with PTHREAD_CANCELED) */
	if (t1_result == PTHREAD_CANCELED) {
		printf("[PASS] t1 was canceled (PTHREAD_CANCELED)\n");
	} else {
		printf("[FAIL] t1 was NOT canceled, result=%p\n", t1_result);
		test_pass = 0;
	}

	/* Check 3: errno was NOT ECANCELED */
	/* Note: If the DEBUGASSERT in t1_thread triggered, we wouldn't reach here.
	 * If we do reach here, the assert didn't fire.
	 */
	if (g_errno_seen == ECANCELED) {
		printf("[FAIL] sem_wait() returned ECANCELED (POSIX violation!)\n");
		test_pass = 0;
	} else if (g_errno_seen == EINTR) {
		printf("[PASS] sem_wait() returned EINTR (POSIX compliant)\n");
	} else if (g_errno_seen == -1) {
		/* Thread was canceled before sem_wait returned - also acceptable */
		printf("[PASS] t1 was canceled inside sem_wait (never returned)\n");
	} else {
		printf("[INFO] sem_wait() returned errno=%d\n", g_errno_seen);
	}

	/* Summary */
	printf("\n========================================\n");
	if (test_pass) {
		printf("RESULT: PASS\n");
	} else {
		printf("RESULT: FAIL\n");
	}
	printf("========================================\n");

	sem_destroy(&g_test_sem);
	pthread_attr_destroy(&attr);

	/* Run the pthread_cond_wait() deferred cancellation test */
	cond_cancel_test_main(argc, argv);

	/* Run the pthread_cond_wait() async cancellation test */
	async_cancel_test_main(argc, argv);

	return test_pass ? 0 : -1;

}

