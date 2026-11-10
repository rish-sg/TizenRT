/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
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
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define XIPMON_DEFAULT_LEN      16384
#define XIPMON_DEFAULT_MS       5
#define XIPMON_HEARTBEAT        1000

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: xipmon_crc32
 *
 * Description:
 *   Table-less CRC32 (reflected polynomial) over a read-only window.  Kept
 *   intentionally simple: the monitor must not depend on any other libc
 *   service that could itself live in the window being checked.
 *
 ****************************************************************************/

static uint32_t xipmon_crc32(FAR const uint8_t *p, size_t len)
{
	uint32_t crc = 0xffffffff;
	size_t i;
	int b;

	for (i = 0; i < len; i++) {
		crc ^= p[i];
		for (b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xedb88320 & -(int)(crc & 1));
		}
	}

	return ~crc;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: utils_xipmon
 *
 * Description:
 *   xipmon [-a hexaddr] [-s bytes] [-i interval-ms] [-c checks]
 *          [-p priority]
 *
 *   Silent-corruption watchdog for execute-in-place code.  The
 *   rtl8730e flash write path pauses the peer CA32 cores around every
 *   erase/program; a defect in that gate would not necessarily hang the
 *   system, it could instead let a core FETCH CORRUPTED INSTRUCTIONS
 *   and continue.  CPUPAUSE/FLASHGATE timeouts cannot see that class,
 *   so xipmon computes a CRC32 over a window of code that is executed
 *   in place from flash (by default the window containing this command
 *   itself) and compares it against the baseline taken at start.
 *
 *   Any mismatch means the instruction/data stream no longer matches
 *   what is in flash: report it loudly, keep counting and fail the run.
 *
 *   Limitation: the L1 instruction cache can mask changes made in the
 *   flash chip for cached lines; the small window, the short interval
 *   and the cache pressure of a full soak make silent misses unlikely
 *   but not impossible.  This is a tripwire, not a proof.
 *
 ****************************************************************************/

int utils_xipmon(int argc, char **args)
{
	FAR const uint8_t *base;
	uintptr_t start;
	size_t len = XIPMON_DEFAULT_LEN;
	unsigned int interval = XIPMON_DEFAULT_MS;
	long checks = 0;
	long n;
	uint32_t want;
	uint32_t now;
	unsigned long mismatches = 0;
	struct sched_param sp;
	int prio = 0;
	int i;

	/* Default window: the flash XIP code of this very function,
	 * rounded down to a 16 KB boundary so the window is stable.
	 */

	start = ((uintptr_t)&utils_xipmon) & ~(uintptr_t)0x3fff;

	for (i = 1; i < argc; i++) {
		if (strcmp(args[i], "-a") == 0 && i + 1 < argc) {
			start = (uintptr_t)strtoul(args[++i], NULL, 16);
		} else if (strcmp(args[i], "-s") == 0 && i + 1 < argc) {
			len = (size_t)strtoul(args[++i], NULL, 0);
		} else if (strcmp(args[i], "-i") == 0 && i + 1 < argc) {
			interval = (unsigned int)atoi(args[++i]);
		} else if (strcmp(args[i], "-c") == 0 && i + 1 < argc) {
			checks = atol(args[++i]);
		} else if (strcmp(args[i], "-p") == 0 && i + 1 < argc) {
			prio = atoi(args[++i]);
		} else {
			printf("Usage: xipmon [-a hexaddr] [-s bytes]"
			       " [-i interval-ms] [-c checks] [-p priority]\n");
			return ERROR;
		}
	}

	if (interval == 0) {
		interval = 1;
	}

	base = (FAR const uint8_t *)start;

	/* Optional re-prioritization.  As a TASH command this monitor runs at
	 * CONFIG_TASH_CMDTASK_PRIORITY (100 on this config), which makes it a
	 * co-equal CPU hog against anything a test suite puts below 100.  The
	 * ostest priority inheritance test, for example, parks a busy-waiting
	 * thread at 99 that only samples its exit condition once per INT_MAX
	 * iteration spin, so a monitor at 100 can delay its completion by
	 * minutes.  Give -p a value below the priorities under test to run as
	 * a background watchdog instead of a competitor.
	 */

	if (prio > 0) {
		if (sched_getparam(0, &sp) == OK) {
			sp.sched_priority = prio;
			if (sched_setparam(0, &sp) != OK) {
				printf("xipmon: sched_setparam(%d) failed,"
				       " errno %d\n", prio, errno);
				prio = 0;
			}
		}
	}

	want = xipmon_crc32(base, len);

	printf("xipmon: watching %lu bytes at 0x%08lx crc=0x%08lx"
	       " every %u ms%s", (unsigned long)len,
	       (unsigned long)start, (unsigned long)want, interval,
	       checks != 0 ? "" : " (infinite)");
	if (prio > 0) {
		printf(" prio=%d", prio);
	}

	printf("\n");

	for (n = 1; checks == 0 || n <= checks; n++) {
		usleep(interval * 1000);

		now = xipmon_crc32(base, len);
		if (now != want) {
			mismatches++;
			printf("xipmon: MISMATCH check %ld crc 0x%08lx"
			       " != 0x%08lx (0x%08lx + %lu)\n", n,
			       (unsigned long)now, (unsigned long)want,
			       (unsigned long)start,
			       (unsigned long)len);
		}

		if ((n % XIPMON_HEARTBEAT) == 0) {
			printf("xipmon: alive checks=%ld mismatches=%lu\n",
			       n, mismatches);
		}
	}

	printf("xipmon: %s: checks=%ld mismatches=%lu window=0x%08lx\n",
	       mismatches == 0 ? "PASS" : "FAIL", n - 1, mismatches,
	       (unsigned long)start);
	return mismatches == 0 ? OK : ERROR;
}