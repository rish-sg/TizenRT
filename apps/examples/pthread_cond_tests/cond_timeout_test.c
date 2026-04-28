/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_timeout_test.c
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

static pthread_mutex_t to_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t to_cond = PTHREAD_COND_INITIALIZER;
static int to_thread_ready = 0;
static int to_timeout_occurred = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *timeout_waiter_thread(void *arg)
{
	struct timespec timeout;
	int status;

	printf("Timeout waiter thread started\n");

	/* Acquire the mutex */
	status = pthread_mutex_lock(&to_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return (void *)-1;
	}

	/* Indicate we're ready and waiting */
	to_thread_ready = 1;
	printf("Timeout waiter thread is ready and waiting\n");

	/* Get current time and add 500ms timeout */
	status = clock_gettime(CLOCK_REALTIME, &timeout);
	if (status != 0) {
		printf("ERROR: clock_gettime failed: %d\n", errno);
		pthread_mutex_unlock(&to_mutex);
		return (void *)-1;
	}

	/* Add 500 milliseconds to the current time */
	timeout.tv_nsec += 500000000;  // 500ms in nanoseconds
	if (timeout.tv_nsec >= 1000000000) {
		timeout.tv_sec++;
		timeout.tv_nsec -= 1000000000;
	}

	printf("Timeout set for 500ms from now\n");

	/* Wait for the condition with timeout */
	status = pthread_cond_timedwait(&to_cond, &to_mutex, &timeout);
	if (status == ETIMEDOUT) {
		printf("SUCCESS: pthread_cond_timedwait timed out as expected\n");
		to_timeout_occurred = 1;
	} else if (status != 0) {
		printf("ERROR: pthread_cond_timedwait failed with unexpected error: %d\n", status);
		pthread_mutex_unlock(&to_mutex);
		return (void *)-1;
	} else {
		printf("ERROR: pthread_cond_timedwait returned successfully when timeout was expected\n");
		pthread_mutex_unlock(&to_mutex);
		return (void *)-1;
	}

	/* Release the mutex */
	status = pthread_mutex_unlock(&to_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed: %d\n", status);
		return (void *)-1;
	}

	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_timeout_test
 ****************************************************************************/

int cond_timeout_test(void)
{
	pthread_t thread;
	int status;
	void *result;
	int ret = TEST_PASS;

	TEST_START("Timeout functionality test");

	/* Initialize the condition variable and mutex */
	pthread_cond_init(&to_cond, NULL);
	pthread_mutex_init(&to_mutex, NULL);

	/* Reset test variables */
	to_thread_ready = 0;
	to_timeout_occurred = 0;

	/* Create the waiter thread */
	status = pthread_create(&thread, NULL, timeout_waiter_thread, NULL);
	if (status != 0) {
		printf("ERROR: pthread_create failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Give the waiter thread time to start and acquire the mutex */
	usleep(100000);  // 100ms

	/* Wait until the thread is ready */
	while (!to_thread_ready) {
		usleep(10000);  // 10ms
	}

	printf("Waiter thread is waiting with timeout, main thread will not signal\n");

	/* Wait for the thread to complete (should timeout after 500ms) */
	status = pthread_join(thread, &result);
	if (status != 0) {
		printf("ERROR: pthread_join failed: %d\n", status);
		return TEST_FAIL;
	}

	if (result != NULL) {
		printf("ERROR: Waiter thread returned error\n");
		return TEST_FAIL;
	}

	/* Verify timeout occurred */
	if (!to_timeout_occurred) {
		printf("ERROR: Timeout was expected but did not occur\n");
		return TEST_FAIL;
	}

	printf("PASS: Timeout occurred as expected\n");

	/* Clean up */
	pthread_cond_destroy(&to_cond);
	pthread_mutex_destroy(&to_mutex);

	TEST_END("Timeout functionality test", ret);
	return ret;
}