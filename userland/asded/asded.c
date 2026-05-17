/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * asded -- ASD text editor
 *
 * A line-oriented modal text editor inspired by vi/ed.
 * Unique feature: real-time C keyword highlighting via ANSI escape codes
 * and a built-in undo/redo stack (up to UNDO_DEPTH levels).
 *
 * Modes:
 *   NORMAL  -- navigate, issue commands
 *   INSERT  -- type text to insert at current line
 *   COMMAND -- :w, :q, :wq, :u (undo), :r (redo), :N (goto line N)
 *
 * Key bindings (NORMAL mode):
 *   j / k    -- move down / up one line
 *   G        -- go to last line
 *   i        -- enter INSERT mode (insert line after current)
 *   I        -- enter INSERT mode (insert line before current)
 *   d        -- delete current line (pushed to undo stack)
 *   :        -- enter COMMAND mode
 *   q        -- quit (warns if unsaved)
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

extern size_t strlen(const char *);
extern int    strcmp(const char *, const char *);
extern int    strncmp(const char *, const char *, size_t);
extern char  *strncpy(char *, const char *, size_t);
extern void  *memset(void *, int, size_t);
extern void  *memmove(void *, const void *, size_t);

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#define MAX_LINES    512
#define LINE_CAP     256
#define UNDO_DEPTH   32
#define PATH_CAP     256

/* ANSI colour codes (sent to serial/fb terminal) */
#define ANSI_RESET   "\033[0m"
#define ANSI_KEYWORD "\033[1;33m"   /* bold yellow  */
#define ANSI_COMMENT "\033[0;32m"   /* green        */
#define ANSI_STRING  "\033[0;36m"   /* cyan         */
#define ANSI_NUMBER  "\033[0;35m"   /* magenta      */
#define ANSI_BOLD    "\033[1m"
#define ANSI_INVERT  "\033[7m"

/* ------------------------------------------------------------------ */
/* Buffer                                                               */
/* ------------------------------------------------------------------ */

static char  g_lines[MAX_LINES][LINE_CAP];
static int   g_nlines  = 0;
static int   g_cur     = 0;   /* current line index (0-based) */
static int   g_dirty   = 0;
static char  g_path[PATH_CAP] = "";

/* ------------------------------------------------------------------ */
/* Undo stack                                                           */
/* ------------------------------------------------------------------ */

typedef enum { OP_INSERT, OP_DELETE, OP_REPLACE } op_kind_t;

typedef struct {
    op_kind_t kind;
    int       line;
    char      text[LINE_CAP];
} undo_entry_t;

static undo_entry_t g_undo[UNDO_DEPTH];
static int          g_undo_top  = 0;   /* next free slot */
static undo_entry_t g_redo[UNDO_DEPTH];
static int          g_redo_top  = 0;

static void undo_push(op_kind_t k, int line, const char *text) {
    if (g_undo_top >= UNDO_DEPTH) {
        /* Shift out oldest entry */
        memmove(&g_undo[0], &g_undo[1],
                (UNDO_DEPTH - 1) * sizeof(undo_entry_t));
        g_undo_top = UNDO_DEPTH - 1;
    }
    g_undo[g_undo_top].kind = k;
    g_undo[g_undo_top].line = line;
    strncpy(g_undo[g_undo_top].text, text ? text : "", LINE_CAP - 1);
    g_undo_top++;
    g_redo_top = 0; /* new action clears redo */
}

static int do_undo(void) {
    if (g_undo_top == 0) return 0;
    g_undo_top--;
    undo_entry_t *e = &g_undo[g_undo_top];
    switch (e->kind) {
    case OP_INSERT:
        /* undo insert => delete that line */
        if (e->line < g_nlines) {
            /* push to redo */
            if (g_redo_top < UNDO_DEPTH) {
                g_redo[g_redo_top].kind = OP_INSERT;
                g_redo[g_redo_top].line = e->line;
                strncpy(g_redo[g_redo_top].text, g_lines[e->line], LINE_CAP-1);
                g_redo_top++;
            }
            memmove(&g_lines[e->line], &g_lines[e->line + 1],
                    (size_t)(g_nlines - e->line - 1) * LINE_CAP);
            g_nlines--;
            if (g_cur >= g_nlines && g_cur > 0) g_cur = g_nlines - 1;
        }
        break;
    case OP_DELETE:
        /* undo delete => re-insert */
        if (g_nlines < MAX_LINES) {
            if (g_redo_top < UNDO_DEPTH) {
                g_redo[g_redo_top].kind = OP_DELETE;
                g_redo[g_redo_top].line = e->line;
                g_redo[g_redo_top].text[0] = '\0';
                g_redo_top++;
            }
            memmove(&g_lines[e->line + 1], &g_lines[e->line],
                    (size_t)(g_nlines - e->line) * LINE_CAP);
            strncpy(g_lines[e->line], e->text, LINE_CAP - 1);
            g_nlines++;
            g_cur = e->line;
        }
        break;
    case OP_REPLACE:
        /* undo replace => restore old text */
        if (e->line < g_nlines) {
            if (g_redo_top < UNDO_DEPTH) {
                g_redo[g_redo_top].kind = OP_REPLACE;
                g_redo[g_redo_top].line = e->line;
                strncpy(g_redo[g_redo_top].text, g_lines[e->line], LINE_CAP-1);
                g_redo_top++;
            }
            strncpy(g_lines[e->line], e->text, LINE_CAP - 1);
            g_cur = e->line;
        }
        break;
    }
    g_dirty = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* C syntax highlighting                                                */
/* ------------------------------------------------------------------ */

static const char *c_keywords[] = {
    "auto","break","case","char","const","continue","default","do",
    "double","else","enum","extern","float","for","goto","if",
    "inline","int","long","register","return","short","signed",
    "sizeof","static","struct","switch","typedef","union","unsigned",
    "void","volatile","while","NULL","define","include","ifdef",
    "ifndef","endif","pragma",NULL
};

static int is_kw_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '#';
}

/*
 * Print one line with C syntax highlighting.
 * Operates on a copy so the original buffer is not modified.
 */
static void print_highlighted(const char *line) {
    int in_str    = 0;   /* inside "..." */
    int in_char   = 0;   /* inside '.' */
    int in_comment = 0;  /* inside // comment */

    const char *p = line;
    while (*p) {
        /* Comment: // to end of line */
        if (!in_str && !in_char && p[0] == '/' && p[1] == '/') {
            puts(ANSI_COMMENT);
            puts(p);
            puts(ANSI_RESET);
            return;
        }
        /* String literal */
        if (!in_comment && !in_char && *p == '"') {
            if (!in_str) {
                puts(ANSI_STRING);
                in_str = 1;
            } else {
                putc('"');
                puts(ANSI_RESET);
                in_str = 0;
                p++;
                continue;
            }
        }
        /* Char literal */
        if (!in_comment && !in_str && *p == '\'') {
            if (!in_char) {
                puts(ANSI_STRING);
                in_char = 1;
            } else {
                putc('\'');
                puts(ANSI_RESET);
                in_char = 0;
                p++;
                continue;
            }
        }
        /* Inside string or char: just output */
        if (in_str || in_char) {
            putc(*p++);
            continue;
        }
        /* Number literal */
        if (*p >= '0' && *p <= '9') {
            puts(ANSI_NUMBER);
            while ((*p >= '0' && *p <= '9') || *p == '.' ||
                   *p == 'x' || *p == 'X' ||
                   (*p >= 'a' && *p <= 'f') ||
                   (*p >= 'A' && *p <= 'F') ||
                   *p == 'u' || *p == 'U' ||
                   *p == 'l' || *p == 'L') {
                putc(*p++);
            }
            puts(ANSI_RESET);
            continue;
        }
        /* Keyword / identifier */
        if (is_kw_char(*p)) {
            char word[64];
            int  wlen = 0;
            const char *start = p;
            while (is_kw_char(*p) && wlen < 63)
                word[wlen++] = *p++;
            word[wlen] = '\0';
            int matched = 0;
            for (int i = 0; c_keywords[i]; i++) {
                if (strcmp(word, c_keywords[i]) == 0) {
                    puts(ANSI_KEYWORD);
                    puts(word);
                    puts(ANSI_RESET);
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                /* plain identifier */
                for (const char *q = start; q < p; q++) putc(*q);
            }
            continue;
        }
        putc(*p++);
    }
    (void)in_comment;
}

/* ------------------------------------------------------------------ */
/* Display                                                              */
/* ------------------------------------------------------------------ */

#define VISIBLE_LINES 20

static void render(void) {
    /* Clear screen */
    puts("\033[2J\033[H");

    /* Header */
    printf(ANSI_INVERT " asded " ANSI_RESET
           "  %s%s  [%d/%d lines]  %s\n",
           g_path[0] ? g_path : "(no file)",
           g_dirty ? " [+]" : "",
           g_cur + 1, g_nlines,
           "-- NORMAL --");
    puts("─────────────────────────────────────────────────────────────\n");

    /* Compute window */
    int start = g_cur - VISIBLE_LINES / 2;
    if (start < 0) start = 0;
    int end = start + VISIBLE_LINES;
    if (end > g_nlines) end = g_nlines;

    for (int i = start; i < end; i++) {
        if (i == g_cur)
            printf(ANSI_INVERT "%4d" ANSI_RESET " ", i + 1);
        else
            printf("%4d ", i + 1);
        /* Highlight .c/.h files */
        int do_hl = 0;
        if (g_path[0]) {
            size_t pl = strlen(g_path);
            if (pl >= 2 &&
                (g_path[pl-1] == 'c' || g_path[pl-1] == 'h') &&
                g_path[pl-2] == '.')
                do_hl = 1;
        }
        if (do_hl)
            print_highlighted(g_lines[i]);
        else
            puts(g_lines[i]);
        putc('\n');
    }

    if (g_nlines == 0)
        puts("  (empty buffer)\n");

    puts("─────────────────────────────────────────────────────────────\n");
    puts("j/k=move  i=insert  I=ins-before  d=del  :=cmd  q=quit\n");
}

/* ------------------------------------------------------------------ */
/* File I/O                                                             */
/* ------------------------------------------------------------------ */

static int load_file(const char *path) {
    int fd = asd_open(path, O_RDONLY);
    if (fd < 0) return 0;
    g_nlines = 0;
    char ch;
    int  col = 0;
    while (asd_read(fd, &ch, 1) == 1 && g_nlines < MAX_LINES) {
        if (ch == '\n') {
            g_lines[g_nlines][col] = '\0';
            g_nlines++;
            col = 0;
        } else if (col < LINE_CAP - 1) {
            g_lines[g_nlines][col++] = ch;
        }
    }
    if (col > 0 && g_nlines < MAX_LINES) {
        g_lines[g_nlines][col] = '\0';
        g_nlines++;
    }
    asd_close(fd);
    g_dirty = 0;
    return 1;
}

static int save_file(const char *path) {
    if (!path || !path[0]) return 0;
    int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    for (int i = 0; i < g_nlines; i++) {
        size_t len = strlen(g_lines[i]);
        asd_write(fd, g_lines[i], len);
        asd_write(fd, "\n", 1);
    }
    asd_close(fd);
    g_dirty = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Insert mode                                                          */
/* ------------------------------------------------------------------ */

static void insert_mode(int before) {
    if (g_nlines >= MAX_LINES) {
        puts("asded: buffer full\n");
        return;
    }
    int ins = before ? g_cur : g_cur + 1;
    if (ins > g_nlines) ins = g_nlines;

    /* Shift lines down */
    memmove(&g_lines[ins + 1], &g_lines[ins],
            (size_t)(g_nlines - ins) * LINE_CAP);
    g_lines[ins][0] = '\0';
    g_nlines++;
    g_cur = ins;

    /* Read new line content */
    puts("\033[2J\033[H");
    printf("INSERT line %d (empty line to cancel):\n> ", ins + 1);
    char buf[LINE_CAP];
    int n = readline(buf, sizeof(buf));
    if (n <= 0) {
        /* Cancel: remove the blank line we inserted */
        memmove(&g_lines[ins], &g_lines[ins + 1],
                (size_t)(g_nlines - ins - 1) * LINE_CAP);
        g_nlines--;
        if (g_cur >= g_nlines && g_cur > 0) g_cur = g_nlines - 1;
        return;
    }
    strncpy(g_lines[ins], buf, LINE_CAP - 1);
    undo_push(OP_INSERT, ins, g_lines[ins]);
    g_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Command mode                                                         */
/* ------------------------------------------------------------------ */

static int command_mode(void) {
    puts("\n:");
    char cmd[64];
    int n = readline(cmd, sizeof(cmd));
    if (n <= 0) return 1;

    if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        if (g_dirty) {
            puts("asded: unsaved changes. Use :q! to force or :wq to save.\n");
            return 1;
        }
        return 0;
    }
    if (strcmp(cmd, "q!") == 0) return 0;
    if (strcmp(cmd, "w") == 0) {
        if (save_file(g_path))
            printf("asded: saved %d lines to %s\n", g_nlines, g_path);
        else
            puts("asded: save failed (no path?)\n");
        return 1;
    }
    if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        save_file(g_path);
        return 0;
    }
    if (strcmp(cmd, "u") == 0 || strcmp(cmd, "undo") == 0) {
        if (!do_undo()) puts("asded: nothing to undo\n");
        return 1;
    }
    /* :N  goto line N */
    int lineno = 0;
    int is_num = 1;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] < '0' || cmd[i] > '9') { is_num = 0; break; }
        lineno = lineno * 10 + (cmd[i] - '0');
    }
    if (is_num && lineno > 0) {
        g_cur = lineno - 1;
        if (g_cur >= g_nlines) g_cur = g_nlines - 1;
        if (g_cur < 0) g_cur = 0;
        return 1;
    }
    /* :w <path> */
    if (strncmp(cmd, "w ", 2) == 0) {
        strncpy(g_path, cmd + 2, PATH_CAP - 1);
        if (save_file(g_path))
            printf("asded: saved to %s\n", g_path);
        else
            puts("asded: save failed\n");
        return 1;
    }
    printf("asded: unknown command ':%s'\n", cmd);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc >= 2) {
        strncpy(g_path, argv[1], PATH_CAP - 1);
        load_file(g_path);
    }

    if (g_nlines == 0) {
        /* Start with one empty line */
        g_lines[0][0] = '\0';
        g_nlines = 1;
    }
    g_cur = 0;

    for (;;) {
        render();

        char key[4];
        int n = readline(key, sizeof(key));
        if (n <= 0) continue;

        char c = key[0];

        switch (c) {
        case 'j':
            if (g_cur < g_nlines - 1) g_cur++;
            break;
        case 'k':
            if (g_cur > 0) g_cur--;
            break;
        case 'G':
            g_cur = g_nlines - 1;
            break;
        case 'g':
            g_cur = 0;
            break;
        case 'i':
            insert_mode(0);
            break;
        case 'I':
            insert_mode(1);
            break;
        case 'd': {
            if (g_nlines == 0) break;
            undo_push(OP_DELETE, g_cur, g_lines[g_cur]);
            memmove(&g_lines[g_cur], &g_lines[g_cur + 1],
                    (size_t)(g_nlines - g_cur - 1) * LINE_CAP);
            g_nlines--;
            if (g_cur >= g_nlines && g_cur > 0) g_cur = g_nlines - 1;
            g_dirty = 1;
            break;
        }
        case ':': {
            int cont = command_mode();
            if (!cont) goto done;
            break;
        }
        case 'q':
            if (!g_dirty) goto done;
            puts("asded: unsaved changes. Use :q! or :wq\n");
            break;
        case 'u':
            if (!do_undo()) puts("asded: nothing to undo\n");
            break;
        default:
            break;
        }
    }

done:
    puts("\033[2J\033[H");
    puts("asded: goodbye.\n");
    asd_exit(0);
    return 0;
}
