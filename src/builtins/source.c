/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2019 (c)
 * 
 *    file: source.c
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "../cmd.h"

#define UTILITY         "source"


int source(int argc, char **argv)
{
    /****************************
     * process the arguments
     ****************************/
    int v = 1, c;
    set_shell_varp("OPTIND", NULL);
    argi = 0;   /* args.c */
    while((c = parse_args(argc, argv, "h:v", &v, 1)) > 0)
    {
        switch(c)
        {
            /*
             * in tcsh, the -h option causes commands to be loaded into the history
             * list, much like 'history -L'.
             */
            case 'h':
                if(!__optarg || __optarg == INVALID_OPTARG)
                {
                    fprintf(stderr, "%s: -%c option is missing arguments\r\n", UTILITY, 'h');
                    return 2;
                }
                char *argv2[] = { "history", "-L", __optarg, NULL };
                return history(3, argv2);
                
            case 'v':
                printf("%s", shell_ver);
                return 0;
        }
    }
    /* unknown option */
    if(c == -1) return 2;
    
    return dot(argc, argv);
}