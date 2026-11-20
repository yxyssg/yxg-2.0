#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "dwmap.h"

#define MAX_FUNCS 4096
#define MAX_ENTRIES 200000

typedef struct {
    char name[128];
    unsigned int start;
    int have_start;
} DbgFunc;

typedef struct {
    unsigned int addr;
    int line;
} DbgEntry;

#define MAX_LOCALS 4096
typedef struct { char func[128]; int off; char name[64]; } DbgLocal;
static DbgLocal g_locals[MAX_LOCALS];
static int      g_local_count = 0;

static DbgFunc  g_funcs[MAX_FUNCS];
static int      g_func_count = 0;
static DbgEntry g_raw[MAX_ENTRIES];
static int      g_raw_count = 0;
static unsigned int g_raw_func[MAX_ENTRIES];

static DbgFunc* find_or_add_func(const char* name) {
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) return &g_funcs[i];
    }
    if (g_func_count >= MAX_FUNCS) return NULL;
    DbgFunc* f = &g_funcs[g_func_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->have_start = 0;
    return f;
}

static int parse_listing_offset(const char* line, unsigned int* out) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != ' ') return 0;
    while (*p == ' ') p++;
    unsigned int v = 0;
    int n = 0;
    while (isxdigit((unsigned char)*p) && n < 8) {
        char c = *p;
        v = (v << 4) | (unsigned int)(isdigit((unsigned char)c) ? c - '0' : (tolower(c) - 'a' + 10));
        p++; n++;
    }
    if (n != 8) return 0;
    *out = v;
    return 1;
}

static int parse_listing_label(const char* line, char* name, int cap) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    while (*p == ' ') p++;
    if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) &&
        isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3]) &&
        p[8] == ' ') return 0;
    const char* start = p;
    const char* q = p;
    while (*q && *q != ':' && *q != ' ' && *q != '\t') q++;
    if (*q != ':') return 0;
    int n = (int)(q - start);
    if (n <= 0 || n >= cap) return 0;
    if (!(isalpha((unsigned char)start[0]) || start[0] == '_')) return 0;
    memcpy(name, start, (size_t)n);
    name[n] = '\0';
    return 1;
}

static int read_exe_symbols(const char* path, DbgFunc* out, int cap, int* out_count) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)512 * 1024 * 1024) { fclose(f); return 0; }

    unsigned char* buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);

    unsigned int pe = *(unsigned int*)(buf + 0x3C);

    unsigned short nsec = *(unsigned short*)(buf + pe + 6);
    unsigned int symptr = *(unsigned int*)(buf + pe + 12);
    unsigned int nsyms  = *(unsigned int*)(buf + pe + 16);

    if (symptr == 0 || nsyms == 0 || symptr + nsyms * 18 > (unsigned int)sz) {
        free(buf);
        return 0;
    }

    unsigned short optsz = *(unsigned short*)(buf + pe + 20);
    unsigned int sec_tab = pe + 24 + optsz;

    int cnt = 0; int seen_text = 0, seen_ext = 0;
    for (unsigned int i = 0; i < nsyms && cnt < cap; i++) {
        unsigned char* s = buf + symptr + i * 18;
        unsigned int value   = *(unsigned int*)(s + 8);
        short        secnum  = *(short*)(s + 12);
        unsigned int auxcnt  = s[17];
        unsigned char sclass = s[16];
        (void)seen_ext;
        if (secnum <= 0) continue;
        if (sclass != 2 && sclass != 3) continue;

        const char* nm;
        char namebuf[9];
        if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0) {
            unsigned int stroff = *(unsigned int*)(s + 4);
            unsigned int base = symptr + nsyms * 18;
            if (base + stroff >= (unsigned int)sz) continue;
            nm = (const char*)(buf + base + stroff);
        } else {
            memcpy(namebuf, s, 8);
            namebuf[8] = '\0';
            nm = namebuf;
        }
        unsigned int text_va = 0;
        if ((unsigned int)secnum - 1 < nsec) {
            unsigned char* sh = buf + sec_tab + (unsigned int)(secnum - 1) * 40;
            if (strncmp((char*)sh, ".text", 5) != 0) continue;
            text_va = *(unsigned int*)(sh + 12);
        } else {
            continue;
        }
        seen_text++; strncpy(out[cnt].name, nm, sizeof(out[cnt].name) - 1);
        out[cnt].name[sizeof(out[cnt].name) - 1] = '\0';
        out[cnt].start = text_va + value;
        out[cnt].have_start = 1;
        cnt++;
        i += auxcnt;
    }
    free(buf);
    *out_count = cnt;
    return 1;
}

int dwmap_generate(const char* lst_path, const char* exe_path, const char* map_path, const char* src_name) {
    FILE* in = fopen(lst_path, "r");
    if (!in) {
        fprintf(stderr, "warning: cannot open %s for debug info\n", lst_path);
        return -1;
    }

    char line[4096];
    DbgFunc* cur = NULL;
    int pending_line = 0;
    int last_line = -1;
    g_func_count = 0;
    g_raw_count = 0;

    while (fgets(line, sizeof(line), in)) {
        char name[128];
        if (parse_listing_label(line, name, sizeof(name))) {
            cur = find_or_add_func(name);
            last_line = -1;
            continue;
        }
        const char* ml = strstr(line, ";@local ");
        if (ml) {
            if (g_local_count < MAX_LOCALS) {
                char fn[128]; int off2 = 0; char nm[64];
                if (sscanf(ml + 8, "%127s %d %63s", fn, &off2, nm) == 3) {
                    DbgLocal* d = &g_locals[g_local_count++];
                    strncpy(d->func, fn, sizeof(d->func) - 1);
                    d->func[sizeof(d->func) - 1] = 0;
                    d->off = off2;
                    strncpy(d->name, nm, sizeof(d->name) - 1);
                    d->name[sizeof(d->name) - 1] = 0;
                }
            }
            continue;
        }
        const char* mk = strstr(line, ";@yxgline");
        if (mk) {
            int v = 0;
            const char* q = mk + 9;
            while (*q == ' ') q++;
            if (sscanf(q, "%d", &v) == 1) pending_line = v;
            continue;
        }
        unsigned int off;
        if (cur && parse_listing_offset(line, &off)) {
            if (!cur->have_start) { cur->start = off; cur->have_start = 1; }
            if (pending_line > 0 && g_raw_count < MAX_ENTRIES &&
                pending_line != last_line) {
                g_raw[g_raw_count].addr = off;
                g_raw[g_raw_count].line = pending_line;
                g_raw_func[g_raw_count] = (unsigned int)(cur - g_funcs);
                g_raw_count++;
                last_line = pending_line;
            }
        }
    }
    fclose(in);

    static DbgFunc exe_syms[MAX_FUNCS];
    int exe_count = 0;
    if (!read_exe_symbols(exe_path, exe_syms, MAX_FUNCS, &exe_count) || exe_count == 0) {
        fprintf(stderr, "warning: no symbol table in %s (debug info skipped)\n", exe_path);
        return -1;
    }

    FILE* out = fopen(map_path, "w");
    if (!out) {
        fprintf(stderr, "warning: cannot write %s\n", map_path);
        return -1;
    }
    fprintf(out, "# yxg dbgmap v1\n");
    if (src_name && *src_name) fprintf(out, "# src %s\n", src_name);

    int written = 0;
    for (int i = 0; i < g_raw_count; i++) {
        DbgFunc* f = &g_funcs[g_raw_func[i]];
        unsigned int linked = 0;
        int found = 0;
        for (int k = 0; k < exe_count; k++) {
            if (strcmp(exe_syms[k].name, f->name) == 0) {
                linked = exe_syms[k].start;
                found = 1;
                break;
            }
        }
        if (!found || !f->have_start) continue;
        unsigned int delta = linked - f->start;
        fprintf(out, "%08x %d %s\n", g_raw[i].addr + delta, g_raw[i].line, f->name);
        written++;
    }
    for (int li = 0; li < g_local_count; li++)
        fprintf(out, "# local %s %d %s\n", g_locals[li].func, g_locals[li].off, g_locals[li].name);
    fclose(out);


    fprintf(stderr, "debug info: %d line mappings, %d locals -> %s\n",
            written, g_local_count, map_path);
    return written > 0 ? 0 : -1;
}
