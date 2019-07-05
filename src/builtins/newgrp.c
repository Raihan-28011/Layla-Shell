/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2016, 2017, 2018, 2019 (c)
 * 
 *    file: newgrp.c
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

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <termios.h>
#include "../cmd.h"
#include "../backend/backend.h"
#include "../debug.h"

#define UTILITY         "newgrp"

/* check if the given group id is part of the supplementary group ids */
static inline int gid_in_list(gid_t gid, gid_t *supp_groups, int supp_group_count)
{
    int i;
    for(i = 0; i < supp_group_count; i++)
        if(supp_groups[i] == gid) return 1;
    return 0;
}

#define ERROR(msg)                          \
do {                                        \
    fprintf(stderr, "%s: %s: %s\r\n",       \
            UTILITY, msg, strerror(errno)); \
    goto fin;                               \
} while(0);


int get_supp_groups(char *name, gid_t gid, gid_t **_supp_groups, int *_n)
{
    int n = 32;
    int len = n*sizeof(gid_t);
    gid_t *supp_groups;
    
get:
    supp_groups = malloc(len);
    if(!supp_groups) return 0;
    memset(supp_groups, 0, len);
    if(getgrouplist(name, gid, supp_groups, &n) < 0)
    {
        /* retry with a bigger buffer */
        free(supp_groups);
        len = n*sizeof(gid_t);
        goto get;
    }
    *_supp_groups = supp_groups;
    *_n = n;
    return 1;
}


gid_t *add_supp_group(gid_t *supp_groups, int supp_group_count, gid_t new_gid)
{
    gid_t *supp_groups2 = malloc((supp_group_count+1)*sizeof(gid_t));
    if(!supp_groups2)
    {
        free(supp_groups);
        return NULL;
    }
    memcpy(supp_groups2, supp_groups, supp_group_count*sizeof(gid_t));
    free(supp_groups);
    supp_groups = supp_groups2;
    supp_groups[supp_group_count] = new_gid;
    return supp_groups;
}


int newgrp(int argc, char **argv)
{
    int   req_login  = 0;
    gid_t new_gid    = 0;
    gid_t old_gid    = getgid();
    int   groups_max = sysconf(_SC_NGROUPS_MAX);
    if(groups_max <= 0) groups_max = 16;
    gid_t *supp_groups = NULL;
    /****************************
     * process the arguments
     ****************************/
    int v = 1, c;
    set_shell_varp("OPTIND", NULL);
    argi = 0;   /* args.c */
    while((c = parse_args(argc, argv, "hvl", &v, 1)) > 0)
    {
        switch(c)
        {
            case 'h':
                print_help(argv[0], REGULAR_BUILTIN_NEWGRP, 1, 0);
                return 0;
                
            case 'v':
                printf("%s", shell_ver);
                return 0;
                
            case 'l':
                req_login = 1;
                break;
        }
    }
    /* unknown option */
    if(c == -1) return 1;
    
    struct passwd *pw = getpwuid(getuid());
    if(v >= argc)
    {
        if(pw) new_gid = pw->pw_gid;
        else ERROR("error reading user info from user database");
        if(setegid(new_gid) < 0)
            ERROR("error setting EGID");
        int   n;
        gid_t *supp_groups;
        //if(getgrouplist(pw->pw_name, pw->pw_gid, supp_groups, &n) < 0)
        if(!get_supp_groups(pw->pw_name, pw->pw_gid, &supp_groups, &n))
            ERROR("error reading group ids from user database");
        if(setgroups(n, supp_groups) < 0)
            ERROR("error setting supplementary group ids");
        goto fin;
    }
    
    /* the passed argument can be a group name or gid */
    char *group = argv[v];
    struct group *grp = getgrnam(group);
    if(grp)
    {
        new_gid = grp->gr_gid;
    }
    else
    {
        new_gid = atoi(group);
        grp = getgrgid(new_gid);
        if(!grp)
            ERROR("error reading group info from user database");
    }
        
    if(pw)
    {
        int  found = 0;
        char **u   = grp->gr_mem;
        while(*u)
        {
            if(strcmp(*u, pw->pw_name) == 0)
            {
                found = 1;
                break;
            }
            u++;
        }
        if(!found)
        {
            /*
             * NOTE: POSIX says we should ask the user to enter the
             *       requested group's password.
             * TODO: implement this.
             *       See this link for info on authentication:
             *       https://stackoverflow.com/questions/10910193/how-to-authenticate-username-password-using-pam-w-o-root-privileges
             */
            /* check password */
            /*
            if(grp->gr_passwd)
            {
                FILE *tty = fopen("/dev/tty", "r");
                struct termios tm;
                tcgetattr(0, &tm);
                tm.c_lflag   &= ~(ECHO);
                tcsetattr(0, TCSANOW, &tm);
                char *pass = ...;
            }
            */
            fprintf(stderr, "%s: user %s is not a member of group %s",
                    UTILITY, pw->pw_name, grp->gr_name);
            goto fin;
        }
    }
    else ERROR("error reading user info from user database");

    /* the following convoluted code is modeled on the POSIX newgrp utility
     * description. see the link:
     *     http://pubs.opengroup.org/onlinepubs/9699919799/utilities/newgrp.html
     */
    int supp_group_count = getgroups(0, NULL);
    int len = supp_group_count*sizeof(gid_t);
    supp_groups = malloc(len);
    if(!supp_groups)
        ERROR("insufficient memory for supplementary group ids");
    if(getgroups(supp_group_count, supp_groups) < 0)
        ERROR("error reading supplementary group ids");
    
    if(gid_in_list(old_gid, supp_groups, supp_group_count))
    {
        if(!gid_in_list(new_gid, supp_groups, supp_group_count))
        {
            if(groups_max > 0 && supp_group_count < groups_max)
            {
                if((supp_groups = add_supp_group(supp_groups, supp_group_count, new_gid)))
                {
                    if(setgroups(supp_group_count+1, supp_groups) < 0)
                        ERROR("error adding new gid to supplementary group ids");
                }
                else ERROR("insufficient memory for supplementary group ids");
            }
        }
    }
    else
    {
        if(gid_in_list(new_gid, supp_groups, supp_group_count))
        {
            int i;
            for(i = 0; i < supp_group_count; i++)
            {
                if(supp_groups[i] == new_gid)
                {
                    for( ; i < supp_group_count-1; i++)
                    {
                        supp_groups[i] = supp_groups[i+1];
                    }
                    supp_groups[i] = 0;
                }
            }
            supp_group_count--;
            if(setgroups(supp_group_count, supp_groups) < 0)
                ERROR("error deleting supplementary group id");
        }
        if(!gid_in_list(old_gid, supp_groups, supp_group_count))
        {
            if(groups_max > 0 && supp_group_count < groups_max)
            {
                if((supp_groups = add_supp_group(supp_groups, supp_group_count, old_gid)))
                {
                    if(setgroups(supp_group_count+1, supp_groups) < 0)
                        ERROR("error adding new gid to supplementary group ids");
                }
                else ERROR("insufficient memory for supplementary group ids");
            }
        }
    }
    
    if(setregid(new_gid, new_gid) < 0) ERROR("error setting new group id");
    
    /* 
     * even if authorization failed, we should create a new 
     * execution enrivonment, as per POSIX.
     */
fin:
    //restore_std();
    if(supp_groups) free(supp_groups);

    struct symtab_entry_s *shell = get_symtab_entry("SHELL");
    char *shell_path = "/bin/sh";
    if(shell && shell->val) shell_path = shell->val;

    char *old_arg0 = shell_argv[0];
    if(req_login)
    {
        char *arg = malloc(strlen(shell_path)+2);
        if(!arg)
        {
            fprintf(stderr, "%s: failed to exec shell: %s\r\n", UTILITY, strerror(errno));
            return 3;
        }
        sprintf(arg, "-%s", shell_path);
        shell_argv[0] = arg;
        execvp(shell_path, shell_argv);
        fprintf(stderr, "%s: failed to exec shell: %s\r\n", UTILITY, strerror(errno));
        shell_argv[0] = old_arg0;
        return 3;
    }
    else
    {
        shell_argv[0] = shell_path;
        execvp(shell_path, shell_argv);
        fprintf(stderr, "%s: failed to exec shell: %s\r\n", UTILITY, strerror(errno));
        shell_argv[0] = old_arg0;
        return 3;
    }
}
