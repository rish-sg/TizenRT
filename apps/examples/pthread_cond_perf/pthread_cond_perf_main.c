/****************************************************************************
 * apps/examples/pthread_cond_perf/pthread_cond_perf_main.c
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
 * Name: pthread_cond_perf_main
 *
 * Description:
 *   Entry point for the pthread_cond_* performance benchmark suite.
 *   Runs 7 benchmark tests measuring various aspects of condvar performance:
 *   1. Single waiter round-trip latency
 *   2. Signal/broadcast without waiters (baseline)
 *   3. Broadcast round-trip latency (10 waiters)
 *   4. Multiple condvar sequential wake (20 threads)
 *   5. Timedwait timeout (20ms, 1000 iterations)
 *   6. List traversal overhead (50 waiters in global list)
 *   7. Contention stress test (1, 5, 10, 20 pairs)
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int pthread_cond_perf_main(int argc, char *argv[])
#endif
{
	int result = TEST_PASS;
	int ret;

	printf("========================================\n");
	printf("  pthread_cond_* Performance Benchmark\n");
	printf("========================================\n");

	/* Test 1: Single waiter latency */
	ret = perf_single_waiter_latency();
	if (ret != TEST_PASS) {
		printf("ERROR: Single waiter latency test failed\n");
		result = TEST_FAIL;
	}

	/* Test 2: Signal without waiters */
	ret = perf_signal_no_waiter();
	if (ret != TEST_PASS) {
		printf("ERROR: Signal without waiters test failed\n");
		result = TEST_FAIL;
	}

	/* Test 3: Broadcast latency */
	ret = perf_broadcast_latency();
	if (ret != TEST_PASS) {
		printf("ERROR: Broadcast latency test failed\n");
		result = TEST_FAIL;
	}

	/* Test 4: Multiple condvar sequential wake */
	ret = perf_multi_condvar_sequential_wake();
	if (ret != TEST_PASS) {
		printf("ERROR: Multiple condvar sequential wake test failed\n");
		result = TEST_FAIL;
	}

	/* Test 5: Timedwait (timeout) */
	ret = perf_timedwait_timeout();
	if (ret != TEST_PASS) {
		printf("ERROR: Timedwait timeout test failed\n");
		result = TEST_FAIL;
	}

	/* Test 6: List traversal overhead (50 waiters) */
	ret = perf_signal_last_condvar();
	if (ret != TEST_PASS) {
		printf("ERROR: List traversal overhead test failed\n");
		result = TEST_FAIL;
	}

	/* Test 7: Contention stress test */
	ret = perf_contention_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Contention stress test failed\n");
		result = TEST_FAIL;
	}

	printf("\n========================================\n");
	if (result == TEST_PASS) {
		printf("  All benchmark tests completed!\n");
	} else {
		printf("  Some benchmark tests failed!\n");
	}
	printf("========================================\n");

	return result;
}
