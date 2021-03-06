/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2019, 2020 (c)
 * 
 *    file: disown.c
 *    This file is part of the Layla Shell project.
 *
 *    Layla Shell is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    Layla Shell is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Layla Shell.  If not, see <http://www.gnu.org/licenses/>.
 */    

#include <stdio.h>
#include <stdlib.h>
#include "builtins.h"
#include "../cmd.h"
#include "../debug.h"

#define UTILITY             "disown"

/* defined in ../jobs.c */
extern struct job_s jobs_table[];

/* define below */
void disown_job(struct job_s *job, int nohup);


/*
 * The disown builtin utility (non-POSIX). Used to disown a job, so it is not sent
 * a SIGHUP when the shell exits.
 * 
 * Returns 0 on success, non-zero otherwise.
 * 
 * See the manpage for the list of options and an explanation of what each option does.
 * You can also run: `help disown` from lsh prompt to see a short
 * explanation on how to use this utility.
 */

int disown_builtin(int argc, char **argv)
{
    struct job_s *job;
    int v = 1, c;
    int all_jobs     = 0;
    int running_only = 0;
    int stopped_only = 0;
    int nohup        = 0;
    
    /****************************
     * process the options
     ****************************/
    while((c = parse_args(argc, argv, "ahrsv", &v, FLAG_ARGS_ERREXIT|FLAG_ARGS_PRINTERR)) > 0)
    {
        switch(c)
        {
            /* -h : keep job in the jobs list */
            case 'h':
                nohup = 1;
                break;
                
            /* -v : print shell version */
            case 'v':
                printf("%s", shell_ver);
                return 0;
                
            /* -a : disown all jobs */
            case 'a':
                all_jobs = 1;
                break;
                
            /* -r : disown only running jobs */
            case 'r':
                running_only = 1;
                break;
                
            /* -s : disown only stopped jobs */
            case 's':
                stopped_only = 1;
                break;
        }
    }
    
    /* unknown option */
    if(c == -1)
    {
        return 2;
    }
    
    /* 
     * ksh : if no job ids given, disown all jobs.
     * bash: if no job ids given, and no -a or -r supplied, disown 
     *       the current job.
     *
     * we follow bash here.
     */
    if(v >= argc)
    {
        /* use the current job */
        if(!all_jobs && !running_only && !stopped_only)
        {
            job = get_job_by_jobid(get_jobid("%%"));
            if(!job)
            {
                PRINT_ERROR("%s: unknown job: %%%%\n", UTILITY);
                return 1;
            }
            disown_job(job, nohup);
        }
        
        /* disown all jobs */
        for(job = &jobs_table[0]; job < &jobs_table[MAX_JOBS]; job++)
        {
            if(job->job_num != 0)
            {
                /* disown only running jobs */
                if(running_only && NOT_RUNNING(job->status))
                {
                    continue;
                }
                
                /* disown only stopped jobs */
                if(stopped_only && !WIFSTOPPED(job->status))
                {
                    continue;
                }
                disown_job(job, nohup);
            }
        }
        
        return 0;
    }
    
    /* process the arguments */
    for( ; v < argc; v++)
    {
        /* first try POSIX-style job ids */
        job = get_job_by_jobid(get_jobid(argv[v]));

        /* maybe we have a process pid? */
        if(!job)
        {
            char *strend;
            long pgid = strtol(argv[v], &strend, 10);
            if(strend != argv[v])
            {
                job = get_job_by_any_pid(pgid);
            }
        }

        /* still nothing? */
        if(!job)
        {
            PRINT_ERROR("%s: unknown job: %s\n", UTILITY, argv[v]);
            return 1;
        }
        
        /* disown only running jobs */
        if(running_only && NOT_RUNNING(job->status))
        {
            continue;
        }
        
        /* disown only stopped jobs */
        if(stopped_only && !WIFSTOPPED(job->status))
        {
            continue;
        }
        disown_job(job, nohup);
    }
    
    return 1;
}


/*
 * Disown the given job.
 */
void disown_job(struct job_s *job, int nohup)
{
    if(nohup)
    {
        /* don't remove the job, just mark it as disowned */
        job->flags |= JOB_FLAG_DISOWNED;
    }
    else
    {
        /* disown this b&^%$ */
        remove_job(job);
    }
}
