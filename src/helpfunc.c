/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2016, 2017, 2018, 2019 (c)
 * 
 *    file: helpfunc.c
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
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/kd.h>
#include "cmd.h"
#include "backend/backend.h"
#include "debug.h"
#include "kbdevent.h"

/********************************************************************
 * 
 * This file contains helper functions for other parts of the shell.
 * 
 ********************************************************************/

/****************************************
 * 1 - Terminal control/status functions
 ****************************************/

/* declared in kbdevent2.c */
extern struct termios tty_attr_old;
extern struct termios tty_attr;
/* declared in initsh.c */
//extern int            old_keyboard_mode;

void term_canon(int on)
{
    if(!isatty(0)) return;
    if(on)
    {
        tcsetattr(0, TCSANOW, &tty_attr_old);
        //// ioctl(0, KDSKBMODE, old_keyboard_mode);
    }
    else
    {
        tcsetattr(0, TCSANOW, &tty_attr);
        //// ioctl(0, KDSKBMODE, K_RAW);
    }
}

/* special struct termios for turning echo on/off */
struct termios echo_tty_attr;

int echoon(int fd)
{
    if(!isatty(fd)) return 0;
    if(tcgetattr(fd, &echo_tty_attr) == -1) return 0;
    echo_tty_attr.c_lflag |= ECHO;
    if((tcsetattr(fd, TCSAFLUSH, &echo_tty_attr) == -1)) return 0;
    return 1;
}

int echooff(int fd)
{
    if(!isatty(fd)) return 0;
    if(tcgetattr(fd, &echo_tty_attr) == -1) return 0;
    echo_tty_attr.c_lflag &= ~ECHO;
    if((tcsetattr(fd, TCSAFLUSH, &echo_tty_attr) == -1)) return 0;
    return 1;
}

/* well. duh! */
int get_screen_size()
{
    struct winsize w;
    /* find the size of the view */
    int fd = isatty(0) ? 0 : isatty(2) ? 2 : -1;
    if(fd == -1) return 0;
    int res = ioctl(fd, TIOCGWINSZ, &w);
    if(res != 0) return 0;
    VGA_HEIGHT = w.ws_row;
    VGA_WIDTH  = w.ws_col;
    /* update the value of terminal columns in environ and in the symbol table */
    char buf[32];
    struct symtab_entry_s *e = get_symtab_entry("COLUMNS");
    if(!e) e = add_to_symtab("COLUMNS");
    if(e && sprintf(buf, "%d", VGA_WIDTH)) symtab_entry_setval(e, buf);
    /* update the value of terminal rows in environ and in the symbol table */
    e = get_symtab_entry("LINES");
    if(!e) e = add_to_symtab("LINES");
    if(e && sprintf(buf, "%d", VGA_HEIGHT)) symtab_entry_setval(e, buf);
    return 1;
}

void move_cur(int row, int col)
{
    fprintf(stdout, "\e[%d;%dH", row, col);
    fflush(stdout);
}

void clear_screen()
{
    fprintf(stdout, "\e[2J");
    fprintf(stdout, "\e[0m");
    fprintf(stdout, "\e[3J\e[1;1H");
}

void set_terminal_color(int FG, int BG)
{
    /*control sequence to set screen color */
    fprintf(stdout, "\x1b[%d;%dm", FG, BG);
}

void update_row_col()
{
    if(feof(stdin)) clearerr(stdin);
    /* 
     * we will temporarily block SIGCHLD so it won't disturb us between our cursor 
     * position request and the terminal's response.
     */
    sigset_t intmask;
    sigemptyset(&intmask);
    sigaddset(&intmask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &intmask, NULL);
    /* request terminal cursor position report (CPR) */
// loop:
    fprintf(stdout, "\x1b[6n");
    /* read the result. it will be reported as:
     *     ESC [ y; x R
     */
    terminal_col = 0;
    terminal_row = 0;
    int c;
    int *in = &terminal_row;
    int delim = ';';
    term_canon(0);
    while((c = getchar()) != EOF)
    {
        if(c == 27 ) continue;
        if(c == '[') continue;
        *in = c-'0';
        while((c = getchar()) != delim)
        {
            *in = ((*in) * 10) + (c-'0');
        }
        if(delim == ';')
        {
            in = &terminal_col;
            delim = 'R';
        }
        else break;
    }
    /* get the trailing 'R' if its still there */
    while(c != EOF && c != 'R') c = getchar();
    sigprocmask(SIG_UNBLOCK, &intmask, NULL);
}

int get_terminal_row()
{
    return terminal_row;
}

int set_terminal_row(int row)
{
    int diff = row-terminal_row;
    if(diff < 0) fprintf(stdout, "\x1b[%dA", -diff);
    else         fprintf(stdout, "\x1b[%dB",  diff);
    return row;
}

int get_terminal_col()
{
    return terminal_col;
}

int set_terminal_col(int col)
{
    fprintf(stdout, "\x1b[%dG", col);
    return col;
}


int beep()
{
    /* in tcsh, special alias beepcmd is run when the shell wants to ring the bell */
    run_alias_cmd("beepcmd");
    putchar('\a');
    return 1;
}


/****************************************
 * 2 - String manipulation functions
 ****************************************/

/* convert string to uppercase */
int strupper(char *str)
{
    if(!str) return 0;
    while(*str)
    {
        //if(*str >= 'a' && *str <= 'z') *str = (*str)-32;
        *str = toupper(*str);
        str++;
    }
    return 1;
}

/* convert string to lowercase */
int strlower(char *str)
{
    if(!str) return 0;
    while(*str)
    {
        //if(*str >= 'A' && *str <= 'Z') *str = (*str)+32;
        *str = tolower(*str);
        str++;
    }
    return 1;
}

/* quick converstion from integer to string */
const char *_itoa_digits = "0123456789";
void _itoa(char *str, int num)
{
    size_t res = 1, i;
    uintmax_t copy = num;
    while(10 <= copy) copy /= 10, res++;
    str[res] = '\0';
    for(i = res; i != 0; i--)
    {
        str[i-1] = _itoa_digits[num % 10];
        num /= 10;
    }
}

/* append a character to a string */
inline void strcat_c(char *str, int pos, char chr)
{
    str[pos++] = chr;
    str[pos  ] = '\0';
}

size_t remove_escaped_newlines(char *buf)
{
    size_t len = strlen(buf);
    if(!len) return 0;
    size_t i = 0;
    while(i < len)
    {
        if(buf[i] == '\\' && buf[i+1] == '\n')
            delete_char_at(buf, i);
        /*
        {
            size_t j;
            for(j = i; j < len-1; j++) buf[j] = buf[j+2];
            len -= 2;
        }
        */
        else i++;
    }
    return len;
}

/*
 * search string for any one of the passed characters.
 * returns a char pointer to the first occurence of the
 * character.
 */
char *strchr_any(char *string, char *chars)
{
    if(!string || !chars) return NULL;
    char *s = string;
    while(*s)
    {
        char *c = chars;
        while(*c)
        {
            if(*s == *c) return s;
            c++;
        }
        s++;
    }
    return NULL;
}

/*
 * returns if two strings are the same.
 */
int is_same_str(char *s1, char *s2)
{
    if((strlen(s1) == strlen(s2)) && strcmp(s1, s2) == 0)
        return 1;
    return 0;
}

/*
 * return the passed string value, quoted in a format that can
 * be used for reinput to the shell.
 */
char *quote_val(char *val)
{
    /* calc needed space */
    int len = 2;    /* for the quotes */
    char *tmp = val;
    while(*tmp)
    {
        switch(*tmp)
        {
            case '\\':
            case  '`':
            case  '$':
            case  '"':
                len++;

            default:
                len++;
                break;
        }
        tmp++;
    }
    /* alloc space */
    char *sub = malloc(len+1);
    if(!sub) return NULL;
    char *s2 = sub;
    *s2++ = '"';
    tmp = val;
    /* now copy the string, escaped */
    while(*tmp)
    {
        switch(*tmp)
        {
            case '\\':
            case  '`':
            case  '$':
            case  '"':
                *s2++ = '\\';

            default:
                *s2++ = *tmp;
                break;
        }
        tmp++;
    }
    *s2++ = '"' ;
    *s2   = '\0';
    return sub;
}

/*
 * converts a string array to a single string with members
 * separated by spaces. last member must be NULL, or else we
 * will loop until we SIGSEGV! a non-zero dofree argument
 * causes the passed list to be freed (if the caller is too 
 * lazy to free its own memory).
 */
char *list_to_str(char **list, int dofree)
{
    if(!list) return NULL;
    int i;
    int len = 0;
    int count = 0;
    char *p, *p2;
    /* get the count */
    for(i = 0; list[i]; i++) ;
    count = i;
    int lens[count];
    /* get total length and each item's length */
    for(i = 0; i < count; i++)
    {
        /* add 1 for the separating space char */
        lens[i] = strlen(list[i])+1;
        len += lens[i];
    }
    p = malloc(len+1);
    if(!p) return NULL;
    *p = 0;
    p2 = p;
    /* now copy the items */
    for(i = 0; i < count; i++)
    {
        sprintf(p, "%s ", list[i]);
        p += lens[i];
    }
    p[-1]= '\0';
    /* free the original list */
    if(dofree)
    {
        for(i = 0; i < count; i++) free(list[i]);
        free(list);
    }
    return p2;
}

int get_linemax()
{
    int line_max;
#ifdef LINE_MAX
    line_max = LINE_MAX;
#else
    line_max = sysconf(_SC_LINE_MAX);
    if(line_max <= 0) line_max = DEFAULT_LINE_MAX;
#endif
    return line_max;
}


/****************************************
 * 3 - Miscellaneous functions
 ****************************************/
int isroot()
{
    static uid_t uid = -1;
    static char  gotuid = 0;
    if(!gotuid)
    {
        uid = geteuid();
        gotuid = 1;
    }
    return (uid == 0);
}

struct cmd_token *make_cmd_token(char *word)
{
    struct cmd_token *cmdtok = (struct cmd_token *)malloc(sizeof(struct cmd_token));
    if(!cmdtok) return NULL;
    size_t  len  = strlen(word);
    char   *data = (char *)malloc(len+1);
    if(!data)
    {
        free(cmdtok);
        return NULL;
    }
    strcpy(data, word);
    cmdtok->data = data;
    cmdtok->len  = len;
    cmdtok->next = NULL;
    return cmdtok;
}


char *search_path(char *file, char *use_path, int exe_only)
{
    /* bash extension for ignored executable files */
    char *EXECIGNORE = get_shell_varp("EXECIGNORE", NULL);

    char *PATH = use_path ? use_path : getenv("PATH");
    char *p    = PATH;
    char *p2;
    
check:
    /* $PATH finished */
    if(!p || *p == '\0')
    {
        errno = ENOENT;
        return 0;
    }
    p2 = p;
    while(*p2 && *p2 != ':') p2++;
    int  plen = p2-p;
    if(!plen) plen = 1;
    int  alen = strlen(file);
    char path[plen+1+alen+1];
    strncpy(path, p, p2-p);
    path[p2-p] = '\0';
    if(p2[-1] != '/') strcat(path, "/");
    strcat(path, file);
    struct stat st;
    if(stat(path, &st) == 0)
    {
        if(!S_ISREG(st.st_mode))
        {
            errno = ENOENT;
            goto next;
        }
        if(exe_only)
        {
            if(access(path, X_OK) != 0)
            {
                errno = ENOEXEC;
                goto next;
            }
        }
        if(EXECIGNORE)
        {
            if(!match_ignore(EXECIGNORE, path)) return get_malloced_str(path);
        }
        else return get_malloced_str(path);
    }
    
next:
    p = p2;
    if(*p2 == ':') p++;
    goto check;
    
// noexec:
    errno = ENOEXEC;
    return 0;
}

int is_function(char *cmd)
{
    return get_func(cmd) ? 1 : 0;
}

int is_builtin(char *cmd)
{
    return is_special_builtin(cmd) ? 1 : is_regular_builtin(cmd);
}

int is_enabled_builtin(char *cmd)
{
    if(!cmd) return 0;
    int     j;
    for(j = 0; j < special_builtin_count; j++)
    {
        if(strcmp(special_builtins[j].name, cmd) == 0 && 
           flag_set(special_builtins[j].flags, BUILTIN_ENABLED))
            return 1;
    }
    for(j = 0; j < regular_builtin_count; j++)
    {
        if(strcmp(regular_builtins[j].name, cmd) == 0 &&
           flag_set(regular_builtins[j].flags, BUILTIN_ENABLED))
            return 1;
    }
    return 0;
}

int is_special_builtin(char *cmd)
{
    if(!cmd) return 0;
    size_t  cmdlen = strlen(cmd);
    int     j;
    for(j = 0; j < special_builtin_count; j++)
    {
        if(special_builtins[j].namelen != cmdlen) continue;
        if(strcmp(special_builtins[j].name, cmd) == 0) return 1;
    }
    return 0;
}

int is_regular_builtin(char *cmd)
{
    if(!cmd) return 0;
    size_t  cmdlen = strlen(cmd);
    int     j;
    for(j = 0; j < regular_builtin_count; j++)
    {
        if(regular_builtins[j].namelen != cmdlen) continue;
        if(strcmp(regular_builtins[j].name, cmd) == 0) return 1;
    }
    return 0;
}


/*
 * flagarg is an optional argument needed by flags. if flags are empty (i.e. zero), flagarg
 * should also be zero.
 */
int fork_command(int argc, char **argv, char *use_path, char *UTILITY, int flags, int flagarg)
{
    pid_t child_pid;
    if((child_pid = fork()) == 0)
    {
        if(option_set('m'))
        {
            setpgid(0, 0);
            tcsetpgrp(0, child_pid);
        }
        reset_nonignored_traps();

        /* request to change the command's nice value (by the nice builtin) */
        if(flag_set(flags, FORK_COMMAND_DONICE))
        {
            if(setpriority(PRIO_PROCESS, 0, flagarg) == -1)
            {
                fprintf(stderr, "%s: failed to set nice value to %d: %s\r\n", UTILITY, flagarg, strerror(errno));
            }
        }
        /* request to ignore SIGHUP (by the nohup builtin) */
        if(flag_set(flags, FORK_COMMAND_IGNORE_HUP))
        {
            /* tcsh ignores the HUP signal here */
            if(signal(SIGHUP, SIG_IGN) < 0)
            {
                fprintf(stderr, "%s: failed to ignore SIGHUP: %s\r\n", UTILITY, strerror(errno));
            }
            /*
             * ... and GNU coreutils nohup modifies the standard streams if they are
             * connected to a terminal.
             */
            if(isatty(0))
            {
                close(0);
                open("/dev/null", O_RDONLY);
            }
            if(isatty(1))
            {
                close(1);
                /* try to open a file in CWD. if failed, try to open it in $HOME */
                if(open("nohup.out", O_APPEND) == -1)
                {
                    char *home = get_shell_varp("HOME", ".");
                    char path[strlen(home)+11];
                    sprintf(path, "%s/nohup.out", home);
                    if(open(path, O_APPEND) == -1)
                    {
                        /* nothing. open the NULL device */
                        open("/dev/null", O_WRONLY);
                    }
                }
            }
            /* redirect stderr to stdout */
            if(isatty(2))
            {
                close(2);
                dup2(1, 2);
            }
        }

        do_export_vars();
        do_exec_cmd(argc, argv, use_path, NULL);
        /* NOTE: we should NEVER come back here, unless there is error of course!! */
        fprintf(stderr, "%s: failed to exec '%s': %s\r\n", UTILITY, argv[0], strerror(errno));
        if(errno == ENOEXEC) exit(EXIT_ERROR_NOEXEC);
        if(errno == ENOENT ) exit(EXIT_ERROR_NOENT );
        exit(EXIT_FAILURE);
    }
    /* ... and parent countinues over here ...    */

    /* NOTE: we re-set the process group id here (and above in the child process) to make
     *       sure it gets set whether the parent or child runs first (i.e. avoid race condition).
     */
    if(option_set('m'))
    {
        setpgid(child_pid, 0);
        /* tell the terminal who's the foreground pgid now */
        tcsetpgrp(0, child_pid);
    }
    //pid_t resid;
    int   status;
    //resid = waitpid(child_pid, &status, WAIT_FLAG);
    waitpid(child_pid, &status, WAIT_FLAG);
    if(WIFSTOPPED(status) && option_set('m'))
    {
        struct job *job = add_job(child_pid, (pid_t[]){child_pid}, 1, argv[0], 0);
        set_cur_job(job);
        notice_termination(child_pid, status);
    }
    else
    {
        set_exit_status(status, 1);
        struct job *job = get_job_by_any_pid(child_pid);
        if(job)
        {
            set_pid_exit_status(job, child_pid, status);
            set_job_exit_status(job, child_pid, status);
        }
    }
    /* reset the terminal's foreground pgid */
    if(option_set('m')) tcsetpgrp(0, tty_pid);
    return status;
}

int file_exists(char *path)
{
    struct stat st;
    if(stat(path, &st) == 0)
    {
        if(S_ISREG(st.st_mode)) return 1;
    }
    return 0;
}

/*
 * return the full path to a temporary filename template, to be 
 * passed to mkstemp().
 */
char *get_tmp_filename()
{
    char *tmpdir = get_shell_varp("TMPDIR", "/tmp");
    int len = strlen(tmpdir);
    char buf[len+14];
    sprintf(buf, "%s%clsh.fcXXXXXX", tmpdir, (tmpdir[len-1] == '/') ? '\0' : '/');
    return get_malloced_str(buf);
}

/*
 * retrive the string value of a symbol table entry (presumably a shell variable).
 * if name is not defined (or is null), return def_val, which can be NULL.
 * this function doesn't return empty strings (name must be set to a non-empty value).
 * you can bypass this by passing "" as the value of def_val;
 */
char *get_shell_varp(char *name, char *def_val)
{
    struct symtab_entry_s *entry = get_symtab_entry(name);
    return (entry && entry->val && entry->val[0]) ? entry->val : def_val;
}

/*
 * retrive the integer value of a symbol table entry (presumably a shell variable).
 * if name is not defined (or is null), return def_val, which usually is passed as 0.
 */
int get_shell_vari(char *name, int def_val)
{
    return (int)get_shell_varl(name, def_val);
}

/*
 * same, but return long (not int).
 */
long get_shell_varl(char *name, int def_val)
{
    struct symtab_entry_s *entry = get_symtab_entry(name);
    if(entry && entry->val && entry->val[0])
    {
        char *strend = NULL;
        long i = strtol(entry->val, &strend, 10);
        if(strend == entry->val) return def_val;
        return i;
    }
    return def_val;
}

/*
 * quick set of var value (without needing to retrieve symtab entry ourselves).
 */
void set_shell_varp(char *name, char *val)
{
    struct symtab_entry_s *entry = get_symtab_entry(name);
    if(entry) symtab_entry_setval(entry, val);
}