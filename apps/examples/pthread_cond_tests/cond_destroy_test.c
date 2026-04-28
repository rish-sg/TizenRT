/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_destroy_test.c
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "pthread_cond_tests.h"
#include <time.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t destroy_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t destroy_cond = PTHREAD_COND_INITIALIZER;
static volatile int destroy_thread_waiting = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *destroy_waiter_thread(void *arg)
{
	int status;
	struct timespec timeout;

	printf("Waiter thread started\n");

	status = pthread_mutex_lock(&destroy_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return (void *)-1;
	}

	destroy_thread_waiting = 1;
	printf("Waiter thread is now waiting on condvar\n");

	/* Use timed wait - if condvar is destroyed, we need a way to exit */
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 3;  /* 3 second timeout */

	status = pthread_cond_timedwait(&destroy_cond, &destroy_mutex, &timeout);
	if (status == ETIMEDOUT) {
		printf("Waiter thread timed out (condvar was destroyed while waiting)\n");
	} else if (status != 0) {
		printf("ERROR: pthread_cond_timedwait failed: %d\n", status);
		pthread_mutex_unlock(&destroy_mutex);
		return (void *)-1;
	} else {
		printf("Waiter thread received signal\n");
	}

	printf("Waiter thread exiting\n");
	pthread_mutex_unlock(&destroy_mutex);
	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_destroy_test
 *
 * Description:
 *   Test pthread_cond_destroy behavior when a thread is waiting.
 *   POSIX defines destroying a condvar with waiting threads as "undefined
 *   behavior". This test demonstrates what happens in TizenRT.
 ****************************************************************************/

int cond_destroy_test(void)
{
	pthread_t thread;
	int status;
	void *result;
	int ret = TEST_PASS;

	TEST_START("Destroy with waiters test");

	/* Initialize */
	pthread_cond_init(&destroy_cond, NULL);
	pthread_mutex_init(&destroy_mutex, NULL);
	destroy_thread_waiting = 0;

	/* Create waiter thread */
	printf("Creating waiter thread...\n");
	status = pthread_create(&thread, NULL, destroy_waiter_thread, NULL);
	if (status != 0) {
		printf("ERROR: pthread_create failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Wait for thread to start waiting */
	while (!destroy_thread_waiting) {
		usleep(10000);  // 10ms
	}

	/* Give a bit more time to ensure thread is blocked in cond_wait */
	usleep(50000);  // 50ms

	/* Destroy condvar while thread is waiting */
	printf("Destroying condvar while thread is waiting...\n");
	status = pthread_cond_destroy(&destroy_cond);
	if (status == 0) {
		printf("NOTE: pthread_cond_destroy succeeded with waiting threads\n");
		printf("      POSIX defines this as undefined behavior\n");
	} else if (status == EBUSY) {
		printf("pthread_cond_destroy returned EBUSY (POSIX compliant)\n");
	} else {
		printf("ERROR: pthread_cond_destroy returned unexpected error: %d\n", status);
		ret = TEST_FAIL;
	}

	/* 
	 * Without a timed wait, the waiter would block forever since the
	 * condvar was destroyed. Wait for the timeout to complete the test.
	 */
	printf("Waiting for waiter thread to timeout...\n");
	printf("  (Without timeout, thread would block forever - condvar destroyed)\n");

	/* Wait for thread to complete */
	status = pthread_join(thread, &result);
	if (status != 0) {
		printf("ERROR: pthread_join failed: %d\n", status);
		ret = TEST_FAIL;
	}

	if (result != NULL) {
		printf("ERROR: Waiter thread returned error\n");
		ret = TEST_FAIL;
	}

	/* Clean up mutex */
	pthread_mutex_destroy(&destroy_mutex);

	TEST_END("Destroy with waiters test", ret);
	return ret;
}