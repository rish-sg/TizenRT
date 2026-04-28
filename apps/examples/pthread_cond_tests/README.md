# Pthread Condition Variable Test Suite

A comprehensive test suite for testing `pthread_cond_*` functions in TizenRT.

## Overview

This test suite validates the correctness of POSIX condition variable implementations in TizenRT. It covers basic functionality, edge cases, concurrent operations, and multi-core (SMP) scenarios.

## How to Run

```bash
TASH>> pthread_cond_tests
```

## Test Cases

### Test 1: Basic Signal/Wait (`cond_basic_test.c`)

**Purpose:** Verify basic condition variable functionality.

**What it does:**
1. Creates one waiter thread that waits on a condition variable
2. Creates one signaler thread that signals after setting shared data
3. Verifies the waiter wakes up and receives correct data

**Pass Criteria:**
- Waiter thread blocks successfully
- Signal wakes the waiter
- Shared data is correctly received

---

### Test 2: Multiple Waiters (`cond_multiple_test.c`)

**Purpose:** Verify that `pthread_cond_signal()` wakes only ONE waiting thread.

**What it does:**
1. Creates 3 waiter threads that all wait on the same condvar
2. Signals ONCE - only one thread should wake up
3. Then broadcasts to wake remaining threads

**Pass Criteria:**
- Only 1 thread wakes after signal (not all 3)
- All 3 threads complete after broadcast

---

### Test 3: Broadcast (`cond_broadcast_test.c`)

**Purpose:** Verify that `pthread_cond_broadcast()` wakes ALL waiting threads.

**What it does:**
1. Creates 3 waiter threads that all wait on the same condvar
2. Broadcasts once - all threads should wake up
3. Verifies all threads woke up

**Pass Criteria:**
- All 3 threads wake up after broadcast
- No thread is left waiting

---

### Test 4: Timeout (`cond_timeout_test.c`)

**Purpose:** Verify `pthread_cond_timedwait()` returns ETIMEDOUT when timeout expires.

**What it does:**
1. Creates a waiter thread using timed wait with 500ms timeout
2. Main thread does NOT signal
3. Waiter should timeout after 500ms

**Pass Criteria:**
- `pthread_cond_timedwait()` returns `ETIMEDOUT`
- Timeout occurs within reasonable time window

---

### Test 5: Signal Without Waiters (`cond_no_waiter_test.c`)

**Purpose:** Verify signaling with no waiters doesn't crash or cause issues.

**What it does:**
1. Signals a condvar with no waiting threads
2. Broadcasts to a condvar with no waiting threads
3. Multiple signals with no waiters

**Pass Criteria:**
- All operations return success (0)
- No crashes or errors

---

### Test 6: Stress Test (`cond_stress_test.c`)

**Purpose:** Test high-frequency concurrent operations for race conditions.

**What it does:**
1. Creates 10 waiter threads and 1 signaler thread
2. Runs for 100+ iterations of wait/signal cycles
3. Tracks signals sent and wakeups received

**Pass Criteria:**
- All threads complete without hanging
- No deadlocks or crashes
- Wakeups ≤ Signals (signals without waiters are lost)

---

### Test 7: Destroy With Waiters (`cond_destroy_test.c`)

**Purpose:** Document POSIX undefined behavior when destroying condvar with waiters.

**What it does:**
1. Creates a waiter thread blocked on condvar
2. Destroys the condvar while thread is waiting
3. Uses timed wait so thread can timeout and exit

**Pass Criteria:**
- Test completes without hanging
- Documents TizenRT's behavior (succeeds, POSIX undefined)

**Note:** POSIX defines destroying a condvar with waiting threads as "undefined behavior". TizenRT allows the destroy to succeed, but the waiting thread would block forever without a timeout.

---

### Test 8: SMP Concurrent (`cond_smp_test.c`)

**Purpose:** Test condition variables on multi-core (SMP) systems.

**What it does:**
1. Detects number of CPUs (requires 2+)
2. Creates waiter threads pinned to CPU 0, CPU 1, etc.
3. Creates signaler threads pinned to different CPUs
4. Runs concurrent wait/signal across CPUs

**Pass Criteria:**
- Threads run on correct CPUs
- No deadlocks or race conditions
- Test completes successfully

**Note:** This test is skipped on single-core systems or when `CONFIG_SMP` is not defined.

---

## Summary Table

| Test | Threads | Key API Tested | Validates |
|------|---------|----------------|-----------|
| Basic | 2 | `pthread_cond_wait`, `pthread_cond_signal` | Basic functionality |
| Multiple | 4 | Signal wakes only ONE | Single wakeup semantics |
| Broadcast | 4 | `pthread_cond_broadcast` | All wakeup semantics |
| Timeout | 1 | `pthread_cond_timedwait` | Timeout expiration |
| No Waiter | 1 | Signal with no waiters | Edge case handling |
| Stress | 11 | High-frequency ops | Race conditions |
| Destroy | 1 | `pthread_cond_destroy` | Undefined behavior |
| SMP | 4+ | CPU affinity + condvar | Multi-core safety |

---

## Files

| File | Purpose |
|------|---------|
| `pthread_cond_tests.h` | Header with macros and declarations |
| `pthread_cond_tests_main.c` | Main entry point, runs all tests |
| `cond_basic_test.c` | Test 1 implementation |
| `cond_multiple_test.c` | Test 2 implementation |
| `cond_broadcast_test.c` | Test 3 implementation |
| `cond_timeout_test.c` | Test 4 implementation |
| `cond_no_waiter_test.c` | Test 5 implementation |
| `cond_stress_test.c` | Test 6 implementation |
| `cond_destroy_test.c` | Test 7 implementation |
| `cond_smp_test.c` | Test 8 implementation |
| `Makefile` | Build configuration |
| `Kconfig` | Configuration options |

---

## Configuration

Enable the test suite in your configuration:

```
CONFIG_EXAMPLES_PTHREAD_COND_TESTS=y
```

For SMP testing, also enable:

```
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2  (or more)
```

---

## Expected Output

```
TASH>> pthread_cond_tests
Starting pthread condition variable tests

=== Starting Basic signal/wait test ===
Waiter thread started
Waiter thread is ready and waiting
Signaler thread set data and will signal
Waiter thread received signal and data is correct
PASS: Basic signal/wait test

=== Starting Multiple waiters with single signal test ===
...
PASS: Multiple waiters with single signal test

=== Starting Broadcast functionality test ===
...
PASS: Broadcast functionality test

=== Starting Timeout functionality test ===
...
PASS: Timeout functionality test

=== Starting Signal without waiters test ===
...
PASS: Signal without waiters test

=== Starting Stress test ===
...
Running stress test...
Stopping threads...
Stress test completed successfully (115 iterations)
PASS: Stress test

=== Starting Destroy with waiters test ===
...
PASS: Destroy with waiters test

=== Starting SMP concurrent test ===
...
Running SMP test...
Stopping threads...
SMP test completed successfully (2 CPUs, 102 iterations)
PASS: SMP concurrent test

========================================
All tests passed!
========================================