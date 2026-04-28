/****************************************************************************
 * apps/examples/pthread_cond_tests/pthread_cond_tests_main.c
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
 * Name: pthread_cond_tests_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int pthread_cond_tests_main(int argc, char *argv[])
#endif
{
	int result = TEST_PASS;
	int ret;

	printf("Starting pthread condition variable tests\n");

	/* Test 1: Basic signal/wait functionality */
	ret = cond_basic_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Basic test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 2: Multiple waiters with single signal */
	ret = cond_multiple_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Multiple waiters test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 3: Broadcast functionality */
	ret = cond_broadcast_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Broadcast test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 4: Timeout functionality */
	ret = cond_timeout_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Timeout test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 5: Signal without waiters */
	ret = cond_no_waiter_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Signal without waiters test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 6: Stress test */
	ret = cond_stress_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Stress test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 7: Destroy with waiters */
	ret = cond_destroy_test();
	if (ret != TEST_PASS) {
		printf("ERROR: Destroy test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	/* Test 8: SMP concurrent test */
	ret = cond_smp_test();
	if (ret != TEST_PASS) {
		printf("ERROR: SMP test failed with %d\n", ret);
		result = TEST_FAIL;
	}

	if (result == TEST_PASS) {
		printf("\n========================================\n");
		printf("All tests passed!\n");
		printf("========================================\n");
	} else {
		printf("\n========================================\n");
		printf("Some tests failed!\n");
		printf("========================================\n");
	}

	return result;
}