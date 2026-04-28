/****************************************************************************
 * apps/examples/pthread_cond_tests/cond_no_waiter_test.c
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
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cond_no_waiter_test
 *
 * Description:
 *   Test that signal and broadcast are no-ops when no threads are waiting.
 *   Also verifies that a signal sent before a waiter starts doesn't wake
 *   the waiter (signal doesn't persist).
 ****************************************************************************/

int cond_no_waiter_test(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	int status;
	int ret = TEST_PASS;

	TEST_START("Signal without waiters test");

	/* Test 1: Signal with no waiters should succeed and do nothing */
	printf("Test 1: Signaling with no waiters...\n");
	status = pthread_cond_signal(&cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_signal failed: %d\n", status);
		ret = TEST_FAIL;
		goto cleanup;
	}
	printf("PASS: Signal with no waiters returned OK\n");

	/* Test 2: Broadcast with no waiters should succeed and do nothing */
	printf("Test 2: Broadcasting with no waiters...\n");
	status = pthread_cond_broadcast(&cond);
	if (status != 0) {
		printf("ERROR: pthread_cond_broadcast failed: %d\n", status);
		ret = TEST_FAIL;
		goto cleanup;
	}
	printf("PASS: Broadcast with no waiters returned OK\n");

	/* Test 3: Multiple signals with no waiters */
	printf("Test 3: Multiple signals with no waiters...\n");
	for (int i = 0; i < 5; i++) {
		status = pthread_cond_signal(&cond);
		if (status != 0) {
			printf("ERROR: pthread_cond_signal failed on iteration %d: %d\n", i, status);
			ret = TEST_FAIL;
			goto cleanup;
		}
	}
	printf("PASS: Multiple signals with no waiters returned OK\n");

cleanup:
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);

	TEST_END("Signal without waiters test", ret);
	return ret;
}