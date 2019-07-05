/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2019 (c)
 * 
 *    file: hup.c
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include "../cmd.h"
#include "../backend/backend.h"
#include "../debug.h"


/*
 * the hup/nohup utilities are tcsh non-POSIX extensions. bash doesn't have them.
 * nohup is part of the GNU coreutils package, not the shell itself.
 * this function does the role of both utilities, depending on the name it is called
 * with (hup or nohup, that is the question).
 */

int hup(int argc, char **argv)
{
    int i;
    int hup = strcmp(argv[0], "hup") == 0 ? 1 : 0;
    char *UTILITY = hup ? "hup" : "nohup";

    for(i = 1; i < argc; i++)
    {
        char *arg = argv[i];
        if(*arg == '-')
        {
            char *p = arg;
            if(strcmp(p, "-") == 0 || strcmp(p, "--") == 0) { i++; break; }
            p++;
            while(*p)
            {
                switch(*p)
                {
                    case 'h':
                        print_help(argv[0], hup ? REGULAR_BUILTIN_HUP : REGULAR_BUILTIN_NOHUP, 1, 0);
                        return 0;
                        
                    case 'v':
                        printf("%s", shell_ver);
                        return 0;
                        
                    default:
                        fprintf(stderr, "%s: invalid option: -%c\n", UTILITY, *p);
                        return 2;
                }
                p++;
            }
        }
        else break;
    }

    if(i >= argc)
    {
        fprintf(stderr, "%s: missing argument: command name\r\n", UTILITY);
        return 2;
    }
    
    int    cargc = argc-i;
    char **cargv = &argv[i];
    return fork_command(cargc, cargv, NULL, UTILITY, hup ? 0 : FORK_COMMAND_IGNORE_HUP, 0);
}