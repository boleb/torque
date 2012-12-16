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
** This program exists to give a way to mark nodes
** Down, Offline, or Free in PBS.
**
** usage: pbsnodes [-s server][-{c|l|o|r}] node node ...
**
** where the node(s) are the names given in the node
** description file.
**
** pbsnodes   clear "DOWN" from all nodes so marked
**
** pbsnodes node1 node2  set nodes node1, node2 "DOWN"
**     unmark "DOWN" from any other node
**
** pbsnodes -a   list all nodes
**
** pbsnodes -l   list all nodes marked in any way
**
** pbsnodes -o node1 node2  mark nodes node1, node2 as OFF_LINE
**     even if currently in use.
**
** pbsnodes -r node1 node2  clear OFF_LINE from listed nodes
**
** pbsnodes -c node1 node2  clear OFF_LINE or DOWN from listed nodes
*/
#include <pbs_config.h>   /* the master config generated by configure */
#include "pbsnodes.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "portability.h"
#include "pbs_ifl.h"
#include        "mcom.h"
#include        "cmds.h"
#include "libcmds.h" /* TShowAbout_exit */


#define LIST 1
#define CLEAR 2
#define OFFLINE 3
#define RESET 4
#define ALLI 5
#define PURGE   6
#define DIAG    7
#define NOTE    8

enum note_flags {unused, set, list};

int quiet = 0;
char *progname;


/* globals */

mbool_t DisplayXML = FALSE;

/* END globals */


/*
 * set_note - set the note attribute for a node
 *
 */

static int set_note(

  int    con,
  char  *name,
  char  *msg)

  {
  char          *errmsg;

  struct attropl  new_attr;
  int             rc;
  int             local_errno = 0;

  new_attr.name     = (char *)ATTR_NODE_note;
  new_attr.resource = NULL;
  new_attr.value    = msg;
  new_attr.op       = SET;
  new_attr.next     = NULL;

  rc = pbs_manager_err(
         con,
         MGR_CMD_SET,
         MGR_OBJ_NODE,
         name,
         &new_attr,
         NULL,
         &local_errno);

  if (rc && !quiet)
    {
    fprintf(stderr, "Error setting note attribute for %s - ",
      name);

    if ((errmsg = pbs_geterrmsg(con)) != NULL)
      {
      fprintf(stderr, "%s\n",
        errmsg);
      }
    }

  return(rc);
  }  /* END set_note() */



static void prt_node_attr(

  struct batch_status *pbs,         /* I */
  int                  IsVerbose)   /* I */

  {

  struct attrl *pat;

  for (pat = pbs->attribs;pat;pat = pat->next)
    {
    if ((pat->value == NULL) || (pat->value[0] == '?'))
      {
      if (IsVerbose == 0)
        continue;
      }

    printf("     %s = %s\n",

           pat->name,
           pat->value);
    }  /* END for (pat) */

  return;
  }  /* END prt_node_attr() */




static char *get_nstate(

  struct batch_status *pbs)  /* I */

  {

  struct attrl *pat;

  for (pat = pbs->attribs;pat != NULL;pat = pat->next)
    {
    if (strcmp(pat->name, ATTR_NODE_state) == 0)
      {
      return(pat->value);
      }
    }

  return((char *)"");
  }




/* returns a pointer to the note if there is one, otherwise NULL */
static char *get_note(

  struct batch_status *pbs)  /* I */

  {

  struct attrl *pat;

  for (pat = pbs->attribs;pat != NULL;pat = pat->next)
    {
    if (strcmp(pat->name, ATTR_NODE_note) == 0)
      {
      return(pat->value);
      }
    }

  return(NULL);
  }



static int marknode(

  int            con,
  const char   *name,
  const char   *state1,
  enum batch_op  op1,
  const char   *state2,
  enum batch_op  op2)

  {
  char          *errmsg;

  struct attropl  new_attr[2];
  int             rc;
  int             local_errno = 0;

  new_attr[0].name     = (char *)ATTR_NODE_state;
  new_attr[0].resource = NULL;
  new_attr[0].value    = (char *)state1;
  new_attr[0].op       = op1;

  if (state2 == NULL)
    {
    new_attr[0].next     = NULL;
    }
  else
    {
    new_attr[0].next     = &new_attr[1];
    new_attr[1].next     = NULL;
    new_attr[1].name     = (char *)ATTR_NODE_state;
    new_attr[1].resource = NULL;
    new_attr[1].value    = (char *)state2;
    new_attr[1].op     = op2;
    }

  rc = pbs_manager_err(
         con,
         MGR_CMD_SET,
         MGR_OBJ_NODE,
         (char *)name,
         new_attr,
         NULL,
         &local_errno);

  if (rc && !quiet)
    {
    fprintf(stderr, "Error marking node %s - ",
            name);

    if ((errmsg = pbs_geterrmsg(con)) != NULL)
      fprintf(stderr, "%s\n",
              errmsg);
    }

  return(rc);
  }  /* END marknode() */




struct batch_status *statnode(

  int   con,
  char *nodearg)

  {

  struct batch_status *bstatus;
  char                *errmsg;
  int                  local_errno = 0;

  bstatus = pbs_statnode_err(con, nodearg, NULL, NULL, &local_errno);

  if (bstatus == NULL)
    {
    if (local_errno)
      {
      if (!quiet)
        {
        if ((errmsg = pbs_geterrmsg(con)) != NULL)
          {
          fprintf(stderr, "%s: %s\n",
                  progname,
                  errmsg);
          }
        else
          {
          fprintf(stderr, "%s: Error %d (%s)\n",
                  progname,
                  local_errno,
                  pbs_strerror(local_errno));
          }
        }

      exit(1);
      }

    if (!quiet)
      fprintf(stderr, "%s: No nodes found\n",
              progname);

    exit(2);
    }

  return bstatus;
  }    /* END statnode() */




void addxmlnode(

  mxml_t              *DE,
  struct batch_status *pbstat)

  {
  mxml_t *NE;
  mxml_t *AE;

  struct attrl *pat;

  NE = NULL;

  MXMLCreateE(&NE, "Node");

  MXMLAddE(DE, NE);

  /* add nodeid */

  AE = NULL;
  MXMLCreateE(&AE, "name");
  MXMLSetVal(AE, pbstat->name, mdfString);
  MXMLAddE(NE, AE);

  for (pat = pbstat->attribs;pat;pat = pat->next)
    {
    AE = NULL;

    if (pat->value == NULL)
      continue;

    MXMLCreateE(&AE, pat->name);

    MXMLSetVal(AE, pat->value, mdfString);

    MXMLAddE(NE, AE);
    }

  return;
  } /* END addxmlnode() */


const char *NState[] =
  {
  "",
  ND_free,
  ND_offline,
  ND_down,
  ND_reserve,
  ND_job_exclusive,
  ND_job_sharing,
  ND_busy,
  ND_state_unknown,
  ND_timeshared,
  ND_active,
  ND_all,
  ND_up,
  NULL
  };


int filterbystate(

  struct batch_status *pbstat,
  enum NStateEnum      ListType,
  char                *S)

  {
  int Display;

  Display = 0;

  switch (ListType)
    {

    case tnsNONE:  /* display down, offline, and unknown nodes */

    default:

      if (strstr(S, ND_down) || strstr(S, ND_offline) || strstr(S, ND_state_unknown))
        {
        Display = 1;
        }

      break;

    case tnsFree:     /* node is idle/free */

      if (strstr(S, ND_free))
        {
        Display = 1;
        }

      break;

    case tnsOffline:  /* node is offline */

      if (strstr(S, ND_offline))
        {
        Display = 1;
        }

      break;

    case tnsDown:     /* node is down or unknown */

      if (strstr(S, ND_down) || strstr(S, ND_state_unknown))
        {
        Display = 1;
        }

      break;

    case tnsReserve:

      if (strstr(S, ND_reserve))
        {
        Display = 1;
        }

      break;

    case tnsJobExclusive:

      if (strstr(S, ND_job_exclusive))
        {
        Display = 1;
        }

      break;

    case tnsJobSharing:

      if (strstr(S, ND_job_sharing))
        {
        Display = 1;
        }

      break;

    case tnsBusy:     /* node cannot accept additional workload */

      if (strstr(S, ND_busy))
        {
        Display = 1;
        }

      break;

    case tnsUnknown:  /* node is unknown - no contact recieved */

      if (strstr(S, ND_state_unknown))
        {
        Display = 1;
        }

      break;

    case tnsTimeshared:

      if (strstr(S, ND_timeshared))
        {
        Display = 1;
        }

      break;

    case tnsActive:   /* one or more jobs running on node */

      if (strstr(S, ND_busy) || strstr(S, ND_job_exclusive) || strstr(S, ND_job_sharing))
        {
        Display = 1;
        }

      break;

    case tnsAll:      /* list all nodes */

      Display = 1;

      break;

    case tnsUp:       /* node is healthy */

      if (!strstr(S, ND_down) && !strstr(S, ND_offline) && !strstr(S, ND_state_unknown))
        {
        Display = 1;
        }

      break;
    }  /* END switch (ListType) */

  return(Display);
  }





int main(

  int  argc,  /* I */
  char **argv)  /* I */

  {
  struct batch_status  *bstatus = NULL;
  int                   con;
  char                 *specified_server = NULL;
  int                   errflg = 0;
  int                   i;
  extern char          *optarg;
  extern int            optind;
  char                **pa;

  struct batch_status  *pbstat;
  int                   flag = ALLI;
  char                 *note = NULL;
  enum  note_flags      note_flag = unused;
  char                **nodeargs = NULL;
  int                   lindex;

  enum NStateEnum ListType = tnsNONE;

  /* get default server, may be changed by -s option */

  progname = strdup(argv[0]);

  while ((i = getopt(argc, argv, "acdlopqrs:x-:N:n")) != EOF)
    {
    switch (i)
      {
      case 'a':

        flag = ALLI;

        break;

      case 'c':

        flag = CLEAR;

        break;

      case 'd':

        flag = DIAG;

        break;

      case 'l':

        flag = LIST;

        break;

      case 'o':

        flag = OFFLINE;

        break;

      case 'p':

        flag = PURGE;

        break;

      case 'q':

        quiet = 1;

        break;

      case 'r':

        flag = RESET;

        break;

      case 's':

        specified_server = optarg;

        break;

      case 'x':

        flag = ALLI;

        DisplayXML = TRUE;

        break;

      case 'N':

        /* preserve any previous option other than the default,
         * to allow -N to be combined with -o, -c, etc
         */

        if (flag == ALLI)
          flag = NOTE;

        note = strdup(optarg);

        if (note == NULL)
          {
          perror("Error: strdup() returned NULL");

          exit(1);
          }

        note_flag = set;

        /* -N n is the same as -N ""  -- it clears the note */

        if (!strcmp(note, "n"))
          *note = '\0';

        if (strlen(note) > MAX_NOTE)
          {
          fprintf(stderr, "Warning: note exceeds length limit (%d) - server may reject it...\n",
            MAX_NOTE);
          }

        if (strchr(note, '\n') != NULL)
          fprintf(stderr, "Warning: note contains a newline - server may reject it...\n");

        break;

      case 'n':

        note_flag = list;

        break;

      case '-':

        if ((optarg != NULL) && !strcmp(optarg, "version"))
          {
          fprintf(stderr, "Version: %s\nRevision: %s\n",
            PACKAGE_VERSION, GIT_HASH);

          exit(0);
          }
        else if ((optarg != NULL) && !strcmp(optarg, "about"))
          {
          TShowAbout_exit();
          }

        errflg = 1;

        break;

      case '?':

      default:

        errflg = 1;

        break;
      }  /* END switch (i) */
    }    /* END while (i = getopt()) */

  if ((note_flag == list) && (flag != LIST))
    {
    fprintf(stderr, "Error: -n requires -l\n");
    errflg = 1;
    }

  for (pa = argv + optind;*pa;pa++)
    {
    if (strlen(*pa) == 0)
      {
      errflg = 1;
      }
    }

  if (errflg != 0)
    {
    if (!quiet)
      {
      fprintf(stderr, "usage:\t%s [-{c|d|l|o|p|r}] [-s server] [-n] [-N \"note\"] [-q] node ...\n",
              progname);

      fprintf(stderr, "\t%s [-{a|x}] [-s server] [-q] [node]\n",
              progname);
      }

    exit(1);
    }

  con = cnt2server(specified_server);

  if (con <= 0)
    {
    if (!quiet)
      {
      fprintf(stderr, "%s: cannot connect to server %s, error=%d (%s)\n",
        progname,
        (specified_server) ? specified_server : pbs_default(),
        con * -1,
        pbs_strerror(con * -1));
      }

    exit(1);
    }

  /* if flag is ALLI, LIST, get status of all nodes */

  if ((flag == ALLI) || (flag == LIST) || (flag == DIAG))
    {
    if ((flag == ALLI) || (flag == LIST) || (flag == DIAG))
      {
      if (flag == LIST)
        {
        /* allow state specification */

        if (argv[optind] != NULL)
          {

          for (lindex = 1;lindex < tnsLAST;lindex++)
            {
            if (!strcasecmp(NState[lindex], argv[optind]))
              {
              ListType = (enum NStateEnum)lindex;

              optind++;

              break;
              }
            }
          }
        }

      /* allow node specification (if none, then create an empty list) */

      if (argv[optind] != NULL)
        {
        nodeargs = argv + optind;
        }
      else
        {
        nodeargs = (char **)calloc(2, sizeof(char **));
        nodeargs[0] = strdup("");
        nodeargs[1] = '\0';
        }
      }
    }


  if ((note_flag == set) && (note != NULL))
    {
    /* set the note attrib string on specified nodes */

    for (pa = argv + optind;*pa;pa++)
      {
      set_note(con, *pa, note);
      }
    }

  switch (flag)
    {

    case DIAG:

      /* NYI */

      break;

    case CLEAR:

      /* clear  OFFLINE from specified nodes */

      for (pa = argv + optind;*pa;pa++)
        {
        marknode(con, *pa, ND_offline, DECR, NULL, DECR);
        }

      break;

    case RESET:

      /* clear OFFLINE, add DOWN to specified nodes */

      for (pa = argv + optind;*pa;pa++)
        {
        marknode(con, *pa, ND_offline, DECR, ND_down, INCR);
        }

      break;

    case OFFLINE:

      /* set OFFLINE on specified nodes */

      for (pa = argv + optind;*pa;pa++)
        {
        marknode(con, *pa, ND_offline, INCR, NULL, INCR);
        }

      break;

    case PURGE:

      /* remove node record */

      /* NYI */

      break;

    case ALLI:

      if (DisplayXML == TRUE)
        {

        char *tmpBuf = NULL, *tail = NULL;
        int  bufsize;

        mxml_t *DE;

        DE = NULL;

        MXMLCreateE(&DE, "Data");

        for (lindex = 0;nodeargs[lindex] != '\0';lindex++)
          {
          bstatus = statnode(con, nodeargs[lindex]);

          for (pbstat = bstatus;pbstat;pbstat = pbstat->next)
            {
            addxmlnode(DE, pbstat);
            }    /* END for (pbstat) */

          pbs_statfree(pbstat);
          }

        MXMLToXString(DE, &tmpBuf, &bufsize, INT_MAX, &tail, TRUE);

        MXMLDestroyE(&DE);

        fprintf(stdout, "%s\n",
                tmpBuf);
        }
      else
        {
        for (lindex = 0;nodeargs[lindex] != '\0';lindex++)
          {
          bstatus = statnode(con, nodeargs[lindex]);

          for (pbstat = bstatus;pbstat;pbstat = pbstat->next)
            {
            printf("%s\n",
                   pbstat->name);

            prt_node_attr(pbstat, 0);

            putchar('\n');
            }  /* END for (bpstat) */

          pbs_statfree(pbstat);
          }
        }

      break;

    case LIST:

      /* list any node that is DOWN, OFFLINE, or UNKNOWN */

      for (lindex = 0;nodeargs[lindex] != '\0';lindex++)
        {
        bstatus = statnode(con, nodeargs[lindex]);

        for (pbstat = bstatus;pbstat != NULL;pbstat = pbstat->next)
          {
          char *S;

          S = get_nstate(pbstat);

          if (filterbystate(pbstat, ListType, S))
            {
            char *n;

            if ((note_flag == list) && (n = get_note(pbstat)))
              {
              printf("%-20.20s %-26.26s %s\n",
                     pbstat->name,
                     S,
                     n);
              }
            else
              {
              printf("%-20.20s %s\n",
                     pbstat->name,
                     S);
              }
            }
          }

        pbs_statfree(pbstat);
        }

      break;
    }  /* END switch (flag) */

  pbs_disconnect(con);

  return(0);
  }  /* END main() */

/* END pbsnodes.c */

