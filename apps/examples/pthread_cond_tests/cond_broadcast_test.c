/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_broadcast_test.c
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

static pthread_mutex_t bcast_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bcast_cond = PTHREAD_COND_INITIALIZER;
static int bcast_shared_data = 0;
static int bcast_threads_ready = 0;
static int bcast_threads_awake = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *broadcast_waiter_thread(void *arg)
{
	int thread_id = (int)(intptr_t)arg;
	int status;

	printf("Waiter thread %d started\n", thread_id);

	/* Acquire the mutex */
	status = pthread_mutex_lock(&bcast_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed in thread %d: %d\n", thread_id, status);
		return (void *)-1;
	}

	/* Indicate we're ready and waiting */
	bcast_threads_ready++;
	printf("Waiter thread %d is ready and waiting (ready count: %d)\n", thread_id, bcast_threads_ready);

	/* Wait for the condition */
	status = pthread_cond_wait(&bcast_cond, &bcast_mutex);
	if (status != 0) {
		printf("ERROR: pthread_cond_wait failed in thread %d: %d\n", thread_id, status);
		pthread_mutex_unlock(&bcast_mutex);
		return (void *)-1;
	}

	/* Increment awake counter */
	bcast_threads_awake++;
	printf("Waiter thread %d received broadcast (awake count: %d)\n", thread_id, bcast_threads_awake);

	/* Check the shared data */
	if (bcast_shared_data != 123) {
		printf("ERROR: Unexpected shared_data value in thread %d: %d\n", thread_id, bcast_shared_data);
		pthread_mutex_unlock(&bcast_mutex);
		return (void *)-1;
	}

	/* Release the mutex */
	status = pthread_mutex_unlock(&bcast_mutex);
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
 * Name: cond_broadcast_test
 ****************************************************************************/

int cond_broadcast_test(void)
{
	pthread_t threads[DEFAULT_NUM_WAITERS];
	int status;
	void *result;
	int i;
	int ret = TEST_PASS;

	TEST_START("Broadcast functionality test");

	/* Initialize the condition variable and mutex */
	pthread_cond_init(&bcast_cond, NULL);
	pthread_mutex_init(&bcast_mutex, NULL);

	/* Reset test variables */
	bcast_shared_data = 0;
	bcast_threads_ready = 0;
	bcast_threads_awake = 0;

	/* Create the waiter threads */
	for (i = 0; i < DEFAULT_NUM_WAITERS; i++) {
		status = pthread_create(&threads[i], NULL, broadcast_waiter_thread, (void *)(intptr_t)i);
		if (status != 0) {
			printf("ERROR: pthread_create failed for thread %d: %d\n", i, status);
			return TEST_FAIL;
		}
	}

	/* Give the waiter threads time to start and acquire the mutex */
	usleep(200000);  // 200ms

	/* Wait until all threads are ready */
	while (bcast_threads_ready < DEFAULT_NUM_WAITERS) {
		usleep(10000);  // 10ms
	}

	printf("All %d waiter threads are ready\n", DEFAULT_NUM_WAITERS);

	/* Acquire the mutex */
	status = pthread_mutex_lock(&bcast_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Set the shared data */
	bcast_shared_data = 123;
	printf("Signaler thread set data and will broadcast\n");

	/* Release the mutex before broadcasting */
	status = pthread_mutex_unlock(&bcast_mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Broadcast the condition to wake all waiters */
	status = pthread_cond_broadcast(&bcast_cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_broadcast failed: %d\n", status);
		return TEST_FAIL;
	}

	printf("Broadcast sent, waiting for threads to complete\n");

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

	/* Verify that all threads were awakened */
	if (bcast_threads_awake != DEFAULT_NUM_WAITERS) {
		printf("ERROR: Expected %d threads to be awakened, but %d were awakened\n", DEFAULT_NUM_WAITERS, bcast_threads_awake);
		return TEST_FAIL;
	}

	printf("All threads were awakened as expected\n");

	/* Clean up */
	pthread_cond_destroy(&bcast_cond);
	pthread_mutex_destroy(&bcast_mutex);

	TEST_END("Broadcast functionality test", ret);
	return ret;
}