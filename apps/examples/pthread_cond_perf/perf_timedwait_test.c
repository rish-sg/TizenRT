/****************************************************************************
 * apps/examples/pthread_cond_perf/perf_timedwait_test.c
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
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_timedwait_timeout
 *
 * Description:
 *   Measures the latency of pthread_cond_timedwait() when the timeout
 *   expires (no signal). No signaler thread needed - just call timedwait
 *   in a loop and let it timeout each time.
 *
 *   This tests the kernel's ability to add/remove a waiter from the global
 *   list when timedwait times out (relevant to the kernel fix).
 ****************************************************************************/

int perf_timedwait_timeout(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	struct timespec start, end, timeout;
	long long total_ns = 0;
	int i, ret;

	BENCH_START("Timedwait (Timeout Expired)");

	for (i = 0; i < TIMEDWAIT_ITERATIONS; i++) {
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_nsec += TIMEDWAIT_TIMEOUT_MS * 1000000LL;

		/* Handle nanosecond overflow */
		while (timeout.tv_nsec >= NS_PER_SEC) {
			timeout.tv_nsec -= NS_PER_SEC;
			timeout.tv_sec++;
		}

		pthread_mutex_lock(&mutex);

		clock_gettime(CLOCK_MONOTONIC, &start);

		ret = pthread_cond_timedwait(&cond, &mutex, &timeout);

		clock_gettime(CLOCK_MONOTONIC, &end);

		pthread_mutex_unlock(&mutex);

		if (ret != ETIMEDOUT) {
			printf("ERROR: Expected ETIMEDOUT, got %d\n", ret);
			continue;
		}

		total_ns += ELAPSED_NS(start, end);
	}

	BENCH_RESULT(TIMEDWAIT_ITERATIONS, total_ns);
	printf("  Timeout:     %d ms (expected)\n", TIMEDWAIT_TIMEOUT_MS);
	printf("  Note:        No signal, timeout expired\n");

	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);

	return TEST_PASS;
}
