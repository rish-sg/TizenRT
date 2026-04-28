/****************************************************************************
 * apps/examples/pthread_cond_tests/pthread_cond_tests.h
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

#ifndef __APPS_EXAMPLES_PTHREAD_COND_TESTS_PTHREAD_COND_TESTS_H
#define __APPS_EXAMPLES_PTHREAD_COND_TESTS_PTHREAD_COND_TESTS_H

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

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Test result codes */
#define TEST_PASS   0
#define TEST_FAIL  -1

/* Common macros for test output */
#define TEST_START(name)  printf("\n=== Starting %s ===\n", name)

#define TEST_END(name, result) \
	do { \
		if (result == TEST_PASS) { \
			printf("PASS: %s\n", name); \
		} else { \
			printf("FAIL: %s\n", name); \
		} \
	} while(0)

/* Default thread counts */
#define DEFAULT_NUM_WAITERS    3
#define STRESS_NUM_WAITERS    10
#define STRESS_ITERATIONS    100
#define MAX_WAITERS_TEST      130

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Existing tests */
int cond_basic_test(void);
int cond_multiple_test(void);
int cond_broadcast_test(void);
int cond_timeout_test(void);

/* New tests (to be added) */
int cond_race_test(void);
int cond_smp_test(void);
int cond_stress_test(void);
int cond_spurious_test(void);
int cond_no_waiter_test(void);
int cond_destroy_test(void);
int cond_max_waiters_test(void);

#endif /* __APPS_EXAMPLES_PTHREAD_COND_TESTS_PTHREAD_COND_TESTS_H */