#include "CFG.h"
#include "Error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ARRAY_INIT_CAP 4

#define ARRAY_ENSURE(arr, cnt, cap, type) do {                     \
    if ((cnt) >= (cap)) {                                          \
        (cap) = ((cap) == 0) ? ARRAY_INIT_CAP : (cap) * 2;        \
        (arr) = realloc((arr), (size_t)(cap) * sizeof(type));      \
        if (!(arr)) OUT_OF_MEMORY();                               \
    }                                                              \
} while(0)

typedef struct LabelMap {
    char* name;
    BasicBlock* bb;
    struct LabelMap* next;
} LabelMap;

static LabelMap* label_map_build(CFG* cfg) {
    LabelMap* head = NULL;
    for (int i = 0; i < cfg->block_count; i++) {
        BasicBlock* bb = cfg->blocks[i];
        if (bb->first_ir && bb->first_ir->type == IR_LABEL
            && bb->first_ir->args1) {
            LabelMap* lm = malloc(sizeof(LabelMap));
            if (!lm) OUT_OF_MEMORY();
            lm->name = strdup(bb->first_ir->args1);
            lm->bb = bb;
            lm->next = head;
            head = lm;
        }
    }
    return head;
}

static BasicBlock* label_map_find(LabelMap* map, const char* name) {
    for (LabelMap* lm = map; lm; lm = lm->next)
        if (strcmp(lm->name, name) == 0) return lm->bb;
    return NULL;
}

static void label_map_free(LabelMap* map) {
    while (map) {
        LabelMap* n = map->next;
        free(map);
        map = n;
    }
}

static bool ir_is_terminator(const IR* ir) {
    if (!ir) return false;
    return ir->type == IR_JMP  || ir->type == IR_JMPZ ||
           ir->type == IR_JMPNZ || ir->type == IR_RET;
}

static bool ir_is_label(const IR* ir) {
    return ir && ir->type == IR_LABEL;
}

static bool str_is_number(const char* s) {
    if (!s || !*s) return false;
    if (*s == '-') s++;
    if (!*s) return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s)) return false;
    return true;
}

static bool looks_like_label(const char* s) {
    if (!s || !*s) return false;
    if (s[0] == '_' || s[0] == '.') return false;
    if (s[0] >= 'A' && s[0] <= 'Z') return true;
    return false;
}

static void collect_block_vars(BasicBlock* bb) {
    IR* ir = bb->first_ir;
    while (ir) {
        if (ir->result && ir->result[0] && !str_is_number(ir->result)
            && !looks_like_label(ir->result)) {
            ARRAY_ENSURE(bb->def_names, bb->def_count,
                         bb->def_capacity, char*);
            bb->def_names[bb->def_count++] = ir->result;
        }

        if (ir->args1 && ir->args1[0] && !str_is_number(ir->args1)
            && !looks_like_label(ir->args1)
            && !(ir->type == IR_ADDRESS_OF && ir->is_function_addr)) {
            if (ir->type != IR_JMP && ir->type != IR_LABEL) {
                ARRAY_ENSURE(bb->use_names, bb->use_count,
                             bb->use_capacity, char*);
                bb->use_names[bb->use_count++] = ir->args1;
            }
        }

        if (ir->args2 && ir->args2[0] && !str_is_number(ir->args2)
            && !looks_like_label(ir->args2)) {
            if (ir->type != IR_JMPZ && ir->type != IR_JMPNZ) {
                ARRAY_ENSURE(bb->use_names, bb->use_count,
                             bb->use_capacity, char*);
                bb->use_names[bb->use_count++] = ir->args2;
            }
        }

        if (ir == bb->last_ir) break;
        ir = ir->next;
    }
}

static void add_edge(BasicBlock* from, BasicBlock* to) {
    ARRAY_ENSURE(from->succs, from->succ_count, from->succ_capacity,
                 BasicBlock*);
    from->succs[from->succ_count++] = to;

    ARRAY_ENSURE(to->preds, to->pred_count, to->pred_capacity,
                 BasicBlock*);
    to->preds[to->pred_count++] = from;
}

CFG* cfg_build(IR* func_ir_start, IR* func_ir_end) {
    if (!func_ir_start) return NULL;

    CFG* cfg = calloc(1, sizeof(CFG));
    if (!cfg) OUT_OF_MEMORY();
    cfg->block_capacity = 16;
    cfg->blocks = calloc((size_t)cfg->block_capacity, sizeof(BasicBlock*));
    if (!cfg->blocks) OUT_OF_MEMORY();

    typedef struct { IR* ir; bool leader; } IRMark;
    int total = 0, cap = 64;
    IRMark* marks = malloc((size_t)cap * sizeof(IRMark));
    if (!marks) OUT_OF_MEMORY();

    bool next_is_leader = true;
    for (IR* ir = func_ir_start;
         ir && ir != (func_ir_end ? func_ir_end->next : NULL);
         ir = ir->next) {
        if (total >= cap) { cap *= 2; marks = realloc(marks, (size_t)cap * sizeof(IRMark)); }
        marks[total].ir     = ir;
        marks[total].leader = next_is_leader || ir_is_label(ir);
        if (marks[total].leader) next_is_leader = false;
        if (ir_is_terminator(ir)) next_is_leader = true;
        total++;
    }

    int bb_id = 0;
    int i = 0;
    while (i < total) {
        if (!marks[i].leader) { i++; continue; }

        int end = i;
        while (end + 1 < total && !marks[end + 1].leader) end++;

        BasicBlock* bb = calloc(1, sizeof(BasicBlock));
        if (!bb) OUT_OF_MEMORY();
        bb->id       = bb_id++;
        bb->first_ir = marks[i].ir;
        bb->last_ir  = marks[end].ir;
        bb->ir_count = end - i + 1;

        collect_block_vars(bb);

        if (cfg->block_count >= cfg->block_capacity) {
            cfg->block_capacity *= 2;
            cfg->blocks = realloc(cfg->blocks,
                (size_t)cfg->block_capacity * sizeof(BasicBlock*));
            if (!cfg->blocks) OUT_OF_MEMORY();
        }
        cfg->blocks[cfg->block_count++] = bb;

        i = end + 1;
    }

    free(marks);

    LabelMap* lab_map = label_map_build(cfg);

    for (int bi = 0; bi < cfg->block_count; bi++) {
        BasicBlock* bb = cfg->blocks[bi];
        IR* term = bb->last_ir;

        if (term->type == IR_JMP) {
            BasicBlock* target = label_map_find(lab_map, term->args1);
            if (target) add_edge(bb, target);
        }
        else if (term->type == IR_JMPZ || term->type == IR_JMPNZ) {
            BasicBlock* target = label_map_find(lab_map, term->args2);
            if (target) add_edge(bb, target);
            if (bi + 1 < cfg->block_count)
                add_edge(bb, cfg->blocks[bi + 1]);
        }
        else if (term->type != IR_RET) {
            if (bi + 1 < cfg->block_count)
                add_edge(bb, cfg->blocks[bi + 1]);
        }
    }

    label_map_free(lab_map);

    cfg->entry = (cfg->block_count > 0) ? cfg->blocks[0] : NULL;

    for (int bi = 0; bi < cfg->block_count; bi++) {
        BasicBlock* bb = cfg->blocks[bi];
        for (int j = 0; j < bb->def_count; j++) {
            bool found = false;
            for (int k = 0; k < cfg->var_count; k++)
                if (strcmp(cfg->var_names[k], bb->def_names[j]) == 0)
                { found = true; break; }
            if (!found) {
                ARRAY_ENSURE(cfg->var_names, cfg->var_count,
                             cfg->var_capacity, char*);
                cfg->var_names[cfg->var_count++] = strdup(bb->def_names[j]);
            }
        }
        for (int j = 0; j < bb->use_count; j++) {
            bool found = false;
            for (int k = 0; k < cfg->var_count; k++)
                if (strcmp(cfg->var_names[k], bb->use_names[j]) == 0)
                { found = true; break; }
            if (!found) {
                ARRAY_ENSURE(cfg->var_names, cfg->var_count,
                             cfg->var_capacity, char*);
                cfg->var_names[cfg->var_count++] = strdup(bb->use_names[j]);
            }
        }
    }

    return cfg;
}

void cfg_assign_linear_ids(CFG* cfg) {
    int gid = 0;
    for (int i = 0; i < cfg->block_count; i++) {
        cfg->blocks[i]->linear_start = gid;
        gid += cfg->blocks[i]->ir_count;
        cfg->blocks[i]->linear_end = gid - 1;
    }
}

void cfg_compute_dominators(CFG* cfg) {
    int n = cfg->block_count;
    if (n == 0) return;

    for (int i = 0; i < n; i++) {
        cfg->blocks[i]->dom_child_count = 0;
        cfg->blocks[i]->idom = NULL;
    }

    bool** dom = malloc((size_t)n * sizeof(bool*));
    for (int i = 0; i < n; i++) {
        dom[i] = malloc((size_t)n * sizeof(bool));
        for (int j = 0; j < n; j++) dom[i][j] = true;
    }
    for (int j = 0; j < n; j++) dom[0][j] = (j == 0);

    bool changed;
    do {
        changed = false;
        for (int i = 1; i < n; i++) {
            bool* new_dom = malloc((size_t)n * sizeof(bool));
            for (int j = 0; j < n; j++) new_dom[j] = true;

            BasicBlock* bb = cfg->blocks[i];
            if (bb->pred_count > 0) {
                int p0 = bb->preds[0]->id;
                for (int j = 0; j < n; j++) new_dom[j] = dom[p0][j];
                for (int p = 1; p < bb->pred_count; p++) {
                    int pid = bb->preds[p]->id;
                    for (int j = 0; j < n; j++)
                        new_dom[j] = new_dom[j] && dom[pid][j];
                }
            }
            new_dom[i] = true;

            for (int j = 0; j < n; j++) {
                if (dom[i][j] != new_dom[j]) { changed = true; break; }
            }
            free(dom[i]);
            dom[i] = new_dom;
        }
    } while (changed);

    for (int i = 0; i < n; i++) {
        BasicBlock* bb = cfg->blocks[i];
        bb->idom = NULL;
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            if (!dom[i][j]) continue;
            bool immediate = true;
            for (int k = 0; k < n; k++) {
                if (k == i || k == j) continue;
                if (dom[i][k] && dom[k][j]) { immediate = false; break; }
            }
            if (immediate) { bb->idom = cfg->blocks[j]; break; }
        }
    }

    for (int i = 0; i < n; i++) {
        BasicBlock* bb = cfg->blocks[i];
        if (bb->idom) {
            BasicBlock* p = bb->idom;
            ARRAY_ENSURE(p->dom_children, p->dom_child_count,
                         p->dom_child_capacity, BasicBlock*);
            p->dom_children[p->dom_child_count++] = bb;
        }
    }

    for (int i = 0; i < n; i++) free(dom[i]);
    free(dom);
}

void cfg_remove_unreachable(CFG* cfg) {
    if (!cfg || cfg->block_count == 0) return;
    int n = cfg->block_count;

    bool* reachable = calloc((size_t)n, sizeof(bool));
    if (!reachable) OUT_OF_MEMORY();
    BasicBlock** queue = malloc(sizeof(BasicBlock*) * (size_t)n);
    if (!queue) { free(reachable); OUT_OF_MEMORY(); }
    int head = 0, tail = 0;
    queue[tail++] = cfg->blocks[0];
    reachable[0] = true;
    while (head < tail) {
        BasicBlock* bb = queue[head++];
        for (int i = 0; i < bb->succ_count; i++) {
            BasicBlock* succ = bb->succs[i];
            if (!succ) continue;
            int sid = succ->id;
            if (!reachable[sid]) {
                reachable[sid] = true;
                queue[tail++] = succ;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        if (!reachable[i]) continue;
        BasicBlock* bb = cfg->blocks[i];

        int new_sc = 0;
        for (int j = 0; j < bb->succ_count; j++) {
            BasicBlock* succ = bb->succs[j];
            if (succ && reachable[succ->id]) {
                bb->succs[new_sc++] = succ;
            }
        }
        bb->succ_count = new_sc;

        int new_pc = 0;
        for (int j = 0; j < bb->pred_count; j++) {
            BasicBlock* pred = bb->preds[j];
            if (pred && reachable[pred->id]) {
                bb->preds[new_pc++] = pred;
            }
        }
        bb->pred_count = new_pc;
    }

    for (int i = 0; i < n; i++) {
        if (!reachable[i]) {
            BasicBlock* bb = cfg->blocks[i];
            free(bb->succs);
            free(bb->preds);
            free(bb->def_names);
            free(bb->use_names);
            free(bb);
            cfg->blocks[i] = NULL;
        }
    }

    int new_count = 0;
    for (int i = 0; i < n; i++) {
        if (cfg->blocks[i] != NULL) {
            cfg->blocks[i]->id = new_count;
            cfg->blocks[new_count] = cfg->blocks[i];
            new_count++;
        }
    }
    cfg->block_count = new_count;
    free(reachable);
    free(queue);
}

void cfg_compute_dominance_frontiers(CFG* cfg) {
    int n = cfg->block_count;
    for (int i = 0; i < n; i++)
        cfg->blocks[i]->df_count = 0;

    for (int i = 0; i < n; i++) {
        BasicBlock* bb = cfg->blocks[i];
        if (bb->pred_count < 2) continue;

        for (int p = 0; p < bb->pred_count; p++) {
            BasicBlock* runner = bb->preds[p];
            while (runner && runner != bb->idom) {
                bool already = false;
                for (int d = 0; d < runner->df_count; d++)
                    if (runner->df[d] == bb) { already = true; break; }
                if (!already) {
                    ARRAY_ENSURE(runner->df, runner->df_count,
                                 runner->df_capacity, BasicBlock*);
                    runner->df[runner->df_count++] = bb;
                }
                runner = runner->idom;
            }
        }
    }
}

static int var_name_index(const CFG* cfg, const char* name) {
    for (int i = 0; i < cfg->var_count; i++)
        if (strcmp(cfg->var_names[i], name) == 0) return i;
    return -1;
}

void cfg_liveness_analysis(CFG* cfg) {
    int n = cfg->block_count;
    int v = cfg->var_count;
    if (v == 0) return;

    for (int i = 0; i < n; i++) {
        BasicBlock* bb = cfg->blocks[i];
        bb->live_gen  = calloc((size_t)v, sizeof(bool));
        bb->live_kill = calloc((size_t)v, sizeof(bool));
        bb->live_in   = calloc((size_t)v, sizeof(bool));
        bb->live_out  = calloc((size_t)v, sizeof(bool));
    }

    for (int bi = 0; bi < n; bi++) {
        BasicBlock* bb = cfg->blocks[bi];

        bool* def_seen = calloc((size_t)v, sizeof(bool));
        if (!def_seen) OUT_OF_MEMORY();

        IR* ir = bb->first_ir;
        while (ir) {
            const char* operands[2] = { ir->args1, ir->args2 };
            for (int op_idx = 0; op_idx < 2; op_idx++) {
                const char* op = operands[op_idx];
                if (!op || !op[0]) continue;
                if (str_is_number(op)) continue;
                if (looks_like_label(op)) continue;
                if (ir->type == IR_JMP  || ir->type == IR_LABEL) continue;
                if ((ir->type == IR_JMPZ || ir->type == IR_JMPNZ)
                    && op_idx == 1) continue;

                int vi = var_name_index(cfg, op);
                if (vi >= 0 && !def_seen[vi]) {
                    bb->live_gen[vi] = true;
                }
            }

            if (ir->result && ir->result[0]
                && !str_is_number(ir->result)
                && !looks_like_label(ir->result)) {
                int vi = var_name_index(cfg, ir->result);
                if (vi >= 0) {
                    bb->live_kill[vi] = true;
                    def_seen[vi] = true;
                }
            }

            if (ir == bb->last_ir) break;
            ir = ir->next;
        }
        free(def_seen);
    }

    bool* new_out = malloc((size_t)v * sizeof(bool));
    bool* new_in  = malloc((size_t)v * sizeof(bool));
    if (!new_out || !new_in) OUT_OF_MEMORY();

    bool changed;
    do {
        changed = false;

        for (int i = n - 1; i >= 0; i--) {
            BasicBlock* bb = cfg->blocks[i];

            memset(new_out, 0, (size_t)v * sizeof(bool));
            for (int s = 0; s < bb->succ_count; s++) {
                BasicBlock* succ = bb->succs[s];
                for (int j = 0; j < v; j++) {
                    new_out[j] = new_out[j] || succ->live_in[j];
                }
            }

            for (int j = 0; j < v; j++) {
                new_in[j] = bb->live_gen[j]
                         || (new_out[j] && !bb->live_kill[j]);

                if (new_in[j]  != bb->live_in[j])  changed = true;
                if (new_out[j] != bb->live_out[j]) changed = true;
            }

            memcpy(bb->live_out, new_out, (size_t)v * sizeof(bool));
            memcpy(bb->live_in,  new_in,  (size_t)v * sizeof(bool));
        }
    } while (changed);

    free(new_out);
    free(new_in);
}

static bool ir_uses_var(const IR* ir, const char* v) {
    if (!ir || !v || !v[0]) return false;
    if (ir->type == IR_LABEL || ir->type == IR_PARAM_DEF) return false;
    if (ir->args1 && strcmp(ir->args1, v) == 0) return true;
    if (ir->args2 && strcmp(ir->args2, v) == 0) return true;
    if (ir->type == IR_PHI && ir->phi_arg_names) {
        for (int i = 0; i < ir->phi_arg_count; i++) {
            if (ir->phi_arg_names[i] && strcmp(ir->phi_arg_names[i], v) == 0)
                return true;
        }
    }
    return false;
}

void insert_gc_clears(CFG* cfg) {
    if (!cfg || cfg->block_count == 0) return;

    for (int i = 0; i < cfg->block_count; i++) {
        BasicBlock* bb = cfg->blocks[i];
        free(bb->live_gen);  bb->live_gen  = NULL;
        free(bb->live_kill); bb->live_kill = NULL;
        free(bb->live_in);   bb->live_in   = NULL;
        free(bb->live_out);  bb->live_out  = NULL;
    }

    cfg_compute_dominators(cfg);
    cfg_liveness_analysis(cfg);

    int n = cfg->block_count;

    for (int bi = 0; bi < n; bi++) {
        BasicBlock* bb = cfg->blocks[bi];
        if (!bb->first_ir) continue;

        IR* term = bb->last_ir;
        if (term && term->type == IR_RET) continue;

        typedef struct { char* name; } Def;
        Def* defs = NULL;
        int def_count = 0, def_cap = 0;

        IR* ir = bb->first_ir;
        while (ir) {
            if (ir->result && ir->result[0]
                && ir->type != IR_LABEL && ir->type != IR_PARAM_DEF
                && ir->type != IR_PHI
                && strcmp(ir->result, "0") != 0
                && strncmp(ir->result, "_var", 4) != 0
                && ir->result[0] != '.') {
                if (def_count >= def_cap) {
                    def_cap = def_cap ? def_cap * 2 : 16;
                    defs = realloc(defs, sizeof(Def) * def_cap);
                }
                defs[def_count++].name = ir->result;
                }
            if (ir == bb->last_ir) break;
            ir = ir->next;
        }

        if (def_count == 0) continue;

        IR* insert_prev = NULL;
        IR* scan = bb->first_ir;
        while (scan && scan != term) {
            insert_prev = scan;
            scan = scan->next;
        }

        for (int di = 0; di < def_count; di++) {
            const char* v = defs[di].name;
            bool used_elsewhere = false;

            for (int bj = 0; bj < n && !used_elsewhere; bj++) {
                if (bj == bi) continue;
                BasicBlock* ob = cfg->blocks[bj];
                IR* oi = ob->first_ir;
                while (oi) {
                    if (ir_uses_var(oi, v)) {
                        used_elsewhere = true;
                        break;
                    }
                    if (oi == ob->last_ir) break;
                    oi = oi->next;
                }
            }
            if (used_elsewhere) continue;

            IR* clear_ir = create_IR(IR_VAR_ASS);
            clear_ir->result = strdup(v);
            clear_ir->args1 = strdup("0");
            clear_ir->args1_type = DATA_INT;
            clear_ir->data_type = DATA_INT;
            assign_def_id(clear_ir);

            if (term) {
                clear_ir->next = term;
                if (insert_prev) {
                    insert_prev->next = clear_ir;
                } else {
                    bb->first_ir = clear_ir;
                }
                insert_prev = clear_ir;
            } else {
                if (bb->last_ir) {
                    bb->last_ir->next = clear_ir;
                    clear_ir->next = NULL;
                    bb->last_ir = clear_ir;
                } else {
                    bb->first_ir = bb->last_ir = clear_ir;
                }
            }
            bb->ir_count++;
        }

        free(defs);
    }
}

void cfg_print(const CFG* cfg) {
    if (!cfg) { printf("CFG: (null)\n"); return; }
    printf("CFG: %d blocks, %d variables\n", cfg->block_count, cfg->var_count);
    for (int i = 0; i < cfg->block_count; i++) {
        const BasicBlock* bb = cfg->blocks[i];
        printf("  BB%d: ir=%d..%d  succs=[", bb->id,
               bb->linear_start, bb->linear_end);
        for (int j = 0; j < bb->succ_count; j++) {
            if (j) printf(",");
            printf("BB%d", bb->succs[j]->id);
        }
        printf("]  preds=[");
        for (int j = 0; j < bb->pred_count; j++) {
            if (j) printf(",");
            printf("BB%d", bb->preds[j]->id);
        }
        printf("]  idom=");
        printf("%s", bb->idom ? "BB" : "(none)");
        if (bb->idom) printf("%d", bb->idom->id);
        printf("\n");
    }
    fflush(stdout);
}

void cfg_destroy(CFG* cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->block_count; i++) {
        BasicBlock* bb = cfg->blocks[i];
        free(bb->succs);
        free(bb->preds);
        free(bb->dom_children);
        free(bb->df);
        free(bb->live_gen);
        free(bb->live_kill);
        free(bb->live_in);
        free(bb->live_out);
        free(bb->def_names);
        free(bb->use_names);
        free(bb);
    }
    free(cfg->blocks);
    if (cfg->intervals) {
        free(cfg->intervals);
        cfg->intervals = NULL;
    }
    for (int i = 0; i < cfg->var_count; i++)
        free(cfg->var_names[i]);
    free(cfg->var_names);
    free(cfg);
}

FuncIR* ir_split_by_function(IR* program_ir) {
    if (!program_ir) return NULL;

    typedef struct { IR* ir; bool in_body; } IrMark;
    int total = 0, cap = 256;
    IrMark* marks = malloc(sizeof(IrMark) * (size_t)cap);
    if (!marks) OUT_OF_MEMORY();

    for (IR* p = program_ir; p; p = p->next) {
        if (total >= cap) {
            cap *= 2;
            marks = realloc(marks, sizeof(IrMark) * (size_t)cap);
            if (!marks) OUT_OF_MEMORY();
        }
        marks[total].ir = p;
        marks[total].in_body = false;
        total++;
    }

    int i = 0;
    while (i < total) {
        if (!(marks[i].ir->type == IR_LABEL && marks[i].ir->has_return)) {
            i++;
            continue;
        }
        int j = i;
        for (int k = i + 1; k < total; k++) {
            if (marks[k].ir->type == IR_LABEL && marks[k].ir->has_return) {
                break;
            }
            if (marks[k].ir->type == IR_VAR && marks[k].ir->is_static_global) {
                break;
            }
            if (marks[k].ir->type == IR_VAR_UNINIT && marks[k].ir->is_static_global) {
                break;
            }
            if (marks[k].ir->type == IR_ARRAY_DECL && marks[k].ir->is_static_global) {
                break;
            }
            j = k;
        }
        for (int k = i; k <= j; k++)
            marks[k].in_body = true;
        i = j + 1;
    }

    FuncIR* head = NULL, *tail = NULL;
    i = 0;
    while (i < total) {
        int j = i;
        while (j + 1 < total) {
            bool boundary =
                (marks[j + 1].ir->type == IR_LABEL
                 && marks[j + 1].ir->has_return)
                || marks[j + 1].in_body != marks[i].in_body;
            if (boundary) break;
            j++;
        }

        FuncIR* fi = calloc(1, sizeof(FuncIR));
        if (!fi) OUT_OF_MEMORY();
        fi->first_ir = marks[i].ir;
        fi->last_ir  = marks[j].ir;
        fi->last_ir->next = NULL;

        if (marks[i].in_body) {
            const char* nm = marks[i].ir->args1;
            fi->func_name = strdup(nm ? nm : "???");
        } else {
            fi->func_name = strdup((i == 0) ? "__preamble__"
                                            : "__interlude__");
        }

        if (!head) {
            head = tail = fi;
        } else {
            tail->next = fi;
            tail = fi;
        }

        i = j + 1;
    }
    free(marks);
    return head;
}

void func_ir_list_free(FuncIR* list) {
    while (list) {
        FuncIR* next = list->next;
        free(list->func_name);
        free(list);
        list = next;
    }
}