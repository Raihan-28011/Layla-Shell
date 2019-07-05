/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2016, 2017, 2018, 2019 (c)
 * 
 *    file: bg.c
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

#include <wait.h>
#include <signal.h>
#include "../cmd.h"
#include "../debug.h"
#include "../symtab/symtab.h"

/* defined in jobs.c */
extern int cur_job ;
extern int prev_job;

#define UTILITY         "bg"

void __bg(struct job *job)
{
    /*
     * POSIX defines bg's output as:
     *     "[%d] %s\n", <job-number>, <command>
     * 
     */
    char current = (job->job_num == cur_job ) ? '+' :
                   (job->job_num == prev_job) ? '-' : ' ';
    printf("[%d]%c %s\r\n", job->job_num, current, job->commandstr);
    kill(-(job->pgid), SIGCONT);

    /* set the $! special parameter */
    struct symtab_entry_s *entry = add_to_symtab("!");
    char buf[12];
    sprintf(buf, "%d", job->pgid);
    symtab_entry_setval(entry, buf);
}


int bg(int argc, char **argv)
{
    if(!option_set('m'))
    {
        fprintf(stderr, "%s: Job control is not enabled\r\n", UTILITY);
        return 2;
    }
    
    int i;
    struct job *job;
    if(argc == 1)
    {
        job = get_job_by_jobid(get_jobid("%%"));
        if(job)
        {
            if(WIFSTOPPED(job->status)) __bg(job);
        }
        return 0;
    }
    
    /****************************
     * process the arguments
     ****************************/
    int v = 1, c;
    set_shell_varp("OPTIND", NULL);
    argi = 0;   /* args.c */
    while((c = parse_args(argc, argv, "hv", &v, 1)) > 0)
    {
        if     (c == 'h') { print_help(argv[0], REGULAR_BUILTIN_BG, 1, 0); }
        else if(c == 'v') { printf("%s", shell_ver); }
    }
    /* unknown option */
    if(c == -1) return 1;
    
    for(i = v; i < argc; i++)
    {
        job = get_job_by_jobid(get_jobid(argv[i]));
        if(!job)
        {
            fprintf(stderr, "%s: unknown job: %s\r\n", UTILITY, argv[i]);
            continue;
        }
        if(WIFSTOPPED(job->status))
        {
            __bg(job);
            prev_job = cur_job;
            cur_job  = job->job_num;
        }
    }
    return 0;
}
