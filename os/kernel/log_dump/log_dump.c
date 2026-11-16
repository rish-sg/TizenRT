/****************************************************************************
 *
 * Copyright 2022 Samsung Electronics All Rights Reserved.
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
#include <tinyara/mm/mm.h>
#include <tinyara/sched.h>
#include <tinyara/kthread.h>
#include <tinyara/compression.h>

#include <mqueue.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <debug.h>
#include <assert.h>

#include <tinyara/mqueue.h>
#include <tinyara/queue.h>
#include <tinyara/clock.h>
#include <tinyara/log_dump/log_dump.h>
#include <tinyara/log_dump/log_dump_internal.h>
#include <syslog.h>
#include <time.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef MIN
#define MIN(a, b)		   (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)		   (((a) > (b)) ? (a) : (b))
#endif

#define LOG_DUMP_MSGQ	"logdump_s"

#define LOG_DUMP_OK			0	/* Used for successful return check */
#define LOG_DUMP_OK_HEAPCHK		1	/* To check heap available after success return */
#define LOG_DUMP_MEM_FAIL		-1
#define LOG_DUMP_COMPHEAD_OFFSET	-1
#define LOG_DUMP_OPT_FAIL		-2
#define LOG_DUMP_FAIL			-99
#define LOG_DUMP_STRLEN_FREEHEAP		15	/* String size to display Free heap size */
#define LOG_DUMP_COMPRESS_NODESZ	5	/* use 4 char to store compressed node size */

#define LOG_CHUNK_SIZE			sizeof(struct log_dump_chunk_s)

/* A live syslog notice is emitted at most once per this many dropped
 * characters (the gap marker in the captured stream is emitted per burst
 * regardless of this limit).
 */
#define LOG_DUMP_DROP_NOTICE_MIN_DROPS	4096
/* Bounded wait for the compress thread to finish the partially filled
 * block on a read/stop request. The wait never blocks the console path;
 * this only protects the reader task from an unbounded stall.
 */
#define LOG_DUMP_LASTBLOCK_TIMEOUT_SEC	5

static bool is_started_to_save;

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct log_dump_msg_s {
      bool comp;
};

/* log_dump_chunk_s is the compressed data node */
struct log_dump_chunk_s {
	struct log_dump_chunk_s *flink;
	char arr[CONFIG_LOG_DUMP_CHUNK_SIZE];	/* to store the compressed log */
	int comp_head;				/* starting index of the 1st compressed block in the chunk */
};

/****************************************************************************
 * Public Variables
 ****************************************************************************/

/****************************************************************************
 * Private Variables
 ****************************************************************************/
static struct file mq_send_file;	/* producer side: O_NONBLOCK, mq_send never blocks */
static struct file mq_recv_file;	/* compress thread side: blocking mq_receive */
static bool log_dump_mq_ready;
static sq_queue_t log_dump_chunks;	/* queue for holding the logs */
static int compress_curbytes = 0;	/* number of bytes in the current chunk that were filled */
static int compress_rdptr = 0;		/* current pointer for compressed data read */
static size_t log_dump_size = 0;
static struct log_dump_chunk_s *read_node;
static int log_dump_chunk_node_cnt;
static unsigned char uncomp_buf[CONFIG_LOG_DUMP_NUMBUFS][CONFIG_LOG_DUMP_CHUNK_SIZE];	/* initial uncompressed log dump data */
static bool uncomp_buf_full[CONFIG_LOG_DUMP_NUMBUFS];
static int uncomp_idx = 0;
static int uncomp_curbytes[CONFIG_LOG_DUMP_NUMBUFS];	/* per-slot write cursor */
static char comp_size[LOG_DUMP_COMPRESS_NODESZ];
static bool set_comp_head = false;
static size_t writesize = CONFIG_LOG_DUMP_CHUNK_SIZE;
static unsigned char *out_buf;
static int compress_ret;
static int comp_scan_idx;		/* compress thread round-robin slot scan index */
static uint32_t log_dump_drops_pending;	/* chars dropped since the last gap marker */
static uint32_t log_dump_drops_total;	/* lifetime count of dropped chars */
static uint32_t log_dump_notice_mark;	/* drops_total value at the last live notice */
static volatile bool gap_notice_active;	/* recursion guard for the live notice */
static sem_t lastblock_sem;		/* signals completion of the partial-block compression */
#ifdef CONFIG_DEBUG_MM_HEAPINFO
static pid_t g_log_dump_pid;		/* PID of log_dump kernel thread */
#endif

/* Structures used to compress last partially filled block */
static unsigned char *last_comp_block;
static size_t last_comp_block_size;
static int last_comp_block_ptr;
static bool compress_last_block;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int log_dump_init(void)
{
	sq_init(&log_dump_chunks);
	/* no chunks are present so we allocate new chunk */
	struct log_dump_chunk_s *node = (struct log_dump_chunk_s *)kmm_malloc(LOG_CHUNK_SIZE);

	if (node == NULL) {
		ldpdbg("memory allocation failure\n");
		return LOG_DUMP_MEM_FAIL;
	}
	DEBUG_SET_PID(node, g_log_dump_pid);

	/* The buffers allocated below are required for log-dump till the 
	 * device is shut down or restarted. So these buffers MUST NEVER
	 * BE FREED. 
	 */
	out_buf = allocate_compress_buffer(0, CONFIG_LOG_DUMP_CHUNK_SIZE);
	if (out_buf == NULL) {
		ldpdbg("memory allocation failure\n");
		goto exit_with_node;
	}

	last_comp_block = allocate_compress_buffer(4, CONFIG_LOG_DUMP_CHUNK_SIZE);
	if (last_comp_block == NULL) {
		ldpdbg("memory allocation failure\n");
		goto exit_with_outbuf;
	}


	node->comp_head = 0;		/* since first node */

	for (int i = 0; i < CONFIG_LOG_DUMP_NUMBUFS; i++) {
		uncomp_buf_full[i] = false;
		uncomp_curbytes[i] = 0;
	}

	uncomp_idx = 0;
	comp_scan_idx = 0;
	compress_curbytes = 0;
	compress_rdptr = 0;
	log_dump_chunk_node_cnt = 0;
	sq_addfirst((sq_entry_t *)node, &log_dump_chunks);
	/* set the size of chunks to 1 chunk */
	log_dump_size = LOG_CHUNK_SIZE;
	/* Initialize to save the log by default */
	is_started_to_save = true;
	last_comp_block_size = 0;
	nxsem_init(&lastblock_sem, 0, 0);

	return LOG_DUMP_OK;

exit_with_outbuf:
	kmm_free(out_buf);
exit_with_node:
	kmm_free(node);

	return LOG_DUMP_MEM_FAIL;
}

/* This is used to wake up the compression thread to compress current log data */
int log_dump_read_wake(void)
{
	compress_last_block = true;
	/* Drain any stale completion credit from a previously timed-out wait */
	while (nxsem_trywait(&lastblock_sem) == 0);

	if (!log_dump_mq_ready) {
		return ERROR;
	}

	int ret;
	int msg_compress = true;
	/* Kernel-internal, fd-free send: an mqd_t-based mq_send() would resolve
	 * the descriptor in the calling task's own fd list, which never contains
	 * the queue opened by the log_dump kernel thread. */
	ret = file_mq_send(&mq_send_file, (const char *)&msg_compress, sizeof(int), 100);
	if (ret < 0) {
		ldpdbg("failed mq_send ret = %d\n", -ret);
		return ERROR;
	}

	/* Bounded wait for the compress thread to finish the partially filled
 * block. The compress thread may legitimately be waiting for the kernel
 * heap; a bounded wait keeps this call from hanging forever. The wait
 * never runs on the console write path.
 */
 {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += LOG_DUMP_LASTBLOCK_TIMEOUT_SEC;
		if (nxsem_timedwait(&lastblock_sem, &ts) < 0) {
			ldpdbg("timed out waiting for last block compression\n");
			compress_last_block = false;
			return ERROR;
		}
	}

	return 0;
}

/* This function is called during read and compress the last partially filled block */
int log_dump_compress_lastblock(void)
{
	read_node = (struct log_dump_chunk_s *)sq_peek(&log_dump_chunks);	/* reset the read node to head */

	if (read_node == NULL) {
		ldpdbg("empty read node\n");
		return LOG_DUMP_MEM_FAIL;
	}

	if (read_node->comp_head < 0) {
		compress_rdptr = compress_curbytes;
	} else {
		compress_rdptr = read_node->comp_head;
	}

	last_comp_block_size = CONFIG_LOG_DUMP_CHUNK_SIZE;

	/* Snapshot the partially filled active slot so that the console writer
 * is never stalled or raced while the block is compressed.
 */
	irqstate_t flags = enter_critical_section();
	int partial = uncomp_curbytes[uncomp_idx];
	memcpy(out_buf, uncomp_buf[uncomp_idx], partial);
	leave_critical_section(flags);

	compress_ret = compress_block(&last_comp_block[4], &last_comp_block_size, (unsigned char *)out_buf, partial);

	if (compress_ret != LOG_DUMP_OK) {
		last_comp_block_size = 0;
		ldpdbg("Fail to compress compress_ret = %d\n", compress_ret);
		return -compress_ret;
	}

	snprintf(comp_size, LOG_DUMP_COMPRESS_NODESZ, "%04d", last_comp_block_size);
	for (int i = 0; i < LOG_DUMP_COMPRESS_NODESZ - 1; i++) {
		last_comp_block[i] = comp_size[i];
	}

	last_comp_block_size += 4;
	last_comp_block_ptr = 0;
	return 0;
}

static void log_dump_mem_check(size_t max_size)
{
	/* If kernel heap is locked, then we cannot perform any
	 * mem free opeartions. So we just return here. The mem
	 * free will be done when the kernel heap is released
	 */
	if (kmm_is_locked()) {
		return;
	}

	if (log_dump_size > max_size) {
		/* remove extra chunks from start */
		size_t extra_chunk_size = log_dump_size - max_size;
		int extra_chunks_count = extra_chunk_size / LOG_CHUNK_SIZE;
		struct log_dump_chunk_s *next_chunk;

		for (int i = 0; i < extra_chunks_count; i++) {
			next_chunk = (struct log_dump_chunk_s *)sq_remfirst(&log_dump_chunks);
			compress_rdptr = 0;	/* reset the read pointer as the head is removed */
			log_dump_size -= LOG_CHUNK_SIZE;
			log_dump_chunk_node_cnt -= 1;
			kmm_free(next_chunk);
		}
	}
}

int log_dump_set(FAR const char *buffer, size_t buflen)
{
	(void)buflen;

	if (strncmp(buffer, LOGDUMP_SAVE_START, strlen(LOGDUMP_SAVE_START) + 1) == 0) {
		is_started_to_save = true;
	} else if (strncmp(buffer, LOGDUMP_SAVE_STOP, strlen(LOGDUMP_SAVE_STOP) + 1) == 0) {
		is_started_to_save = false;
		/* Now we should compress the partially filled uncompressed buffer */
		if (log_dump_read_wake() != OK) {
			ldpdbg("ERROR: log dump read wake fail\n");
			/* Should we return error from here?? because partially filled uncompressed buffer
			 * might not be compressed but log_dump save has stopped now, so operation is success */
		}
	} else if (strncmp(buffer, LOGDUMP_GET_SIZE, strlen(LOGDUMP_GET_SIZE) + 1) == 0) {
		return log_dump_get_size();
	} else {
		return LOG_DUMP_OPT_FAIL;
	}
	return LOG_DUMP_OK;
}

static int log_dump_tobuffer(char ch, size_t *free_size)
{
	/* need to check if the current chunks size is over max_log_size or greater than x% of free heap */

	size_t max_size;

#ifdef CONFIG_DEBUG_MM_HEAPINFO
	*free_size = kmm_get_heap_free_size();
#else
	struct mallinfo mem;

	mem = kmm_mallinfo();
	*free_size = mem.fordblks;
#endif

	size_t percent_free_size = (*free_size * CONFIG_LOG_DUMP_MAX_FREE_HEAP) / 100;

	if (percent_free_size < CONFIG_LOG_DUMP_MAX_SIZE) {
		max_size = LOG_CHUNK_SIZE;	/* setting the lower limit */
	} else {
		max_size = CONFIG_LOG_DUMP_MAX_SIZE;	/* setting the upper limit */
	}

	if (compress_curbytes == CONFIG_LOG_DUMP_CHUNK_SIZE) {	/* last chunk is full */

		if (log_dump_size + LOG_CHUNK_SIZE < max_size) {
			/* memory available, so fetch a new buffer */

			struct log_dump_chunk_s *node = (struct log_dump_chunk_s *)kmm_malloc(LOG_CHUNK_SIZE);

			if (node == NULL) {
				return LOG_DUMP_MEM_FAIL;
			}
			DEBUG_SET_PID(node, g_log_dump_pid);
			compress_curbytes = 1;
			node->arr[0] = ch;
			sq_addlast((sq_entry_t *) node, &log_dump_chunks);
			if (set_comp_head) {	/* new node, so compressed block starts at 0 */
				node->comp_head = 0;
				set_comp_head = false;
			} else {
				node->comp_head = LOG_DUMP_COMPHEAD_OFFSET;
				set_comp_head = false;
			}
			log_dump_size += LOG_CHUNK_SIZE;
			log_dump_chunk_node_cnt += 1;

		} else {
			/* logs reached memory limit, reuse the head and free extra logs */
			struct log_dump_chunk_s *node = (struct log_dump_chunk_s *)sq_remfirst(&log_dump_chunks);
			ASSERT(node);

			compress_curbytes = 1;
			compress_rdptr = 0;	/* reset the read pointer as head is modified */
			node->arr[0] = ch;
			sq_addlast((sq_entry_t *) node, &log_dump_chunks);
			if (set_comp_head) {	/* new node, so compressed block starts at 0 */
				node->comp_head = 0;
				set_comp_head = false;
			} else {
				node->comp_head = LOG_DUMP_COMPHEAD_OFFSET;
				set_comp_head = false;
			}
			log_dump_mem_check(max_size);
		}

	} else {					/* there is place in tail */
		struct log_dump_chunk_s *log_dump_tail = (struct log_dump_chunk_s *)sq_tail(&log_dump_chunks);
		ASSERT(log_dump_tail);
		log_dump_tail->arr[compress_curbytes] = ch;
		compress_curbytes += 1;
		if (set_comp_head) {	/* new node, so compressed block starts at 0 */
			if (log_dump_tail->comp_head < 0) {	/* compressed node not present */
				log_dump_tail->comp_head = compress_curbytes - 1;
			}
			set_comp_head = false;
		}
		if (compress_curbytes % CONFIG_LOG_DUMP_MEMCHECK_SIZE == 0) {
			log_dump_mem_check(max_size);
			return LOG_DUMP_OK_HEAPCHK;
		}
	}
	return LOG_DUMP_OK;
}

/* Name: log_dump_inject_active
 *
 * Append diagnostic text to the active uncompressed slot so that it
 * flows through the normal compression stream right before the next
 * captured console bytes. Never blocks: silently skipped (or truncated)
 * when the slot is full.
 ****************************************************************************/
static void log_dump_inject_active(FAR const char *text, int len)
{
	irqstate_t flags = enter_critical_section();

	if (!uncomp_buf_full[uncomp_idx]) {
		for (int i = 0; i < len; i++) {
			if (uncomp_curbytes[uncomp_idx] == CONFIG_LOG_DUMP_CHUNK_SIZE) {
				break;
			}
			uncomp_buf[uncomp_idx][uncomp_curbytes[uncomp_idx]] = text[i];
			uncomp_curbytes[uncomp_idx]++;
		}
	}

	leave_critical_section(flags);
}

/* Name: log_dump_flush_drops
 *
 * Account for characters dropped because all staging buffers were
 * waiting for compression: place a gap marker into the captured stream
 * and emit a rate-limited notice. Called from the compress thread only;
 * the marker goes through the normal compression stream, so the reader
 * needs no format change.
 ****************************************************************************/
static void log_dump_flush_drops(void)
{
	uint32_t dropped = log_dump_drops_pending;

	if (dropped == 0) {
		return;
	}
	log_dump_drops_pending = 0;

	char marker[48];
	int len = snprintf(marker, sizeof(marker), "\n[log_dump: %lu bytes dropped]\n", (unsigned long)dropped);
	log_dump_inject_active(marker, len);

	/* Rate-limited live notice. The notice itself flows back through the
 * console hook, so guard against re-entrancy.
 */
	if (!gap_notice_active &&
	    (log_dump_notice_mark == 0 ||
	     log_dump_drops_total - log_dump_notice_mark >= LOG_DUMP_DROP_NOTICE_MIN_DROPS)) {
		gap_notice_active = true;
		syslog(LOG_ERR, "log_dump: console capture dropped %lu bytes (compress thread cannot keep up)\n", (unsigned long)log_dump_drops_total);
		log_dump_notice_mark = log_dump_drops_total;
		gap_notice_active = false;
	}
}
/* Name: log_dump_compress_ready_slots
 *
 * Compress-thread side: compress every full staging slot, push the
 * compressed stream into the final chunk list, report dropped-character
 * gaps and release the slots back to the console writer. Everything that
 * can block (compression allocations, kmm_malloc/kmm_mallinfo inside
 * log_dump_tobuffer) runs here and never in the console write path.
 ****************************************************************************/
static int log_dump_compress_ready_slots(void)
{
	int ret = LOG_DUMP_OK;

	for (int n = 0; n < CONFIG_LOG_DUMP_NUMBUFS; n++) {
		int i = comp_scan_idx++;
		if (comp_scan_idx == CONFIG_LOG_DUMP_NUMBUFS) {
			comp_scan_idx = 0;
		}

		if (!uncomp_buf_full[i]) {
			continue;
		}

		writesize = CONFIG_LOG_DUMP_CHUNK_SIZE;
		compress_ret = compress_block(out_buf, (long unsigned int *)&writesize, (unsigned char *)uncomp_buf[i], CONFIG_LOG_DUMP_CHUNK_SIZE);
		if (compress_ret != LOG_DUMP_OK) {
			ldpdbg("Fail to compress compress_ret = %d\n", compress_ret);
		} else {
			size_t free_size = 0;
			int store_ret = LOG_DUMP_OK;

			snprintf(comp_size, LOG_DUMP_COMPRESS_NODESZ, "%04d", writesize);
			for (int m = 0; m < LOG_DUMP_COMPRESS_NODESZ - 1 && store_ret >= 0; m++) {
				store_ret = log_dump_tobuffer(comp_size[m], &free_size);
			}
			for (int write_idx = 0; write_idx < writesize && store_ret >= 0; write_idx++) {
				store_ret = log_dump_tobuffer(out_buf[write_idx], &free_size);
			}
			if (store_ret < 0) {
				ldpdbg("Fail to store compressed log ret = %d\n", store_ret);
			} else if (store_ret == LOG_DUMP_OK_HEAPCHK) {
				/* Preserve upstream behavior: record the free heap size in the
				 * active staging slot, to be compressed with the next chunk.
					*/
				char fsz_array[LOG_DUMP_STRLEN_FREEHEAP];
				snprintf(fsz_array, sizeof(fsz_array), "<fsz:%08d>", (unsigned int)free_size);
				log_dump_inject_active(fsz_array, strlen(fsz_array));
			}
		}

		log_dump_flush_drops();
		uncomp_buf_full[i] = false;
	}

	return ret;
}

int log_dump_save(char ch)
{
	if (is_started_to_save == false) {
		return LOG_DUMP_OK;
	}

	irqstate_t flags = enter_critical_section();

	if (uncomp_buf_full[uncomp_idx]) {
		/* All staging buffers are still waiting for compression: drop the
 * character instead of ever blocking the console write path. The
 * drop is reported by the compress thread (gap marker + rate-limited
 * notice) when a slot frees up.
 */
		log_dump_drops_pending++;
		log_dump_drops_total++;
		leave_critical_section(flags);
		return LOG_DUMP_FAIL;
	}

	uncomp_buf[uncomp_idx][uncomp_curbytes[uncomp_idx]] = ch;
	uncomp_curbytes[uncomp_idx]++;

	bool kick = false;
	if (uncomp_curbytes[uncomp_idx] == CONFIG_LOG_DUMP_CHUNK_SIZE) {
		uncomp_curbytes[uncomp_idx] = 0;
		uncomp_buf_full[uncomp_idx] = true;
		int temp = uncomp_idx++;
		if (uncomp_idx == CONFIG_LOG_DUMP_NUMBUFS) {
			uncomp_idx = 0;
		}
		if (uncomp_buf_full[uncomp_idx]) {
			/* next slot still pending compression: characters drop until it
 * is released by the compress thread
 */
			uncomp_idx = temp;
		}
		kick = true;
	}

	leave_critical_section(flags);

	if (kick && log_dump_mq_ready) {
		/* Non-blocking wake-up of the compress thread. The send file object is
 * opened O_NONBLOCK; a full queue only means the thread is behind,
 * and the slot stays marked full for a later kick.
 */
		int msg_compress = false;
		file_mq_send(&mq_send_file, (const char *)&msg_compress, sizeof(int), 100);
	}

	return LOG_DUMP_OK;
}

int log_dump_get_size(void)
{
	/* If log_dump save is stopped, that means partially uncompressed buffer
	 * is compressed and size should include last_comp_block_size */
	if (is_started_to_save == false) {
		return log_dump_chunk_node_cnt * CONFIG_LOG_DUMP_CHUNK_SIZE + compress_curbytes + last_comp_block_size;
	} else {
		ldpdbg("Fail to get log dump size, try after stopping log dump save\n");
		return LOG_DUMP_FAIL;
	}
}

size_t log_dump_read(FAR char *buffer, size_t buflen)
{
	size_t ret = 0;

	sched_lock();	/* to ensure that the read is not disturbed by add_char */

	while (ret < buflen) {
		if (read_node == (struct log_dump_chunk_s *)sq_tail(&log_dump_chunks)) {	/* last chunk */
			if (compress_rdptr == compress_curbytes) {	/* all data has been read */
				if (last_comp_block_ptr < last_comp_block_size) {
					buffer[ret] = last_comp_block[last_comp_block_ptr];
					ret++;
					last_comp_block_ptr++;
				} else {
					break;
				}
			} else {
				buffer[ret] = read_node->arr[compress_rdptr];
				compress_rdptr++;
				ret++;
			}
		} else {	/* reading a filled chunk */

			buffer[ret] = read_node->arr[compress_rdptr];
			compress_rdptr++;
			ret++;
			if (compress_rdptr == CONFIG_LOG_DUMP_CHUNK_SIZE) {	/* completed reading a chunk */
				compress_rdptr = 0;
				read_node = (struct log_dump_chunk_s *)sq_next((sq_entry_t *)read_node);
			}
		}
	}

	sched_unlock();

	return ret;
}

/****************************************************************************
 * Name: log_dump_initialize
 *
 * Description:
 *   Start the log dump thread that stores console logs to a buffer and
 *   compresses them.
 *
 ****************************************************************************/

void log_dump_initialize(void)
{
	svdbg("Starting log_dump thread\n");

	pid_t pid = kthread_create(LOG_DUMP_NAME, CONFIG_LOG_DUMP_PRIO, LOG_DUMP_STACKSIZE, log_dump, NULL);
	if (pid < 0) {
		sdbg("Failed to start log dump\n");
	}
}

int log_dump(int argc, char *argv[])
{
	struct mq_attr attr;
	int ret;

	attr.mq_maxmsg = 32;
	attr.mq_msgsize = sizeof(int);
	attr.mq_flags = 0;

#ifdef CONFIG_DEBUG_MM_HEAPINFO
	/* Store the PID of log_dump kernel thread for memory attribution */
	g_log_dump_pid = getpid();
#endif

	/* Open the message queue with two separate file objects.
	 *
	 * An mqd_t is a per-task-group fd-table index; a queue opened by this
	 * kernel thread is not resolvable from the producer tasks (serial write
	 * path, /proc/logsave clients), so the fd-based mq_send()/mq_receive()
	 * must not be used here. file_mq_open() hands back a struct file that is
	 * valid in every context, and O_NONBLOCK is per-file-object:
	 *
	 *  - mq_recv_file: opened without O_NONBLOCK; file_mq_receive() blocks
	 *    when the queue is empty, so the compress thread sleeps (visible as
	 *    MQNEMPTY in ps) instead of spinning.
	 *  - mq_send_file: opened with O_NONBLOCK; producers on the console
	 *    write path never sleep on a full queue - a full queue only means
	 *    the compress thread is behind, and the slot stays marked full
	 *    for a later kick.
	 */
	ret = file_mq_open(&mq_recv_file, LOG_DUMP_MSGQ, O_RDONLY | O_CREAT, 0666, &attr);
	if (ret < 0) {
		ldpdbg("Fail to open mq, errno %d. EXIT!\n", -ret);
		return 0;
	}

	ret = file_mq_open(&mq_send_file, LOG_DUMP_MSGQ, O_WRONLY | O_NONBLOCK);
	if (ret < 0) {
		ldpdbg("Fail to open send mq, errno %d. EXIT!\n", -ret);
		file_mq_close(&mq_recv_file);
		return 0;
	}

	if (log_dump_init() != LOG_DUMP_OK) {
		ldpdbg("Fail to init log dump\n");
		file_mq_close(&mq_recv_file);
		file_mq_close(&mq_send_file);
		return 0;
	}

	log_dump_mq_ready = true;

	int msg_compress;
	int nbytes;

	while (true) {
		nbytes = file_mq_receive(&mq_recv_file, (char *)&msg_compress, sizeof(int), NULL);
		if (nbytes <= 0) {
			/* Interrupted or transient error; keep waiting for work */
			continue;
		}
		if (msg_compress) {
			/* To compress last partially filled block during app read*/
			log_dump_compress_lastblock();
			compress_last_block = false;
			nxsem_post(&lastblock_sem);
		} else {
			/* compress every completely filled staging slot */
			log_dump_compress_ready_slots();
		}
	}
}
