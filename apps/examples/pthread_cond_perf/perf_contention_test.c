/****************************************************************************
 * apps/examples/pthread_cond_perf/perf_contention_test.c
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

#include "pthread_cond_perf.h"
#include <sched.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Per-pair data for contention test */
struct contention_pair_data {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_mutex_t done_mutex;
	pthread_cond_t done_cond;
	volatile int data_ready;
	volatile int done;
	volatile int running;
	long long iterations;
};

static struct contention_pair_data g_pairs[CONTENTION_MAX_PAIRS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: contention_consumer_thread
 *
 * Description:
 *   Consumer thread for one pair in the contention test. Loops waiting on
 *   its pair's condvar and signaling back, counting iterations until
 *   g_pairs[id].running is set to 0.
 ****************************************************************************/

static void *contention_consumer_thread(void *arg)
{
	int id = *(int *)arg;
	struct contention_pair_data *pair = &g_pairs[id];

	while (pair->running) {
		pthread_mutex_lock(&pair->mutex);
		while (!pair->data_ready && pair->running) {
			pthread_cond_wait(&pair->cond, &pair->mutex);
		}

		if (!pair->running) {
			pthread_mutex_unlock(&pair->mutex);
			break;
		}

		pair->data_ready = 0;
		pthread_mutex_unlock(&pair->mutex);

		pthread_mutex_lock(&pair->done_mutex);
		pair->done = 1;
		pthread_cond_signal(&pair->done_cond);
		pthread_mutex_unlock(&pair->done_mutex);
	}

	return NULL;
}

/****************************************************************************
 * Name: contention_producer_thread
 *
 * Description:
 *   Producer thread for one pair in the contention test. Signals the
 *   consumer and waits for signal-back in a loop for CONTENTION_DURATION_SEC
 *   seconds. Counts total iterations.
 ****************************************************************************/

static void *contention_producer_thread(void *arg)
{
	int id = *(int *)arg;
	struct contention_pair_data *pair = &g_pairs[id];
	struct timespec test_start, current;
	long long elapsed_sec;

	clock_gettime(CLOCK_MONOTONIC, &test_start);

	while (1) {
		/* Signal the consumer */
		pthread_mutex_lock(&pair->mutex);
		pair->data_ready = 1;
		pthread_cond_signal(&pair->cond);
		pthread_mutex_unlock(&pair->mutex);

		/* Wait for consumer to signal back */
		pthread_mutex_lock(&pair->done_mutex);
		while (!pair->done) {
			pthread_cond_wait(&pair->done_cond, &pair->done_mutex);
		}
		pair->done = 0;
		pthread_mutex_unlock(&pair->done_mutex);

		pair->iterations++;

		/* Check if test duration elapsed */
		clock_gettime(CLOCK_MONOTONIC, &current);
		elapsed_sec = current.tv_sec - test_start.tv_sec;
		if (elapsed_sec >= CONTENTION_DURATION_SEC) {
			break;
		}
	}

	/* Signal consumer to exit */
	pthread_mutex_lock(&pair->mutex);
	pair->running = 0;
	pthread_cond_signal(&pair->cond);
	pthread_mutex_unlock(&pair->mutex);

	return NULL;
}

/****************************************************************************
 * Name: contention_run_with_pairs
 *
 * Description:
 *   Runs the contention test with the specified number of producer-consumer
 *   pairs. All pairs run simultaneously for CONTENTION_DURATION_SEC seconds.
 *   Reports total iterations and average time per operation per pair.
 ****************************************************************************/

static int contention_run_with_pairs(int num_pairs)
{
	pthread_t producers[CONTENTION_MAX_PAIRS];
	pthread_t consumers[CONTENTION_MAX_PAIRS];
	pthread_attr_t attr;
#ifdef CONFIG_SMP
	cpu_set_t cpuset;
#endif
	int ids[CONTENTION_MAX_PAIRS];
	long long total_iterations = 0;
	int i;
	int status;

	/* Initialize per-pair data */
	for (i = 0; i < num_pairs; i++) {
		pthread_mutex_init(&g_pairs[i].mutex, NULL);
		pthread_cond_init(&g_pairs[i].cond, NULL);
		pthread_mutex_init(&g_pairs[i].done_mutex, NULL);
		pthread_cond_init(&g_pairs[i].done_cond, NULL);
		g_pairs[i].data_ready = 0;
		g_pairs[i].done = 0;
		g_pairs[i].running = 1;
		g_pairs[i].iterations = 0;
		ids[i] = i;
	}

	/* Initialize thread attributes */
	pthread_attr_init(&attr);

	/* Create pairs with alternating CPU affinity.
	 * Even pairs → CPU 0, Odd pairs → CPU 1.
	 * Both consumer and producer of a pair run on the same CPU.
	 * The data_ready flag prevents race conditions even if the
	 * producer signals before the consumer calls pthread_cond_wait.
	 */
	for (i = 0; i < num_pairs; i++) {
#ifdef CONFIG_SMP
		int cpu = i % 2;

		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);
		pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
#endif

		/* Create consumer for this pair */
		status = pthread_create(&consumers[i], &attr,
					contention_consumer_thread, &ids[i]);
		if (status != 0) {
			printf("ERROR: pthread_create consumer %d failed: %d\n",
				i, status);
			for (int j = 0; j < i; j++) {
				pthread_mutex_lock(&g_pairs[j].mutex);
				g_pairs[j].running = 0;
				pthread_cond_signal(&g_pairs[j].cond);
				pthread_mutex_unlock(&g_pairs[j].mutex);
				pthread_join(consumers[j], NULL);
				pthread_join(producers[j], NULL);
			}
			pthread_attr_destroy(&attr);
			return TEST_FAIL;
		}

		/* Create producer for this pair (same CPU as consumer) */
		status = pthread_create(&producers[i], &attr,
					contention_producer_thread, &ids[i]);
		if (status != 0) {
			printf("ERROR: pthread_create producer %d failed: %d\n",
				i, status);
			/* Stop the consumer we just created */
			pthread_mutex_lock(&g_pairs[i].mutex);
			g_pairs[i].running = 0;
			pthread_cond_signal(&g_pairs[i].cond);
			pthread_mutex_unlock(&g_pairs[i].mutex);
			pthread_join(consumers[i], NULL);
			/* Clean up previously created pairs */
			for (int j = 0; j < i; j++) {
				pthread_mutex_lock(&g_pairs[j].mutex);
				g_pairs[j].running = 0;
				pthread_cond_signal(&g_pairs[j].cond);
				pthread_mutex_unlock(&g_pairs[j].mutex);
				pthread_join(consumers[j], NULL);
				pthread_join(producers[j], NULL);
			}
			pthread_attr_destroy(&attr);
			return TEST_FAIL;
		}
	}

	/* Wait for all producers to complete */
	for (i = 0; i < num_pairs; i++) {
		pthread_join(producers[i], NULL);
	}

	/* Wait for all consumers to complete */
	for (i = 0; i < num_pairs; i++) {
		pthread_join(consumers[i], NULL);
	}

	/* Sum total iterations */
	for (i = 0; i < num_pairs; i++) {
		total_iterations += g_pairs[i].iterations;
	}

	/* Destroy thread attributes */
	pthread_attr_destroy(&attr);

	/* Print results */
	printf("\n  Pairs:       %d\n", num_pairs);
#ifdef CONFIG_SMP
	printf("  SMP:         Enabled (pairs distributed across CPU0/CPU1)\n");
#else
	printf("  SMP:         Disabled (single CPU)\n");
#endif
	printf("  Duration:    %d seconds\n", CONTENTION_DURATION_SEC);
	printf("  Iterations:  %lld\n", total_iterations);
	printf("  Avg per pair: %.2f us\n",
		(double)(CONTENTION_DURATION_SEC * 1000000LL) * num_pairs / total_iterations);

	/* Clean up */
	for (i = 0; i < num_pairs; i++) {
		pthread_mutex_destroy(&g_pairs[i].mutex);
		pthread_cond_destroy(&g_pairs[i].cond);
		pthread_mutex_destroy(&g_pairs[i].done_mutex);
		pthread_cond_destroy(&g_pairs[i].done_cond);
	}

	return TEST_PASS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_contention_test
 *
 * Description:
 *   Runs the contention stress test with 1, 5, 10, and 20 producer-consumer
 *   pairs. Each pair does a ping-pong (signal/wait) on its own condvar.
 *   All pairs run simultaneously, competing for the global waiter list lock.
 *   If per-pair throughput drops with more pairs, the global list lock is
 *   a bottleneck.
 ****************************************************************************/

int perf_contention_test(void)
{
	int num_pairs[] = {1, 5, 10, 20};
	int num_tests = 4;
	int i;
	int ret;

	BENCH_START("Contention Stress Test");

	for (i = 0; i < num_tests; i++) {
		ret = contention_run_with_pairs(num_pairs[i]);
		if (ret != TEST_PASS) {
			printf("ERROR: Contention test with %d pairs failed\n",
				num_pairs[i]);
			return TEST_FAIL;
		}
	}

	return TEST_PASS;
}
