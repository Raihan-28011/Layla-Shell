/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2016, 2017, 2018, 2019 (c)
 * 
 *    file: initsh.c
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

/* macro definitions needed to use gethostname() and setenv() */
#define _XOPEN_SOURCE   500
#define _POSIX_C_SOURCE 200112L

/* 
 * we use the GNU version of basename() as it doesn't modify the
 * path variable passed to it.
 */
#define _GNU_SOURCE         /* basename() */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <fcntl.h>
// #include <libgen.h>         
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <linux/kd.h>
#include "early_environ.h"
#include "cmd.h"
#include "builtins/setx.h"
#include "kbdevent.h"
#include "debug.h"
#include "symtab/symtab.h"
#include "parser/parser.h"
#include "backend/backend.h"        /* match_filename() */

extern char **environ;

int   null_environ_index = 0;
char *rcfile = "~/.lshrc";
int   noprofile = 0;        /* do not load login scripts */
int   norc      = 0;        /* do not load rc scripts */

/* defined in main.c */
extern char  *cmdstr_filename;
extern struct source_s  __src;
extern int    read_stdin     ;


/*
 * POSIX says an interactive shell must read the ENV shell variable,
 * and if found, it must be expanded to an absolute pathname that
 * refers to a file containing shell commands to be executed in the
 * current shell environment.
 * bash only reads and executes $ENV if it is running in POSIX mode
 * (when invoked as sh, or --posix option was supplied). otherwise, 
 * it reads $BASH_ENV if the shell is non-interactive.
 * this function is not called if the shell is not interactive.
 */
int check_env_file()
{
    if((getuid() != geteuid()) || (getgid() != getegid()))
        return 0;
    struct symtab_entry_s *ENV = get_symtab_entry("ENV");
    if(!ENV || !ENV->val || !ENV->val[0]) return 0;
    if(!read_file(ENV->val, src))
    {
        fprintf(stderr, "%s: failed to read '%s': %s\r\n", SHELL_NAME, ENV->val, strerror(errno));
        return 0;
    }
    do_cmd();
    free(src->buffer);
    return 1;
}


void initsh(int argc, char **argv, int init_tty)
{
    size_t i;
    int path_max = sysconf(_PC_PATH_MAX);
    if(path_max <= 0) path_max = DEFAULT_PATH_MAX;
    
    /* init environ
     * 
     * NOTE: some environ vars are set by the shell, while others 
     *       are only set from the early environ if not already set.
     *       this way, we let the user customize our behavior by
     *       defining her own values and passing them to us when she
     *       invokes the shell.
     */
    uid_t euid = geteuid();
    gid_t egid = getegid();
    struct passwd *pw = getpwuid(euid);
    char buf[256];
    char *p = NULL, *e;
    struct symtab_entry_s *entry;
    int len = 0;
    
    /*
     * populate our global symbol table from the environment
     * variables list.
     */
    char **p2 = environ;
    while(*p2)
    {
        /* add environment variable to global symbol table     */
        /* POSIX says we should initialize shell vars from env */
        struct symtab_entry_s *entry = NULL;
        char *eq = strchr(*p2, '=');
        if(eq)
        {
            int len = eq-(*p2);
            char buf[len+1];
            strncpy(buf, *p2, len);
            buf[len] = '\0';
            //entry = add_to_symtab(buf);

            //symtab_entry_setval(entry, eq+1);

            /* parse functions that were passed to us in the environment */
            if(eq[1] == '(' && eq[2] == ')')
            {
                entry = add_func(buf);
                char *f = eq+3;
                while(*f && isspace(*f)) f++;
                if(!*f || *f == '}')
                {
                    /* empty function body */
                    //symtab_entry_setval(entry, eq+1);
                }
                else
                {
                    entry->val_type = SYM_FUNC;
#if 0
                    struct source_s save_src = __src;
                    src->filename = dot_filename;
                    src->buffer   = f;
                    src->bufsize  = strlen(f);
                    src->curpos   = -2;
                    struct token_s *tok = tokenize(src);
                    struct node_s *body = parse_function_body(tok);
                    if(body) entry->func_body = body;
                    entry->val_type = SYM_FUNC;
                    __src = save_src;
#endif
                }
            }
            else entry = add_to_symtab(buf);
            //else symtab_entry_setval(entry, eq+1);
            if(entry) symtab_entry_setval(entry, eq+1);
        }
        else
        {
            entry = add_to_symtab(*p2);
        }
        if(entry) entry->flags = FLAG_EXPORT;
        p2++;
    }

    /* now initialize some of the variables to our predefined values */
    for(i = 0; i < early_environ_length; p = NULL, i++)
    {
        char *name  = early_environ[i].name;
        int flags = FLAG_EXPORT;

        switch(i)
        {            
            case INDEX_HOME:
                /*
                 * $HOME should be set by the login utility, not us.
                 * we'll just try to set it in case it was not already set.
                 */
                e = getenv("HOME");
                if(!e || !*e)
                {
                    /* get and save the value of $HOME */
                    e = pw->pw_dir;
                    setenv(name, e, 1);
                }
                break;
                
            case INDEX_HOST:
            case INDEX_HOSTNAME:
                /* get and save the value of $HOST */
                buf[0] = '\0';
                if(gethostname(buf, MAXHOSTNAMELEN) != 0)
                    strcpy(buf, early_environ[i].value);
                setenv(name, buf, 1);
                e = buf;
                break;

            /* OLDPWD comes before PWD in our environment list. if OLDPWD
             * was set, use the same value for PWD. otherwise, fall through
             * to the OLDPWD case below.
             */
            case INDEX_PWD:
                if(cwd)
                {
                    e = cwd;
                    break;
                }
                
            case INDEX_OLDPWD:
                p = getcwd(NULL, 0);
                /* do POSIX-style on PWD */
                if(p && (*p == '/'))
                {
                    /*
                     * TODO: complete this loop to comply with POSIX
                     */
                    char *p2 = p;
                    while(*p2++)
                    {
                        /* we have "/." */
                        if(*p2 == '.' && p2[-1] == '/')
                        {
                            /* we have "/./" or "/." */
                            if(p2[1] == '/' || p2[1] == '\0') break;
                            /* we have "/../" or "/.." */
                            if(p2[1] == '.' && (p2[2] == '/' || p2[2] == '\0')) break;
                        }
                    }
                    if(strlen(p) < path_max)
                    {
                        early_environ[INDEX_PWD   ].value = p;
                        early_environ[INDEX_OLDPWD].value = p;
                    }
                }
                e = p;

                /* init cwd */
                len = strlen(p)+1;
                len = (path_max > len) ? path_max : len;
                cwd = (char *)malloc(len);
                if(!cwd)
                {
                    fprintf(stderr, "%s: FATAL ERROR: Insufficient memory for cwd string\r\n", SHELL_NAME);
                    exit(EXIT_FAILURE);
                }
                memset((void *)cwd, 0, len);
                strcpy(cwd, p);
                if(i == INDEX_OLDPWD) setenv("OLDPWD", cwd, 1);
                else                  setenv("PWD"   , cwd, 1);
                break;
                
            case INDEX_SHELL:
                /*
                 * $SHELL should be set by the login utility, not us.
                 * we'll just try to set it in case it was not already set.
                 */
                e = getenv("SHELL");
                if(!e || !*e)
                {
                    /*
                     * set $SHELL to the current user's login shell path. bash does
                     * this, ksh doesn't (it relies on 'login' setting $SHELL).
                     */
                    e = pw->pw_shell;
                    setenv(name, e, 1);
                }
                break;
                
            case INDEX_UID:
            case INDEX_EUID:
                /* save the value of UID */
                sprintf(buf, "%u", euid);
                setenv(name, buf, 1);
                e = buf;
                flags |= FLAG_READONLY;
                break;
                
            case INDEX_GID:
            case INDEX_EGID:
                /* save the value of GID */
                sprintf(buf, "%u", egid);
                setenv(name, buf, 1);
                e = buf;
                flags |= FLAG_READONLY;
                break;
                
            case INDEX_LOGNAME:
            case INDEX_USER:
                /* save the value of USER */
                p = pw->pw_name;
                setenv(name, p, 1);
                e = p;
                p = NULL;
                break;
                
                /*
                 * ksh gives default values to PS* variables, and so do we.
                 */
            case INDEX_PS1:
            case INDEX_PS2:
            case INDEX_PS3:
            case INDEX_PS4:
                setenv(name, early_environ[i].value, 1);
                e = early_environ[i].value;
                break;
                
            case INDEX_HISTFILE:
                p = getenv("HOME");
                if(!p) break;
                size_t  homelen = strlen(p);
                size_t  hist_file_len = strlen(hist_file);
                e = (char *)malloc(homelen+hist_file_len+2);
                if(!e)
                {
                    p = NULL;
                    break;
                }
                strcpy(e, p);
                if(p[homelen-1] != '/') strcat(e, "/");
                strcat(e, hist_file);
                setenv(name, e, 1);
                p = NULL;
                break;
                
            case INDEX_HISTSIZE:
                p = getenv("HISTSIZE");
                if(!p) break;
                int n = atoi(p);
                if(n <= 0 || n > MAX_CMD_HISTORY)
                    n = MAX_CMD_HISTORY;
                sprintf(buf, "%d", n);
                len = strlen(buf);
                e = (char *)malloc(len+1);
                if(!e)
                {
                    p = NULL;
                    break;
                }
                strcpy(e, buf);
                setenv(name, e, 1);
                p = NULL;
                break;
                
            case INDEX_PATH:
                if(option_set('r')) flags |= FLAG_READONLY;
                
            default:
                e = getenv(name);
                if(!e || !*e)
                {
                    if(early_environ[i].value[0])
                    {
                        e = early_environ[i].value;
                        setenv(name, e, 1);
                    }
                }
                /* set the last few entries to readonly status */
                if(i >= INDEX_MACHTYPE) flags |= FLAG_READONLY;
                break;
        }
        
        /* add environment variable to global symbol table     */
        /* POSIX says we should initialize shell vars from env */
        entry = add_to_symtab(name);
        if(entry)
        {
            if(e)
            {
                if(i == INDEX_PWD || i == INDEX_OLDPWD)
                     symtab_entry_setval(entry, __get_malloced_str(e));
                else symtab_entry_setval(entry, e);
            }
            entry->flags = flags;
        }
        if(p) free(p);
    }
    
    /*
     * bash ignores $SHELLOPTS, $BASHOPTS, $CDPATH and $GLOBIGNORE when running
     * without the privileged mode with an unequal uids or gids. we only ignore 
     * the variables we use in this shell.
     */
    uid_t ruid = getuid();
    gid_t rgid = getgid();
    if(!option_set('p') && (euid != ruid || egid != rgid))
    //if(option_set('p'))
    {
        entry = get_symtab_entry("CDPATH");
        if(entry) symtab_entry_setval(entry, NULL);
        entry = get_symtab_entry("GLOBIGNORE");
        if(entry) symtab_entry_setval(entry, NULL);
        entry = get_symtab_entry("FIGNORE");
        if(entry) symtab_entry_setval(entry, NULL);
        entry = get_symtab_entry("SHELLOPTS");
        if(entry) symtab_entry_setval(entry, NULL);
    }

    /* init shell variables */
    init_shell_vars(pw->pw_name, pw->pw_gid, argv[0]);
  
#if 0
    /* init aliases */
    memset((void *)__aliases, 0, sizeof(__aliases));

    /* init history */
    init_history();
    
    /* init our internal clock */
    start_clock();
#endif
    
    if(!init_tty) return;

    /* make sure our process group id is the same as our pid, and that the shell
     * knows we are the forground process.
     */
    setpgid(0, tty_pid);
    tcsetpgrp(0, tty_pid);
    
    /* get screen size */
    if(!get_screen_size())
    {
        fprintf(stderr, "%s: ERROR: Failed to read screen size\r\n", SHELL_NAME);
        fprintf(stderr, "       Assuming 80x25\r\n");
        /* update the value of terminal columns in the symbol table */
        VGA_WIDTH = 0;
        entry = get_symtab_entry("COLUMNS");
        if(entry)
        {
            if(entry->val) VGA_WIDTH = atoi(entry->val);
        }
        else entry = add_to_symtab("COLUMNS");
        if(VGA_WIDTH <= 0)
        {
            VGA_WIDTH = 80;
            symtab_entry_setval(entry, "80");
        }
        /* update the value of terminal rows in the symbol table */
        VGA_HEIGHT = 0;
        entry = get_symtab_entry("LINES");
        if(entry)
        {
            if(entry->val) VGA_HEIGHT = atoi(entry->val);
        }
        else entry = add_to_symtab("LINES");
        if(VGA_HEIGHT <= 0)
        {
            VGA_HEIGHT = 25;
            symtab_entry_setval(entry, "25");
        }
    }

    /* check we are on a terminal device */
    if (isatty(2) == 0)
    {
        fprintf(stderr, "%s: not running in a terminal.\r\n", SHELL_NAME);
        exit(EXIT_FAILURE);
    }

    /* init terminal device to raw mode */
    if(!rawon())
    {
        fprintf(stderr, "%s: FATAL ERROR: Failed to set terminal attributes (errno = %d)\r\n", SHELL_NAME, errno);
        exit(EXIT_FAILURE);
    }

    setbuf(stdout, NULL);
    ALT_MASK   = 0; 
    CTRL_MASK  = 0; 
    SHIFT_MASK = 0;
}

/*
 * If this is a login shell, read and parse /etc/profile
 * and then ~/.profile.
 */
void init_login()
{
    if(noprofile) return;       /* bash extension */
    /*
     * we are called before initsh() gets to set terminal raw mode.
     * so at least get the current termios struct so that we don't
     * mess up the terminal while we run them startup scripts.
     */
    extern struct termios tty_attr_old;
    if(tcgetattr(0, &tty_attr_old) == -1) return;

    /* read global init script */
    if(read_file("/etc/profile", src))
    {
        do_cmd();
        free(src->buffer);
    }
    /* then the local init script, if any */
    if(read_file(".profile", src))
    {
        do_cmd();
        free(src->buffer);
    }
    else
    {
        /* ksh disables processing of ~/.profile in the privileged mode */
        if(!option_set('p') && read_file("~/.profile", src))
        {
            do_cmd();
            free(src->buffer);
        }
    }
}

/*
 * similar to init_login(), except is invoked for interactive shells only.
 */
void init_rc()
{
    /* read global init script */
    if(read_file("/etc/lshrc", src))
    {
        do_cmd();
        free(src->buffer);
    }
    if(!option_set('p'))
    {
        if(!check_env_file())
        {
            /* similar to what ksh does for login shells -- see init_login() */
            if(!norc && read_file(rcfile, src))
            {
                do_cmd();
                free(src->buffer);
            }
        }
    }
}

/*
 * Parse command-line arguments and set the shell
 * options accordingly.
 */
char parse_options(int argc, char **argv)
{
    /* reset all options */
    reset_options();

    /* then set some default options */
    set_option('h', 1);     /* hashing */
    set_option('m', 1);     /* job control */
    set_option('H', 1);     /* history expansion */
    set_option('w', 1);     /* history facilities */
    set_option('B', 1);     /* brace expansion */
    /* auto-update $LINES AND $COLUMNS */
    set_optionx(OPTION_CHECK_WINSIZE       , 1);
    /* auto-save multiline commands as single liners */
    set_optionx(OPTION_CMD_HIST            , 1);
    /* escape special chars in filenames during auto-completion */
    set_optionx(OPTION_COMPLETE_FULL_QUOTE , 1);
    /* expand aliases */
    set_optionx(OPTION_EXPAND_ALIASES      , 1);
    /* force the use of $FIGNORE */
    set_optionx(OPTION_FORCE_FIGNORE       , 1);
    /* complete hostnames during auto-completion */
    set_optionx(OPTION_HOST_COMPLETE       , 1);
    /* complete user names during auto-completion */
    set_optionx(OPTION_USER_COMPLETE       , 1);
    /* don't reset -e option in subshells */
    set_optionx(OPTION_INHERIT_ERREXIT     , 1);
    /* #-words begin comments in interactive shells */
    set_optionx(OPTION_INTERACTIVE_COMMENTS, 1);
    /* word-expansion on PS strings */
    set_optionx(OPTION_PROMPT_VARS         , 1);
    /* let source (or dot) use $PATH to find scripts */
    set_optionx(OPTION_SOURCE_PATH         , 1);
    /* output shift builtin errors */
    set_optionx(OPTION_SHIFT_VERBOSE       , 1);
    /* clear the screen on startup */
    set_optionx(OPTION_CLEAR_SCREEN        , 1);
    /* automatically add '/' and ' ' as suffix when doing filename completions */
    set_optionx(OPTION_ADD_SUFFIX          , 1);
    /* recognize only executables during filename completion */
    set_optionx(OPTION_RECOGNIZE_ONLY_EXE  , 1);
    /* automatically save history on exit */
    set_optionx(OPTION_SAVE_HIST           , 1);
    
    /* now read command-line options */
    struct symtab_entry_s *entry;
    int    i             = 1;
    int    expect_cmdstr = 0;
    int    param         = 0;
    char   islogin       = 0;
    char   end_loop      = 0;

    if(argv[0][0] == '-') islogin = 1;
    
    if(argc == 1)
    {
        /* $0 is the name of the shell or shell script */
        entry = add_to_symtab("0");
        symtab_entry_setval(entry, argv[0]);
        entry = add_to_symtab("#");
        symtab_entry_setval(entry, "0");
        if(isatty(0) && isatty(2))
        {
            set_option('i', 1);     /* set interactive shell */
            read_stdin    = 1;
        }
        goto init;
    }
    
    if(strcmp(argv[1], "-c") == 0)
    {
        i++;
        expect_cmdstr = 1;
        read_stdin    = 0;
        end_loop      = 1;
        set_option('c', 1);
    }
    else if(strcmp(argv[1], "-s") == 0)
    {
        i++;
        expect_cmdstr = 0;
        read_stdin    = 1;
        set_option('s', 1);
    }

    for( ; i < argc; i++)
    {
        int skip;
        char *arg = argv[i];
        switch(arg[0])
        {
            case '-':
                if(arg[1] == '\0')
                {
                    end_loop = 1;
                    i++;
                    break;
                }
                if(strcmp(arg, "--") == 0)
                {
                    end_loop = 1;
                    i++;
                    break;
                }
                if(strcmp(arg, "--dirsfile") == 0 || strcmp(arg, "--dirs-file") == 0) /* bash */
                {
                    read_dirsfile = 1;
                    char *file = argv[i+1];
                    struct symtab_entry_s *entry = add_to_symtab("DIRSFILE");
                    if(file == NULL || *file == '-')       /* without argument, read the default file */
                    {
                        symtab_entry_setval(entry, DIRSTACK_FILE);
                    }
                    else
                    {
                        symtab_entry_setval(entry, file);
                        i++;
                    }
                    break;
                }
                if(strcmp(arg, "--help") == 0)                                          /* bash, csh */
                {
                    help(1, (char *[]){ "help", NULL });
                    exit_gracefully(EXIT_SUCCESS, NULL);
                }
                if(strcmp(arg, "--init-file") == 0 || strcmp(arg, "--rcfile") == 0) /* bash */
                {
                    if(argv[++i] == NULL)
                    {
                        fprintf(stderr, "%s: missing argument: init/rc file name\n", SHELL_NAME);
                        exit_gracefully(EXIT_FAILURE, NULL);
                    }
                    rcfile = argv[i];
                    break;
                }
                if(strcmp(arg, "--login") == 0 || strcmp(arg, "-l") == 0)           /* bash, csh */
                {
                    islogin = 1;
                    break;
                }
                if(strcmp(arg, "--noprofile") == 0)                                     /* bash */
                {
                    noprofile = 1;
                    break;
                }
                if(strcmp(arg, "--norc") == 0)                                          /* bash */
                {
                    norc = 1;
                    break;
                }
                if(strcmp(arg, "--restricted") == 0)                                    /* bash */
                {
                    set_option('r', 1);
                    set_optionx(OPTION_RESTRICTED_SHELL, 1);
                    break;
                }
                if(strcmp(arg, "--verbose") == 0)                                       /* bash */
                {
                    set_option('v', 1);
                    break;
                }
                if(strcmp(arg, "--version") == 0)                                       /* bash, csh */
                {
                    printf("%s on %s\r\n", OS_TYPE, CPU_ARCH);
                    exit_gracefully(EXIT_SUCCESS, NULL);
                }
                /* fall through to the next case */
                
            case '+':
                if(strcmp(arg, "+O") == 0 || strcmp(arg, "-O") == 0)                    /* bash */
                {
                    /* setx extended options (similar to bash's shopt utility options) */
                    __int64_t c;
                    arg = argv[++i];
                    if(!arg)
                    {
                        purge_xoptions('a', arg[0] == '-' ? 0 : 1);
                        break;
                    }
                    if((c = optionx_index(arg)) < 0)
                    {
                        fprintf(stderr, "%s: invalid option: %s\r\n", SHELL_NAME, arg);
                        break;
                    }
                    if(!set_optionx(c, arg[0] == '-' ? 1 : 0))
                    {
                        fprintf(stderr, "%s: error setting: %s\r\n", SHELL_NAME, arg);
                    }
                    break;
                }
                /* normal, set-like options */
                skip = do_options(argv[i], argv[i+1]);
                if(skip < 0) exit(EXIT_FAILURE);
                i += skip;
                break;
                
            default:
                end_loop = 1;
                break;
        }
        if(end_loop) break;
    }
    //debug ("i = %d\r\n", i);

    if(expect_cmdstr)
    {
        if(i >= argc)
        {
            fprintf(stderr, "%s: missing command string\r\n", SHELL_NAME);
            exit(EXIT_FAILURE);
        }
        /* is it an empty string? exit 0 as per POSIX */
        if(argv[i][0] == '\0') exit(EXIT_SUCCESS);
        /* otherwise, read it */
        __src.buffer   = argv[i++];
        __src.bufsize  = strlen(__src.buffer);
        __src.filename = cmdstr_filename;
        __src.curpos   = -2;
        if(i >= argc)
        {
            /* $0 is the name of the shell or shell script */
            entry = add_to_symtab("0");
            symtab_entry_setval(entry, argv[0]);
        }
        else
        {
            entry = add_to_symtab("0");
            symtab_entry_setval(entry, argv[i++]);
        }
        /* similar to $BASH_EXECUTION_STRING (bash) and $command (tcsh) */
        entry = add_to_symtab("COMMAND_STRING");
        symtab_entry_setval(entry, __src.buffer);
    }
    else
    {
        if(i >= argc || option_set('s'))
        {
            read_stdin = 1;
            /* $0 is the name of the shell or shell script */
            entry = add_to_symtab("0");
            symtab_entry_setval(entry, argv[0]);
        }
        else
        {
            read_stdin = 0;
            char *cmdfile = argv[i++];
            if(!read_file(cmdfile, src))
            {
                fprintf(stderr, "%s: failed to read '%s': %s\r\n", SHELL_NAME,
                        cmdfile, strerror(errno));
                exit(EXIT_ERROR_NOENT);
            }
            /* enfore non-interactive mode */
            set_option('i', 0);
            set_option('m', 0);
            /* $0 is the name of the shell or shell script */
            entry = add_to_symtab("0");
            symtab_entry_setval(entry, cmdfile);
        }
    }

    char buf[32];
    for( ; i < argc; i++)
    {
        sprintf(buf, "%d", ++param);
        entry = add_to_symtab(buf);
        symtab_entry_setval(entry, argv[i]);
    }
    entry = add_to_symtab("#");
    sprintf(buf, "%d", param);
    symtab_entry_setval(entry, buf);

init:
    if (option_set('s') && !option_set('i')) set_option('i', 1);
    if (option_set('i') && !option_set('s')) set_option('s', 1);
    if(!option_set('i') && !option_set('c') && read_stdin)
    //if(param == 0)
    {
        if(isatty(0) && isatty(2))
        {
             set_option('i', 1);     /* set interactive shell */
        }
        else set_option('i', 0);     /* set non-interactive shell */
    }
    
    /* if not an interactive shell ... */
    if(!option_set('i'))
    {
        set_option('m', 0);
        set_option('H', 0);
        set_option('w', 0);
        set_optionx(OPTION_CHECK_WINSIZE       , 0);
        set_optionx(OPTION_COMPLETE_FULL_QUOTE , 0);
        set_optionx(OPTION_EXPAND_ALIASES      , 0);
        set_optionx(OPTION_INTERACTIVE_COMMENTS, 0);
    }
    
    /* is it a restricted shell? the basename of $SHELL determines this */
    entry = get_symtab_entry("SHELL");
    if(entry)
    {
        char *b = basename(entry->val);
        if(strcmp(b, "rsh") == 0 || strcmp(b, "rlsh") == 0 || strcmp(b, "lrsh") == 0)
        {
            set_option('r', 1);
            set_optionx(OPTION_RESTRICTED_SHELL, 1);
        }
    }
    
    /* check the $VISUAL editor */
    entry = get_symtab_entry("VISUAL");
    if(!entry) entry = get_symtab_entry("EDITOR");
    if(entry)
    {
        if(match_filename("*[Vv][Ii]*", entry->val, 0, 0))
            set_option('y', 1);
        /*
         * TODO: check the *macs* pattern for emacs-style editing.
         */
    }
    
    /* save the option flags to the symbol table */
    symtab_save_options();
    return islogin;
}