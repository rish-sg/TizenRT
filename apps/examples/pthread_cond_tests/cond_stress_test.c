/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_stress_test.c
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

static pthread_mutex_t stress_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stress_cond = PTHREAD_COND_INITIALIZER;
static volatile int stress_iterations = 0;
static volatile int stress_waiters_waiting = 0;
static volatile int stress_wakeups = 0;
static volatile int stress_signals = 0;
static volatile int stress_stop = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *stress_waiter_thread(void *arg)
{
	int status;

	while (!stress_stop) {
		status = pthread_mutex_lock(&stress_mutex);
		if (status != 0) {
			printf("ERROR: pthread_mutex_lock failed in waiter: %d\n", status);
			return (void *)-1;
		}

		stress_waiters_waiting++;

		/* Wait for signal */
		status = pthread_cond_wait(&stress_cond, &stress_mutex);
		if (status != 0) {
			printf("ERROR: pthread_cond_wait failed: %d\n", status);
			stress_waiters_waiting--;
			pthread_mutex_unlock(&stress_mutex);
			return (void *)-1;
		}

		stress_waiters_waiting--;
		stress_wakeups++;
		stress_iterations++;

		pthread_mutex_unlock(&stress_mutex);
	}

	return NULL;
}

static void *stress_signaler_thread(void *arg)
{
	int status;

	while (!stress_stop) {
		status = pthread_mutex_lock(&stress_mutex);
		if (status != 0) {
			printf("ERROR: pthread_mutex_lock failed in signaler: %d\n", status);
			return (void *)-1;
		}

		/* Only signal if there are waiters */
		if (stress_waiters_waiting > 0) {
			status = pthread_cond_signal(&stress_cond);
			if (status != 0) {
				printf("ERROR: pthread_cond_signal failed: %d\n", status);
				pthread_mutex_unlock(&stress_mutex);
				return (void *)-1;
			}
			stress_signals++;
		}

		pthread_mutex_unlock(&stress_mutex);

		/* Small delay to allow waiter to process */
		usleep(1000);  // 1ms
	}

	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_stress_test
 *
 * Description:
 *   Stress test with multiple waiters and signalers running concurrently.
 *   Runs for a fixed number of iterations to expose race conditions.
 ****************************************************************************/

int cond_stress_test(void)
{
	pthread_t waiter_threads[STRESS_NUM_WAITERS];
	pthread_t signaler_thread;
	int status;
	void *result;
	int i;
	int ret = TEST_PASS;

	TEST_START("Stress test");

	/* Reset test variables */
	stress_iterations = 0;
	stress_waiters_waiting = 0;
	stress_wakeups = 0;
	stress_signals = 0;
	stress_stop = 0;

	/* Initialize the condition variable and mutex */
	pthread_cond_init(&stress_cond, NULL);
	pthread_mutex_init(&stress_mutex, NULL);

	printf("Creating %d waiter threads and 1 signaler thread\n", STRESS_NUM_WAITERS);
	printf("Running stress test...\n");

	/* Create waiter threads */
	for (i = 0; i < STRESS_NUM_WAITERS; i++) {
		status = pthread_create(&waiter_threads[i], NULL, stress_waiter_thread, NULL);
		if (status != 0) {
			printf("ERROR: pthread_create failed for waiter %d: %d\n", i, status);
			stress_stop = 1;
			return TEST_FAIL;
		}
	}

	/* Create signaler thread */
	status = pthread_create(&signaler_thread, NULL, stress_signaler_thread, NULL);
	if (status != 0) {
		printf("ERROR: pthread_create failed for signaler: %d\n", status);
		stress_stop = 1;
		return TEST_FAIL;
	}

	/* Wait for iterations to complete */
	while (stress_iterations < STRESS_ITERATIONS) {
		usleep(10000);  // 10ms
	}

	printf("Stopping threads...\n");
	stress_stop = 1;

	/* Wake any remaining waiters */
	pthread_mutex_lock(&stress_mutex);
	pthread_cond_broadcast(&stress_cond);
	pthread_mutex_unlock(&stress_mutex);

	/* Wait for all threads to complete */
	for (i = 0; i < STRESS_NUM_WAITERS; i++) {
		status = pthread_join(waiter_threads[i], &result);
		if (status != 0) {
			printf("ERROR: pthread_join failed for waiter %d: %d\n", i, status);
			ret = TEST_FAIL;
		}
	}

	status = pthread_join(signaler_thread, &result);
	if (status != 0) {
		printf("ERROR: pthread_join failed for signaler: %d\n", status);
		ret = TEST_FAIL;
	}

	/* Report results */
	printf("Stress test completed successfully (%d iterations)\n", stress_iterations);

	/* Clean up */
	pthread_cond_destroy(&stress_cond);
	pthread_mutex_destroy(&stress_mutex);

	TEST_END("Stress test", ret);
	return ret;
}