/****************************************************************************
 * apps/examples/pthread_cond_perf/perf_latency_test.c
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

/****************************************************************************
 * Private Data for Single Waiter Test (Round-Trip)
 ****************************************************************************/

static pthread_mutex_t g_rt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_rt_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_rt_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_rt_done_cond = PTHREAD_COND_INITIALIZER;
static volatile int g_rt_data_ready = 0;
static volatile int g_rt_done = 0;

/****************************************************************************
 * Private Data for Broadcast Test (Round-Trip)
 ****************************************************************************/

static pthread_mutex_t g_bcast_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_bcast_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_bcast_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_bcast_done_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_bcast_all_ready_cond = PTHREAD_COND_INITIALIZER;
static int g_bcast_generation = 0;
static int g_bcast_done_count = 0;
static int g_bcast_waiters_ready = 0;
static int g_bcast_num_waiters = BROADCAST_NUM_WAITERS;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: roundtrip_waiter_thread
 *
 * Description:
 *   Waiter thread for single waiter round-trip test. Loops for the given
 *   number of iterations, waiting on g_rt_cond and signaling back on
 *   g_rt_done_cond each time.
 ****************************************************************************/

static void *roundtrip_waiter_thread(void *arg)
{
	int iterations = *(int *)arg;
	int i;

	for (i = 0; i < iterations; i++) {
		pthread_mutex_lock(&g_rt_mutex);
		while (!g_rt_data_ready) {
			pthread_cond_wait(&g_rt_cond, &g_rt_mutex);
		}
		g_rt_data_ready = 0;
		pthread_mutex_unlock(&g_rt_mutex);

		pthread_mutex_lock(&g_rt_done_mutex);
		g_rt_done = 1;
		pthread_cond_signal(&g_rt_done_cond);
		pthread_mutex_unlock(&g_rt_done_mutex);
	}

	return NULL;
}

/****************************************************************************
 * Name: broadcast_waiter_thread
 *
 * Description:
 *   Waiter thread for broadcast test. Loops for the given number of
 *   iterations, waiting on g_bcast_cond for broadcast, then signaling
 *   done on g_bcast_done_cond each time. Threads are reused across
 *   iterations to eliminate thread creation/destruction variance.
 ****************************************************************************/

static void *broadcast_waiter_thread(void *arg)
{
	int iterations = *(int *)arg;
	int i;
	int my_gen = 0;

	for (i = 0; i < iterations; i++) {
		/* Phase 1: Signal ready, wait for broadcast */
		pthread_mutex_lock(&g_bcast_mutex);
		g_bcast_waiters_ready++;
		pthread_cond_signal(&g_bcast_all_ready_cond);

		while (g_bcast_generation == my_gen) {
			pthread_cond_wait(&g_bcast_cond, &g_bcast_mutex);
		}
		my_gen = g_bcast_generation;
		pthread_mutex_unlock(&g_bcast_mutex);

		/* Phase 2: Signal done */
		pthread_mutex_lock(&g_bcast_done_mutex);
		g_bcast_done_count++;
		pthread_cond_signal(&g_bcast_done_cond);
		pthread_mutex_unlock(&g_bcast_done_mutex);
	}

	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_single_waiter_latency
 *
 * Description:
 *   Measures the round-trip latency of signal/wake with a single waiter.
 *   One waiter thread loops 1000 times: waits on condvar, signals back.
 *   Main thread signals the waiter and waits for signal-back each iteration.
 *   Timer measures the full round-trip: signal + wake + signal-back.
 ****************************************************************************/

int perf_single_waiter_latency(void)
{
	pthread_t thread;
	int i;
	int status;
	struct timespec start, end;
	long long total_ns = 0;
	int iterations = LATENCY_ITERATIONS;

	BENCH_START("Single Waiter Latency (Round-Trip)");

	g_rt_data_ready = 0;
	g_rt_done = 0;

	pthread_mutex_destroy(&g_rt_mutex);
	pthread_cond_destroy(&g_rt_cond);
	pthread_mutex_destroy(&g_rt_done_mutex);
	pthread_cond_destroy(&g_rt_done_cond);
	pthread_mutex_init(&g_rt_mutex, NULL);
	pthread_cond_init(&g_rt_cond, NULL);
	pthread_mutex_init(&g_rt_done_mutex, NULL);
	pthread_cond_init(&g_rt_done_cond, NULL);

	status = pthread_create(&thread, NULL, roundtrip_waiter_thread, &iterations);
	if (status != 0) {
		printf("ERROR: pthread_create failed: %d\n", status);
		return TEST_FAIL;
	}

	usleep(10000);

	for (i = 0; i < LATENCY_ITERATIONS; i++) {
		clock_gettime(CLOCK_MONOTONIC, &start);

		pthread_mutex_lock(&g_rt_mutex);
		g_rt_data_ready = 1;
		pthread_cond_signal(&g_rt_cond);
		pthread_mutex_unlock(&g_rt_mutex);

		pthread_mutex_lock(&g_rt_done_mutex);
		while (!g_rt_done) {
			pthread_cond_wait(&g_rt_done_cond, &g_rt_done_mutex);
		}
		g_rt_done = 0;
		pthread_mutex_unlock(&g_rt_done_mutex);

		clock_gettime(CLOCK_MONOTONIC, &end);

		total_ns += ELAPSED_NS(start, end);
	}

	pthread_join(thread, NULL);

	BENCH_RESULT(LATENCY_ITERATIONS, total_ns);
	printf("  Note:        Round-trip time (signal + wake + signal back)\n");

	return TEST_PASS;
}

/****************************************************************************
 * Name: perf_signal_no_waiter
 *
 * Description:
 *   Measures the raw overhead of pthread_cond_signal() and
 *   pthread_cond_broadcast() when no threads are waiting.
 *   Calls signal/broadcast 100000 times in a tight loop with no waiters.
 *   This provides a baseline for comparing with other tests.
 ****************************************************************************/

int perf_signal_no_waiter(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	struct timespec start, end;
	long long total_ns;
	int i;

	BENCH_START("Signal Without Waiters");

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < SIGNAL_NO_WAITER_ITER; i++) {
		pthread_cond_signal(&cond);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	total_ns = ELAPSED_NS(start, end);

	printf("  Test:        Signal (no waiters)\n");
	BENCH_RESULT(SIGNAL_NO_WAITER_ITER, total_ns);

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < SIGNAL_NO_WAITER_ITER; i++) {
		pthread_cond_broadcast(&cond);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	total_ns = ELAPSED_NS(start, end);

	printf("\n  Test:        Broadcast (no waiters)\n");
	BENCH_RESULT(SIGNAL_NO_WAITER_ITER, total_ns);

	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);

	return TEST_PASS;
}

/****************************************************************************
 * Name: perf_broadcast_latency
 *
 * Description:
 *   Measures the round-trip latency of pthread_cond_broadcast() with 10
 *   waiters. Creates 10 waiter threads once, then loops: broadcasts to
 *   wake all, waits for all to signal back. Threads are reused across
 *   iterations to eliminate thread creation/destruction variance.
 *   Timer measures from broadcast to all waiters responding.
 ****************************************************************************/

int perf_broadcast_latency(void)
{
	pthread_t threads[BROADCAST_NUM_WAITERS];
	struct timespec start, end;
	long long total_ns;
	int i, iter;
	int status;
	int threads_created;
	long long grand_total_ns = 0;
	int iterations = BROADCAST_ITERATIONS;
	int expected_waiters = 0;
	int expected_done = 0;

	BENCH_START("Broadcast Latency (Round-Trip)");

	g_bcast_num_waiters = BROADCAST_NUM_WAITERS;
	g_bcast_generation = 0;
	g_bcast_done_count = 0;
	g_bcast_waiters_ready = 0;
	threads_created = 0;

	/* Create waiter threads once - they loop internally */
	for (i = 0; i < BROADCAST_NUM_WAITERS; i++) {
		status = pthread_create(&threads[i], NULL, broadcast_waiter_thread, &iterations);
		if (status != 0) {
			printf("ERROR: pthread_create failed for thread %d: %d\n", i, status);
			/* Wake any created threads so they can exit */
			g_bcast_generation++;
			pthread_cond_broadcast(&g_bcast_cond);
			for (int j = 0; j < threads_created; j++) {
				pthread_join(threads[j], NULL);
			}
			return TEST_FAIL;
		}
		threads_created++;
	}

	for (iter = 0; iter < BROADCAST_ITERATIONS; iter++) {
		expected_waiters += BROADCAST_NUM_WAITERS;

		/* Wait for all waiters to be ready for this iteration */
		pthread_mutex_lock(&g_bcast_mutex);
		while (g_bcast_waiters_ready < expected_waiters) {
			pthread_cond_wait(&g_bcast_all_ready_cond, &g_bcast_mutex);
		}

		/* Start timer and broadcast */
		clock_gettime(CLOCK_MONOTONIC, &start);
		g_bcast_generation++;
		pthread_cond_broadcast(&g_bcast_cond);
		pthread_mutex_unlock(&g_bcast_mutex);

		/* Wait for all waiters to signal done */
		expected_done += BROADCAST_NUM_WAITERS;
		pthread_mutex_lock(&g_bcast_done_mutex);
		while (g_bcast_done_count < expected_done) {
			pthread_cond_wait(&g_bcast_done_cond, &g_bcast_done_mutex);
		}
		pthread_mutex_unlock(&g_bcast_done_mutex);

		clock_gettime(CLOCK_MONOTONIC, &end);

		total_ns = ELAPSED_NS(start, end);
		grand_total_ns += total_ns;
	}

	/* Join threads once after all iterations */
	for (i = 0; i < BROADCAST_NUM_WAITERS; i++) {
		pthread_join(threads[i], NULL);
	}

	BENCH_RESULT(BROADCAST_ITERATIONS, grand_total_ns);
	printf("  Waiters:     %d\n", BROADCAST_NUM_WAITERS);
	printf("  Avg/waiter:  %.2f us\n",
		(double)grand_total_ns / BROADCAST_ITERATIONS / BROADCAST_NUM_WAITERS / 1000.0);
	printf("  Note:        Round-trip time (broadcast + all wake + all signal back)\n");

	return TEST_PASS;
}

/****************************************************************************
 * Private Data for Multiple Condvar Sequential Wake Test
 ****************************************************************************/

static pthread_mutex_t g_multi_mutex[MULTI_COND_NUM_THREADS];
static pthread_cond_t g_multi_cond[MULTI_COND_NUM_THREADS];
static pthread_mutex_t g_multi_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_multi_done_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_multi_all_ready_cond = PTHREAD_COND_INITIALIZER;
static int g_multi_data_ready[MULTI_COND_NUM_THREADS];
static int g_multi_done[MULTI_COND_NUM_THREADS];
static int g_multi_waiters_ready;

/****************************************************************************
 * Name: multi_cond_waiter_thread
 *
 * Description:
 *   Waiter thread for multi condvar test. Each thread waits on its own
 *   condvar (g_multi_cond[id]) and signals done on g_multi_done_cond.
 ****************************************************************************/

static void *multi_cond_waiter_thread(void *arg)
{
	int id = *(int *)arg;

	pthread_mutex_lock(&g_multi_mutex[id]);
	g_multi_waiters_ready++;
	if (g_multi_waiters_ready == MULTI_COND_NUM_THREADS) {
		pthread_cond_signal(&g_multi_all_ready_cond);
	}

	while (!g_multi_data_ready[id]) {
		pthread_cond_wait(&g_multi_cond[id], &g_multi_mutex[id]);
	}
	g_multi_data_ready[id] = 0;
	pthread_mutex_unlock(&g_multi_mutex[id]);

	pthread_mutex_lock(&g_multi_done_mutex);
	g_multi_done[id] = 1;
	pthread_cond_signal(&g_multi_done_cond);
	pthread_mutex_unlock(&g_multi_done_mutex);

	return NULL;
}

/****************************************************************************
 * Name: perf_multi_condvar_sequential_wake
 *
 * Description:
 *   Measures the cost of signaling 20 separate condvars one-by-one.
 *   20 waiter threads, each with its own condvar, are woken sequentially.
 *   Timer measures the total time to signal and wait for all 20 threads.
 *   Reports avg per iteration and avg per thread.
 ****************************************************************************/

int perf_multi_condvar_sequential_wake(void)
{
	pthread_t threads[MULTI_COND_NUM_THREADS];
	int thread_ids[MULTI_COND_NUM_THREADS];
	struct timespec total_start, total_end;
	long long grand_total_ns = 0;
	int i, iter;
	int status;
	int threads_created;

	BENCH_START("Multiple Condvar Sequential Wake");

	printf("  Threads:     %d\n", MULTI_COND_NUM_THREADS);

	for (iter = 0; iter < MULTI_COND_ITERATIONS; iter++) {
		g_multi_waiters_ready = 0;
		threads_created = 0;
		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			g_multi_data_ready[i] = 0;
			g_multi_done[i] = 0;
		}

		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			pthread_mutex_init(&g_multi_mutex[i], NULL);
			pthread_cond_init(&g_multi_cond[i], NULL);
		}
		pthread_mutex_destroy(&g_multi_done_mutex);
		pthread_cond_destroy(&g_multi_done_cond);
		pthread_cond_destroy(&g_multi_all_ready_cond);
		pthread_mutex_init(&g_multi_done_mutex, NULL);
		pthread_cond_init(&g_multi_done_cond, NULL);
		pthread_cond_init(&g_multi_all_ready_cond, NULL);

		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			thread_ids[i] = i;
			status = pthread_create(&threads[i], NULL, multi_cond_waiter_thread, &thread_ids[i]);
			if (status != 0) {
				printf("ERROR: pthread_create failed for thread %d: %d\n", i, status);
				for (int j = 0; j < threads_created; j++) {
					pthread_mutex_lock(&g_multi_mutex[j]);
					g_multi_data_ready[j] = 1;
					pthread_cond_signal(&g_multi_cond[j]);
					pthread_mutex_unlock(&g_multi_mutex[j]);
					pthread_join(threads[j], NULL);
				}
				return TEST_FAIL;
			}
			threads_created++;
		}

		pthread_mutex_lock(&g_multi_mutex[0]);
		while (g_multi_waiters_ready < MULTI_COND_NUM_THREADS) {
			pthread_cond_wait(&g_multi_all_ready_cond, &g_multi_mutex[0]);
		}
		pthread_mutex_unlock(&g_multi_mutex[0]);

		clock_gettime(CLOCK_MONOTONIC, &total_start);

		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			pthread_mutex_lock(&g_multi_mutex[i]);
			g_multi_data_ready[i] = 1;
			pthread_cond_signal(&g_multi_cond[i]);
			pthread_mutex_unlock(&g_multi_mutex[i]);

			pthread_mutex_lock(&g_multi_done_mutex);
			while (!g_multi_done[i]) {
				pthread_cond_wait(&g_multi_done_cond, &g_multi_done_mutex);
			}
			pthread_mutex_unlock(&g_multi_done_mutex);
		}

		clock_gettime(CLOCK_MONOTONIC, &total_end);

		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			pthread_join(threads[i], NULL);
		}

		for (i = 0; i < MULTI_COND_NUM_THREADS; i++) {
			pthread_mutex_destroy(&g_multi_mutex[i]);
			pthread_cond_destroy(&g_multi_cond[i]);
		}

		grand_total_ns += ELAPSED_NS(total_start, total_end);
	}

	printf("  Iterations:  %d\n", MULTI_COND_ITERATIONS);
	printf("  Avg per iteration:  %.2f us\n",
		(double)(grand_total_ns / MULTI_COND_ITERATIONS) / 1000.0);
	printf("  Avg per thread:     %.2f us\n",
		(double)(grand_total_ns / MULTI_COND_ITERATIONS) / MULTI_COND_NUM_THREADS / 1000.0);
	printf("\n  Note: %d threads with separate condvars, woken one-by-one\n",
		MULTI_COND_NUM_THREADS);

	return TEST_PASS;
}

/****************************************************************************
 * Private Data for Many Condvars Test
 *
 * This test measures the overhead of traversing the global waiter list.
 * 50 threads wait on 50 separate condvars (populating the list with 50 nodes).
 * A separate condvar (cond_var_op) has NO waiter.
 * We signal cond_var_op 100000 times - each call must traverse all 50 nodes
 * and find no match. This isolates the list traversal overhead.
 ****************************************************************************/

static pthread_mutex_t g_many_mutex[MANY_COND_NUM_THREADS];
static pthread_cond_t g_many_cond[MANY_COND_NUM_THREADS];
static pthread_cond_t g_many_all_ready_cond = PTHREAD_COND_INITIALIZER;
static int g_many_data_ready[MANY_COND_NUM_THREADS];
static int g_many_waiters_ready;

/* The condvar we signal - has no waiter, so signal must traverse entire list */
static pthread_mutex_t g_many_op_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_many_op_cond = PTHREAD_COND_INITIALIZER;

/****************************************************************************
 * Name: many_cond_waiter_thread
 *
 * Description:
 *   Waiter thread for list traversal test. Blocks indefinitely on its own
 *   condvar to populate the global waiter list. These threads never wake
 *   during the measurement phase.
 ****************************************************************************/

static void *many_cond_waiter_thread(void *arg)
{
	int id = *(int *)arg;

	pthread_mutex_lock(&g_many_mutex[id]);
	g_many_waiters_ready++;
	if (g_many_waiters_ready == MANY_COND_NUM_THREADS) {
		pthread_cond_signal(&g_many_all_ready_cond);
	}

	/* Wait indefinitely - these threads just populate the waiter list */
	while (!g_many_data_ready[id]) {
		pthread_cond_wait(&g_many_cond[id], &g_many_mutex[id]);
	}
	pthread_mutex_unlock(&g_many_mutex[id]);

	return NULL;
}

/****************************************************************************
 * Name: perf_signal_last_condvar
 *
 * Description:
 *   Measures the overhead of traversing the global waiter list in
 *   pthread_cond_signal(). 50 threads wait on 50 separate condvars,
 *   populating the global list with 50 nodes. A separate condvar with
 *   no waiter is signaled 100000 times - each signal must traverse all
 *   50 nodes and find no match. Compare with Test 2 (0 waiters) to
 *   isolate the pure list traversal overhead (~4.6 ns per node).
 ****************************************************************************/

int perf_signal_last_condvar(void)
{
	pthread_t threads[MANY_COND_NUM_THREADS];
	int thread_ids[MANY_COND_NUM_THREADS];
	struct timespec start, end;
	long long total_ns;
	int i;
	int status;
	int threads_created;

	BENCH_START("List Traversal Overhead (50 Waiters)");

	printf("  Waiters:     %d\n", MANY_COND_NUM_THREADS);

	/* Initialize mutexes and condvars for waiter threads */
	for (i = 0; i < MANY_COND_NUM_THREADS; i++) {
		pthread_mutex_init(&g_many_mutex[i], NULL);
		pthread_cond_init(&g_many_cond[i], NULL);
		g_many_data_ready[i] = 0;
	}
	pthread_cond_init(&g_many_all_ready_cond, NULL);

	/* Initialize the condvar we will signal (no waiter) */
	pthread_mutex_init(&g_many_op_mutex, NULL);
	pthread_cond_init(&g_many_op_cond, NULL);

	g_many_waiters_ready = 0;
	threads_created = 0;

	/* Create waiter threads - they will block and populate the global list */
	for (i = 0; i < MANY_COND_NUM_THREADS; i++) {
		thread_ids[i] = i;
		status = pthread_create(&threads[i], NULL, many_cond_waiter_thread, &thread_ids[i]);
		if (status != 0) {
			printf("ERROR: pthread_create failed for thread %d: %d\n", i, status);
			for (int j = 0; j < threads_created; j++) {
				pthread_mutex_lock(&g_many_mutex[j]);
				g_many_data_ready[j] = 1;
				pthread_cond_signal(&g_many_cond[j]);
				pthread_mutex_unlock(&g_many_mutex[j]);
				pthread_join(threads[j], NULL);
			}
			return TEST_FAIL;
		}
		threads_created++;
	}

	/* Wait for all waiter threads to be ready and blocked */
	pthread_mutex_lock(&g_many_mutex[0]);
	while (g_many_waiters_ready < MANY_COND_NUM_THREADS) {
		pthread_cond_wait(&g_many_all_ready_cond, &g_many_mutex[0]);
	}
	pthread_mutex_unlock(&g_many_mutex[0]);

	/* Small delay to ensure all threads are blocked in pthread_cond_wait */
	usleep(1000);

	/* Now the global waiter list has 50 nodes.
	 * Signal g_many_op_cond (which has NO waiter) MANY_COND_ITERATIONS times.
	 * Each signal call must traverse all 50 nodes and find no match.
	 */
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < MANY_COND_ITERATIONS; i++) {
		pthread_cond_signal(&g_many_op_cond);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	total_ns = ELAPSED_NS(start, end);

	BENCH_RESULT(MANY_COND_ITERATIONS, total_ns);
	printf("  Note: 50 waiters in list, signaling condvar with no waiter\n");
	printf("  Note: Each signal must traverse all 50 nodes (no match found)\n");

	/* Wake up all waiter threads and clean up */
	for (i = 0; i < MANY_COND_NUM_THREADS; i++) {
		pthread_mutex_lock(&g_many_mutex[i]);
		g_many_data_ready[i] = 1;
		pthread_cond_signal(&g_many_cond[i]);
		pthread_mutex_unlock(&g_many_mutex[i]);
	}

	for (i = 0; i < MANY_COND_NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	for (i = 0; i < MANY_COND_NUM_THREADS; i++) {
		pthread_mutex_destroy(&g_many_mutex[i]);
		pthread_cond_destroy(&g_many_cond[i]);
	}
	pthread_cond_destroy(&g_many_all_ready_cond);
	pthread_mutex_destroy(&g_many_op_mutex);
	pthread_cond_destroy(&g_many_op_cond);

	return TEST_PASS;
}
