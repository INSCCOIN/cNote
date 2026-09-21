#define _GNU_SOURCE
#include "fb.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define MAXL 400
#define MAXC 400
#define MAXV 800

enum { M_NONE, M_FILE, M_EDIT, M_SET, M_ASK };
enum { ASK_QUIT, ASK_NEW, ASK_OPEN };

static char lines[MAXL][MAXC];
static int nlines = 1, cx, cy, dirty;
static char path[512];
static char notesdir[400];

static int font = 1, theme = 1, wrap = 1, tabw = 4;
static int menu = M_NONE, msel, ask, ask_kind;
static char prompt[128], pin[400];
static int pinning, want_quit;

static uint16_t Cbg, Cfg, Cbar, Cbarfg, Cacc, Cdim, Csel, Ccur;

static struct termios oldt;
static int rawon;

static void theme_apply(void)
{
    switch (theme) {
    case 0: /* paper */
        Cbg = rgb565(246, 239, 230);
        Cfg = rgb565(28, 24, 20);
        Cbar = rgb565(52, 44, 40);
        Cbarfg = rgb565(246, 239, 230);
        Cacc = rgb565(180, 90, 70);
        Cdim = rgb565(140, 130, 120);
        Csel = rgb565(220, 200, 170);
        Ccur = rgb565(40, 80, 140);
        break;
    case 2: /* amber */
        Cbg = rgb565(18, 14, 8);
        Cfg = rgb565(240, 180, 70);
        Cbar = rgb565(40, 28, 10);
        Cbarfg = rgb565(240, 200, 90);
        Cacc = rgb565(255, 140, 40);
        Cdim = rgb565(120, 80, 30);
        Csel = rgb565(60, 40, 16);
        Ccur = rgb565(255, 200, 80);
        break;
    case 3: /* high contrast */
        Cbg = rgb565(8, 16, 40);
        Cfg = rgb565(240, 244, 255);
        Cbar = rgb565(200, 210, 230);
        Cbarfg = rgb565(10, 16, 36);
        Cacc = rgb565(80, 140, 255);
        Cdim = rgb565(120, 140, 180);
        Csel = rgb565(30, 50, 90);
        Ccur = rgb565(255, 220, 80);
        break;
    default: /* phosphor */
        Cbg = rgb565(6, 14, 10);
        Cfg = rgb565(90, 220, 120);
        Cbar = rgb565(12, 32, 22);
        Cbarfg = rgb565(160, 255, 180);
        Cacc = rgb565(220, 200, 70);
        Cdim = rgb565(50, 100, 70);
        Csel = rgb565(16, 48, 30);
        Ccur = rgb565(200, 255, 210);
        break;
    }
}

static char *home_dir(void)
{
    const char *h = getenv("HOME");
    struct passwd *pw;
    if (h && *h)
        return (char *)h;
    pw = getpwuid(getuid());
    return pw ? pw->pw_dir : ".";
}

static void cfg_load(void)
{
    char p[512], line[80];
    FILE *f;
    snprintf(p, sizeof p, "%s/.cnote.cfg", home_dir());
    f = fopen(p, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "font=", 5))
            font = atoi(line + 5);
        else if (!strncmp(line, "theme=", 6))
            theme = atoi(line + 6);
        else if (!strncmp(line, "wrap=", 5))
            wrap = atoi(line + 5);
        else if (!strncmp(line, "tab=", 4))
            tabw = atoi(line + 4);
    }
    fclose(f);
    if (font < 1)
        font = 1;
    if (font > 2)
        font = 2;
    if (theme < 0 || theme > 3)
        theme = 1;
    if (tabw != 2 && tabw != 4 && tabw != 8)
        tabw = 4;
}

static void cfg_save(void)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/.cnote.cfg", home_dir());
    f = fopen(p, "w");
    if (!f)
        return;
    fprintf(f, "font=%d\ntheme=%d\nwrap=%d\ntab=%d\n", font, theme, wrap, tabw);
    fclose(f);
}

static void buf_clear(void)
{
    memset(lines, 0, sizeof lines);
    nlines = 1;
    cx = cy = 0;
    dirty = 0;
}

static int load_path(const char *p)
{
    FILE *f;
    char line[MAXC];
    buf_clear();
    snprintf(path, sizeof path, "%s", p);
    f = fopen(p, "r");
    if (!f)
        return 0;
    nlines = 0;
    while (fgets(line, sizeof line, f) && nlines < MAXL) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        snprintf(lines[nlines++], MAXC, "%s", line);
    }
    fclose(f);
    if (nlines == 0)
        nlines = 1;
    dirty = 0;
    cx = cy = 0;
    return 1;
}

static int save_path(const char *p)
{
    FILE *f;
    int i;
    f = fopen(p, "w");
    if (!f)
        return 0;
    for (i = 0; i < nlines; i++) {
        fputs(lines[i], f);
        fputc('\n', f);
    }
    fclose(f);
    snprintf(path, sizeof path, "%s", p);
    dirty = 0;
    return 1;
}

static const char *basename_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* visual wrap table */
static int v_line[MAXV], v_off[MAXV], v_len[MAXV], nv, toprow;

static int cols(void)
{
    int w = (int)FB_W - 8;
    int cw = font_w(font);
    int c = cw ? w / cw : 40;
    if (c < 8)
        c = 8;
    return c;
}

static int rows(void)
{
    int h = (int)FB_H - 16 - 14;
    int ch = font_h(font);
    int r = ch ? h / ch : 20;
    if (r < 4)
        r = 4;
    return r;
}

static void rebuild_vis(void)
{
    int i, cmax = cols();
    nv = 0;
    if (!wrap)
        cmax = MAXC - 1;
    for (i = 0; i < nlines && nv < MAXV; i++) {
        int n = (int)strlen(lines[i]), off = 0;
        if (n == 0) {
            v_line[nv] = i;
            v_off[nv] = 0;
            v_len[nv] = 0;
            nv++;
            continue;
        }
        while (off < n && nv < MAXV) {
            int take = n - off;
            if (take > cmax) {
                int k, brk = cmax;
                if (wrap) {
                    for (k = cmax; k > cmax / 3; k--)
                        if (lines[i][off + k] == ' ') {
                            brk = k + 1;
                            break;
                        }
                }
                take = brk;
            }
            v_line[nv] = i;
            v_off[nv] = off;
            v_len[nv] = take;
            nv++;
            off += take;
        }
    }
    if (nv == 0) {
        v_line[0] = v_off[0] = v_len[0] = 0;
        nv = 1;
    }
}

static int vis_of_cursor(void)
{
    int i;
    for (i = 0; i < nv; i++)
        if (v_line[i] == cy && cx >= v_off[i] && cx <= v_off[i] + v_len[i])
            return i;
    for (i = nv - 1; i >= 0; i--)
        if (v_line[i] == cy)
            return i;
    return 0;
}

static void clamp_cx(void)
{
    int n = (int)strlen(lines[cy]);
    if (cx < 0)
        cx = 0;
    if (cx > n)
        cx = n;
}

static void insert_ch(int ch)
{
    int n, i;
    if (cy < 0 || cy >= nlines)
        return;
    n = (int)strlen(lines[cy]);
    if (n >= MAXC - 2)
        return;
    if (cx > n)
        cx = n;
    memmove(lines[cy] + cx + 1, lines[cy] + cx, (size_t)(n - cx + 1));
    lines[cy][cx++] = (char)ch;
    dirty = 1;
    (void)i;
}

static void insert_tab(void)
{
    int i;
    for (i = 0; i < tabw; i++)
        insert_ch(' ');
}

static void split_line(void)
{
    int n;
    if (nlines >= MAXL)
        return;
    n = (int)strlen(lines[cy]);
    if (cx > n)
        cx = n;
    memmove(lines[cy + 1], lines[cy], (size_t)(nlines - cy) * MAXC);
    nlines++;
    snprintf(lines[cy + 1], MAXC, "%s", lines[cy] + cx);
    lines[cy][cx] = 0;
    cy++;
    cx = 0;
    dirty = 1;
}

static void backspace(void)
{
    int n;
    if (cx > 0) {
        n = (int)strlen(lines[cy]);
        memmove(lines[cy] + cx - 1, lines[cy] + cx, (size_t)(n - cx + 1));
        cx--;
        dirty = 1;
        return;
    }
    if (cy == 0)
        return;
    n = (int)strlen(lines[cy - 1]);
    if (n + (int)strlen(lines[cy]) >= MAXC - 1)
        return;
    cx = n;
    strncat(lines[cy - 1], lines[cy], MAXC - 1 - n);
    memmove(lines[cy], lines[cy + 1], (size_t)(nlines - cy - 1) * MAXC);
    nlines--;
    cy--;
    dirty = 1;
}

static void move_left(void)
{
    if (cx > 0)
        cx--;
    else if (cy > 0) {
        cy--;
        cx = (int)strlen(lines[cy]);
    }
}

static void move_right(void)
{
    int n = (int)strlen(lines[cy]);
    if (cx < n)
        cx++;
    else if (cy + 1 < nlines) {
        cy++;
        cx = 0;
    }
}

static void move_up(void)
{
    int v = vis_of_cursor(), col;
    if (v <= 0)
        return;
    col = cx - v_off[v];
    v--;
    cy = v_line[v];
    cx = v_off[v] + col;
    if (cx > v_off[v] + v_len[v])
        cx = v_off[v] + v_len[v];
    clamp_cx();
}

static void move_down(void)
{
    int v = vis_of_cursor(), col;
    if (v + 1 >= nv)
        return;
    col = cx - v_off[v];
    v++;
    cy = v_line[v];
    cx = v_off[v] + col;
    if (cx > v_off[v] + v_len[v])
        cx = v_off[v] + v_len[v];
    clamp_cx();
}

static void io_open(void)
{
    struct termios t;
    tcgetattr(0, &oldt);
    t = oldt;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    rawon = 1;
}

static void io_close(void)
{
    if (rawon)
        tcsetattr(0, TCSANOW, &oldt);
}

static void status(char *out, size_t n)
{
    snprintf(out, n, "%s%s  L%d C%d  tab%d %s",
             dirty ? "*" : "",
             path[0] ? basename_of(path) : "untitled",
             cy + 1, cx + 1, tabw, wrap ? "wrap" : "nowrap");
}

static void draw_menu_bar(int sc)
{
    const char *tabs[] = {"File", "Edit", "Settings"};
    int ids[] = {M_FILE, M_EDIT, M_SET};
    int i, x = 4;
    fill(0, 0, (int)FB_W, 16, Cbar);
    for (i = 0; i < 3; i++) {
        int w = (int)strlen(tabs[i]) * 6 + 10;
        if (menu == ids[i])
            fill(x - 2, 1, w, 14, Cacc);
        text(x, 4, tabs[i], menu == ids[i] ? Cbg : Cbarfg);
        x += w + 6;
    }
    (void)sc;
}

static void draw_drop(void)
{
    const char *items[8];
    int n = 0, i, w = 150, h, x = 4, y = 16;
    if (menu == M_FILE) {
        items[n++] = "New";
        items[n++] = "Open…";
        items[n++] = "Save";
        items[n++] = "Save as…";
        items[n++] = "Quit  Ctrl+X";
        x = 4;
    } else if (menu == M_EDIT) {
        items[n++] = "Insert tab";
        items[n++] = "Split line";
        x = 46;
    } else if (menu == M_SET) {
        items[n++] = font == 1 ? "Font  small >" : "Font  large >";
        items[n++] = theme == 0 ? "Color paper >" :
                     theme == 1 ? "Color green >" :
                     theme == 2 ? "Color amber >" : "Color night >";
        items[n++] = wrap ? "Wrap  on >" : "Wrap  off >";
        items[n++] = tabw == 2 ? "Indent 2 >" : tabw == 8 ? "Indent 8 >" : "Indent 4 >";
        x = 88;
    } else
        return;
    h = 6 + n * 14;
    fill(x, y, w, h, Csel);
    rect(x, y, w, h, Cacc);
    for (i = 0; i < n; i++) {
        if (i == msel)
            fill(x + 1, y + 3 + i * 14, w - 2, 13, Cacc);
        text(x + 6, y + 6 + i * 14, items[i], i == msel ? Cbg : Cfg);
    }
}

static void draw_ask(void)
{
    int w = 260, h = 70, x, y;
    x = ((int)FB_W - w) / 2;
    y = ((int)FB_H - h) / 2;
    fill(x, y, w, h, Csel);
    rect(x, y, w, h, Cacc);
    text(x + 10, y + 12, "Save changes first?", Cfg);
    text(x + 10, y + 32, "Y save   N discard   Esc stay", Cdim);
}

static void draw(void)
{
    int sc = font, cw = font_w(sc), ch = font_h(sc);
    int rmax = rows(), cmax = cols();
    int i, vcur, y0 = 18;
    char st[96], slice[128];

    rebuild_vis();
    vcur = vis_of_cursor();
    if (vcur < toprow)
        toprow = vcur;
    if (vcur >= toprow + rmax)
        toprow = vcur - rmax + 1;
    if (toprow < 0)
        toprow = 0;

    fb_clear(Cbg);
    draw_menu_bar(sc);

    for (i = 0; i < rmax; i++) {
        int vi = toprow + i, n;
        int y = y0 + i * ch;
        if (vi >= nv)
            break;
        n = v_len[vi];
        if (n > (int)sizeof slice - 1)
            n = (int)sizeof slice - 1;
        memcpy(slice, lines[v_line[vi]] + v_off[vi], (size_t)n);
        slice[n] = 0;
        text_s(4, y, slice, Cfg, sc);
        if (vi == vcur && !pinning && menu == M_NONE && ask == 0) {
            int col = cx - v_off[vi];
            if (col < 0)
                col = 0;
            if (col > cmax)
                col = cmax;
            fill(4 + col * cw, y, cw, ch, Ccur);
            if (slice[col]) {
                char one[2] = {slice[col], 0};
                text_s(4 + col * cw, y, one, Cbg, sc);
            }
        }
    }

    fill(0, (int)FB_H - 14, (int)FB_W, 14, Cbar);
    if (pinning) {
        snprintf(st, sizeof st, "%s%s", prompt, pin);
        text(4, (int)FB_H - 11, st, Cbarfg);
    } else {
        status(st, sizeof st);
        text(4, (int)FB_H - 11, st, Cbarfg);
    }
    if (menu)
        draw_drop();
    if (ask)
        draw_ask();
    fb_flip();
}

static void start_pin(const char *pr, const char *seed)
{
    pinning = 1;
    snprintf(prompt, sizeof prompt, "%s", pr);
    snprintf(pin, sizeof pin, "%s", seed ? seed : "");
}

static void do_new(void)
{
    buf_clear();
    path[0] = 0;
}

static void do_save(void)
{
    if (!path[0]) {
        char def[512];
        snprintf(def, sizeof def, "%s/untitled.txt", notesdir);
        start_pin("save: ", def);
        return;
    }
    if (!save_path(path))
        start_pin("save failed — path: ", path);
}

static void try_open_name(void)
{
    char full[512];
    if (strchr(pin, '/'))
        snprintf(full, sizeof full, "%s", pin);
    else
        snprintf(full, sizeof full, "%s/%s", notesdir, pin);
    if (!load_path(full))
        start_pin("can't open, name: ", pin);
}

static void menu_run(int enter)
{
    if (menu == M_FILE) {
        if (!enter)
            return;
        if (msel == 0) {
            if (dirty) {
                ask = 1;
                ask_kind = ASK_NEW;
            } else
                do_new();
            menu = M_NONE;
        } else if (msel == 1) {
            start_pin("open: ", "");
            menu = M_NONE;
        } else if (msel == 2) {
            do_save();
            menu = M_NONE;
        } else if (msel == 3) {
            start_pin("save as: ", path[0] ? path : "");
            menu = M_NONE;
        } else if (msel == 4) {
            menu = M_NONE;
            if (dirty) {
                ask = 1;
                ask_kind = ASK_QUIT;
            } else
                want_quit = 1;
        }
    } else if (menu == M_EDIT) {
        if (!enter)
            return;
        if (msel == 0)
            insert_tab();
        if (msel == 1)
            split_line();
        menu = M_NONE;
    } else if (menu == M_SET) {
        if (!enter)
            return;
        if (msel == 0)
            font = font == 1 ? 2 : 1;
        else if (msel == 1)
            theme = (theme + 1) % 4;
        else if (msel == 2)
            wrap = !wrap;
        else if (msel == 3)
            tabw = tabw == 2 ? 4 : tabw == 4 ? 8 : 2;
        theme_apply();
        cfg_save();
    }
}

static void handle_ask(unsigned char c)
{
    if (c == 'y' || c == 'Y') {
        ask = 0;
        if (!path[0]) {
            char def[512];
            snprintf(def, sizeof def, "%s/untitled.txt", notesdir);
            start_pin("save: ", def);
            /* after pin, user still needs to confirm quit — keep kind */
            return;
        }
        save_path(path);
        if (ask_kind == ASK_QUIT)
            want_quit = 1;
        else if (ask_kind == ASK_NEW)
            do_new();
    } else if (c == 'n' || c == 'N') {
        ask = 0;
        if (ask_kind == ASK_QUIT)
            want_quit = 1;
        else if (ask_kind == ASK_NEW)
            do_new();
    } else if (c == 27)
        ask = 0;
}

static void handle_pin(unsigned char c)
{
    size_t n = strlen(pin);
    if (c == 13 || c == 10) {
        pinning = 0;
        if (!strncmp(prompt, "open", 4))
            try_open_name();
        else if (!strncmp(prompt, "save", 4)) {
            if (strchr(pin, '/'))
                save_path(pin);
            else {
                char full[512];
                snprintf(full, sizeof full, "%s/%s", notesdir, pin);
                save_path(full);
            }
        }
        return;
    }
    if (c == 27) {
        pinning = 0;
        return;
    }
    if ((c == 8 || c == 127) && n) {
        pin[n - 1] = 0;
        return;
    }
    if (c >= 32 && c < 127 && n + 1 < sizeof pin) {
        pin[n] = (char)c;
        pin[n + 1] = 0;
    }
}

static void handle(unsigned char c, unsigned char *seq, int slen)
{
    if (ask) {
        handle_ask(c);
        return;
    }
    if (pinning) {
        handle_pin(c);
        return;
    }
    if (c == 24) { /* Ctrl+X */
        if (dirty) {
            ask = 1;
            ask_kind = ASK_QUIT;
        } else
            want_quit = 1;
        return;
    }
    if (c == 19) { /* Ctrl+S */
        do_save();
        return;
    }
    if (c == 15) { /* Ctrl+O */
        start_pin("open: ", "");
        return;
    }
    if (c == 14) { /* Ctrl+N */
        if (dirty) {
            ask = 1;
            ask_kind = ASK_NEW;
        } else
            do_new();
        return;
    }
    if (c == 27 && slen >= 3 && seq[1] == '[') {
        if (menu) {
            if (seq[2] == 'A' && msel > 0)
                msel--;
            else if (seq[2] == 'B')
                msel++;
            else if (seq[2] == 'C') {
                menu = menu == M_FILE ? M_EDIT : menu == M_EDIT ? M_SET : M_FILE;
                msel = 0;
            } else if (seq[2] == 'D') {
                menu = menu == M_SET ? M_EDIT : menu == M_EDIT ? M_FILE : M_SET;
                msel = 0;
            }
            if (menu == M_FILE && msel > 4)
                msel = 4;
            if (menu == M_EDIT && msel > 1)
                msel = 1;
            if (menu == M_SET && msel > 3)
                msel = 3;
            return;
        }
        if (seq[2] == 'A')
            move_up();
        else if (seq[2] == 'B')
            move_down();
        else if (seq[2] == 'C')
            move_right();
        else if (seq[2] == 'D')
            move_left();
        else if (seq[2] == 'H')
            cx = 0;
        else if (seq[2] == 'F')
            cx = (int)strlen(lines[cy]);
        return;
    }
    if (c == 27) {
        if (menu)
            menu = M_NONE;
        else {
            menu = M_FILE;
            msel = 0;
        }
        return;
    }
    if (menu) {
        if (c == 13 || c == 10)
            menu_run(1);
        else if (c == 9) {
            menu = menu == M_FILE ? M_EDIT : menu == M_EDIT ? M_SET : M_FILE;
            msel = 0;
        }
        return;
    }
    if (c == 9) {
        insert_tab();
        return;
    }
    if (c == 13 || c == 10) {
        split_line();
        return;
    }
    if (c == 8 || c == 127) {
        backspace();
        return;
    }
    if (c >= 32 && c < 127)
        insert_ch(c);
}

int main(int argc, char **argv)
{
    snprintf(notesdir, sizeof notesdir, "%s/notes", home_dir());
    mkdir(notesdir, 0755);
    cfg_load();
    theme_apply();
    buf_clear();
    if (argc > 1)
        load_path(argv[1]);
    else {
        char def[512];
        snprintf(def, sizeof def, "%s/scratch.txt", notesdir);
        if (!load_path(def))
            snprintf(path, sizeof path, "%s", def);
    }
    if (fb_open() < 0) {
        fprintf(stderr, "cNote needs /dev/fb0\n");
        return 1;
    }
    io_open();
    while (!want_quit) {
        unsigned char b[16];
        int n = (int)read(0, b, sizeof b), i;
        draw();
        if (n <= 0) {
            usleep(20000);
            continue;
        }
        for (i = 0; i < n; i++) {
            if (b[i] == 27 && i + 2 < n) {
                handle(b[i], b + i, n - i);
                i += 2;
            } else
                handle(b[i], b + i, 1);
        }
    }
    io_close();
    fb_close();
    return 0;
}
