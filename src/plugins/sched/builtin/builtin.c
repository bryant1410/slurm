/*****************************************************************************\
 *  builtin.h - header for simple builtin scheduler plugin.
 *		Periodically when pending jobs can start.
 *		This is a minimal implementation of the logic found in
 *		src/plugins/sched/backfill/backfill.c and disregards
 *		how jobs are scheduled sequencially.
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/sched/builtin/builtin.h"

#ifndef BACKFILL_INTERVAL
#  define BACKFILL_INTERVAL	30
#endif

/*********************** local variables *********************/
static bool stop_builtin = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static bool config_flag = false;
static int backfill_interval = BACKFILL_INTERVAL;
static int max_backfill_job_cnt = 50;
static int sched_timeout = 0;

/*********************** local functions *********************/
static void _compute_start_times(void);
static void _load_config(void);
static void _my_sleep(int secs);

/* Terminate builtin_agent */
extern void stop_builtin_agent(void)
{
	pthread_mutex_lock(&term_lock);
	stop_builtin = true;
	pthread_cond_signal(&term_cond);
	pthread_mutex_unlock(&term_lock);
}

static void _my_sleep(int secs)
{
	struct timespec ts = {0, 0};

	ts.tv_sec = time(NULL) + secs;
	pthread_mutex_lock(&term_lock);
	if (!stop_builtin)
		pthread_cond_timedwait(&term_cond, &term_lock, &ts);
	pthread_mutex_unlock(&term_lock);
}

static void _load_config(void)
{
	char *sched_params, *tmp_ptr;

	sched_timeout = slurm_get_msg_timeout() / 2;
	sched_timeout = MAX(sched_timeout, 1);
	sched_timeout = MIN(sched_timeout, 10);

	sched_params = slurm_get_sched_params();

	if (sched_params && (tmp_ptr=strstr(sched_params, "interval=")))
		backfill_interval = atoi(tmp_ptr + 9);
	if (backfill_interval < 1) {
		fatal("Invalid backfill scheduler interval: %d",
		      backfill_interval);
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "max_job_bf=")))
		max_backfill_job_cnt = atoi(tmp_ptr + 11);
	if (max_backfill_job_cnt < 1) {
		fatal("Invalid backfill scheduler max_job_bf: %d",
		      max_backfill_job_cnt);
	}
	xfree(sched_params);
}

static void _compute_start_times(void)
{
	int j, rc = SLURM_SUCCESS, job_cnt = 0;
	List job_queue;
	job_queue_rec_t *job_queue_rec;
	List preemptee_candidates = NULL;
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL;
	uint32_t max_nodes, min_nodes, req_nodes;
	time_t now = time(NULL), sched_start;

	sched_start = now;
	job_queue = build_job_queue();
	while ((job_queue_rec = (job_queue_rec_t *) 
				list_pop_bottom(job_queue, sort_job_queue2))) {
		job_ptr  = job_queue_rec->job_ptr;
		part_ptr = job_queue_rec->part_ptr;
		xfree(job_queue_rec);
		if (part_ptr != job_ptr->part_ptr)
			continue;	/* Only test one partition */

		if (job_cnt++ > max_backfill_job_cnt) {
			debug("backfill: loop taking to long, breaking out");
			break;
		}

		/* Determine minimum and maximum node counts */
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);

		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);

		max_nodes = MIN(max_nodes, 500000);     /* prevent overflows */

		if (job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;

		if (min_nodes > max_nodes) {
			/* job's min_nodes exceeds partition's max_nodes */
			continue;
		}

		j = job_test_resv(job_ptr, &now, true, &avail_bitmap);
		if (j != SLURM_SUCCESS)
			continue;

		rc = select_g_job_test(job_ptr, avail_bitmap,
				       min_nodes, max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       preemptee_candidates, NULL);
		last_job_update = now;
		FREE_NULL_BITMAP(avail_bitmap);

		if ((time(NULL) - sched_start) >= sched_timeout) {
			debug("backfill: loop taking to long, breaking out");
			break;
		}
	}
	list_destroy(job_queue);
}

/* Note that slurm.conf has changed */
extern void builtin_reconfig(void)
{
	config_flag = true;
}

/* builtin_agent - detached thread periodically when pending jobs can start */
extern void *builtin_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_backfill_time = 0;
	/* Read config and partitions; Write jobs and nodes */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	_load_config();
	last_backfill_time = time(NULL);
	while (!stop_builtin) {
		_my_sleep(backfill_interval);
		if (stop_builtin)
			break;
		if (config_flag) {
			config_flag = false;
			_load_config();
		}
		now = time(NULL);
		wait_time = difftime(now, last_backfill_time);
		if ((wait_time < backfill_interval))
			continue;

		lock_slurmctld(all_locks);
		_compute_start_times();
		last_backfill_time = time(NULL);
		unlock_slurmctld(all_locks);
	}
	return NULL;
}
