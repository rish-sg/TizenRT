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

#include <tinyara/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <tinyara/mtd/mtd.h>

#define FF_DEFAULT_DEV      "/dev/mtdblock7"
#define FF_DEFAULT_SECS     60
#define FF_DEFAULT_EBLKS    2
#define FF_STACKSIZE        8192

struct ff_bar_s
{
	volatile int count;
};

struct ff_task_s
{
	FAR const char *dev;
	FAR uint8_t *wbuf;
	FAR uint8_t *rbuf;
	FAR struct ff_bar_s *bar;
	off_t first;                  /* First owned erase block index  */
	int eblks;                    /* Owned erase blocks             */
	unsigned long esize;
	int secs;
	int cpu;
	unsigned long ops;
	unsigned long fails;
};

/****************************************************************************
 * Name: ff_protected_reason
 *
 * Description:
 *   Same partition guard as mtdtest: only /dev/mtdblock7 is a safe
 *   target (4 and 5 hold the XIP kernel slots, 8 the boot parameters).
 *
 ****************************************************************************/

static FAR const char *ff_protected_reason(int minor)
{
	switch (minor) {
	case 4:
		return "kernel slot A (executed in place, writing would brick the board)";
	case 5:
		return "kernel slot B (OTA backup image)";
	case 8:
		return "boot parameters (owned by the binary manager)";
	default:
		return NULL;
	}
}

static int ff_expired(FAR struct timespec *t0, int secs)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t0->tv_sec) >= secs;
}

/****************************************************************************
 * Name: ff_writer
 *
 * Description:
 *   One writer pinned to one CPU.  Starts only when both writers have
 *   arrived, then writes and verifies its own erase blocks until the
 *   deadline.  Two cores hammering the same flash chip drive the exact
 *   locking paths that hung before the event-stream and bounded-pause
 *   fixes: the cross-core flash driver spin_lock (WFE park plus SEV
 *   wake) and, through the write window, the CPU pause handshake that
 *   parks the peer core away from XIP.
 *
 ****************************************************************************/

static FAR void *ff_writer(FAR void *arg)
{
	FAR struct ff_task_s *t = (FAR struct ff_task_s *)arg;
	struct timespec t0;
	struct sched_param sp;
	uint8_t pattern;
	off_t off;
	int fd;
	int blk;
	int n;

	fd = open(t->dev, O_RDWR);
	if (fd < 0) {
		printf("flashfight: cpu%d cannot open %s, errno %d\n",
		       t->cpu, t->dev, errno);
		t->fails++;
		return NULL;
	}

	sched_getparam(0, &sp);
	printf("flashfight: cpu%d ready (blocks %ld..%ld, prio %d)\n", t->cpu,
	       (long)t->first, (long)(t->first + t->eblks - 1),
	       sp.sched_priority);
	__sync_fetch_and_add(&t->bar->count, 1);
	while (t->bar->count < 2) {
		usleep(200);
	}

	usleep(10000);              /* Let both cores leave the barrier
	                             * at the same instant */
	clock_gettime(CLOCK_MONOTONIC, &t0);

	do {
		for (blk = 0; blk < t->eblks; blk++) {
			n = (int)(t->first + blk);
			off = (off_t)n * (off_t)t->esize;
			pattern = (uint8_t)(0x5A ^ (uint8_t)t->ops ^
			                    (uint8_t)blk ^ (uint8_t)t->cpu);

			memset(t->wbuf, pattern, t->esize);
			if (lseek(fd, off, SEEK_SET) < 0 ||
			    write(fd, t->wbuf, t->esize) != (ssize_t)t->esize) {
				printf("flashfight: cpu%d WRITE FAIL off %ld"
				       " errno %d\n", t->cpu, (long)off,
				       errno);
				t->fails++;
				break;
			}

			if (lseek(fd, off, SEEK_SET) < 0 ||
			    read(fd, t->rbuf, t->esize) != (ssize_t)t->esize) {
				printf("flashfight: cpu%d READ FAIL off %ld"
				       " errno %d\n", t->cpu, (long)off,
				       errno);
				t->fails++;
				break;
			}

			if (memcmp(t->wbuf, t->rbuf, t->esize) != 0) {
				printf("flashfight: cpu%d VERIFY FAIL off %ld\n",
				       t->cpu, (long)off);
				t->fails++;
				break;
			}

			t->ops++;
		}
	} while (!ff_expired(&t0, t->secs) && t->fails == 0);

	close(fd);
	return NULL;
}

/****************************************************************************
 * Name: utils_flashfight
 *
 * Description:
 *   flashfight [-d dev] [-t seconds] [-b erase-blocks-per-cpu]
 *
 *   Dual-core flash contention stress for the rtl8730e SMP XIP fix
 *   (TD-TEST-02).  One writer thread is pinned to each CA32 core; both
 *   write and verify disjoint erase block ranges of the same MTD
 *   partition simultaneously.  Intended to run concurrently with
 *   mtdtest/xipmon/ostest in the soak matrix.
 *
 ****************************************************************************/

int utils_flashfight(int argc, char **args)
{
	struct ff_bar_s bar;
	struct ff_task_s task[2];
	struct mtd_geometry_s geo;
	struct sched_param sp;
	pthread_attr_t attr;
	pthread_t tid[2];
	FAR const char *dev = FF_DEFAULT_DEV;
	FAR const char *reason;
	cpu_set_t set;
	int secs = FF_DEFAULT_SECS;
	int eblks = FF_DEFAULT_EBLKS;
	int prio = 0;
	int fd;
	int minor = -1;
	int failed = 0;
	int i;
	int cpu;

	for (i = 1; i < argc; i++) {
		if (strcmp(args[i], "-d") == 0 && i + 1 < argc) {
			dev = args[++i];
		} else if (strcmp(args[i], "-t") == 0 && i + 1 < argc) {
			secs = atoi(args[++i]);
		} else if (strcmp(args[i], "-b") == 0 && i + 1 < argc) {
			eblks = atoi(args[++i]);
		} else if (strcmp(args[i], "-p") == 0 && i + 1 < argc) {
			prio = atoi(args[++i]);
		} else {
			printf("Usage: flashfight [-d dev] [-t seconds]"
			       " [-b erase-blocks-per-cpu] [-p priority]\n");
			return ERROR;
		}
	}

	if (sscanf(dev, "/dev/mtdblock%d", &minor) != 1) {
		minor = -1;
	}

	reason = ff_protected_reason(minor);
	if (reason != NULL) {
		printf("flashfight: refusing %s: %s\n", dev, reason);
		return ERROR;
	}

	if (secs <= 0) {
		secs = FF_DEFAULT_SECS;
	}

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		printf("flashfight: cannot open %s, errno %d\n", dev, errno);
		return ERROR;
	}

	if (ioctl(fd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo)) < 0 ||
	    geo.erasesize == 0) {
		printf("flashfight: MTDIOC_GEOMETRY failed on %s\n", dev);
		close(fd);
		return ERROR;
	}

	close(fd);

	/* Erase block 0 holds the FTL bookkeeping: the two writers
	 * together own blocks 1 .. 2*eblks.
	 */

	if (eblks <= 0 || 2 * eblks + 1 > (int)geo.neraseblocks) {
		eblks = ((int)geo.neraseblocks - 1) / 2;
	}

	if (eblks <= 0) {
		printf("flashfight: %s has no usable erase blocks\n", dev);
		return ERROR;
	}

	memset(task, 0, sizeof(task));
	bar.count = 0;

	for (cpu = 0; cpu < 2; cpu++) {
		task[cpu].dev   = dev;
		task[cpu].bar   = &bar;
		task[cpu].first = 1 + (off_t)cpu * eblks;
		task[cpu].eblks = eblks;
		task[cpu].esize = geo.erasesize;
		task[cpu].secs  = secs;
		task[cpu].cpu   = cpu;
		task[cpu].wbuf  = (FAR uint8_t *)malloc(geo.erasesize);
		task[cpu].rbuf  = (FAR uint8_t *)malloc(geo.erasesize);
		if (task[cpu].wbuf == NULL || task[cpu].rbuf == NULL) {
			printf("flashfight: out of memory\n");
			failed = 1;
		}
	}

	/* Optional re-prioritization of the whole group (see the matching
	 * note in utils_xipmon.c).  At the default TASH command priority the
	 * writers are co-equal CPU competitors of any test that runs beside
	 * them, which is what the contention soak wants, but is wrong when
	 * the point of the run is to observe a scheduler test: the ostest
	 * priority inheritance test parks a busy-waiting thread at 99 that
	 * only samples its exit condition once per INT_MAX iteration spin, so
	 * writers at 100 can hold its completion up for minutes.  With -p
	 * below the priorities under test the flash contention continues
	 * while the CPU is handed to the threads under observation.
	 */

	if (prio > 0) {
		if (sched_getparam(0, &sp) == OK) {
			sp.sched_priority = prio;
			if (sched_setparam(0, &sp) != OK) {
				printf("flashfight: sched_setparam(%d) failed,"
				       " errno %d\n", prio, errno);
				prio = 0;
			}
		}
	}

	if (!failed) {
		printf("flashfight: %s fighting on %d blocks per CPU for"
		       " %d s\n", dev, eblks, secs);
		if (prio > 0) {
			printf("flashfight: writers at priority %d\n", prio);
		}

		for (cpu = 0; cpu < 2; cpu++) {
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, FF_STACKSIZE);
			if (prio > 0) {
				sp.sched_priority = prio;
				if (pthread_attr_setschedparam(&attr,
				                               &sp) != 0) {
					printf("flashfight:"
					       " attr_setschedparam cpu%d"
					       " failed\n", cpu);
					failed = 1;
					bar.count = 2;
					break;
				}
			}

			CPU_ZERO(&set);
			CPU_SET(cpu, &set);
			if (pthread_attr_setaffinity_np(&attr, sizeof(set),
			                                &set) != 0) {
				printf("flashfight: cannot pin cpu%d,"
				       " errno %d\n", cpu, errno);
				failed = 1;
				bar.count = 2;
				break;
			}

			if (pthread_create(&tid[cpu], &attr, ff_writer,
			                       &task[cpu]) != 0) {
				printf("flashfight: pthread_create cpu%d"
				       " failed\n", cpu);
				failed = 1;
				bar.count = 2;
				break;
			}
		}

		if (!failed) {
			for (cpu = 0; cpu < 2; cpu++) {
				pthread_join(tid[cpu], NULL);
			}
		}
	}

	for (cpu = 0; cpu < 2; cpu++) {
		printf("flashfight: cpu%d ops=%lu fails=%lu %s\n", cpu,
		       task[cpu].ops, task[cpu].fails,
		       (task[cpu].fails == 0 && task[cpu].ops > 0) ?
		       "PASS" : "FAIL");
		if (task[cpu].fails != 0 || task[cpu].ops == 0) {
			failed = 1;
		}

		free(task[cpu].wbuf);
		free(task[cpu].rbuf);
	}

	printf("flashfight: %s\n", failed ? "FAILED" : "PASS");
	return failed ? ERROR : OK;
}