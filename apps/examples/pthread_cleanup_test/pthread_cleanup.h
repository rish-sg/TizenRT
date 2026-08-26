/****************************************************************************
 * apps/examples/pthread_cleanup_test/pthread_cleanup.h
 *
 *   Copyright (C) 2026 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution. 3. Neither the name Samsung Electronics nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PTHREAD_CLEANUP_TEST_H
#define __APPS_EXAMPLES_PTHREAD_CLEANUP_TEST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Test result macros */

#define TEST_PASS(fmt, ...) \
	printf("[PASS] " fmt "\n", ##__VA_ARGS__)

#define TEST_FAIL(fmt, ...) \
	printf("[FAIL] " fmt "\n", ##__VA_ARGS__)

#define TEST_INFO(fmt, ...) \
	printf("[INFO] " fmt "\n", ##__VA_ARGS__)

#define TEST_START(name) \
	printf("\n[TEST] %s\n", name); \
	printf("[INFO] Starting test...\n")

#define TEST_DONE(name, result) \
	if (result) { \
		TEST_PASS("%s completed successfully", name); \
	} else { \
		TEST_FAIL("%s failed", name); \
	}

/* Configuration defaults */

#ifndef CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_STACKSIZE
#define DEFAULT_STACKSIZE 8192
#else
#define DEFAULT_STACKSIZE CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_STACKSIZE
#endif

#ifndef CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_THREAD_COUNT
#define DEFAULT_THREAD_COUNT 10
#else
#define DEFAULT_THREAD_COUNT CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_THREAD_COUNT
#endif

#ifndef CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_NESTING_DEPTH
#define DEFAULT_NESTING_DEPTH 50
#else
#define DEFAULT_NESTING_DEPTH CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_NESTING_DEPTH
#endif

#ifndef CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_ITERATIONS
#define DEFAULT_ITERATIONS 1000
#else
#define DEFAULT_ITERATIONS CONFIG_EXAMPLES_PTHREAD_CLEANUP_TEST_ITERATIONS
#endif

/* Test IDs */

typedef enum {
	/* Basic functionality tests */
	TC01_SINGLE_CLEANUP_WITH_EXIT = 1,
	TC02_MULTIPLE_CLEANUP_WITH_EXIT,
	TC03_CLEANUP_WITH_POP_EXECUTE,
	TC04_CLEANUP_WITH_POP_NO_EXECUTE,
	TC05_NESTED_PUSH_POP,
	
	/* Cancellation tests */
	TC06_DEFERRED_CANCELLATION,
	TC07_ASYNCHRONOUS_CANCELLATION,
	TC08_CANCELLATION_WITH_CLEANUP,
	TC09_CANCEL_DISABLED,
	TC10_CANCEL_STATE_TRANSITIONS,
	TC11_DETACHED_THREAD_CANCELLATION,
	
	/* Cancellation point tests */
	TC12_CANCEL_AT_COND_WAIT = 12,
	TC13_CANCEL_AT_PTHREAD_JOIN = 13,
	TC13B_CANCEL_AFTER_THREAD_EXIT = 130,  /* Special value for B variant */
	TC14_CANCEL_AT_SEM_WAIT = 14,
	TC15_CANCEL_AT_SLEEP = 15,
	TC16_CANCEL_AT_MULTIPLE_POINTS = 16,
	
	/* Stress tests */
	TC17_HIGH_FREQUENCY_PUSH_POP = 17,
	TC18_MULTI_THREAD_CLEANUP = 18,
	TC19_DEEP_NESTING = 19,
	TC20_RAPID_CREATE_CANCEL = 20,
	TC21_LONG_RUNNING_CLEANUP = 21,
	
	/* Resource cleanup tests */
	TC22_MEMORY_CLEANUP = 22,
	TC23_MUTEX_CLEANUP = 23,
	TC24_SEMAPHORE_CLEANUP = 24,
	TC25_FILE_DESCRIPTOR_CLEANUP = 25,
	TC26_MULTIPLE_RESOURCES = 26,
	TC27_CLEANUP_ORDERING = 27,
	
	/* Edge case tests */
	TC28_NULL_ARGUMENT = 28,
	TC29_CLEANUP_CALLS_EXIT = 29,
	TC30_CLEANUP_DURING_CANCEL = 30,
	TC31_POP_WITHOUT_PUSH = 31,
	TC32_ASYNCHRONOUS_TYPE_CLEANUP = 32,
	TC33_MIXED_CANCELLATION_TYPES = 33,

	/* Advanced syscall & kernel tests */
	TC34_SYSCALL_FROM_LOADABLE_MODULE = 34,
	TC35_SYSCALL_ERROR_HANDLING = 35,
	TC36_PRIORITY_INHERITANCE_CLEANUP = 36,
	TC37_REALTIME_SCHED_CLEANUP = 37,
	TC38_PRIORITY_CHANGE_DURING_CLEANUP = 38,
	TC40_MULTI_HEAP_CLEANUP = 40,
	TC41_SIGNAL_HANDLER_INTERACTION = 41,
	TC42_TIMER_TRIGGERED_CANCELLATION = 42,
	TC43_SIGPROCMASK_CLEANUP = 43,
	TC44_BARRIER_CANCELLATION = 44,
	TC45_RWLOCK_CLEANUP = 45,
	TC46_ONCE_CONTROL_CANCELLATION = 46,
	TC47_TSD_DESTRUCTOR_ORDERING = 47,
	TC48_TSD_CLEANUP_INTERACTION = 48,
	TC49_REENTRANT_CLEANUP = 49,
	TC50_CANCELLATION_DURING_CLEANUP = 50,

	/* New API verification tests */
	TC51_CONCURRENT_PUSH = 51,

	TC52_POP_ERROR_CASES = 52,
	TC53_SELF_CANCEL = 53,
	TC54_DOUBLE_CANCEL = 54,
	TC55_STATE_INHERITANCE = 55,
	TC56_TESTCANCEL_BASIC = 56,
	TC57_TESTCANCEL_CPU_BOUND = 57,
	TC58_MACRO_PAIRING = 58,
	TC59_RAPID_STATE_TOGGLE = 59,
	TC60_COMBINED_API_TEST = 60,

	/* Must be last - used for array sizing */
	TC_MAX = 61
} test_id_t;




/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Test functions */

int test_basic_cleanup_with_exit(void);
int test_multiple_cleanup_handlers(void);
int test_cleanup_pop_execute(void);
int test_cleanup_pop_no_execute(void);
int test_nested_push_pop(void);

int test_deferred_cancellation(void);
int test_asynchronous_cancellation(void);
int test_cancellation_with_cleanup(void);
int test_cancel_disabled_thread(void);
int test_cancel_state_transitions(void);
int test_detached_thread_cancellation(void);

int test_cancel_at_cond_wait(void);
int test_cancel_at_pthread_join(void);
int test_cancel_after_thread_exit(void);
int test_cancel_at_sem_wait(void);
int test_cancel_at_sleep(void);
int test_cancel_at_multiple_points(void);

int test_high_frequency_push_pop(void);
int test_multi_thread_cleanup(void);
int test_deep_nesting(void);
int test_rapid_create_cancel(void);
int test_long_running_cleanup(void);

int test_memory_cleanup(void);
int test_mutex_cleanup(void);
int test_semaphore_cleanup(void);
int test_file_descriptor_cleanup(void);
int test_multiple_resources(void);
int test_cleanup_ordering(void);

int test_null_argument_cleanup(void);
int test_cleanup_calls_exit(void);
int test_cleanup_during_cancel(void);
int test_pop_without_push(void);
int test_asynchronous_type_cleanup(void);
int test_mixed_cancellation_types(void);

/* Advanced syscall & kernel tests */
int test_syscall_from_loadable_module(void);
int test_syscall_error_handling(void);
int test_stack_overflow_protection(void);
int test_barrier_cancellation(void);
int test_priority_inheritance_cleanup(void);
int test_realtime_sched_cleanup(void);
int test_priority_change_during_cleanup(void);
int test_multi_heap_cleanup(void);
int test_signal_handler_interaction(void);
int test_timer_triggered_cancellation(void);
int test_sigprocmask_cleanup(void);
int test_rwlock_cleanup(void);
int test_once_control_cancellation(void);
int test_tsd_destructor_ordering(void);
int test_tsd_cleanup_interaction(void);
int test_reentrant_cleanup(void);
int test_cancellation_during_cleanup(void);


/* New API verification tests */
int test_concurrent_push(void);
int test_pop_error_cases(void);
int test_self_cancel(void);
int test_double_cancel(void);
int test_cancel_state_inheritance(void);
int test_testcancel_basic(void);
int test_testcancel_cpu_bound(void);
int test_macro_pairing(void);
int test_rapid_state_toggle(void);
int test_combined_api_test(void);

/* Test runner */


void run_test(test_id_t id);
void run_all_tests(void);
void run_test_category(int category);
void show_test_menu(void);

/* Main entry point */

int pthread_cleanup_main(int argc, char *argv[]);

#endif /* __APPS_EXAMPLES_PTHREAD_CLEANUP_TEST_H */
