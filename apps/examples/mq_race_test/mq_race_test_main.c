/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * apps/examples/mq_race_test/mq_race_test_main.c
 *
 *   This test reproduces the race condition between task_terminate() and
 *   mq_dosend() that triggers the assert at line 447 in mq_sndinternal.c.
 *
 *   The race window exists in task_terminate() between:
 *     - sched_removereadytorun() which removes the TCB from the
 *       g_waitingformqnotempty list
 *     - mq_recover() (called via task_exithook() -> task_recover()) which
 *       decrements nwaitnotempty
 *
 *   During this window, nwaitnotempty > 0 but the TCB is already gone from
 *   the waiting list. If another task calls mq_send() on the same queue
 *   during this window, mq_dosend() sees nwaitnotempty > 0, searches the
 *   list, finds nothing, and hits ASSERT(btcb).
 *
 *   Test strategy:
 *     - Receiver task (low priority): blocks on mq_receive() on empty queue
 *       -> sets nwaitnotempty = 1, TCB added to g_waitingformqnotempty
 *     - Deleter task (below main priority): calls task_delete(receiver_pid)
 *       -> triggers task_terminate() which opens the race window
 *     - Sender task (high priority): polls g_mq_race_window_open flag,
 *       then calls mq_send() on the same queue
 *       -> since the flag is set inside the race window, ASSERT fires
 *
 *   The flag g_mq_race_window_open is set by task_terminate() (in the kernel)
 *   after sched_removereadytorun() removes the TCB but before mq_recover()
 *   decrements the count. The sender polls this flag and calls mq_send()
 *   only when the flag is set, guaranteeing the assert is hit.
 *
 *   This requires a flat build so the kernel can access the app's global.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sched.h>
#include <errno.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MQ_RACE_TEST_QUEUE_NAME    "/mqrace"
#define MQ_RACE_TEST_MSG            "RACE_TEST_MSG"
#define MQ_RACE_TEST_MSGLEN        (13)
#define MQ_RACE_TEST_MAXMSG        (1024)
#define MQ_RACE_TEST_MSGSIZE       (MQ_RACE_TEST_MSGLEN)

/* Number of iterations to run the race test. */
#define MQ_RACE_TEST_ITERATIONS    (1000)

/* Stack size for test tasks */
#define MQ_RACE_TEST_STACKSIZE     (8192)

/* Task priorities. Main (the TASH shell) runs at priority 100.
 * - Receiver: 50 (low - blocks on mq_receive)
 * - Deleter:  80 (below main - calls task_delete)
 * - Sender:   150 (high - calls mq_send during race window)
 */
#define MQ_RACE_TEST_PRIO_RECEIVER  (50)
#define MQ_RACE_TEST_PRIO_DELETER   (80)
#define MQ_RACE_TEST_PRIO_SENDER    (150)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mqd_t g_mqfd;
static volatile pid_t g_receiver_pid;
static volatile int g_receiver_ready;
static volatile int g_iteration_done;

/* Global flag set by task_terminate() in the kernel (debug code).
 * The sender task polls this flag to know when the race window is open.
 * Must be non-static so the kernel can find it via extern.
 */
volatile int g_mq_race_window_open = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: receiver_task
 *
 * Description:
 *   This task blocks on mq_receive(). Since the queue is empty, it will
 *   block, setting nwaitnotempty = 1 and adding its TCB to
 *   g_waitingformqnotempty.
 *
 ****************************************************************************/

static int receiver_task(int argc, char *argv[])
{
	char msg_buffer[MQ_RACE_TEST_MSGSIZE];
	ssize_t nbytes;

	/* Signal that the receiver is about to block on mq_receive() */
	g_receiver_ready = 1;

	/* This will block because the queue is empty */
	nbytes = mq_receive(g_mqfd, msg_buffer, MQ_RACE_TEST_MSGSIZE, NULL);

	if (nbytes < 0) {
		/* Expected to be killed before receiving a message */
		printf("[mq_race_test] receiver: mq_receive interrupted (errno=%d)\n", errno);
	} else {
		printf("[mq_race_test] receiver: mq_receive succeeded (unexpected)\n");
	}

	return 0;
}

/****************************************************************************
 * Name: sender_task
 *
 * Description:
 *   This task polls the g_mq_race_window_open flag. When the flag is set
 *   by task_terminate() (after the TCB is removed from the waiting list
 *   but before mq_recover() decrements nwaitnotempty), the sender calls
 *   mq_send() on the same queue. This triggers the assert in mq_dosend()
 *   because nwaitnotempty > 0 but the TCB is gone from the list.
 *
 *   The sender has the highest priority so it preempts the deleter's
 *   busy-wait loop inside task_terminate().
 *
 ****************************************************************************/

static int sender_task(int argc, char *argv[])
{
	char msg_buffer[MQ_RACE_TEST_MSGSIZE];

	memcpy(msg_buffer, MQ_RACE_TEST_MSG, MQ_RACE_TEST_MSGLEN);

	/* Poll for the race window flag. This is set by task_terminate()
	 * after sched_removereadytorun() removes the TCB from the waiting
	 * list but before mq_recover() decrements nwaitnotempty.
	 * The usleep(1) yields the CPU so the deleter can run.
	 */
	while (!g_mq_race_window_open) {
		usleep(1);
	}

	/* The flag is set - we are now in the race window!
	 * nwaitnotempty > 0 but the receiver's TCB is gone from the list.
	 * mq_send() -> mq_dosend() will see nwaitnotempty > 0, search the
	 * list, find nothing, and hit ASSERT(btcb) at line 447.
	 */
	mq_send(g_mqfd, msg_buffer, MQ_RACE_TEST_MSGLEN, 42);

	return 0;
}

/****************************************************************************
 * Name: deleter_task
 *
 * Description:
 *   This task waits for the receiver to be blocked on mq_receive(),
 *   then deletes the receiver task via task_delete(). This triggers
 *   task_terminate() which opens the race window (sets g_mq_race_window_open).
 *
 *   The deleter has priority below main (80 < 100) so main can create
 *   the sender task before the deleter runs.
 *
 ****************************************************************************/

static int deleter_task(int argc, char *argv[])
{
	/* Wait for the receiver to be ready (blocked on mq_receive) */
	while (!g_receiver_ready) {
		usleep(10);
	}

	/* Small delay to ensure the receiver is actually blocked in mq_receive */
	usleep(100);

	/* Delete the receiver task - this triggers task_terminate() which
	 * sets g_mq_race_window_open = 1 and enters a busy-wait loop.
	 * During the busy-wait, the sender (higher priority) will preempt
	 * and call mq_send() -> ASSERT fires.
	 */
	if (g_receiver_pid > 0) {
		task_delete(g_receiver_pid);
	}

	g_iteration_done = 1;

	return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mq_race_test_main
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int main(int argc, FAR char *argv[])
#else
int mq_race_test_main(int argc, char *argv[])
#endif
{
	struct mq_attr attr;
	int iteration;
	int sender_pid;
	int deleter_pid;
	int receiver_pid;

	printf("=== Message Queue Race Condition Test ===\n");
	printf("This test attempts to trigger the assert at line 447 in mq_dosend()\n");
	printf("by racing task_terminate() with mq_send().\n");
	printf("Running %d iterations...\n\n", MQ_RACE_TEST_ITERATIONS);

	/* Set up message queue attributes */
	attr.mq_maxmsg = MQ_RACE_TEST_MAXMSG;
	attr.mq_msgsize = MQ_RACE_TEST_MSGSIZE;
	attr.mq_flags = 0;

	printf("Priorities: receiver=%d deleter=%d sender=%d (main=100)\n",
	       MQ_RACE_TEST_PRIO_RECEIVER, MQ_RACE_TEST_PRIO_DELETER,
	       MQ_RACE_TEST_PRIO_SENDER);

	for (iteration = 0; iteration < MQ_RACE_TEST_ITERATIONS; iteration++) {
		/* Unlink any existing queue with the same name */
		mq_unlink(MQ_RACE_TEST_QUEUE_NAME);

		/* Create and open the message queue for read-write */
		g_mqfd = mq_open(MQ_RACE_TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0666, &attr);
		if (g_mqfd == (mqd_t)-1) {
			printf("[mq_race_test] ERROR: mq_open failed (errno=%d) at iteration %d\n",
			       errno, iteration);
			goto cleanup;
		}

		/* Reset state for this iteration */
		g_receiver_pid = 0;
		g_receiver_ready = 0;
		g_iteration_done = 0;
		g_mq_race_window_open = 0;

		/* Create the receiver task (low priority) - it will block on mq_receive() */
		receiver_pid = task_create("mq_receiver", MQ_RACE_TEST_PRIO_RECEIVER,
		                           MQ_RACE_TEST_STACKSIZE, receiver_task, NULL);
		if (receiver_pid < 0) {
			printf("[mq_race_test] ERROR: Failed to create receiver task (errno=%d)\n", errno);
			mq_close(g_mqfd);
			mq_unlink(MQ_RACE_TEST_QUEUE_NAME);
			goto cleanup;
		}
		g_receiver_pid = receiver_pid;

		/* Create the deleter task (below main priority) - it will task_delete(receiver).
		 * Since deleter priority (80) < main priority (100), main continues running
		 * and can create the sender task.
		 */
		deleter_pid = task_create("mq_deleter", MQ_RACE_TEST_PRIO_DELETER,
		                          MQ_RACE_TEST_STACKSIZE, deleter_task, NULL);
		if (deleter_pid < 0) {
			printf("[mq_race_test] ERROR: Failed to create deleter task (errno=%d)\n", errno);
			task_delete(receiver_pid);
			mq_close(g_mqfd);
			mq_unlink(MQ_RACE_TEST_QUEUE_NAME);
			goto cleanup;
		}

		/* Create the sender task (high priority) - it will poll for the
		 * race window flag and call mq_send(). Since sender priority (150)
		 * > main priority (100), the sender preempts main. But the sender
		 * immediately enters the polling loop (usleep(1)) which yields
		 * back to main, and then to the deleter.
		 */
		sender_pid = task_create("mq_sender", MQ_RACE_TEST_PRIO_SENDER,
		                         MQ_RACE_TEST_STACKSIZE, sender_task, NULL);
		if (sender_pid < 0) {
			printf("[mq_race_test] ERROR: Failed to create sender task (errno=%d)\n", errno);
			task_delete(deleter_pid);
			task_delete(receiver_pid);
			mq_close(g_mqfd);
			mq_unlink(MQ_RACE_TEST_QUEUE_NAME);
			goto cleanup;
		}

		/* Wait for the deleter to finish (it signals g_iteration_done) */
		while (!g_iteration_done) {
			usleep(1000);
		}

		/* Give some time for the sender to finish */
		usleep(5000);

		/* Clean up: delete sender and deleter if still running */
		task_delete(sender_pid);
		task_delete(deleter_pid);

		/* Close and unlink the queue */
		mq_close(g_mqfd);
		mq_unlink(MQ_RACE_TEST_QUEUE_NAME);

		if ((iteration + 1) % 100 == 0) {
			printf("[mq_race_test] Completed %d/%d iterations (no assert triggered)\n",
			       iteration + 1, MQ_RACE_TEST_ITERATIONS);
		}
	}

	printf("\n[mq_race_test] Test completed successfully after %d iterations.\n",
	       MQ_RACE_TEST_ITERATIONS);
	printf("[mq_race_test] If you see this message, the race was not triggered.\n");
	printf("[mq_race_test] On an unfixed kernel, the assert would have fired.\n");

cleanup:
	return 0;
}
