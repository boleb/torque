/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * svr_holdjob.c
 *
 * Functions relating to the Hold and Release Job Batch Requests.
 *
 * Included funtions are:
 * req_holdjob()
 * req_releasejob()
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <pthread.h>
#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "work_task.h"
#include "pbs_error.h"
#include "log.h"
#include "../lib/Liblog/log_event.h"
#include "acct.h"
#include "svrfunc.h"
#include "csv.h"
#include "array.h"
#include "ji_mutex.h"

/* Private Functions Local to this file */

void process_checkpoint_reply(batch_request *);
void process_hold_reply(batch_request *);

/* Global Data Items: */

extern struct server server;
extern char *msg_jobholdset;
extern char *msg_jobholdrel;
extern char *msg_mombadhold;
extern char *msg_postmomnojob;
extern int LOGLEVEL;

int chk_hold_priv(long val, int perm);
int get_hold(tlist_head *, const char **, pbs_attribute *);

/* external functions */
extern int svr_authorize_jobreq(struct batch_request *,job *);
extern job *chk_job_request(char *,struct batch_request *);
int copy_batchrequest(struct batch_request **newreq, struct batch_request *preq, int type, int jobid);

/*
 * chk_hold_priv - check that client has privilege to set/clear hold
 */

int chk_hold_priv(

  long val,   /* hold bits being changed */
  int  perm)  /* client privilege */

  {
  if ((val & HOLD_s) && ((perm & ATR_DFLAG_MGWR) == 0))
    {
    return(PBSE_PERM);
    }

  if ((val & HOLD_o) && ((perm & (ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)) == 0))
    {
    return(PBSE_PERM);
    }

  return(PBSE_NONE);
  }  /* END chk_hold_priv() */



/*
 * req_holdjob - service the Hold Job Request
 *
 * This request sets one or more holds on a job.
 * The state of the job may change as a result.
 */

int req_holdjob(

    struct batch_request *vp) /* I */

  {
  long                 *hold_val;
  int                   newstate;
  int                   newsub;
  long                  old_hold;
  job                  *pjob;
  char                 *pset;
  int                   rc;
  pbs_attribute         temphold;
  pbs_attribute        *pattr;
  struct batch_request *preq = (struct batch_request *)vp;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];
  struct batch_request *dup_req = NULL;

  pjob = chk_job_request(preq->rq_ind.rq_hold.rq_orig.rq_objname, preq);

  if (pjob == NULL)
    {
    return(PBSE_NONE);
    }

  /* cannot do anything until we decode the holds to be set */

  if ((rc = get_hold(&preq->rq_ind.rq_hold.rq_orig.rq_attr, (const char **)&pset,
                     &temphold)) != 0)
    {
    req_reject(rc, 0, preq, NULL, NULL);

    unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);

    return(PBSE_NONE);
    }

  /* if other than HOLD_u is being set, must have privil */

  if ((rc = chk_hold_priv(temphold.at_val.at_long, preq->rq_perm)) != 0)
    {
    req_reject(rc, 0, preq, NULL, NULL);

    unlock_ji_mutex(pjob, __func__, "2", LOGLEVEL);

    return(PBSE_NONE);
    }

  hold_val = &pjob->ji_wattr[JOB_ATR_hold].at_val.at_long;

  old_hold = *hold_val;
  *hold_val |= temphold.at_val.at_long;
  pjob->ji_wattr[JOB_ATR_hold].at_flags |= ATR_VFLAG_SET;
  sprintf(log_buf, msg_jobholdset, pset, preq->rq_user, preq->rq_host);

  pattr = &pjob->ji_wattr[JOB_ATR_checkpoint];

  if ((pjob->ji_qs.ji_state == JOB_STATE_RUNNING) &&
      ((pattr->at_flags & ATR_VFLAG_SET) &&
       ((csv_find_string(pattr->at_val.at_str, "s") != NULL) ||
        (csv_find_string(pattr->at_val.at_str, "c") != NULL) ||
        (csv_find_string(pattr->at_val.at_str, "enabled") != NULL))))
    {

    /* have MOM attempt checkpointing */

    if ((rc = copy_batchrequest(&dup_req, preq, 0, -1)) != 0)
      {
      req_reject(rc, 0, preq, NULL, "memory allocation failure");
      }
    /* The dup_req is freed in relay_to_mom (failure)
     * or in issue_Drequest (success) */
    else if ((rc = relay_to_mom(&pjob, dup_req, NULL)) != PBSE_NONE)
      {
      free_br(dup_req);
      *hold_val = old_hold;  /* reset to the old value */
      req_reject(rc, 0, preq, NULL, NULL);
      }
    else
      {
      if (pjob != NULL)
        {
        pjob->ji_qs.ji_svrflags |= JOB_SVFLG_HASRUN | JOB_SVFLG_CHECKPOINT_FILE;
        
        job_save(pjob, SAVEJOB_QUICK, 0);
        
        /* fill in log_buf again, since relay_to_mom changed it */
        sprintf(log_buf, msg_jobholdset, pset, preq->rq_user, preq->rq_host);
        
        log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);
        unlock_ji_mutex(pjob, __func__, "3", LOGLEVEL);
        pjob = NULL;
        req_reject(rc, 0, preq, NULL, "relay to mom failed");
        }

      process_hold_reply(dup_req);
      }
    }
#ifdef ENABLE_BLCR
  else if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /*
     * This system is configured with BLCR checkpointing to be used,
     * but this Running job does not have checkpointing enabled,
     * so we reject the request
     */

    log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

    req_reject(PBSE_IVALREQ, 0, preq, NULL,
      "job not held since checkpointing is expected but not enabled for job");
    }
#endif
  else
    {
    /* everything went well, may need to update the job state */

    log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

    if (old_hold != *hold_val)
      {
      /* indicate attributes changed     */

      pjob->ji_modified = 1;

      svr_evaljobstate(pjob, &newstate, &newsub, 0);

      svr_setjobstate(pjob, newstate, newsub, FALSE);
      }

    reply_ack(preq);
    }

  if (pjob != NULL)
    unlock_ji_mutex(pjob, __func__, "3", LOGLEVEL);

  return(PBSE_NONE);
  }  /* END req_holdjob() */




/*
 * req_checkpointjob - service the Checkpoint Job Request
 *
 */

void *req_checkpointjob(

   struct batch_request *vp) /* I */

  {
  struct batch_request *preq = (struct batch_request *)vp;
  job                  *pjob;
  int                   rc;
  pbs_attribute        *pattr;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];
  struct batch_request *dup_req = NULL;

  if ((pjob = chk_job_request(preq->rq_ind.rq_manager.rq_objname, preq)) == NULL)
    {
    return(NULL);
    }

  pattr = &pjob->ji_wattr[JOB_ATR_checkpoint];

  if ((pjob->ji_qs.ji_state == JOB_STATE_RUNNING) &&
      ((pattr->at_flags & ATR_VFLAG_SET) &&
       ((csv_find_string(pattr->at_val.at_str, "s") != NULL) ||
        (csv_find_string(pattr->at_val.at_str, "c") != NULL) ||
        (csv_find_string(pattr->at_val.at_str, "enabled") != NULL))))
    {
    /* have MOM attempt checkpointing */

    if ((rc = copy_batchrequest(&dup_req, preq, 0, -1)) != 0)
      {
      req_reject(rc, 0, preq, NULL, "failure to allocate memory");
      }
    /* The dup_req is freed in relay_to_mom (failure)
     * or in issue_Drequest (success) */
    else if ((rc = relay_to_mom(&pjob, dup_req, NULL)) != PBSE_NONE)
      {
      req_reject(rc, 0, preq, NULL, NULL);
      free_br(dup_req);
      }
    else
      {
      if (pjob != NULL)
        {
        pjob->ji_qs.ji_svrflags |= JOB_SVFLG_CHECKPOINT_FILE;
        
        job_save(pjob, SAVEJOB_QUICK, 0);
        log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);
        unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);
        pjob = NULL;
        }

      process_checkpoint_reply(dup_req);
      }
    }
  else
    {
    /* Job does not have checkpointing enabled, so reject the request */

    log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

    req_reject(PBSE_IVALREQ, 0, preq, NULL, "job is not checkpointable");
    }

  if (pjob != NULL)
    unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);

  return(NULL);
  }  /* END req_checkpointjob() */




/*
 * release_job - releases the hold on job j
 * @param j - the job to modify
 * @return 0 if successful, a PBS error on failure
 */
int release_job(

  struct batch_request *preq, /* I */
  void                 *j)    /* I/O */

  {
  long           old_hold;
  int            rc = 0;
  int            newstate;
  int            newsub;
  char          *pset;
  job           *pjob = (job *)j;
  char           log_buf[LOCAL_LOG_BUF_SIZE];

  pbs_attribute  temphold;

  /* cannot do anything until we decode the holds to be set */

  if ((rc = get_hold(&preq->rq_ind.rq_hold.rq_orig.rq_attr, (const char **)&pset, &temphold)) != 0)
    {
    return(rc);
    }

  /* if other than HOLD_u is being released, must have privil */

  if ((rc = chk_hold_priv(temphold.at_val.at_long, preq->rq_perm)) != 0)
    {
    return(rc);
    }

  /* unset the hold */

  old_hold = pjob->ji_wattr[JOB_ATR_hold].at_val.at_long;

  if ((rc = job_attr_def[JOB_ATR_hold].at_set(&pjob->ji_wattr[JOB_ATR_hold], &temphold, DECR)))
    {
    return(rc);
    }

  /* everything went well, if holds changed, update the job state */

  if (old_hold != pjob->ji_wattr[JOB_ATR_hold].at_val.at_long)
    {
    pjob->ji_modified = 1; /* indicates attributes changed */

    svr_evaljobstate(pjob, &newstate, &newsub, 0);

    svr_setjobstate(pjob, newstate, newsub, FALSE); /* saves job */
    }

  sprintf(log_buf, msg_jobholdrel,
    pset,
    preq->rq_user,
    preq->rq_host);

  log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);

  return(rc);
  } /* END release_job() */




/*
 * req_releasejob - service the Release Job Request
 *
 *  This request clears one or more holds on a job.
 * As a result, the job might change state.
 */

int req_releasejob(

    struct batch_request *vp) /* I */

  {
  job  *pjob;
  int   rc;
  struct batch_request *preq = (struct batch_request *)vp; 

  pjob = chk_job_request(preq->rq_ind.rq_release.rq_objname, preq);

  if (pjob == NULL)
    {
    return(PBSE_NONE);
    }

  if ((rc = release_job(preq,pjob)) != 0)
    {
    req_reject(rc,0,preq,NULL,NULL);
    }
  else
    {
    reply_ack(preq);
    }

  unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);

  return(PBSE_NONE);
  }  /* END req_releasejob() */



int release_whole_array(

  job_array            *pa,   /* I/0 */
  struct batch_request *preq) /* I */

  {
  int  i;
  int  rc;
  job *pjob;

  for (i = 0; i < pa->ai_qs.array_size; i++)
    {
    if (pa->job_ids[i] == NULL)
      continue;

    if ((pjob = svr_find_job(pa->job_ids[i], FALSE)) == NULL)
      {
      free(pa->job_ids[i]);
      pa->job_ids[i] = NULL;
      }
    else
      {
      if ((rc = release_job(preq, pjob)) != 0)
        {
        unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);
        return(rc);
        }
  
      unlock_ji_mutex(pjob, __func__, "2", LOGLEVEL);
      }
    }

  /* SUCCESS */
  return(PBSE_NONE);
  } /* END release_whole_array */



int req_releasearray(

    struct batch_request *vp) /* I */

  {
  job                  *pjob;
  job_array            *pa;
  char                 *range;
  int                   rc;
  int                   index;
  struct batch_request *preq = (struct batch_request *)vp;

  pa = get_array(preq->rq_ind.rq_release.rq_objname);
  if (pa == NULL)
    {
    req_reject(PBSE_IVALREQ,0,preq,NULL,"Cannot find array");
    return(PBSE_NONE);
    }

  while (TRUE)
    {
    if (((index = first_job_index(pa)) == -1) ||
        (pa->job_ids[index] == NULL))
      {
      unlock_ai_mutex(pa, __func__, "1", LOGLEVEL);

      return(PBSE_NONE);
      }

    if ((pjob = svr_find_job(pa->job_ids[index], FALSE)) == NULL)
      {
      free(pa->job_ids[index]);
      pa->job_ids[index] = NULL;
      }
    else
      break;
    }

  if (svr_authorize_jobreq(preq, pjob) == -1)
    {
    req_reject(PBSE_PERM,0,preq,NULL,NULL);

    unlock_ai_mutex(pa, __func__, "2", LOGLEVEL);
    unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);

    return(PBSE_NONE);
    }

  unlock_ji_mutex(pjob, __func__, "2", LOGLEVEL);

  range = preq->rq_extend;
  if ((range != NULL) &&
      (strstr(range,ARRAY_RANGE) != NULL))
    {
    /* parse the array range */
    if ((rc = release_array_range(pa,preq,range)) != 0)
      {
      unlock_ai_mutex(pa, __func__, "3", LOGLEVEL);

      req_reject(rc,0,preq,NULL,NULL);

      return(PBSE_NONE);
      }
    }
  else if ((rc = release_whole_array(pa,preq)) != 0)
    {
    unlock_ai_mutex(pa, __func__, "4", LOGLEVEL);

    req_reject(rc,0,preq,NULL,NULL);

    return(PBSE_NONE);
    }
  
  unlock_ai_mutex(pa, __func__, "5", LOGLEVEL);

  reply_ack(preq);

  return(PBSE_NONE);
  } /* END req_releasearray() */




/*
 * get_hold - search a list of attributes (svrattrl) for the hold-types
 *  pbs_attribute.  This is used by the Hold Job and Release Job request,
 * therefore it is an error if the hold-types pbs_attribute is not present,
 * or there is more than one.
 *
 * Decode the hold pbs_attribute into temphold.
 */

int get_hold(

  tlist_head     *phead,
  const char   **pset,     /* O - ptr to hold value */
  pbs_attribute  *temphold)   /* O - ptr to pbs_attribute to decode value into  */


  {
  int   have_one = 0;

  struct svrattrl *holdattr = NULL;

  struct svrattrl *pal;

  pal = (struct svrattrl *)GET_NEXT((*phead));

  while (pal != NULL)
    {
    if (!strcmp(pal->al_name, job_attr_def[JOB_ATR_hold].at_name))
      {
      holdattr = pal;

      *pset    = pal->al_value;

      have_one++;
      }
    else
      {
      return(PBSE_IVALREQ);
      }

    pal = (struct svrattrl *)GET_NEXT(pal->al_link);
    }

  if (have_one != 1)
    {
    return(PBSE_IVALREQ);
    }

  /* decode into temporary pbs_attribute structure */

  clear_attr(temphold, &job_attr_def[JOB_ATR_hold]);

  return(job_attr_def[JOB_ATR_hold].at_decode(
           temphold,
           holdattr->al_name,
           (char *)0,
           holdattr->al_value,
           0));
  }






/*
 * process_hold_reply
 * called when a hold request was sent to MOM and the answer
 * is received.  Completes the hold request for running jobs.
 */

void process_hold_reply(

  batch_request *preq)

  {
  job                  *pjob;
  pbs_attribute         temphold;

  int                   newstate;
  int                   newsub;
  int                   rc;
  char                 *pset;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];

  /* preq was handled previously */
  if (preq == NULL)
    return;

  preq->rq_conn = preq->rq_orgconn;  /* restore client socket */

  if ((pjob = svr_find_job(preq->rq_ind.rq_hold.rq_orig.rq_objname, FALSE)) == NULL)
    {
    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB,
              preq->rq_ind.rq_hold.rq_orig.rq_objname,
              msg_postmomnojob);
    req_reject(PBSE_UNKJOBID, 0, preq, NULL, msg_postmomnojob);

    return;
    }
  else if (preq->rq_reply.brp_code != 0)
    {

    rc = get_hold(&preq->rq_ind.rq_hold.rq_orig.rq_attr, (const char **)&pset, &temphold);

    if (rc == 0)
      {
      rc = job_attr_def[JOB_ATR_hold].at_set(&pjob->ji_wattr[JOB_ATR_hold],
           &temphold, DECR);
      }

    pjob->ji_qs.ji_substate = JOB_SUBSTATE_RUNNING;  /* reset it */

    pjob->ji_modified = 1;    /* indicate attributes changed */
    svr_evaljobstate(pjob, &newstate, &newsub, 0);
    svr_setjobstate(pjob, newstate, newsub, FALSE); /* saves job */

    if (preq->rq_reply.brp_code != PBSE_NOSUP)
      {
      sprintf(log_buf, msg_mombadhold, preq->rq_reply.brp_code);
      log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);
      req_reject(preq->rq_reply.brp_code, 0, preq, NULL, log_buf);
      }
    else
      {
      reply_ack(preq);
      }
    }
  else
    {
    /* record that MOM has a checkpoint file */

    /* PBS_CHECKPOINT_MIGRATEABLE is defined as zero therefore this code will never fire.
     * And if these flags are not set, start_exec will not try to run the job from
     * the checkpoint image file.
     */

    pjob->ji_qs.ji_svrflags |= JOB_SVFLG_CHECKPOINT_FILE;

    if (preq->rq_reply.brp_auxcode)  /* checkpoint can be moved */
      {
      pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_CHECKPOINT_FILE;
      pjob->ji_qs.ji_svrflags |=  JOB_SVFLG_HASRUN | JOB_SVFLG_CHECKPOINT_MIGRATEABLE;
      }

    pjob->ji_modified = 1;    /* indicate attributes changed     */

    svr_evaljobstate(pjob, &newstate, &newsub, 0);
    svr_setjobstate(pjob, newstate, newsub, FALSE); /* saves job */

    account_record(PBS_ACCT_CHKPNT, pjob, "Checkpointed and held"); /* note in accounting file */
    reply_ack(preq);
    }

  unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);

  } /* END process_hold_reply() */




/*
 * process_checkpoint_reply
 * called when a checkpoint request was sent to MOM and the answer
 * is received.  Completes the checkpoint request for running jobs.
 */

void process_checkpoint_reply(

  batch_request *preq)

  {
  job *pjob;

  /* preq handled previously */
  if (preq == NULL)
    return;

  preq->rq_conn = preq->rq_orgconn;  /* restore client socket */

  if ((pjob = svr_find_job(preq->rq_ind.rq_manager.rq_objname, FALSE)) == NULL)
    {
    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB,
      preq->rq_ind.rq_manager.rq_objname,
      msg_postmomnojob);
    req_reject(PBSE_UNKJOBID, 0, preq, NULL, msg_postmomnojob);
    }
  else
    {
    /* record that MOM has a checkpoint file */

    account_record(PBS_ACCT_CHKPNT, pjob, "Checkpointed"); /* note in accounting file */
    reply_ack(preq);

    unlock_ji_mutex(pjob, __func__, "1", LOGLEVEL);
    }
  } /* END process_checkpoint_reply() */

