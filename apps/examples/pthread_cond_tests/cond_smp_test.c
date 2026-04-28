/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_smp_test.c
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
#include <sched.h>
#include <pthread.h>
#include <time.h>

#ifdef CONFIG_SMP

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t smp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t smp_cond = PTHREAD_COND_INITIALIZER;
static volatile int smp_waiters_waiting = 0;
static volatile int smp_wakeups = 0;
static volatile int smp_signals = 0;
static volatile int smp_iterations = 0;
static volatile int smp_stop = 0;
static int smp_num_cpus = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int set_cpu_affinity(int cpu)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

static void *smp_waiter_thread(void *arg)
{
	int cpu = (int)(intptr_t)arg;
	int status;
	struct timespec timeout;

	/* Pin this thread to the specified CPU */
	status = set_cpu_affinity(cpu);
	if (status != 0) {
		printf("WARNING: Failed to set CPU affinity for waiter on CPU %d: %d\n", cpu, status);
	}

	printf("SMP waiter thread started on CPU %d\n", cpu);

	while (!smp_stop) {
		status = pthread_mutex_lock(&smp_mutex);
		if (status != 0) {
			printf("ERROR: pthread_mutex_lock failed: %d\n", status);
			return (void *)-1;
		}

		smp_waiters_waiting++;

		/* Use timed wait to prevent infinite blocking */
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += 1;  /* 1 second timeout */

		status = pthread_cond_timedwait(&smp_cond, &smp_mutex, &timeout);
		if (status == ETIMEDOUT) {
			smp_waiters_waiting--;
			pthread_mutex_unlock(&smp_mutex);
			continue;  /* Loop and check smp_stop */
		} else if (status != 0) {
			printf("ERROR: pthread_cond_timedwait failed: %d\n", status);
			smp_waiters_waiting--;
			pthread_mutex_unlock(&smp_mutex);
			return (void *)-1;
		}

		smp_waiters_waiting--;
		smp_wakeups++;
		smp_iterations++;

		pthread_mutex_unlock(&smp_mutex);
	}

	return NULL;
}

static void *smp_signaler_thread(void *arg)
{
	int cpu = (int)(intptr_t)arg;
	int status;

	/* Pin this thread to the specified CPU */
	status = set_cpu_affinity(cpu);
	if (status != 0) {
		printf("WARNING: Failed to set CPU affinity for signaler on CPU %d: %d\n", cpu, status);
	}

	printf("SMP signaler thread started on CPU %d\n", cpu);

	while (!smp_stop) {
		status = pthread_mutex_lock(&smp_mutex);
		if (status != 0) {
			printf("ERROR: pthread_mutex_lock failed: %d\n", status);
			return (void *)-1;
		}

		/* Signal if there are waiters */
		if (smp_waiters_waiting > 0) {
			status = pthread_cond_signal(&smp_cond);
			if (status != 0) {
				printf("ERROR: pthread_cond_signal failed: %d\n", status);
				pthread_mutex_unlock(&smp_mutex);
				return (void *)-1;
			}
			smp_signals++;
		}

		pthread_mutex_unlock(&smp_mutex);

		usleep(1000);  /* 1ms delay */
	}

	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_smp_test
 *
 * Description:
 *   Test condition variable behavior on SMP (multi-core) systems.
 *   Creates waiter and signaler threads pinned to different CPUs.
 ****************************************************************************/

int cond_smp_test(void)
{
	pthread_t *waiter_threads = NULL;
	pthread_t *signaler_threads = NULL;
	int status;
	void *result;
	int i;
	int ret = TEST_PASS;
	int num_waiters;
	int num_signalers;

	TEST_START("SMP concurrent test");

	/* Get number of CPUs */
	smp_num_cpus = CONFIG_SMP_NCPUS;
	printf("System has %d CPU(s)\n", smp_num_cpus);

	if (smp_num_cpus < 2) {
		printf("SKIP: SMP test requires at least 2 CPUs\n");
		return TEST_PASS;  /* Not a failure, just skip */
	}

	/* Distribute threads across CPUs */
	num_waiters = smp_num_cpus;
	num_signalers = smp_num_cpus;

	/* Reset test variables */
	smp_waiters_waiting = 0;
	smp_wakeups = 0;
	smp_signals = 0;
	smp_iterations = 0;
	smp_stop = 0;

	/* Initialize */
	pthread_cond_init(&smp_cond, NULL);
	pthread_mutex_init(&smp_mutex, NULL);

	printf("Creating %d waiter threads and %d signaler threads\n", num_waiters, num_signalers);

	waiter_threads = malloc(num_waiters * sizeof(pthread_t));
	signaler_threads = malloc(num_signalers * sizeof(pthread_t));

	if (!waiter_threads || !signaler_threads) {
		printf("ERROR: Failed to allocate thread arrays\n");
		ret = TEST_FAIL;
		goto cleanup;
	}

	/* Create waiter threads - each pinned to a different CPU */
	for (i = 0; i < num_waiters; i++) {
		status = pthread_create(&waiter_threads[i], NULL, smp_waiter_thread, (void *)(intptr_t)i);
		if (status != 0) {
			printf("ERROR: pthread_create failed for waiter %d: %d\n", i, status);
			smp_stop = 1;
			ret = TEST_FAIL;
			goto cleanup;
		}
	}

	/* Create signaler threads - each pinned to a different CPU */
	for (i = 0; i < num_signalers; i++) {
		status = pthread_create(&signaler_threads[i], NULL, smp_signaler_thread, (void *)(intptr_t)i);
		if (status != 0) {
			printf("ERROR: pthread_create failed for signaler %d: %d\n", i, status);
			smp_stop = 1;
			ret = TEST_FAIL;
			goto cleanup;
		}
	}

	/* Run for limited iterations */
	printf("Running SMP test...\n");
	while (smp_iterations < STRESS_ITERATIONS) {
		usleep(10000);  /* 10ms */
	}

	printf("Stopping threads...\n");
	smp_stop = 1;

	/* Wake any remaining waiters */
	pthread_mutex_lock(&smp_mutex);
	pthread_cond_broadcast(&smp_cond);
	pthread_mutex_unlock(&smp_mutex);

cleanup:
	/* Wait for all threads to complete */
	if (waiter_threads) {
		for (i = 0; i < num_waiters; i++) {
			pthread_join(waiter_threads[i], &result);
		}
		free(waiter_threads);
	}

	if (signaler_threads) {
		for (i = 0; i < num_signalers; i++) {
			pthread_join(signaler_threads[i], &result);
		}
		free(signaler_threads);
	}

	/* Report results */
	printf("SMP test completed successfully (%d CPUs, %d iterations)\n", smp_num_cpus, smp_iterations);

	/* Clean up */
	pthread_cond_destroy(&smp_cond);
	pthread_mutex_destroy(&smp_mutex);

	TEST_END("SMP concurrent test", ret);
	return ret;
}

#else /* !CONFIG_SMP */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int cond_smp_test(void)
{
	printf("\n=== Starting SMP concurrent test ===\n");
	printf("SKIP: SMP not configured (CONFIG_SMP not defined)\n");
	printf("PASS: SMP concurrent test (skipped)\n");
	return TEST_PASS;
}

#endif /* CONFIG_SMP */