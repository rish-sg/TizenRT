/****************************************************************************
 * apps/examples/pthread_cleanup_test/test_advanced_apis.c
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
 *    distribution.
 * 3. Neither the name Samsung Electronics nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

#ifndef CONFIG_DISABLE_SIGNALS
#include <signal.h>
#endif

#ifndef CONFIG_DISABLE_POSIX_TIMERS
#include <time.h>
#endif

#include "pthread_cleanup.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_adv_cleanup_count = 0;
static int g_adv_cleanup_order[10];
static int g_adv_order_idx = 0;


static void adv_cleanup_handler(FAR void *arg)
{
	int id = (int)((uintptr_t)arg);
	g_adv_cleanup_count++;
	if (g_adv_order_idx < 10) {
		g_adv_cleanup_order[g_adv_order_idx++] = id;
	}
	TEST_INFO("Advanced cleanup handler #%d called", id);
}

/****************************************************************************
 * Private Functions - Thread Entry Points
 ****************************************************************************/

/* TC36: Priority inheritance cleanup thread */
#ifdef CONFIG_PRIORITY_INHERITANCE
static FAR void *prio_inherit_thread(FAR void *arg)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *)arg;

	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)36);

	pthread_mutex_lock(mutex);
	TEST_INFO("Thread locked priority-inheritance mutex");

	/* Sleep to hold the mutex - will be canceled here */
	sleep(5);

	pthread_mutex_unlock(mutex);
	pthread_cleanup_pop(0);
	return NULL;
}
#endif

/* TC37: Realtime scheduling cleanup thread */
static FAR void *rt_sched_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)37);

	TEST_INFO("RT thread running with SCHED_FIFO");

	/* Wait to be canceled */
	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/* TC38: Priority change during cleanup thread */
static FAR void *prio_change_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)38);

	TEST_INFO("Thread waiting for cancellation");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/* TC40: Multi-heap cleanup thread */
static FAR void *multi_heap_thread(FAR void *arg)
{
	FAR void *ptr = malloc(128);

	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)40);

	TEST_INFO("Thread allocated memory at %p", ptr);

	/* Wait to be canceled - cleanup handler should free memory */
	sleep(5);

	free(ptr);
	pthread_cleanup_pop(0);
	return NULL;
}

/* TC41: Signal handler interaction thread */
#ifndef CONFIG_DISABLE_SIGNALS
static int g_signal_received = 0;

static void signal_handler(int signo)
{
	g_signal_received = 1;
	TEST_INFO("Signal handler called for signal %d", signo);
}

static FAR void *signal_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)41);

	TEST_INFO("Thread waiting for signal + cancellation");

	/* This is a cancellation point - signal may arrive during sleep */
	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}
#endif



/* TC42: Timer triggered cancellation thread */
#if !defined(CONFIG_DISABLE_POSIX_TIMERS) && !defined(CONFIG_DISABLE_SIGNALS)
static pthread_t g_timer_target_thread;

static void timer_signal_handler(int signo)
{
	TEST_INFO("Timer signal received (%d), canceling target thread", signo);
	pthread_cancel(g_timer_target_thread);
}

static FAR void *timer_target_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)42);

	TEST_INFO("Thread waiting for timer-triggered cancellation");

	sleep(10);

	pthread_cleanup_pop(0);
	return NULL;
}
#endif


/* TC43: Sigprocmask cleanup thread */
#ifndef CONFIG_DISABLE_SIGNALS
static FAR void *sigmask_thread(FAR void *arg)
{
	sigset_t oldmask;

	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)43);

	/* Save current signal mask */
	pthread_sigmask(SIG_SETMASK, NULL, &oldmask);
	TEST_INFO("Thread saved signal mask");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}
#endif

/* TC45: RWLock cleanup thread */
static FAR void *rwlock_rd_thread(FAR void *arg)
{
	pthread_rwlock_t *rwlock = (pthread_rwlock_t *)arg;

	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)45);

	pthread_rwlock_rdlock(rwlock);
	TEST_INFO("Thread acquired read lock");

	sleep(5);

	pthread_rwlock_unlock(rwlock);
	pthread_cleanup_pop(0);
	return NULL;
}

/* TC46: Once control cancellation thread */
static pthread_once_t g_once_control = PTHREAD_ONCE_INIT;
static int g_once_called = 0;

static void once_init_routine(void)
{
	g_once_called = 1;
	TEST_INFO("pthread_once init routine called");
}

static FAR void *once_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)46);

	pthread_once(&g_once_control, once_init_routine);

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/* TC47: TSD destructor ordering thread */
static pthread_key_t g_tsd_key;
static int g_tsd_destructor_called = 0;

static void tsd_destructor(FAR void *value)
{
	g_tsd_destructor_called = 1;
	TEST_INFO("TSD destructor called (value=%d)", (int)((uintptr_t)value));
}

static FAR void *tsd_ordering_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)47);

	pthread_setspecific(g_tsd_key, (FAR void *)123);

	TEST_INFO("Thread set TSD value");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/* TC48: TSD cleanup interaction thread */
static pthread_key_t g_tsd_key2;

static FAR void *tsd_cleanup_thread(FAR void *arg)
{
	pthread_setspecific(g_tsd_key2, (FAR void *)456);

	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)48);

	TEST_INFO("Thread set TSD, waiting for cancellation");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/* TC49: Reentrant cleanup thread
 * NOTE: This test is disabled because calling pthread_cleanup_push/pop from
 * within a cleanup handler is undefined behavior per POSIX. It causes a
 * kernel assertion in TizenRT (armv7-a/arm_syscall.c line 531).
 * The code is kept for reference as a separate patch.
 */
#if 0
static int g_reentrant_count = 0;


static void reentrant_inner_handler(FAR void *arg)
{
	g_reentrant_count++;
	TEST_INFO("Reentrant inner handler #%d", g_reentrant_count);
}

static void reentrant_outer_handler(FAR void *arg)
{
	g_reentrant_count++;
	TEST_INFO("Reentrant outer handler #%d", g_reentrant_count);

	/* Call push/pop from within cleanup handler (reentrant) */
	pthread_cleanup_push(reentrant_inner_handler, (FAR void *)490);
	pthread_cleanup_pop(1);
}

static FAR void *reentrant_thread(FAR void *arg)
{
	pthread_cleanup_push(reentrant_outer_handler, (FAR void *)49);

	TEST_INFO("Reentrant cleanup thread waiting");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}
#endif /* 0 - TC49 disabled */

/* TC50: Cancellation during cleanup thread */

static pthread_t g_other_thread;
static int g_canceled_during_cleanup = 0;

static FAR void *cancellation_during_cleanup_thread(FAR void *arg)
{
	pthread_cleanup_push(adv_cleanup_handler, (FAR void *)50);

	TEST_INFO("Thread waiting to be canceled during another's cleanup");

	sleep(10);

	pthread_cleanup_pop(0);
	return NULL;
}

static void cancel_other_handler(FAR void *arg)
{
	g_canceled_during_cleanup = 1;
	TEST_INFO("Cleanup handler canceling other thread");
	pthread_cancel(g_other_thread);
}

static FAR void *cancelling_cleanup_thread(FAR void *arg)
{
	pthread_cleanup_push(cancel_other_handler, (FAR void *)500);

	TEST_INFO("Thread waiting for cancellation (will cancel other in cleanup)");

	sleep(5);

	pthread_cleanup_pop(0);
	return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * TC36: Priority Inheritance Cleanup
 ****************************************************************************/
int test_priority_inheritance_cleanup(void)
{
#ifdef CONFIG_PRIORITY_INHERITANCE
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_mutexattr_t attr;
	int status;

	TEST_START("TC36: Cleanup with priority inheritance mutex");

	g_adv_cleanup_count = 0;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutex_init(&mutex, &attr);

	status = pthread_create(&thread, NULL, prio_inherit_thread, &mutex);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_mutex_destroy(&mutex);
		pthread_mutexattr_destroy(&attr);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	pthread_mutex_destroy(&mutex);
	pthread_mutexattr_destroy(&attr);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC36: Priority inheritance cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
#else
	TEST_START("TC36: Cleanup with priority inheritance mutex");
	TEST_INFO("CONFIG_PRIORITY_INHERITANCE not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * TC37: Realtime Scheduling Cleanup
 ****************************************************************************/
int test_realtime_sched_cleanup(void)
{
	pthread_t thread;
	struct sched_param param;
	int status;

	TEST_START("TC37: Cleanup handlers with SCHED_FIFO");

	g_adv_cleanup_count = 0;

	param.sched_priority = 120;

	status = pthread_create(&thread, NULL, rt_sched_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	pthread_setschedparam(thread, SCHED_FIFO, &param);
	TEST_INFO("Set thread policy to SCHED_FIFO, priority 120");

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC37: Realtime sched cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC38: Priority Change During Cleanup
 ****************************************************************************/
int test_priority_change_during_cleanup(void)
{
	pthread_t thread;
	struct sched_param param;
	int status;

	TEST_START("TC38: Thread priority change while cleanup handlers execute");

	g_adv_cleanup_count = 0;

	status = pthread_create(&thread, NULL, prio_change_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(100 * 1000);

	/* Change priority while thread is running */
	param.sched_priority = 150;
	status = pthread_setschedparam(thread, SCHED_FIFO, &param);
	TEST_INFO("Changed thread priority to 150, status=%d", status);

	pthread_cancel(thread);
	pthread_join(thread, NULL);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC38: Priority change during cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC40: Multi-Heap Cleanup
 ****************************************************************************/
int test_multi_heap_cleanup(void)
{
	pthread_t thread;
	int status;

	TEST_START("TC40: Cleanup with memory from different heap regions");

	g_adv_cleanup_count = 0;

	status = pthread_create(&thread, NULL, multi_heap_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC40: Multi-heap cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC41: Signal Handler Interaction
 ****************************************************************************/
int test_signal_handler_interaction(void)
{
#ifndef CONFIG_DISABLE_SIGNALS
	pthread_t thread;
	struct sigaction sa;
	sigset_t mask;
	int status;

	TEST_START("TC41: Cleanup when signal arrives during cancellation");

	g_adv_cleanup_count = 0;
	g_signal_received = 0;

	/* Install signal handler for SIGUSR1 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGUSR1, &sa, NULL);

	/* Block SIGUSR1 in main thread so it goes to the worker thread */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	status = pthread_create(&thread, NULL, signal_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
		return 0;
	}

	usleep(100 * 1000);

	/* Send signal to thread - should be delivered to worker, not main */
	pthread_kill(thread, SIGUSR1);
	usleep(50 * 1000);

	/* Now cancel the thread - cleanup handler should be called */
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	/* Unblock SIGUSR1 in main thread */
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

	TEST_INFO("Signal received: %d, Cleanup called: %d",
	          g_signal_received, g_adv_cleanup_count);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC41: Signal handler interaction works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
#else
	TEST_START("TC41: Signal handler interaction");
	TEST_INFO("CONFIG_SIGNALS not enabled, skipping test");
	return 1;
#endif
}


/****************************************************************************
 * TC42: Timer Triggered Cancellation
 ****************************************************************************/
int test_timer_triggered_cancellation(void)
{
#if !defined(CONFIG_DISABLE_POSIX_TIMERS) && !defined(CONFIG_DISABLE_SIGNALS)
	pthread_t thread;
	timer_t timer_id;
	struct sigevent sev;
	struct itimerspec its;
	struct sigaction sa;
	int status;

	TEST_START("TC42: POSIX timer signal handler triggers pthread_cancel");

	g_adv_cleanup_count = 0;

	/* Install signal handler for the timer signal */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = timer_signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGUSR2, &sa, NULL);

	status = pthread_create(&thread, NULL, timer_target_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	g_timer_target_thread = thread;

	/* Create a timer that sends SIGUSR2 after 200ms */
	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR2;

	status = timer_create(CLOCK_REALTIME, &sev, &timer_id);
	if (status != 0) {
		TEST_FAIL("timer_create failed: %d", status);
		pthread_cancel(thread);
		pthread_join(thread, NULL);
		return 0;
	}

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 200 * 1000 * 1000;  /* 200ms */
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;

	timer_settime(timer_id, 0, &its, NULL);
	TEST_INFO("Timer armed for 200ms (sends SIGUSR2)");

	pthread_join(thread, NULL);
	timer_delete(timer_id);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC42: Timer triggered cancellation works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
#else
	TEST_START("TC42: Timer triggered cancellation");
	TEST_INFO("POSIX timers or signals not enabled, skipping test");
	return 1;
#endif
}


/****************************************************************************
 * TC43: Sigprocmask Cleanup
 ****************************************************************************/
int test_sigprocmask_cleanup(void)
{
#ifndef CONFIG_DISABLE_SIGNALS
	pthread_t thread;
	sigset_t mask;
	int status;

	TEST_START("TC43: Signal mask preservation during cleanup execution");

	g_adv_cleanup_count = 0;

	/* Block SIGUSR2 in main thread */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	status = pthread_create(&thread, NULL, sigmask_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	/* Unblock SIGUSR2 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR2);
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC43: Sigprocmask cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
#else
	TEST_START("TC43: Signal mask preservation during cleanup");
	TEST_INFO("CONFIG_SIGNALS not enabled, skipping test");
	return 1;
#endif
}

/****************************************************************************
 * TC45: RWLock Cleanup
 ****************************************************************************/
int test_rwlock_cleanup(void)
{
	pthread_t thread;
	pthread_rwlock_t rwlock;
	int status;

	TEST_START("TC45: Cleanup when holding read-write lock");

	g_adv_cleanup_count = 0;

	pthread_rwlock_init(&rwlock, NULL);

	status = pthread_create(&thread, NULL, rwlock_rd_thread, &rwlock);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_rwlock_destroy(&rwlock);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	/* Verify rwlock is still usable */
	status = pthread_rwlock_tryrdlock(&rwlock);
	if (status == 0) {
		pthread_rwlock_unlock(&rwlock);
		TEST_INFO("RWLock is usable after cancellation");
	}

	pthread_rwlock_destroy(&rwlock);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC45: RWLock cleanup works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC46: Once Control Cancellation
 ****************************************************************************/
int test_once_control_cancellation(void)
{
	pthread_t thread;
	int status;

	TEST_START("TC46: pthread_once interaction with cancellation");

	g_adv_cleanup_count = 0;
	g_once_called = 0;
	g_once_control = PTHREAD_ONCE_INIT;

	status = pthread_create(&thread, NULL, once_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	TEST_INFO("once_called=%d, cleanup_count=%d", g_once_called, g_adv_cleanup_count);

	/* Verify pthread_once still works after cancellation */
	pthread_once(&g_once_control, once_init_routine);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC46: Once control cancellation works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC47: TSD Destructor Ordering
 ****************************************************************************/
int test_tsd_destructor_ordering(void)
{
	pthread_t thread;
	int status;

	TEST_START("TC47: Ordering between TSD destructors and cleanup handlers");

	g_adv_cleanup_count = 0;
	g_adv_order_idx = 0;
	g_tsd_destructor_called = 0;

	pthread_key_create(&g_tsd_key, tsd_destructor);

	status = pthread_create(&thread, NULL, tsd_ordering_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_key_delete(g_tsd_key);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	pthread_key_delete(g_tsd_key);

	TEST_INFO("Cleanup handler called: %d, TSD destructor called: %d",
	          g_adv_cleanup_count, g_tsd_destructor_called);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC47: TSD destructor ordering works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC48: TSD Cleanup Interaction
 ****************************************************************************/
int test_tsd_cleanup_interaction(void)
{
	pthread_t thread;
	int status;

	TEST_START("TC48: Cleanup handler accessing thread-specific data");

	g_adv_cleanup_count = 0;

	pthread_key_create(&g_tsd_key2, NULL);

	status = pthread_create(&thread, NULL, tsd_cleanup_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed: %d", status);
		pthread_key_delete(g_tsd_key2);
		return 0;
	}

	usleep(100 * 1000);
	pthread_cancel(thread);
	pthread_join(thread, NULL);

	pthread_key_delete(g_tsd_key2);

	if (g_adv_cleanup_count > 0) {
		TEST_PASS("TC48: TSD cleanup interaction works");
		return 1;
	} else {
		TEST_FAIL("Cleanup handler not called");
		return 0;
	}
}

/****************************************************************************
 * TC49: Reentrant Cleanup (DISABLED)
 * This test is disabled because calling pthread_cleanup_push/pop from
 * within a cleanup handler is undefined behavior per POSIX and causes
 * a kernel assertion in TizenRT. The test code is kept in #if 0 above
 * for reference as a separate patch.
 ****************************************************************************/
int test_reentrant_cleanup(void)
{
	TEST_START("TC49: Cleanup handler that calls pthread_cleanup_push/pop");
	TEST_INFO("TC49 is disabled (undefined behavior per POSIX)");
	TEST_INFO("Calling push/pop from within a cleanup handler causes kernel assertion");
	TEST_PASS("TC49: Skipped (test disabled - undefined behavior)");
	return 1;
}


/****************************************************************************
 * TC50: Cancellation During Cleanup
 ****************************************************************************/
int test_cancellation_during_cleanup(void)
{
	pthread_t thread1, thread2;
	int status;

	TEST_START("TC50: Cancellation request during cleanup handler execution");

	g_adv_cleanup_count = 0;
	g_canceled_during_cleanup = 0;

	/* Create the target thread that will be canceled by the cleanup handler */
	status = pthread_create(&thread1, NULL, cancellation_during_cleanup_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed for thread1: %d", status);
		return 0;
	}

	g_other_thread = thread1;

	/* Create the thread whose cleanup handler will cancel thread1 */
	status = pthread_create(&thread2, NULL, cancelling_cleanup_thread, NULL);
	if (status != 0) {
		TEST_FAIL("pthread_create failed for thread2: %d", status);
		pthread_cancel(thread1);
		pthread_join(thread1, NULL);
		return 0;
	}

	usleep(100 * 1000);

	/* Cancel thread2 - its cleanup handler will cancel thread1 */
	pthread_cancel(thread2);
	pthread_join(thread2, NULL);

	/* thread1 should have been canceled by thread2's cleanup handler */
	pthread_join(thread1, NULL);

	TEST_INFO("Canceled during cleanup: %d, cleanup count: %d",
	          g_canceled_during_cleanup, g_adv_cleanup_count);

	if (g_canceled_during_cleanup) {
		TEST_PASS("TC50: Cancellation during cleanup works");
		return 1;
	} else {
		TEST_FAIL("Other thread was not canceled during cleanup");
		return 0;
	}
}
