/* 
 *    Programmed By: Mohammed Isam Mohammed [mohammed_isam1984@yahoo.com]
 *    Copyright 2019, 2020 (c)
 * 
 *    file: declare.c
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
#include "builtins.h"
#include "../cmd.h"
#include "../symtab/symtab.h"
#include "../parser/node.h"
#include "../debug.h"

/* flags for purge_table() */
#define FLAG_PRINT_FUNCDEF  (1 << 0)
#define FLAG_PRINT_LOCAL    (1 << 2)
#define FLAG_PRINT_FORMAL   (1 << 3)

void   purge_table(struct symtab_s *symtab, struct alpha_list_s *list, int flags);
void   do_print_var(struct symtab_entry_s *entry, struct alpha_list_s *list, int print_formal);
void   do_print_funcdef(struct symtab_entry_s *entry);


/*
 * the declare builtin utility (non-POSIX).. used to define variables and print them.
 * returns 0 if the variables were defined or printed, non-zero otherwise.
 * 
 * see the manpage for the list of options and an explanation of what each option does.
 * you can also run: `help declare` or `declare -h` from lsh prompt to see a short
 * explanation on how to use this utility.
 */

int declare_builtin(int argc, char **argv)
{
    /*
     * if we're called from the global scope, declare variables globally,
     * otherwise declare them locally (similar to the local builtin).
     */
    struct symtab_s *symtab;
    int local = (get_local_symtab()->level > 1);
    if(local)
    {
        symtab = symtab_stack_pop();
    }

    int res = do_declare(argc, argv, !local);

    if(local)
    {
        symtab_stack_add(symtab);
    }
    return res;
}


/*
 * declare variables and functions, setting and unsetting their attributes
 * as requested.. positional parameters ($1, $2, $3, ...) and special parameters
 * ($0, $$, $?) cannot be set this way.
 * 
 * 'msg_verb' contains a verb we'll print as part of the error message, in
 * case we failed to set the given variable.
 * 
 * return value:
 *   1   if a variable is a positional parameter, a special parameter, or if
 *         any other error occurred during setting a variable's attributes 
 *         (e.g. setting a readonly variable).
 *   0   if all variables are declared and their attributes set successfully.
 */
int do_declare(int argc, char **argv, int global)
{
    int v = 1, c, res = 0;
    int print_formal = 0;
    int funcs = 0;
    /* the name we're called with (declare, typeset, or local) */
    char *utility = argv[0];
    /* flags to be set */
    int set_flags = global ? 0 : FLAG_LOCAL;
    /* flags to be unset */
    int unset_flags = 0;
    /* flags for printing values */
    int print_flags = FLAG_PRINT_FUNCDEF | (global ? 0 : FLAG_PRINT_LOCAL);
    
    /****************************
     * process the options
     ****************************/
    while((c = parse_args(argc, argv, "+fFghilprtuvx", &v, 1)) > 0)
    {
        /* check if the options string starts with '+', which 'turns off' options */
        char plus = (argv[v][0] == '+') ? 1 : 0;
        int *flags = plus ? &unset_flags : &set_flags;
        switch(c)
        {
            case 'f':               /* restrict output to function names and definitions */
                funcs = !plus;
                break;
                
            case 'F':               /* don't print function definitions */
                if((funcs = !plus))
                {
                    print_flags &= ~FLAG_PRINT_FUNCDEF;
                }
                break;
                
            case 'g':               /* declare on the global level (even inside functions) */
                global = !plus;
                break;
                
            case 'h':
                print_help(argv[0], strcmp(utility, "local") == 0 ?
                                    &LOCAL_BUILTIN : &DECLARE_BUILTIN, 0);
                return 0;
                
            case 'i':               /* assign only int values */
                (*flags) |= FLAG_INTVAL;
                break;

            case 'l':           /* convert variable's value to lowercase on assignment */
                (*flags) |= FLAG_ALLSMALL;
                if(!plus)
                {
                    unset_flags |= FLAG_ALLCAPS;
                }
                break;
                
            case 'p':               /* print variables and their attributes */
                if((print_formal = !plus))
                {
                    print_flags |= FLAG_PRINT_FORMAL;
                }
                break;
                
            case 'r':               /* mark as readonly variables */
                if(plus)
                {
                    PRINT_ERROR("%s: cannot use the '+r' option to remove the "
                                "readonly attribute\n", utility);
                    return 2;
                }
                (*flags) |= FLAG_READONLY;
                break;
                
            case 't':           /* turn function tracing on/off for the given function */
                (*flags) |= FLAG_FUNCTRACE;
                break;
                
            case 'u':           /* convert variable's value to uppercase on assignment */
                (*flags) |= FLAG_ALLCAPS;
                if(!plus)
                {
                    unset_flags |= FLAG_ALLSMALL;
                }
                break;
                
            case 'v':
                printf("%s", shell_ver);
                return 0;
                
            case 'x':               /* mark as export variables/functions */
                (*flags) |= FLAG_EXPORT;
                break;
        }
    }

    /* unknown option */
    if(c == -1)
    {
        return 2;
    }

    /* no arguments. print all variables */
    if(v >= argc || print_formal)
    {
        return purge_vars(&argv[v], utility, funcs, print_flags);
    }
    
    /* turn off some illegal flag combinations */
    if(funcs)
    {
        set_flags &= ~(FLAG_INTVAL | FLAG_ALLCAPS | FLAG_ALLSMALL);
    }
    else
    {
        set_flags &= ~FLAG_FUNCTRACE;
    }

    /* loop on the arguments */
    for( ; v < argc; v++)
    {
        /* cannot define functions using declare -f (bash) */
        if(funcs)
        {
            /* bash seems to return 1 without printing any error message */
            /*
            PRINT_ERROR("%s: cannot use the '-f' option to define functions\n",
                        utility);
            */
            res = 1;
        }
        else
        {
            /* is this a "name=value" string? */
            char *arg = argv[v];
            char *equals = strchr(arg, '=');
            if(equals == arg)
            {
                PRINT_ERROR("%s: empty variable name at: %s\n", utility, arg);
                res = 1;
                continue;
            }
                
            /* if yes, get the value part */
            struct symtab_entry_s *entry;
            char *val = equals ? equals+1 : NULL;
            int flags = SET_FLAG_FORCE_NEW | (global ? SET_FLAG_GLOBAL : 0);
            int append = (equals && equals[-1] == '+');
    
            /* and the name part */
            size_t name_len = equals ? (size_t)(equals-arg) : strlen(arg);
            char name_buf[name_len+1];
            strncpy(name_buf, arg, name_len);
            name_buf[name_len] = '\0';

            if(append)
            {
                name_buf[name_len-1] = '\0';
                flags |= SET_FLAG_APPEND;
            }

            if((entry = do_set(name_buf, val, set_flags, unset_flags, flags)))
            {
                /* if var is readonly, do_set returns it too */
                if(flag_set(entry->flags, FLAG_READONLY))
                {
                    READONLY_ASSIGN_ERROR(utility, name_buf);
                }
            }
            else
            {
                /* do_set() should have printed the error message */
                res = 1;
            }
        }
    }
    return res;
}


/*
 * print the values and attributes of variables or functions, depending on the
 * flags parameter.
 */
int purge_vars(char **args, char *utility, int funcs, int flags)
{
    int print_funcdef = funcs && flag_set(flags, FLAG_PRINT_FUNCDEF);
    int local_only    = flag_set(flags, FLAG_PRINT_LOCAL);
    int print_formal  = flag_set(flags, FLAG_PRINT_FORMAL);
    int res = 0;

    /* use an alpha list to sort variables alphabetically */
    struct alpha_list_s list;
    init_alpha_list(&list);
    
    /* 
     * function definitions are printed straight away, while variables are
     * added to the alpha list, which we print now.
     */
    struct alpha_list_s *plist = funcs ? NULL : &list;

    if(args && args[0])
    {
        /* which symbol table we'll use to search for these vars/funcs? */
        struct symtab_entry_s *(*f)(char *) = funcs ? get_func : 
                                              local_only ? get_local_symtab_entry : 
                                              get_symtab_entry;
        char **p;
        for(p = args; *p; p++)
        {
            /* fetch the variable/function from the symbol table */
            char *arg = *p;
            struct symtab_entry_s *entry = f(arg);

            if(!entry)
            {
                /* variable/function not found */
                char *word = funcs ? "function" : "variable";
                PRINT_ERROR("%s: unknown %s name: %s\n", utility, word, arg);
                res = 1;
            }
            else
            {
                if(print_funcdef)
                {
                    do_print_funcdef(entry);
                }
                
                /* print the variable (or function) value and attributes */
                do_print_var(entry, plist, print_formal);
            }
        }
        
        if(!funcs)
        {
            print_alpha_list(plist);
        }
    }
    else
    {
        if(funcs)
        {
            /* all the functions are stored in one global table */
            purge_table(func_table, NULL, flags);
        }
        else if(local_only)
        {
            purge_table(get_local_symtab(), plist, flags);
            print_alpha_list(plist);
        }
        else
        {
            /* print the variables in all the tables in the symbol table stack */
            int i;
            struct symtab_stack_s *stack = get_symtab_stack();
            for(i = 0; i < stack->symtab_count; i++)
            {
                purge_table(stack->symtab_list[i], plist, flags);
            }
            print_alpha_list(plist);

            /* and functions if -p is specified */
            if(flag_set(flags, FLAG_PRINT_FORMAL))
            {
                purge_table(func_table, NULL, flags);
            }
        }
    }

    free_alpha_list(&list);
    return res;
}


/*
 * print the names and values of all the variables stored in the symbol
 * table pointed to by 'symtab'.. 'flags' indicate whether we should print
 * function definitions, whether to print output in a "formal" way by 
 * preceding each line with 'declare ...', etc. if 'list' is non-NULL, output
 * is added to this alpha list, otherwise output is printed directly to stdout.
 */
void purge_table(struct symtab_s *symtab, struct alpha_list_s *list, int flags)
{
    int print_funcdef = flag_set(flags, FLAG_PRINT_FUNCDEF);
    int print_formal  = flag_set(flags, FLAG_PRINT_FORMAL );
    
    if(!symtab)
    {
        return;
    }
    
    /*
     * iterate through the symbol table entries, printing each in turn.
     * how we iterate through the entries depends on how we implement
     * the symbol table struct (hashtables vs linked lists).
     */
    
#ifdef USE_HASH_TABLES
    
    if(symtab->used)
    {
        struct symtab_entry_s **h1 = symtab->items;
        struct symtab_entry_s **h2 = symtab->items + symtab->size;
        for( ; h1 < h2; h1++)
        {
            struct symtab_entry_s *entry = *h1;
                
#else

    struct symtab_entry_s *entry  = symtab->first;
            
#endif

            while(entry)
            {
                /* a function for which we should print the function definition */
                if(entry->val_type == SYM_FUNC && print_funcdef)
                {
                    do_print_funcdef(entry);
                }
                
                do_print_var(entry, list, print_formal);
                entry = entry->next;
            }
                
#ifdef USE_HASH_TABLES

        }
    }
        
#endif

}


/*
 * output the value and attributes of the symbol table entry.. if the entry
 * represents a variable, its name and value are printed.. if it represents a
 * function, its function name (and definition, if name_only == zero) is printed.
 */
void do_print_var(struct symtab_entry_s *entry, struct alpha_list_s *list, int print_formal)
{
    char prefix[32], *p2 = prefix;
    prefix[0] = '\0';

    /* print the variable/function name and attribs */
    if(print_formal)
    {
        static unsigned int f[] = { FLAG_EXPORT, FLAG_READONLY, FLAG_INTVAL,
                                    FLAG_ALLCAPS, FLAG_ALLSMALL, FLAG_FUNCTRACE };
        static char p[] = { 'x', 'r', 'i', 'u', 'l', 't' };
        int i, j = 0;

        sprintf(p2, "declare ");
        p2 += 8;

        for(i = 0; i < 6; i++)
        {
            if(flag_set(entry->flags, f[i]))
            {
                sprintf(p2, "-%c ", p[i]);
                p2 += 3;
                j++;
            }
        }
    
        if(entry->val_type == SYM_FUNC)
        {
            sprintf(p2, "-f ");
        }
        else if(j == 0)
        {
            /*
             * if no flags are set, add the special option '--', which tells
             * 'declare' to end option parsing and start reading arguments 
             * (if this output is used as input to the shell).
             */
            sprintf(p2, "-- ");
        }
    }
    
    /* if the entry is not a function, print the value */
    if(entry->val_type == SYM_STR && entry->val)
    {
        char *val = quote_val(entry->val, 1, 0);
        if(val)
        {
            p2 = alpha_list_make_str("%s%s=%s", prefix, entry->name, val);
            free(val);
        }
        else
        {
            p2 = alpha_list_make_str("%s%s=\"\"", prefix, entry->name);
        }
    }
    else
    {
        p2 = alpha_list_make_str("%s%s%s", prefix, entry->name, print_formal ? "" : "=");
    }
    
    /* add to the alpha list, or print directly */
    if(list)
    {
        add_to_alpha_list(list, p2);
    }
    else
    {
        printf("%s\n", p2);
        if(p2)
        {
            free(p2);
        }
    }
}


void do_print_funcdef(struct symtab_entry_s *entry)
{
    if(entry->val)
    {
        /* function definition strings start with '()' (bash) */
        if(entry->val[0] == '(' && entry->val[1] == ')')
        {
            printf("%s %s\n", entry->name, entry->val);
        }
        else
        {
            /* if by any chance the '()' are missing, add them manually */
            printf("%s () { %s }\n", entry->name, entry->val);
        }
    }
    else
    {
        /*
         * if the function entry contains a nodetree of the parsed function body,
         * convert it to a string and print it.
         */
        printf("%s () { ", entry->name);
        char *f = cmd_nodetree_to_str(entry->func_body);
        if(f)
        {
            printf("%s", f);
            free(f);
        }
        printf(" }\n");
    }
}
