/*****************************************************************************\
 *  hostlist.c - Convert hostlist expressions between Slurm and Moab formats
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>

#include "src/common/hostlist.h"
#include "src/common/node_select.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MOAB_FORMAT1 1

/*
 * Convert Moab supplied TASKLIST expression into a SLURM hostlist expression
 *
 * Moab format 1: tux0:tux0:tux1:tux1:tux2   (list host for each cpu)
 * Moab format 2: tux[0-1]*2:tux2            (list cpu count after host name)
 *
 * SLURM format:  tux0,tux0,tux1,tux1,tux2   (if consumable resources enabled)
 * SLURM format:  tux0,tux1,tux2             (if consumable resources disabled)
 *
 * NOTE: returned string must be released with xfree()
 */
extern char * moab2slurm_task_list(char *moab_tasklist, int *task_cnt)
{
	char *slurm_tasklist, *host, *tmp1, *tmp2, *tok, *tok_p;
	int i, reps;
	hostlist_t hl;
	static uint32_t cr_test = 0, cr_enabled = 0;

	if (cr_test == 0) {
		select_g_get_info_from_plugin(SELECT_CR_PLUGIN,
						&cr_enabled);
		cr_test = 1;
	}

	*task_cnt = 0;
	tmp1 = strchr(moab_tasklist, (int) '*');

	if (tmp1 == NULL) {	/* Moab format 1 */
		slurm_tasklist = xstrdup(moab_tasklist);
		if (moab_tasklist[0])
			*task_cnt = 1;
		for (i=0; slurm_tasklist[i]!='\0'; i++) {
			if (slurm_tasklist[i] == ':') {
				slurm_tasklist[i] = ',';
				(*task_cnt)++;
			}
		}		
		return slurm_tasklist;
	}

	/* Moab format 2 */
	slurm_tasklist = xstrdup("");
	tmp1 = xstrdup(moab_tasklist);
	tok = strtok_r(tmp1, ":", &tok_p);
	while (tok) {
		/* find task count, assume 1 if no "*" */
		tmp2 = strchr(tok, (int) '*');
		if (tmp2) {
			reps = atoi(tmp2 + 1);
			tmp2[0] = '\0';
		} else
			reps = 1;

		/* find host expression */
		hl = hostlist_create(tok);
		while ((host = hostlist_shift(hl))) {
			for (i=0; i<reps; i++) {
				if (slurm_tasklist[0])
					xstrcat(slurm_tasklist, ",");
				xstrcat(slurm_tasklist, host);
				if (!cr_enabled)
					break;
			}
			free(host);
			(*task_cnt) += reps;
		}
		hostlist_destroy(hl);

		/* get next token */
		tok = strtok_r(NULL, ":", &tok_p);
	}
	xfree(tmp1);
	return slurm_tasklist;
}

#ifndef MOAB_FORMAT1
/* Append to buf a compact tasklist expression (e.g. "tux[0-1]*2")
 * Prepend ":" to expression as needed */
static void _append_hl_buf(char **buf, hostlist_t *hl_tmp, int *reps)
{
	int host_str_len = 4096;
	char *host_str, rep_str[8];

	host_str = xmalloc(host_str_len);
	hostlist_uniq(*hl_tmp);
	while (hostlist_ranged_string(*hl_tmp, host_str_len, host_str) < 0) {
		host_str_len *= 2;
		xrealloc(*host_str, host_str_len);
	}
	if (*buf)
		xstrcat(*buf, ":");
	xstrcat(*buf, host_str);
	snprintf(rep_str, 8, "*%d", *reps);
	xstrcat(*buf, rep_str);
	xfree(host_str);
	hostlist_destroy(*hl_tmp);
	*hl_tmp = (hostlist_t) NULL;
	*reps = 0;
}
#endif

/*
 * Report a job's tasks a a MOAB TASKLIST expression
 *
 * Moab format 1: tux0:tux0:tux1:tux1:tux2   (list host for each cpu)
 * Moab format 2: tux[0-1]*2:tux2            (list cpu count after host name)
 *
 * NOTE: returned string must be released with xfree()
 */
extern char * slurm_job2moab_task_list(struct job_record *job_ptr)
{
#ifdef MOAB_FORMAT1
	/*
	 * Moab format 1: tux0:tux0:tux1:tux1:tux2
	 */
	int i, j;
	char *buf = NULL, *host;
	hostlist_t hl = hostlist_create(job_ptr->nodes);

	if (hl == NULL) {
		error("hostlist_create error for job %u, %s",
			job_ptr->job_id, job_ptr->nodes);
		return buf;
	}

	for (i=0; i<job_ptr->alloc_lps_cnt; i++) {
		host = hostlist_shift(hl);
		if (host == NULL) {
			error("bad alloc_lps_cnt for job %u (%s, %d)", 
				job_ptr->job_id, job_ptr->nodes,
				job_ptr->alloc_lps_cnt);
			break;
		}
		for (j=0; j<job_ptr->alloc_lps[i]; j++) {
			if (buf)
				xstrcat(buf, ":");
			xstrcat(buf, host);
		}
		free(host);
	}
	hostlist_destroy(hl);
	return buf;
#else
	/*
	 * Moab format 2: tux[0-1]*2:tux2
	 */
	int i, reps = -1;
	char *buf = NULL, *host;
	hostlist_t hl = hostlist_create(job_ptr->nodes);
	hostlist_t hl_tmp = (hostlist_t) NULL;

	if (hl == NULL) {
		error("hostlist_create error for job %u, %s",
			job_ptr->job_id, job_ptr->nodes);
		return buf;
	}

	for (i=0; i<job_ptr->alloc_lps_cnt; i++) {
		host = hostlist_shift(hl);
		if (host == NULL) {
			error("bad alloc_lps_cnt for job %u (%s, %d)", 
				job_ptr->job_id, job_ptr->nodes,
				job_ptr->alloc_lps_cnt);
			break;
		}

		if (reps == job_ptr->alloc_lps[i]) {
			/* append to existing hostlist record */
			if (hostlist_push(hl_tmp, host))
				error("hostlist_push failure");
		} else {
			if (hl_tmp)
				_append_hl_buf(&buf, &hl_tmp, &reps);

			/* start new hostlist record */
			hl_tmp = hostlist_create(host);
			if (hl_tmp)
				reps = job_ptr->alloc_lps[i];
			else
				error("hostlist_create failure");
		}
		free(host);
	}
	hostlist_destroy(hl);
	if (hl_tmp)
		_append_hl_buf(&buf, &hl_tmp, &reps);
	return buf;
#endif
}
