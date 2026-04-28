/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_basic_test.c
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
static int thread_ready = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *waiter_thread(void *arg)
{
	int status;

	printf("Waiter thread started\n");

	/* Acquire the mutex */
	status = pthread_mutex_lock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return (void *)-1;
	}

	/* Indicate we're ready and waiting */
	thread_ready = 1;
	printf("Waiter thread is ready and waiting\n");

	/* Wait for the condition */
	status = pthread_cond_wait(&cond, &mutex);
	if (status != 0) {
		printf("ERROR: pthread_cond_wait failed: %d\n", status);
		pthread_mutex_unlock(&mutex);
		return (void *)-1;
	}

	/* Check the shared data */
	if (shared_data != 42) {
		printf("ERROR: Unexpected shared_data value: %d\n", shared_data);
		pthread_mutex_unlock(&mutex);
		return (void *)-1;
	}

	printf("Waiter thread received signal and data is correct\n");

	/* Release the mutex */
	status = pthread_mutex_unlock(&mutex);
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
 * Name: cond_basic_test
 ****************************************************************************/

int cond_basic_test(void)
{
	pthread_t thread;
	int status;
	void *result;
	int ret = TEST_PASS;

	TEST_START("Basic signal/wait test");

	/* Initialize the condition variable and mutex */
	pthread_cond_init(&cond, NULL);
	pthread_mutex_init(&mutex, NULL);

	/* Reset test variables */
	shared_data = 0;
	thread_ready = 0;

	/* Create the waiter thread */
	status = pthread_create(&thread, NULL, waiter_thread, NULL);
	if (status != 0) {
		printf("ERROR: pthread_create failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Give the waiter thread time to start and acquire the mutex */
	usleep(100000);  // 100ms

	/* Wait until the thread is ready */
	while (!thread_ready) {
		usleep(10000);  // 10ms
	}

	/* Acquire the mutex */
	status = pthread_mutex_lock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_lock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Set the shared data */
	shared_data = 42;
	printf("Signaler thread set data and will signal\n");

	/* Release the mutex before signaling */
	status = pthread_mutex_unlock(&mutex);
	if (status != 0) {
		printf("ERROR: pthread_mutex_unlock failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Signal the condition */
	status = pthread_cond_signal(&cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_signal failed: %d\n", status);
		return TEST_FAIL;
	}

	/* Wait for the thread to complete */
	status = pthread_join(thread, &result);
	if (status != 0) {
		printf("ERROR: pthread_join failed: %d\n", status);
		return TEST_FAIL;
	}

	if (result != NULL) {
		printf("ERROR: Waiter thread returned error\n");
		return TEST_FAIL;
	}

	/* Clean up */
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);

	TEST_END("Basic signal/wait test", ret);
	return ret;
}