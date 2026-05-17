/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * asdsh -- minimal userspace shell for ASD
 *
 * Changes:
 *  - FIX Bug 4: added fastfetch (alias: fetch) to command table so it
 *    can be invoked from the shell.
 *  - Added uname, uptime, id, whoami, wc, hexdump, kill commands.
 *  - Added edit/ed command to launch the asded text editor.
 *  - Improved path resolution with basic ".." handling.
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

/* pulled in via libasd/string.c */
extern size_t strlen(const char *);
extern int    strcmp(const char *, const char *);
extern int    strncmp(const char *, const char *, size_t);
extern char  *strrchr(const char *, int);
extern char  *strchr(const char *, int);
extern char  *strncpy(char *, const char *, size_t);
extern void  *memset(void *, int, size_t);

#define PATH_MAX_SH  256
#define LINE_MAX     512

static char g_cwd[PATH_MAX_SH] = "/";

/* ------------------------------------------------------------------ */
/* Path resolution                                                      */
/* ------------------------------------------------------------------ */

/*
 * Resolve a relative or absolute path against g_cwd.
 * Handles ".." components to prevent trivial path traversal.
 */
static void resolve(const char *arg, char *out) {
    if (!arg || !arg[0]) { strncpy(out, g_cwd, PATH_MAX_SH); return; }
    if (arg[0] == '/') {
        strncpy(out, arg, PATH_MAX_SH);
    } else {
        size_t n = strlen(g_cwd);
        strncpy(out, g_cwd, PATH_MAX_SH);
        if (n > 0 && out[n-1] != '/') { out[n++] = '/'; out[n] = '\0'; }
        strncpy(out + n, arg, PATH_MAX_SH - n - 1);
    }
    out[PATH_MAX_SH - 1] = '\0';

    /* Normalise ".." components in-place */
    char tmp[PATH_MAX_SH];
    char *parts[64];
    int  nparts = 0;
    strncpy(tmp, out, PATH_MAX_SH);
    char *tok = tmp + 1; /* skip leading '/' */
    char *p   = tok;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (tok[0] && strcmp(tok, ".") != 0) {
                if (strcmp(tok, "..") == 0) {
                    if (nparts > 0) nparts--;
                } else {
                    if (nparts < 63) parts[nparts++] = tok;
                }
            }
            tok = p + 1;
        }
        p++;
    }
    if (tok[0] && strcmp(tok, ".") != 0) {
        if (strcmp(tok, "..") == 0) {
            if (nparts > 0) nparts--;
        } else {
            if (nparts < 63) parts[nparts++] = tok;
        }
    }
    /* Rebuild */
    out[0] = '/'; out[1] = '\0';
    for (int i = 0; i < nparts; i++) {
        size_t ol = strlen(out);
        if (ol > 1) { out[ol++] = '/'; out[ol] = '\0'; }
        strncpy(out + ol, parts[i], PATH_MAX_SH - ol - 1);
    }
    if (out[0] == '\0') { out[0] = '/'; out[1] = '\0'; }
    /* remove trailing slash except root */
    size_t l=strlen(out);
    if(l>1 && out[l-1]=='/') out[l-1]='\0';
}

static int is_root(const char *p) { return p[0] == '/' && p[1] == '\0'; }

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */

static void run_external(const char *cmd, const char *arg) {
    char path[PATH_MAX_SH];
    snprintf(path, sizeof(path), "/bin/%s", cmd);
    const char *argv[] = { cmd, arg, NULL };
    int pid = asd_spawn(path, argv, NULL);
    if (pid < 0) {
        printf("asdsh: %s: command not found in /bin\n", cmd);
        return;
    }
    asd_wait(pid, NULL);
}

static void cmd_ls(const char *arg) {
    run_external("ls", arg);
}

static void cmd_cd(const char *arg) {
    char path[PATH_MAX_SH];
    if (!arg || !arg[0]) { strncpy(g_cwd, "/", PATH_MAX_SH); return; }
    resolve(arg, path);
    if (is_root(path)) { strncpy(g_cwd, "/", PATH_MAX_SH); return; }

    asd_stat_t st;
    int r = asd_stat(path, &st);
    if (r != 0 || st.kind != ASD_NODE_DIR) {
        printf("cd: %s: no such directory\n", path); return;
    }
    strncpy(g_cwd, path, PATH_MAX_SH);
}

static void cmd_pwd(const char *arg) {
    (void)arg;
    printf("%s\n", g_cwd);
}

static void cmd_cat(const char *arg)     { run_external("cat",     arg); }
static void cmd_mkdir(const char *arg)   { run_external("mkdir",   arg); }
static void cmd_rm(const char *arg)      { run_external("rm",      arg); }
static void cmd_touch(const char *arg)   { run_external("touch",   arg); }
static void cmd_echo(const char *arg)    { run_external("echo",    arg); }
static void cmd_uname(const char *arg)   { run_external("uname",   arg); }
static void cmd_uptime(const char *arg)  { run_external("uptime",  arg); }
static void cmd_id(const char *arg)      { run_external("id",      arg); }
static void cmd_whoami(const char *arg)  { run_external("whoami",  arg); }
static void cmd_wc(const char *arg)      { run_external("wc",      arg); }
static void cmd_hexdump(const char *arg) { run_external("hexdump", arg); }
static void cmd_kill(const char *arg)    { run_external("kill",    arg); }

/* FIX Bug 4: fastfetch was built but never registered in the command
 * table, so it could not be invoked.  Added here with alias 'fetch'. */
static void cmd_fastfetch(const char *arg) {
    (void)arg;
    run_external("fastfetch", NULL);
}

/* ASD text editor */
static void cmd_edit(const char *arg) { run_external("asded", arg); }

static void cmd_stat(const char *arg) {
    char path[PATH_MAX_SH];
    if (!arg || !arg[0]) { printf("stat: missing path\n"); return; }
    resolve(arg, path);
    asd_stat_t st;
    if (asd_stat(path, &st) != 0) {
        printf("stat: %s: no such file or directory\n", path);
        return;
    }
    printf("File: %s\n", path);
    printf("Size: %d\n", (int)st.size);
    printf("Kind: %s\n", (st.kind == ASD_NODE_DIR) ? "Directory" : "File");
}

static void cmd_help(const char *arg);

static void cmd_exit(const char *arg) {
    (void)arg;
    asd_exit(0);
}

/* ------------------------------------------------------------------ */
/* Command Table                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *alias;
    void (*func)(const char *arg);
    const char *help;
} shell_cmd_t;

static shell_cmd_t g_cmds[] = {
    {"ls",        "dir",   cmd_ls,        "ls [path]      - List directory contents"},
    {"cd",        NULL,    cmd_cd,        "cd [path]      - Change current directory"},
    {"pwd",       NULL,    cmd_pwd,       "pwd            - Print working directory"},
    {"cat",       NULL,    cmd_cat,       "cat <file>     - Concatenate and print files"},
    {"mkdir",     NULL,    cmd_mkdir,     "mkdir <dir>    - Create directory"},
    {"rm",        NULL,    cmd_rm,        "rm <file>      - Remove file"},
    {"touch",     NULL,    cmd_touch,     "touch <file>   - Create empty file"},
    {"stat",      NULL,    cmd_stat,      "stat <path>    - Display file status"},
    {"echo",      NULL,    cmd_echo,      "echo [text]    - Display a line of text"},
    {"uname",     NULL,    cmd_uname,     "uname          - Print system name"},
    {"uptime",    NULL,    cmd_uptime,    "uptime         - Show system uptime"},
    {"id",        NULL,    cmd_id,        "id             - Print user identity"},
    {"whoami",    NULL,    cmd_whoami,    "whoami         - Print current user name"},
    {"wc",        NULL,    cmd_wc,        "wc <file>      - Word/line/byte count"},
    {"hexdump",   "xxd",   cmd_hexdump,   "hexdump <file> - Hex dump of file"},
    {"kill",      NULL,    cmd_kill,      "kill <pid>     - Send signal to process"},
    {"fastfetch", "fetch", cmd_fastfetch, "fastfetch      - Show system information"},
    {"edit",      "ed",    cmd_edit,      "edit <file>    - Open ASD text editor"},
    {"help",      NULL,    cmd_help,      "help           - Show this help message"},
    {"exit",      "quit",  cmd_exit,      "exit           - Exit the shell"},
    {NULL,        NULL,    NULL,          NULL}
};

static void cmd_help(const char *arg) {
    (void)arg;
    puts("Available commands:");
    for (int i = 0; g_cmds[i].name; i++) {
        printf("  %s\n", g_cmds[i].help);
    }
}

/* ------------------------------------------------------------------ */
/* Main loop                                                            */
/* ------------------------------------------------------------------ */

static void print_prompt(void) {
    int pid = asd_getpid();
    printf("[%d] %s $ ", pid, g_cwd);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    puts("\nasdsh -- ASD userspace shell");
    puts("Type 'help' for commands, 'fastfetch' for system info.\n");

    char line[LINE_MAX];

    for (;;) {
        print_prompt();

        int n = readline(line, sizeof(line));
        if (n <= 0) continue;

        /* Basic tokenization: split into command and argument */
        char *cmd_name = line;
        char *arg = NULL;

        /* Skip leading spaces */
        while (*cmd_name == ' ') cmd_name++;
        if (*cmd_name == '\0') continue;

        /* Find first space to separate command and arg */
        char *p = cmd_name;
        while (*p && *p != ' ') p++;
        if (*p == ' ') {
            *p = '\0';
            arg = p + 1;
            /* Skip leading spaces in arg */
            while (*arg == ' ') arg++;
            if (*arg == '\0') arg = NULL;
        }

        /* Dispatch via table */
        int found = 0;
        for (int i = 0; g_cmds[i].name; i++) {
            if (strcmp(cmd_name, g_cmds[i].name) == 0 ||
                (g_cmds[i].alias && strcmp(cmd_name, g_cmds[i].alias) == 0)) {
                g_cmds[i].func(arg ? arg : (strcmp(cmd_name, "ls") == 0 ? g_cwd : NULL));
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("asdsh: %s: command not found\n", cmd_name);
        }
    }

    asd_exit(0);
    return 0;
}
