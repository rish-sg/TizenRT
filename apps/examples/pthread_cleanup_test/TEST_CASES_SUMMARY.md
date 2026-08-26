# Pthread Cleanup & Cancellation Test Cases Summary

## Overview

This document describes all test cases in the pthread cleanup and cancellation test suite for TizenRT. The test suite verifies the correct functioning of the following APIs:

- `pthread_cleanup_push()`
- `pthread_cleanup_pop()`
- `enter_cancellation_point()`
- `leave_cancellation_point()`

---

## Test Case Summary

| ID | Test Name | Category | Status |
|----|-----------|----------|--------|
| TC01 | Single cleanup handler with pthread_exit | Basic | ✅ Active |
| TC02 | Multiple cleanup handlers with pthread_exit | Basic | ✅ Active |
| TC03 | Cleanup handler with pop(execute=1) | Basic | ✅ Active |
| TC04 | Cleanup handler with pop(execute=0) | Basic | ✅ Active |
| TC05 | Nested push/pop pairs | Basic | ✅ Active |
| TC06 | Deferred cancellation | Cancellation | ✅ Active |
| TC07 | Asynchronous cancellation | Cancellation | ✅ Active |
| TC08 | Cancellation with cleanup handlers | Cancellation | ✅ Active |
| TC09 | Cancel disabled thread | Cancellation | ✅ Active |
| TC10 | Cancel state transitions | Cancellation | ✅ Active |
| TC11 | Detached thread cancellation | Cancellation | ✅ Active |
| TC12 | Cancel at cond_wait | Cancellation Point | ✅ Active |
| TC13 | Cancel at pthread_join | Cancellation Point | ✅ Active |
| TC13B | Cancel after thread exited (ESRCH) | Cancellation Point | ✅ Active |
| TC14 | Cancel at sem_wait | Cancellation Point | ✅ Active |
| TC15 | Cancel at sleep | Cancellation Point | ✅ Active |
| TC16 | Multiple cancellation points | Cancellation Point | ✅ Active |
| TC17 | High-frequency push/pop | Stress | ✅ Active |
| TC18 | Multi-thread cleanup | Stress | ✅ Active |
| TC19 | Deep nesting (50+ levels) | Stress | ✅ Active |
| TC20 | Rapid create/cancel cycles | Stress | ✅ Active |
| TC21 | Long-running cleanup handlers | Stress | ✅ Active |
| TC22 | Memory cleanup | Resource | ✅ Active |
| TC23 | Mutex cleanup | Resource | ✅ Active |
| TC24 | Semaphore cleanup | Resource | ✅ Active |
| TC25 | File descriptor cleanup | Resource | ✅ Active |
| TC26 | Multiple resources | Resource | ✅ Active |
| TC27 | Cleanup ordering verification | Resource | ✅ Active |
| TC28 | NULL argument to cleanup handler | Edge Case | ✅ Active |
| TC29 | Cleanup handler calls pthread_exit | Edge Case | ✅ Active |
| TC30 | Cleanup during cancel | Edge Case | ✅ Active |
| TC31 | Pop without matching push | Edge Case | ✅ Active |
| TC32 | Asynchronous type cleanup | Edge Case | ✅ Active |
| TC33 | Mixed cancellation types | Edge Case | ✅ Active |
| TC34 | Syscall from loadable module | Advanced Kernel | ✅ Active |
| TC35 | Syscall error handling | Advanced Kernel | ✅ Active |
| TC36 | Priority inheritance cleanup | Advanced Kernel | ✅ Active |
| TC37 | Realtime sched cleanup | Advanced Kernel | ✅ Active |
| TC38 | Priority change during cleanup | Advanced Kernel | ✅ Active |
| TC40 | Multi-heap cleanup | Advanced Kernel | ✅ Active |
| TC41 | Signal handler interaction | Advanced Kernel | ✅ Active |
| TC42 | Timer triggered cancellation | Advanced Kernel | ✅ Active |
| TC43 | Sigprocmask cleanup | Advanced Kernel | ✅ Active |
| TC44 | Barrier cancellation | Advanced Kernel | ✅ Active |
| TC45 | RWLock cleanup | Advanced Kernel | ✅ Active |
| TC46 | Once control cancellation | Advanced Kernel | ✅ Active |
| TC47 | TSD destructor ordering | Advanced Kernel | ✅ Active |
| TC48 | TSD cleanup interaction | Advanced Kernel | ✅ Active |
| TC49 | Reentrant cleanup | Advanced Kernel | ⚠️ Disabled |
| TC50 | Cancellation during cleanup | Advanced Kernel | ✅ Active |
| TC51 | Concurrent push | New API | ✅ Active |
| TC52 | Pop error cases | New API | ✅ Active |
| TC53 | Self cancel | New API | ✅ Active |
| TC54 | Double cancel | New API | ✅ Active |
| TC55 | State inheritance | New API | ✅ Active |
| TC56 | Testcancel basic | New API | ✅ Active |
| TC57 | Testcancel CPU-bound | New API | ✅ Active |
| TC58 | Macro pairing | New API | ✅ Active |
| TC59 | Rapid state toggle | New API | ✅ Active |
| TC60 | Combined API test | New API | ✅ Active |

---

## Detailed Test Descriptions

### Basic Functionality Tests (TC01-TC05)

#### TC01: Single Cleanup Handler with pthread_exit
- **File**: `test_basic.c`
- **Description**: Creates a thread that registers a single cleanup handler and exits via `pthread_exit()`. Verifies the handler is called exactly once.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop()`, `pthread_exit()`

#### TC02: Multiple Cleanup Handlers with pthread_exit
- **File**: `test_basic.c`
- **Description**: Registers multiple cleanup handlers (3-5) and verifies they execute in LIFO (last-in, first-out) order when the thread exits.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop()`, `pthread_exit()`

#### TC03: Cleanup Handler with pop(execute=1)
- **File**: `test_basic.c`
- **Description**: Tests `pthread_cleanup_pop(1)` which executes the handler immediately upon pop, even without thread exit.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop(execute=1)`

#### TC04: Cleanup Handler with pop(execute=0)
- **File**: `test_basic.c`
- **Description**: Tests `pthread_cleanup_pop(0)` which removes the handler without executing it.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop(execute=0)`

#### TC05: Nested Push/Pop Pairs
- **File**: `test_basic.c`
- **Description**: Tests nested push/pop pairs (push A, push B, pop B, pop A) to verify proper stack behavior.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop()`

---

### Cancellation Tests (TC06-TC11)

#### TC06: Deferred Cancellation
- **File**: `test_cancel.c`
- **Description**: Tests deferred (default) cancellation type. Thread is canceled at a cancellation point after `pthread_cancel()` is called.
- **APIs Tested**: `pthread_cancel()`, `pthread_setcancelstate()`, `pthread_setcanceltype()`

#### TC07: Asynchronous Cancellation
- **File**: `test_cancel.c`
- **Description**: Tests asynchronous cancellation type (`PTHREAD_CANCEL_ASYNCHRONOUS`). Thread can be canceled at any point, not just at cancellation points.
- **APIs Tested**: `pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS)`, `pthread_cancel()`

#### TC08: Cancellation with Cleanup Handlers
- **File**: `test_cancel.c`
- **Description**: Thread registers cleanup handlers and is then canceled. Verifies all cleanup handlers execute in LIFO order.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cancel()`

#### TC09: Cancel Disabled Thread
- **File**: `test_cancel.c`
- **Description**: Thread disables cancellation (`PTHREAD_CANCEL_DISABLE`). `pthread_cancel()` is called but thread continues running until cancellation is re-enabled.
- **APIs Tested**: `pthread_setcancelstate(PTHREAD_CANCEL_DISABLE)`, `pthread_cancel()`

#### TC10: Cancel State Transitions
- **File**: `test_cancel.c`
- **Description**: Tests transitions between ENABLE→DISABLE→ENABLE states and verifies cancellation only occurs when enabled.
- **APIs Tested**: `pthread_setcancelstate()`

#### TC11: Detached Thread Cancellation
- **File**: `test_cancel.c`
- **Description**: Tests cancellation of a detached thread. Verifies cleanup handlers run properly even without a join.
- **APIs Tested**: `pthread_detach()`, `pthread_cancel()`

---

### Cancellation Point Tests (TC12-TC16)

#### TC12: Cancel at cond_wait
- **File**: `test_cancel_points.c`
- **Description**: Thread blocks on `pthread_cond_wait()` (a cancellation point) and is canceled while waiting.
- **APIs Tested**: `pthread_cond_wait()`, `pthread_cancel()`, `enter_cancellation_point()`, `leave_cancellation_point()`

#### TC13: Cancel at pthread_join
- **File**: `test_cancel_points.c`
- **Description**: Thread blocks on `pthread_join()` (a cancellation point) and is canceled while waiting.
- **APIs Tested**: `pthread_join()`, `pthread_cancel()`

#### TC13B: Cancel after thread exited (ESRCH test)
- **File**: `test_cancel_points.c`
- **Description**: Calls `pthread_cancel()` on a thread that has already exited. Verifies the return value is `ESRCH` (No such process).
- **APIs Tested**: `pthread_cancel()` (error handling)

#### TC14: Cancel at sem_wait
- **File**: `test_cancel_points.c`
- **Description**: Thread blocks on `sem_wait()` (a cancellation point) and is canceled while waiting.
- **APIs Tested**: `sem_wait()`, `pthread_cancel()`

#### TC15: Cancel at sleep
- **File**: `test_cancel_points.c`
- **Description**: Thread is in `sleep()` (a cancellation point) and is canceled while sleeping.
- **APIs Tested**: `sleep()`, `pthread_cancel()`

#### TC16: Multiple Cancellation Points
- **File**: `test_cancel_points.c`
- **Description**: Thread passes through multiple cancellation points in sequence. Verifies cancellation can occur at any of them.
- **APIs Tested**: Multiple cancellation point APIs

---

### Stress Tests (TC17-TC21)

#### TC17: High-Frequency Push/Pop
- **File**: `test_stress.c`
- **Description**: Performs 1000+ push/pop cycles in a tight loop to stress-test the cleanup stack implementation.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop()`

#### TC18: Multi-Thread Cleanup
- **File**: `test_stress.c`
- **Description**: Creates 10+ threads simultaneously, each with cleanup handlers. All threads are canceled and cleanup handlers verified.
- **APIs Tested**: `pthread_create()`, `pthread_cancel()`, `pthread_cleanup_push/pop()`

#### TC19: Deep Nesting
- **File**: `test_stress.c`
- **Description**: Pushes 50+ nested cleanup handlers and verifies they all execute in correct LIFO order.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_cleanup_pop()`

#### TC20: Rapid Create/Cancel Cycles
- **File**: `test_stress.c`
- **Description**: Repeatedly creates and cancels threads in rapid succession to test resource cleanup under load.
- **APIs Tested**: `pthread_create()`, `pthread_cancel()`, `pthread_join()`

#### TC21: Long-Running Cleanup Handlers
- **File**: `test_stress.c`
- **Description**: Cleanup handlers that perform lengthy operations (e.g., sleep, I/O). Verifies they complete without issues.
- **APIs Tested**: `pthread_cleanup_push/pop()`

---

### Resource Cleanup Tests (TC22-TC27)

#### TC22: Memory Cleanup
- **File**: `test_resources.c`
- **Description**: Thread allocates memory and registers a cleanup handler to free it. Thread is canceled and memory leak is checked.
- **APIs Tested**: `pthread_cleanup_push/pop()`, `malloc()`, `free()`

#### TC23: Mutex Cleanup
- **File**: `test_resources.c`
- **Description**: Thread holds a mutex and registers a cleanup handler to unlock it. Verifies mutex is released after cancellation.
- **APIs Tested**: `pthread_mutex_lock()`, `pthread_cleanup_push/pop()`

#### TC24: Semaphore Cleanup
- **File**: `test_resources.c`
- **Description**: Thread holds a semaphore and registers a cleanup handler to post it. Verifies semaphore is released.
- **APIs Tested**: `sem_wait()`, `pthread_cleanup_push/pop()`

#### TC25: File Descriptor Cleanup
- **File**: `test_resources.c`
- **Description**: Thread opens a file and registers a cleanup handler to close it. Verifies fd is closed after cancellation.
- **APIs Tested**: `open()`, `close()`, `pthread_cleanup_push/pop()`

#### TC26: Multiple Resources
- **File**: `test_resources.c`
- **Description**: Thread holds multiple resources (mutex, semaphore, memory, fd) and cleanup handlers release them all.
- **APIs Tested**: Multiple resource APIs + `pthread_cleanup_push/pop()`

#### TC27: Cleanup Ordering Verification
- **File**: `test_resources.c`
- **Description**: Verifies cleanup handlers execute in strict LIFO order by recording execution order and checking.
- **APIs Tested**: `pthread_cleanup_push/pop()`

---

### Edge Case Tests (TC28-TC33)

#### TC28: NULL Argument to Cleanup Handler
- **File**: `test_edge_cases.c`
- **Description**: Passes NULL as the argument to cleanup handler. Verifies handler handles NULL gracefully.
- **APIs Tested**: `pthread_cleanup_push(NULL)`

#### TC29: Cleanup Handler Calls pthread_exit
- **File**: `test_edge_cases.c`
- **Description**: Cleanup handler calls `pthread_exit()` from within. Verifies this doesn't cause infinite recursion.
- **APIs Tested**: `pthread_cleanup_push()`, `pthread_exit()`

#### TC30: Cleanup During Cancel
- **File**: `test_edge_cases.c`
- **Description**: Tests cleanup handler execution during thread cancellation (not normal exit).
- **APIs Tested**: `pthread_cancel()`, `pthread_cleanup_push/pop()`

#### TC31: Pop Without Matching Push
- **File**: `test_edge_cases.c`
- **Description**: Attempts `pthread_cleanup_pop()` without a matching push. Tests error handling.
- **APIs Tested**: `pthread_cleanup_pop()` (error case)

#### TC32: Asynchronous Type Cleanup
- **File**: `test_edge_cases.c`
- **Description**: Tests cleanup handler execution with `PTHREAD_CANCEL_ASYNCHRONOUS` type.
- **APIs Tested**: `pthread_setcanceltype()`, `pthread_cleanup_push/pop()`

#### TC33: Mixed Cancellation Types
- **File**: `test_edge_cases.c`
- **Description**: Thread switches between deferred and asynchronous cancellation types and verifies cleanup still works.
- **APIs Tested**: `pthread_setcanceltype()`, `pthread_cleanup_push/pop()`

---

### Advanced Syscall & Kernel Tests (TC34-TC50)

*These tests are in `test_advanced.c` and `test_advanced_apis.c`.*

#### TC34: Syscall from Loadable Module
- **File**: `test_advanced.c`
- **Description**: Tests that `pthread_cleanup_push/pop` syscalls work correctly from loadable modules (not just the flat build).
- **APIs Tested**: `pthread_cleanup_push/pop()` via syscall path
- **Config Required**: Loadable module support

#### TC35: Syscall Error Handling
- **File**: `test_advanced.c`
- **Description**: Tests error handling for invalid syscall parameters (bad stack index, NULL routine).
- **APIs Tested**: `pthread_cleanup_push/pop()` error paths

#### TC36: Priority Inheritance Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread holds a priority-inheritance mutex (`PTHREAD_PRIO_INHERIT`) and is canceled. Verifies cleanup handler releases the mutex.
- **APIs Tested**: `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`, `pthread_cleanup_push/pop()`
- **Config Required**: `CONFIG_PRIORITY_INHERITANCE`

#### TC37: Realtime Scheduling Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread runs with `SCHED_FIFO` scheduling policy and is canceled. Verifies cleanup handlers execute correctly under realtime scheduling.
- **APIs Tested**: `pthread_setschedparam(SCHED_FIFO)`, `pthread_cleanup_push/pop()`

#### TC38: Priority Change During Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread's priority is changed via `pthread_setschedparam()` while it's running, then the thread is canceled. Verifies cleanup works after priority change.
- **APIs Tested**: `pthread_setschedparam()`, `pthread_cleanup_push/pop()`

#### TC40: Multi-Heap Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread allocates memory from heap regions and is canceled. Verifies cleanup handler can free the memory.
- **APIs Tested**: `malloc()`, `free()`, `pthread_cleanup_push/pop()`

#### TC41: Signal Handler Interaction
- **File**: `test_advanced_apis.c`
- **Description**: Thread receives a signal (SIGUSR1) during a cancellation point, then is canceled. Verifies cleanup handler executes after signal handler runs.
- **APIs Tested**: `sigaction()`, `pthread_sigmask()`, `pthread_kill()`, `pthread_cancel()`, `pthread_cleanup_push/pop()`
- **Config Required**: `CONFIG_SIGNALS` (not `CONFIG_DISABLE_SIGNALS`)
- **Implementation Notes**: Signal is blocked in main thread using `pthread_sigmask(SIG_BLOCK)` so it's delivered to the worker thread.

#### TC42: Timer Triggered Cancellation
- **File**: `test_advanced_apis.c`
- **Description**: POSIX timer sends SIGUSR2 signal, whose handler calls `pthread_cancel()` on the target thread. Verifies cleanup handlers run.
- **APIs Tested**: `timer_create()`, `timer_settime()`, `sigaction()`, `pthread_cancel()`, `pthread_cleanup_push/pop()`
- **Config Required**: `CONFIG_POSIX_TIMERS` (not `CONFIG_DISABLE_POSIX_TIMERS`), `CONFIG_SIGNALS`
- **Implementation Notes**: Uses `SIGEV_SIGNAL` (not `SIGEV_THREAD` which is not supported in TizenRT).

#### TC43: Sigprocmask Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread blocks SIGUSR2 using `pthread_sigmask()`, then is canceled. Verifies signal mask is preserved during cleanup execution.
- **APIs Tested**: `pthread_sigmask()`, `pthread_cleanup_push/pop()`
- **Config Required**: `CONFIG_SIGNALS`

#### TC44: Barrier Cancellation
- **File**: `test_advanced.c`
- **Description**: Thread blocks on `pthread_barrier_wait()` and is canceled while waiting. Verifies cleanup handlers execute.
- **APIs Tested**: `pthread_barrier_wait()`, `pthread_cancel()`, `pthread_cleanup_push/pop()`

#### TC45: RWLock Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread holds a read lock (`pthread_rwlock_rdlock()`) and is canceled. Verifies cleanup handler runs and rwlock is still usable.
- **APIs Tested**: `pthread_rwlock_rdlock()`, `pthread_cleanup_push/pop()`

#### TC46: Once Control Cancellation
- **File**: `test_advanced_apis.c`
- **Description**: Thread is canceled during/after `pthread_once()`. Verifies `pthread_once` control is left in a consistent state and can still be used.
- **APIs Tested**: `pthread_once()`, `pthread_cancel()`, `pthread_cleanup_push/pop()`

#### TC47: TSD Destructor Ordering
- **File**: `test_advanced_apis.c`
- **Description**: Thread sets thread-specific data (TSD) and registers a cleanup handler. When canceled, verifies that cleanup handlers run before TSD destructors.
- **APIs Tested**: `pthread_key_create()`, `pthread_setspecific()`, `pthread_cleanup_push/pop()`
- **Config Required**: `CONFIG_NPTHREAD_KEYS > 0`

#### TC48: TSD Cleanup Interaction
- **File**: `test_advanced_apis.c`
- **Description**: Cleanup handler accesses thread-specific data via `pthread_getspecific()`. Verifies TSD is still valid during cleanup execution.
- **APIs Tested**: `pthread_key_create()`, `pthread_setspecific()`, `pthread_getspecific()`, `pthread_cleanup_push/pop()`

#### TC49: Reentrant Cleanup (DISABLED)
- **File**: `test_advanced_apis.c`
- **Description**: *Intended to test* cleanup handler that calls `pthread_cleanup_push/pop` from within (reentrant behavior).
- **Status**: **DISABLED** - This test exercises **undefined behavior** per POSIX standards.
- **Issue**: Calling `pthread_cleanup_push/pop` from within a cleanup handler modifies the cleanup stack while it's being unwound, causing a kernel assertion in TizenRT (`armv7-a/arm_syscall.c line 531`).
- **Code**: The test code is preserved in `#if 0` block for reference as a separate patch.
- **Function**: `test_reentrant_cleanup()` is a stub that reports the test as disabled.

#### TC50: Cancellation During Cleanup
- **File**: `test_advanced_apis.c`
- **Description**: Thread A's cleanup handler calls `pthread_cancel()` on Thread B (which also has cleanup handlers). Verifies both cleanup chains execute correctly.
- **APIs Tested**: `pthread_cancel()`, `pthread_cleanup_push/pop()`

---

### New API Verification Tests (TC51-TC60)

*These tests are in `new_apis_test.c`.*

#### TC51: Concurrent Push
- **Description**: Multiple threads concurrently call `pthread_cleanup_push/pop`. Verifies thread-safety of the cleanup stack.
- **APIs Tested**: `pthread_cleanup_push/pop()` (concurrent access)

#### TC52: Pop Error Cases
- **Description**: Tests error cases for `pthread_cleanup_pop` - extra pops without matching pushes.
- **APIs Tested**: `pthread_cleanup_pop()` (error cases)

#### TC53: Self Cancel
- **Description**: Thread calls `pthread_cancel(pthread_self())` and verifies cleanup handlers run.
- **APIs Tested**: `pthread_cancel(pthread_self())`, `pthread_cleanup_push/pop()`

#### TC54: Double Cancel
- **Description**: Multiple `pthread_cancel()` calls on the same thread. Verifies idempotent behavior.
- **APIs Tested**: `pthread_cancel()` (multiple calls)

#### TC55: State Inheritance
- **Description**: Verifies cancellation state inheritance when creating child threads.
- **APIs Tested**: `pthread_setcancelstate()`, `pthread_create()`

#### TC56: Testcancel Basic
- **Description**: Tests `pthread_testcancel()` at a non-blocking point. Verifies it creates a cancellation point.
- **APIs Tested**: `pthread_testcancel()`

#### TC57: Testcancel CPU-Bound
- **Description**: Thread runs a CPU-bound loop with periodic `pthread_testcancel()` calls. Verifies cancellation works in CPU-bound threads.
- **APIs Tested**: `pthread_testcancel()`

#### TC58: Macro Pairing
- **Description**: Tests that `pthread_cleanup_push` and `pthread_cleanup_pop` macros work correctly in various block scopes (if/else, for loops, switch cases).
- **APIs Tested**: `pthread_cleanup_push/pop()` (macro pairing)

#### TC59: Rapid State Toggle
- **Description**: Rapidly toggles cancellation state between ENABLE and DISABLE. Verifies no race conditions.
- **APIs Tested**: `pthread_setcancelstate()` (rapid toggling)

#### TC60: Combined API Test
- **Description**: Integration test that combines all 5 APIs (`push`, `pop`, `cancel`, `testcancel`, `setcancelstate`) in a single scenario.
- **APIs Tested**: All 5 APIs together

---

## Configuration Requirements

| Config | Required For | Default |
|--------|-------------|---------|
| `CONFIG_DISABLE_PTHREAD` | All tests (must NOT be set) | Not set |
| `CONFIG_DISABLE_SIGNALS` | TC41, TC42, TC43 (must NOT be set) | Not set |
| `CONFIG_DISABLE_POSIX_TIMERS` | TC42 (must NOT be set) | Not set |
| `CONFIG_PRIORITY_INHERITANCE` | TC36 | Optional |
| `CONFIG_NPTHREAD_KEYS > 0` | TC47, TC48 | Default > 0 |

---

## File Structure

```
apps/examples/pthread_cleanup_test/
├── Makefile                    - Build configuration
├── Kconfig                     - Kconfig entry
├── Kconfig_ENTRY               - Kconfig entry definition
├── Make.defs                   - Build definitions
├── pthread_cleanup.h           - Header with enums, macros, prototypes
├── pthread_cleanup_main.c      - Main entry point, test runner, menu
├── test_basic.c                - TC01-TC05: Basic functionality tests
├── test_cancel.c               - TC06-TC11: Cancellation tests
├── test_cancel_points.c        - TC12-TC16: Cancellation point tests
├── test_stress.c               - TC17-TC21: Stress tests
├── test_resources.c            - TC22-TC27: Resource cleanup tests
├── test_edge_cases.c           - TC28-TC33: Edge case tests
├── test_advanced.c             - TC34, TC35, TC44: Advanced syscall tests
├── test_advanced_apis.c        - TC36-TC50: Advanced kernel API tests
├── new_apis_test.c             - TC51-TC60: New API verification tests
└── README.md                   - Original README
```

---

## Known Issues and Limitations

1. **TC49 (Reentrant Cleanup)**: Disabled because calling `pthread_cleanup_push/pop` from within a cleanup handler is undefined behavior per POSIX. It causes a kernel assertion in TizenRT. The code is preserved in `#if 0` for reference.

2. **TC39 (Stack Overflow Protection)**: Removed from the test suite because it intentionally exceeds `CONFIG_PTHREAD_CLEANUP_STACKSIZE`, which triggers a kernel assertion. This is expected behavior (the kernel correctly detects the overflow).

3. **IntelliSense Errors**: The C/C++ IntelliSense in VS Code shows false positive errors for this project due to the `FAR` macro and variadic `printf` macros. These errors do not affect compilation with GCC.

4. **Signal Tests (TC41, TC42, TC43)**: These tests require signals to be enabled (`CONFIG_DISABLE_SIGNALS` must NOT be set). If signals are disabled, the tests gracefully skip with a message.

5. **Timer Test (TC42)**: Uses `SIGEV_SIGNAL` instead of `SIGEV_THREAD` because TizenRT's `struct sigevent` does not support `SIGEV_THREAD`.

---

## Test Execution

### Running Individual Tests
```
pthread_cleanup> 41       # Run TC41
```

### Running by Category
```
pthread_cleanup> d        # Run category 7 (Advanced tests TC34-TC50)
pthread_cleanup> n        # Run category 8 (New API tests TC51-TC60)
```

### Running All Tests
```
pthread_cleanup> a        # Run all tests
```

### Stability Test
```
pthread_cleanup> i        # Infinite stability test (Ctrl+C to stop)
```
