#include <tinyara/config.h>

#include <sched.h>
#include <assert.h>

#include <tinyara/sched.h>

#include "sched/sched.h"

#if defined(CONFIG_NET) && CONFIG_NBSDSOCKET_DESCRIPTORS > 0

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sched_getsockets
 *
 * Description:
 *   Return a pointer to the BSD socket list for this thread
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   A pointer to the BSD socket list of the calling task's group.
 *
 * Assumptions:
 *
 ****************************************************************************/

FAR struct socketlist *sched_getsockets(void)
{
	FAR struct tcb_s *rtcb = this_task();
	FAR struct task_group_s *group = rtcb->group;

	DEBUGASSERT(group);

	return &group->tg_socketlist;
}

#endif /* defined(CONFIG_NET) && CONFIG_NBSDSOCKET_DESCRIPTORS > 0 */