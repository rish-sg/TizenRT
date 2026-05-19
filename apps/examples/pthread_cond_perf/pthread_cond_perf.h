/****************************************************************************
 * apps/examples/pthread_cond_perf/pthread_cond_perf.h
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

#ifndef __APPS_EXAMPLES_PTHREAD_COND_PERF_PTHREAD_COND_PERF_H
#define __APPS_EXAMPLES_PTHREAD_COND_PERF_PTHREAD_COND_PERF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <limits.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Test result codes */
#define TEST_PASS   0
#define TEST_FAIL  -1

/* Benchmark iteration counts */
#define LATENCY_ITERATIONS        10000
#define SIGNAL_NO_WAITER_ITER     100000
#define BROADCAST_ITERATIONS      100
/* Number of waiters for broadcast test */
#define BROADCAST_NUM_WAITERS     10

/* Number of threads for multiple condvar test */
#define MULTI_COND_NUM_THREADS    20
#define MULTI_COND_ITERATIONS     100

/* Timedwait test parameters */
#define TIMEDWAIT_ITERATIONS      1000
#define TIMEDWAIT_TIMEOUT_MS      20   /* 20ms timeout for timeout test */

/* Many condvars test parameters */
#define MANY_COND_NUM_THREADS     50
#define MANY_COND_ITERATIONS      100000

/* Contention test parameters */
#define CONTENTION_DURATION_SEC   5
#define CONTENTION_MAX_PAIRS      20

/* Nanoseconds per second */
#define NS_PER_SEC                1000000000LL

/****************************************************************************
 * Helper Macros
 ****************************************************************************/

/* Calculate elapsed time in nanoseconds */
#define ELAPSED_NS(start, end) \
	(((end).tv_sec - (start).tv_sec) * NS_PER_SEC + \
	 ((end).tv_nsec - (start).tv_nsec))

/* Print benchmark header */
#define BENCH_START(name) \
	printf("\n=== [%s] ===\n", name)

/* Print benchmark result */
#define BENCH_RESULT(iterations, total_ns) \
	do { \
		double avg_ns = (double)(total_ns) / (iterations); \
		printf("  Iterations:  %d\n", iterations); \
		printf("  Total time:  %.2f us\n", (double)(total_ns) / 1000.0); \
		printf("  Average:     %.2f us\n", avg_ns / 1000.0); \
		printf("  Throughput:  %.0f ops/sec\n", NS_PER_SEC / avg_ns); \
	} while(0)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Latency tests */
int perf_single_waiter_latency(void);
int perf_signal_no_waiter(void);
int perf_broadcast_latency(void);
int perf_multi_condvar_sequential_wake(void);

/* Timedwait test */
int perf_timedwait_timeout(void);

/* Many condvars test */
int perf_signal_last_condvar(void);

/* Contention test */
int perf_contention_test(void);

#endif /* __APPS_EXAMPLES_PTHREAD_COND_PERF_PTHREAD_COND_PERF_H */
