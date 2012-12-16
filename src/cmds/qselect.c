#include "license_pbs.h" /* See here for the software license */
/*
 *
 * qselect - (PBS) select batch job
 *
 * Authors:
 *      Terry Heidelberg
 *      Livermore Computing
 *
 *      Bruce Kelly
 *      National Energy Research Supercomputer Center
 *
 *      Lawrence Livermore National Laboratory
 *      University of California
 */

#include "cmds.h"
#include "net_cache.h"
#include <pbs_config.h>   /* the master config generated by configure */


void
set_attrop(struct attropl **list, char *a_name, char *r_name, char *v_name, enum batch_op op)
  {

  struct attropl *attr;

  attr = (struct attropl *) calloc(1, sizeof(struct attropl));

  if (attr == NULL)
    {
    fprintf(stderr, "qselect: out of memory\n");
    exit(2);
    }

  if (a_name == NULL)
    attr->name = NULL;
  else
    {
    attr->name = (char *) calloc(1, strlen(a_name) + 1);

    if (attr->name == NULL)
      {
      fprintf(stderr, "qselect: out of memory\n");
      exit(2);
      }

    strcpy(attr->name, a_name);
    }

  if (r_name == NULL)
    attr->resource = NULL;
  else
    {
    attr->resource = (char *) calloc(1, strlen(r_name) + 1);

    if (attr->resource == NULL)
      {
      fprintf(stderr, "qselect: out of memory\n");
      exit(2);
      }

    strcpy(attr->resource, r_name);
    }

  if (v_name == NULL)
    attr->value = NULL;
  else
    {
    attr->value = (char *) calloc(1, strlen(v_name) + 1);

    if (attr->value == NULL)
      {
      fprintf(stderr, "qselect: out of memory\n");
      exit(2);
      }

    strcpy(attr->value, v_name);
    }

  attr->op = op;

  attr->next = *list;
  *list = attr;
  return;
  }



#define OPSTRING_LEN 4
#define OP_LEN 2
#define OP_ENUM_LEN 6
static const char *opstring_vals[] = { "eq", "ne", "ge", "gt", "le", "lt" };
static enum batch_op opstring_enums[] = { EQ, NE, GE, GT, LE, LT };



void
check_op(char *optarg, enum batch_op *op, char *optargout)
  {
  char opstring[OP_LEN+1];
  int i;
  int cp_pos;

  *op = EQ;   /* default */
  cp_pos = 0;

  if (optarg[0] == '.')
    {
    strncpy(opstring, &optarg[1], OP_LEN);
    opstring[OP_LEN] = '\0';
    cp_pos = OPSTRING_LEN;

    for (i = 0; i < OP_ENUM_LEN; i++)
      {
      if (strncmp(opstring, opstring_vals[i], OP_LEN) == 0)
        {
        *op = opstring_enums[i];
        break;
        }
      }
    }

  strcpy(optargout, &optarg[cp_pos]);

  return;
  }



int
check_res_op(char *optarg, char *resource_name, enum batch_op *op, char *resource_value, char **res_pos)
  {
  char opstring[OPSTRING_LEN];
  int i;
  int hit;
  char *p;

  p = strchr(optarg, '.');

  if (p == NULL || *p == '\0')
    {
    fprintf(stderr, "qselect: illegal -l value\n");
    fprintf(stderr, "resource_list: %s\n", optarg);
    return (1);
    }
  else
    {
    strncpy(resource_name, optarg, p - optarg);
    resource_name[p-optarg] = '\0';
    *res_pos = p + OPSTRING_LEN;
    }

  if (p[0] == '.')
    {
    strncpy(opstring, &p[1] , OP_LEN);
    opstring[OP_LEN] = '\0';
    hit = 0;

    for (i = 0; i < OP_ENUM_LEN; i++)
      {
      if (strncmp(opstring, opstring_vals[i], OP_LEN) == 0)
        {
        *op = opstring_enums[i];
        hit = 1;
        break;
        }
      }

    if (! hit)
      {
      fprintf(stderr, "qselect: illegal -l value\n");
      fprintf(stderr, "resource_list: %s\n", optarg);
      return (1);
      }
    }

  p = strchr(*res_pos, ',');

  if (p == NULL)
    {
    p = strchr(*res_pos, '\0');
    }

  strncpy(resource_value, *res_pos, p - (*res_pos));

  resource_value[p-(*res_pos)] = '\0';

  if (strlen(resource_value) == 0)
    {
    fprintf(stderr, "qselect: illegal -l value\n");
    fprintf(stderr, "resource_list: %s\n", optarg);
    return (1);
    }

  *res_pos = (*p == '\0') ? p : (p += 1) ;

  if (**res_pos == '\0' && *(p - 1) == ',')
    {
    fprintf(stderr, "qselect: illegal -l value\n");
    fprintf(stderr, "resource_list: %s\n", optarg);
    return (1);
    }

  return(0);  /* ok */
  }


/* qselect */

int main(

  int    argc,
  char **argv)

  {
  int c;
  int errflg = 0;
  int any_failed = 0;
  char *errmsg;

#define MAX_OPTARG_LEN 256
#define MAX_RESOURCE_NAME_LEN 256
  char optargout[MAX_OPTARG_LEN+1];
  char resource_name[MAX_RESOURCE_NAME_LEN+1];

  enum batch_op op;
  enum batch_op *pop = &op;

  struct attropl *select_list = 0;

  static char destination[PBS_MAXROUTEDEST+1] = "";
  char server_out[MAXSERVERNAME] = "";

  char *queue_name_out;
  char *server_name_out;

  int connect;
  char **selectjob_list;
  char *res_pos;
  char *pc;
  int u_cnt, o_cnt, s_cnt, n_cnt;
  time_t after;
  char a_value[80];
  int exec_only = 0;

  if (getenv("PBS_QSTAT_EXECONLY") != NULL)
    exec_only = 1;
  
  initialize_network_info();

#define GETOPT_ARGS "a:A:ec:h:l:N:p:q:r:s:u:"

  while ((c = getopt(argc, argv, GETOPT_ARGS)) != EOF)
    switch (c)
      {

      case 'a':
        check_op(optarg, pop, optargout);

        if ((after = cvtdate(optargout)) < 0)
          {
          fprintf(stderr, "qselect: illegal -a value\n");
          errflg++;
          break;
          }

        sprintf(a_value, "%ld", (long)after);

        set_attrop(&select_list, (char *)ATTR_a, NULL, a_value, op);
        break;

      case 'e':
        exec_only = 1;
        break;

      case 'c':
        check_op(optarg, pop, optargout);
        pc = optargout;

        while (isspace((int)*pc)) pc++;

        if (strlen(pc) == 0)
          {
          fprintf(stderr, "qselect: illegal -c value\n");
          errflg++;
          break;
          }

        if (strcmp(pc, "u") == 0)
          {
          if ((op != EQ) && (op != NE))
            {
            fprintf(stderr, "qselect: illegal -c value\n");
            errflg++;
            break;
            }
          }
        else if ((strcmp(pc, "n") != 0) &&
                 (strcmp(pc, "s") != 0) &&
                 (strcmp(pc, "c") != 0))
          {
          if (strncmp(pc, "c=", 2) != 0)
            {
            fprintf(stderr, "qselect: illegal -c value\n");
            errflg++;
            break;
            }

          pc += 2;

          if (strlen(pc) == 0)
            {
            fprintf(stderr, "qselect: illegal -c value\n");
            errflg++;
            break;
            }

          while (*pc != '\0')
            {
            if (!isdigit((int)*pc))
              {
              fprintf(stderr, "qselect: illegal -c value\n");
              errflg++;
              break;
              }

            pc++;
            }
          }

        set_attrop(&select_list, (char *)ATTR_c, NULL, optargout, op);

        break;

      case 'h':
        check_op(optarg, pop, optargout);
        pc = optargout;

        while (isspace((int)*pc)) pc++;

        if (strlen(pc) == 0)
          {
          fprintf(stderr, "qselect: illegal -h value\n");
          errflg++;
          break;
          }

        u_cnt = o_cnt = s_cnt = n_cnt = 0;

        while (*pc)
          {
          if (*pc == 'u')
            u_cnt++;
          else if (*pc == 'o')
            o_cnt++;
          else if (*pc == 's')
            s_cnt++;
          else if (*pc == 'n')
            n_cnt++;
          else
            {
            fprintf(stderr, "qselect: illegal -h value\n");
            errflg++;
            break;
            }

          pc++;
          }

        if (n_cnt && (u_cnt + o_cnt + s_cnt))
          {
          fprintf(stderr, "qselect: illegal -h value\n");
          errflg++;
          break;
          }

        set_attrop(&select_list, (char *)ATTR_h, NULL, optargout, op);

        break;

      case 'l':
        res_pos = optarg;

        while (*res_pos != '\0')
          {
          if (check_res_op(res_pos, resource_name, pop, optargout, &res_pos) != 0)
            {
            errflg++;
            break;
            }

          set_attrop(&select_list, (char *)ATTR_l, resource_name, optargout, op);
          }

        break;

      case 'p':
        check_op(optarg, pop, optargout);
        set_attrop(&select_list, (char *)ATTR_p, NULL, optargout, op);
        break;

      case 'q':
        strncpy(destination, optarg, PBS_MAXROUTEDEST);
        check_op(optarg, pop, optargout);
        set_attrop(&select_list, (char *)ATTR_q, NULL, optargout, op);
        break;

      case 'r':
        op = EQ;
        pc = optarg;

        while (isspace((int)(*pc))) pc++;

        if (strlen(pc) != 1)
          {
          fprintf(stderr, "qsub: illegal -r value\n");
          errflg++;
          break;
          }

        if (*pc != 'y' && *pc != 'n')
          {
          fprintf(stderr, "qsub: illegal -r value\n");
          errflg++;
          break;
          }

        set_attrop(&select_list, (char *)ATTR_r, NULL, pc, op);

        break;

      case 's':
        check_op(optarg, pop, optargout);
        pc = optargout;

        while (isspace((int)(*pc))) pc++;

        if (strlen(optarg) == 0)
          {
          fprintf(stderr, "qselect: illegal -s value\n");
          errflg++;
          break;
          }

        while (*pc)
          {
          if (*pc != 'C' && *pc != 'E' && *pc != 'H' && 
              *pc != 'Q' && *pc != 'R' && *pc != 'T' && 
              *pc != 'W')
            {
            fprintf(stderr, "qselect: illegal -s value\n");
            errflg++;
            break;
            }

          pc++;
          }

        set_attrop(&select_list, (char *)ATTR_state, NULL, optargout, op);

        break;

      case 'u':
        op = EQ;

        if (parse_at_list(optarg, FALSE, FALSE))
          {
          fprintf(stderr, "qselect: illegal -u value\n");
          errflg++;
          break;
          }

        set_attrop(&select_list, (char *)ATTR_u, NULL, optarg, op);

        break;

      case 'A':
        op = EQ;
        set_attrop(&select_list, (char *)ATTR_A, NULL, optarg, op);
        break;

      case 'N':
        op = EQ;
        set_attrop(&select_list, (char *)ATTR_N, NULL, optarg, op);
        break;

      default :
        errflg++;
      }

  if (errflg || (optind < argc))
    {
    static char usage[] = "usage: qselect \
                          [-a [op]date_time] [-A account_string] [-e] [-c [op]interval] \n\
                          [-h hold_list] [-l resource_list] [-N name] [-p [op]priority] \n\
                          [-q destination] [-r y|n] [-s states] [-u user_name]\n";
    fprintf(stderr,"%s", usage);
    exit(2);
    }

  if (notNULL(destination))
    {
    if (parse_destination_id(destination, &queue_name_out, &server_name_out))
      {
      fprintf(stderr, "qselect: illegally formed destination: %s\n", destination);
      exit(2);
      }
    else
      {
      if (notNULL(server_name_out))
        {
        strcpy(server_out, server_name_out);
        }
      }
    }

  connect = cnt2server(server_out);

  if (connect <= 0)
    {
    any_failed = -1 * connect;

    if (server_out[0] != 0)
      fprintf(stderr, "qselect: cannot connect to server %s (errno=%d) %s\n",
            server_out, any_failed, pbs_strerror(any_failed));
    else
      fprintf(stderr, "qselect: cannot connect to server %s (errno=%d) %s\n",
            pbs_server, any_failed, pbs_strerror(any_failed));

    exit(any_failed);
    }

  selectjob_list = pbs_selectjob_err(connect, select_list, exec_only ? (char *)EXECQUEONLY : NULL, &any_failed);

  if (selectjob_list == NULL)
    {
    if (any_failed != PBSE_NONE)
      {
      errmsg = pbs_geterrmsg(connect);

      if (errmsg != NULL)
        {
        fprintf(stderr, "qselect: %s\n", errmsg);
        }
      else
        {
        fprintf(stderr, "qselect: Error (%d - %s) selecting jobs\n",
                any_failed,
                pbs_strerror(any_failed));
        }

      exit(any_failed);
      }
    }
  else     /* got some jobs ids */
    {
    int i = 0;

    while (selectjob_list[i] != NULL)
      {
      printf("%s\n", selectjob_list[i++]);
      }

    free(selectjob_list);
    }

  pbs_disconnect(connect);

  exit(0);
  }
