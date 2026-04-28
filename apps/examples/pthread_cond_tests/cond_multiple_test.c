/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_multiple_test.c
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

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int shared_data = 0;
static int threads_ready = 0;
static int threads_awake = 0;
static int threads_done = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *waiter_thread(void *arg)
{
	int thread_id = (int)(intptr_t)arg;
	int status;

	printf("Waiter thread %d started\n", thread_id);

	/* Acquire the mutex */
	status = pthread_mutex_lock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed in thread %d: %d\n", thread_id, status);
		return (void *)-1;
	}

	/* Indicate we're ready and waiting */
	threads_ready++;
	printf("Waiter thread %d is ready and waiting (ready count: %d)\n", thread_id, threads_ready);

	/* Wait for the condition */
	status = pthread_cond_wait(&cond, &mutex);
	if (status != 0) {
		printf("ERROR: pthread_cond_wait failed in thread %d: %d\n", thread_id, status);
		pthread_mutex_unlock(&mutex);
		return (void *)-1;
	}

	/* Increment awake counter */
	threads_awake++;
	printf("Waiter thread %d received signal (awake count: %d)\n", thread_id, threads_awake);

	/* Check the shared data */
	if (shared_data != 99) {
		printf("ERROR: Unexpected shared_data value in thread %d: %d\n", thread_id, shared_data);
		pthread_mutex_unlock(&mutex);
		return (void *)-1;
	}

	threads_done++;
	printf("Waiter thread %d done (done count: %d)\n", thread_id, threads_done);

	/* Release the mutex */
	status = pthread_mutex_unlock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed in thread %d: %d\n", thread_id, status);
		return (void *)-1;
	}

	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_multiple_test
 ****************************************************************************/

int cond_multiple_test(void)
{
	pthread_t threads[DEFAULT_NUM_WAITERS];
	int status;
	void *result;
	int i;
	int ret = TEST_PASS;

	TEST_START("Multiple waiters with single signal test");

	/* Initialize the condition variable and mutex */
	pthread_cond_init(&cond, NULL);
	pthread_mutex_init(&mutex, NULL);

	/* Reset test variables */
	shared_data = 0;
	threads_ready = 0;
	threads_awake = 0;
	threads_done = 0;

	/* Create the waiter threads */
	for (i = 0; i < DEFAULT_NUM_WAITERS; i++) {
		status = pthread_create(&threads[i], NULL, waiter_thread, (void *)(intptr_t)i);
		if (status != 0) {
			printf("ERROR: pthread_create failed for thread %d: %d\n", i, status);
			return TEST_FAIL;
		}
	}

	/* Give the waiter threads time to start and acquire the mutex */
	usleep(200000);  // 200ms

	/* Wait until all threads are ready */
	while (threads_ready < DEFAULT_NUM_WAITERS) {
		usleep(10000);  // 10ms
	}

	printf("All %d waiter threads are ready\n", DEFAULT_NUM_WAITERS);

	/* Acquire the mutex */
	status = pthread_mutex_lock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Set the shared data */
	shared_data = 99;
	printf("Signaler thread set data and will signal once\n");

	/* Release the mutex before signaling */
	status = pthread_mutex_unlock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Signal the condition only once */
	status = pthread_cond_signal(&cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_signal failed: %d\n", status);
		return TEST_FAIL;
	}

	printf("Signal sent, waiting for one thread to wake up\n");

	/* Wait for exactly one thread to wake up */
	usleep(100000);  // 100ms for one thread to process

	/* Verify that only one thread was awakened */
	if (threads_awake != 1) {
		printf("ERROR: Expected exactly 1 thread to be awakened, but %d were awakened\n", threads_awake);
		return TEST_FAIL;
	}

	printf("PASS: Only one thread was awakened by signal\n");

	/* Now broadcast to wake the remaining threads */
	status = pthread_mutex_lock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return TEST_FAIL;
	}

	printf("Broadcasting to wake remaining waiter threads\n");
	status = pthread_cond_broadcast(&cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_broadcast failed: %d\n", status);
		pthread_mutex_unlock(&mutex);
		return TEST_FAIL;
	}

	status = pthread_mutex_unlock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Wait for all threads to complete */
	for (i = 0; i < DEFAULT_NUM_WAITERS; i++) {
		status = pthread_join(threads[i], &result);
		if (status != 0) {
			printf("ERROR: pthread_join failed for thread %d: %d\n", i, status);
			return TEST_FAIL;
		}

		if (result != NULL) {
			printf("ERROR: Waiter thread %d returned error\n", i);
			return TEST_FAIL;
		}
	}

	/* Verify all threads completed */
	if (threads_done != DEFAULT_NUM_WAITERS) {
		printf("ERROR: Expected %d threads to complete, but %d completed\n", DEFAULT_NUM_WAITERS, threads_done);
		return TEST_FAIL;
	}

	printf("PASS: All threads completed successfully\n");

	/* Clean up */
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);

	TEST_END("Multiple waiters with single signal test", ret);
	return ret;
}