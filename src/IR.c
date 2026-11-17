#include "IR.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Error.h"

int temp_i = 0;
static int label_i = 0;
static int foreach_idx_counter = 0;
static int global_def_id = 0;

static LoopContext* ctx_stack = NULL;
static int ctx_top = -1;
static int ctx_capacity = 0;

static AST_Function*  current_ir_function = NULL;
static char* current_ir_ret_ptr = NULL;

#define EXC_FRAME_SIZE 56

static const char* current_break_label(void);
static const char* current_continue_label(void);

typedef struct {
    char* finally_label;
    char* reason_temp;
    char* ret_slot_temp;
    bool has_value;
    DataType ret_type;
    char* break_label;
    char* continue_label;
} FinallyCtx;

static FinallyCtx* finally_stack = NULL;
static int finally_top = -1;
static int finally_capacity = 0;

static int exc_name_counter = 0;

static int string_literal_length(const char* s) {
    int len = 0;
    if (!s) return 0;
    for (const char* p = s; *p; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        len++;
    }
    return len;
}

static char* gen_exc_internal_name(const char* tag) {
    char buf[64];
    snprintf(buf, sizeof(buf), "_exc_%s_%d", tag, exc_name_counter++);
    return strdup(buf);
}

static void push_finally(const char* flabel, const char* reason, const char* ret_slot) {
    if (finally_top + 1 >= finally_capacity) {
        finally_capacity = finally_capacity ? finally_capacity * 2 : 8;
        finally_stack = realloc(finally_stack,
                                sizeof(FinallyCtx) * (size_t)finally_capacity);
        if (!finally_stack) OUT_OF_MEMORY();
    }
    finally_top++;
    finally_stack[finally_top].finally_label = strdup(flabel);
    finally_stack[finally_top].reason_temp = strdup(reason);
    finally_stack[finally_top].ret_slot_temp = strdup(ret_slot);
    finally_stack[finally_top].has_value = false;
    finally_stack[finally_top].ret_type = DATA_INT;
    finally_stack[finally_top].break_label =
        current_break_label() ? strdup(current_break_label()) : NULL;
    finally_stack[finally_top].continue_label =
        current_continue_label() ? strdup(current_continue_label()) : NULL;
}

static void pop_finally(void) {
    if (finally_top >= 0) {
        free(finally_stack[finally_top].finally_label);
        free(finally_stack[finally_top].reason_temp);
        free(finally_stack[finally_top].ret_slot_temp);
        free(finally_stack[finally_top].break_label);
        free(finally_stack[finally_top].continue_label);
        finally_top--;
    }
}

int assign_def_id(IR* ir) {
    if (!ir) return -1;
    ir->def_id = ++global_def_id;
    return ir->def_id;
}

int get_def_id_by_name(const char* name, IR* head, IR* stop_before) {
    if (!name || !head) return -1;
    IR* found = NULL;
    for (IR* cur = head; cur && cur != stop_before; cur = cur->next) {
        if (cur->result && strcmp(cur->result, name) == 0) {
            found = cur;
        }
    }
    return found ? found->def_id : -1;
}

static bool is_number(const char* s) {
    if (!s || !*s) return false;
    if (*s == '-') s++;
    while (*s) { if (!isdigit((unsigned char)*s)) return false; s++; }
    return true;
}

static bool ast_is_const_int(ASTNode* node, int* out_val) {
    if (!node || node->type != AST_LITERAL) return false;
    AST_Literal* lit = (AST_Literal*)node;
    if (lit->is_call_function) return false;
    if (lit->base.value.type != TOKEN_NUMBER) return false;
    if (!lit->base.value.value || !is_number(lit->base.value.value))
        return false;
    if (out_val) *out_val = parse_integer(lit->base.value.value);
    return true;
}

static IR* chain_append(IR* head, IR* ir) {
    if (head == NULL) return ir;
    IR* p = head;
    while (p->next) p = p->next;
    p->next = ir;
    return head;
}

static void push_context(const char* break_lbl, const char* continue_lbl) {
    if (ctx_top + 1 >= ctx_capacity) {
        ctx_capacity = (ctx_capacity == 0) ? 8 : ctx_capacity * 2;
        ctx_stack = realloc(ctx_stack, sizeof(LoopContext) * ctx_capacity);
    }
    ctx_top++;
    ctx_stack[ctx_top].break_label = strdup(break_lbl);
    ctx_stack[ctx_top].continue_label = continue_lbl ? strdup(continue_lbl) : NULL;
}

static void pop_context(void) {
    if (ctx_top >= 0) {
        free(ctx_stack[ctx_top].break_label);
        free(ctx_stack[ctx_top].continue_label);
        ctx_top--;
    }
}

static const char* current_break_label(void) {
    return (ctx_top >= 0) ? ctx_stack[ctx_top].break_label : NULL;
}

static const char* current_continue_label(void) {
    return (ctx_top >= 0) ? ctx_stack[ctx_top].continue_label : NULL;
}

static void flatten_init_list(ASTNode* node, ASTNode*** out, int* count, int* capacity) {
    if (node == NULL) return;
    if (node->type == AST_INIT_LIST) {
        AST_InitList* list = (AST_InitList*)node;
        for (int i = 0; i < list->count; i++)
            flatten_init_list(list->elements[i], out, count, capacity);
    } else {
        if (*count >= *capacity) {
            *capacity = (*capacity == 0) ? 16 : (*capacity) * 2;
            *out = realloc(*out, sizeof(ASTNode*) * (*capacity));
        }
        (*out)[(*count)++] = node;
    }
}

static IR* ir_hoist_global_decls(IR* head)
{
    if (head == NULL || head->next == NULL) return head;
    IR* globals_head = NULL;
    IR* globals_tail = NULL;
    IR* rest_head = NULL;
    IR* rest_tail = NULL;
    IR* cur = head;
    while (cur != NULL) {
        IR* next = cur->next;
        cur->next = NULL;
        bool is_global_decl = cur->is_static_global
                              && (cur->type == IR_VAR
                                  || cur->type == IR_VAR_UNINIT
                                  || cur->type == IR_ARRAY_DECL);
        if (is_global_decl) {
            if (globals_head == NULL) { globals_head = globals_tail = cur; }
            else { globals_tail->next = cur; globals_tail = cur; }
        } else {
            if (rest_head == NULL) { rest_head = rest_tail = cur; }
            else { rest_tail->next = cur; rest_tail = cur; }
        }
        cur = next;
    }
    if (globals_head == NULL) return rest_head;
    globals_tail->next = rest_head;
    return globals_head;
}

IR* parse_IR(ASTNode* ast_node)
{
    if (ast_node == NULL)
    {
        INTERNAL_ERROR("IR: parse failed");
    }

    return ir_hoist_global_decls(start_generate_IR(ast_node));
}

IR* create_IR(IRType ir_type)
{
    IR* ir = calloc(1,sizeof(IR));
    if (ir == NULL)
    {
        INTERNAL_ERROR("IR: generation failed");
    }
    ir->args1 = NULL;
    ir->args2 = NULL;
    ir->next = NULL;
    ir->result = NULL;
    ir->label = NULL;
    ir->type = ir_type;
    ir->def_id = -1;
    ir->args1_def = -1;
    ir->args2_def = -1;
    ir->vtable_label = NULL;
    return ir;
}

static IR* ir_exc_frame_pop(void) {
    IR* pop = create_IR(IR_CALL);
    pop->args1 = strdup("_exc_frame_pop");
    pop->data_type = DATA_VOID;
    pop->call_func_args_num = 0;
    return pop;
}

char* create_func_temp_name(int index) {
    int len = snprintf(NULL, 0, ".f%d",index);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, ".f%d",index);
    }
    return temp;
}

char* create_temp() {
    int len = snprintf(NULL, 0, ".t%d",++temp_i);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, ".t%d",temp_i);
    }
    return temp;
}

char* create_ret_ptr_temp(void) {
    static int ret_temp_i = 0;
    int len = snprintf(NULL, 0, ".r%d", ++ret_temp_i);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, ".r%d", ret_temp_i);
    }
    return temp;
}

char* create_label()
{
    int len = snprintf(NULL, 0, "l%d", ++label_i);
    char* temp = malloc(len + 1);
    if (temp) {
        snprintf(temp, len + 1, "l%d", label_i);
    }
    return temp;
}

IR* connect_IR(IR* ir1, IR* ir2)
{
    if (ir1 == NULL)
    {
        return ir2;
    }
    IR* temp = ir1;
    while (temp->next != NULL)
    {
        temp = temp->next;
    }
    temp->next = ir2;
    return ir1;
}

static int count_local_vars(ASTNode* node) {
    if (node == NULL) return 0;

    int count = 0;

    switch (node->type) {
    case AST_VAR:
    case AST_UNINIT_VAR:
        count = 1;
        break;

    case AST_IF: {
        AST_If* if_node = (AST_If*)node;
        count = count_local_vars(if_node->code);
        for (int i = 0; i < if_node->elif_Count; i++) {
            count += count_local_vars(if_node->Elif[i]);
        }
        if (if_node->has_else) {
            count += count_local_vars(if_node->Else);
        }
        break;
    }

    case AST_WHILE: {
        AST_While* while_node = (AST_While*)node;
        count = count_local_vars(while_node->code);
        break;
    }

    case AST_FOR: {
        AST_For* f = (AST_For*)node;
        count += count_local_vars(f->init);
        count += count_local_vars(f->code);
        break;
    }
    case AST_FOREACH: {
        AST_ForEach* fe = (AST_ForEach*)node;
        count += 1;
        count += count_local_vars(fe->code);
        break;
    }

    case AST_FUNCTION:
        break;

    case AST_TRY: {
        AST_Try* tn = (AST_Try*)node;
        count += count_local_vars(tn->code);
        for (int i = 0; i < tn->catch_count; i++)
            count += count_local_vars(tn->catches[i].body);
        if (tn->finally_code)
            count += count_local_vars(tn->finally_code);
        break;
    }

    default:
        break;
    }

    count += count_local_vars(node->next);
    return count;
}

char* get_last_result(IR* ir) {
    if (ir == NULL) {
        INTERNAL_ERROR("IR: get_last_result with NULL chain");
    }

    IR* current = ir;
    while (current->next != NULL) {
        current = current->next;
    }

    if (current->result == NULL) {
        INTERNAL_ERROR("IR: last node has NULL result");
    }

    return current->result;
}

static char* gen_multidim_address(IR** chain_out, const char* base_temp,
                                   int* dim_sizes, int dim_count,
                                   ASTNode** indices, int index_count,
                                   int elem_size) {
    if (base_temp == NULL || dim_sizes == NULL || indices == NULL)
        return NULL;
    if (dim_count < index_count || index_count <= 0)
        return NULL;

    for (int i = 0; i < index_count; i++) {
        if (indices[i] == NULL) return NULL;
    }

    IR* chain = NULL;

    IR* idx0_ir = generate_IR(indices[0]);
    IR* idx0_last = idx0_ir;
    while (idx0_last->next) idx0_last = idx0_last->next;
    chain = idx0_ir;
    char* current_offset = idx0_last->result;

    for (int d = 1; d < index_count; d++) {
        IR* idx_ir = generate_IR(indices[d]);
        IR* idx_last = idx_ir;
        while (idx_last->next) idx_last = idx_last->next;

        IR* mul = create_IR(IR_BINARY);
        mul->op = IR_MUL;
        mul->args1 = strdup(current_offset);
        mul->args1_type = DATA_INT;
        char dim_str[16];
        sprintf(dim_str, "%d", dim_sizes[d]);
        mul->args2 = strdup(dim_str);
        mul->args2_type = DATA_INT;
        mul->result = create_temp();
        assign_def_id(mul);
        mul->data_type = DATA_INT;

        IR* add = create_IR(IR_BINARY);
        add->op = IR_ADD;
        add->args1 = strdup(mul->result);
        add->args1_type = DATA_INT;
        add->args2 = strdup(idx_last->result);
        add->args2_type = DATA_INT;
        add->result = create_temp();
        assign_def_id(add);
        add->data_type = DATA_INT;

        chain = connect_IR(chain, idx_ir);
        chain = connect_IR(chain, mul);
        chain = connect_IR(chain, add);

        current_offset = add->result;
    }

    IR* mul_esz = create_IR(IR_BINARY);
    mul_esz->op = IR_MUL;
    mul_esz->args1 = strdup(current_offset);
    mul_esz->args1_type = DATA_INT;
    char esz_str[16];
    sprintf(esz_str, "%d", elem_size);
    mul_esz->args2 = strdup(esz_str);
    mul_esz->args2_type = DATA_INT;
    mul_esz->result = create_temp();
    assign_def_id(mul_esz);
    mul_esz->data_type = DATA_INT;
    chain = connect_IR(chain, mul_esz);

    IR* add_base = create_IR(IR_BINARY);
    add_base->op = IR_ADD;
    add_base->args1 = strdup(base_temp);
    add_base->args1_type = DATA_INT;
    add_base->args2 = strdup(mul_esz->result);
    add_base->args2_type = DATA_INT;
    add_base->result = create_temp();
    assign_def_id(add_base);
    add_base->data_type = DATA_INT;
    chain = connect_IR(chain, add_base);

    *chain_out = chain;
    return strdup(add_base->result);
}

IR* start_generate_IR(ASTNode* ast_node)
{
    if (ast_node == NULL)
    {
        INTERNAL_ERROR("IR: generation failed");
    }

    IR* ir_chain = NULL;
    IR* last_ir = NULL;
    ASTNode* current_node = ast_node;

    while (current_node != NULL)
    {
        IR* node_ir = generate_IR(current_node);

        if (node_ir == NULL) {
            current_node = current_node->next;
            continue;
        }

        if (ir_chain == NULL) {
            ir_chain = node_ir;
        } else {
            if (last_ir == NULL) {
                last_ir = ir_chain;
                while (last_ir->next != NULL) {
                    last_ir = last_ir->next;
                }
            }
            last_ir->next = node_ir;
        }

        last_ir = node_ir;
        while (last_ir != NULL && last_ir->next != NULL) {
            last_ir = last_ir->next;
        }

        current_node = current_node->next;
    }

    return ir_chain;
}

IR_OP_Type Type_TO_OP(TokenType type, bool is_unsigned) {
    switch (type) {
    case TOKEN_ADD: return IR_ADD;
    case TOKEN_SUB: return IR_SUB;
    case TOKEN_MUL: return IR_MUL;
    case TOKEN_DIV: return is_unsigned ? IR_UDIV : IR_DIV;
    case TOKEN_MOD: return is_unsigned ? IR_UMOD : IR_MOD;
    case TOKEN_EQ:  return IR_JE;
    case TOKEN_NE:  return IR_JNE;
    case TOKEN_LT:  return is_unsigned ? IR_ULT : IR_JL;
    case TOKEN_LE:  return is_unsigned ? IR_ULE : IR_JLE;
    case TOKEN_GT:  return is_unsigned ? IR_UGT : IR_JG;
    case TOKEN_GE:  return is_unsigned ? IR_UGE : IR_JGE;
    case TOKEN_BITWISE_AND: return IR_BITWISE_AND;
    case TOKEN_BITWISE_OR:  return IR_BITWISE_OR;
    case TOKEN_SHIFT_LEFT:  return IR_SHL;
    case TOKEN_SHIFT_RIGHT: return is_unsigned ? IR_SHR : IR_SAR;
    case TOKEN_BITWISE_XOR: return IR_BITWISE_XOR;
    default: return IR_OP_ERROR;
    }
}

IR* ir_tail(IR* head) {
    if (!head) return NULL;
    while (head->next) head = head->next;
    return head;
}

static bool ast_is_int_const(ASTNode* node, long long* out) {
    if (!node) return false;
    if (node->type == AST_UNARY) {
        AST_Unary* un = (AST_Unary*)node;
        if (un->op.type != TOKEN_SUB) return false;
        long long v = 0;
        if (!ast_is_int_const(un->right, &v)) return false;
        if (out) *out = -v;
        return true;
    }
    if (node->type != AST_LITERAL) return false;
    AST_Literal* lit = (AST_Literal*)node;
    if (lit->is_call_function) return false;
    if (lit->base.value.type != TOKEN_NUMBER) return false;
    if (!lit->base.value.value) return false;
    if (strchr(lit->base.value.value, '.')) return false;
    if (out) *out = parse_integer64(lit->base.value.value);
    return true;
}

static bool ast_is_float_const(ASTNode* node, float* out) {
    if (!node) return false;
    if (node->type == AST_UNARY) {
        AST_Unary* un = (AST_Unary*)node;
        if (un->op.type != TOKEN_SUB) return false;
        float v = 0.0f;
        if (!ast_is_float_const(un->right, &v)) return false;
        if (out) *out = -v;
        return true;
    }
    if (node->type != AST_LITERAL) return false;
    AST_Literal* lit = (AST_Literal*)node;
    if (lit->is_call_function) return false;
    if (!lit->base.value.value) return false;
    bool is_float_tok = (lit->base.value.type == TOKEN_FLOAT_LITERAL);
    bool has_dot = (strchr(lit->base.value.value, '.') != NULL);
    if (!is_float_tok && !has_dot) return false;
    char* end = NULL;
    float v = strtof(lit->base.value.value, &end);
    if (!end || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

static long long fold_truncate_value(long long v, DataType t) {
    switch (t) {
    case DATA_INT8:
    case DATA_CHAR: {
        long long r = v & 0xFF;
        if (r & 0x80) r -= 0x100;
        return r;
    }
    case DATA_UINT8:
        return v & 0xFF;
    case DATA_INT16: {
        long long r = v & 0xFFFF;
        if (r & 0x8000) r -= 0x10000;
        return r;
    }
    case DATA_UINT16:
        return v & 0xFFFF;
    case DATA_BOOL:
        return (v != 0) ? 1 : 0;
    default:
        return v;
    }
}

static bool fold_result_is_int_family(DataType t) {
    switch (t) {
    case DATA_INT:    case DATA_UINT:
    case DATA_INT32:  case DATA_UINT32:
    case DATA_INT8:   case DATA_INT16:
    case DATA_UINT8:  case DATA_UINT16:
    case DATA_CHAR:   case DATA_BOOL:
        return true;
    default:
        return false;
    }
}

static IR* fold_result_literal(DataType result_type, const char* value_str) {
    IR* lit = create_IR(IR_LITERAL);
    lit->result = create_temp();
    assign_def_id(lit);
    lit->args1 = strdup(value_str);
    lit->args1_type = result_type;
    lit->data_type = result_type;
    return lit;
}

static IR* try_fold_const_binary(AST_Binary* bin) {
    if (!bin || !bin->left || !bin->right) return NULL;

    TokenType op = bin->op.type;
    switch (op) {
    case TOKEN_ADD: case TOKEN_SUB: case TOKEN_MUL:
    case TOKEN_DIV: case TOKEN_MOD:
    case TOKEN_EQ:  case TOKEN_NE:
    case TOKEN_LT:  case TOKEN_LE:
    case TOKEN_GT:  case TOKEN_GE:
    case TOKEN_BITWISE_AND: case TOKEN_BITWISE_OR:
    case TOKEN_BITWISE_XOR:
    case TOKEN_SHIFT_LEFT:  case TOKEN_SHIFT_RIGHT:
        break;
    default:
        return NULL;
    }

    if (get_type_size(bin->resultType) >= 8) return NULL;

    long long li = 0, ri = 0;
    bool lic = ast_is_int_const(bin->left, &li);
    bool ric = ast_is_int_const(bin->right, &ri);

    if (!lic && !ric) {
        float lf = 0.0f, rf = 0.0f;
        if (ast_is_float_const(bin->left, &lf) &&
            ast_is_float_const(bin->right, &rf)) {
            bool is_cmp = (op == TOKEN_EQ || op == TOKEN_NE ||
                           op == TOKEN_LT || op == TOKEN_LE ||
                           op == TOKEN_GT || op == TOKEN_GE);
            float r = 0.0f;
            int cmp = 0;
            bool valid = true;
            switch (op) {
            case TOKEN_ADD: r = lf + rf; break;
            case TOKEN_SUB: r = lf - rf; break;
            case TOKEN_MUL: r = lf * rf; break;
            case TOKEN_DIV:
                if (rf == 0.0f) valid = false;
                else r = lf / rf;
                break;
            case TOKEN_EQ: cmp = (lf == rf); break;
            case TOKEN_NE: cmp = (lf != rf); break;
            case TOKEN_LT: cmp = (lf <  rf); break;
            case TOKEN_LE: cmp = (lf <= rf); break;
            case TOKEN_GT: cmp = (lf >  rf); break;
            case TOKEN_GE: cmp = (lf >= rf); break;
            default: valid = false; break;
            }
            if (!valid) return NULL;
            if (!is_cmp && !isfinite(r)) return NULL;
            if (is_cmp) {
                return fold_result_literal(bin->resultType,
                                           cmp ? "1" : "0");
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%.9g", (double)r);
            return fold_result_literal(bin->resultType, buf);
        }
    }

    if (lic && ric && fold_result_is_int_family(bin->resultType)) {
        unsigned ua = (unsigned)(li & 0xFFFFFFFFLL);
        unsigned ub = (unsigned)(ri & 0xFFFFFFFFLL);
        long long r = 0;
        bool valid = true;

        switch (op) {
        case TOKEN_ADD: r = (long long)(int)(ua + ub); break;
        case TOKEN_SUB: r = (long long)(int)(ua - ub); break;
        case TOKEN_MUL: r = (long long)(int)(ua * ub); break;
        case TOKEN_DIV:
            if (bin->is_unsigned) {
                if (ub == 0) valid = false;
                else r = (long long)(int)(ua / ub);
            } else {
                int sa = (int)ua, sb = (int)ub;
                if (sb == 0) valid = false;
                else if (sa == INT32_MIN && sb == -1) valid = false;
                else r = (long long)(sa / sb);
            }
            break;
        case TOKEN_MOD:
            if (bin->is_unsigned) {
                if (ub == 0) valid = false;
                else r = (long long)(int)(ua % ub);
            } else {
                int sa = (int)ua, sb = (int)ub;
                if (sb == 0) valid = false;
                else if (sa == INT32_MIN && sb == -1) valid = false;
                else r = (long long)(sa % sb);
            }
            break;
        case TOKEN_EQ:
            r = bin->is_unsigned ? ((ua == ub) ? 1 : 0)
                                 : (((int)ua == (int)ub) ? 1 : 0);
            break;
        case TOKEN_NE:
            r = bin->is_unsigned ? ((ua != ub) ? 1 : 0)
                                 : (((int)ua != (int)ub) ? 1 : 0);
            break;
        case TOKEN_LT:
            r = bin->is_unsigned ? ((ua < ub) ? 1 : 0)
                                 : (((int)ua < (int)ub) ? 1 : 0);
            break;
        case TOKEN_LE:
            r = bin->is_unsigned ? ((ua <= ub) ? 1 : 0)
                                 : (((int)ua <= (int)ub) ? 1 : 0);
            break;
        case TOKEN_GT:
            r = bin->is_unsigned ? ((ua > ub) ? 1 : 0)
                                 : (((int)ua > (int)ub) ? 1 : 0);
            break;
        case TOKEN_GE:
            r = bin->is_unsigned ? ((ua >= ub) ? 1 : 0)
                                 : (((int)ua >= (int)ub) ? 1 : 0);
            break;
        case TOKEN_BITWISE_AND: r = (long long)(int)(ua & ub); break;
        case TOKEN_BITWISE_OR:  r = (long long)(int)(ua | ub); break;
        case TOKEN_BITWISE_XOR: r = (long long)(int)(ua ^ ub); break;
        case TOKEN_SHIFT_LEFT:
            if (ub >= 32) valid = false;
            else r = (long long)(int)(ua << ub);
            break;
        case TOKEN_SHIFT_RIGHT:
            if (ub >= 32) valid = false;
            else if (bin->is_unsigned) r = (long long)(int)(ua >> ub);
            else r = (long long)((int)ua >> ub);
            break;
        default:
            valid = false;
            break;
        }

        if (valid) {
            r = fold_truncate_value(r, bin->resultType);
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", r);
            return fold_result_literal(bin->resultType, buf);
        }
        return NULL;
    }

    if (lic != ric && fold_result_is_int_family(bin->resultType)
        && bin->result_ptr_depth == 0) {
        long long k = lic ? li : ri;
        bool var_left = ric;

        if ((op == TOKEN_SUB || op == TOKEN_DIV || op == TOKEN_MOD ||
             op == TOKEN_SHIFT_LEFT || op == TOKEN_SHIFT_RIGHT)
            && !var_left) {
            return NULL;
        }

        bool append_zero = false;
        bool do_shl_one  = false;
        switch (op) {
        case TOKEN_ADD:
        case TOKEN_SUB:
            if (k != 0) return NULL;
            break;
        case TOKEN_MUL:
            if (k == 0) append_zero = true;
            else if (k == 1) { }
            else if (k == 2) do_shl_one = true;
            else return NULL;
            break;
        case TOKEN_DIV:
            if (k != 1) return NULL;
            break;
        case TOKEN_MOD:
            if (k == 1) append_zero = true;
            else return NULL;
            break;
        case TOKEN_BITWISE_AND:
            if (k == 0) append_zero = true;
            else return NULL;
            break;
        case TOKEN_BITWISE_OR:
        case TOKEN_BITWISE_XOR:
            if (k != 0) return NULL;
            break;
        case TOKEN_SHIFT_LEFT:
        case TOKEN_SHIFT_RIGHT:
            if (k != 0) return NULL;
            break;
        default:
            return NULL;
        }

        ASTNode* var_node = var_left ? bin->left : bin->right;
        IR* chain = generate_IR(var_node);
        if (!chain) return NULL;
        IR* last = ir_tail(chain);

        if (append_zero) {
            IR* z = fold_result_literal(bin->resultType, "0");
            connect_IR(chain, z);
            return chain;
        }
        if (do_shl_one) {
            IR* shl = create_IR(IR_BINARY);
            shl->op = IR_SHL;
            shl->args1 = strdup(last->result);
            shl->args1_type = last->data_type;
            shl->args2 = strdup("1");
            shl->args2_type = DATA_INT;
            shl->result = create_temp();
            assign_def_id(shl);
            shl->data_type = bin->resultType;
            connect_IR(chain, shl);
            return chain;
        }
        return chain;
    }

    return NULL;
}
static int get_type_size(DataType type) {
    switch (type) {
    case DATA_CHAR:
    case DATA_BOOL:   return 1;
    case DATA_INT8:   case DATA_UINT8:   return 1;
    case DATA_INT16:  case DATA_UINT16:  return 2;
    case DATA_INT:    case DATA_UINT:
    case DATA_INT32:  case DATA_UINT32:
    case DATA_FLOAT:  case DATA_UFLOAT:
    case DATA_FLOAT32: case DATA_UFLOAT32:
    case DATA_STRING: return 4;
    case DATA_INT64:  case DATA_UINT64:
    case DATA_FLOAT64: case DATA_UFLOAT64:
    case DATA_STR:    return 8;
    case DATA_FLOAT8:  case DATA_UFLOAT8:  return 1;
    case DATA_FLOAT16: case DATA_UFLOAT16: return 2;
    case DATA_INT128:  case DATA_UINT128:
    case DATA_FLOAT128: case DATA_UFLOAT128: return 16;
    default:          return 4;
    }
}

static bool ir_is_unsigned_type(DataType t) {
    switch (t) {
        case DATA_UINT: case DATA_UINT8: case DATA_UINT16:
        case DATA_UINT32: case DATA_UINT64: case DATA_UINT128:
        case DATA_UFLOAT: case DATA_UFLOAT8: case DATA_UFLOAT16:
        case DATA_UFLOAT32: case DATA_UFLOAT64: case DATA_UFLOAT128:
        case DATA_CHAR: case DATA_BOOL:
            return true;
        default:
            return false;
    }
}

static bool ir_is_param_name(const char* internal)
{
    if (current_ir_function == NULL || internal == NULL) return false;
    ASTNode* p = current_ir_function->Parameter;
    while (p) {
        if (p->type == AST_VAR || p->type == AST_UNINIT_VAR) {
            AST_Var* pv = (AST_Var*)p;
            if (pv->internal_name && strcmp(pv->internal_name, internal) == 0)
                return true;
        } else if (p->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* pa = (AST_ArrayDecl*)p;
            if (pa->internal_name && strcmp(pa->internal_name, internal) == 0)
                return true;
        }
        p = p->next;
    }
    return false;
}

static IR* gen_array_access_ir(AST_ArrayAccess* acc, bool gen_load, char** addr_out) {
    if (acc->index_count == 0) return NULL;

    if (acc->func_sig != NULL && acc->index_count == 1) {
        IR* base_ir = generate_IR(acc->array);
        IR* base_last = base_ir;
        while (base_last->next) base_last = base_last->next;
        char* base_temp = base_last->result;

        if (acc->is_heap_array) {
            IR* deref = create_IR(IR_DEREF);
            deref->result = create_temp();
            assign_def_id(deref);
            deref->args1 = strdup(base_temp);
            deref->args1_type = DATA_INT;
            deref->data_type = DATA_INT;
            deref->ptr_depth = 0;
            base_ir = connect_IR(base_ir, deref);
            base_temp = deref->result;
        }
        IR* idx_ir = generate_IR(acc->indices[0]);
        IR* idx_last = idx_ir;
        while (idx_last->next) idx_last = idx_last->next;

        IR* mul = create_IR(IR_BINARY);
        mul->op = IR_MUL;
        mul->args1 = strdup(idx_last->result);
        mul->args1_type = DATA_INT;
        mul->args2 = strdup("4");
        mul->args2_type = DATA_INT;
        mul->result = create_temp();
        assign_def_id(mul);
        mul->data_type = DATA_INT;
        IR* add = create_IR(IR_BINARY);
        add->op = IR_ADD;
        add->args1 = strdup(base_temp);
        add->args1_type = DATA_INT;
        add->args2 = strdup(mul->result);
        add->args2_type = DATA_INT;
        add->result = create_temp();
        assign_def_id(add);
        add->data_type = DATA_INT;

        IR* load = create_IR(IR_DEREF);
        load->result = create_temp();
        assign_def_id(load);
        load->args1 = strdup(add->result);
        load->args1_type = DATA_INT;
        load->data_type = DATA_INT;
        load->ptr_depth = 0;
        IR* chain = connect_IR(base_ir, idx_ir);
        chain = connect_IR(chain, mul);
        chain = connect_IR(chain, add);
        chain = connect_IR(chain, load);
        /* When the caller wants the element address (assignment target) the
           address is base + i*4, not the loaded function pointer value. */
        if (addr_out) *addr_out = strdup(gen_load ? load->result : add->result);
        return chain;
    }

    IR* base_ir = generate_IR(acc->array);
    IR* base_last = base_ir;
    while (base_last->next) base_last = base_last->next;
    char* base_temp = base_last->result;
    ASTNode* arr = acc->array;
    const char* internal = NULL;
    if (arr->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)arr;
        if (!lit->is_call_function && lit->internal_name)
            internal = lit->internal_name;
        else if (!lit->is_call_function)
            internal = lit->base.value.value;
    } else if (arr->type == AST_VAR || arr->type == AST_VAR_ASS) {
        AST_Var* v = (AST_Var*)arr;
        internal = v->internal_name;
        if (!internal) internal = v->name.value;
    }
    int var_ptr_depth = 0;
    if (arr->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)arr;
        var_ptr_depth = lit->ptr_depth;
    } else if (arr->type == AST_VAR || arr->type == AST_VAR_ASS) {
        AST_Var* v = (AST_Var*)arr;
        var_ptr_depth = v->ptr_depth;
    }

    if (internal && acc->dim_count == 0 && acc->full_dim_count == 0
        && var_ptr_depth == 0 && !acc->is_heap_array) {
        IR* addr_ir = create_IR(IR_ADDRESS_OF);
        addr_ir->result = create_temp();
        assign_def_id(addr_ir);
        addr_ir->args1 = strdup(internal);
        addr_ir->args1_type = DATA_INT;
        addr_ir->data_type = DATA_INT;
        addr_ir->is_function_addr = false;
        base_ir = connect_IR(base_ir, addr_ir);
        base_temp = addr_ir->result;
    }

    char* fat_ptr_base = NULL;

    bool is_dynamic = false;

    if (acc->is_heap_array) {
        is_dynamic = true;
    }

    if (!is_dynamic && acc->array != NULL) {
        if (acc->array->type == AST_VAR || acc->array->type == AST_UNINIT_VAR) {
            AST_Var* av = (AST_Var*)acc->array;
            if (av->dim_count > 0 && av->dim_sizes != NULL) {
                bool all_unknown = true;
                for (int d = 0; d < av->dim_count; d++) {
                    if (av->dim_sizes[d] > 0) { all_unknown = false; break; }
                }
                if (all_unknown) is_dynamic = true;
            }
        } else if (acc->array->type == AST_LITERAL) {
            AST_Literal* al = (AST_Literal*)acc->array;
            if (al->base.value.type == TOKEN_IDENTIFIER && al->internal_name != NULL) {
                if (acc->full_dim_sizes != NULL && acc->full_dim_count > 0) {
                    bool all_unknown = true;
                    for (int d = 0; d < acc->full_dim_count; d++) {
                        if (acc->full_dim_sizes[d] > 0) { all_unknown = false; break; }
                    }
                    if (all_unknown) is_dynamic = true;
                }
            }
        }
    }
    // if (is_dynamic) {
    //     fat_ptr_base = strdup(base_last->args1);
    // }

    int elem_size = get_type_size(acc->elementType);
    IR* chain = base_ir;

    if (is_dynamic) {
        bool base_is_field = false;
        if (acc->array != NULL && acc->array->type == AST_LITERAL) {
            AST_Literal* base_lit = (AST_Literal*)acc->array;
            if (base_lit->is_field_ref) {
                base_is_field = true;
            }
        }
        if (base_is_field) {
            IR* deref = create_IR(IR_DEREF);
            deref->result = create_temp();
            assign_def_id(deref);
            deref->args1 = strdup(base_temp);
            deref->args1_type = DATA_INT;
            deref->data_type = DATA_INT;
            deref->ptr_depth = 0;
            chain = connect_IR(chain, deref);
            base_temp = deref->result;
        }
    }

    char* offset_temp = NULL;
    for (int d = 0; d < acc->index_count; d++) {
        IR* idx_ir = generate_IR(acc->indices[d]);
        IR* idx_last = idx_ir;
        while (idx_last->next) idx_last = idx_last->next;
        char* idx_val = idx_last->result;
        chain = connect_IR(chain, idx_ir);

        if (acc->full_dim_sizes != NULL && d < acc->full_dim_count) {
            int dim = acc->full_dim_sizes[d];

            if (dim > 0) {
                char dimStr[16];
                sprintf(dimStr, "%d", dim);

                IR* dim_lit = create_IR(IR_LITERAL);
                dim_lit->args1 = strdup(dimStr);
                dim_lit->args1_type = DATA_INT;
                dim_lit->data_type = DATA_INT;
                dim_lit->result = create_temp();
                assign_def_id(dim_lit);
                chain = connect_IR(chain, dim_lit);

                IR* check = create_IR(IR_BOUNDS_CHECK);
                check->args1 = strdup(idx_val);
                check->args2 = strdup(dim_lit->result);
                check->data_type = DATA_INT;
                chain = connect_IR(chain, check);
            } else if (dim == -1 && d == 0) {
                if (is_dynamic && ir_is_param_name(internal)) {
                    IR* plen = create_IR(IR_ARRAY_LENGTH);
                    plen->result = create_temp();
                    assign_def_id(plen);
                    plen->args1 = strdup(internal);
                    plen->data_type = DATA_INT;
                    chain = connect_IR(chain, plen);

                    IR* pcheck = create_IR(IR_BOUNDS_CHECK);
                    pcheck->args1 = strdup(idx_val);
                    pcheck->args2 = strdup(plen->result);
                    pcheck->data_type = DATA_INT;
                    chain = connect_IR(chain, pcheck);
                } else if (is_dynamic) {
                    IR* sub4 = create_IR(IR_BINARY);
                    sub4->op = IR_SUB;
                    sub4->args1 = strdup(base_temp);
                    sub4->args1_type = DATA_INT;
                    sub4->args2 = strdup("4");
                    sub4->args2_type = DATA_INT;
                    sub4->result = create_temp();
                    assign_def_id(sub4);
                    sub4->data_type = DATA_INT;
                    chain = connect_IR(chain, sub4);

                    IR* len_load = create_IR(IR_DEREF);
                    len_load->result = create_temp();
                    assign_def_id(len_load);
                    len_load->args1 = strdup(sub4->result);
                    len_load->args1_type = DATA_INT;
                    len_load->data_type = DATA_INT;
                    len_load->ptr_depth = 0;
                    chain = connect_IR(chain, len_load);

                    IR* check = create_IR(IR_BOUNDS_CHECK);
                    check->args1 = strdup(idx_val);
                    check->args2 = strdup(len_load->result);
                    check->data_type = DATA_INT;
                    chain = connect_IR(chain, check);
                } else {
                    IR* len_ir = create_IR(IR_ARRAY_LENGTH);
                    len_ir->result = create_temp();
                    assign_def_id(len_ir);
                    len_ir->args1 = strdup(base_temp);
                    len_ir->data_type = DATA_INT;
                    chain = connect_IR(chain, len_ir);
                    IR* check = create_IR(IR_BOUNDS_CHECK);
                    check->args1 = strdup(idx_val);
                    check->args2 = strdup(len_ir->result);
                    check->data_type = DATA_INT;
                    chain = connect_IR(chain, check);
                }
            }
        }

        if (d == 0) {
            offset_temp = idx_val;
        } else {
            int cur_dim = acc->full_dim_sizes[d];
            char dim_str[16];
            sprintf(dim_str, "%d", cur_dim);

            IR* mul = create_IR(IR_BINARY);
            mul->op = IR_MUL;
            mul->args1 = strdup(offset_temp);
            mul->args1_type = DATA_INT;
            mul->args2 = strdup(dim_str);
            mul->args2_type = DATA_INT;
            mul->result = create_temp();
            assign_def_id(mul);
            mul->data_type = DATA_INT;
            chain = connect_IR(chain, mul);

            IR* add = create_IR(IR_BINARY);
            add->op = IR_ADD;
            add->args1 = strdup(mul->result);
            add->args1_type = DATA_INT;
            add->args2 = strdup(idx_val);
            add->args2_type = DATA_INT;
            add->result = create_temp();
            assign_def_id(add);
            add->data_type = DATA_INT;
            chain = connect_IR(chain, add);

            offset_temp = add->result;
        }
    }

    if (!offset_temp) {
        return chain;
    }

    int stride = elem_size;
    for (int s = acc->index_count; s < acc->full_dim_count; s++) {
        if (acc->full_dim_sizes && acc->full_dim_sizes[s] > 0)
            stride *= acc->full_dim_sizes[s];
    }
    IR* mul_elem = create_IR(IR_BINARY);
    mul_elem->op = IR_MUL;
    mul_elem->args1 = strdup(offset_temp);
    mul_elem->args1_type = DATA_INT;
    char esz[16];
    sprintf(esz, "%d", stride);
    mul_elem->args2 = strdup(esz);
    mul_elem->args2_type = DATA_INT;
    mul_elem->result = create_temp();
    assign_def_id(mul_elem);
    mul_elem->data_type = DATA_INT;
    chain = connect_IR(chain, mul_elem);

    IR* add_base = create_IR(IR_BINARY);
    add_base->op = IR_ADD;
    add_base->args1 = strdup(base_temp);
    add_base->args1_type = DATA_INT;
    add_base->args2 = strdup(mul_elem->result);
    add_base->args2_type = DATA_INT;
    add_base->result = create_temp();
    assign_def_id(add_base);
    add_base->data_type = DATA_INT;
    chain = connect_IR(chain, add_base);

    char* addr_temp = add_base->result;

    if (gen_load) {
        if (acc->func_sig != NULL) {
            return chain;
        }
        if (acc->dim_count > 0) {
            IR* pass = create_IR(IR_LITERAL);
            pass->result = create_temp();
            assign_def_id(pass);
            pass->args1 = strdup(addr_temp);
            pass->args1_type = DATA_INT;
            pass->data_type = acc->elementType;
            chain = connect_IR(chain, pass);
            return chain;
        }
        IR* load_ir = create_IR(IR_ARRAY_LOAD);
        load_ir->result = create_temp();
        assign_def_id(load_ir);
        load_ir->args1 = strdup(addr_temp);
        load_ir->args1_type = DATA_INT;
        load_ir->data_type = acc->elementType;
        load_ir->elem_size = elem_size;
        chain = connect_IR(chain, load_ir);
        return chain;
    } else {
        if (addr_out) *addr_out = addr_temp;
        return chain;
    }
}

IR* generate_IR(ASTNode* ast_node)
{
    if (ast_node == NULL)
    {
        INTERNAL_ERROR("IR: generate_IR called with NULL");
    }

    switch (ast_node->type)
    {
    case AST_LITERAL:
        AST_Literal* ast_literal = (AST_Literal*)ast_node;
        IR* Literal_ir;

        if (ast_literal->is_call_function) {
            Literal_ir = create_IR(IR_CALL);
        } else {
            Literal_ir = create_IR(IR_LITERAL);
        }

        Literal_ir->result = create_temp();
        assign_def_id(Literal_ir);

        if (ast_literal->base.value.type == TOKEN_IDENTIFIER) {
            Literal_ir->args1 = strdup(ast_literal->internal_name
                                       ? ast_literal->internal_name : "0");
        } else if (ast_literal->base.value.type == TOKEN_CHAR_LITERAL) {
            char chbuf[16];
            char ch = (ast_literal->base.value.value != NULL)
                      ? ast_literal->base.value.value[0] : 0;
            snprintf(chbuf, sizeof(chbuf), "%d", (int)(unsigned char)ch);
            Literal_ir->args1 = strdup(chbuf);
        } else if (ast_literal->base.value.type == TOKEN_NUMBER &&
                   ast_literal->base.value.value != NULL &&
                   !strchr(ast_literal->base.value.value, '.')) {
            Literal_ir->args1 = normalize_number_str(ast_literal->base.value.value);
        } else if (ast_literal->base.value.value != NULL) {
            Literal_ir->args1 = strdup(ast_literal->base.value.value);
        } else {
            Literal_ir->args1 = strdup("0");
        }

        Literal_ir->args2 = NULL;

        Literal_ir->data_type = (ast_literal->ptr_depth > 0)
                                ? DATA_INT : ast_literal->dataType;
        Literal_ir->args1_type = (ast_literal->ptr_depth > 0)
                                 ? DATA_INT : ast_literal->dataType;
        Literal_ir->is_dynamic_array = ast_literal->is_dynamic_array;

        if (ast_literal->is_call_function)
        {
            if (ast_literal->is_method_call) {
                IR* chain = NULL;
                #define ADD_MC(node) do { \
                    if (!chain) chain = (node); \
                    else { IR* t = chain; while (t->next) t = t->next; t->next = (node); } \
                } while(0)
                char* fp_name = ast_literal->internal_name;

                IR* load_this = create_IR(IR_DEREF);
                load_this->result = create_temp();
                assign_def_id(load_this);
                load_this->args1 = strdup(fp_name);
                load_this->args1_type = DATA_INT;
                load_this->data_type = DATA_INT;
                load_this->ptr_depth = 0;
                ADD_MC(load_this);
                char* this_temp = load_this->result;

                int user_arg_count = 0;
                char** arg_temps = NULL;
                DataType* arg_dtypes = NULL;
                ASTNode* arg_node = ast_literal->call_function_Parameter;
                while (arg_node) {
                    IR* arg_ir = generate_IR(arg_node);
                    IR* last = arg_ir;
                    while (last->next) last = last->next;
                    user_arg_count++;
                    arg_temps = realloc(arg_temps, sizeof(char*) * user_arg_count);
                    arg_dtypes = realloc(arg_dtypes, sizeof(DataType) * user_arg_count);
                    arg_temps[user_arg_count - 1] = strdup(last->result);
                    arg_dtypes[user_arg_count - 1] = last->data_type;
                    ADD_MC(arg_ir);
                    arg_node = arg_node->next;
                }
                for (int i = user_arg_count - 1; i >= 0; i--) {
                    IR* push = create_IR(IR_PUSH);
                    push->args1 = strdup(arg_temps[i]);
                    push->args1_type = arg_dtypes[i];
                    if (arg_dtypes[i] == DATA_STR) {
                        push->push_bytes = 8;
                        push->is_str_param = 1;
                    } else if (arg_dtypes[i] == DATA_FLOAT
                               || arg_dtypes[i] == DATA_UFLOAT) {
                        push->push_bytes = 4;
                               } else {
                                   push->push_bytes = 4;
                               }
                    ADD_MC(push);
                    free(arg_temps[i]);
                }
                free(arg_temps);
                free(arg_dtypes);

                IR* push_this = create_IR(IR_PUSH);
                push_this->args1 = strdup(this_temp);
                push_this->args1_type = DATA_INT;
                push_this->push_bytes = 4;
                ADD_MC(push_this);

                IR* off4 = create_IR(IR_BINARY);
                off4->op = IR_ADD;
                off4->args1 = strdup(fp_name);
                off4->args1_type = DATA_INT;
                off4->args2 = strdup("4");
                off4->args2_type = DATA_INT;
                off4->result = create_temp();
                assign_def_id(off4);
                off4->data_type = DATA_INT;
                ADD_MC(off4);
                IR* load_func = create_IR(IR_DEREF);
                load_func->result = create_temp();
                assign_def_id(load_func);
                load_func->args1 = strdup(off4->result);
                load_func->args1_type = DATA_INT;
                load_func->data_type = DATA_INT;
                load_func->ptr_depth = 0;
                ADD_MC(load_func);

                IR* call_ir = create_IR(IR_CALL_INDIRECT);
                call_ir->args1 = strdup(load_func->result);
                call_ir->args1_type = DATA_INT;
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = (ast_literal->ptr_depth > 0)
                                     ? DATA_INT : ast_literal->dataType;
                call_ir->call_func_args_num = user_arg_count + 1;
                call_ir->total_push_bytes = (user_arg_count + 1) * 4;
                ADD_MC(call_ir);
                #undef ADD_MC
                return chain;
            }

            if (ast_literal->method_invoke &&
                ast_literal->call_function_Parameter != NULL &&
                ast_literal->call_function_Parameter->type == AST_MEMBER_ACCESS) {
                AST_MemberAccess* ma = (AST_MemberAccess*)ast_literal->call_function_Parameter;

                ASTNode* obj_node = ma->object;
                if (obj_node != NULL && obj_node->type == AST_DEREF) {
                    obj_node = ((AST_Deref*)obj_node)->operand;
                }

                IR* obj_chain = NULL;
                if (obj_node->type == AST_LITERAL) {
                    AST_Literal* obj_lit = (AST_Literal*)obj_node;
                    if (!obj_lit->is_call_function
                        && obj_lit->internal_name != NULL
                        && !obj_lit->is_field_ref) {
                        IR* lit = create_IR(IR_LITERAL);
                        assign_def_id(lit);
                        lit->result = create_temp();
                        lit->args1 = strdup(obj_lit->internal_name);
                        lit->args1_type = DATA_INT;
                        lit->data_type = DATA_INT;
                        obj_chain = lit;
                    }
                } else if (obj_node->type == AST_VAR ||
                           obj_node->type == AST_UNINIT_VAR ||
                           obj_node->type == AST_VAR_ASS) {
                    AST_Var* obj_var = (AST_Var*)obj_node;
                    if (obj_var->internal_name != NULL) {
                        IR* lit = create_IR(IR_LITERAL);
                        assign_def_id(lit);
                        lit->result = create_temp();
                        lit->args1 = strdup(obj_var->internal_name);
                        lit->args1_type = DATA_INT;
                        lit->data_type = DATA_INT;
                        obj_chain = lit;
                    }
                }
                if (obj_chain == NULL) {
                    obj_chain = generate_IR(obj_node);
                }
                if (obj_chain == NULL) return NULL;
                IR* obj_last = obj_chain;
                while (obj_last->next != NULL) {
                    obj_last = obj_last->next;
                }
                char* this_temp = obj_last->result;

                bool is_class_name = false;
                if (ma->object->type == AST_LITERAL) {
                    AST_Literal* obj_lit = (AST_Literal*)ma->object;
                    if (!obj_lit->is_call_function
                        && obj_lit->base.value.type == TOKEN_IDENTIFIER
                        && obj_lit->internal_name == NULL
                        && !obj_lit->is_field_ref) {
                        is_class_name = true;
                    }
                }

                bool callee_returns_array = ast_literal->returns_array;
                int callee_ret_total_bytes = ast_literal->return_total_bytes;
                if (ast_literal->dataType == DATA_STR
                    && callee_ret_total_bytes == 0) {
                    callee_returns_array = true;
                    callee_ret_total_bytes = 8;
                }

                IR* after_obj_chain = obj_chain;
                char* callee_ret_buf = NULL;
                if (callee_returns_array && callee_ret_total_bytes > 0) {
                    if (callee_ret_total_bytes < 4)
                        callee_ret_total_bytes = 4;
                    callee_ret_buf = create_temp();
                    IR* ret_buf_alloc = create_IR(IR_ALLOC_ARRAY);
                    ret_buf_alloc->result = strdup(callee_ret_buf);
                    ret_buf_alloc->args1 = strdup("1");
                    ret_buf_alloc->args1_type = DATA_INT;
                    ret_buf_alloc->elem_size = callee_ret_total_bytes;
                    ret_buf_alloc->data_type = DATA_INT;
                    ret_buf_alloc->stack_bytes = callee_ret_total_bytes;
                    assign_def_id(ret_buf_alloc);
                    after_obj_chain = connect_IR(after_obj_chain, ret_buf_alloc);
                }

                int total_arg_count;
                if (is_class_name) {
                    total_arg_count = ast_literal->method_arg_count;
                } else {
                    total_arg_count = 1 + ast_literal->method_arg_count;
                }
                if (callee_ret_buf != NULL)
                    total_arg_count++;

                char** arg_temps = malloc(sizeof(char*) * total_arg_count);
                DataType* arg_types = malloc(sizeof(DataType) * total_arg_count);
                int arg_base = 0;
                if (callee_ret_buf != NULL) {
                    arg_temps[arg_base] = strdup(callee_ret_buf);
                    arg_types[arg_base] = DATA_INT;
                    arg_base++;
                }
                if (!is_class_name) {
                    arg_temps[arg_base] = strdup(this_temp);
                    arg_types[arg_base] = DATA_INT;
                    arg_base++;
                }
                ASTNode* arg_node = ast_literal->method_arguments;
                int idx = arg_base;
                while (arg_node) {
                    IR* arg_ir = generate_IR(arg_node);
                    if (arg_ir == NULL) {
                        for (int j = 0; j < idx; j++) free(arg_temps[j]);
                        free(arg_temps); free(arg_types);
                        return NULL;
                    }
                    IR* last = arg_ir;
                    while (last->next) last = last->next;
                    arg_temps[idx] = strdup(last->result);
                    arg_types[idx] = last->data_type;
                    if (last->type == IR_ADDRESS_OF
                        && last->args1_type == DATA_STR
                        && !last->is_function_addr) {
                        arg_types[idx] = DATA_STR;
                    }
                    idx++;
                    after_obj_chain = connect_IR(after_obj_chain, arg_ir);
                    arg_node = arg_node->next;
                }
                IR* push_chain_ = NULL;
                IR* push_last_ = NULL;
                int total_push = 0;
                int hidden_offset = (callee_ret_buf != NULL) ? 1 : 0;
                for (int i = total_arg_count - 1; i >= 0; i--) {
                    IR* push_ir = create_IR(IR_PUSH);
                    push_ir->args1 = strdup(arg_temps[i]);
                    push_ir->args1_type = arg_types[i];
                    int pb = 4;
                    int size_idx = i - hidden_offset;
                    if (ast_literal->param_byte_sizes != NULL
                        && size_idx >= 0
                        && size_idx < ast_literal->call_function_ParameterCount) {
                        pb = ast_literal->param_byte_sizes[size_idx];
                        }
                    if (arg_types[i] == DATA_STR) {
                        pb = 8;
                        push_ir->is_str_param = 1;
                    }
                    if ((arg_types[i] == DATA_INT64 || arg_types[i] == DATA_UINT64) && pb < 8) {
                        pb = 8;
                    }
                    push_ir->push_bytes = pb;
                    total_push += push_ir->push_bytes;
                    push_ir->next = NULL;
                    if (push_chain_ == NULL) {
                        push_chain_ = push_last_ = push_ir;
                    } else {
                        push_last_->next = push_ir;
                        push_last_ = push_ir;
                    }
                }
                if (push_last_) {
                    after_obj_chain = connect_IR(after_obj_chain, push_chain_);
                }

                IR* call_ir;
                if (ma->vtable_index >= 0) {
                    IR* load_vtbl = create_IR(IR_LOAD_MEMBER);
                    load_vtbl->result = create_temp();
                    assign_def_id(load_vtbl);
                    load_vtbl->args1 = strdup(this_temp);
                    load_vtbl->args1_type = DATA_INT;
                    load_vtbl->args2 = strdup("4");
                    load_vtbl->data_type = DATA_INT;

                    IR* slot_off = create_IR(IR_BINARY);
                    slot_off->op = IR_ADD;
                    slot_off->args1 = strdup(load_vtbl->result);
                    slot_off->args1_type = DATA_INT;
                    char slot_str[16];
                    snprintf(slot_str, sizeof(slot_str), "%d",
                             ma->vtable_index * 4);
                    slot_off->args2 = strdup(slot_str);
                    slot_off->args2_type = DATA_INT;
                    slot_off->result = create_temp();
                    assign_def_id(slot_off);
                    slot_off->data_type = DATA_INT;

                    IR* load_func = create_IR(IR_DEREF);
                    load_func->result = create_temp();
                    assign_def_id(load_func);
                    load_func->args1 = strdup(slot_off->result);
                    load_func->args1_type = DATA_INT;
                    load_func->data_type = DATA_INT;
                    load_func->ptr_depth = 0;

                    after_obj_chain = connect_IR(after_obj_chain, load_vtbl);
                    after_obj_chain = connect_IR(after_obj_chain, slot_off);
                    after_obj_chain = connect_IR(after_obj_chain, load_func);

                    call_ir = create_IR(IR_CALL_INDIRECT);
                    call_ir->args1 = strdup(load_func->result);
                    call_ir->args1_type = DATA_INT;
                } else {
                    call_ir = create_IR(IR_CALL);
                    call_ir->args1 = strdup(ast_literal->internal_name);
                }
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = (ast_literal->ptr_depth > 0)
                                     ? DATA_INT : ast_literal->dataType;
                call_ir->call_func_args_num = total_arg_count;
                call_ir->total_push_bytes = total_push;
                call_ir->temp_stack_alloc = 0;
                after_obj_chain = connect_IR(after_obj_chain, call_ir);

                if (callee_returns_array && callee_ret_buf != NULL
                    && callee_ret_total_bytes > 4) {
                    IR* pass_buf = create_IR(IR_LITERAL);
                    pass_buf->result = create_temp();
                    assign_def_id(pass_buf);
                    pass_buf->args1 = strdup(callee_ret_buf);
                    pass_buf->args1_type = DATA_INT;
                    pass_buf->data_type = DATA_INT;
                    after_obj_chain = connect_IR(after_obj_chain, pass_buf);
                }
                for (int i = 0; i < total_arg_count; i++) {
                    free(arg_temps[i]);
                }
                free(arg_temps);
                free(arg_types);
                return after_obj_chain;
            }
            if (!ast_literal->is_indirect_call
                && ast_literal->base.value.value != NULL
                && strcmp(ast_literal->base.value.value, "_arrayToString") == 0) {
                AST_Literal* arr_lit = (AST_Literal*)ast_literal->call_function_Parameter;
                if (arr_lit && arr_lit->base.type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* acc = (AST_ArrayAccess*)arr_lit;
                    if (acc->dim_count > 0) {
                        IR* base_ir = generate_IR(acc->array);
                        IR* base_last = base_ir;
                        while (base_last->next) base_last = base_last->next;
                        char* base_temp = base_last->result;

                        int totalDims = acc->full_dim_count;
                        int* dimSizes = acc->full_dim_sizes;

                        IR* addr_chain = NULL;
                        char* addr_temp = gen_multidim_address(&addr_chain,
                                            base_temp,
                                            dimSizes, totalDims,
                                            acc->indices, acc->index_count,
                                            get_type_size(acc->elementType));
                        if (!addr_temp) return NULL;

                        IR* chain = connect_IR(base_ir, addr_chain);

                        int sub_len = acc->dim_sizes[0];

                        IR* tostr = create_IR(IR_ARRAY_TOSTRING);
                        tostr->result = create_temp();
                        assign_def_id(tostr);
                        tostr->args1 = strdup(addr_temp);
                        tostr->args1_type = acc->elementType;
                        char lenStr[16];
                        sprintf(lenStr, "%d", sub_len);
                        tostr->args2 = strdup(lenStr);
                        tostr->data_type = DATA_STRING;

                        chain = connect_IR(chain, tostr);
                        free(addr_temp);
                        return chain;
                    }
                }
                if (!arr_lit) return NULL;
                char* internal_name = arr_lit->internal_name;
                if (!internal_name) internal_name = arr_lit->base.value.value;

                IR* base_ir = generate_IR(ast_literal->call_function_Parameter);
                IR* base_last = base_ir;
                while (base_last->next) base_last = base_last->next;

                IR* tostr_ir = create_IR(IR_ARRAY_TOSTRING);
                tostr_ir->args1 = strdup(base_last->result);
                tostr_ir->args1_type = arr_lit->dataType;
                tostr_ir->args2 = strdup(internal_name);
                tostr_ir->data_type = DATA_STRING;
                tostr_ir->result = create_temp();
                assign_def_id(tostr_ir);

                return connect_IR(base_ir, tostr_ir);
            }
            if (!ast_literal->is_indirect_call
                && ast_literal->base.value.value != NULL
                && (strcmp(ast_literal->base.value.value, "format") == 0 ||
                    strcmp(ast_literal->base.value.value, "_format") == 0)) {
                IR *chain = NULL, *last = NULL;
                ASTNode *arg = ast_literal->call_function_Parameter;
                int count = 0;
                char **names = NULL;
                DataType *types = NULL;

                while (arg) {
                    bool is_direct = false;
                    if (arg->type == AST_LITERAL) {
                        AST_Literal *lit = (AST_Literal*)arg;
                        if (lit->base.value.type == TOKEN_IDENTIFIER
                            && !lit->is_call_function
                            && lit->internal_name != NULL) {
                            is_direct = true;
                        }
                    }

                    names = realloc(names, sizeof(char*) * (count + 1));
                    types = realloc(types, sizeof(DataType) * (count + 1));

                    if (is_direct) {
                        AST_Literal *lit = (AST_Literal*)arg;
                        names[count] = strdup(lit->internal_name);
                        types[count] = lit->dataType;
                    } else {
                        IR *arg_ir = generate_IR(arg);
                        if (arg_ir == NULL) {
                            INTERNAL_ERROR("IR: format argument IR is NULL");
                            free(names);
                            free(types);
                            return chain;
                        }
                        IR *tail = arg_ir;
                        while (tail->next && tail != NULL) tail = tail->next;

                        names[count] = strdup(tail->result);
                        types[count] = tail->data_type;

                        if (!chain) chain = arg_ir;
                        else last->next = arg_ir;
                        last = tail;
                    }

                    arg = arg->next;
                    count++;
                }

                IR *fmt_ir = create_IR(IR_FORMAT);
                fmt_ir->result = create_temp();
                assign_def_id(fmt_ir);
                fmt_ir->data_type = DATA_STRING;

                if (count > 0) {
                    fmt_ir->args1 = strdup(names[0]);
                    fmt_ir->call_func_args_num = count - 1;
                    if (count - 1 > 0) {
                        fmt_ir->call_func_args_type = malloc(sizeof(DataType) * (count - 1));
                        memcpy(fmt_ir->call_func_args_type, &types[1], sizeof(DataType) * (count - 1));
                        fmt_ir->label = malloc(sizeof(char*) * (count - 1));
                        fmt_ir->label_count = count - 1;
                        for (int i = 1; i < count; i++) {
                            fmt_ir->label[i - 1] = strdup(names[i]);
                        }
                    }
                } else {
                    fmt_ir->args1 = NULL;
                    fmt_ir->call_func_args_num = 0;
                }

                for (int i = 0; i < count; i++) free(names[i]);
                free(names);
                free(types);

                if (last) last->next = fmt_ir;
                else chain = fmt_ir;

                return chain;
            }

            if (!ast_literal->is_indirect_call
                && ast_literal->internal_name != NULL
                && strcmp(ast_literal->internal_name, "_print_err") == 0) {
                IR* chain = NULL;
                ASTNode* p = ast_literal->call_function_Parameter;
                while (p) {
                    IR* pi = generate_IR(p);
                    if (!pi) { p = p->next; continue; }
                    IR* tail = pi;
                    while (tail->next) tail = tail->next;
                    IR* pr = create_IR(IR_PRINT);
                    pr->args1 = strdup(tail->result);
                    pr->call_func_args_type = malloc(sizeof(DataType));
                    pr->call_func_args_type[0] = tail->data_type;
                    pr->call_func_args_num = 1;
                    pr->is_err = true;
                    tail->next = pr;
                    chain = connect_IR(chain, pi);
                    p = p->next;
                }
                return chain;
            }

            if (!ast_literal->is_indirect_call
                && ast_literal->internal_name != NULL
                && strcmp(ast_literal->internal_name, "_str_to_string") == 0
                && ast_literal->call_function_ParameterCount == 1) {
                ASTNode* arg_node = ast_literal->call_function_Parameter;

                const char* str_internal = NULL;
                if (arg_node->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)arg_node;
                    if (!lit->is_call_function) {
                        str_internal = lit->internal_name;
                        if (!str_internal) str_internal = lit->base.value.value;
                    }
                } else if (arg_node->type == AST_VAR || arg_node->type == AST_VAR_ASS) {
                    AST_Var* var = (AST_Var*)arg_node;
                    str_internal = var->internal_name;
                    if (!str_internal) str_internal = var->name.value;
                }

                if (str_internal) {
                    IR* chain = NULL;
                    IR* tail = NULL;
                    #define ADD_IR(node) do { \
                        if (!chain) { chain = tail = (node); } \
                        else { tail->next = (node); tail = (node); } \
                    } while(0)

                    IR* str_ptr = create_IR(IR_LITERAL);
                    str_ptr->result = create_temp();
                    assign_def_id(str_ptr);
                    str_ptr->args1 = strdup(str_internal);
                    str_ptr->args1_type = DATA_STR;
                    str_ptr->data_type = DATA_INT;
                    ADD_IR(str_ptr);

                    IR* str_addr = create_IR(IR_ADDRESS_OF);
                    str_addr->result = create_temp();
                    assign_def_id(str_addr);
                    str_addr->args1 = strdup(str_internal);
                    str_addr->args1_type = DATA_STR;
                    str_addr->data_type = DATA_INT;
                    str_addr->is_function_addr = false;
                    ADD_IR(str_addr);

                    IR* str_off = create_IR(IR_BINARY);
                    str_off->op = IR_ADD;
                    str_off->args1 = strdup(str_addr->result);
                    str_off->args1_type = DATA_INT;
                    str_off->args2 = strdup("4");
                    str_off->args2_type = DATA_INT;
                    str_off->result = create_temp();
                    assign_def_id(str_off);
                    str_off->data_type = DATA_INT;
                    ADD_IR(str_off);

                    IR* str_len_load = create_IR(IR_DEREF);
                    str_len_load->result = create_temp();
                    assign_def_id(str_len_load);
                    str_len_load->args1 = strdup(str_off->result);
                    str_len_load->args1_type = DATA_INT;
                    str_len_load->data_type = DATA_INT;
                    str_len_load->ptr_depth = 0;
                    ADD_IR(str_len_load);

                    IR* push_len = create_IR(IR_PUSH);
                    push_len->args1 = strdup(str_len_load->result);
                    push_len->args1_type = DATA_INT;
                    push_len->push_bytes = 4;
                    ADD_IR(push_len);

                    IR* push_ptr = create_IR(IR_PUSH);
                    push_ptr->args1 = strdup(str_ptr->result);
                    push_ptr->args1_type = DATA_INT;
                    push_ptr->push_bytes = 4;
                    ADD_IR(push_ptr);

                    IR* call_ir = create_IR(IR_CALL);
                    call_ir->args1 = strdup("_str_to_string");
                    call_ir->result = create_temp();
                    assign_def_id(call_ir);
                    call_ir->data_type = DATA_STRING;
                    call_ir->call_func_args_num = 2;
                    call_ir->total_push_bytes = 8;
                    ADD_IR(call_ir);

                    #undef ADD_IR
                    return chain;
                } else {
                    IR* arg_chain = generate_IR(arg_node);
                    if (!arg_chain) return NULL;
                    IR* arg_last = arg_chain;
                    while (arg_last->next) arg_last = arg_last->next;
                    char* src_temp = arg_last->result;
                    DataType src_dt = arg_last->data_type;

                    IR* chain = arg_chain;
                    IR* tail = chain;
                    while (tail->next) tail = tail->next;

                    if (src_dt == DATA_STR) {
                        IR* push_str = create_IR(IR_PUSH);
                        push_str->args1 = strdup(src_temp);
                        push_str->args1_type = DATA_STR;
                        push_str->push_bytes = 8;
                        push_str->is_str_param = 1;
                        tail->next = push_str;
                        tail = push_str;
                    } else {
                        IR* ld_ptr = create_IR(IR_DEREF);
                        ld_ptr->result = create_temp();
                        assign_def_id(ld_ptr);
                        ld_ptr->args1 = strdup(src_temp);
                        ld_ptr->args1_type = DATA_INT;
                        ld_ptr->data_type = DATA_INT;
                        ld_ptr->ptr_depth = 0;
                        tail->next = ld_ptr;
                        tail = ld_ptr;
                        char* ptr_temp = ld_ptr->result;

                        IR* addr4 = create_IR(IR_BINARY);
                        addr4->op = IR_ADD;
                        addr4->args1 = strdup(src_temp);
                        addr4->args1_type = DATA_INT;
                        addr4->args2 = strdup("4");
                        addr4->args2_type = DATA_INT;
                        addr4->result = create_temp();
                        assign_def_id(addr4);
                        addr4->data_type = DATA_INT;
                        tail->next = addr4;
                        tail = addr4;

                        IR* ld_len = create_IR(IR_DEREF);
                        ld_len->result = create_temp();
                        assign_def_id(ld_len);
                        ld_len->args1 = strdup(addr4->result);
                        ld_len->args1_type = DATA_INT;
                        ld_len->data_type = DATA_INT;
                        ld_len->ptr_depth = 0;
                        tail->next = ld_len;
                        tail = ld_len;

                        IR* push_len = create_IR(IR_PUSH);
                        push_len->args1 = strdup(ld_len->result);
                        push_len->args1_type = DATA_INT;
                        push_len->push_bytes = 4;
                        tail->next = push_len;
                        tail = push_len;

                        IR* push_ptr = create_IR(IR_PUSH);
                        push_ptr->args1 = strdup(ptr_temp);
                        push_ptr->args1_type = DATA_INT;
                        push_ptr->push_bytes = 4;
                        tail->next = push_ptr;
                        tail = push_ptr;
                    }

                    IR* call_ir = create_IR(IR_CALL);
                    call_ir->args1 = strdup("_str_to_string");
                    call_ir->result = create_temp();
                    assign_def_id(call_ir);
                    call_ir->data_type = DATA_STRING;
                    call_ir->call_func_args_num = 2;
                    call_ir->total_push_bytes = 8;
                    tail->next = call_ir;

                    return chain;
                }
            }
            IR* calc_chain = NULL;
            IR* calc_last = NULL;
            IR* callee_ir_chain = NULL;
            char* callee_temp = NULL;
            int arg_count = 0;

            int max_args = ast_literal->call_function_ParameterCount;
            char** arg_temps = malloc(max_args * sizeof(char*));
            DataType* arg_types = malloc(max_args * sizeof(DataType));
            if (ast_literal->is_indirect_call && ast_literal->param_byte_sizes == NULL && max_args > 0) {
                ast_literal->param_byte_sizes = malloc(sizeof(int) * max_args);
                for (int i = 0; i < max_args; i++) {
                    ast_literal->param_byte_sizes[i] = 4;
                }
            }
            int ir_arg_pos = 0;
            ASTNode* arg_node = ast_literal->call_function_Parameter;
            while (arg_node) {
                if (arg_node->type == AST_VARARG_GROUP) {
                    AST_VarargGroup* vg = (AST_VarargGroup*)arg_node;
                    if (vg->count == 1 && vg->elements
                        && vg->elements->type == AST_ADDRESS_OF) {
                        IR* arg_ir = generate_IR(vg->elements);
                        IR* last = arg_ir;
                        while (last->next) last = last->next;
                        if (arg_count >= max_args) {
                            max_args = (max_args == 0) ? 4 : max_args * 2;
                            arg_temps = realloc(arg_temps, sizeof(char*) * max_args);
                            arg_types = realloc(arg_types, sizeof(DataType) * max_args);
                        }
                        arg_temps[arg_count] = strdup(last->result);
                        arg_types[arg_count] = DATA_INT;
                        if (calc_chain == NULL) {
                            calc_chain = arg_ir;
                            calc_last = last;
                        } else {
                            calc_last->next = arg_ir;
                            calc_last = last;
                        }
                        arg_node = arg_node->next;
                        arg_count++;
                        continue;
                    }
                    int vg_elem_size = 4;
                    if (vg->target_elem_type != DATA_ERROR) {
                        vg_elem_size = get_type_size(vg->target_elem_type);
                        if (vg_elem_size < 4) vg_elem_size = 4;
                    }

                    IR* vg_alloc = create_IR(IR_ALLOC_ARRAY);
                    vg_alloc->result = create_temp();
                    assign_def_id(vg_alloc);
                    char vg_cnt_str[16];
                    snprintf(vg_cnt_str, sizeof(vg_cnt_str), "%d", vg->count);
                    vg_alloc->args1 = strdup(vg_cnt_str);
                    vg_alloc->args1_type = DATA_INT;
                    vg_alloc->data_type = DATA_INT;
                    vg_alloc->elem_size = vg_elem_size;

                    if (calc_chain == NULL) {
                        calc_chain = vg_alloc;
                        calc_last = vg_alloc;
                    } else {
                        calc_last->next = vg_alloc;
                        calc_last = vg_alloc;
                    }
                    char* vg_arr_ptr = vg_alloc->result;

                    ASTNode* vg_elem = vg->elements;
                    int vg_ei = 0;
                    while (vg_elem) {
                        ASTNode* vg_next = vg_elem->next;
                        vg_elem->next = NULL;
                        IR* vg_elem_ir = generate_IR(vg_elem);
                        IR* vg_elem_last = vg_elem_ir;
                        while (vg_elem_last->next) vg_elem_last = vg_elem_last->next;
                        calc_last->next = vg_elem_ir;
                        calc_last = vg_elem_last;

                        IR* vg_idx_lit = create_IR(IR_LITERAL);
                        vg_idx_lit->result = create_temp();
                        assign_def_id(vg_idx_lit);
                        char vg_ei_str[16];
                        snprintf(vg_ei_str, sizeof(vg_ei_str), "%d", vg_ei);
                        vg_idx_lit->args1 = strdup(vg_ei_str);
                        vg_idx_lit->args1_type = DATA_INT;
                        vg_idx_lit->data_type = DATA_INT;
                        calc_last->next = vg_idx_lit;
                        calc_last = vg_idx_lit;

                        IR* vg_mul = create_IR(IR_BINARY);
                        vg_mul->op = IR_MUL;
                        vg_mul->args1 = strdup(vg_idx_lit->result);
                        vg_mul->args1_type = DATA_INT;
                        char vg_es_str[16];
                        snprintf(vg_es_str, sizeof(vg_es_str), "%d", vg_elem_size);
                        vg_mul->args2 = strdup(vg_es_str);
                        vg_mul->args2_type = DATA_INT;
                        vg_mul->result = create_temp();
                        assign_def_id(vg_mul);
                        vg_mul->data_type = DATA_INT;
                        calc_last->next = vg_mul;
                        calc_last = vg_mul;

                        IR* vg_add = create_IR(IR_BINARY);
                        vg_add->op = IR_ADD;
                        vg_add->args1 = strdup(vg_arr_ptr);
                        vg_add->args1_type = DATA_INT;
                        vg_add->args2 = strdup(vg_mul->result);
                        vg_add->args2_type = DATA_INT;
                        vg_add->result = create_temp();
                        assign_def_id(vg_add);
                        vg_add->data_type = DATA_INT;
                        calc_last->next = vg_add;
                        calc_last = vg_add;

                        IR* vg_store = create_IR(IR_ARRAY_STORE);
                        vg_store->args1 = strdup(vg_add->result);
                        vg_store->args1_type = DATA_INT;
                        vg_store->args2 = strdup(vg_elem_last->result);
                        vg_store->args2_type = vg_elem_last->data_type;
                        vg_store->data_type = vg_elem_last->data_type;
                        vg_store->elem_size = vg_elem_size;
                        calc_last->next = vg_store;
                        calc_last = vg_store;

                        vg_ei++;
                        vg_elem = vg_next;
                    }

                    IR* vg_cnt_lit = create_IR(IR_LITERAL);
                    vg_cnt_lit->result = create_temp();
                    assign_def_id(vg_cnt_lit);
                    vg_cnt_lit->args1 = strdup(vg_cnt_str);
                    vg_cnt_lit->args1_type = DATA_INT;
                    vg_cnt_lit->data_type = DATA_INT;
                    calc_last->next = vg_cnt_lit;
                    calc_last = vg_cnt_lit;

                    if (arg_count + 2 > max_args) {
                        while (max_args < arg_count + 2) {
                            max_args = (max_args == 0) ? 4 : max_args * 2;
                        }
                        arg_temps = realloc(arg_temps, sizeof(char*) * max_args);
                        arg_types = realloc(arg_types, sizeof(DataType) * max_args);
                    }

                    arg_temps[arg_count] = strdup(vg_arr_ptr);
                    arg_types[arg_count] = DATA_INT;
                    arg_count++;

                    arg_temps[arg_count] = strdup(vg_cnt_lit->result);
                    arg_types[arg_count] = DATA_INT;
                    arg_count++;

                    arg_node = arg_node->next;
                    continue;
                }
                IR* arg_ir = generate_IR(arg_node);
                IR* last = arg_ir;
                while (last->next) last = last->next;

                arg_temps[arg_count] = strdup(last->result);
                arg_types[arg_count] = last->data_type;

                if (last->type == IR_LITERAL && last->args1
                    && strncmp(last->args1, "_var", 4) == 0) {
                    free(arg_temps[arg_count]);
                    arg_temps[arg_count] = strdup(last->args1);
                }

                if (last->type == IR_ADDRESS_OF
                    && last->args1_type == DATA_STR
                    && !last->is_function_addr) {
                    arg_types[arg_count] = DATA_STR;
                } else if (arg_node->type == AST_STR_FROM_STRING) {
                    arg_types[arg_count] = DATA_STR;
                }

                if (calc_chain == NULL) {
                    calc_chain = arg_ir;
                    calc_last = last;
                } else {
                    calc_last->next = arg_ir;
                    calc_last = last;
                }

                arg_node = arg_node->next;
                arg_count++;
                ir_arg_pos++;
            }

            int hidden_for_addr = arg_count - ast_literal->call_function_ParameterCount;
            for (int i = 0; i < arg_count; i++) {
                int pb = 4;
                int orig_i = i - hidden_for_addr;
                if (orig_i >= 0 && ast_literal->param_byte_sizes != NULL
                    && orig_i < ast_literal->call_function_ParameterCount) {
                    pb = ast_literal->param_byte_sizes[orig_i];
                }
                if (pb > 4 && arg_types[i] != DATA_STR) {
                    ASTNode* a = ast_literal->call_function_Parameter;
                    for (int j = 0; j < i && a; j++) a = a->next;
                    if (a) {
                        char* addr_name = NULL;
                        bool is_dyn = false;
                        if (a->type == AST_LITERAL) {
                            AST_Literal* lit = (AST_Literal*)a;
                            if (!lit->is_call_function) {
                                addr_name = lit->internal_name;
                                if (lit->is_dynamic_array) {
                                    is_dyn = true;
                                }
                            }
                        } else if (a->type == AST_VAR || a->type == AST_VAR_ASS) {
                            AST_Var* av = (AST_Var*)a;
                            addr_name = av->internal_name;
                            if (av->Value && av->Value->type == AST_NEW_ARRAY) {
                                is_dyn = true;
                            }
                            if (av->is_dynamic_array) {
                                is_dyn = true;
                            }
                        }
                        if (addr_name && !is_dyn && pb >= 8) {
                            if (a->type == AST_LITERAL) {
                                AST_Literal* dl = (AST_Literal*)a;
                                if (dl->dataType == DATA_INT && dl->is_dynamic_array) {
                                    is_dyn = true;
                                }
                            } else if (a->type == AST_VAR || a->type == AST_VAR_ASS) {
                                if (((AST_Var*)a)->is_dynamic_array) is_dyn = true;
                            }
                        }
                        if (addr_name && !is_dyn && pb >= 8) {
                            is_dyn = true;
                        }
                        if (addr_name) {
                            if (is_dyn) {
                                free(arg_temps[i]);
                                arg_temps[i] = strdup(addr_name);
                                arg_types[i] = DATA_INT;
                            } else {
                                IR* addr_ir = create_IR(IR_ADDRESS_OF);
                                addr_ir->result = create_temp();
                                assign_def_id(addr_ir);
                                addr_ir->args1 = strdup(addr_name);
                                addr_ir->args1_type = DATA_INT;
                                addr_ir->data_type = DATA_INT;
                                addr_ir->is_function_addr = false;

                                calc_last->next = addr_ir;
                                calc_last = addr_ir;

                                free(arg_temps[i]);
                                arg_temps[i] = strdup(addr_ir->result);
                                arg_types[i] = DATA_INT;
                            }
                        }
                    }
                }
            }

            if (ast_literal->dataType == DATA_STR && ast_literal->return_total_bytes == 0) {
                ast_literal->returns_array = true;
                ast_literal->return_total_bytes = 8;
            }

            bool callee_returns_array = ast_literal->returns_array;
            if (ast_literal->multi_return_type != NULL) {
                callee_returns_array = true;
                if (ast_literal->return_total_bytes <= 0) {
                    int total = 0;
                    for (int i = 0; i < ast_literal->multi_return_type->count; i++) {
                        int mr_sz = ast_literal->multi_return_type->is_variadic[i]
                                    ? 8 : get_type_size(ast_literal->multi_return_type->types[i]);
                        if (mr_sz < 4) mr_sz = 4;
                        total += mr_sz;
                    }
                    if (total < 4) total = 4;
                    ast_literal->return_total_bytes = total;
                }
            }

            if (ast_literal->is_indirect_call
                && ast_literal->indirect_callee != NULL
                && ast_literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                AST_ReturnExtract* ext =
                    (AST_ReturnExtract*)ast_literal->indirect_callee;
                callee_returns_array = false;
                ast_literal->return_total_bytes = 0;
                if (ext->extracted_func_sig != NULL
                    && ext->extracted_func_sig->multi_return != NULL
                    && ext->extracted_func_sig->ret_func_sig == NULL) {
                    MultiReturnType* mrt = ext->extracted_func_sig->multi_return;
                    bool all_func_ptrs = true;
                    if (mrt->func_sigs != NULL) {
                        for (int i = 0; i < mrt->count; i++) {
                            if (mrt->func_sigs[i] == NULL) {
                                all_func_ptrs = false;
                                break;
                            }
                        }
                    } else {
                        all_func_ptrs = false;
                    }
                    if (ext->extracted_func_sig->return_type == DATA_INT) {
                        all_func_ptrs = true;
                    }
                    if (!all_func_ptrs) {
                        callee_returns_array = true;
                        int total = 0;
                        for (int i = 0; i < mrt->count; i++)
                            total += mrt->is_variadic[i] ? 8 : 4;
                        if (total < 4) total = 4;
                        ast_literal->return_total_bytes = total;
                    }
                }
            }

            char* callee_ret_buf = NULL;
            int callee_ret_total_bytes = ast_literal->return_total_bytes;

            if (!ast_literal->is_indirect_call && ast_literal->multi_return_type == NULL &&
                !ast_literal->returns_array) {
                callee_returns_array = false;
            }

            if (callee_returns_array && callee_ret_total_bytes > 0) {
                if (callee_ret_total_bytes < 4)
                    callee_ret_total_bytes = 4;
                callee_ret_buf = create_temp();
                IR* alloc = create_IR(IR_ALLOC_ARRAY);
                alloc->result = strdup(callee_ret_buf);
                alloc->args1 = strdup("1");
                alloc->args1_type = DATA_INT;
                alloc->elem_size = callee_ret_total_bytes;
                alloc->data_type = DATA_INT;
                if (ast_literal->multi_return_type != NULL) {
                    alloc->stack_bytes = 0;
                } else {
                    alloc->stack_bytes = callee_ret_total_bytes;
                }
                assign_def_id(alloc);
                if (calc_last) {
                    calc_last->next = alloc;
                    calc_last = alloc;
                } else {
                    calc_chain = alloc;
                    calc_last = alloc;
                }
            }
            if (callee_returns_array && callee_ret_buf) {
                char** new_temps = malloc((size_t)(arg_count + 1) * sizeof(char*));
                DataType* new_types = malloc((size_t)(arg_count + 1) * sizeof(DataType));
                new_temps[0] = strdup(callee_ret_buf);
                new_types[0] = DATA_INT;
                for (int i = 0; i < arg_count; i++) {
                    new_temps[i + 1] = arg_temps[i];
                    new_types[i + 1] = arg_types[i];
                }
                free(arg_temps);
                free(arg_types);
                arg_temps = new_temps;
                arg_types = new_types;
                arg_count++;
            }

            IR* push_chain_ = NULL;
            IR* push_last_ = NULL;
            int total_push = 0;
            int hidden_for_push = arg_count - ast_literal->call_function_ParameterCount;
            for (int i = arg_count - 1; i >= 0; i--) {
                IR* push_ir = create_IR(IR_PUSH);
                push_ir->args1 = strdup(arg_temps[i]);
                push_ir->args1_type = arg_types[i];
                int pb = 4;
                int orig_idx = i - hidden_for_push;
                if (orig_idx >= 0
                    && ast_literal->param_byte_sizes != NULL
                    && orig_idx < ast_literal->call_function_ParameterCount) {
                    pb = ast_literal->param_byte_sizes[orig_idx];
                }
                if (arg_types[i] == DATA_STR) {
                    pb = 8;
                    push_ir->is_str_param = 1;
                }
                if ((arg_types[i] == DATA_INT64 || arg_types[i] == DATA_UINT64) && pb < 8) {
                    pb = 8;
                }
                push_ir->push_bytes = pb;
                total_push += push_ir->push_bytes;
                push_ir->next = NULL;
                if (push_chain_ == NULL) {
                    push_chain_ = push_last_ = push_ir;
                } else {
                    push_last_->next = push_ir;
                    push_last_ = push_ir;
                }
            }

            if (ast_literal->is_indirect_call && ast_literal->indirect_callee != NULL) {
                callee_ir_chain = generate_IR(ast_literal->indirect_callee);
                IR* clast = callee_ir_chain;
                while (clast->next) clast = clast->next;
                callee_temp = clast->result;

                if (ast_literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* callee_lit =
                        (AST_Literal*)ast_literal->indirect_callee;
                    if (callee_lit->is_call_function
                        && callee_lit->call_result_class_name != NULL
                        && strcmp(callee_lit->call_result_class_name,
                                  "Function") == 0) {
                        IR* load_raw = create_IR(IR_LOAD_MEMBER);
                        load_raw->result = create_temp();
                        assign_def_id(load_raw);
                        load_raw->args1 = strdup(callee_temp);
                        load_raw->args1_type = DATA_INT;
                        load_raw->args2 = strdup("8");
                        load_raw->data_type = DATA_INT;
                        clast->next = load_raw;
                        callee_temp = load_raw->result;
                                  }
                }
            } else if (ast_literal->is_indirect_call
                     && ast_literal->is_field_ref) {
                IR* this_ir = create_IR(IR_LOAD_THIS);
                this_ir->result = create_temp();
                assign_def_id(this_ir);
                this_ir->data_type = DATA_INT;

                IR* load = create_IR(IR_LOAD_MEMBER);
                load->result = create_temp();
                assign_def_id(load);
                load->args1 = strdup(this_ir->result);
                load->args1_type = DATA_INT;
                char off_str[16];
                sprintf(off_str, "%d", ast_literal->field_offset);
                load->args2 = strdup(off_str);
                load->data_type = DATA_INT;

                this_ir->next = load;
                callee_ir_chain = this_ir;
                callee_temp = load->result;
            } else if (ast_literal->is_indirect_call && ast_literal->is_closure_call
                     && ast_literal->indirect_callee == NULL
                     && ast_literal->internal_name != NULL) {
                IR* ref_ir = create_IR(IR_LITERAL);
                ref_ir->result = create_temp();
                assign_def_id(ref_ir);
                ref_ir->args1 = strdup(ast_literal->internal_name);
                ref_ir->args1_type = DATA_INT;
                ref_ir->data_type = DATA_INT;
                callee_ir_chain = ref_ir;
                callee_temp = ref_ir->result;
            }
            if (callee_ir_chain != NULL) {
                IR* ctail = callee_ir_chain;
                while (ctail->next) ctail = ctail->next;
                if (calc_chain != NULL) {
                    ctail->next = calc_chain;
                    calc_chain = callee_ir_chain;
                } else {
                    calc_chain = callee_ir_chain;
                    calc_last = ctail;
                }
            }
            if (calc_last) {
                calc_last->next = push_chain_;
            } else {
                calc_chain = push_chain_;
                calc_last = push_last_;
            }

            IR* call_ir;

            if (ast_literal->is_indirect_call
                && ast_literal->indirect_callee != NULL
                && ast_literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                AST_ReturnExtract* ext =
                    (AST_ReturnExtract*)ast_literal->indirect_callee;
                if (ext->extracted_func_sig != NULL) {
                    bool has_real_multi = (ext->extracted_func_sig->multi_return != NULL
                                            && ext->extracted_func_sig->ret_func_sig == NULL);
                    if (has_real_multi) {
                        MultiReturnType* mrt = ext->extracted_func_sig->multi_return;
                        bool all_func_ptrs = true;
                        if (mrt->func_sigs != NULL) {
                            for (int i = 0; i < mrt->count; i++) {
                                if (mrt->func_sigs[i] == NULL) {
                                    all_func_ptrs = false;
                                    break;
                                }
                            }
                        } else {
                            all_func_ptrs = false;
                        }
                        if (ext->extracted_func_sig->return_type == DATA_INT) {
                            all_func_ptrs = true;
                        }
                        if (all_func_ptrs) {
                            has_real_multi = false;
                        }
                    }
                    callee_returns_array = has_real_multi;
                    if (callee_returns_array) {
                        int total = 0;
                        for (int i = 0; i < ext->extracted_func_sig->multi_return->count; i++)
                            total += ext->extracted_func_sig->multi_return->is_variadic[i] ? 8 : 4;
                        if (total < 4) total = 4;
                        ast_literal->return_total_bytes = total;
                    } else {
                        callee_returns_array = false;
                        ast_literal->return_total_bytes = 0;
                    }
                } else {
                    callee_returns_array = false;
                    ast_literal->return_total_bytes = 0;
                }
            }

            if (ast_literal->is_indirect_call) {
                if (ast_literal->is_closure_call
                    && callee_temp != NULL) {
                    IR* load_func = create_IR(IR_DEREF);
                    load_func->result = create_temp();
                    assign_def_id(load_func);
                    load_func->args1 = strdup(callee_temp);
                    load_func->args1_type = DATA_INT;
                    load_func->data_type = DATA_INT;
                    load_func->ptr_depth = 0;
                    if (calc_last) {
                        calc_last->next = load_func;
                        calc_last = load_func;
                    } else {
                        calc_chain = calc_last = load_func;
                    }
                    char* func_ptr_temp = load_func->result;

                    char** new_temps = malloc((size_t)(arg_count + 1) * sizeof(char*));
                    DataType* new_types = malloc((size_t)(arg_count + 1) * sizeof(DataType));
                    new_temps[0] = strdup(callee_temp);
                    new_types[0] = DATA_INT;
                    for (int i = 0; i < arg_count; i++) {
                        new_temps[i + 1] = arg_temps[i];
                        new_types[i + 1] = arg_types[i];
                    }
                    free(arg_temps);
                    free(arg_types);
                    arg_temps = new_temps;
                    arg_types = new_types;
                    arg_count++;

                    IR* pp = push_chain_;
                    while (pp) {
                        IR* np = pp->next;
                        free(pp->args1);
                        free(pp);
                        pp = np;
                    }
                    push_chain_ = push_last_ = NULL;
                    total_push = 0;
                    for (int i = arg_count - 1; i >= 0; i--) {
                        IR* push_ir = create_IR(IR_PUSH);
                        push_ir->args1 = strdup(arg_temps[i]);
                        push_ir->args1_type = arg_types[i];
                        push_ir->push_bytes = 4;
                        total_push += 4;
                        push_ir->next = NULL;
                        if (push_chain_ == NULL) {
                            push_chain_ = push_last_ = push_ir;
                        } else {
                            push_last_->next = push_ir;
                            push_last_ = push_ir;
                        }
                    }

                    if (calc_last) {
                        calc_last->next = push_chain_;
                    } else {
                        calc_chain = push_chain_;
                        calc_last = push_last_;
                    }

                    call_ir = create_IR(IR_CALL_INDIRECT);
                    call_ir->args1 = strdup(func_ptr_temp);
                    call_ir->args1_type = DATA_INT;
                } else {
                    call_ir = create_IR(IR_CALL_INDIRECT);
                    if (callee_temp != NULL) {
                        call_ir->args1 = strdup(callee_temp);
                    } else {
                        call_ir->args1 = strdup(ast_literal->internal_name);
                    }
                }
            } else {
                call_ir = create_IR(IR_CALL);
                if (ast_literal->internal_name != NULL) {
                    call_ir->args1 = strdup(ast_literal->internal_name);
                } else if (ast_literal->base.value.value != NULL) {
                    call_ir->args1 = strdup(ast_literal->base.value.value);
                } else {
                    call_ir->args1 = strdup("_undefined_function_");
                }
            }
            call_ir->result = create_temp();
            assign_def_id(call_ir);
            call_ir->data_type = (ast_literal->ptr_depth > 0)
                                 ? DATA_INT : ast_literal->dataType;
            call_ir->call_func_args_num = arg_count;
            call_ir->total_push_bytes = total_push;
            int str_alloc_total = 0;
            IR* scan = calc_chain;
            while (scan) {
                if (scan->type == IR_ALLOC_ARRAY && scan->stack_bytes > 0) {
                    if (scan->result == NULL || scan->result[0] != '.') {
                        str_alloc_total += scan->stack_bytes;
                    }
                }
                if (scan == call_ir) break;
                scan = scan->next;
            }
            call_ir->temp_stack_alloc = 0;
            if (push_last_) {
                push_last_->next = call_ir;
            } else if (calc_last) {
                calc_last->next = call_ir;
            } else {
                calc_chain = call_ir;
            }

            for (int i = 0; i < arg_count; i++) {
                free(arg_temps[i]);
            }
            free(arg_temps);
            free(arg_types);

            if (callee_returns_array && callee_ret_buf &&
                (callee_ret_total_bytes > 4 || ast_literal->multi_return_type != NULL)) {
                IR* pass_buf = create_IR(IR_LITERAL);
                pass_buf->result = create_temp();
                pass_buf->args1 = strdup(pass_buf->result);
                pass_buf->args1_type = ast_literal->dataType;
                pass_buf->data_type = ast_literal->dataType;
                pass_buf->returns_array = true;
                pass_buf->hidden_ret_buf = strdup(callee_ret_buf);
                call_ir->next = pass_buf;
                if (ast_literal->multi_return_type != NULL && callee_ret_buf != NULL) {
                    ast_literal->multi_ret_buf = strdup(callee_ret_buf);
                }
            }
            return calc_chain;
        } else if (ast_literal->is_field_ref) {
            if (ast_literal->field_offset < 0) {
                IR* lit = create_IR(IR_LITERAL);
                lit->result = create_temp();
                assign_def_id(lit);
                lit->args1 = strdup(ast_literal->internal_name);
                lit->data_type = ast_literal->dataType;
                return lit;
            }
            IR* this_ir = create_IR(IR_LOAD_THIS);
            this_ir->result = create_temp();
            assign_def_id(this_ir);
            this_ir->data_type = DATA_INT;

            if (ast_literal->is_field_array) {
                IR* off_lit = create_IR(IR_LITERAL);
                off_lit->result = create_temp();
                assign_def_id(off_lit);
                char off_str[16];
                sprintf(off_str, "%d", ast_literal->field_offset);
                off_lit->args1 = strdup(off_str);
                off_lit->args1_type = DATA_INT;
                off_lit->data_type = DATA_INT;

                IR* add_ir = create_IR(IR_BINARY);
                add_ir->op = IR_ADD;
                add_ir->args1 = strdup(this_ir->result);
                add_ir->args1_type = DATA_INT;
                add_ir->args2 = strdup(off_lit->result);
                add_ir->args2_type = DATA_INT;
                add_ir->result = create_temp();
                assign_def_id(add_ir);
                add_ir->data_type = DATA_INT;

                IR* chain = connect_IR(this_ir, off_lit);
                chain = connect_IR(chain, add_ir);
                return chain;
            }

            IR* load_member = create_IR(IR_LOAD_MEMBER);
            load_member->result = create_temp();
            assign_def_id(load_member);
            load_member->args1 = strdup(this_ir->result);
            load_member->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", ast_literal->field_offset);
            load_member->args2 = strdup(off_str);
            load_member->data_type = ast_literal->dataType;
            IR* chain = connect_IR(this_ir, load_member);
            return chain;
        } else if (ast_literal->is_outer_field_ref) {
            IR* outer_load = create_IR(IR_LITERAL);
            outer_load->result = create_temp();
            assign_def_id(outer_load);
            outer_load->args1 = strdup("_outer");
            outer_load->args1_type = DATA_INT;
            outer_load->data_type = DATA_INT;

            IR* field_load = create_IR(IR_LOAD_MEMBER);
            field_load->result = create_temp();
            assign_def_id(field_load);
            field_load->args1 = strdup(outer_load->result);
            field_load->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", ast_literal->field_offset);
            field_load->args2 = strdup(off_str);
            field_load->data_type = ast_literal->dataType;

            IR* chain = connect_IR(outer_load, field_load);
            return chain;
        }

        if (!ast_literal->is_call_function
            && ast_literal->base.value.type == TOKEN_IDENTIFIER
            && ast_literal->closure_func != NULL
            && ast_literal->closure_func->is_closure
            && ast_literal->closure_func->upvalue_count > 0) {
            AST_Function* target = ast_literal->closure_func;
            int cl_size = 4 + 4 * target->upvalue_count;
            IR* alloc_ir = create_IR(IR_ALLOC_ARRAY);
            alloc_ir->result = create_temp();
            assign_def_id(alloc_ir);
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", cl_size);
            alloc_ir->args1 = strdup(sz_buf);
            alloc_ir->args1_type = DATA_INT;
            alloc_ir->elem_size = 1;
            alloc_ir->data_type = DATA_INT;
            IR* chain = alloc_ir;
            IR* tail  = alloc_ir;

            IR* fn_addr = create_IR(IR_ADDRESS_OF);
            fn_addr->result = create_temp();
            assign_def_id(fn_addr);
            fn_addr->args1 = strdup(target->internal_name);
            fn_addr->args1_type = DATA_INT;
            fn_addr->data_type = DATA_INT;
            fn_addr->is_function_addr = true;
            tail->next = fn_addr;
            tail = fn_addr;

            IR* store_fn = create_IR(IR_PTR_STORE);
            store_fn->args1 = strdup(alloc_ir->result);
            store_fn->args1_type = DATA_INT;
            store_fn->args2 = strdup(fn_addr->result);
            store_fn->args2_type = DATA_INT;
            store_fn->data_type = DATA_INT;
            tail->next = store_fn;
            tail = store_fn;

            int off = 4;
            for (int u = 0; u < target->upvalue_count; u++) {
                IR* off_ir = create_IR(IR_BINARY);
                off_ir->op = IR_ADD;
                off_ir->args1 = strdup(alloc_ir->result);
                off_ir->args1_type = DATA_INT;
                char ostr[16];
                snprintf(ostr, sizeof(ostr), "%d", off);
                off_ir->args2 = strdup(ostr);
                off_ir->args2_type = DATA_INT;
                off_ir->result = create_temp();
                assign_def_id(off_ir);
                off_ir->data_type = DATA_INT;
                tail->next = off_ir;
                tail = off_ir;

                IR* val_ir = create_IR(IR_LITERAL);
                val_ir->result = create_temp();
                assign_def_id(val_ir);
                val_ir->args1 = strdup(
                    target->upvalue_internal_names
                    ? target->upvalue_internal_names[u] : "0");
                val_ir->args1_type = DATA_INT;
                val_ir->data_type = target->upvalue_types
                    ? target->upvalue_types[u] : DATA_INT;
                tail->next = val_ir;
                tail = val_ir;

                IR* store_ir = create_IR(IR_PTR_STORE);
                store_ir->args1 = strdup(off_ir->result);
                store_ir->args1_type = DATA_INT;
                store_ir->args2 = strdup(val_ir->result);
                store_ir->args2_type = DATA_INT;
                store_ir->data_type = DATA_INT;
                tail->next = store_ir;
                tail = store_ir;

                off += 4;
            }

            IR* result_ir = create_IR(IR_LITERAL);
            result_ir->result = create_temp();
            assign_def_id(result_ir);
            result_ir->args1 = strdup(alloc_ir->result);
            result_ir->args1_type = DATA_INT;
            result_ir->data_type = DATA_INT;
            tail->next = result_ir;

            free(Literal_ir->result);
            free(Literal_ir->args1);
            free(Literal_ir);
            return chain;
        }

        if (ast_literal->dataType == DATA_STR
        && !ast_literal->is_call_function
        && ast_literal->internal_name != NULL
        && ast_literal->base.value.type != TOKEN_IDENTIFIER)
        {
            if (Literal_ir->args1) free(Literal_ir->args1);
            Literal_ir->args1 = strdup("0");
            Literal_ir->args1_type = DATA_INT;
            Literal_ir->data_type = DATA_STR;

            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(Literal_ir->result);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;
            dst_addr->is_function_addr = false;

            IR* src_addr = create_IR(IR_ADDRESS_OF);
            src_addr->result = create_temp();
            assign_def_id(src_addr);
            src_addr->args1 = strdup(ast_literal->internal_name);
            src_addr->args1_type = DATA_INT;
            src_addr->data_type = DATA_INT;
            src_addr->is_function_addr = false;

            IR* copy = create_IR(IR_COPY_BLOCK);
            assign_def_id(copy);
            copy->args1 = strdup(dst_addr->result);
            copy->args1_type = DATA_INT;
            copy->args2 = strdup(src_addr->result);
            copy->args2_type = DATA_INT;
            copy->result = strdup("8");
            copy->copy_bytes = 8;
            copy->data_type = DATA_INT;

            IR* result_pass = create_IR(IR_LITERAL);
            result_pass->result = strdup(Literal_ir->result);
            assign_def_id(result_pass);
            result_pass->args1 = strdup(Literal_ir->result);
            result_pass->args1_type = DATA_STR;
            result_pass->data_type = DATA_STR;

            Literal_ir->next = dst_addr;
            dst_addr->next = src_addr;
            src_addr->next = copy;
            copy->next = result_pass;

            return Literal_ir;
        }

        return Literal_ir;

    case AST_BINARY:
        AST_Binary* ast_binary = (AST_Binary*)ast_node;
            
        IR* folded_ir = try_fold_const_binary(ast_binary);
        if (folded_ir != NULL) {
            return folded_ir;
        }

        if (ast_binary->op.type == TOKEN_ADD) {
            DataType lt = DATA_ERROR;
            DataType rt = DATA_ERROR;
            const char* left_name = NULL;
            const char* right_name = NULL;

            if (ast_binary->left->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)ast_binary->left;
                if (!lit->is_call_function) {
                    lt = lit->dataType;
                    left_name = lit->internal_name;
                    if (!left_name) left_name = lit->base.value.value;
                }
            } else if (ast_binary->left->type == AST_VAR ||
                       ast_binary->left->type == AST_VAR_ASS) {
                AST_Var* var = (AST_Var*)ast_binary->left;
                lt = var->dataType;
                left_name = var->internal_name;
                if (!left_name) left_name = var->name.value;
            }

            if (ast_binary->right->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)ast_binary->right;
                if (!lit->is_call_function) {
                    rt = lit->dataType;
                    right_name = lit->internal_name;
                    if (!right_name) right_name = lit->base.value.value;
                }
            } else if (ast_binary->right->type == AST_VAR ||
                       ast_binary->right->type == AST_VAR_ASS) {
                AST_Var* var = (AST_Var*)ast_binary->right;
                rt = var->dataType;
                right_name = var->internal_name;
                if (!right_name) right_name = var->name.value;
            }

            if (lt == DATA_STR && rt == DATA_STR &&
                left_name != NULL && right_name != NULL) {

                IR* chain = NULL;
                IR* tail = NULL;

                #define ADD_IR(node) do { \
                    if (!chain) { chain = tail = (node); } \
                    else { tail->next = (node); tail = (node); } \
                } while(0)

                char* names[2] = { (char*)right_name, (char*)left_name };
                for (int side = 0; side < 2; side++) {
                    IR* addr = create_IR(IR_ADDRESS_OF);
                    addr->result = create_temp();
                    assign_def_id(addr);
                    addr->args1 = strdup(names[side]);
                    addr->args1_type = DATA_STR;
                    addr->data_type = DATA_INT;
                    addr->is_function_addr = false;
                    ADD_IR(addr);

                    IR* offset = create_IR(IR_BINARY);
                    offset->op = IR_ADD;
                    offset->args1 = strdup(addr->result);
                    offset->args1_type = DATA_INT;
                    offset->args2 = strdup("4");
                    offset->args2_type = DATA_INT;
                    offset->result = create_temp();
                    assign_def_id(offset);
                    offset->data_type = DATA_INT;
                    ADD_IR(offset);

                    IR* ld_len = create_IR(IR_DEREF);
                    ld_len->result = create_temp();
                    assign_def_id(ld_len);
                    ld_len->args1 = strdup(offset->result);
                    ld_len->args1_type = DATA_INT;
                    ld_len->data_type = DATA_INT;
                    ld_len->ptr_depth = 0;
                    ADD_IR(ld_len);

                    IR* push_len = create_IR(IR_PUSH);
                    push_len->args1 = strdup(ld_len->result);
                    push_len->args1_type = DATA_INT;
                    push_len->push_bytes = 4;
                    ADD_IR(push_len);

                    IR* ld_ptr = create_IR(IR_LITERAL);
                    ld_ptr->result = create_temp();
                    assign_def_id(ld_ptr);
                    ld_ptr->args1 = strdup(names[side]);
                    ld_ptr->args1_type = DATA_STR;
                    ld_ptr->data_type = DATA_INT;
                    ADD_IR(ld_ptr);

                    IR* push_ptr = create_IR(IR_PUSH);
                    push_ptr->args1 = strdup(ld_ptr->result);
                    push_ptr->args1_type = DATA_INT;
                    push_ptr->push_bytes = 4;
                    ADD_IR(push_ptr);
                }

                IR* call_ir = create_IR(IR_CALL);
                call_ir->args1 = strdup("_str_concat_str");
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = DATA_STRING;
                call_ir->call_func_args_num = 4;
                call_ir->total_push_bytes = 16;
                ADD_IR(call_ir);

                #undef ADD_IR
                return chain;
            }

            if ((lt == DATA_STRING && rt == DATA_STR) ||
                (lt == DATA_STR && rt == DATA_STRING)) {
                IR* chain = NULL;
                IR* tail = NULL;
                #define ADD_IR(node) do { \
                    if (!chain) { chain = tail = (node); } \
                    else { tail->next = (node); tail = (node); } \
                } while(0)

                const char* str_side_name = NULL;
                const char* string_side_name = NULL;

                if (lt == DATA_STR) {
                    str_side_name = left_name;
                    string_side_name = right_name;
                } else {
                    str_side_name = right_name;
                    string_side_name = left_name;
                }

                IR* str_ptr = create_IR(IR_LITERAL);
                str_ptr->result = create_temp();
                assign_def_id(str_ptr);
                str_ptr->args1 = strdup(str_side_name);
                str_ptr->args1_type = DATA_STR;
                str_ptr->data_type = DATA_INT;
                ADD_IR(str_ptr);
                char* str_ptr_temp = str_ptr->result;

                IR* str_addr = create_IR(IR_ADDRESS_OF);
                str_addr->result = create_temp();
                assign_def_id(str_addr);
                str_addr->args1 = strdup(str_side_name);
                str_addr->args1_type = DATA_STR;
                str_addr->data_type = DATA_INT;
                str_addr->is_function_addr = false;
                ADD_IR(str_addr);

                IR* str_off = create_IR(IR_BINARY);
                str_off->op = IR_ADD;
                str_off->args1 = strdup(str_addr->result);
                str_off->args1_type = DATA_INT;
                str_off->args2 = strdup("4");
                str_off->args2_type = DATA_INT;
                str_off->result = create_temp();
                assign_def_id(str_off);
                str_off->data_type = DATA_INT;
                ADD_IR(str_off);

                IR* str_len_load = create_IR(IR_DEREF);
                str_len_load->result = create_temp();
                assign_def_id(str_len_load);
                str_len_load->args1 = strdup(str_off->result);
                str_len_load->args1_type = DATA_INT;
                str_len_load->data_type = DATA_INT;
                str_len_load->ptr_depth = 0;
                ADD_IR(str_len_load);
                char* str_len_temp = str_len_load->result;

                IR* string_ptr_lit = create_IR(IR_LITERAL);
                string_ptr_lit->result = create_temp();
                assign_def_id(string_ptr_lit);
                string_ptr_lit->args1 = strdup(string_side_name);
                string_ptr_lit->args1_type = DATA_STRING;
                string_ptr_lit->data_type = DATA_INT;
                ADD_IR(string_ptr_lit);
                char* string_ptr_temp = string_ptr_lit->result;

                IR* push_for_strlen = create_IR(IR_PUSH);
                push_for_strlen->args1 = strdup(string_ptr_temp);
                push_for_strlen->args1_type = DATA_STRING;
                push_for_strlen->push_bytes = 4;
                ADD_IR(push_for_strlen);

                IR* strlen_call = create_IR(IR_CALL);
                strlen_call->args1 = strdup("_str_length");
                strlen_call->result = create_temp();
                assign_def_id(strlen_call);
                strlen_call->data_type = DATA_INT;
                strlen_call->call_func_args_num = 1;
                strlen_call->total_push_bytes = 4;
                ADD_IR(strlen_call);
                char* string_len_temp = strlen_call->result;

                IR* push_a_len;
                IR* push_a_ptr;
                IR* push_b_len;
                IR* push_b_ptr;

                if (lt == DATA_STR && rt == DATA_STRING) {
                    push_a_len = create_IR(IR_PUSH);
                    push_a_len->args1 = strdup(str_len_temp);
                    push_a_len->args1_type = DATA_INT;
                    push_a_len->push_bytes = 4;

                    push_a_ptr = create_IR(IR_PUSH);
                    push_a_ptr->args1 = strdup(str_ptr_temp);
                    push_a_ptr->args1_type = DATA_INT;
                    push_a_ptr->push_bytes = 4;

                    push_b_len = create_IR(IR_PUSH);
                    push_b_len->args1 = strdup(string_len_temp);
                    push_b_len->args1_type = DATA_INT;
                    push_b_len->push_bytes = 4;

                    push_b_ptr = create_IR(IR_PUSH);
                    push_b_ptr->args1 = strdup(string_ptr_temp);
                    push_b_ptr->args1_type = DATA_INT;
                    push_b_ptr->push_bytes = 4;
                } else {
                    push_a_len = create_IR(IR_PUSH);
                    push_a_len->args1 = strdup(string_len_temp);
                    push_a_len->args1_type = DATA_INT;
                    push_a_len->push_bytes = 4;

                    push_a_ptr = create_IR(IR_PUSH);
                    push_a_ptr->args1 = strdup(string_ptr_temp);
                    push_a_ptr->args1_type = DATA_INT;
                    push_a_ptr->push_bytes = 4;

                    push_b_len = create_IR(IR_PUSH);
                    push_b_len->args1 = strdup(str_len_temp);
                    push_b_len->args1_type = DATA_INT;
                    push_b_len->push_bytes = 4;

                    push_b_ptr = create_IR(IR_PUSH);
                    push_b_ptr->args1 = strdup(str_ptr_temp);
                    push_b_ptr->args1_type = DATA_INT;
                    push_b_ptr->push_bytes = 4;
                }

                ADD_IR(push_b_len);
                ADD_IR(push_b_ptr);
                ADD_IR(push_a_len);
                ADD_IR(push_a_ptr);

                IR* call_concat_str = create_IR(IR_CALL);
                call_concat_str->args1 = strdup("_str_concat_str");
                call_concat_str->result = create_temp();
                assign_def_id(call_concat_str);
                call_concat_str->data_type = DATA_STRING;
                call_concat_str->call_func_args_num = 4;
                call_concat_str->total_push_bytes = 16;
                ADD_IR(call_concat_str);

                #undef ADD_IR
                return chain;
            }
            if ((lt == DATA_STR || lt == DATA_STRING) && rt == DATA_CHAR &&
                left_name != NULL && right_name != NULL) {

                IR* chain = NULL;
                IR* tail = NULL;
                #define ADD_IR(node) do { \
                    if (!chain) { chain = tail = (node); } \
                    else { tail->next = (node); tail = (node); } \
                } while(0)

                IR* ch = create_IR(IR_LITERAL);
                ch->result = create_temp();
                assign_def_id(ch);
                ch->args1 = strdup(right_name);
                ch->args1_type = DATA_CHAR;
                ch->data_type = DATA_INT;
                ADD_IR(ch);

                IR* push_ch = create_IR(IR_PUSH);
                push_ch->args1 = strdup(ch->result);
                push_ch->args1_type = DATA_INT;
                push_ch->push_bytes = 4;
                ADD_IR(push_ch);

                IR* ld = create_IR(IR_LITERAL);
                ld->result = create_temp();
                assign_def_id(ld);
                ld->args1 = strdup(left_name);
                ld->args1_type = DATA_STR;
                ld->data_type = DATA_INT;
                ADD_IR(ld);

                IR* push_str = create_IR(IR_PUSH);
                push_str->args1 = strdup(ld->result);
                push_str->args1_type = DATA_INT;
                push_str->push_bytes = 4;
                ADD_IR(push_str);

                IR* call_ir = create_IR(IR_CALL);
                call_ir->args1 = strdup("_str_plus_char_2");
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = DATA_STRING;
                call_ir->call_func_args_num = 2;
                call_ir->total_push_bytes = 8;
                ADD_IR(call_ir);

                #undef ADD_IR
                return chain;
            }

            if (lt == DATA_CHAR && (rt == DATA_STR || rt == DATA_STRING) &&
                left_name != NULL && right_name != NULL) {

                IR* chain = NULL;
                IR* tail = NULL;
                #define ADD_IR(node) do { \
                    if (!chain) { chain = tail = (node); } \
                    else { tail->next = (node); tail = (node); } \
                } while(0)

                IR* ld = create_IR(IR_LITERAL);
                ld->result = create_temp();
                assign_def_id(ld);
                ld->args1 = strdup(right_name);
                ld->args1_type = DATA_STR;
                ld->data_type = DATA_INT;
                ADD_IR(ld);

                IR* push_str = create_IR(IR_PUSH);
                push_str->args1 = strdup(ld->result);
                push_str->args1_type = DATA_INT;
                push_str->push_bytes = 4;
                ADD_IR(push_str);

                IR* ch = create_IR(IR_LITERAL);
                ch->result = create_temp();
                assign_def_id(ch);
                ch->args1 = strdup(left_name);
                ch->args1_type = DATA_CHAR;
                ch->data_type = DATA_INT;
                ADD_IR(ch);

                IR* push_ch = create_IR(IR_PUSH);
                push_ch->args1 = strdup(ch->result);
                push_ch->args1_type = DATA_INT;
                push_ch->push_bytes = 4;
                ADD_IR(push_ch);

                IR* call_ir = create_IR(IR_CALL);
                call_ir->args1 = strdup("_char_plus_str_2");
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = DATA_STRING;
                call_ir->call_func_args_num = 2;
                call_ir->total_push_bytes = 8;
                ADD_IR(call_ir);

                #undef ADD_IR
                return chain;
            }
            if (ast_binary->resultType == DATA_STRING) {
                IR* left_chain  = generate_IR(ast_binary->left);
                IR* right_chain = generate_IR(ast_binary->right);
                if (!left_chain || !right_chain) return NULL;
                IR* left_last  = left_chain;  while(left_last->next)  left_last  = left_last->next;
                IR* right_last = right_chain; while(right_last->next) right_last = right_last->next;
                char* left_temp  = left_last->result;
                char* right_temp = right_last->result;
                IR* chain = left_chain;
                chain = connect_IR(chain, right_chain);
                char* left_len_temp = NULL;

                if (ast_binary->left->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)ast_binary->left;
                    if (lit->base.value.type == TOKEN_STRING_LITERAL) {
                        int slen = string_literal_length(lit->base.value.value);
                        IR* len_lit = create_IR(IR_LITERAL);
                        len_lit->result = create_temp();
                        assign_def_id(len_lit);
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", slen);
                        len_lit->args1 = strdup(buf);
                        len_lit->args1_type = DATA_INT;
                        len_lit->data_type = DATA_INT;
                        chain = connect_IR(chain, len_lit);
                        left_len_temp = len_lit->result;
                    }
                }
                if (!left_len_temp) {
                    IR* push_left = create_IR(IR_PUSH);
                    push_left->args1 = strdup(left_temp);
                    push_left->args1_type = DATA_STRING;
                    push_left->push_bytes = 4;
                    chain = connect_IR(chain, push_left);
                    IR* strlen_left = create_IR(IR_CALL);
                    strlen_left->args1 = strdup("_str_length");
                    strlen_left->result = create_temp();
                    assign_def_id(strlen_left);
                    strlen_left->data_type = DATA_INT;
                    strlen_left->call_func_args_num = 1;
                    strlen_left->total_push_bytes = 4;
                    chain = connect_IR(chain, strlen_left);
                    left_len_temp = strlen_left->result;
                }

                char* right_len_temp = NULL;
                if (ast_binary->right->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)ast_binary->right;
                    if (lit->base.value.type == TOKEN_STRING_LITERAL) {
                        int slen = string_literal_length(lit->base.value.value);
                        IR* len_lit = create_IR(IR_LITERAL);
                        len_lit->result = create_temp();
                        assign_def_id(len_lit);
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%d", slen);
                        len_lit->args1 = strdup(buf);
                        len_lit->args1_type = DATA_INT;
                        len_lit->data_type = DATA_INT;
                        chain = connect_IR(chain, len_lit);
                        right_len_temp = len_lit->result;
                    }
                }
                if (!right_len_temp) {
                    IR* push_right = create_IR(IR_PUSH);
                    push_right->args1 = strdup(right_temp);
                    push_right->args1_type = DATA_STRING;
                    push_right->push_bytes = 4;
                    chain = connect_IR(chain, push_right);
                    IR* strlen_right = create_IR(IR_CALL);
                    strlen_right->args1 = strdup("_str_length");
                    strlen_right->result = create_temp();
                    assign_def_id(strlen_right);
                    strlen_right->data_type = DATA_INT;
                    strlen_right->call_func_args_num = 1;
                    strlen_right->total_push_bytes = 4;
                    chain = connect_IR(chain, strlen_right);
                    right_len_temp = strlen_right->result;
                }
                IR* push_rlen = create_IR(IR_PUSH);
                push_rlen->args1 = strdup(right_len_temp);
                push_rlen->args1_type = DATA_INT;
                push_rlen->push_bytes = 4;
                chain = connect_IR(chain, push_rlen);

                IR* push_rptr = create_IR(IR_PUSH);
                push_rptr->args1 = strdup(right_temp);
                push_rptr->args1_type = DATA_STRING;
                push_rptr->push_bytes = 4;
                chain = connect_IR(chain, push_rptr);

                IR* push_llen = create_IR(IR_PUSH);
                push_llen->args1 = strdup(left_len_temp);
                push_llen->args1_type = DATA_INT;
                push_llen->push_bytes = 4;
                chain = connect_IR(chain, push_llen);

                IR* push_lptr = create_IR(IR_PUSH);
                push_lptr->args1 = strdup(left_temp);
                push_lptr->args1_type = DATA_STRING;
                push_lptr->push_bytes = 4;
                chain = connect_IR(chain, push_lptr);

                IR* call_concat = create_IR(IR_CALL);
                call_concat->args1 = strdup("_str_concat_str");
                call_concat->result = create_temp();
                assign_def_id(call_concat);
                call_concat->data_type = DATA_STRING;
                call_concat->call_func_args_num = 4;
                call_concat->total_push_bytes = 16;
                chain = connect_IR(chain, call_concat);
                return chain;
            }
        }

        if (ast_binary->op.type == TOKEN_LOGICAL_AND)
        {
            int lv = 0, rv = 0;
            bool lc = ast_is_const_int(ast_binary->left, &lv);
            bool rc = ast_is_const_int(ast_binary->right, &rv);

            if (lc && lv == 0) {
                IR* lit = create_IR(IR_LITERAL);
                lit->result = create_temp();
                assign_def_id(lit);
                lit->args1 = strdup("0");
                lit->args1_type = DATA_INT;
                lit->data_type = DATA_BOOL;
                return lit;
            }
            if (lc && lv != 0) {
                IR* chain = generate_IR(ast_binary->right);
                if (!chain) return NULL;
                return chain;
            }
            if (!lc && rc && rv != 0) {
                IR* chain = generate_IR(ast_binary->left);
                if (!chain) return NULL;
                return chain;
            }
            if (!lc && rc && rv == 0) {
                IR* chain = generate_IR(ast_binary->left);
                if (!chain) return NULL;
                IR* lit = create_IR(IR_LITERAL);
                lit->result = create_temp();
                assign_def_id(lit);
                lit->args1 = strdup("0");
                lit->args1_type = DATA_INT;
                lit->data_type = DATA_BOOL;
                return connect_IR(chain, lit);
            }

            IR* left_chain  = generate_IR(ast_binary->left);
            IR* right_chain = generate_IR(ast_binary->right);
            if (!left_chain || !right_chain) return NULL;

            IR* left_last  = left_chain;  while(left_last->next)  left_last  = left_last->next;
            IR* right_last = right_chain; while(right_last->next) right_last = right_last->next;

            char* result      = create_temp();
            char* false_label = create_label();
            char* end_label   = create_label();

            IR* jz_left = create_IR(IR_JMPZ);
            jz_left->args1 = strdup(left_last->result);
            jz_left->args2 = strdup(false_label);

            IR* jz_right = create_IR(IR_JMPZ);
            jz_right->args1 = strdup(right_last->result);
            jz_right->args2 = strdup(false_label);

            IR* set_true = create_IR(IR_LITERAL);
            set_true->args1 = strdup("1");
            set_true->args1_type = DATA_INT;
            set_true->data_type  = DATA_BOOL;
            set_true->result = strdup(result);
            assign_def_id(set_true);

            IR* jmp_end = create_IR(IR_JMP);
            jmp_end->args1 = strdup(end_label);

            IR* label_false = create_IR(IR_LABEL);
            label_false->args1 = strdup(false_label);

            IR* set_false = create_IR(IR_LITERAL);
            set_false->args1 = strdup("0");
            set_false->args1_type = DATA_INT;
            set_false->data_type  = DATA_BOOL;
            set_false->result = strdup(result);
            assign_def_id(set_false);

            IR* label_end = create_IR(IR_LABEL);
            label_end->args1 = strdup(end_label);
                        IR* chain = left_chain;
            chain = connect_IR(chain, jz_left);
            chain = connect_IR(chain, right_chain);
            chain = connect_IR(chain, jz_right);
            chain = connect_IR(chain, set_true);
            chain = connect_IR(chain, jmp_end);
            chain = connect_IR(chain, label_false);
            chain = connect_IR(chain, set_false);
            chain = connect_IR(chain, label_end);
            IR* result_pass = create_IR(IR_LITERAL);
            result_pass->result = strdup(result);
            assign_def_id(result_pass);
            result_pass->args1 = strdup(result);
            result_pass->args1_type = DATA_BOOL;
            result_pass->data_type  = DATA_BOOL;

            chain = connect_IR(chain, result_pass);
            return chain;
        }

    if (ast_binary->op.type == TOKEN_LOGICAL_OR) {
        int lv = 0, rv = 0;
        bool lc = ast_is_const_int(ast_binary->left, &lv);
        bool rc = ast_is_const_int(ast_binary->right, &rv);

        if (lc && lv != 0) {
            IR* lit = create_IR(IR_LITERAL);
            lit->result = create_temp();
            assign_def_id(lit);
            lit->args1 = strdup("1");
            lit->args1_type = DATA_INT;
            lit->data_type = DATA_BOOL;
            return lit;
        }
        if (lc && lv == 0) {
            IR* chain = generate_IR(ast_binary->right);
            if (!chain) return NULL;
            return chain;
        }
        if (!lc && rc && rv != 0) {
            IR* chain = generate_IR(ast_binary->left);
            if (!chain) return NULL;
            IR* lit = create_IR(IR_LITERAL);
            lit->result = create_temp();
            assign_def_id(lit);
            lit->args1 = strdup("1");
            lit->args1_type = DATA_INT;
            lit->data_type = DATA_BOOL;
            return connect_IR(chain, lit);
        }
        if (!lc && rc && rv == 0) {
            IR* chain = generate_IR(ast_binary->left);
            if (!chain) return NULL;
            return chain;
        }

        IR* left_chain  = generate_IR(ast_binary->left);
        IR* right_chain = generate_IR(ast_binary->right);
        if (!left_chain || !right_chain) return NULL;

        IR* left_last  = left_chain;  while(left_last->next)  left_last  = left_last->next;
        IR* right_last = right_chain; while(right_last->next) right_last = right_last->next;

        char* result     = create_temp();
        char* true_label = create_label();
        char* end_label  = create_label();

        IR* jnz_left = create_IR(IR_JMPNZ);
        jnz_left->args1 = strdup(left_last->result);
        jnz_left->args2 = strdup(true_label);

        IR* jnz_right = create_IR(IR_JMPNZ);
        jnz_right->args1 = strdup(right_last->result);
        jnz_right->args2 = strdup(true_label);

        IR* set_false = create_IR(IR_LITERAL);
        set_false->args1 = strdup("0");
        set_false->args1_type = DATA_INT;
        set_false->data_type  = DATA_BOOL;
        set_false->result = strdup(result);
        assign_def_id(set_false);

        IR* jmp_end = create_IR(IR_JMP);
        jmp_end->args1 = strdup(end_label);

        IR* label_true = create_IR(IR_LABEL);
        label_true->args1 = strdup(true_label);

        IR* set_true = create_IR(IR_LITERAL);
        set_true->args1 = strdup("1");
        set_true->args1_type = DATA_INT;
        set_true->data_type  = DATA_BOOL;
        set_true->result = strdup(result);
        assign_def_id(set_true);

        IR* label_end = create_IR(IR_LABEL);
        label_end->args1 = strdup(end_label);

        IR* chain = left_chain;
        chain = connect_IR(chain, jnz_left);
        chain = connect_IR(chain, right_chain);
        chain = connect_IR(chain, jnz_right);
        chain = connect_IR(chain, set_false);
        chain = connect_IR(chain, jmp_end);
        chain = connect_IR(chain, label_true);
        chain = connect_IR(chain, set_true);
        chain = connect_IR(chain, label_end);
        IR* result_pass = create_IR(IR_LITERAL);
        result_pass->result = strdup(result);
        assign_def_id(result_pass);
        result_pass->args1 = strdup(result);
        result_pass->args1_type = DATA_BOOL;
        result_pass->data_type  = DATA_BOOL;

        chain = connect_IR(chain, result_pass);
        return chain;
    }

        IR* Binary_ir = create_IR(IR_BINARY);

        IR* left_ir_chain = generate_IR(ast_binary->left);
        IR* left_last = left_ir_chain;
        while (left_last->next != NULL) {
            left_last = left_last->next;
        }

        IR* right_ir_chain = generate_IR(ast_binary->right);

        IR* right_last = right_ir_chain;
        while (right_last->next != NULL) {
            right_last = right_last->next;
        }

        Binary_ir->op = Type_TO_OP(ast_binary->op.type,ast_binary->is_unsigned);

        if (ast_binary->result_ptr_depth > 0 && ast_binary->elem_size > 1) {
            IR* elem_size_ir = create_IR(IR_LITERAL);
            elem_size_ir->result = create_temp();
            assign_def_id(elem_size_ir);
            char sz_str[16];
            sprintf(sz_str, "%d", ast_binary->elem_size);
            elem_size_ir->args1 = strdup(sz_str);
            elem_size_ir->args1_type = DATA_INT;
            elem_size_ir->data_type = DATA_INT;

            IR* mul_ir = create_IR(IR_BINARY);
            mul_ir->op = IR_MUL;
            mul_ir->args1 = strdup(right_last->result);
            mul_ir->args1_type = right_last->data_type;
            mul_ir->args2 = strdup(elem_size_ir->result);
            mul_ir->args2_type = DATA_INT;
            mul_ir->result = create_temp();
            assign_def_id(mul_ir);
            mul_ir->data_type = DATA_INT;
            right_ir_chain = connect_IR(right_ir_chain, elem_size_ir);
            right_ir_chain = connect_IR(right_ir_chain, mul_ir);
            right_last = mul_ir;
        }
        Binary_ir->result = create_temp();
        assign_def_id(Binary_ir);

        Binary_ir->args1 = strdup(left_last->result);
        Binary_ir->args1_type = left_last->data_type;
        Binary_ir->args2 = strdup(right_last->result);
        Binary_ir->args2_type = right_last->data_type;

        IR* binary_ir_chain = connect_IR(left_ir_chain,right_ir_chain);
        Binary_ir->data_type = ast_binary->resultType;
        IR* bin_result = connect_IR(binary_ir_chain, Binary_ir);
        DataType rt = ast_binary->resultType;
        if (rt == DATA_INT32) {
            DataType lt = left_last->data_type;
            DataType rrt = right_last->data_type;
            if (lt == DATA_UINT8 || lt == DATA_UINT16 || lt == DATA_INT8 || lt == DATA_INT16)
                rt = lt;
            else if (rrt == DATA_UINT8 || rrt == DATA_UINT16 || rrt == DATA_INT8 || rrt == DATA_INT16)
                rt = rrt;
        }
        if (rt == DATA_INT8 || rt == DATA_INT16 ||
            rt == DATA_UINT8 || rt == DATA_UINT16 ||
            rt == DATA_CHAR || rt == DATA_BOOL) {
            IR* trunc = create_IR(IR_TRUNCATE);
            trunc->result = create_temp();
            assign_def_id(trunc);
            trunc->args1 = strdup(Binary_ir->result);
            trunc->args1_type = rt;
            trunc->data_type = rt;
            bin_result = connect_IR(bin_result, trunc);
        }
        return bin_result;

    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)ast_node;

        if (un->op.type == TOKEN_SUB) {
            IR* right_ir = generate_IR(un->right);
            if (right_ir == NULL) return NULL;

            IR* right_last = right_ir;
            while (right_last->next != NULL)
                right_last = right_last->next;

            DataType operand_type = right_last->data_type;
            DataType result_type = DATA_INT;
            if (operand_type == DATA_FLOAT || operand_type == DATA_UFLOAT) {
                result_type = operand_type;
            } else if (operand_type == DATA_INT64 || operand_type == DATA_UINT64 ||
                       operand_type == DATA_INT128 || operand_type == DATA_UINT128) {
                result_type = operand_type;
            }

            IR* zero_ir = create_IR(IR_LITERAL);
            if (result_type == DATA_FLOAT || result_type == DATA_UFLOAT) {
                zero_ir->args1 = strdup("0.0");
                zero_ir->args1_type = result_type;
                zero_ir->data_type = result_type;
            } else {
                zero_ir->args1 = strdup("0");
                zero_ir->args1_type = result_type;
                zero_ir->data_type = result_type;
            }
            zero_ir->result = create_temp();
            assign_def_id(zero_ir);

            IR* sub_ir = create_IR(IR_BINARY);
            sub_ir->op = IR_SUB;
            sub_ir->args1 = strdup(zero_ir->result);
            sub_ir->args1_type = zero_ir->data_type;
            sub_ir->args2 = strdup(right_last->result);
            sub_ir->args2_type = right_last->data_type;
            sub_ir->result = create_temp();
            assign_def_id(sub_ir);
            sub_ir->data_type = result_type;

            IR* chain = connect_IR(zero_ir, right_ir);
            chain = connect_IR(chain, sub_ir);
            if (sub_ir->data_type == DATA_INT8 ||
                sub_ir->data_type == DATA_INT16 ||
                sub_ir->data_type == DATA_UINT8 ||
                sub_ir->data_type == DATA_UINT16 ||
                sub_ir->data_type == DATA_CHAR) {
                IR* trunc = create_IR(IR_TRUNCATE);
                trunc->result = create_temp();
                assign_def_id(trunc);
                trunc->args1 = strdup(sub_ir->result);
                trunc->args1_type = sub_ir->data_type;
                trunc->data_type = sub_ir->data_type;
                chain = connect_IR(chain, trunc);
            }
            return chain;
        }

        else if (un->op.type == TOKEN_OPPOSE) {
            IR* right_ir = generate_IR(un->right);
            if (right_ir == NULL) return NULL;

            IR* right_last = right_ir;
            while (right_last->next != NULL)
                right_last = right_last->next;

            IR* eq_ir = create_IR(IR_BINARY);
            eq_ir->op = IR_JE;
            eq_ir->args1 = strdup(right_last->result);
            eq_ir->args1_type = right_last->data_type;
            eq_ir->args2 = strdup("0");
            eq_ir->args2_type = DATA_INT;
            eq_ir->result = create_temp();
            assign_def_id(eq_ir);
            eq_ir->data_type = DATA_BOOL;

            return connect_IR(right_ir, eq_ir);
        }

        else if (un->op.type == TOKEN_BITWISE_NOT) {
            IR* right_ir = generate_IR(un->right);
            if (right_ir == NULL) return NULL;

            IR* right_last = right_ir;
            while (right_last->next != NULL)
                right_last = right_last->next;

            IR* not_ir = create_IR(IR_BITWISE_NOT);
            not_ir->args1 = strdup(right_last->result);
            not_ir->args1_type = right_last->data_type;
            not_ir->result = create_temp();
            assign_def_id(not_ir);
            not_ir->data_type = un->dataType;

            IR* chain = connect_IR(right_ir, not_ir);
            if (not_ir->data_type == DATA_INT8 ||
                not_ir->data_type == DATA_INT16 ||
                not_ir->data_type == DATA_UINT8 ||
                not_ir->data_type == DATA_UINT16 ||
                not_ir->data_type == DATA_CHAR) {
                IR* trunc2 = create_IR(IR_TRUNCATE);
                trunc2->result = create_temp();
                assign_def_id(trunc2);
                trunc2->args1 = strdup(not_ir->result);
                trunc2->args1_type = not_ir->data_type;
                trunc2->data_type = not_ir->data_type;
                chain = connect_IR(chain, trunc2);
            }
            return chain;
        } else if (un->op.type == TOKEN_INCREMENT || un->op.type == TOKEN_DECREMENT)
        {
            bool is_inc = (un->op.type == TOKEN_INCREMENT);
            ASTNode* operand = un->right;

            char step_str[16];
            if (un->ptr_step > 1) {
                sprintf(step_str, "%d", un->ptr_step);
            } else {
                strcpy(step_str, "1");
            }

            if (operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)operand;
                const char* var_name = lit->internal_name;
                IR* load_ir = create_IR(IR_LITERAL);
                load_ir->result = create_temp();
                assign_def_id(load_ir);
                load_ir->args1 = strdup(var_name);
                load_ir->args1_type = DATA_IDENTIFIER;
                load_ir->data_type = lit->dataType;
                IR* one_ir = create_IR(IR_LITERAL);
                one_ir->result = create_temp();
                assign_def_id(one_ir);
                one_ir->args1 = strdup(step_str);
                one_ir->args1_type = DATA_INT;
                one_ir->data_type = DATA_INT;
                IR* bin_ir = create_IR(IR_BINARY);
                bin_ir->op = is_inc ? IR_ADD : IR_SUB;
                bin_ir->args1 = strdup(load_ir->result);
                bin_ir->args1_type = load_ir->data_type;
                bin_ir->args2 = strdup(one_ir->result);
                bin_ir->args2_type = DATA_INT;
                bin_ir->result = create_temp();
                assign_def_id(bin_ir);
                bin_ir->data_type = lit->dataType;
                IR* store_ir = create_IR(IR_VAR_ASS);
                store_ir->result = strdup(var_name);
                store_ir->args1 = strdup(bin_ir->result);
                store_ir->args1_type = bin_ir->data_type;
                store_ir->data_type = lit->dataType;
                IR* result_ir = create_IR(IR_LITERAL);
                result_ir->result = create_temp();
                assign_def_id(result_ir);
                result_ir->args1 = strdup(bin_ir->result);
                result_ir->args1_type = bin_ir->data_type;
                result_ir->data_type = lit->dataType;
                IR* chain = load_ir;
                chain = connect_IR(chain, one_ir);
                chain = connect_IR(chain, bin_ir);
                chain = connect_IR(chain, store_ir);
                chain = connect_IR(chain, result_ir);
                if (result_ir->data_type == DATA_INT8 ||
                    result_ir->data_type == DATA_INT16 ||
                    result_ir->data_type == DATA_UINT8 ||
                    result_ir->data_type == DATA_UINT16 ||
                    result_ir->data_type == DATA_CHAR) {
                    IR* trunc3 = create_IR(IR_TRUNCATE);
                    trunc3->result = create_temp();
                    assign_def_id(trunc3);
                    trunc3->args1 = strdup(result_ir->result);
                    trunc3->args1_type = result_ir->data_type;
                    trunc3->data_type = result_ir->data_type;
                    chain = connect_IR(chain, trunc3);
                }
                return chain;
            }
            else if (operand->type == AST_ARRAY_ACCESS) {
                AST_ArrayAccess* acc = (AST_ArrayAccess*)operand;
                char* addr_temp = NULL;
                IR* addr_chain = gen_array_access_ir(acc, false, &addr_temp);
                if (!addr_chain || !addr_temp) return NULL;
                IR* load_ir = create_IR(IR_ARRAY_LOAD);
                load_ir->result = create_temp();
                assign_def_id(load_ir);
                load_ir->args1 = strdup(addr_temp);
                load_ir->args1_type = DATA_INT;
                load_ir->data_type = acc->elementType;
                load_ir->elem_size = get_type_size(acc->elementType);
                IR* one_ir = create_IR(IR_LITERAL);
                one_ir->result = create_temp();
                assign_def_id(one_ir);
                one_ir->args1 = strdup(step_str);
                one_ir->args1_type = DATA_INT;
                one_ir->data_type = DATA_INT;
                IR* bin_ir = create_IR(IR_BINARY);
                bin_ir->op = is_inc ? IR_ADD : IR_SUB;
                bin_ir->args1 = strdup(load_ir->result);
                bin_ir->args1_type = load_ir->data_type;
                bin_ir->args2 = strdup(one_ir->result);
                bin_ir->args2_type = DATA_INT;
                bin_ir->result = create_temp();
                assign_def_id(bin_ir);
                bin_ir->data_type = acc->elementType;
                IR* store_ir = create_IR(IR_ARRAY_STORE);
                store_ir->args1 = strdup(addr_temp);
                store_ir->args1_type = DATA_INT;
                store_ir->args2 = strdup(bin_ir->result);
                store_ir->args2_type = bin_ir->data_type;
                store_ir->data_type = acc->elementType;
                store_ir->elem_size = get_type_size(acc->elementType);
                IR* result_ir = create_IR(IR_LITERAL);
                result_ir->result = create_temp();
                assign_def_id(result_ir);
                result_ir->args1 = strdup(bin_ir->result);
                result_ir->args1_type = bin_ir->data_type;
                result_ir->data_type = acc->elementType;
                IR* chain = addr_chain;
                chain = connect_IR(chain, load_ir);
                chain = connect_IR(chain, one_ir);
                chain = connect_IR(chain, bin_ir);
                chain = connect_IR(chain, store_ir);
                chain = connect_IR(chain, result_ir);
                if (result_ir->data_type == DATA_INT8 ||
                    result_ir->data_type == DATA_INT16 ||
                    result_ir->data_type == DATA_UINT8 ||
                    result_ir->data_type == DATA_UINT16 ||
                    result_ir->data_type == DATA_CHAR) {
                    IR* trunc4 = create_IR(IR_TRUNCATE);
                    trunc4->result = create_temp();
                    assign_def_id(trunc4);
                    trunc4->args1 = strdup(result_ir->result);
                    trunc4->args1_type = result_ir->data_type;
                    trunc4->data_type = result_ir->data_type;
                    chain = connect_IR(chain, trunc4);
                }
                return chain;
            } else if (operand->type == AST_UNARY) {
                IR* inner_ir = generate_IR(operand);
                IR* inner_last = inner_ir;
                while (inner_last->next) inner_last = inner_last->next;
                ASTNode* inner_op = ((AST_Unary*)operand)->right;
                char* var_name = NULL;
                if (inner_op->type == AST_LITERAL) {
                    var_name = ((AST_Literal*)inner_op)->internal_name;
                }
                if (var_name == NULL) {
                    IR* one_ir = create_IR(IR_LITERAL);
                    one_ir->result = create_temp();
                    assign_def_id(one_ir);
                    one_ir->args1 = strdup(step_str);
                    one_ir->args1_type = DATA_INT;
                    one_ir->data_type = DATA_INT;
                    IR* bin_ir = create_IR(IR_BINARY);
                    bin_ir->op = is_inc ? IR_ADD : IR_SUB;
                    bin_ir->args1 = strdup(inner_last->result);
                    bin_ir->args1_type = inner_last->data_type;
                    bin_ir->args2 = strdup(one_ir->result);
                    bin_ir->args2_type = DATA_INT;
                    bin_ir->result = create_temp();
                    assign_def_id(bin_ir);
                    bin_ir->data_type = inner_last->data_type;
                    IR* result_ir = create_IR(IR_LITERAL);
                    result_ir->result = create_temp();
                    assign_def_id(result_ir);
                    result_ir->args1 = strdup(bin_ir->result);
                    result_ir->args1_type = bin_ir->data_type;
                    result_ir->data_type = inner_last->data_type;
                    IR* chain = connect_IR(inner_ir, one_ir);
                    chain = connect_IR(chain, bin_ir);
                    chain = connect_IR(chain, result_ir);
                    if (result_ir->data_type == DATA_INT8 ||
                        result_ir->data_type == DATA_INT16 ||
                        result_ir->data_type == DATA_UINT8 ||
                        result_ir->data_type == DATA_UINT16 ||
                        result_ir->data_type == DATA_CHAR) {
                        IR* trunc5 = create_IR(IR_TRUNCATE);
                        trunc5->result = create_temp();
                        assign_def_id(trunc5);
                        trunc5->args1 = strdup(result_ir->result);
                        trunc5->args1_type = result_ir->data_type;
                        trunc5->data_type = result_ir->data_type;
                        chain = connect_IR(chain, trunc5);
                    }
                    return chain;
                }
                IR* one_ir = create_IR(IR_LITERAL);
                one_ir->result = create_temp();
                assign_def_id(one_ir);
                one_ir->args1 = strdup(step_str);
                one_ir->args1_type = DATA_INT;
                one_ir->data_type = DATA_INT;
                IR* bin_ir = create_IR(IR_BINARY);
                bin_ir->op = is_inc ? IR_ADD : IR_SUB;
                bin_ir->args1 = strdup(inner_last->result);
                bin_ir->args1_type = inner_last->data_type;
                bin_ir->args2 = strdup(one_ir->result);
                bin_ir->args2_type = DATA_INT;
                bin_ir->result = create_temp();
                assign_def_id(bin_ir);
                bin_ir->data_type = inner_last->data_type;
                IR* store_ir = create_IR(IR_VAR_ASS);
                store_ir->result = strdup(var_name);
                store_ir->args1 = strdup(bin_ir->result);
                store_ir->args1_type = bin_ir->data_type;
                store_ir->data_type = inner_last->data_type;
                IR* result_ir = create_IR(IR_LITERAL);
                result_ir->result = create_temp();
                assign_def_id(result_ir);
                result_ir->args1 = strdup(bin_ir->result);
                result_ir->args1_type = bin_ir->data_type;
                result_ir->data_type = inner_last->data_type;
                IR* chain = inner_ir;
                chain = connect_IR(chain, one_ir);
                chain = connect_IR(chain, bin_ir);
                chain = connect_IR(chain, store_ir);
                chain = connect_IR(chain, result_ir);
                if (result_ir->data_type == DATA_INT8 ||
                    result_ir->data_type == DATA_INT16 ||
                    result_ir->data_type == DATA_UINT8 ||
                    result_ir->data_type == DATA_UINT16 ||
                    result_ir->data_type == DATA_CHAR) {
                    IR* trunc6 = create_IR(IR_TRUNCATE);
                    trunc6->result = create_temp();
                    assign_def_id(trunc6);
                    trunc6->args1 = strdup(result_ir->result);
                    trunc6->args1_type = result_ir->data_type;
                    trunc6->data_type = result_ir->data_type;
                    chain = connect_IR(chain, trunc6);
                }
                return chain;
            }
            return NULL;
        }

        break;
    }

    case AST_CAST: {
        AST_Cast* cast = (AST_Cast*)ast_node;
        IR* operand_ir = generate_IR(cast->operand);
        if (!operand_ir) return NULL;

        IR* last = operand_ir;
        while (last->next) last = last->next;

        IR* cast_ir = create_IR(IR_CAST);
        cast_ir->result = create_temp();
        assign_def_id(cast_ir);
        cast_ir->args1 = strdup(last->result);
        cast_ir->args1_type = last->data_type;
        cast_ir->data_type = (cast->target_ptr_depth > 0)
                             ? DATA_INT : cast->target_type;
        cast_ir->ptr_depth = cast->target_ptr_depth;
        cast_ir->elem_size = cast->target_ptr_depth > 0
                             ? get_type_size(cast->target_type) : 0;

        DataType src = cast->source_type;
        DataType dst = cast->target_type;
        bool src_f = (src == DATA_FLOAT  || src == DATA_UFLOAT ||
                      src == DATA_FLOAT32|| src == DATA_UFLOAT32 ||
                      src == DATA_FLOAT64|| src == DATA_UFLOAT64);
        bool dst_f = (dst == DATA_FLOAT  || dst == DATA_UFLOAT ||
                      dst == DATA_FLOAT32|| dst == DATA_UFLOAT32 ||
                      dst == DATA_FLOAT64|| dst == DATA_UFLOAT64);

        if (src == DATA_IDENTIFIER && dst == DATA_IDENTIFIER
            && cast->source_ptr_depth == 0 && cast->target_ptr_depth == 0) {
            if (cast->is_checked) {
                cast_ir->cast_kind = CAST_DOWNCAST;
                cast_ir->class_id = cast->target_class_id;
            } else {
                cast_ir->cast_kind = CAST_NOP;
            }
            } else if (cast->target_ptr_depth > 0 && cast->source_ptr_depth == 0) {
                cast_ir->cast_kind = CAST_INT_TO_PTR;
            } else if (cast->target_ptr_depth == 0 && cast->source_ptr_depth > 0
                       && (dst == DATA_INT || dst == DATA_UINT)) {
                cast_ir->cast_kind = CAST_PTR_TO_INT;
            } else if (cast->target_ptr_depth > 0 && cast->source_ptr_depth > 0) {
                cast_ir->cast_kind = CAST_PTR_REINTERP;
            } else if (!src_f && dst_f) {
                cast_ir->cast_kind = CAST_INT_TO_FLOAT;
            } else if (src_f && !dst_f) {
                cast_ir->cast_kind = CAST_FLOAT_TO_INT;
            } else {
                int sb = get_type_size(src);
                int db = get_type_size(dst);
                if (db < sb) cast_ir->cast_kind = CAST_TRUNC;
                else if (db > sb) cast_ir->cast_kind =
                                    ir_is_unsigned_type(src)
                                    ? CAST_ZERO_EXTEND : CAST_SIGN_EXTEND;
                else cast_ir->cast_kind = CAST_NOP;
            }

        return connect_IR(operand_ir, cast_ir);
    }

    case AST_STR_SLICE: {
        AST_StrSlice* slice = (AST_StrSlice*)ast_node;

        const char* str_internal = NULL;
        bool src_is_expr = false;
        if (slice->source->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)slice->source;
            str_internal = lit->internal_name;
            if (!str_internal) str_internal = lit->base.value.value;
        } else if (slice->source->type == AST_VAR || slice->source->type == AST_VAR_ASS) {
            AST_Var* var = (AST_Var*)slice->source;
            str_internal = var->internal_name;
            if (!str_internal) str_internal = var->name.value;
        }
        if (!str_internal) src_is_expr = true;

        IR* start_ir = generate_IR(slice->start);
        IR* start_last = start_ir; while (start_last->next) start_last = start_last->next;
        char* start_temp = start_last->result;

        IR* end_ir = generate_IR(slice->end);
        IR* end_last = end_ir; while (end_last->next) end_last = end_last->next;
        char* end_temp = end_last->result;

        IR* len_ir = create_IR(IR_BINARY);
        len_ir->op = IR_SUB;
        len_ir->args1 = strdup(end_temp);
        len_ir->args1_type = DATA_INT;
        len_ir->args2 = strdup(start_temp);
        len_ir->args2_type = DATA_INT;
        len_ir->result = create_temp();
        assign_def_id(len_ir);
        len_ir->data_type = DATA_INT;

        IR* buf_alloc = create_IR(IR_ALLOC_ARRAY);
        buf_alloc->result = create_temp();
        assign_def_id(buf_alloc);
        buf_alloc->args1 = strdup("1");
        buf_alloc->args1_type = DATA_INT;
        buf_alloc->elem_size = 8;
        buf_alloc->data_type = DATA_INT;
        buf_alloc->stack_bytes = 8;
        char* buf_temp = buf_alloc->result;

        IR* chain_prefix = NULL;
        IR* str_addr_of = create_IR(IR_ADDRESS_OF);
        str_addr_of->result = create_temp();
        assign_def_id(str_addr_of);
        str_addr_of->args1_type = DATA_STR;
        str_addr_of->data_type = DATA_INT;
        str_addr_of->is_function_addr = false;

        if (!src_is_expr) {
            str_addr_of->args1 = strdup(str_internal);
            chain_prefix = str_addr_of;
        } else {
            IR* src_ir = generate_IR(slice->source);
            IR* src_last = src_ir; while (src_last->next) src_last = src_last->next;
            char* src_temp = src_last->result;
            str_addr_of->args1 = strdup(src_temp);
            chain_prefix = src_ir;
            chain_prefix = connect_IR(chain_prefix, str_addr_of);
        }

        IR* str_data_ptr = create_IR(IR_DEREF);
        str_data_ptr->result = create_temp();
        assign_def_id(str_data_ptr);
        str_data_ptr->args1 = strdup(str_addr_of->result);
        str_data_ptr->args1_type = DATA_INT;
        str_data_ptr->data_type = DATA_INT;
        str_data_ptr->ptr_depth = 0;

        IR* str_offset_4 = create_IR(IR_BINARY);
        str_offset_4->op = IR_ADD;
        str_offset_4->args1 = strdup(str_addr_of->result);
        str_offset_4->args1_type = DATA_INT;
        str_offset_4->args2 = strdup("4");
        str_offset_4->args2_type = DATA_INT;
        str_offset_4->result = create_temp();
        assign_def_id(str_offset_4);
        str_offset_4->data_type = DATA_INT;

        IR* str_len_ir = create_IR(IR_DEREF);
        str_len_ir->result = create_temp();
        assign_def_id(str_len_ir);
        str_len_ir->args1 = strdup(str_offset_4->result);
        str_len_ir->args1_type = DATA_INT;
        str_len_ir->data_type = DATA_INT;
        str_len_ir->ptr_depth = 0;

        IR* push_n = create_IR(IR_PUSH);
        push_n->args1 = strdup(len_ir->result);
        push_n->args1_type = DATA_INT;
        push_n->push_bytes = 4;

        IR* push_start = create_IR(IR_PUSH);
        push_start->args1 = strdup(start_temp);
        push_start->args1_type = DATA_INT;
        push_start->push_bytes = 4;

        IR* push_str_len = create_IR(IR_PUSH);
        push_str_len->args1 = strdup(str_len_ir->result);
        push_str_len->args1_type = DATA_INT;
        push_str_len->push_bytes = 4;

        IR* push_str_ptr = create_IR(IR_PUSH);
        push_str_ptr->args1 = strdup(str_data_ptr->result);
        push_str_ptr->args1_type = DATA_INT;
        push_str_ptr->push_bytes = 4;

        IR* push_buf = create_IR(IR_PUSH);
        push_buf->args1 = strdup(buf_temp);
        push_buf->args1_type = DATA_INT;
        push_buf->push_bytes = 4;

        IR* call_ir = create_IR(IR_CALL);
        call_ir->args1 = strdup("_str_substring_str");
        call_ir->result = NULL;
        call_ir->data_type = DATA_VOID;
        call_ir->call_func_args_num = 5;
        call_ir->total_push_bytes = 20;

        IR* chain = chain_prefix;
        chain = connect_IR(chain, str_data_ptr);
        chain = connect_IR(chain, str_offset_4);
        chain = connect_IR(chain, str_len_ir);
        chain = connect_IR(chain, start_ir);
        chain = connect_IR(chain, end_ir);
        chain = connect_IR(chain, len_ir);
        chain = connect_IR(chain, buf_alloc);
        chain = connect_IR(chain, push_n);
        chain = connect_IR(chain, push_start);
        chain = connect_IR(chain, push_str_len);
        chain = connect_IR(chain, push_str_ptr);
        chain = connect_IR(chain, push_buf);
        chain = connect_IR(chain, call_ir);

        IR* slice_result = create_IR(IR_LITERAL);
        slice_result->result = create_temp();
        assign_def_id(slice_result);
        slice_result->args1 = strdup("0");
        slice_result->args1_type = DATA_INT;
        slice_result->data_type = DATA_STR;

        IR* slice_dst_addr = create_IR(IR_ADDRESS_OF);
        slice_dst_addr->result = create_temp();
        assign_def_id(slice_dst_addr);
        slice_dst_addr->args1 = strdup(slice_result->result);
        slice_dst_addr->args1_type = DATA_INT;
        slice_dst_addr->data_type = DATA_INT;
        slice_dst_addr->is_function_addr = false;

        IR* slice_copy = create_IR(IR_COPY_BLOCK);
        assign_def_id(slice_copy);
        slice_copy->args1 = strdup(slice_dst_addr->result);
        slice_copy->args1_type = DATA_INT;
        slice_copy->args2 = strdup(buf_temp);
        slice_copy->args2_type = DATA_INT;
        slice_copy->result = strdup("8");
        slice_copy->data_type = DATA_INT;
        slice_copy->copy_bytes = 8;

        IR* slice_pass = create_IR(IR_LITERAL);
        slice_pass->result = strdup(slice_result->result);
        assign_def_id(slice_pass);
        slice_pass->args1 = strdup(slice_result->result);
        slice_pass->args1_type = DATA_STR;
        slice_pass->data_type = DATA_STR;

        chain = connect_IR(chain, slice_result);
        chain = connect_IR(chain, slice_dst_addr);
        chain = connect_IR(chain, slice_copy);
        chain = connect_IR(chain, slice_pass);
        return chain;
    }

        case AST_STR_FROM_STRING: {
            AST_StrFromString* conv = (AST_StrFromString*)ast_node;
            IR* src_ir = generate_IR(conv->source);
            if (!src_ir) return NULL;
            IR* src_last = src_ir; while (src_last->next) src_last = src_last->next;
            char* string_ptr = src_last->result;

            IR* chain = src_ir;

            IR* dst_decl = create_IR(IR_LITERAL);
            dst_decl->result = create_temp();
            assign_def_id(dst_decl);
            dst_decl->args1 = strdup("0");
            dst_decl->args1_type = DATA_INT;
            dst_decl->data_type = DATA_STR;
            chain = connect_IR(chain, dst_decl);

            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(dst_decl->result);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;
            dst_addr->is_function_addr = false;
            chain = connect_IR(chain, dst_addr);

            IR* store_ptr = create_IR(IR_PTR_STORE);
            store_ptr->args1 = strdup(dst_addr->result);
            store_ptr->args1_type = DATA_INT;
            store_ptr->args2 = strdup(string_ptr);
            store_ptr->args2_type = DATA_STRING;
            store_ptr->data_type = DATA_INT;
            chain = connect_IR(chain, store_ptr);

            bool slen_known = false;
            int slen = 0;
            if (conv->source->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)conv->source;
                if (lit->base.value.type == TOKEN_STRING_LITERAL) {
                    slen = string_literal_length(lit->base.value.value);
                    slen_known = true;
                }
            }
            char* len_temp = NULL;
            if (slen_known) {
                char len_buf[16];
                snprintf(len_buf, sizeof(len_buf), "%d", slen);
                IR* len_lit = create_IR(IR_LITERAL);
                len_lit->result = create_temp();
                assign_def_id(len_lit);
                len_lit->args1 = strdup(len_buf);
                len_lit->args1_type = DATA_INT;
                len_lit->data_type = DATA_INT;
                chain = connect_IR(chain, len_lit);
                len_temp = strdup(len_lit->result);
            } else {
                IR* push_len_arg = create_IR(IR_PUSH);
                push_len_arg->args1 = strdup(string_ptr);
                push_len_arg->args1_type = DATA_STRING;
                push_len_arg->push_bytes = 4;
                chain = connect_IR(chain, push_len_arg);
                IR* strlen_call = create_IR(IR_CALL);
                strlen_call->result = create_temp();
                assign_def_id(strlen_call);
                strlen_call->args1 = strdup("_str_length");
                strlen_call->data_type = DATA_INT;
                strlen_call->call_func_args_num = 1;
                strlen_call->total_push_bytes = 4;
                chain = connect_IR(chain, strlen_call);
                len_temp = strdup(strlen_call->result);
            }

            IR* add4 = create_IR(IR_BINARY);
            add4->op = IR_ADD;
            add4->args1 = strdup(dst_addr->result);
            add4->args1_type = DATA_INT;
            add4->args2 = strdup("4");
            add4->args2_type = DATA_INT;
            add4->result = create_temp();
            assign_def_id(add4);
            add4->data_type = DATA_INT;
            chain = connect_IR(chain, add4);

            IR* store_len = create_IR(IR_PTR_STORE);
            store_len->args1 = strdup(add4->result);
            store_len->args1_type = DATA_INT;
            store_len->args2 = strdup(len_temp);
            store_len->args2_type = DATA_INT;
            store_len->data_type = DATA_INT;
            chain = connect_IR(chain, store_len);
            free(len_temp);

            IR* result_pass = create_IR(IR_LITERAL);
            result_pass->result = strdup(dst_decl->result);
            assign_def_id(result_pass);
            result_pass->args1 = strdup(dst_decl->result);
            result_pass->args1_type = DATA_STR;
            result_pass->data_type = DATA_STR;
            chain = connect_IR(chain, result_pass);

            return chain;
        }

        case AST_DEREF: {
            AST_Deref* deref = (AST_Deref*)ast_node;

            if (deref->deref_array_info != NULL && deref->ptr_depth <= 0) {
                IR* operand_ir = generate_IR(deref->operand);
                IR* operand_last = operand_ir;
                while (operand_last->next) operand_last = operand_last->next;
                IR* ir = create_IR(IR_LITERAL);
                ir->result = create_temp();
                assign_def_id(ir);
                ir->args1 = strdup(operand_last->result);
                ir->args1_type = DATA_INT;
                ir->data_type  = DATA_INT;
                return connect_IR(operand_ir, ir);
            }

            IR* operand_ir = generate_IR(deref->operand);
            IR* operand_last = operand_ir;
            while (operand_last->next) operand_last = operand_last->next;
            IR* ir = create_IR(IR_DEREF);
            ir->result = create_temp();
            assign_def_id(ir);
            ir->args1 = strdup(operand_last->result);
            ir->args1_type = DATA_INT;
            ir->data_type  = deref->resultType;
            ir->ptr_depth  = deref->ptr_depth;
            return connect_IR(operand_ir, ir);
        }

        case AST_ADDRESS_OF: {
            AST_AddressOf* addr = (AST_AddressOf*)ast_node;
            if (addr->const_value_str != NULL) {
                DataType cdt = addr->operand->type == AST_LITERAL
                               ? ((AST_Literal*)addr->operand)->dataType
                               : DATA_INT;

                IR* decl = create_IR(IR_VAR_UNINIT);
                decl->result = create_temp();
                assign_def_id(decl);
                decl->data_type = cdt;
                decl->stack_bytes = 4;

                IR* val = create_IR(IR_LITERAL);
                val->result = create_temp();
                assign_def_id(val);
                val->args1 = strdup(addr->const_value_str);
                val->args1_type = cdt;
                val->data_type = cdt;

                IR* assign = create_IR(IR_VAR);
                assign->result = strdup(decl->result);
                assign->args1 = strdup(val->result);
                assign->args1_type = cdt;
                assign->data_type = cdt;
                assign->stack_bytes = 4;

                IR* addr_ir = create_IR(IR_ADDRESS_OF);
                addr_ir->result = create_temp();
                assign_def_id(addr_ir);
                addr_ir->args1 = strdup(decl->result);
                addr_ir->args1_type = DATA_INT;
                addr_ir->data_type = DATA_INT;
                addr_ir->is_function_addr = false;

                decl->next = val;
                val->next = assign;
                assign->next = addr_ir;
                return decl;
            }
            if (addr->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)addr->operand;
                if (lit->is_class_name && !lit->is_call_function
                    && lit->internal_name != NULL) {
                    IR* ir = create_IR(IR_LITERAL);
                    ir->result = create_temp();
                    assign_def_id(ir);
                    ir->args1 = strdup(lit->internal_name);
                    ir->args1_type = DATA_INT;
                    ir->data_type = DATA_INT;
                    return ir;
                }
                const char* label = lit->internal_name
                                    ? lit->internal_name
                                    : lit->base.value.value;
                IR* ir = create_IR(IR_ADDRESS_OF);
                ir->result = create_temp();
                assign_def_id(ir);
                ir->args1 = strdup(label);
                ir->args1_type = DATA_INT;
                ir->data_type = DATA_INT;
                ir->is_function_addr = (addr->target_func_sig != NULL);
                return ir;
            } else if (addr->operand->type == AST_DEREF) {
                AST_Deref* deref = (AST_Deref*)addr->operand;
                IR* operand_ir = generate_IR(deref->operand);
                return operand_ir;
            } else if (addr->operand->type == AST_ARRAY_ACCESS) {
                AST_ArrayAccess* acc = (AST_ArrayAccess*)addr->operand;
                char* addr_temp = NULL;
                IR* chain = gen_array_access_ir(acc, false, &addr_temp);
                if (!chain || !addr_temp) return NULL;
                return chain;
            } else {
                INTERNAL_ERROR("IR: cannot take address of complex expression");
                return NULL;
            }
        }

        case AST_NULL: {
            IR* ir = create_IR(IR_LITERAL);
            ir->result = create_temp();
            assign_def_id(ir);
            ir->args1 = strdup("0");
            ir->args1_type = DATA_INT;
            ir->data_type = DATA_INT;
            return ir;
        }

        case AST_CONDITIONAL: {
            AST_Conditional* tern = (AST_Conditional*)ast_node;

            IR* cond_chain = generate_IR(tern->condition);
            IR* cond_last  = cond_chain;
            while (cond_last->next) cond_last = cond_last->next;

            char* else_label = create_label();
            char* end_label  = create_label();
            char* result     = create_temp();

            IR* jz = create_IR(IR_JMPZ);
            jz->args1 = strdup(cond_last->result);
            jz->args2 = strdup(else_label);

            IR* then_chain = generate_IR(tern->then_expr);
            IR* then_last  = then_chain;
            while (then_last->next) then_last = then_last->next;

            IR* assign_then = create_IR(IR_LITERAL);
            assign_then->args1      = strdup(then_last->result);
            assign_then->args1_type = then_last->data_type;
            assign_then->result     = strdup(result);
            assign_def_id(assign_then);
            assign_then->data_type  = tern->resultType;

            IR* jmp_end = create_IR(IR_JMP);
            jmp_end->args1 = strdup(end_label);

            IR* label_else = create_IR(IR_LABEL);
            label_else->args1 = strdup(else_label);

            IR* else_chain = generate_IR(tern->else_expr);
            IR* else_last  = else_chain;
            while (else_last->next) else_last = else_last->next;

            IR* assign_else = create_IR(IR_LITERAL);
            assign_else->args1      = strdup(else_last->result);
            assign_else->args1_type = else_last->data_type;
            assign_else->result     = strdup(result);
            assign_def_id(assign_else);
            assign_else->data_type  = tern->resultType;

            IR* label_end = create_IR(IR_LABEL);
            label_end->args1 = strdup(end_label);

            IR* result_pass = create_IR(IR_LITERAL);
            result_pass->result     = strdup(result);
            assign_def_id(result_pass);
            result_pass->args1      = strdup(result);
            result_pass->args1_type = tern->resultType;
            result_pass->data_type  = tern->resultType;

            IR* chain = cond_chain;
            chain = connect_IR(chain, jz);
            chain = connect_IR(chain, then_chain);
            chain = connect_IR(chain, assign_then);
            chain = connect_IR(chain, jmp_end);
            chain = connect_IR(chain, label_else);
            chain = connect_IR(chain, else_chain);
            chain = connect_IR(chain, assign_else);
            chain = connect_IR(chain, label_end);
            chain = connect_IR(chain, result_pass);

            if (tern->resultType == DATA_INT8 ||
                tern->resultType == DATA_INT16 ||
                tern->resultType == DATA_UINT8 ||
                tern->resultType == DATA_UINT16 ||
                tern->resultType == DATA_CHAR) {
                IR* trunc7 = create_IR(IR_TRUNCATE);
                trunc7->result = create_temp();
                assign_def_id(trunc7);
                trunc7->args1 = strdup(result_pass->result);
                trunc7->args1_type = tern->resultType;
                trunc7->data_type = tern->resultType;
                chain = connect_IR(chain, trunc7);
            }

            return chain;
        }

        case AST_ARRAY_DECL:
        {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)ast_node;

            if (arr->is_view) {
                IR* decl = create_IR(IR_VAR_UNINIT);
                decl->result = strdup(arr->internal_name);
                decl->data_type = DATA_INT;
                decl->stack_bytes = 4;
                decl->is_dynamic_array = false;

                IR* init_ir = generate_IR(arr->init_list);
                IR* init_last = init_ir;
                while (init_last->next) init_last = init_last->next;

                IR* assign = create_IR(IR_VAR_ASS);
                assign->result = strdup(arr->internal_name);
                assign->args1 = strdup(init_last->result);
                assign->args1_type = DATA_INT;
                assign->data_type = DATA_INT;

                IR* chain = connect_IR(decl, init_ir);
                chain = connect_IR(chain, assign);
                return chain;
            }

            if (arr->init_list != NULL && arr->init_list->type == AST_NEW_ARRAY) {
                IR* decl = create_IR(IR_VAR_UNINIT);
                decl->result = strdup(arr->internal_name);
                decl->data_type = DATA_INT;
                decl->stack_bytes = 4;
                decl->is_dynamic_array = true;

                IR* init_chain = generate_IR(arr->init_list);
                IR* last = init_chain;
                while (last->next) last = last->next;

                IR* dst = create_IR(IR_ADDRESS_OF);
                assign_def_id(dst);
                dst->result = create_temp();
                dst->args1 = strdup(arr->internal_name);
                dst->args1_type = DATA_INT;
                dst->data_type = DATA_INT;
                dst->is_function_addr = false;

                IR* store = create_IR(IR_PTR_STORE);
                assign_def_id(store);
                store->args1 = strdup(dst->result);
                store->args1_type = DATA_INT;
                store->args2 = strdup(last->result);
                store->args2_type = DATA_INT;
                store->data_type = DATA_INT;

                IR* chain = connect_IR(decl, init_chain);
                chain = connect_IR(chain, dst);
                chain = connect_IR(chain, store);
                return chain;
            }

            bool all_dynamic = (arr->dim_count > 0);
            for (int i = 0; i < arr->dim_count; i++) {
                if (arr->dim_sizes[i] > 0) { all_dynamic = false; break; }
            }

            if (all_dynamic) {
                IR* decl = create_IR(IR_VAR_UNINIT);
                decl->result = strdup(arr->internal_name);
                decl->data_type = DATA_INT;
                decl->stack_bytes = 8;
                decl->is_dynamic_array = true;

                if (arr->init_list != NULL) {
                    IR* init_ir = generate_IR(arr->init_list);
                    IR* last = init_ir;
                    while (last->next) last = last->next;

                    IR* dst_addr = create_IR(IR_ADDRESS_OF);
                    dst_addr->result = create_temp();
                    assign_def_id(dst_addr);
                    dst_addr->args1 = strdup(arr->internal_name);
                    dst_addr->args1_type = DATA_INT;
                    dst_addr->data_type = DATA_INT;
                    dst_addr->is_function_addr = false;

                    char* src_addr = NULL;
                    IR* extra_chain = NULL;

                    if ((arr->init_list->type == AST_LITERAL
                         && ((AST_Literal*)arr->init_list)->is_call_function)
                        || arr->init_list->type == AST_RETURN_EXTRACT) {
                        IR* store_ptr = create_IR(IR_PTR_STORE);
                        store_ptr->args1 = strdup(dst_addr->result);
                        store_ptr->args1_type = DATA_INT;
                        store_ptr->args2 = strdup(last->result);
                        store_ptr->args2_type = DATA_INT;
                        store_ptr->data_type = DATA_INT;

                        IR* sub4 = create_IR(IR_BINARY);
                        sub4->op = IR_SUB;
                        sub4->args1 = strdup(last->result);
                        sub4->args1_type = DATA_INT;
                        sub4->args2 = strdup("4");
                        sub4->args2_type = DATA_INT;
                        sub4->result = create_temp();
                        assign_def_id(sub4);
                        sub4->data_type = DATA_INT;

                        IR* deref_len = create_IR(IR_DEREF);
                        deref_len->result = create_temp();
                        assign_def_id(deref_len);
                        deref_len->args1 = strdup(sub4->result);
                        deref_len->args1_type = DATA_INT;
                        deref_len->data_type = DATA_INT;
                        deref_len->ptr_depth = 0;

                        IR* off4 = create_IR(IR_BINARY);
                        off4->op = IR_ADD;
                        off4->args1 = strdup(dst_addr->result);
                        off4->args1_type = DATA_INT;
                        off4->args2 = strdup("4");
                        off4->args2_type = DATA_INT;
                        off4->result = create_temp();
                        assign_def_id(off4);
                        off4->data_type = DATA_INT;

                        IR* store_len = create_IR(IR_PTR_STORE);
                        store_len->args1 = strdup(off4->result);
                        store_len->args1_type = DATA_INT;
                        store_len->args2 = strdup(deref_len->result);
                        store_len->args2_type = DATA_INT;
                        store_len->data_type = DATA_INT;

                        IR* chain = decl;
                        chain = connect_IR(chain, init_ir);
                        chain = connect_IR(chain, dst_addr);
                        chain = connect_IR(chain, store_ptr);
                        chain = connect_IR(chain, sub4);
                        chain = connect_IR(chain, deref_len);
                        chain = connect_IR(chain, off4);
                        chain = connect_IR(chain, store_len);
                        return chain;
                    }

                    if (arr->init_list->type == AST_NEW_ARRAY) {
                        src_addr = strdup(last->result);
                    } else {
                        const char* src_var_name = NULL;
                        if (arr->init_list->type == AST_LITERAL) {
                            AST_Literal* lit = (AST_Literal*)arr->init_list;
                            src_var_name = lit->internal_name
                                         ? lit->internal_name
                                         : lit->base.value.value;
                        } else if (arr->init_list->type == AST_VAR) {
                            src_var_name = ((AST_Var*)arr->init_list)->internal_name;
                        }

                        if (src_var_name) {
                            IR* src_addr_ir = create_IR(IR_ADDRESS_OF);
                            src_addr_ir->result = create_temp();
                            assign_def_id(src_addr_ir);
                            src_addr_ir->args1 = strdup(src_var_name);
                            src_addr_ir->args1_type = DATA_INT;
                            src_addr_ir->data_type = DATA_INT;
                            src_addr_ir->is_function_addr = false;
                            extra_chain = src_addr_ir;
                            src_addr = strdup(src_addr_ir->result);
                        } else {
                            src_addr = strdup(last->result);
                        }
                    }

                    IR* copy_ir = create_IR(IR_COPY_BLOCK);
                    copy_ir->args1 = strdup(dst_addr->result);
                    copy_ir->args1_type = DATA_INT;
                    copy_ir->args2 = src_addr;
                    copy_ir->args2_type = DATA_INT;
                    copy_ir->result = strdup("8");
                    copy_ir->data_type = DATA_INT;
                    copy_ir->copy_bytes = 8;
                    assign_def_id(copy_ir);

                    IR* chain = decl;
                    chain = connect_IR(chain, init_ir);
                    if (extra_chain) chain = connect_IR(chain, extra_chain);
                    chain = connect_IR(chain, dst_addr);
                    chain = connect_IR(chain, copy_ir);
                    return chain;
                } else {
                    return decl;
                }
            }

            int total_elements = 1;
            for (int i = 0; i < arr->dim_count; i++) {
                if (arr->dim_sizes[i] > 0)
                    total_elements *= arr->dim_sizes[i];
            }

            IR* decl_ir = create_IR(IR_ARRAY_DECL);
            decl_ir->result = strdup(arr->internal_name);
            decl_ir->data_type = arr->elementType;
            decl_ir->array_size = total_elements;
            decl_ir->elem_size = get_type_size(arr->elementType);
            if (total_elements > 0 && arr->total_bytes > 0) {
                long expected_bytes =
                    (long)decl_ir->elem_size * (long)total_elements;
                if ((long)arr->total_bytes != expected_bytes) {
                    INTERNAL_ERROR("IR: array '%s' total_bytes %d != "
                                   "elem_size %d * elements %d (%ld)",
                                   arr->internal_name ? arr->internal_name
                                                      : "(unnamed)",
                                   arr->total_bytes, decl_ir->elem_size,
                                   total_elements, expected_bytes);
                }
            }
            decl_ir->stack_bytes = arr->total_bytes;
            if (current_ir_function == NULL || arr->modifier == 3) {
                decl_ir->is_static_global = true;
            }

            IR* chain = decl_ir;

            if (arr->init_list != NULL
                && arr->init_list->type == AST_LITERAL
                && ((AST_Literal*)arr->init_list)->is_call_function
                && ((AST_Literal*)arr->init_list)->returns_array) {
                IR* init_ir = generate_IR(arr->init_list);
                IR* init_last = init_ir;
                while (init_last->next) init_last = init_last->next;

                IR* dst_addr = create_IR(IR_ADDRESS_OF);
                dst_addr->result = create_temp();
                assign_def_id(dst_addr);
                dst_addr->args1 = strdup(arr->internal_name);
                dst_addr->args1_type = DATA_INT;
                dst_addr->data_type = DATA_INT;
                dst_addr->is_function_addr = false;

                IR* copy = create_IR(IR_COPY_BLOCK);
                copy->args1 = strdup(dst_addr->result);
                copy->args1_type = DATA_INT;
                copy->args2 = strdup(init_last->result);
                copy->args2_type = DATA_INT;
                char sz_buf[16];
                snprintf(sz_buf, sizeof(sz_buf), "%d", arr->total_bytes);
                copy->result = strdup(sz_buf);
                copy->data_type = DATA_INT;
                copy->copy_bytes = arr->total_bytes;
                assign_def_id(copy);

                chain = connect_IR(chain, init_ir);
                chain = connect_IR(chain, dst_addr);
                chain = connect_IR(chain, copy);
                return chain;
            }

            if (arr->init_list != NULL) {
                ASTNode** flat_elems = NULL;
                int flat_count = 0, flat_cap = 0;
                flatten_init_list(arr->init_list, &flat_elems, &flat_count, &flat_cap);

                if (flat_count > total_elements) {
                    INTERNAL_ERROR("IR: too many initializers");
                    flat_count = total_elements;
                }

                if (current_ir_function == NULL) {
                    decl_ir->init_count = total_elements;
                    decl_ir->init_values = malloc(sizeof(int) * total_elements);
                    for (int i = 0; i < total_elements; i++)
                        decl_ir->init_values[i] = 0;
                    for (int k = 0; k < flat_count; k++) {
                        if (flat_elems[k]->type == AST_LITERAL) {
                            AST_Literal* lit = (AST_Literal*)flat_elems[k];
                            if (lit->base.value.type == TOKEN_NUMBER) {
                                decl_ir->init_values[k] = parse_integer(lit->base.value.value);
                            }
                        }
                    }
                } else {
                    bool all_const = (flat_count > 0 && total_elements > 0);
                    for (int k = 0; k < flat_count && all_const; k++) {
                        if (flat_elems[k]->type != AST_LITERAL) {
                            all_const = false;
                            break;
                        }
                        AST_Literal* lit = (AST_Literal*)flat_elems[k];
                        if (lit->is_call_function ||
                            lit->base.value.type != TOKEN_NUMBER) {
                            all_const = false;
                            break;
                            }
                    }
                    if (all_const &&
                        (arr->elementType == DATA_INT  ||
                         arr->elementType == DATA_UINT ||
                         arr->elementType == DATA_INT32 ||
                         arr->elementType == DATA_UINT32)) {
                        decl_ir->init_count = total_elements;
                        decl_ir->init_values = malloc(sizeof(int) * total_elements);
                        for (int i = 0; i < total_elements; i++)
                            decl_ir->init_values[i] = 0;
                        for (int k = 0; k < flat_count; k++) {
                            AST_Literal* lit = (AST_Literal*)flat_elems[k];
                            decl_ir->init_values[k] = parse_integer(lit->base.value.value);
                        }
                    } else {
                        IR* base_lit = create_IR(IR_LITERAL);
                        base_lit->result = create_temp();
                        assign_def_id(base_lit);
                        base_lit->args1 = strdup(arr->internal_name);
                        base_lit->args1_type = DATA_IDENTIFIER;
                        base_lit->data_type = DATA_INT;
                        chain = connect_IR(chain, base_lit);
                        char* base_temp = base_lit->result;

                        int elem_sz = get_type_size(arr->elementType);

                        for (int k = 0; k < flat_count; k++) {
                            char k_str[16];
                            sprintf(k_str, "%d", k);
                            IR* idx_lit = create_IR(IR_LITERAL);
                            idx_lit->result = create_temp();
                            assign_def_id(idx_lit);
                            idx_lit->args1 = strdup(k_str);
                            idx_lit->args1_type = DATA_INT;
                            idx_lit->data_type = DATA_INT;

                            IR* mul = create_IR(IR_BINARY);
                            mul->op = IR_MUL;
                            mul->args1 = strdup(idx_lit->result);
                            mul->args1_type = DATA_INT;
                            char sz_str[16];
                            sprintf(sz_str, "%d", elem_sz);
                            mul->args2 = strdup(sz_str);
                            mul->args2_type = DATA_INT;
                            mul->result = create_temp();
                            assign_def_id(mul);
                            mul->data_type = DATA_INT;

                            IR* add = create_IR(IR_BINARY);
                            add->op = IR_ADD;
                            add->args1 = strdup(base_temp);
                            add->args1_type = DATA_INT;
                            add->args2 = strdup(mul->result);
                            add->args2_type = DATA_INT;
                            add->result = create_temp();
                            assign_def_id(add);
                            add->data_type = DATA_INT;

                            IR* val_ir = generate_IR(flat_elems[k]);
                            IR* val_last = val_ir;
                            while (val_last->next) val_last = val_last->next;

                            IR* store = create_IR(IR_ARRAY_STORE);
                            store->args1 = strdup(add->result);
                            store->args1_type = DATA_INT;
                            store->args2 = strdup(val_last->result);
                            store->args2_type = val_last->data_type;
                            store->data_type = arr->elementType;
                            store->elem_size = elem_sz;

                            chain = connect_IR(chain, idx_lit);
                            chain = connect_IR(chain, mul);
                            chain = connect_IR(chain, add);
                            chain = connect_IR(chain, val_ir);
                            chain = connect_IR(chain, store);
                        }
                    }
                }
                free(flat_elems);
            }

            return chain;
        }

        case AST_ARRAY_ACCESS:
        {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)ast_node;

            DataType arr_type = DATA_ERROR;
            const char* internal_name = NULL;

            if (acc->array->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)acc->array;
                if (!lit->is_call_function) {
                    arr_type = lit->dataType;
                    internal_name = lit->internal_name;
                    if (internal_name == NULL) internal_name = lit->base.value.value;
                }
            } else if (acc->array->type == AST_VAR || acc->array->type == AST_VAR_ASS) {
                AST_Var* var = (AST_Var*)acc->array;
                arr_type = var->dataType;
                internal_name = var->internal_name;
                if (internal_name == NULL) internal_name = var->name.value;
            }

            if (arr_type == DATA_STR && acc->index_count == 1 && internal_name != NULL) {
                IR* str_addr = create_IR(IR_ADDRESS_OF);
                str_addr->result = create_temp();
                assign_def_id(str_addr);
                str_addr->args1 = strdup(internal_name);
                str_addr->args1_type = DATA_STR;
                str_addr->data_type = DATA_INT;
                str_addr->is_function_addr = false;

                IR* data_ptr = create_IR(IR_DEREF);
                data_ptr->result = create_temp();
                assign_def_id(data_ptr);
                data_ptr->args1 = strdup(str_addr->result);
                data_ptr->args1_type = DATA_INT;
                data_ptr->data_type = DATA_INT;
                data_ptr->ptr_depth = 0;

                IR* idx_ir = generate_IR(acc->indices[0]);
                IR* idx_last = idx_ir;
                while (idx_last->next) idx_last = idx_last->next;
                IR* ptr_plus_idx = create_IR(IR_BINARY);
                ptr_plus_idx->op = IR_ADD;
                ptr_plus_idx->args1 = strdup(data_ptr->result);
                ptr_plus_idx->args1_type = DATA_INT;
                ptr_plus_idx->args2 = strdup(idx_last->result);
                ptr_plus_idx->args2_type = DATA_INT;
                ptr_plus_idx->result = create_temp();
                assign_def_id(ptr_plus_idx);
                ptr_plus_idx->data_type = DATA_INT;

                IR* deref_char = create_IR(IR_DEREF);
                deref_char->result = create_temp();
                assign_def_id(deref_char);
                deref_char->args1 = strdup(ptr_plus_idx->result);
                deref_char->args1_type = DATA_INT;
                deref_char->data_type = DATA_CHAR;
                deref_char->ptr_depth = 0;

                IR* chain = str_addr;
                chain = connect_IR(chain, data_ptr);
                chain = connect_IR(chain, idx_ir);
                chain = connect_IR(chain, ptr_plus_idx);
                chain = connect_IR(chain, deref_char);
                return chain;
            }

            if (acc->elementType == DATA_STRING && acc->index_count >= 1
                && internal_name != NULL) {
                return gen_array_access_ir(acc, true, NULL);
            }

            if (arr_type == DATA_STRING && acc->index_count == 1 && internal_name != NULL
                && acc->dim_count == 0 && acc->full_dim_count == 0) {
                IR* ptr_ir = create_IR(IR_LITERAL);
                ptr_ir->result = create_temp();
                assign_def_id(ptr_ir);
                ptr_ir->args1 = strdup(internal_name);
                ptr_ir->args1_type = DATA_STRING;
                ptr_ir->data_type = DATA_INT;
                IR* idx_ir = generate_IR(acc->indices[0]);
                IR* idx_last = idx_ir;
                while (idx_last->next) idx_last = idx_last->next;
                IR* add_ir = create_IR(IR_BINARY);
                add_ir->op = IR_ADD;
                add_ir->args1 = strdup(ptr_ir->result);
                add_ir->args1_type = DATA_INT;
                add_ir->args2 = strdup(idx_last->result);
                add_ir->args2_type = DATA_INT;
                add_ir->result = create_temp();
                assign_def_id(add_ir);
                add_ir->data_type = DATA_INT;
                IR* deref_char = create_IR(IR_DEREF);
                deref_char->result = create_temp();
                assign_def_id(deref_char);
                deref_char->args1 = strdup(add_ir->result);
                deref_char->args1_type = DATA_INT;
                deref_char->data_type = DATA_CHAR;
                deref_char->ptr_depth = 0;
                IR* chain = ptr_ir;
                chain = connect_IR(chain, idx_ir);
                chain = connect_IR(chain, add_ir);
                chain = connect_IR(chain, deref_char);
                return chain;
            }

            return gen_array_access_ir(acc, true, NULL);
        }

    case AST_ARRAY_ASS:
        {
            AST_ArrayAss* ass = (AST_ArrayAss*)ast_node;
            AST_ArrayAccess* acc = (AST_ArrayAccess*)ass->target;

            char* addr_temp = NULL;
            IR* addr_chain = gen_array_access_ir(acc, false, &addr_temp);
            if (!addr_chain || !addr_temp) return NULL;

            IR* val_ir = generate_IR(ass->value);
            IR* val_last = val_ir;
            while (val_last->next) val_last = val_last->next;

            int elem_sz = get_type_size(acc->elementType);

            bool is_scalar_64 = (acc->elementType == DATA_INT64
                                 || acc->elementType == DATA_UINT64
                                 || acc->elementType == DATA_FLOAT64
                                 || acc->elementType == DATA_UFLOAT64);

            if (elem_sz > 4 && !is_scalar_64) {
                IR* copy_ir = create_IR(IR_COPY_BLOCK);
                copy_ir->args1 = strdup(addr_temp);
                copy_ir->args1_type = DATA_INT;
                copy_ir->args2 = strdup(val_last->result);
                copy_ir->args2_type = DATA_INT;
                char sz_buf[16];
                snprintf(sz_buf, sizeof(sz_buf), "%d", elem_sz);
                copy_ir->result = strdup(sz_buf);
                copy_ir->data_type = DATA_INT;
                copy_ir->copy_bytes = elem_sz;
                assign_def_id(copy_ir);
                IR* chain = addr_chain;
                chain = connect_IR(chain, val_ir);
                chain = connect_IR(chain, copy_ir);
                return chain;
            }

            IR* store_ir = create_IR(IR_ARRAY_STORE);
            store_ir->args1 = strdup(addr_temp);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(val_last->result);
            store_ir->args2_type = val_last->data_type;
            store_ir->data_type = acc->elementType;
            store_ir->elem_size = get_type_size(acc->elementType);

            IR* chain = addr_chain;
            chain = connect_IR(chain, val_ir);
            chain = connect_IR(chain, store_ir);
            return chain;
        }

    case AST_NEW_ARRAY:
        {
            AST_NewArray* narr = (AST_NewArray*)ast_node;
            IR* chain = NULL;
            char* total_temp = NULL;

            for (int i = 0; i < narr->dim_count; i++) {
                char* dim_temp = NULL;

                if (narr->dim_sizes[i] >= 0) {
                    IR* lit = create_IR(IR_LITERAL);
                    lit->result = create_temp();
                    assign_def_id(lit);
                    char buf[16];
                    sprintf(buf, "%d", narr->dim_sizes[i]);
                    lit->args1 = strdup(buf);
                    lit->args1_type = DATA_INT;
                    lit->data_type = DATA_INT;
                    chain = chain_append(chain, lit);
                    dim_temp = lit->result;
                } else if (narr->dim_exprs[i] != NULL) {
                    IR* expr_ir = generate_IR(narr->dim_exprs[i]);
                    IR* last = expr_ir;
                    while (last->next) last = last->next;
                    chain = chain_append(chain, expr_ir);
                    dim_temp = last->result;
                } else {
                    INTERNAL_ERROR("IR: new array dim %d has no size", i);
                }

                if (i == 0) {
                    total_temp = dim_temp;
                } else {
                    IR* mul = create_IR(IR_BINARY);
                    mul->op = IR_MUL;
                    mul->args1 = strdup(total_temp);
                    mul->args1_type = DATA_INT;
                    mul->args2 = strdup(dim_temp);
                    mul->args2_type = DATA_INT;
                    mul->result = create_temp();
                    assign_def_id(mul);
                    mul->data_type = DATA_INT;
                    chain = chain_append(chain, mul);
                    total_temp = mul->result;
                }
            }

            int elem_size = (narr->element_total_bytes > 0)
                            ? narr->element_total_bytes
                            : get_type_size(narr->elementType);

            IR* alloc_ir = create_IR(IR_ALLOC_ARRAY);
            alloc_ir->result = create_temp();
            assign_def_id(alloc_ir);
            alloc_ir->args1 = strdup(total_temp ? total_temp : "1");
            alloc_ir->args1_type = DATA_INT;
            alloc_ir->elem_size = elem_size;
            alloc_ir->data_type = DATA_INT;
            chain = chain_append(chain, alloc_ir);

            IR* result_pass = create_IR(IR_LITERAL);
            result_pass->result = create_temp();
            assign_def_id(result_pass);
            result_pass->args1 = strdup(alloc_ir->result);
            result_pass->args1_type = DATA_INT;
            result_pass->data_type = DATA_INT;
            chain = chain_append(chain, result_pass);

            return chain;
        }

        case AST_ARRAY_LENGTH:
        {
            AST_ArrayLength* len = (AST_ArrayLength*)ast_node;

            if (len->array->type == AST_MEMBER_ACCESS) {
                AST_MemberAccess* ma = (AST_MemberAccess*)len->array;
                if (ma->is_field_array && ma->member_ptr_depth > 0) {
                    IR* base_ir = generate_IR(len->array);
                    IR* base_last = base_ir;
                    while (base_last->next) base_last = base_last->next;
                    char* base_temp = base_last->result;

                    IR* sub4 = create_IR(IR_BINARY);
                    sub4->op = IR_SUB;
                    sub4->args1 = strdup(base_temp);
                    sub4->args1_type = DATA_INT;
                    sub4->args2 = strdup("4");
                    sub4->args2_type = DATA_INT;
                    sub4->result = create_temp();
                    assign_def_id(sub4);
                    sub4->data_type = DATA_INT;

                    IR* len_load = create_IR(IR_DEREF);
                    len_load->result = create_temp();
                    assign_def_id(len_load);
                    len_load->args1 = strdup(sub4->result);
                    len_load->args1_type = DATA_INT;
                    len_load->data_type = DATA_INT;
                    len_load->ptr_depth = 0;

                    IR* chain = connect_IR(base_ir, sub4);
                    chain = connect_IR(chain, len_load);
                    return chain;
                }
            }

            if (len->array->type == AST_RETURN_EXTRACT) {
                /* expr.length 其中 expr 是可变返回值的 getReturn(i)：
                   先取出数组指针，再读 [ptr-4] 的长度头 */
                IR* base_ir = generate_IR(len->array);
                IR* base_last = base_ir;
                while (base_last->next) base_last = base_last->next;
                char* base_temp = base_last->result;

                IR* sub4 = create_IR(IR_BINARY);
                sub4->op = IR_SUB;
                sub4->args1 = strdup(base_temp);
                sub4->args1_type = DATA_INT;
                sub4->args2 = strdup("4");
                sub4->args2_type = DATA_INT;
                sub4->result = create_temp();
                assign_def_id(sub4);
                sub4->data_type = DATA_INT;

                IR* len_load = create_IR(IR_DEREF);
                len_load->result = create_temp();
                assign_def_id(len_load);
                len_load->args1 = strdup(sub4->result);
                len_load->args1_type = DATA_INT;
                len_load->data_type = DATA_INT;
                len_load->ptr_depth = 0;

                IR* chain2 = connect_IR(base_ir, sub4);
                chain2 = connect_IR(chain2, len_load);
                return chain2;
            }
            DataType array_type = DATA_ERROR;
            const char* internal_name = NULL;

        if (len->array->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)len->array;
            if (!lit->is_call_function) {
                array_type = lit->dataType;
                internal_name = lit->internal_name;
                if (internal_name == NULL) {
                    internal_name = lit->base.value.value;
                }
            }
        } else if (len->array->type == AST_VAR || len->array->type == AST_VAR_ASS) {
            AST_Var* var = (AST_Var*)len->array;
            array_type = var->dataType;
            internal_name = var->internal_name;
            if (internal_name == NULL) {
                internal_name = var->name.value;
            }
        }

        if (array_type == DATA_STR && internal_name != NULL) {
            IR* addr_ir = create_IR(IR_ADDRESS_OF);
            addr_ir->result = create_temp();
            assign_def_id(addr_ir);
            addr_ir->args1 = strdup(internal_name);
            addr_ir->args1_type = DATA_STR;
            addr_ir->data_type = DATA_INT;
            addr_ir->is_function_addr = false;

            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(addr_ir->result);
            add_ir->args1_type = DATA_INT;
            add_ir->args2 = strdup("4");
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;

            IR* load_ir = create_IR(IR_DEREF);
            load_ir->result = create_temp();
            assign_def_id(load_ir);
            load_ir->args1 = strdup(add_ir->result);
            load_ir->args1_type = DATA_INT;
            load_ir->data_type = DATA_INT;
            load_ir->ptr_depth = 0;

            IR* chain = addr_ir;
            chain = connect_IR(chain, add_ir);
            chain = connect_IR(chain, load_ir);
            return chain;
        }

        if (len->is_dynamic && internal_name != NULL
            && !ir_is_param_name(internal_name)) {
            IR* lit_ir = create_IR(IR_LITERAL);
            lit_ir->result = create_temp();
            assign_def_id(lit_ir);
            lit_ir->args1 = strdup(internal_name);
            lit_ir->args1_type = DATA_INT;
            lit_ir->data_type = DATA_INT;
            IR* sub_ir = create_IR(IR_BINARY);
            sub_ir->op = IR_SUB;
            sub_ir->args1 = strdup(lit_ir->result);
            sub_ir->args1_type = DATA_INT;
            sub_ir->args2 = strdup("4");
            sub_ir->args2_type = DATA_INT;
            sub_ir->result = create_temp();
            assign_def_id(sub_ir);
            sub_ir->data_type = DATA_INT;
            IR* load_ir = create_IR(IR_DEREF);
            load_ir->result = create_temp();
            assign_def_id(load_ir);
            load_ir->args1 = strdup(sub_ir->result);
            load_ir->args1_type = DATA_INT;
            load_ir->data_type = DATA_INT;
            load_ir->ptr_depth = 0;
            IR* chain = lit_ir;
            chain = connect_IR(chain, sub_ir);
            chain = connect_IR(chain, load_ir);
            return chain;
        }

        if (len->array->type == AST_ARRAY_ACCESS) {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)len->array;
            if (acc->dim_count > 0 && acc->dim_sizes != NULL) {
                IR* ir = create_IR(IR_LITERAL);
                ir->result = create_temp();
                assign_def_id(ir);
                char lenStr[16];
                sprintf(lenStr, "%d", acc->dim_sizes[0]);
                ir->args1 = strdup(lenStr);
                ir->args1_type = DATA_INT;
                ir->data_type = DATA_INT;
                return ir;
            } else {
                INTERNAL_ERROR("IR: invalid sub-array in .length");
            }
        }

        if (!len->is_dynamic && len->total_elements > 0) {
            IR* ir = create_IR(IR_LITERAL);
            ir->result = create_temp();
            assign_def_id(ir);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", len->total_elements);
            ir->args1 = strdup(buf);
            ir->args1_type = DATA_INT;
            ir->data_type = DATA_INT;
            return ir;
        }
        AST_Literal* lit = (AST_Literal*)len->array;
        char* arr_internal_name = lit->internal_name;
        if (arr_internal_name == NULL) {
            arr_internal_name = lit->base.value.value;
        }
        IR* len_ir = create_IR(IR_ARRAY_LENGTH);
        len_ir->result = create_temp();
        assign_def_id(len_ir);
        len_ir->args1 = strdup(arr_internal_name);
        len_ir->data_type = DATA_INT;
        return len_ir;
    }

    case AST_PRINT: {
        AST_Print* ast_print = (AST_Print*)ast_node;
        IR* head = NULL;
        IR* tail = NULL;

        ASTNode* param = ast_print->Parameter;
        while (param) {
            IR* param_chain = NULL;
            IR* param_last = NULL;

            if (param->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)param;
                if (!lit->is_call_function
                    && lit->dataType == DATA_STR
                    && lit->internal_name != NULL) {

                    IR* ld = create_IR(IR_LITERAL);
                    ld->result = create_temp();
                    assign_def_id(ld);
                    ld->args1 = strdup(lit->internal_name);
                    ld->args1_type = DATA_STR;
                    ld->data_type = DATA_INT;

                    IR* tostr = create_IR(IR_STR_TO_STRING);
                    tostr->args1 = strdup(ld->result);
                    tostr->args1_type = DATA_STR;
                    tostr->data_type = DATA_STRING;
                    tostr->result = create_temp();
                    assign_def_id(tostr);

                    IR* print_ir = create_IR(IR_PRINT);
                    print_ir->args1 = strdup(tostr->result);
                    print_ir->call_func_args_type = malloc(sizeof(DataType));
                    print_ir->call_func_args_type[0] = DATA_STRING;
                    print_ir->call_func_args_num = 1;

                    ld->next = tostr;
                    tostr->next = print_ir;

                    if (!head) { head = ld; tail = print_ir; }
                    else { tail->next = ld; tail = print_ir; }

                    param = param->next;
                    continue;
                }
            }

            IR* param_ir = generate_IR(param);
            if (!param_ir) {
                param = param->next;
                continue;
            }

            IR* last = param_ir;
            while (last->next) last = last->next;

            DataType param_dt = last->data_type;
            if (param->type == AST_LITERAL) {
                param_dt = ((AST_Literal*)param)->dataType;
            } else if (param->type == AST_VAR || param->type == AST_VAR_ASS) {
                param_dt = ((AST_Var*)param)->dataType;
            } else if (param->type == AST_STR_SLICE ||
                       param->type == AST_STR_FROM_STRING) {
                param_dt = DATA_STR;
            }

            if (param_dt == DATA_STR) {
                IR* tostr = create_IR(IR_STR_TO_STRING);
                tostr->args1 = strdup(last->result);
                tostr->args1_type = DATA_STR;
                tostr->data_type = DATA_STRING;
                tostr->result = create_temp();
                assign_def_id(tostr);
                last->next = tostr;
                last = tostr;
            }

            IR* print_ir = create_IR(IR_PRINT);
            print_ir->args1 = strdup(last->result);
            print_ir->call_func_args_type = malloc(sizeof(DataType));
            print_ir->call_func_args_type[0] = last->data_type;
            print_ir->call_func_args_num = 1;

            if (!head) {
                head = param_ir;
                tail = param_ir;
                while (tail->next) tail = tail->next;
            } else {
                tail->next = param_ir;
                while (tail->next) tail = tail->next;
            }
            tail->next = print_ir;
            tail = print_ir;

            param = param->next;
        }
        return head;
    }

    case AST_INPUT: {
        AST_Input* inp = (AST_Input*)ast_node;
        IR* fmt_chain = generate_IR(inp->format_string);
        IR* fmt_last = fmt_chain;
        while (fmt_last->next) fmt_last = fmt_last->next;

        IR* ir = create_IR(IR_INPUT);
        ir->args1 = strdup(fmt_last->result);
        ir->call_func_args_num = inp->arg_count;
        ir->call_func_args_type = malloc(sizeof(DataType) * inp->arg_count);
        ir->label = malloc(sizeof(char*) * inp->arg_count);
        ir->label_count = inp->arg_count;
        for (int i = 0; i < inp->arg_count; i++) {
            AST_Literal* var = (AST_Literal*)inp->args[i];
            ir->call_func_args_type[i] = var->dataType;
            ir->label[i] = strdup(var->internal_name);
        }
        return connect_IR(fmt_chain, ir);
    }

    case AST_VAR:
        AST_Var* ast_var = (AST_Var*)ast_node;
        if (ast_var->is_const) {
            switch (ast_var->dataType) {
                case DATA_INT: case DATA_UINT:
                case DATA_INT8: case DATA_INT16: case DATA_INT32: case DATA_INT64:
                case DATA_UINT8: case DATA_UINT16: case DATA_UINT32: case DATA_UINT64:
                case DATA_CHAR: case DATA_BOOL:
                case DATA_FLOAT: case DATA_UFLOAT:
                case DATA_FLOAT8: case DATA_FLOAT16: case DATA_FLOAT32:
                case DATA_FLOAT64: case DATA_FLOAT128:
                case DATA_UFLOAT8: case DATA_UFLOAT16: case DATA_UFLOAT32:
                case DATA_UFLOAT64: case DATA_UFLOAT128:
                    return NULL;
                default:
                    break;
            }
        }
        if (ast_var->is_static_local && ast_var->static_guard_name) {
            IR* chain = NULL;
            IR* lg = create_IR(IR_LITERAL);
            lg->result = create_temp();
            assign_def_id(lg);
            lg->args1 = strdup(ast_var->static_guard_name);
            lg->args1_type = DATA_INT;
            lg->data_type = DATA_INT;
            chain = lg;

            char* L_init = create_label();
            char* L_skip = create_label();
            IR* jz = create_IR(IR_JMPZ);
            jz->args1 = strdup(lg->result);
            jz->args2 = strdup(L_init);
            chain = connect_IR(chain, jz);
            IR* jmp_s = create_IR(IR_JMP);
            jmp_s->args1 = strdup(L_skip);
            chain = connect_IR(chain, jmp_s);

            IR* lbl_i = create_IR(IR_LABEL);
            lbl_i->args1 = strdup(L_init);
            chain = connect_IR(chain, lbl_i);
            if (ast_var->Value) {
                IR* vi = generate_IR(ast_var->Value);
                IR* vl = vi;
                while (vl->next) vl = vl->next;
                chain = connect_IR(chain, vi);
                IR* as = create_IR(IR_VAR_ASS);
                as->result = strdup(ast_var->static_storage_name);
                as->is_static_local = true;
                as->args1  = strdup(vl->result);
                as->args1_type = vl->data_type;
                as->data_type  = ast_var->dataType;
                chain = connect_IR(chain, as);
            }

            IR* one = create_IR(IR_LITERAL);
            one->result = create_temp();
            assign_def_id(one);
            one->args1 = strdup("1");
            one->args1_type = DATA_INT;
            one->data_type  = DATA_INT;
            chain = connect_IR(chain, one);
            IR* sg = create_IR(IR_VAR_ASS);
            sg->result = strdup(ast_var->static_guard_name);
            sg->args1  = strdup(one->result);
            sg->args1_type = DATA_INT;
            sg->data_type  = DATA_INT;
            chain = connect_IR(chain, sg);

            IR* lbl_s = create_IR(IR_LABEL);
            lbl_s->args1 = strdup(L_skip);
            chain = connect_IR(chain, lbl_s);
            return chain;
        }
        if (ast_var->is_field_ref && ast_var->Value != NULL) {
            IR* this_ir = create_IR(IR_LOAD_THIS);
            this_ir->result = create_temp();
            assign_def_id(this_ir);
            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(this_ir->result);
            add_ir->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", ast_var->field_offset);
            add_ir->args2 = strdup(off_str);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;
            IR* val_ir = generate_IR(ast_var->Value);
            IR* val_last = val_ir;
            while (val_last->next) val_last = val_last->next;
            IR* store_ir = create_IR(IR_ARRAY_STORE);
            store_ir->args1 = strdup(add_ir->result);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(val_last->result);
            store_ir->args2_type = val_last->data_type;
            store_ir->data_type = ast_var->dataType;
            store_ir->elem_size = get_type_size(ast_var->dataType);
            IR* chain = connect_IR(this_ir, add_ir);
            chain = connect_IR(chain, val_ir);
            chain = connect_IR(chain, store_ir);
            return chain;
        }
        if (ast_var->Value && ast_var->Value->type == AST_INIT_LIST)
        {
            AST_InitList* list = (AST_InitList*)ast_var->Value;
            int elem_size = get_type_size(ast_var->dataType);
            int total_bytes = elem_size * list->count;

            IR* alloc = create_IR(IR_ALLOC_ARRAY);
            alloc->result = create_temp();
            assign_def_id(alloc);
            char cnt_str[16];
            sprintf(cnt_str, "%d", list->count);
            alloc->args1 = strdup(cnt_str);
            alloc->args1_type = DATA_INT;
            alloc->elem_size = elem_size;
            alloc->data_type = DATA_INT;

            IR* chain = alloc;
            IR* last = alloc;

            for (int i = 0; i < list->count; i++) {
                IR* idx_lit = create_IR(IR_LITERAL);
                idx_lit->result = create_temp();
                assign_def_id(idx_lit);
                char istr[16];
                sprintf(istr, "%d", i);
                idx_lit->args1 = strdup(istr);
                idx_lit->args1_type = DATA_INT;
                idx_lit->data_type = DATA_INT;

                IR* mul = create_IR(IR_BINARY);
                mul->op = IR_MUL;
                mul->args1 = strdup(idx_lit->result);
                mul->args1_type = DATA_INT;
                char es[16];
                sprintf(es, "%d", elem_size);
                mul->args2 = strdup(es);
                mul->args2_type = DATA_INT;
                mul->result = create_temp();
                assign_def_id(mul);
                mul->data_type = DATA_INT;

                IR* add = create_IR(IR_BINARY);
                add->op = IR_ADD;
                add->args1 = strdup(alloc->result);
                add->args1_type = DATA_INT;
                add->args2 = strdup(mul->result);
                add->args2_type = DATA_INT;
                add->result = create_temp();
                assign_def_id(add);
                add->data_type = DATA_INT;

                IR* elem_ir = generate_IR(list->elements[i]);
                IR* elem_last = elem_ir;
                while (elem_last->next) elem_last = elem_last->next;

                IR* store = create_IR(IR_ARRAY_STORE);
                store->args1 = strdup(add->result);
                store->args1_type = DATA_INT;
                store->args2 = strdup(elem_last->result);
                store->args2_type = elem_last->data_type;
                store->data_type = ast_var->dataType;
                store->elem_size = elem_size;

                chain = connect_IR(chain, idx_lit);
                chain = connect_IR(chain, mul);
                chain = connect_IR(chain, add);
                chain = connect_IR(chain, elem_ir);
                chain = connect_IR(chain, store);
            }

            IR* var_ir = create_IR(IR_VAR);
            var_ir->result = strdup(ast_var->internal_name);
            var_ir->args1 = strdup(alloc->result);
            var_ir->args1_type = DATA_INT;
            var_ir->data_type = DATA_INT;
            chain = connect_IR(chain, var_ir);

            return chain;
        }

        if (ast_var->dataType == DATA_STR && ast_var->Value != NULL
            && ast_var->Value->type == AST_STR_FROM_STRING) {
            AST_StrFromString* conv = (AST_StrFromString*)ast_var->Value;
            IR* src_ir = generate_IR(conv->source);
            if (!src_ir) return NULL;
            IR* src_last = src_ir; while (src_last->next) src_last = src_last->next;
            char* string_ptr = src_last->result;

            IR* var_ir = create_IR(IR_VAR_UNINIT);
            var_ir->result = strdup(ast_var->internal_name);
            var_ir->data_type = DATA_STR;
            if (ast_var->total_bytes > 4) var_ir->stack_bytes = ast_var->total_bytes;

            IR* addr_ir = create_IR(IR_ADDRESS_OF);
            addr_ir->result = create_temp();
            assign_def_id(addr_ir);
            addr_ir->args1 = strdup(ast_var->internal_name);
            addr_ir->args1_type = DATA_INT;
            addr_ir->is_function_addr = false;
            addr_ir->data_type = DATA_INT;
            IR* store_ptr = create_IR(IR_PTR_STORE);
            store_ptr->args1 = strdup(addr_ir->result);
            store_ptr->args1_type = DATA_INT;
            store_ptr->args2 = strdup(string_ptr);
            store_ptr->args2_type = DATA_STRING;
            store_ptr->data_type = DATA_INT;
            IR* add4 = create_IR(IR_BINARY);
            add4->op = IR_ADD;
            add4->args1 = strdup(addr_ir->result);
            add4->args1_type = DATA_INT;
            add4->args2 = strdup("4");
            add4->args2_type = DATA_INT;
            add4->result = create_temp();
            assign_def_id(add4);
            add4->data_type = DATA_INT;
            IR* store_len = create_IR(IR_PTR_STORE);
            store_len->args1 = strdup(add4->result);
            store_len->args1_type = DATA_INT;
            store_len->data_type = DATA_INT;
            IR* chain = var_ir;
            chain = connect_IR(chain, src_ir);
            chain = connect_IR(chain, addr_ir);
            chain = connect_IR(chain, store_ptr);
            if (conv->source->type == AST_LITERAL
                && ((AST_Literal*)conv->source)->base.value.type == TOKEN_STRING_LITERAL) {
                char len_buf[16];
                snprintf(len_buf, sizeof(len_buf), "%d",
                         string_literal_length(((AST_Literal*)conv->source)->base.value.value));
                store_len->args2 = strdup(len_buf);
                store_len->args2_type = DATA_INT;
            } else {
                IR* push_str = create_IR(IR_PUSH);
                push_str->args1 = strdup(string_ptr);
                push_str->args1_type = DATA_STRING;
                push_str->push_bytes = 4;
                IR* len_call = create_IR(IR_CALL);
                len_call->result = create_temp();
                assign_def_id(len_call);
                len_call->args1 = strdup("_str_length");
                len_call->data_type = DATA_INT;
                len_call->call_func_args_num = 1;
                len_call->total_push_bytes = 4;
                store_len->args2 = strdup(len_call->result);
                store_len->args2_type = DATA_INT;
                chain = connect_IR(chain, push_str);
                chain = connect_IR(chain, len_call);
            }
            chain = connect_IR(chain, add4);
            chain = connect_IR(chain, store_len);
            return chain;
        }

        bool is_scalar_to_wide = false;
        if (ast_var->total_bytes == 8 && ast_var->Value != NULL
            && (ast_var->dataType == DATA_INT64 || ast_var->dataType == DATA_UINT64)) {
            DataType src_dt = DATA_ERROR;
            if (ast_var->Value->type == AST_LITERAL) {
                src_dt = ((AST_Literal*)ast_var->Value)->dataType;
            } else if (ast_var->Value->type == AST_VAR
                       || ast_var->Value->type == AST_VAR_ASS
                       || ast_var->Value->type == AST_UNINIT_VAR) {
                src_dt = ((AST_Var*)ast_var->Value)->dataType;
                       }
            if (src_dt == DATA_INT || src_dt == DATA_INT8 || src_dt == DATA_INT16
                || src_dt == DATA_INT32
                || src_dt == DATA_UINT || src_dt == DATA_UINT8
                || src_dt == DATA_UINT16 || src_dt == DATA_UINT32
                || src_dt == DATA_CHAR || src_dt == DATA_BOOL) {
                is_scalar_to_wide = true;
            }
        }

        if (ast_var->total_bytes > 4 && ast_var->Value != NULL
            && ast_var->dataType != DATA_STRING
            && ast_var->Value->type != AST_NEW_ARRAY
            && !is_scalar_to_wide) {
            bool is_simple_literal = false;
            bool is_negated_literal = false;
            AST_Literal* negated_lit = NULL;
            if (ast_var->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)ast_var->Value;
                if (!lit->is_call_function
                    && lit->base.value.type == TOKEN_NUMBER) {
                    is_simple_literal = true;
                }
            } else if (ast_var->total_bytes == 8
                       && ast_var->Value->type == AST_UNARY) {
                AST_Unary* un = (AST_Unary*)ast_var->Value;
                if (un->op.type == TOKEN_SUB
                    && un->right->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)un->right;
                    if (!lit->is_call_function
                        && lit->base.value.type == TOKEN_NUMBER) {
                        is_negated_literal = true;
                        negated_lit = lit;
                    }
                }
            }
            if (is_simple_literal && ast_var->total_bytes == 8) {
                AST_Literal* lit = (AST_Literal*)ast_var->Value;
                long long val = parse_integer64(lit->base.value.value);

                IR* var_decl = create_IR(IR_VAR_UNINIT);
                var_decl->result = strdup(ast_var->internal_name);
                var_decl->data_type = ast_var->dataType;
                var_decl->stack_bytes = 8;

                IR* dst_addr = create_IR(IR_ADDRESS_OF);
                dst_addr->result = create_temp();
                assign_def_id(dst_addr);
                dst_addr->args1 = strdup(ast_var->internal_name);
                dst_addr->args1_type = DATA_INT;
                dst_addr->data_type = DATA_INT;
                dst_addr->is_function_addr = false;

                IR* val_ir = create_IR(IR_LITERAL);
                val_ir->result = create_temp();
                assign_def_id(val_ir);
                char low_str[32];
                snprintf(low_str, sizeof(low_str), "0x%X",
                         (unsigned int)(val & 0xFFFFFFFFLL));
                val_ir->args1 = strdup(low_str);
                val_ir->args1_type = DATA_INT;
                val_ir->data_type = DATA_INT;
                bool is_unsigned =
                    (ast_var->dataType == DATA_UINT64
                     || ast_var->dataType == DATA_UINT);
                IR* high_ir = create_IR(IR_LITERAL);
                high_ir->result = create_temp();
                assign_def_id(high_ir);
                char high_str[32];
                snprintf(high_str, sizeof(high_str), "0x%X",
                         (unsigned int)((val >> 32) & 0xFFFFFFFFLL));
                high_ir->args1 = strdup(high_str);
                high_ir->args1_type = DATA_INT;
                high_ir->data_type = DATA_INT;

                IR* store_low = create_IR(IR_PTR_STORE);
                store_low->args1 = strdup(dst_addr->result);
                store_low->args1_type = DATA_INT;
                store_low->args2 = strdup(val_ir->result);
                store_low->args2_type = DATA_INT;
                store_low->data_type = DATA_INT;

                IR* addr_high = create_IR(IR_BINARY);
                addr_high->op = IR_ADD;
                addr_high->args1 = strdup(dst_addr->result);
                addr_high->args1_type = DATA_INT;
                addr_high->args2 = strdup("4");
                addr_high->args2_type = DATA_INT;
                addr_high->result = create_temp();
                assign_def_id(addr_high);
                addr_high->data_type = DATA_INT;

                IR* store_high = create_IR(IR_PTR_STORE);
                store_high->args1 = strdup(addr_high->result);
                store_high->args1_type = DATA_INT;
                store_high->args2 = strdup(high_ir->result);
                store_high->args2_type = DATA_INT;
                store_high->data_type = DATA_INT;

                IR* chain = var_decl;
                chain = connect_IR(chain, dst_addr);
                chain = connect_IR(chain, val_ir);
                chain = connect_IR(chain, high_ir);
                chain = connect_IR(chain, store_low);
                chain = connect_IR(chain, addr_high);
                chain = connect_IR(chain, store_high);
                return chain;
            }

            if (is_negated_literal && ast_var->total_bytes == 8) {
                long long val = -parse_integer64(negated_lit->base.value.value);
                IR* var_decl = create_IR(IR_VAR_UNINIT);
                var_decl->result = strdup(ast_var->internal_name);
                var_decl->data_type = ast_var->dataType;
                var_decl->stack_bytes = 8;
                IR* dst_addr = create_IR(IR_ADDRESS_OF);
                dst_addr->result = create_temp();
                assign_def_id(dst_addr);
                dst_addr->args1 = strdup(ast_var->internal_name);
                dst_addr->args1_type = DATA_INT;
                dst_addr->data_type = DATA_INT;
                dst_addr->is_function_addr = false;
                IR* val_ir = create_IR(IR_LITERAL);
                val_ir->result = create_temp();
                assign_def_id(val_ir);
                char low_str[32];
                snprintf(low_str, sizeof(low_str), "0x%X",
                         (unsigned int)(val & 0xFFFFFFFFLL));
                val_ir->args1 = strdup(low_str);
                val_ir->args1_type = DATA_INT;
                val_ir->data_type = DATA_INT;
                IR* high_ir = create_IR(IR_LITERAL);
                high_ir->result = create_temp();
                assign_def_id(high_ir);
                char high_str[32];
                snprintf(high_str, sizeof(high_str), "0x%X",
                         (unsigned int)((val >> 32) & 0xFFFFFFFFLL));
                high_ir->args1 = strdup(high_str);
                high_ir->args1_type = DATA_INT;
                high_ir->data_type = DATA_INT;
                IR* store_low = create_IR(IR_PTR_STORE);
                store_low->args1 = strdup(dst_addr->result);
                store_low->args1_type = DATA_INT;
                store_low->args2 = strdup(val_ir->result);
                store_low->args2_type = DATA_INT;
                store_low->data_type = DATA_INT;
                IR* addr_high = create_IR(IR_BINARY);
                addr_high->op = IR_ADD;
                addr_high->args1 = strdup(dst_addr->result);
                addr_high->args1_type = DATA_INT;
                addr_high->args2 = strdup("4");
                addr_high->args2_type = DATA_INT;
                addr_high->result = create_temp();
                assign_def_id(addr_high);
                addr_high->data_type = DATA_INT;
                IR* store_high = create_IR(IR_PTR_STORE);
                store_high->args1 = strdup(addr_high->result);
                store_high->args1_type = DATA_INT;
                store_high->args2 = strdup(high_ir->result);
                store_high->args2_type = DATA_INT;
                store_high->data_type = DATA_INT;
                IR* chain = var_decl;
                chain = connect_IR(chain, dst_addr);
                chain = connect_IR(chain, val_ir);
                chain = connect_IR(chain, high_ir);
                chain = connect_IR(chain, store_low);
                chain = connect_IR(chain, addr_high);
                chain = connect_IR(chain, store_high);
                return chain;
            }

            IR* var_decl = create_IR(IR_VAR_UNINIT);
            var_decl->result = strdup(ast_var->internal_name);
            var_decl->data_type = ast_var->dataType;
            var_decl->stack_bytes = ast_var->total_bytes;
            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(ast_var->internal_name);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;
            dst_addr->is_function_addr = false;
            IR* value_ir = generate_IR(ast_var->Value);
            if (!value_ir) return NULL;
            IR* value_last = ir_tail(value_ir);
            IR* copy_ir = create_IR(IR_COPY_BLOCK);
            copy_ir->args1 = strdup(dst_addr->result);
            copy_ir->args1_type = DATA_INT;
            copy_ir->args2 = strdup(value_last->result);
            copy_ir->args2_type = DATA_INT;
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", ast_var->total_bytes);
            copy_ir->result = strdup(sz_buf);
            copy_ir->data_type = DATA_INT;
            copy_ir->copy_bytes = ast_var->total_bytes;
            assign_def_id(copy_ir);
            IR* chain = var_decl;
            chain = connect_IR(chain, dst_addr);
            chain = connect_IR(chain, value_ir);
            chain = connect_IR(chain, copy_ir);
            return chain;
        }

        if (ast_var->dataType == DATA_STR && ast_var->Value != NULL
            && !(ast_var->Value->type == AST_STR_FROM_STRING)
            && ast_var->total_bytes >= 8) {
            IR* var_decl = create_IR(IR_VAR_UNINIT);
            var_decl->result = strdup(ast_var->internal_name);
            var_decl->data_type = DATA_STR;
            if (ast_var->total_bytes > 4)
                var_decl->stack_bytes = ast_var->total_bytes;

            IR* value_ir = generate_IR(ast_var->Value);
            if (!value_ir) return NULL;
            IR* value_last = ir_tail(value_ir);

            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(ast_var->internal_name);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;
            dst_addr->is_function_addr = false;

            IR* copy_ir = create_IR(IR_COPY_BLOCK);
            copy_ir->args1 = strdup(dst_addr->result);
            copy_ir->args1_type = DATA_INT;
            copy_ir->args2 = strdup(value_last->result);
            copy_ir->args2_type = DATA_INT;
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", ast_var->total_bytes);
            copy_ir->result = strdup(sz_buf);
            copy_ir->data_type = DATA_INT;
            copy_ir->copy_bytes = ast_var->total_bytes;
            assign_def_id(copy_ir);

            IR* chain = var_decl;
            chain = connect_IR(chain, value_ir);
            chain = connect_IR(chain, dst_addr);
            chain = connect_IR(chain, copy_ir);
            return chain;
        }

        IR* Var_ir = create_IR(IR_VAR);
        bool simple_literal = false;
        if (ast_var->Value != NULL && !ast_var->is_static_local
            && ast_var->dataType != DATA_STR
            && !ast_var->is_field_ref
            && ast_var->total_bytes <= 4
            && (ast_var->dim_count == 0 || ast_var->dim_sizes == NULL)
            && !(ast_var->Value->type == AST_INIT_LIST)
            && !(ast_var->Value->type == AST_NEW_ARRAY))
        {
            if (ast_var->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)ast_var->Value;
                if (!lit->is_call_function) {
                    bool is_closure_lit =
                        (lit->closure_func != NULL
                         && lit->closure_func->is_closure
                         && lit->closure_func->upvalue_count > 0);
                    if (!is_closure_lit) {
                        simple_literal = true;
                    }
                }
            }
        }
        IR* Var_Value_IR = NULL;
        if (simple_literal) {
            AST_Literal* lit = (AST_Literal*)ast_var->Value;
            if (lit->is_field_ref) {
                IR* this_ir = create_IR(IR_LOAD_THIS);
                this_ir->result = create_temp();
                assign_def_id(this_ir);

                IR* load_member = create_IR(IR_LOAD_MEMBER);
                load_member->result = create_temp();
                assign_def_id(load_member);
                load_member->args1 = strdup(this_ir->result);
                load_member->args1_type = DATA_INT;
                char off_str[16];
                sprintf(off_str, "%d", lit->field_offset);
                load_member->args2 = strdup(off_str);
                load_member->data_type = lit->dataType;

                IR* Var_ir_f = create_IR(IR_VAR);
                Var_ir_f->data_type = ast_var->dataType;
                Var_ir_f->result = strdup(ast_var->internal_name);
                Var_ir_f->args1 = strdup(load_member->result);
                Var_ir_f->args1_type = load_member->data_type;

                IR* fchain = connect_IR(this_ir, load_member);
                return connect_IR(fchain, Var_ir_f);
            }

            Var_Value_IR = create_IR(IR_LITERAL);
            Var_Value_IR->result = create_temp();
            assign_def_id(Var_Value_IR);
            if (lit->base.value.value != NULL) {
                Var_Value_IR->args1 = strdup(
                    lit->internal_name ? lit->internal_name
                                       : (lit->base.value.value ? lit->base.value.value : "0")
                );
            } else {
                Var_Value_IR->args1 = strdup("0");
            }
            Var_Value_IR->args1_type = ast_var->dataType;
            Var_Value_IR->data_type = ast_var->dataType;
        } else {
            Var_Value_IR = generate_IR(ast_var->Value);
        }
        Var_ir->data_type = (ast_var->ptr_depth > 0)
                            ? DATA_INT : ast_var->dataType;
       Var_ir->ptr_depth = ast_var->ptr_depth;
       if (ast_var->dim_count > 0) {
           Var_ir->dim_count = ast_var->dim_count;
       }
       Var_ir->result = strdup(ast_var->internal_name);
        if (ast_var->Value && ast_var->Value->type == AST_NEW_ARRAY) {
            Var_ir->is_dynamic_array = true;
        } else if (ast_var->dim_count > 0) {
            bool is_dyn = false;
            if (ast_var->dim_sizes) {
                for (int i = 0; i < ast_var->dim_count; i++) {
                    if (ast_var->dim_sizes[i] <= 0) { is_dyn = true; break; }
                }
            } else {
                is_dyn = true;
            }
            Var_ir->is_dynamic_array = is_dyn;
        }
        if (ast_var->total_bytes > 4) {
            Var_ir->stack_bytes = ast_var->total_bytes;
        }

        IR* Var_Value_IR_temp = Var_Value_IR;
        while (Var_Value_IR_temp->next != NULL)
            Var_Value_IR_temp = Var_Value_IR_temp->next;

        if (Var_Value_IR_temp->type == IR_LITERAL)
        {
            Var_ir->args1 = strdup(Var_Value_IR_temp->args1);
            Var_ir->args1_type = Var_Value_IR_temp->args1_type;
        }
        else if (Var_Value_IR_temp->type == IR_BINARY ||
                 Var_Value_IR_temp->type == IR_CALL ||
                 Var_Value_IR_temp->type == IR_ARRAY_LOAD)
        {
            Var_ir->args1 = strdup(Var_Value_IR_temp->result);
            Var_ir->args1_type = Var_Value_IR_temp->data_type;
        }
        else
        {
            if (Var_Value_IR_temp->result == NULL && Var_Value_IR_temp->args1 != NULL) {
                Var_ir->args1 = strdup(Var_Value_IR_temp->args1);
            } else {
                Var_ir->args1 = Var_Value_IR_temp->result ? strdup(Var_Value_IR_temp->result) : NULL;
            }
            Var_ir->args1_type = Var_Value_IR_temp->data_type;
        }

        if (ast_var->dataType == DATA_STR && Var_Value_IR_temp->data_type == DATA_STRING) {
            IR* conv = create_IR(IR_STR_FROM_STRING);
            conv->result = create_temp();
            assign_def_id(conv);
            conv->args1 = strdup(Var_Value_IR_temp->result);
            conv->args1_type = DATA_STRING;
            conv->data_type = DATA_STR;
            connect_IR(Var_Value_IR, conv);
            Var_Value_IR_temp = conv;
        }
        if (ast_var->dataType == DATA_STRING && Var_Value_IR_temp->data_type == DATA_STR) {
            const char* src_temp = Var_Value_IR_temp->result;
            char* src_internal = NULL;
            if (ast_var->Value != NULL) {
                if (ast_var->Value->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)ast_var->Value;
                    if (!lit->is_call_function) {
                        src_internal = lit->internal_name;
                        if (!src_internal) src_internal = lit->base.value.value;
                    }
                } else if (ast_var->Value->type == AST_VAR || ast_var->Value->type == AST_VAR_ASS) {
                    AST_Var* v = (AST_Var*)ast_var->Value;
                    src_internal = v->internal_name;
                    if (!src_internal) src_internal = v->name.value;
                }
            }
            if (src_internal) {
                IR* s_ptr = create_IR(IR_LITERAL);
                s_ptr->result = create_temp();
                assign_def_id(s_ptr);
                s_ptr->args1 = strdup(src_internal);
                s_ptr->args1_type = DATA_STR;
                s_ptr->data_type = DATA_INT;
                connect_IR(Var_Value_IR, s_ptr);

                IR* s_addr = create_IR(IR_ADDRESS_OF);
                s_addr->result = create_temp();
                assign_def_id(s_addr);
                s_addr->args1 = strdup(src_internal);
                s_addr->args1_type = DATA_STR;
                s_addr->data_type = DATA_INT;
                s_addr->is_function_addr = false;
                connect_IR(Var_Value_IR, s_addr);

                IR* s_off = create_IR(IR_BINARY);
                s_off->op = IR_ADD;
                s_off->args1 = strdup(s_addr->result);
                s_off->args1_type = DATA_INT;
                s_off->args2 = strdup("4");
                s_off->args2_type = DATA_INT;
                s_off->result = create_temp();
                assign_def_id(s_off);
                s_off->data_type = DATA_INT;
                connect_IR(Var_Value_IR, s_off);

                IR* s_len = create_IR(IR_DEREF);
                s_len->result = create_temp();
                assign_def_id(s_len);
                s_len->args1 = strdup(s_off->result);
                s_len->args1_type = DATA_INT;
                s_len->data_type = DATA_INT;
                s_len->ptr_depth = 0;
                connect_IR(Var_Value_IR, s_len);

                IR* p_len = create_IR(IR_PUSH);
                p_len->args1 = strdup(s_len->result);
                p_len->args1_type = DATA_INT;
                p_len->push_bytes = 4;
                connect_IR(Var_Value_IR, p_len);

                IR* p_ptr = create_IR(IR_PUSH);
                p_ptr->args1 = strdup(s_ptr->result);
                p_ptr->args1_type = DATA_INT;
                p_ptr->push_bytes = 4;
                connect_IR(Var_Value_IR, p_ptr);

                IR* call_ir = create_IR(IR_CALL);
                call_ir->args1 = strdup("_str_to_string");
                call_ir->result = create_temp();
                assign_def_id(call_ir);
                call_ir->data_type = DATA_STRING;
                call_ir->call_func_args_num = 2;
                call_ir->total_push_bytes = 8;
                connect_IR(Var_Value_IR, call_ir);

                Var_ir->args1 = strdup(call_ir->result);
                Var_ir->args1_type = DATA_STRING;
                if (current_ir_function == NULL) {
                    Var_ir->is_static_global = true;
                } else if (ast_var->is_static) {
                    Var_ir->is_static_local = true;
                }
                connect_IR(Var_Value_IR, Var_ir);
                return Var_Value_IR;
            }
        }
        if (current_ir_function == NULL) {
            Var_ir->is_static_global = true;
        } else if (ast_var->is_static) {
            Var_ir->is_static_local = true;
        }
        connect_IR(Var_Value_IR, Var_ir);
        return Var_Value_IR;

    case AST_VAR_ASS:
        AST_Var* ast_var_ass = (AST_Var*)ast_node;
        if (ast_var_ass->ptr_depth == -1 && ast_var_ass->deref_target != NULL) {
            IR* ptr_ir = generate_IR(ast_var_ass->deref_target);
            IR* ptr_last = ptr_ir;
            while (ptr_last->next) ptr_last = ptr_last->next;
            char* ptr_addr = ptr_last->result;
            IR* val_ir = generate_IR(ast_var_ass->Value);
            IR* val_last = val_ir;
            while (val_last->next) val_last = val_last->next;
            char* val_temp = val_last->result;
            IR* store = create_IR(IR_PTR_STORE);
            store->args1 = strdup(ptr_addr);
            store->args1_type = DATA_INT;
            store->args2 = strdup(val_temp);
            store->args2_type = val_last->data_type;
            store->data_type = ast_var_ass->dataType;
            IR* chain = connect_IR(ptr_ir, val_ir);
            chain = connect_IR(chain, store);
            return chain;
        }
            
        if (ast_var_ass->is_field_ref && ast_var_ass->copy_bytes > 4) {
            IR* this_ir = create_IR(IR_LOAD_THIS);
            this_ir->result = create_temp();
            assign_def_id(this_ir);

            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(this_ir->result);
            add_ir->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", ast_var_ass->field_offset);
            add_ir->args2 = strdup(off_str);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;

            IR* val_ir = generate_IR(ast_var_ass->Value);
            IR* val_last = ir_tail(val_ir);

            if (ast_var_ass->Value != NULL
                && ast_var_ass->Value->type == AST_NEW_ARRAY) {
                IR* store_ir = create_IR(IR_PTR_STORE);
                store_ir->args1 = strdup(add_ir->result);
                store_ir->args1_type = DATA_INT;
                store_ir->args2 = strdup(val_last->result);
                store_ir->args2_type = DATA_INT;
                store_ir->data_type = DATA_INT;
                assign_def_id(store_ir);

                IR* chain = this_ir;
                chain = connect_IR(chain, add_ir);
                chain = connect_IR(chain, val_ir);
                chain = connect_IR(chain, store_ir);
                return chain;
            }

            IR* copy_ir = create_IR(IR_COPY_BLOCK);
            copy_ir->args1 = strdup(add_ir->result);
            copy_ir->args1_type = DATA_INT;
            copy_ir->args2 = strdup(val_last->result);
            copy_ir->args2_type = DATA_INT;
            char sz_buf[16];
            sprintf(sz_buf, "%d", ast_var_ass->copy_bytes);
            copy_ir->result = strdup(sz_buf);
            copy_ir->data_type = DATA_INT;
            copy_ir->copy_bytes = ast_var_ass->copy_bytes;
            assign_def_id(copy_ir);

            IR* chain = this_ir;
            chain = connect_IR(chain, add_ir);
            chain = connect_IR(chain, val_ir);
            chain = connect_IR(chain, copy_ir);
            return chain;
        }

        if (ast_var_ass->is_field_ref) {
            IR* this_ir = create_IR(IR_LOAD_THIS);
            this_ir->result = create_temp();
            assign_def_id(this_ir);
            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(this_ir->result);
            add_ir->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", ast_var_ass->field_offset);
            add_ir->args2 = strdup(off_str);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;
            IR* val_ir = generate_IR(ast_var_ass->Value);
            IR* val_last = val_ir;
            while (val_last->next) val_last = val_last->next;
            if (ast_var_ass->dataType == DATA_STRING && val_last->data_type == DATA_STR) {
                IR* to_string = create_IR(IR_STR_TO_STRING);
                to_string->args1 = strdup(val_last->result);
                to_string->args1_type = DATA_STR;
                to_string->data_type = DATA_STRING;
                to_string->result = create_temp();
                assign_def_id(to_string);
                connect_IR(val_ir, to_string);
                val_last = to_string;
            }
            IR* store_ir = create_IR(IR_ARRAY_STORE);
            store_ir->args1 = strdup(add_ir->result);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(val_last->result);
            store_ir->args2_type = val_last->data_type;
            store_ir->data_type = ast_var_ass->dataType;
            store_ir->elem_size = get_type_size(ast_var_ass->dataType);
            IR* chain = connect_IR(this_ir, add_ir);
            chain = connect_IR(chain, val_ir);
            chain = connect_IR(chain, store_ir);
            return chain;
        }

        if (ast_var_ass->Value != NULL
            && ast_var_ass->Value->type == AST_NEW_ARRAY) {
            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(ast_var_ass->internal_name);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;

            IR* value_ir = generate_IR(ast_var_ass->Value);
            IR* value_last = ir_tail(value_ir);

            IR* store_ir = create_IR(IR_PTR_STORE);
            store_ir->args1 = strdup(dst_addr->result);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(value_last->result);
            store_ir->args2_type = DATA_INT;
            store_ir->data_type = DATA_INT;
            assign_def_id(store_ir);

            IR* chain = connect_IR(dst_addr, value_ir);
            chain = connect_IR(chain, store_ir);
            return chain;
        }

        bool is_scalar_to_wide_ass = false;
        if (ast_var_ass->copy_bytes == 8 && ast_var_ass->Value != NULL
            && (ast_var_ass->dataType == DATA_INT64
                || ast_var_ass->dataType == DATA_UINT64)) {
            DataType src_dt = DATA_ERROR;
            if (ast_var_ass->Value->type == AST_LITERAL) {
                src_dt = ((AST_Literal*)ast_var_ass->Value)->dataType;
            } else if (ast_var_ass->Value->type == AST_VAR
                       || ast_var_ass->Value->type == AST_VAR_ASS
                       || ast_var_ass->Value->type == AST_UNINIT_VAR) {
                src_dt = ((AST_Var*)ast_var_ass->Value)->dataType;
            }
            if (src_dt == DATA_INT || src_dt == DATA_INT8 || src_dt == DATA_INT16
                || src_dt == DATA_INT32
                || src_dt == DATA_UINT || src_dt == DATA_UINT8
                || src_dt == DATA_UINT16 || src_dt == DATA_UINT32
                || src_dt == DATA_CHAR || src_dt == DATA_BOOL) {
                is_scalar_to_wide_ass = true;
            }
        }

        if (ast_var_ass->copy_bytes > 4 && !is_scalar_to_wide_ass) {
            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(ast_var_ass->internal_name);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;

            IR* value_ir = generate_IR(ast_var_ass->Value);
            IR* value_last = ir_tail(value_ir);

            IR* copy_ir = create_IR(IR_COPY_BLOCK);
            copy_ir->args1 = strdup(dst_addr->result);
            copy_ir->args1_type = DATA_INT;
            copy_ir->args2 = strdup(value_last->result);
            copy_ir->args2_type = DATA_INT;
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", ast_var_ass->copy_bytes);
            copy_ir->result = strdup(sz_buf);
            copy_ir->data_type = DATA_INT;
            copy_ir->copy_bytes = ast_var_ass->copy_bytes;
            assign_def_id(copy_ir);

            IR* chain = connect_IR(dst_addr, value_ir);
            chain = connect_IR(chain, copy_ir);
            return chain;
        }

        if (ast_var_ass->dataType == DATA_STR && ast_var_ass->Value != NULL
            && ast_var_ass->Value->type == AST_STR_FROM_STRING) {
            AST_StrFromString* conv = (AST_StrFromString*)ast_var_ass->Value;

            IR* src_ir = generate_IR(conv->source);
            if (!src_ir) return NULL;
            IR* src_last = src_ir;
            while (src_last->next) src_last = src_last->next;
            char* string_ptr = src_last->result;

            IR* var_ir = create_IR(IR_VAR_UNINIT);
            var_ir->result = strdup(ast_var_ass->internal_name);
            var_ir->data_type = DATA_STR;
            if (ast_var_ass->total_bytes > 4) var_ir->stack_bytes = ast_var_ass->total_bytes;

            IR* addr_ir = create_IR(IR_ADDRESS_OF);
            addr_ir->result = create_temp();
            assign_def_id(addr_ir);
            addr_ir->args1 = strdup(ast_var_ass->internal_name);
            addr_ir->args1_type = DATA_INT;
            addr_ir->is_function_addr = false;
            addr_ir->data_type = DATA_INT;

            IR* store_ptr = create_IR(IR_PTR_STORE);
            store_ptr->args1 = strdup(addr_ir->result);
            store_ptr->args1_type = DATA_INT;
            store_ptr->args2 = strdup(string_ptr);
            store_ptr->args2_type = DATA_STRING;
            store_ptr->data_type = DATA_INT;

            IR* add4 = create_IR(IR_BINARY);
            add4->op = IR_ADD;
            add4->args1 = strdup(addr_ir->result);
            add4->args1_type = DATA_INT;
            add4->args2 = strdup("4");
            add4->args2_type = DATA_INT;
            add4->result = create_temp();
            assign_def_id(add4);
            add4->data_type = DATA_INT;

            IR* store_len = create_IR(IR_PTR_STORE);
            store_len->args1 = strdup(add4->result);
            store_len->args1_type = DATA_INT;
            store_len->data_type = DATA_INT;

            if (conv->source->type == AST_LITERAL
                && ((AST_Literal*)conv->source)->base.value.type == TOKEN_STRING_LITERAL) {
                char len_buf[16];
                snprintf(len_buf, sizeof(len_buf), "%d",
                         string_literal_length(((AST_Literal*)conv->source)->base.value.value));
                store_len->args2 = strdup(len_buf);
                store_len->args2_type = DATA_INT;

                IR* chain = src_ir;
                chain = connect_IR(chain, var_ir);
                chain = connect_IR(chain, addr_ir);
                chain = connect_IR(chain, store_ptr);
                chain = connect_IR(chain, add4);
                chain = connect_IR(chain, store_len);
                return chain;
            } else {
                IR* push_str = create_IR(IR_PUSH);
                push_str->args1 = strdup(string_ptr);
                push_str->args1_type = DATA_STRING;
                push_str->push_bytes = 4;
                IR* len_call = create_IR(IR_CALL);
                len_call->result = create_temp();
                assign_def_id(len_call);
                len_call->args1 = strdup("_str_length");
                len_call->data_type = DATA_INT;
                len_call->call_func_args_num = 1;
                len_call->total_push_bytes = 4;
                store_len->args2 = strdup(len_call->result);
                store_len->args2_type = DATA_INT;

                IR* chain = src_ir;
                chain = connect_IR(chain, var_ir);
                chain = connect_IR(chain, push_str);
                chain = connect_IR(chain, len_call);
                chain = connect_IR(chain, addr_ir);
                chain = connect_IR(chain, store_ptr);
                chain = connect_IR(chain, add4);
                chain = connect_IR(chain, store_len);
                return chain;
            }
        }

        if (ast_var_ass->dataType == DATA_STR && ast_var_ass->Value != NULL
            && !(ast_var_ass->Value->type == AST_STR_FROM_STRING)
            && ast_var_ass->copy_bytes >= 8)
        {
            IR* value_ir = generate_IR(ast_var_ass->Value);
            if (!value_ir) return NULL;
            IR* value_last = ir_tail(value_ir);

            IR* dst_addr = create_IR(IR_ADDRESS_OF);
            dst_addr->result = create_temp();
            assign_def_id(dst_addr);
            dst_addr->args1 = strdup(ast_var_ass->internal_name);
            dst_addr->args1_type = DATA_INT;
            dst_addr->data_type = DATA_INT;
            dst_addr->is_function_addr = false;

            IR* copy_ir = create_IR(IR_COPY_BLOCK);
            copy_ir->args1 = strdup(dst_addr->result);
            copy_ir->args1_type = DATA_INT;
            copy_ir->args2 = strdup(value_last->result);
            copy_ir->args2_type = DATA_INT;
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", ast_var_ass->copy_bytes);
            copy_ir->result = strdup(sz_buf);
            copy_ir->data_type = DATA_INT;
            copy_ir->copy_bytes = ast_var_ass->copy_bytes;
            assign_def_id(copy_ir);

            IR* chain = connect_IR(value_ir, dst_addr);
            chain = connect_IR(chain, copy_ir);
            return chain;
        }

        IR* Var_ir_ass = create_IR(IR_VAR_ASS);
        IR* Var_ass_Value_IR = generate_IR(ast_var_ass->Value);
        Var_ir_ass->data_type = (ast_var_ass->ptr_depth > 0)
                                ? DATA_INT : ast_var_ass->dataType;
        Var_ir_ass->ptr_depth = (ast_var_ass->ptr_depth > 0)
                                ? ast_var_ass->ptr_depth : 0;
        Var_ir_ass->result = strdup(ast_var_ass->internal_name);
        if (ast_var_ass->dim_count > 0) {
            Var_ir_ass->is_dynamic_array = true;
        }
        IR* Var_ass_Value_IR_temp = Var_ass_Value_IR;
        while (Var_ass_Value_IR_temp->next != NULL)
            Var_ass_Value_IR_temp = Var_ass_Value_IR_temp->next;
                if (Var_ass_Value_IR_temp->type == IR_LITERAL)
        {
            Var_ir_ass->args1 = strdup(Var_ass_Value_IR_temp->args1);
            Var_ir_ass->args1_type = Var_ass_Value_IR_temp->args1_type;
        }
        else if (Var_ass_Value_IR_temp->type == IR_BINARY ||
                 Var_ass_Value_IR_temp->type == IR_CALL ||
                 Var_ass_Value_IR_temp->type == IR_ARRAY_LOAD)
        {
            Var_ir_ass->args1 = strdup(Var_ass_Value_IR_temp->result);
            Var_ir_ass->args1_type = Var_ass_Value_IR_temp->data_type;
        }
        else
        {
            Var_ir_ass->args1 = Var_ass_Value_IR_temp->result ? strdup(Var_ass_Value_IR_temp->result) : NULL;
            Var_ir_ass->args1_type = Var_ass_Value_IR_temp->data_type;
        }
        if (ast_var_ass->dataType == DATA_STR && Var_ass_Value_IR_temp->data_type == DATA_STRING) {
            IR* conv = create_IR(IR_STR_FROM_STRING);
            conv->result = create_temp();
            assign_def_id(conv);
            conv->args1 = strdup(Var_ass_Value_IR_temp->result);
            conv->args1_type = DATA_STRING;
            conv->data_type = DATA_STR;
            connect_IR(Var_ass_Value_IR_temp, conv);
            Var_ass_Value_IR_temp = conv;
        }
        if (ast_var_ass->dataType == DATA_STRING && Var_ass_Value_IR_temp->data_type == DATA_STR) {
            IR* to_string = create_IR(IR_STR_TO_STRING);
            to_string->args1 = strdup(Var_ass_Value_IR_temp->result);
            to_string->args1_type = DATA_STR;
            to_string->data_type  = DATA_STRING;
            to_string->result     = create_temp();
            assign_def_id(to_string);
            connect_IR(Var_ass_Value_IR, to_string);
            Var_ass_Value_IR_temp = to_string;
            if (Var_ir_ass->args1 != NULL) free(Var_ir_ass->args1);
            Var_ir_ass->args1 = strdup(to_string->result);
            Var_ir_ass->args1_type = DATA_STRING;
        }
        connect_IR(Var_ass_Value_IR, Var_ir_ass);
        return Var_ass_Value_IR;

    case AST_UNINIT_VAR:
        AST_Var* ast_var_uninit = (AST_Var*)ast_node;
        if (ast_var_uninit->is_const) {
            return NULL;
        }
        IR* ir_var_uninit = create_IR(IR_VAR_UNINIT);
        ir_var_uninit->result = strdup(ast_var_uninit->internal_name);
        if (ast_var_uninit->dim_count > 0 || ast_var_uninit->ptr_depth > 0) {
            ir_var_uninit->data_type = DATA_INT;
        } else {
            ir_var_uninit->data_type = ast_var_uninit->dataType;
        }
        if (ast_var_uninit->dim_count > 0) {
            bool uninit_is_dyn = false;
            if (ast_var_uninit->dim_sizes) {
                for (int udi = 0; udi < ast_var_uninit->dim_count; udi++) {
                    if (ast_var_uninit->dim_sizes[udi] <= 0) { uninit_is_dyn = true; break; }
                }
           } else {
               uninit_is_dyn = true;
           }
           ir_var_uninit->is_dynamic_array = uninit_is_dyn;
           ir_var_uninit->dim_count = ast_var_uninit->dim_count;
       }
        if (ast_var_uninit->total_bytes > 4) {
            ir_var_uninit->stack_bytes = ast_var_uninit->total_bytes;
        }
        if (current_ir_function == NULL) {
            ir_var_uninit->is_static_global = true;
        } else if (ast_var_uninit->is_static) {
            ir_var_uninit->is_static_local = true;
        }
        return ir_var_uninit;

    case AST_IF:
        AST_If* ast_if = (AST_If*)ast_node;
        IR* ir_chain = NULL;

        char* endif_label = create_label();
        int elif_count = ast_if->elif_Count;
        char** elif_labels = malloc(elif_count * sizeof(char*));
        for (int i = 0; i < elif_count; i++) {
            elif_labels[i] = create_label();
        }
        char* else_label = ast_if->has_else ? create_label() : endif_label;

        if (ast_if->Condition == NULL) {
            INTERNAL_ERROR("IR: if condition is NULL");
        }
        IR* cond_ir = generate_IR(ast_if->Condition);
        ir_chain = cond_ir;
        IR* last = cond_ir;
        while (last->next) last = last->next;
        char* cond_result = last->result;

        IR* jmpz = create_IR(IR_JMPZ);
        jmpz->args1 = strdup(cond_result);
        jmpz->args2 = strdup(elif_count > 0 ? elif_labels[0] : else_label);
        connect_IR(ir_chain, jmpz);

        if (ast_if->code != NULL) {
            IR* then_ir = start_generate_IR(ast_if->code);
            connect_IR(ir_chain, then_ir);
        }
        IR* jmp_to_endif = create_IR(IR_JMP);
        jmp_to_endif->args1 = strdup(endif_label);
        connect_IR(ir_chain, jmp_to_endif);

        for (int i = 0; i < elif_count; i++) {
            IR* label = create_IR(IR_LABEL);
            label->args1 = strdup(elif_labels[i]);
            connect_IR(ir_chain, label);

            if (ast_if->elif_condition[i] == NULL) {
                INTERNAL_ERROR("IR: elif condition %d is NULL", i);
            }
            IR* elif_cond_ir = generate_IR(ast_if->elif_condition[i]);
            connect_IR(ir_chain, elif_cond_ir);
            last = elif_cond_ir;
            while (last->next) last = last->next;
            char* elif_cond_result = last->result;

            char* next_label = (i+1 < elif_count) ? elif_labels[i+1] : else_label;
            IR* jmpz_elif = create_IR(IR_JMPZ);
            jmpz_elif->args1 = strdup(elif_cond_result);
            jmpz_elif->args2 = strdup(next_label);
            connect_IR(ir_chain, jmpz_elif);

            if (ast_if->Elif[i] != NULL) {
                IR* elif_code_ir = start_generate_IR(ast_if->Elif[i]);
                connect_IR(ir_chain, elif_code_ir);
            }
            IR* jmp_elif_to_endif = create_IR(IR_JMP);
            jmp_elif_to_endif->args1 = strdup(endif_label);
            connect_IR(ir_chain, jmp_elif_to_endif);
        }

        if (ast_if->has_else) {
            IR* else_label_ir = create_IR(IR_LABEL);
            else_label_ir->args1 = strdup(else_label);
            connect_IR(ir_chain, else_label_ir);

            if (ast_if->Else != NULL) {
                IR* else_ir = start_generate_IR(ast_if->Else);
                connect_IR(ir_chain, else_ir);
            }
        }

        IR* endif_label_ir = create_IR(IR_LABEL);
        endif_label_ir->args1 = strdup(endif_label);
        connect_IR(ir_chain, endif_label_ir);

        free(elif_labels);
        return ir_chain;

        case AST_SWITCH:
            {
                AST_Switch* sw = (AST_Switch*)ast_node;

                IR* cond_chain = generate_IR(sw->condition);
                IR* cond_last = cond_chain;
                while (cond_last->next) cond_last = cond_last->next;
                char* cond_temp = cond_last->result;

                IR* chain = cond_chain;

                char* end_label = create_label();
                push_context(end_label, NULL);

                char** case_labels = malloc(sizeof(char*) * sw->case_count);
                for (int i = 0; i < sw->case_count; i++) {
                    case_labels[i] = create_label();
                    AST_Case* cs = sw->cases[i];
                    if (cs->is_default) continue;

                    IR* val_ir = generate_IR(cs->value);
                    IR* val_last = val_ir;
                    while (val_last->next) val_last = val_last->next;
                    char* val_temp = val_last->result;
                    chain = connect_IR(chain, val_ir);

                    IR* cmp = create_IR(IR_BINARY);
                    cmp->op = IR_JE;
                    cmp->args1 = strdup(cond_temp);
                    cmp->args1_type = cond_last->data_type;
                    cmp->args2 = strdup(val_temp);
                    cmp->args2_type = val_last->data_type;
                    cmp->result = create_temp();
                    assign_def_id(cmp);
                    cmp->data_type = DATA_BOOL;
                    chain = connect_IR(chain, cmp);

                    IR* jmp = create_IR(IR_JMPNZ);
                    jmp->args1 = strdup(cmp->result);
                    jmp->args2 = strdup(case_labels[i]);
                    chain = connect_IR(chain, jmp);
                }

                char* default_label = NULL;
                if (sw->has_default) {
                    default_label = create_label();
                }
                IR* jmp_no_match = create_IR(IR_JMP);
                jmp_no_match->args1 = strdup(sw->has_default ? default_label : end_label);
                chain = connect_IR(chain, jmp_no_match);

                for (int i = 0; i < sw->case_count; i++) {
                    AST_Case* cs = sw->cases[i];
                    if (cs->is_default) continue;

                    IR* lbl = create_IR(IR_LABEL);
                    lbl->args1 = strdup(case_labels[i]);
                    chain = connect_IR(chain, lbl);

                    if (cs->body) {
                        IR* body_ir = start_generate_IR(cs->body);
                        chain = connect_IR(chain, body_ir);
                    }
                }
                free(case_labels);

                if (sw->has_default) {
                    IR* def_lbl = create_IR(IR_LABEL);
                    def_lbl->args1 = strdup(default_label);
                    chain = connect_IR(chain, def_lbl);

                    if (sw->default_body) {
                        IR* def_ir = start_generate_IR(sw->default_body);
                        chain = connect_IR(chain, def_ir);
                    }
                }

                IR* end_lbl = create_IR(IR_LABEL);
                end_lbl->args1 = strdup(end_label);
                chain = connect_IR(chain, end_lbl);

                pop_context();
                return chain;
            }

    case AST_WHILE:
        AST_While* ast_while = (AST_While*)ast_node;
        if (ast_while->has_do) {
            IR* chain = NULL;

            char* end_label = create_label();
            char* start_label = create_label();
            char* cont_label = create_label();
            push_context(end_label, cont_label);
            IR* label_start = create_IR(IR_LABEL);
            label_start->args1 = strdup(start_label);
            chain = label_start;

            if (ast_while->code != NULL) {
                IR* body_ir = start_generate_IR(ast_while->code);
                chain = connect_IR(chain, body_ir);
            }

            IR* label_cont = create_IR(IR_LABEL);
            label_cont->args1 = strdup(cont_label);
            chain = connect_IR(chain, label_cont);

            IR* cond_ir = generate_IR(ast_while->Condition);
            IR* cond_last = cond_ir;
            while (cond_last->next) cond_last = cond_last->next;
            chain = connect_IR(chain, cond_ir);

            IR* jmpnz = create_IR(IR_JMPNZ);
            jmpnz->args1 = strdup(cond_last->result);
            jmpnz->args2 = strdup(start_label);
            chain = connect_IR(chain, jmpnz);

            IR* label_end = create_IR(IR_LABEL);
            label_end->args1 = strdup(end_label);
            chain = connect_IR(chain, label_end);

            pop_context();
            return chain;
        }
        IR* while_condition_ir = generate_IR(ast_while->Condition);
        IR* while_last_ir = while_condition_ir;
        while (while_last_ir->next != NULL)
        {
            while_last_ir = while_last_ir->next;
        }

        IR* while_jmpz = create_IR(IR_JMPZ);
        while_jmpz->args1 = strdup(while_last_ir->result);
        char* while_end_label = create_label();
        while_jmpz->args2 = strdup(while_end_label);
        char* while_start_label = create_label();
        push_context(while_end_label, while_start_label);
        IR* while_start_ir = create_IR(IR_LABEL);
        while_start_ir->args1 = strdup(while_start_label);
        connect_IR(while_start_ir,while_condition_ir);
        connect_IR(while_condition_ir, while_jmpz);

        if (ast_while->code != NULL)
        {
            IR* while_code_ir = start_generate_IR(ast_while->code);
            connect_IR(while_condition_ir, while_code_ir);
        }

        IR* while_code_ir = create_IR(IR_JMP);
        while_code_ir->args1 = strdup(while_start_label);
        connect_IR(while_condition_ir, while_code_ir);

        IR* while_end_ir = create_IR(IR_LABEL);
        while_end_ir->args1 = strdup(while_end_label);
        connect_IR(while_condition_ir, while_end_ir);
        pop_context();
        return while_start_ir;

    case AST_FOR: {
        AST_For* for_node = (AST_For*)ast_node;
        IR* chain = NULL;

        if (for_node->init) {
            chain = start_generate_IR(for_node->init);
        }

        char* start_label = create_label();
        char* end_label   = create_label();
        char* cont_label  = create_label();
        push_context(end_label, cont_label);

        IR* label_start = create_IR(IR_LABEL);
        label_start->args1 = strdup(start_label);
        chain = connect_IR(chain, label_start);

        if (for_node->condition) {
            IR* cond_ir = generate_IR(for_node->condition);
            IR* cond_last = cond_ir;
            while (cond_last->next) cond_last = cond_last->next;
            chain = connect_IR(chain, cond_ir);

            IR* jmpz = create_IR(IR_JMPZ);
            jmpz->args1 = strdup(cond_last->result);
            jmpz->args2 = strdup(end_label);
            chain = connect_IR(chain, jmpz);
        }

        if (for_node->code) {
            IR* body_ir = start_generate_IR(for_node->code);
            chain = connect_IR(chain, body_ir);
        }

        IR* cont_lbl_ir = create_IR(IR_LABEL);
        cont_lbl_ir->args1 = strdup(cont_label);
        chain = connect_IR(chain, cont_lbl_ir);

        if (for_node->increment) {
            IR* incr_ir = start_generate_IR(for_node->increment);
            chain = connect_IR(chain, incr_ir);
        }

        IR* jmp_back = create_IR(IR_JMP);
        jmp_back->args1 = strdup(start_label);
        chain = connect_IR(chain, jmp_back);

        IR* label_end = create_IR(IR_LABEL);
        label_end->args1 = strdup(end_label);
        chain = connect_IR(chain, label_end);

        pop_context();
        return chain;
    }

    case AST_FOREACH: {
        AST_ForEach* foreach_node = (AST_ForEach*)ast_node;

        if (foreach_node->is_container) {
            IR* chain = NULL;

            IR* iter_decl = create_IR(IR_VAR_UNINIT);
            iter_decl->result = strdup(foreach_node->internal_iterator);
            iter_decl->data_type = foreach_node->elemType;
            iter_decl->stack_bytes = get_type_size(foreach_node->elemType);
            chain = iter_decl;

            IR* obj_chain = generate_IR(foreach_node->array);
            if (!obj_chain) {
                INTERNAL_ERROR("IR: foreach container expression failed");
            }
            IR* obj_last = obj_chain;
            while (obj_last->next) obj_last = obj_last->next;
            chain = connect_IR(chain, obj_chain);
            char* obj_ptr = obj_last->result;

            IR* push_obj = create_IR(IR_PUSH);
            push_obj->args1 = strdup(obj_ptr);
            push_obj->args1_type = DATA_INT;
            push_obj->push_bytes = 4;
            chain = connect_IR(chain, push_obj);

            IR* call_size = create_IR(IR_CALL);
            call_size->args1 = strdup(foreach_node->container_size_method);
            call_size->result = create_temp();
            assign_def_id(call_size);
            call_size->data_type = DATA_INT;
            call_size->call_func_args_num = 1;
            call_size->total_push_bytes = 4;
            chain = connect_IR(chain, call_size);
            char* length_temp = call_size->result;

            char idx_var_name[32];
            snprintf(idx_var_name, sizeof(idx_var_name),
                     "_foreach_idx%d", foreach_idx_counter++);

            IR* idx_init = create_IR(IR_VAR);
            idx_init->result = strdup(idx_var_name);
            idx_init->args1 = strdup("0");
            idx_init->args1_type = DATA_INT;
            idx_init->data_type = DATA_INT;
            chain = connect_IR(chain, idx_init);

            char* start_label = create_label();
            char* end_label   = create_label();
            char* cont_label  = create_label();
            push_context(end_label, cont_label);

            IR* label_start = create_IR(IR_LABEL);
            label_start->args1 = strdup(start_label);
            chain = connect_IR(chain, label_start);

            IR* idx_load = create_IR(IR_LITERAL);
            idx_load->result = create_temp();
            assign_def_id(idx_load);
            idx_load->args1 = strdup(idx_var_name);
            idx_load->args1_type = DATA_INT;
            idx_load->data_type = DATA_INT;
            chain = connect_IR(chain, idx_load);

            IR* cmp = create_IR(IR_BINARY);
            cmp->op = IR_JL;
            cmp->args1 = strdup(idx_load->result);
            cmp->args1_type = DATA_INT;
            cmp->args2 = strdup(length_temp);
            cmp->args2_type = DATA_INT;
            cmp->result = create_temp();
            assign_def_id(cmp);
            cmp->data_type = DATA_BOOL;
            chain = connect_IR(chain, cmp);

            IR* jmpz = create_IR(IR_JMPZ);
            jmpz->args1 = strdup(cmp->result);
            jmpz->args2 = strdup(end_label);
            chain = connect_IR(chain, jmpz);

            IR* idx_val = create_IR(IR_LITERAL);
            idx_val->result = create_temp();
            assign_def_id(idx_val);
            idx_val->args1 = strdup(idx_var_name);
            idx_val->args1_type = DATA_INT;
            idx_val->data_type = DATA_INT;
            chain = connect_IR(chain, idx_val);

            IR* push_idx = create_IR(IR_PUSH);
            push_idx->args1 = strdup(idx_val->result);
            push_idx->args1_type = DATA_INT;
            push_idx->push_bytes = 4;
            chain = connect_IR(chain, push_idx);

            IR* push_this = create_IR(IR_PUSH);
            push_this->args1 = strdup(obj_ptr);
            push_this->args1_type = DATA_INT;
            push_this->push_bytes = 4;
            chain = connect_IR(chain, push_this);

            IR* call_get = create_IR(IR_CALL);
            call_get->args1 = strdup(foreach_node->container_get_method);
            call_get->result = create_temp();
            assign_def_id(call_get);
            call_get->data_type = foreach_node->elemType;
            call_get->call_func_args_num = 2;
            call_get->total_push_bytes = 8;
            chain = connect_IR(chain, call_get);

            IR* assign = create_IR(IR_VAR_ASS);
            assign->result = strdup(foreach_node->internal_iterator);
            assign->args1 = strdup(call_get->result);
            assign->args1_type = foreach_node->elemType;
            assign->data_type = foreach_node->elemType;
            int elem_sz = get_type_size(foreach_node->elemType);
            if (elem_sz > 4) assign->copy_bytes = elem_sz;
            chain = connect_IR(chain, assign);

            if (foreach_node->code) {
                IR* body_ir = start_generate_IR(foreach_node->code);
                chain = connect_IR(chain, body_ir);
            }

            IR* cont_lbl = create_IR(IR_LABEL);
            cont_lbl->args1 = strdup(cont_label);
            chain = connect_IR(chain, cont_lbl);

            IR* inc_load = create_IR(IR_LITERAL);
            inc_load->result = create_temp();
            assign_def_id(inc_load);
            inc_load->args1 = strdup(idx_var_name);
            inc_load->args1_type = DATA_INT;
            inc_load->data_type = DATA_INT;
            chain = connect_IR(chain, inc_load);

            IR* inc = create_IR(IR_BINARY);
            inc->op = IR_ADD;
            inc->args1 = strdup(inc_load->result);
            inc->args1_type = DATA_INT;
            inc->args2 = strdup("1");
            inc->args2_type = DATA_INT;
            inc->result = create_temp();
            assign_def_id(inc);
            inc->data_type = DATA_INT;
            chain = connect_IR(chain, inc);

            IR* inc_assign = create_IR(IR_VAR_ASS);
            inc_assign->result = strdup(idx_var_name);
            inc_assign->args1 = strdup(inc->result);
            inc_assign->args1_type = DATA_INT;
            inc_assign->data_type = DATA_INT;
            chain = connect_IR(chain, inc_assign);

            IR* jmp_back = create_IR(IR_JMP);
            jmp_back->args1 = strdup(start_label);
            chain = connect_IR(chain, jmp_back);

            IR* label_end = create_IR(IR_LABEL);
            label_end->args1 = strdup(end_label);
            chain = connect_IR(chain, label_end);

            pop_context();
            return chain;
        }

        IR* chain = NULL;

        IR* iter_decl = create_IR(IR_VAR_UNINIT);
        iter_decl->result = strdup(foreach_node->internal_iterator);
        iter_decl->data_type = foreach_node->elemType;
        iter_decl->stack_bytes = get_type_size(foreach_node->elemType);

        chain = iter_decl;

        IR* array_ir = generate_IR(foreach_node->array);
        IR* array_last = array_ir;
        while (array_last->next) array_last = array_last->next;
        char* array_base = array_last->result;
        if (array_last->type == IR_LITERAL) {
            array_last->data_type = DATA_INT;
            array_last->args1_type = DATA_INT;
        }
        chain = connect_IR(chain, array_ir);

        char* length_temp = NULL;
        if (foreach_node->is_dynamic_array
            && (foreach_node->array->type == AST_LITERAL
                || foreach_node->array->type == AST_VAR
                || foreach_node->array->type == AST_VAR_ASS)) {
            const char* dyn_internal = NULL;
            if (foreach_node->array->type == AST_LITERAL) {
                AST_Literal* dl = (AST_Literal*)foreach_node->array;
                dyn_internal = dl->internal_name ? dl->internal_name
                                                 : dl->base.value.value;
            } else {
                AST_Var* dv = (AST_Var*)foreach_node->array;
                dyn_internal = dv->internal_name ? dv->internal_name
                                                 : dv->name.value;
            }
            IR* dp_lit = create_IR(IR_LITERAL);
            dp_lit->result = create_temp();
            assign_def_id(dp_lit);
            dp_lit->args1 = strdup(dyn_internal ? dyn_internal : "0");
            dp_lit->args1_type = DATA_INT;
            dp_lit->data_type = DATA_INT;
            chain = connect_IR(chain, dp_lit);

            IR* dp_sub = create_IR(IR_BINARY);
            dp_sub->op = IR_SUB;
            dp_sub->args1 = strdup(dp_lit->result);
            dp_sub->args1_type = DATA_INT;
            dp_sub->args2 = strdup("4");
            dp_sub->args2_type = DATA_INT;
            dp_sub->result = create_temp();
            assign_def_id(dp_sub);
            dp_sub->data_type = DATA_INT;
            chain = connect_IR(chain, dp_sub);

            IR* dp_len = create_IR(IR_DEREF);
            dp_len->result = create_temp();
            assign_def_id(dp_len);
            dp_len->args1 = strdup(dp_sub->result);
            dp_len->args1_type = DATA_INT;
            dp_len->data_type = DATA_INT;
            dp_len->ptr_depth = 0;
            chain = connect_IR(chain, dp_len);
            length_temp = dp_len->result;
        } else if (foreach_node->array->type == AST_ARRAY_ACCESS) {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)foreach_node->array;
            if (acc->dim_count > 0 && acc->dim_sizes != NULL) {
                char len_str[16];
                sprintf(len_str, "%d", acc->dim_sizes[0]);
                IR* len_lit = create_IR(IR_LITERAL);
                len_lit->result = create_temp();
                assign_def_id(len_lit);
                len_lit->args1 = strdup(len_str);
                len_lit->args1_type = DATA_INT;
                len_lit->data_type = DATA_INT;
                chain = connect_IR(chain, len_lit);
                length_temp = len_lit->result;
            } else {
                INTERNAL_ERROR("IR: foreach subarray has unknown size");
            }
        } else {
            if (!foreach_node->is_dynamic_array && foreach_node->total_elements > 0) {
                IR* len_lit = create_IR(IR_LITERAL);
                len_lit->result = create_temp();
                assign_def_id(len_lit);
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", foreach_node->total_elements);
                len_lit->args1 = strdup(buf);
                len_lit->args1_type = DATA_INT;
                len_lit->data_type = DATA_INT;
                chain = connect_IR(chain, len_lit);
                length_temp = len_lit->result;
            } else {
                AST_Literal* lit = (AST_Literal*)foreach_node->array;
                const char* internal_name = lit->internal_name;
                if (internal_name == NULL) {
                    internal_name = lit->base.value.value;
                }
                IR* addr_ir = create_IR(IR_ADDRESS_OF);
                addr_ir->result = create_temp();
                assign_def_id(addr_ir);
                addr_ir->args1 = strdup(internal_name);
                addr_ir->args1_type = DATA_INT;
                addr_ir->data_type = DATA_INT;
                IR* off_ir = create_IR(IR_BINARY);
                off_ir->op = IR_ADD;
                off_ir->args1 = strdup(addr_ir->result);
                off_ir->args1_type = DATA_INT;
                off_ir->args2 = strdup("4");
                off_ir->args2_type = DATA_INT;
                off_ir->result = create_temp();
                assign_def_id(off_ir);
                off_ir->data_type = DATA_INT;
                IR* len_ir = create_IR(IR_DEREF);
                len_ir->result = create_temp();
                assign_def_id(len_ir);
                len_ir->args1 = strdup(off_ir->result);
                len_ir->args1_type = DATA_INT;
                len_ir->data_type = DATA_INT;
                len_ir->ptr_depth = 0;
                chain = connect_IR(chain, addr_ir);
                chain = connect_IR(chain, off_ir);
                chain = connect_IR(chain, len_ir);
                length_temp = len_ir->result;
            }
        }
        char idx_var_name[32];
        snprintf(idx_var_name, sizeof(idx_var_name),
                 "_foreach_idx%d", foreach_idx_counter++);

        IR* idx_init = create_IR(IR_VAR);
        idx_init->result = strdup(idx_var_name);
        idx_init->args1 = strdup("0");
        idx_init->args1_type = DATA_INT;
        idx_init->data_type = DATA_INT;
        chain = connect_IR(chain, idx_init);

        char* start_label = create_label();
        char* end_label   = create_label();
        char* continue_label = create_label();
        push_context(end_label, continue_label);

        IR* label_start = create_IR(IR_LABEL);
        label_start->args1 = strdup(start_label);
        chain = connect_IR(chain, label_start);

        IR* idx_load = create_IR(IR_LITERAL);
        idx_load->result = create_temp();
        assign_def_id(idx_load);
        idx_load->args1 = strdup(idx_var_name);
        idx_load->args1_type = DATA_INT;
        idx_load->data_type = DATA_INT;
        chain = connect_IR(chain, idx_load);

        IR* cmp = create_IR(IR_BINARY);
        cmp->op = IR_JL;
        cmp->args1 = strdup(idx_load->result);
        cmp->args1_type = DATA_INT;
        cmp->args2 = strdup(length_temp);
        cmp->args2_type = DATA_INT;
        cmp->result = create_temp();
        assign_def_id(cmp);
        cmp->data_type = DATA_BOOL;
        chain = connect_IR(chain, cmp);

        IR* jmpz = create_IR(IR_JMPZ);
        jmpz->args1 = strdup(cmp->result);
        jmpz->args2 = strdup(end_label);
        chain = connect_IR(chain, jmpz);

        int elem_size = get_type_size(foreach_node->elemType);

        IR* idx_val = create_IR(IR_LITERAL);
        idx_val->result = create_temp();
        assign_def_id(idx_val);
        idx_val->args1 = strdup(idx_var_name);
        idx_val->args1_type = DATA_INT;
        idx_val->data_type = DATA_INT;
        chain = connect_IR(chain, idx_val);

        IR* mul = create_IR(IR_BINARY);
        mul->op = IR_MUL;
        mul->args1 = strdup(idx_val->result);
        mul->args1_type = DATA_INT;
        char esz[16];
        sprintf(esz, "%d", elem_size);
        mul->args2 = strdup(esz);
        mul->args2_type = DATA_INT;
        mul->result = create_temp();
        assign_def_id(mul);
        mul->data_type = DATA_INT;
        chain = connect_IR(chain, mul);

        IR* add = create_IR(IR_BINARY);
        add->op = IR_ADD;
        add->args1 = strdup(array_base);
        add->args1_type = DATA_INT;
        add->args2 = strdup(mul->result);
        add->args2_type = DATA_INT;
        add->result = create_temp();
        assign_def_id(add);
        add->data_type = DATA_INT;
        chain = connect_IR(chain, add);

        if (elem_size > 4) {
            IR* iter_addr = create_IR(IR_ADDRESS_OF);
            iter_addr->result = create_temp();
            assign_def_id(iter_addr);
            iter_addr->args1 = strdup(foreach_node->internal_iterator);
            iter_addr->args1_type = DATA_INT;
            iter_addr->data_type = DATA_INT;
            iter_addr->is_function_addr = false;

            IR* copy = create_IR(IR_COPY_BLOCK);
            copy->args1 = strdup(iter_addr->result);
            copy->args1_type = DATA_INT;
            copy->args2 = strdup(add->result);
            copy->args2_type = DATA_INT;
            char sz_buf[16];
            snprintf(sz_buf, sizeof(sz_buf), "%d", elem_size);
            copy->result = strdup(sz_buf);
            copy->data_type = DATA_INT;
            copy->copy_bytes = elem_size;
            assign_def_id(copy);
            chain = connect_IR(chain, iter_addr);
            chain = connect_IR(chain, copy);
        } else {
            IR* load = create_IR(IR_ARRAY_LOAD);
            load->args1 = strdup(add->result);
            load->args1_type = DATA_INT;
            load->result = create_temp();
            assign_def_id(load);
            load->data_type = foreach_node->elemType;
            load->elem_size = elem_size;
            chain = connect_IR(chain, load);
            IR* assign = create_IR(IR_VAR_ASS);
            assign->result = strdup(foreach_node->internal_iterator);
            assign->args1 = strdup(load->result);
            assign->args1_type = load->data_type;
            assign->data_type = load->data_type;
            chain = connect_IR(chain, assign);
        }

        if (foreach_node->code) {
            IR* body_ir = start_generate_IR(foreach_node->code);
            chain = connect_IR(chain, body_ir);
        }

        IR* cont_lbl = create_IR(IR_LABEL);
        cont_lbl->args1 = strdup(continue_label);
        chain = connect_IR(chain, cont_lbl);

        IR* inc_load = create_IR(IR_LITERAL);
        inc_load->result = create_temp();
        assign_def_id(inc_load);
        inc_load->args1 = strdup(idx_var_name);
        inc_load->args1_type = DATA_INT;
        inc_load->data_type = DATA_INT;
        chain = connect_IR(chain, inc_load);

        IR* inc = create_IR(IR_BINARY);
        inc->op = IR_ADD;
        inc->args1 = strdup(inc_load->result);
        inc->args1_type = DATA_INT;
        inc->args2 = strdup("1");
        inc->args2_type = DATA_INT;
        inc->result = create_temp();
        assign_def_id(inc);
        inc->data_type = DATA_INT;
        chain = connect_IR(chain, inc);

        IR* inc_assign = create_IR(IR_VAR_ASS);
        inc_assign->result = strdup(idx_var_name);
        inc_assign->args1 = strdup(inc->result);
        inc_assign->args1_type = DATA_INT;
        inc_assign->data_type = DATA_INT;
        chain = connect_IR(chain, inc_assign);

        IR* jmp_back = create_IR(IR_JMP);
        jmp_back->args1 = strdup(start_label);
        chain = connect_IR(chain, jmp_back);

        IR* label_end = create_IR(IR_LABEL);
        label_end->args1 = strdup(end_label);
        chain = connect_IR(chain, label_end);

        pop_context();
        return chain;
    }

    case AST_BREAK: {
        const char* label = current_break_label();
        if (!label) {
            INTERNAL_ERROR("IR: break outside loop/switch");
        }
        if (finally_top >= 0
            && finally_stack[finally_top].break_label != NULL
            && strcmp(finally_stack[finally_top].break_label, label) == 0) {
            IR* sreason = create_IR(IR_LITERAL);
            sreason->result = strdup(finally_stack[finally_top].reason_temp);
            assign_def_id(sreason);
            sreason->args1 = strdup("3");
            sreason->args1_type = DATA_INT;
            sreason->data_type = DATA_INT;
            IR* jf = create_IR(IR_JMP);
            jf->args1 = strdup(finally_stack[finally_top].finally_label);
            IR* chain = connect_IR(sreason, ir_exc_frame_pop());
            chain = connect_IR(chain, jf);
            return chain;
        }
        IR* jmp = create_IR(IR_JMP);
        jmp->args1 = strdup(label);
        return jmp;
    }

    case AST_CONTINUE: {
        const char* label = current_continue_label();
        if (!label) {
            INTERNAL_ERROR("IR: continue outside loop");
        }
        if (finally_top >= 0
            && finally_stack[finally_top].continue_label != NULL
            && strcmp(finally_stack[finally_top].continue_label, label) == 0) {
            IR* sreason = create_IR(IR_LITERAL);
            sreason->result = strdup(finally_stack[finally_top].reason_temp);
            assign_def_id(sreason);
            sreason->args1 = strdup("4");
            sreason->args1_type = DATA_INT;
            sreason->data_type = DATA_INT;
            IR* jf = create_IR(IR_JMP);
            jf->args1 = strdup(finally_stack[finally_top].finally_label);
            IR* chain = connect_IR(sreason, ir_exc_frame_pop());
            chain = connect_IR(chain, jf);
            return chain;
        }
        IR* jmp = create_IR(IR_JMP);
        jmp->args1 = strdup(label);
        return jmp;
    }

    case AST_FUNCTION: {
        if (((AST_Function*)ast_node)->is_native) {
            return NULL;
        }

        int saved_temp_i = temp_i;
        temp_i = 0;

        AST_Function* saved_ir_func = current_ir_function;
        char* saved_ret_ptr = current_ir_ret_ptr;
        current_ir_function = (AST_Function*)ast_node;
        current_ir_ret_ptr = NULL;

        AST_Function* ast_function = (AST_Function*)ast_node;

        int func_local_count = count_local_vars(ast_function->Code);
        IR* start_function_ir = create_IR(IR_LABEL);
        if (strcmp(ast_function->name.value, "main") == 0)
            start_function_ir->args1 = strdup("_main");
        else
            start_function_ir->args1 = strdup(ast_function->internal_name);
        start_function_ir->has_return = true;
        start_function_ir->func_local_count = func_local_count;
        if (ast_function->has_varargs && ast_function->vararg_param_count > 0) {
            start_function_ir->vararg_count = ast_function->vararg_param_count;
            start_function_ir->vararg_bytes_array = malloc(sizeof(int) * ast_function->vararg_param_count);
            start_function_ir->vararg_internal_names_array = malloc(sizeof(char*) * ast_function->vararg_param_count);
            for (int vi = 0; vi < ast_function->vararg_param_count; vi++) {
                int func_temp_bytes = 0;
                if (ast_function->multi_return != NULL ||
                    ast_function->returns_array ||
                    ast_function->returns_struct) {
                    func_temp_bytes += 4;
                }
                for (int pi = 0; pi < ast_function->vararg_param_indices[vi]; pi++) {
                    bool is_prev_va = false;
                    for (int vj = 0; vj < vi; vj++) {
                        if (ast_function->vararg_param_indices[vj] == pi)
                            is_prev_va = true;
                    }
                    int sz = get_type_size(ast_function->parameterType[pi]);
                    if (sz < 4) sz = 4;
                    if (is_prev_va) {
                        ASTNode* pn = ast_function->Parameter;
                        for (int walk = 0; walk < pi && pn; walk++) pn = pn->next;
                        if (pn && (pn->type == AST_VAR || pn->type == AST_VAR_ASS
                                   || pn->type == AST_UNINIT_VAR)) {
                            AST_Var* pv = (AST_Var*)pn;
                            if (pv->func_ptr_sig != NULL) {
                                sz = 4;
                            } else {
                                sz = 8;
                            }
                        } else {
                            sz = 8;
                        }
                    } else {
                        ASTNode* pn = ast_function->Parameter;
                        for (int walk = 0; walk < pi && pn; walk++) pn = pn->next;
                        if (pn) {
                            if (pn->type == AST_ARRAY_DECL) {
                                AST_ArrayDecl* arr = (AST_ArrayDecl*)pn;
                                bool all_dynamic = true;
                                for (int d = 0; d < arr->dim_count; d++) {
                                    if (arr->dim_sizes[d] > 0) { all_dynamic = false; break; }
                                }
                                if (all_dynamic && arr->dim_count > 0) sz = 8;
                            } else if (pn->type == AST_VAR || pn->type == AST_VAR_ASS
                                       || pn->type == AST_UNINIT_VAR) {
                                AST_Var* pv = (AST_Var*)pn;
                                if (pv->total_bytes > 4) sz = pv->total_bytes;
                            }
                        }
                    }
                    func_temp_bytes += sz;
                }
                start_function_ir->vararg_bytes_array[vi] = func_temp_bytes;
                const char* va_internal = ast_function->vararg_internal_names[vi];
                ASTNode* vn = ast_function->Parameter;
                int vii = 0;
                while (vn && vii < ast_function->vararg_param_indices[vi]) {
                    vn = vn->next;
                    vii++;
                }
                if (vn) {
                    if (vn->type == AST_VAR || vn->type == AST_UNINIT_VAR) {
                        AST_Var* var = (AST_Var*)vn;
                        if (var->internal_name) va_internal = var->internal_name;
                    } else if (vn->type == AST_ARRAY_DECL) {
                        AST_ArrayDecl* arr = (AST_ArrayDecl*)vn;
                        if (arr->internal_name) va_internal = arr->internal_name;
                    }
                }
                start_function_ir->vararg_internal_names_array[vi] = strdup(va_internal);
            }
            start_function_ir->vararg_internal_name_ir = strdup(start_function_ir->vararg_internal_names_array[0]);
        }
        IR* func_entry = start_function_ir;

        IR* param_chain = NULL;
        IR* last_param = NULL;
        ASTNode* param_node = ast_function->Parameter;
        IR* hidden_param = NULL;
        if (ast_function->returns_array || ast_function->returns_struct) {
            bool all_dynamic = true;
            if (!ast_function->returns_struct) {
                for (int i = 0; i < ast_function->return_dim_count; i++) {
                    if (ast_function->return_dim_sizes[i] > 0) {
                        all_dynamic = false;
                        break;
                    }
                }
            } else {
                all_dynamic = false;
            }
            if (!all_dynamic) {
                current_ir_ret_ptr = create_ret_ptr_temp();
                hidden_param = create_IR(IR_PARAM_DEF);
                hidden_param->result = strdup(current_ir_ret_ptr);
                hidden_param->data_type = DATA_INT;
                hidden_param->next = NULL;
                func_entry->func_local_count++;
            }
        }

        if (ast_function->multi_return != NULL) {
            current_ir_ret_ptr = create_ret_ptr_temp();
            IR* multi_hidden = create_IR(IR_PARAM_DEF);
            multi_hidden->result = strdup(current_ir_ret_ptr);
            multi_hidden->data_type = DATA_INT;
            multi_hidden->next = NULL;
            func_entry->func_local_count++;
            if (param_chain == NULL) {
                param_chain = multi_hidden;
                last_param = multi_hidden;
            } else {
                multi_hidden->next = param_chain;
                param_chain = multi_hidden;
            }
        }

        int param_idx = 0;
        while (param_node) {
            char* internal_name = NULL;
            DataType param_type = DATA_ERROR;
            int param_bytes = 4;
            bool is_array_val = false;
            bool is_dynamic_param = false;
            int arr_size = 0;
            int elem_sz = 4;

            if (param_node->type == AST_VAR || param_node->type == AST_UNINIT_VAR) {
                AST_Var* var = (AST_Var*)param_node;
                internal_name = var->internal_name;
                param_type = var->dataType;
                elem_sz = get_type_size(var->dataType);
                param_bytes = get_type_size(param_type);
                for (int vj = 0; vj < ast_function->vararg_param_count; vj++) {
                    if (ast_function->vararg_param_indices[vj] == param_idx) {
                        param_bytes = 8;
                        break;
                    }
                }
                if (var->dim_count > 0) {
                    param_bytes = var->total_bytes;
                    is_array_val = true;
                    arr_size = 1;
                    for (int d = 0; d < var->dim_count; d++) {
                        if (var->dim_sizes[d] > 0)
                            arr_size *= var->dim_sizes[d];
                    }
                } else if (var->total_bytes > 4 && internal_name != NULL
                           && strcmp(internal_name, "_this") != 0
                           && strcmp(var->name.value, "this") != 0) {
                    param_bytes = var->total_bytes;
                    is_array_val = false;
                }
            } else if (param_node->type == AST_ARRAY_DECL) {
                AST_ArrayDecl* arr = (AST_ArrayDecl*)param_node;
                internal_name = arr->internal_name;
                param_type = arr->elementType;
                elem_sz = get_type_size(arr->elementType);
                bool all_dynamic = (arr->dim_count > 0);
                for (int d = 0; d < arr->dim_count; d++) {
                    if (arr->dim_sizes[d] > 0) { all_dynamic = false; break; }
                }
                param_bytes = all_dynamic ? 8 : 4;
                is_array_val = true;
                is_dynamic_param = all_dynamic;
                arr_size = 1;
                for (int d = 0; d < arr->dim_count; d++) {
                    if (arr->dim_sizes[d] > 0)
                        arr_size *= arr->dim_sizes[d];
                }
            } else if (param_node->type == AST_VARARG) {
                param_node = param_node->next;
                param_idx++;
                continue;
            } else {
                INTERNAL_ERROR("IR: unexpected parameter node type");
            }

            IR* param_def = create_IR(IR_PARAM_DEF);
            param_def->result = strdup(internal_name);
            param_def->data_type = param_type;
            param_def->push_bytes = param_bytes;
            param_def->array_size = arr_size;
            param_def->elem_size = elem_sz;
            param_def->is_array_param = is_array_val;
            param_def->is_dynamic_array = is_dynamic_param;
            param_def->data_type = param_type;
            param_def->push_bytes = param_bytes;
            param_def->next = NULL;

            if (param_chain == NULL) {
                param_chain = param_def;
                last_param = param_def;
            } else {
                last_param->next = param_def;
                last_param = param_def;
            }
            param_node = param_node->next;
            param_idx++;
        }

        if (ast_function->multi_return == NULL && ast_function->Code != NULL) {
            ASTNode* scan = ast_function->Code;
            while (scan) {
                if (scan->type == AST_MULTI_RETURN) {
                    AST_MultiReturn* mr = (AST_MultiReturn*)scan;
                    MultiReturnType* mrt = calloc(1, sizeof(MultiReturnType));
                    if (!mrt) OUT_OF_MEMORY();
                    mrt->count = mr->value_count;
                    mrt->types = malloc(sizeof(DataType) * mr->value_count);
                    mrt->is_variadic = malloc(sizeof(bool) * mr->value_count);
                    mrt->func_sigs = calloc(mr->value_count, sizeof(FuncSignature*));
                    mrt->type_param_indices = calloc(mr->value_count, sizeof(int));
                    for (int i = 0; i < mr->value_count; i++) {
                        mrt->types[i] = DATA_INT;
                        mrt->is_variadic[i] = false;
                        mrt->type_param_indices[i] = -1;
                    }
                    ast_function->multi_return = mrt;
                    if (current_ir_ret_ptr == NULL) {
                        current_ir_ret_ptr = create_ret_ptr_temp();
                        IR* multi_hidden2 = create_IR(IR_PARAM_DEF);
                        multi_hidden2->result = strdup(current_ir_ret_ptr);
                        multi_hidden2->data_type = DATA_INT;
                        multi_hidden2->next = NULL;
                        func_entry->func_local_count++;
                        if (param_chain == NULL) {
                            param_chain = multi_hidden2;
                            last_param = multi_hidden2;
                        } else {
                            multi_hidden2->next = param_chain;
                            param_chain = multi_hidden2;
                        }
                    }
                    break;
                }
                scan = scan->next;
            }
        }

        IR* func_body_ir = NULL;
        if (ast_function->Code != NULL) {
            func_body_ir = start_generate_IR(ast_function->Code);
        }
        if (current_ir_ret_ptr != NULL) {
            bool wrote_ret_buf = false;
            {
                IR* scan = func_body_ir;
                while (scan) {
                    if (scan->args1 && strcmp(scan->args1, current_ir_ret_ptr) == 0) {
                        wrote_ret_buf = true;
                        break;
                    }
                    scan = scan->next;
                }
            }
            if (!wrote_ret_buf) {
                IR* prev = NULL;
                IR* cur = func_body_ir;
                while (cur) {
                    IR* next = cur->next;
                    if (cur->type == IR_RET && cur->ret_has_hidden_ptr
                        && !cur->has_return) {
                        if (prev) prev->next = next;
                        else func_body_ir = next;
                        cur->next = NULL;
                        free(cur);
                    } else {
                        prev = cur;
                    }
                    cur = next;
                }
                ASTNode* code = ast_function->Code;
                while (code) {
                    if (code->type == AST_MULTI_RETURN) {
                        IR* mr_ir = generate_IR(code);
                        if (mr_ir) {
                            func_body_ir = connect_IR(func_body_ir, mr_ir);
                        }
                        break;
                    }
                    code = code->next;
                }
            }

            if (func_body_ir == NULL) {
                ASTNode* code = ast_function->Code;
                while (code) {
                    if (code->type == AST_MULTI_RETURN) {
                        AST_MultiReturn* mr = (AST_MultiReturn*)code;
                        MultiReturnType* mrt = ast_function->multi_return;
                        int offset = 0;
                        for (int i = 0; i < mr->value_count && i < mrt->count; i++) {
                            ASTNode* val = mr->values[i];
                            const char* func_addr_name = NULL;
                            if (val->type == AST_LITERAL) {
                                AST_Literal* lit = (AST_Literal*)val;
                                func_addr_name = lit->internal_name;
                                if (!func_addr_name) func_addr_name = lit->base.value.value;
                            }
                            if (!func_addr_name) func_addr_name = "0";

                            IR* addr_ir = create_IR(IR_ADDRESS_OF);
                            addr_ir->result = create_temp();
                            assign_def_id(addr_ir);
                            addr_ir->args1 = strdup(func_addr_name);
                            addr_ir->args1_type = DATA_INT;
                            addr_ir->data_type = DATA_INT;
                            addr_ir->is_function_addr = true;
                            if (func_body_ir == NULL) {
                                func_body_ir = addr_ir;
                            } else {
                                IR* t = func_body_ir;
                                while (t->next) t = t->next;
                                t->next = addr_ir;
                            }

                            IR* dst_addr = create_IR(IR_BINARY);
                            dst_addr->op = IR_ADD;
                            dst_addr->args1 = strdup(current_ir_ret_ptr);
                            dst_addr->args1_type = DATA_INT;
                            char off_str[16];
                            sprintf(off_str, "%d", offset);
                            dst_addr->args2 = strdup(off_str);
                            dst_addr->args2_type = DATA_INT;
                            dst_addr->result = create_temp();
                            assign_def_id(dst_addr);
                            dst_addr->data_type = DATA_INT;
                            addr_ir->next = dst_addr;

                            IR* store = create_IR(IR_PTR_STORE);
                            store->args1 = strdup(dst_addr->result);
                            store->args1_type = DATA_INT;
                            store->args2 = strdup(addr_ir->result);
                            store->args2_type = DATA_INT;
                            store->data_type = DATA_INT;
                            dst_addr->next = store;

                            if (mrt->is_variadic && mrt->is_variadic[i]) {
                                offset += 8;
                            } else {
                                offset += 4;
                            }
                        }
                        IR* ret_ir = create_IR(IR_RET);
                        ret_ir->has_return = false;
                        ret_ir->data_type = DATA_VOID;
                        ret_ir->ret_has_hidden_ptr = true;
                        IR* t = func_body_ir;
                        while (t && t->next) t = t->next;
                        if (t) t->next = ret_ir;
                        else func_body_ir = ret_ir;
                        break;
                    }
                    code = code->next;
                }
            }
        }
        ASTNode* s = ast_function->Code;
        while (s) {
            if (s->type == AST_VAR || s->type == AST_UNINIT_VAR) {
                AST_Var* sv = (AST_Var*)s;
                if (sv->is_static_local && sv->internal_name
                    && sv->static_guard_name) {
                    IR* d = create_IR(sv->has_static_init ? IR_VAR : IR_VAR_UNINIT);
                    d->result    = strdup(sv->internal_name);
                    d->data_type = sv->dataType;
                    d->is_static_local = true;
                    if (sv->static_init_value) {
                        d->args1      = strdup(sv->static_init_value);
                        d->args1_type = sv->dataType;
                    }
                    d->next = func_body_ir;
                    func_body_ir = d;

                    IR* g = create_IR(IR_VAR_UNINIT);
                    g->result    = strdup(sv->static_guard_name);
                    g->data_type = DATA_INT;
                    g->next = func_body_ir;
                    g->is_static_local = true;
                    func_body_ir = g;
                }
            }
            s = s->next;
        }
        if (ast_function->multi_return_node != NULL && ast_function->Code == NULL) {
            IR* ret_ir = generate_IR((ASTNode*)ast_function->multi_return_node);
            if (func_body_ir) {
                func_body_ir = connect_IR(func_body_ir, ret_ir);
            } else {
                func_body_ir = ret_ir;
            }
        } else if (ast_function->Return != NULL && ast_function->Code == NULL) {
            IR* ret_ir = generate_IR((ASTNode*)ast_function->Return);
            if (func_body_ir) {
                func_body_ir = connect_IR(func_body_ir, ret_ir);
            } else {
                func_body_ir = ret_ir;
            }
        }

        IR* nested_funcs_ir = NULL;
        IR* nested_funcs_tail = NULL;
        {
            IR* prev = NULL;
            IR* cur = func_body_ir;
            while (cur) {
                if (cur->type == IR_LABEL && cur->has_return) {
                    IR* nf_start = cur;
                    IR* nf_end = cur;
                    while (nf_end) {
                        if (nf_end != nf_start && nf_end->type == IR_LABEL
                            && nf_end->has_return) {
                            break;
                        }
                        if (nf_end->type == IR_RET) {
                            break;
                        }
                        nf_end = nf_end->next;
                    }
                    if (!nf_end) break;
                    IR* after_nf = nf_end->next;
                    nf_end->next = NULL;
                    if (prev) {
                        prev->next = after_nf;
                    } else {
                        func_body_ir = after_nf;
                    }
                    if (!nested_funcs_ir) {
                        nested_funcs_ir = nf_start;
                        nested_funcs_tail = nf_end;
                    } else {
                        nested_funcs_tail->next = nf_start;
                        nested_funcs_tail = nf_end;
                    }
                    cur = after_nf;
                    continue;
                }
                prev = cur;
                cur = cur->next;
            }
        }

        IR* func_last = func_body_ir;
        while (func_last && func_last->next) func_last = func_last->next;
        if (ast_function->multi_return == NULL) {
            if (func_last == NULL || func_last->type != IR_RET) {
                IR* default_ret = create_IR(IR_RET);
                default_ret->has_return = false;
                func_body_ir = connect_IR(func_body_ir, default_ret);
                func_last = default_ret;
            }
        }

        if (nested_funcs_ir) {
            func_last->next = nested_funcs_ir;
        }

        if (ast_function->has_varargs && ast_function->vararg_param_count > 0) {
            for (int vi = 0; vi < ast_function->vararg_param_count; vi++) {
                bool is_func_ptr = false;
                ASTNode* pn = ast_function->Parameter;
                int pidx = ast_function->vararg_param_indices[vi];
                for (int i = 0; i < pidx && pn; i++) pn = pn->next;
                if (pn && (pn->type == AST_VAR || pn->type == AST_UNINIT_VAR)) {
                    AST_Var* pv = (AST_Var*)pn;
                    if (pv->func_ptr_sig != NULL) is_func_ptr = true;
                }

                if (!is_func_ptr) {
                    IR* va_decl = create_IR(IR_VAR_UNINIT);
                    va_decl->result = strdup(ast_function->vararg_internal_names[vi]);
                    va_decl->data_type = DATA_INT;
                    va_decl->stack_bytes = 8;
                    va_decl->is_dynamic_array = true;
                    va_decl->next = func_body_ir;
                    func_body_ir = va_decl;
                }
            }
        }

        if (current_ir_ret_ptr != NULL && hidden_param != NULL) {
            hidden_param->next = param_chain;
            param_chain = hidden_param;
            if (last_param == NULL)
                last_param = hidden_param;
        }
        start_function_ir->next = param_chain;
        IR* full_chain = start_function_ir;
        if (param_chain) {
            last_param->next = func_body_ir;
        } else {
            start_function_ir->next = func_body_ir;
        }

        func_entry->func_temp_count = temp_i;

        temp_i = saved_temp_i;
        current_ir_function = saved_ir_func;
        current_ir_ret_ptr  = saved_ret_ptr;

        return full_chain;
    }

    case AST_VARARG: {
        AST_Vararg* va = (AST_Vararg*)ast_node;

        IR* var_ir = create_IR(IR_VAR_UNINIT);
        var_ir->result = strdup(va->internal_name);
        var_ir->data_type = DATA_INT;
        var_ir->stack_bytes = 4;
        return var_ir;
    }

    case AST_CLASS_DEF: {
        AST_ClassDef* cls = (AST_ClassDef*)ast_node;
        IR* chain = NULL;
        ASTNode* m = cls->members;
        while (m) {
            if (m->type == AST_FUNCTION) {
                if (cls->is_generic) {
                    AST_Function* mf = (AST_Function*)m;
                    if (mf->internal_name == NULL
                        || strchr(mf->internal_name, '$') == NULL) {
                        m = m->next;
                        continue;
                    }
                }
                ASTNode* saved_next = m->next;
                m->next = NULL;
                IR* method_ir = generate_IR(m);
                m->next = saved_next;
                chain = connect_IR(chain, method_ir);
            } else if (m->type == AST_CLASS_DEF) {
                ASTNode* saved_next = m->next;
                m->next = NULL;
                IR* nested_ir = generate_IR(m);
                m->next = saved_next;
                chain = connect_IR(chain, nested_ir);
            } else if ((m->type == AST_VAR || m->type == AST_UNINIT_VAR)
                        && ((AST_Var*)m)->is_static) {
                ASTNode* saved_next = m->next;
                m->next = NULL;
                IR* var_ir = generate_IR(m);
                m->next = saved_next;
                chain = connect_IR(chain, var_ir);
            } else if (m->type == AST_ARRAY_DECL
                        && ((AST_ArrayDecl*)m)->modifier == 3) {
                ASTNode* saved_next = m->next;
                m->next = NULL;
                IR* arr_ir = generate_IR(m);
                m->next = saved_next;
                chain = connect_IR(chain, arr_ir);
            }
            m = m->next;
        }
        if (chain == NULL) {
            IR* placeholder = create_IR(IR_LITERAL);
            placeholder->result = create_temp();
            assign_def_id(placeholder);
            placeholder->args1 = strdup("0");
            placeholder->args1_type = DATA_INT;
            placeholder->data_type = DATA_INT;
            chain = placeholder;
        }
        return chain;
    }

    case AST_NEW_INSTANCE: {
        AST_NewInstance* new_inst = (AST_NewInstance*)ast_node;

        IR* alloc_ir = create_IR(IR_ALLOC_CLASS);
        alloc_ir->result = create_temp();
        assign_def_id(alloc_ir);
        char size_str[16];
        sprintf(size_str, "%d", new_inst->instance_size);
        alloc_ir->args1 = strdup(size_str);
        alloc_ir->args1_type = DATA_INT;
        alloc_ir->data_type = DATA_INT;
        alloc_ir->class_id = new_inst->class_id;

        if (new_inst->vtable_label && new_inst->vtable_label[0]) {
            alloc_ir->vtable_label = strdup(new_inst->vtable_label);
        }

        IR* chain = alloc_ir;
        IR* last = alloc_ir;

        char* obj_temp = strdup(alloc_ir->result);

        for (int i = 0; i < new_inst->field_init_count; i++) {
            if (new_inst->field_inits == NULL) break;
            FieldInit* fi = &new_inst->field_inits[i];
            if (fi->offset < 0) continue;
            
            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(obj_temp);
            add_ir->args1_type = DATA_INT;
            char off_str[16];
            sprintf(off_str, "%d", fi->offset);
            add_ir->args2 = strdup(off_str);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;

            char* init_val = fi->value_str ? strdup(fi->value_str) : strdup("0");
            IR* val_ir = create_IR(IR_LITERAL);
            val_ir->result = create_temp();
            assign_def_id(val_ir);
            val_ir->args1 = init_val;
            val_ir->args1_type = fi->type;
            val_ir->data_type = fi->type;

            IR* store_ir = create_IR(IR_ARRAY_STORE);
            store_ir->args1 = strdup(add_ir->result);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(val_ir->result);
            store_ir->args2_type = fi->type;
            store_ir->data_type = fi->type;
            store_ir->elem_size = get_type_size(fi->type);

            last->next = add_ir;
            add_ir->next = val_ir;
            val_ir->next = store_ir;
            last = store_ir;
        }

        if (new_inst->has_constructor && new_inst->constructor_name) {
            int total_args = 1 + new_inst->constructor_arg_count;
            char** arg_temps = malloc(sizeof(char*) * total_args);
            DataType* arg_types = malloc(sizeof(DataType) * total_args);

            arg_temps[0] = strdup(obj_temp);
            arg_types[0] = DATA_INT;

            ASTNode* arg_node = new_inst->arguments;
            int idx = 1;
            while (arg_node && idx < total_args) {
                IR* arg_ir = generate_IR(arg_node);
                if (!arg_ir) {
                    for (int j = 0; j < idx; j++) free(arg_temps[j]);
                    free(arg_temps); free(arg_types);
                    free(obj_temp);
                    return chain;
                }
                IR* arg_last = arg_ir;
                while (arg_last->next) arg_last = arg_last->next;

                arg_temps[idx] = strdup(arg_last->result);
                arg_types[idx] = arg_last->data_type;

                last->next = arg_ir;
                last = arg_last;
                idx++;
                arg_node = arg_node->next;
            }

            for (int i = total_args - 1; i >= 0; i--) {
                IR* push_ir = create_IR(IR_PUSH);
                push_ir->args1 = strdup(arg_temps[i]);
                push_ir->args1_type = arg_types[i];
                last->next = push_ir;
                last = push_ir;
            }

            IR* call_ir = create_IR(IR_CALL);
            call_ir->args1 = strdup(new_inst->constructor_name);
            call_ir->result = create_temp();
            assign_def_id(call_ir);
            call_ir->data_type = DATA_VOID;
            call_ir->call_func_args_num = total_args;
            last->next = call_ir;
            last = call_ir;

            for (int i = 0; i < total_args; i++) free(arg_temps[i]);
            free(arg_temps);
            free(arg_types);
        }

        {
            IR* pass = create_IR(IR_LITERAL);
            pass->result = create_temp();
            assign_def_id(pass);
            pass->args1 = strdup(obj_temp);
            pass->args1_type = DATA_INT;
            pass->data_type = DATA_INT;

            if (last) {
                last->next = pass;
                last = pass;
            } else {
                chain = pass;
                last = pass;
            }
        }

        free(obj_temp);
        return chain;
    }

    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* access = (AST_MemberAccess*)ast_node;

        if (access->is_method_ptr) {
            IR* chain = NULL;
            #define ADD_MP(node) do { \
                if (!chain) chain = (node); \
                else { IR* t = chain; while (t->next) t = t->next; t->next = (node); } \
            } while(0)

            ASTNode* real_obj = access->object;
            if (real_obj && real_obj->type == AST_DEREF) {
                real_obj = ((AST_Deref*)real_obj)->operand;
            }
            IR* obj_chain = generate_IR(real_obj);
            IR* obj_last = obj_chain;
            while (obj_last->next) obj_last = obj_last->next;
            char* this_temp = obj_last->result;
            chain = obj_chain;

            IR* alloc = create_IR(IR_ALLOC_ARRAY);
            alloc->result = create_temp();
            assign_def_id(alloc);
            alloc->args1 = strdup("1");
            alloc->args1_type = DATA_INT;
            alloc->elem_size = 8;
            alloc->data_type = DATA_INT;
            alloc->stack_bytes = 8;
            ADD_MP(alloc);
            char* fat_ptr = alloc->result;

            IR* store_this = create_IR(IR_PTR_STORE);
            store_this->args1 = strdup(fat_ptr);
            store_this->args1_type = DATA_INT;
            store_this->args2 = strdup(this_temp);
            store_this->args2_type = DATA_INT;
            store_this->data_type = DATA_INT;
            ADD_MP(store_this);

            IR* func_addr = create_IR(IR_ADDRESS_OF);
            func_addr->result = create_temp();
            assign_def_id(func_addr);
            func_addr->args1 = strdup(access->method_internal);
            func_addr->args1_type = DATA_INT;
            func_addr->data_type = DATA_INT;
            func_addr->is_function_addr = true;
            ADD_MP(func_addr);

            IR* off4 = create_IR(IR_BINARY);
            off4->op = IR_ADD;
            off4->args1 = strdup(fat_ptr);
            off4->args1_type = DATA_INT;
            off4->args2 = strdup("4");
            off4->args2_type = DATA_INT;
            off4->result = create_temp();
            assign_def_id(off4);
            off4->data_type = DATA_INT;
            ADD_MP(off4);

            IR* store_func = create_IR(IR_PTR_STORE);
            store_func->args1 = strdup(off4->result);
            store_func->args1_type = DATA_INT;
            store_func->args2 = strdup(func_addr->result);
            store_func->args2_type = DATA_INT;
            store_func->data_type = DATA_INT;
            ADD_MP(store_func);

            IR* result = create_IR(IR_LITERAL);
            result->result = create_temp();
            assign_def_id(result);
            result->args1 = strdup(fat_ptr);
            result->args1_type = DATA_INT;
            result->data_type = DATA_INT;
            ADD_MP(result);

            #undef ADD_MP
            return chain;
        }

        if (access->is_method) {
            IR* lit = create_IR(IR_LITERAL);
            lit->result = create_temp();
            assign_def_id(lit);
            lit->args1 = strdup(access->member_name);
            lit->args1_type = DATA_IDENTIFIER;
            lit->data_type = access->member_type;
            return lit;
        }
        if (access->const_value_str != NULL) {
            IR* lit = create_IR(IR_LITERAL);
            lit->result = create_temp();
            assign_def_id(lit);
            lit->args1 = strdup(access->const_value_str);
            lit->args1_type = access->member_type;
            lit->data_type = access->member_type;
            return lit;
        }
        if (access->member_offset == -1) {
            IR* lit = create_IR(IR_LITERAL);
            lit->result = create_temp();
            assign_def_id(lit);
            lit->args1 = strdup(access->member_name);
            lit->args1_type = DATA_IDENTIFIER;
            lit->data_type = access->member_type;
            return lit;
        }
        ASTNode* real_object = access->object;
        bool obj_was_deref = false;
        if (real_object && real_object->type == AST_DEREF) {
            real_object = ((AST_Deref*)real_object)->operand;
            obj_was_deref = true;
        }
        IR* obj_chain = generate_IR(real_object);
        IR* obj_last = obj_chain;
        while (obj_last->next) obj_last = obj_last->next;
        if (obj_was_deref) {
            /* (*p).field: 先取出 p 里存的指针作为对象基址 */
            IR* deref_obj = create_IR(IR_DEREF);
            deref_obj->result = create_temp();
            assign_def_id(deref_obj);
            deref_obj->args1 = strdup(obj_last->result);
            deref_obj->args1_type = DATA_INT;
            deref_obj->data_type = DATA_INT;
            deref_obj->ptr_depth = 0;
            connect_IR(obj_chain, deref_obj);
            obj_last = deref_obj;
        }

        if (access->is_field_array) {
            IR* off_lit = create_IR(IR_LITERAL);
            off_lit->result = create_temp();
            assign_def_id(off_lit);
            char off_str[16];
            sprintf(off_str, "%d", access->member_offset);
            off_lit->args1 = strdup(off_str);
            off_lit->args1_type = DATA_INT;
            off_lit->data_type = DATA_INT;

            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(obj_last->result);
            add_ir->args1_type = DATA_INT;
            add_ir->args2 = strdup(off_lit->result);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;

            IR* chain = connect_IR(obj_chain, off_lit);
            chain = connect_IR(chain, add_ir);

            if (access->member_ptr_depth > 0) {
                IR* deref = create_IR(IR_DEREF);
                deref->result = create_temp();
                assign_def_id(deref);
                deref->args1 = strdup(add_ir->result);
                deref->args1_type = DATA_INT;
                deref->data_type = DATA_INT;
                deref->ptr_depth = 0;
                chain = connect_IR(chain, deref);
            }
            return chain;
        }

        IR* load = create_IR(IR_LOAD_MEMBER);
        load->result = create_temp();
        assign_def_id(load);
        load->args1 = strdup(obj_last->result);
        char off_str[16];
        sprintf(off_str, "%d", access->member_offset);
        load->args2 = strdup(off_str);
        load->data_type = access->member_type;
        return connect_IR(obj_chain, load);
    }

    case AST_THIS: {
        IR* ir = create_IR(IR_LOAD_THIS);
        ir->result = create_temp();
        assign_def_id(ir);
        ir->data_type = DATA_INT;
        ir->args1_type = DATA_INT;
        return ir;
    }

    case AST_MEMBER_ASS: {
        AST_MemberAss* ass = (AST_MemberAss*)ast_node;

        if (ass->member_offset == -1) {
            IR* val_ir = generate_IR(ass->value);
            IR* val_last = val_ir;
            while (val_last->next) val_last = val_last->next;
            if (ass->member_type == DATA_STRING && val_last->data_type == DATA_STR) {
                IR* to_string = create_IR(IR_STR_TO_STRING);
                to_string->args1 = strdup(val_last->result);
                to_string->args1_type = DATA_STR;
                to_string->data_type = DATA_STRING;
                to_string->result = create_temp();
                assign_def_id(to_string);
                connect_IR(val_ir, to_string);
                val_last = to_string;
            }
            IR* assign_ir = create_IR(IR_VAR_ASS);
            assign_ir->result = strdup(ass->member_name);
            assign_ir->args1 = strdup(val_last->result);
            assign_ir->args1_type = val_last->data_type;
            assign_ir->data_type = ass->member_type;
            return connect_IR(val_ir, assign_ir);
        }

        ASTNode* real_object = ass->object;
        bool ass_was_deref = false;
        if (real_object && real_object->type == AST_DEREF) {
            real_object = ((AST_Deref*)real_object)->operand;
            ass_was_deref = true;
        }
        IR* obj_ir = generate_IR(real_object);
        IR* obj_last = obj_ir;
        while (obj_last->next) obj_last = obj_last->next;
        if (ass_was_deref) {
            IR* deref_ass = create_IR(IR_DEREF);
            deref_ass->result = create_temp();
            assign_def_id(deref_ass);
            deref_ass->args1 = strdup(obj_last->result);
            deref_ass->args1_type = DATA_INT;
            deref_ass->data_type = DATA_INT;
            deref_ass->ptr_depth = 0;
            connect_IR(obj_ir, deref_ass);
            obj_last = deref_ass;
        }
        char* obj_temp = obj_last->result;

        int offset = ass->member_offset;
        char offset_str[16];
        sprintf(offset_str, "%d", offset);

        IR* add_ir = create_IR(IR_BINARY);
        add_ir->op = IR_ADD;
        add_ir->args1 = strdup(obj_temp);
        add_ir->args1_type = DATA_INT;
        add_ir->args2 = strdup(offset_str);
        add_ir->args2_type = DATA_INT;
        add_ir->result = create_temp();
        assign_def_id(add_ir);
        add_ir->data_type = DATA_INT;

        IR* val_ir = generate_IR(ass->value);
        IR* val_last = val_ir;
        while (val_last->next) val_last = val_last->next;
        if (ass->member_type == DATA_STRING && val_last->data_type == DATA_STR) {
            IR* to_string = create_IR(IR_STR_TO_STRING);
            to_string->args1 = strdup(val_last->result);
            to_string->args1_type = DATA_STR;
            to_string->data_type = DATA_STRING;
            to_string->result = create_temp();
            assign_def_id(to_string);
            connect_IR(val_ir, to_string);
            val_last = to_string;
        }
        char* val_temp = val_last->result;

        IR* store = create_IR(IR_ARRAY_STORE);
        store->args1 = strdup(add_ir->result);
        store->args2 = strdup(val_temp);
        store->data_type = ass->member_type;
        store->elem_size = get_type_size(ass->member_type);
        IR* chain = connect_IR(obj_ir, add_ir);
        chain = connect_IR(chain, val_ir);
        chain = connect_IR(chain, store);
        return chain;
    }

    case AST_RETURN:
        AST_Return* ast_ret = (AST_Return*)ast_node;

        if (current_ir_function
            && (current_ir_function->returns_array || current_ir_function->returns_struct)
            && current_ir_ret_ptr != NULL)
        {
            if (ast_ret->value == NULL)
            {
                INTERNAL_ERROR("IR: array-returning function has NULL return value");
                return NULL;
            }
            IR* val_ir = generate_IR(ast_ret->value);
            if (!val_ir) return NULL;
            IR* val_last = val_ir;
            while (val_last->next != NULL)
                val_last = val_last->next;
            char* src_ptr = val_last->result;

            int total_bytes;
            AST_Function* func = current_ir_function;
            if (func->returns_struct) {
                total_bytes = func->return_struct_bytes;
            } else {
                total_bytes = 1;
                for (int i = 0; i < func->return_dim_count; i++) {
                    if (func->return_dim_sizes[i] > 0)
                        total_bytes *= func->return_dim_sizes[i];
                    else
                        total_bytes *= 1;
                }
                total_bytes *= get_type_size(func->return_element_type);
            }

            IR* copy = create_IR(IR_COPY_BLOCK);
            copy->args1 = strdup(current_ir_ret_ptr);
            copy->args1_type = DATA_INT;
            copy->args2 = strdup(src_ptr);
            copy->args2_type = DATA_INT;
            char sz_buf[32];
            snprintf(sz_buf, sizeof(sz_buf), "%d", total_bytes);
            copy->result = strdup(sz_buf);
            copy->data_type = DATA_INT;
            assign_def_id(copy);

            IR* ret_ir = create_IR(IR_RET);
            ret_ir->has_return = false;
            ret_ir->data_type = DATA_VOID;
            ret_ir->ret_has_hidden_ptr = true;

            IR* chain = connect_IR(val_ir, copy);
            chain = connect_IR(chain, ret_ir);
            return chain;
        }

        if (finally_top >= 0 && ast_ret->value != NULL) {
            IR* val_ir = generate_IR(ast_ret->value);
            if (!val_ir) return NULL;
            IR* last = val_ir;
            while (last->next) last = last->next;

            IR* save = create_IR(IR_LITERAL);
            save->result = strdup(finally_stack[finally_top].ret_slot_temp);
            assign_def_id(save);
            save->args1 = strdup(last->result);
            save->args1_type = last->data_type;
            save->data_type = ast_ret->returnType;
            if (save->data_type == DATA_BOOL || save->data_type == DATA_CHAR ||
                save->data_type == DATA_INT8  || save->data_type == DATA_UINT8 ||
                save->data_type == DATA_INT16 || save->data_type == DATA_UINT16) {
                save->data_type = DATA_INT;
            }

            IR* sreason = create_IR(IR_LITERAL);
            sreason->result = strdup(finally_stack[finally_top].reason_temp);
            assign_def_id(sreason);
            sreason->args1 = strdup("1");
            sreason->args1_type = DATA_INT;
            sreason->data_type = DATA_INT;

            IR* jf = create_IR(IR_JMP);
            jf->args1 = strdup(finally_stack[finally_top].finally_label);

            finally_stack[finally_top].has_value = true;
            finally_stack[finally_top].ret_type = ast_ret->returnType;

            IR* chain = connect_IR(val_ir, save);
            chain = connect_IR(chain, sreason);
            chain = connect_IR(chain, ir_exc_frame_pop());
            chain = connect_IR(chain, jf);
            return chain;
        }

        if (finally_top >= 0 && ast_ret->value == NULL) {
            IR* sreason = create_IR(IR_LITERAL);
            sreason->result = strdup(finally_stack[finally_top].reason_temp);
            assign_def_id(sreason);
            sreason->args1 = strdup("1");
            sreason->args1_type = DATA_INT;
            sreason->data_type = DATA_INT;

            IR* jf = create_IR(IR_JMP);
            jf->args1 = strdup(finally_stack[finally_top].finally_label);

            finally_stack[finally_top].has_value = false;

            IR* chain = connect_IR(sreason, ir_exc_frame_pop());
            chain = connect_IR(chain, jf);
            return chain;
        }

        IR* ret_ir = create_IR(IR_RET);
        ret_ir->ret_has_hidden_ptr = false;
        if (current_ir_function
            && (current_ir_function->returns_array || current_ir_function->returns_struct)
            && current_ir_ret_ptr != NULL) {
            ret_ir->ret_has_hidden_ptr = true;
        }
        if (ast_ret->value != NULL) {
            IR* val_ir = generate_IR(ast_ret->value);
            IR* last = val_ir;
            while (last->next) last = last->next;
            ret_ir->args1 = strdup(last->result);
            ret_ir->has_return = true;
            ret_ir->data_type = (current_ir_function
                                 && current_ir_function->return_ptr_depth > 0)
                                ? DATA_INT : ast_ret->returnType;
            return connect_IR(val_ir, ret_ir);
        } else {
            ret_ir->has_return = false;
            ret_ir->data_type = (current_ir_function
                                 && current_ir_function->return_ptr_depth > 0)
                                ? DATA_INT : ast_ret->returnType;
            return ret_ir;
        }

    case AST_MULTI_RETURN: {
        AST_MultiReturn* mr = (AST_MultiReturn*)ast_node;
        if (!current_ir_function) {
            INTERNAL_ERROR("IR: multi-return outside function");
            return NULL;
        }
        if (!current_ir_function->multi_return
            || current_ir_function->multi_return->count != mr->value_count) {
            MultiReturnType* mrt = calloc(1, sizeof(MultiReturnType));
            if (!mrt) OUT_OF_MEMORY();
            mrt->count = mr->value_count;
            mrt->types = malloc(sizeof(DataType) * mr->value_count);
            mrt->is_variadic = malloc(sizeof(bool) * mr->value_count);
            mrt->func_sigs = calloc(mr->value_count, sizeof(FuncSignature*));
            mrt->type_param_indices = calloc(mr->value_count, sizeof(int));
            for (int i = 0; i < mr->value_count; i++) {
                mrt->types[i] = DATA_INT;
                mrt->is_variadic[i] = false;
                mrt->type_param_indices[i] = -1;
            }
            current_ir_function->multi_return = mrt;
        }
        IR* chain = NULL;
        MultiReturnType* mrt = current_ir_function->multi_return;
        int offset = 0;
        for (int i = 0; i < mr->value_count && i < mrt->count; i++) {
            const char* func_addr_name = NULL;
            bool is_closure = false;
            AST_Function* target_nested = NULL;
            bool value_is_func_name = false;

            if (mr->values[i]->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)mr->values[i];
                if (!lit->is_call_function
                    && lit->base.value.type == TOKEN_IDENTIFIER
                    && lit->base.value.value != NULL
                    && lit->is_function_ref) {
                    value_is_func_name = true;
                    const char* val_name = lit->base.value.value;
                    if (current_ir_function && current_ir_function->Code) {
                        ASTNode* nf = current_ir_function->Code;
                        while (nf) {
                            if (nf->type == AST_FUNCTION) {
                                AST_Function* nff = (AST_Function*)nf;
                                if (strcmp(nff->name.value, val_name) == 0
                                    || (nff->internal_name
                                        && strcmp(nff->internal_name, val_name) == 0)) {
                                    target_nested = nff;
                                    func_addr_name = nff->internal_name
                                        ? nff->internal_name : nff->name.value;
                                    is_closure = nff->is_closure;
                                    break;
                                }
                            }
                            nf = nf->next;
                        }
                    }
                    if (!func_addr_name) {
                        func_addr_name = lit->internal_name
                            ? lit->internal_name
                            : val_name;
                    }
                }
            }

            IR* val_chain = NULL;
            IR* val_last = NULL;

            if (!value_is_func_name) {
                val_chain = generate_IR(mr->values[i]);
                if (!val_chain) continue;
                val_last = val_chain;
                while (val_last->next) val_last = val_last->next;
            } else if (is_closure && target_nested && target_nested->upvalue_count > 0) {
                int cl_size = 4 + 4 * target_nested->upvalue_count;
                IR* alloc_ir = create_IR(IR_ALLOC_ARRAY);
                alloc_ir->result = create_temp();
                assign_def_id(alloc_ir);
                char sz_buf[16];
                snprintf(sz_buf, sizeof(sz_buf), "%d", cl_size);
                alloc_ir->args1 = strdup(sz_buf);
                alloc_ir->args1_type = DATA_INT;
                alloc_ir->elem_size = 1;
                alloc_ir->data_type = DATA_INT;
                val_chain = alloc_ir;
                val_last = alloc_ir;

                IR* fn_addr = create_IR(IR_ADDRESS_OF);
                fn_addr->result = create_temp();
                assign_def_id(fn_addr);
                fn_addr->args1 = strdup(func_addr_name);
                fn_addr->args1_type = DATA_INT;
                fn_addr->data_type = DATA_INT;
                fn_addr->is_function_addr = true;
                val_last->next = fn_addr;
                val_last = fn_addr;

                IR* store_fn = create_IR(IR_PTR_STORE);
                store_fn->args1 = strdup(alloc_ir->result);
                store_fn->args1_type = DATA_INT;
                store_fn->args2 = strdup(fn_addr->result);
                store_fn->args2_type = DATA_INT;
                store_fn->data_type = DATA_INT;
                val_last->next = store_fn;
                val_last = store_fn;

                int off = 4;
                for (int u = 0; u < target_nested->upvalue_count; u++) {
                    IR* off_ir = create_IR(IR_BINARY);
                    off_ir->op = IR_ADD;
                    off_ir->args1 = strdup(alloc_ir->result);
                    off_ir->args1_type = DATA_INT;
                    char ostr[16];
                    snprintf(ostr, sizeof(ostr), "%d", off);
                    off_ir->args2 = strdup(ostr);
                    off_ir->args2_type = DATA_INT;
                    off_ir->result = create_temp();
                    assign_def_id(off_ir);
                    off_ir->data_type = DATA_INT;
                    val_last->next = off_ir;
                    val_last = off_ir;

                    IR* uv_ir = create_IR(IR_LITERAL);
                    uv_ir->result = create_temp();
                    assign_def_id(uv_ir);
                    uv_ir->args1 = strdup(
                        target_nested->upvalue_internal_names
                        ? target_nested->upvalue_internal_names[u] : "0");
                    uv_ir->args1_type = target_nested->upvalue_types
                        ? target_nested->upvalue_types[u] : DATA_INT;
                    uv_ir->data_type = uv_ir->args1_type;
                    val_last->next = uv_ir;
                    val_last = uv_ir;

                    IR* st_uv = create_IR(IR_PTR_STORE);
                    st_uv->args1 = strdup(off_ir->result);
                    st_uv->args1_type = DATA_INT;
                    st_uv->args2 = strdup(uv_ir->result);
                    st_uv->args2_type = DATA_INT;
                    st_uv->data_type = DATA_INT;
                    val_last->next = st_uv;
                    val_last = st_uv;
                    off += 4;
                }

                IR* result_ir = create_IR(IR_LITERAL);
                result_ir->result = create_temp();
                assign_def_id(result_ir);
                result_ir->args1 = strdup(alloc_ir->result);
                result_ir->args1_type = DATA_INT;
                result_ir->data_type = DATA_INT;
                val_last->next = result_ir;
                val_last = result_ir;
            } else {
                IR* addr_ir = create_IR(IR_ADDRESS_OF);
                addr_ir->result = create_temp();
                assign_def_id(addr_ir);
                addr_ir->args1 = strdup(func_addr_name);
                addr_ir->args1_type = DATA_INT;
                addr_ir->data_type = DATA_INT;
                addr_ir->is_function_addr = true;
                val_chain = addr_ir;
                val_last = addr_ir;
            }

            chain = connect_IR(chain, val_chain);

            if (i < mrt->count && mrt->is_variadic && mrt->is_variadic[i]) {
                IR* dst_addr = create_IR(IR_BINARY);
                dst_addr->op = IR_ADD;
                dst_addr->args1 = strdup(current_ir_ret_ptr);
                dst_addr->args1_type = DATA_INT;
                char off_str[16];
                sprintf(off_str, "%d", offset);
                dst_addr->args2 = strdup(off_str);
                dst_addr->args2_type = DATA_INT;
                dst_addr->result = create_temp();
                assign_def_id(dst_addr);
                dst_addr->data_type = DATA_INT;
                IR* copy = create_IR(IR_COPY_BLOCK);
                copy->args1 = strdup(dst_addr->result);
                copy->args1_type = DATA_INT;
                copy->args2 = strdup(val_last->result);
                copy->args2_type = DATA_INT;
                char sz[16];
                sprintf(sz, "%d", 8);
                copy->result = strdup(sz);
                copy->data_type = DATA_INT;
                copy->copy_bytes = 8;
                assign_def_id(copy);
                chain = connect_IR(chain, dst_addr);
                chain = connect_IR(chain, copy);
                offset += 8;
            } else {
                IR* dst_addr = create_IR(IR_BINARY);
                dst_addr->op = IR_ADD;
                dst_addr->args1 = strdup(current_ir_ret_ptr);
                dst_addr->args1_type = DATA_INT;
                char off_str[16];
                sprintf(off_str, "%d", offset);
                dst_addr->args2 = strdup(off_str);
                dst_addr->args2_type = DATA_INT;
                dst_addr->result = create_temp();
                assign_def_id(dst_addr);
                dst_addr->data_type = DATA_INT;
                IR* store = create_IR(IR_PTR_STORE);
                store->args1 = strdup(dst_addr->result);
                store->args1_type = DATA_INT;
                store->args2 = strdup(val_last->result);
                store->args2_type = val_last->data_type;
                store->data_type = val_last->data_type;
                chain = connect_IR(chain, dst_addr);
                chain = connect_IR(chain, store);
                int mr_val_sz = get_type_size(val_last->data_type);
                if (mr_val_sz < 4) mr_val_sz = 4;
                offset += mr_val_sz;
            }
        }
        IR* ret_ir = create_IR(IR_RET);
        ret_ir->has_return = false;
        ret_ir->data_type = DATA_VOID;
        ret_ir->ret_has_hidden_ptr = true;
        chain = connect_IR(chain, ret_ir);
        return chain;
    }

    case AST_RETURN_EXTRACT: {
        AST_ReturnExtract* e = (AST_ReturnExtract*)ast_node;

        if (e->is_thread_object && e->mr_type != NULL) {
            IR* obj_chain = generate_IR(e->object);
            IR* obj_last = obj_chain;
            while (obj_last->next) obj_last = obj_last->next;

            const char* obj_internal = NULL;
            if (e->object->type == AST_LITERAL) {
                AST_Literal* ol = (AST_Literal*)e->object;
                obj_internal = ol->internal_name ? ol->internal_name
                                                 : ol->base.value.value;
            } else if (e->object->type == AST_VAR) {
                obj_internal = ((AST_Var*)e->object)->internal_name;
            }

            IR* off_lit = create_IR(IR_LITERAL);
            off_lit->result = create_temp();
            assign_def_id(off_lit);
            char offb[16];
            snprintf(offb, sizeof(offb), "%d", e->offset);
            off_lit->args1 = strdup(offb);
            off_lit->args1_type = DATA_INT;
            off_lit->data_type = DATA_INT;

            IR* push_off = create_IR(IR_PUSH);
            push_off->args1 = strdup(off_lit->result);
            push_off->args1_type = DATA_INT;
            push_off->push_bytes = 4;

            IR* push_obj = create_IR(IR_PUSH);
            push_obj->args1 = strdup(obj_internal ? obj_internal : "0");
            push_obj->args1_type = DATA_INT;
            push_obj->push_bytes = 4;

            const char* method_name;
            DataType result_dt;
            if (e->result_type == DATA_INT64 || e->result_type == DATA_UINT64
                || e->result_type == DATA_FLOAT64 || e->result_type == DATA_UFLOAT64) {
                method_name = "Thread_resultSretAt64_int";
                result_dt = e->result_type;
            } else if (e->result_type == DATA_STR) {
                method_name = "Thread_resultStrAt_int";
                result_dt = DATA_STR;
            } else {
                method_name = "Thread_resultSretAt_int";
                result_dt = e->result_type;
            }

            IR* call_ir = create_IR(IR_CALL);
            call_ir->args1 = strdup(method_name);
            call_ir->result = create_temp();
            assign_def_id(call_ir);
            call_ir->data_type = result_dt;
            call_ir->call_func_args_num = 2;
            call_ir->total_push_bytes = 8;

            IR* chain = connect_IR(obj_chain, off_lit);
            chain = connect_IR(chain, push_off);
            chain = connect_IR(chain, push_obj);
            chain = connect_IR(chain, call_ir);

            if (e->is_variadic_src && e->sub_index >= 0
                    && result_dt != DATA_STR
                    && result_dt != DATA_INT64 && result_dt != DATA_UINT64
                    && result_dt != DATA_FLOAT64 && result_dt != DATA_UFLOAT64) {
                int elem_sz = get_type_size(e->result_type);
                if (elem_sz < 4) elem_sz = 4;

                IR* sub_off = create_IR(IR_LITERAL);
                sub_off->result = create_temp();
                assign_def_id(sub_off);
                char sb[16];
                snprintf(sb, sizeof(sb), "%d", e->sub_index * elem_sz);
                sub_off->args1 = strdup(sb);
                sub_off->args1_type = DATA_INT;
                sub_off->data_type = DATA_INT;
                chain = connect_IR(chain, sub_off);

                IR* add = create_IR(IR_BINARY);
                add->op = IR_ADD;
                add->args1 = strdup(call_ir->result);
                add->args1_type = DATA_INT;
                add->args2 = strdup(sub_off->result);
                add->args2_type = DATA_INT;
                add->result = create_temp();
                assign_def_id(add);
                add->data_type = DATA_INT;
                chain = connect_IR(chain, add);

                IR* elem_load = create_IR(IR_ARRAY_LOAD);
                elem_load->result = create_temp();
                assign_def_id(elem_load);
                elem_load->args1 = strdup(add->result);
                elem_load->args1_type = DATA_INT;
                elem_load->data_type = e->result_type;
                elem_load->elem_size = elem_sz;
                chain = connect_IR(chain, elem_load);
            }

            return chain;
        }

        AST_Literal* call_lit = (AST_Literal*)e->object;
        if (e->total_bytes > 0 && !call_lit->returns_array) {
            call_lit->returns_array = true;
            call_lit->return_total_bytes = e->total_bytes < 4 ? 4 : e->total_bytes;
        }
        IR* call_chain = generate_IR(e->object);
        if (!call_chain) return NULL;
        IR* tail = call_chain;
        while (tail->next) tail = tail->next;
        IR* sret_alloc = NULL;
        {
            IR* scan = call_chain;
            IR* call_node = NULL;
            while (scan) {
                if (scan->type == IR_CALL || scan->type == IR_CALL_INDIRECT)
                    call_node = scan;
                if (scan == tail) break;
                scan = scan->next;
            }
            if (call_node) {
                if (call_node->result) {
                    free(call_node->result);
                    call_node->result = NULL;
                }
                call_node->data_type = DATA_VOID;
                scan = call_chain;
                IR* last_alloc = NULL;
                while (scan && scan != call_node) {
                    if (scan->type == IR_ALLOC_ARRAY
                        && scan->result && scan->result[0])
                        last_alloc = scan;
                    scan = scan->next;
                }
                sret_alloc = last_alloc;
            }
        }
        char* sret_ptr = NULL;
        if (sret_alloc) {
            sret_ptr = sret_alloc->result;
            char off_str[16];
            snprintf(off_str, sizeof(off_str), "%d", e->offset);
            IR* addr = create_IR(IR_BINARY);
            addr->op = IR_ADD;
            addr->args1 = strdup(sret_ptr);
            addr->args1_type = DATA_INT;
            addr->args2 = strdup(off_str);
            addr->args2_type = DATA_INT;
            addr->result = create_temp();
            assign_def_id(addr);
            addr->data_type = DATA_INT;
            IR* load_val = create_IR(IR_ARRAY_LOAD);
            load_val->result = create_temp();
            assign_def_id(load_val);
            load_val->args1 = strdup(addr->result);
            load_val->args1_type = DATA_INT;
            DataType extract_dt = e->result_type;
            if ((extract_dt == DATA_ERROR || extract_dt == DATA_INT)
                && call_lit->multi_return_type != NULL
                && e->index >= 0 && e->index < call_lit->multi_return_type->count) {
                extract_dt = call_lit->multi_return_type->types[e->index];
                if (extract_dt == DATA_ERROR) extract_dt = DATA_INT;
            }
            if ((extract_dt == DATA_ERROR || extract_dt == DATA_INT)
                && call_lit->returned_func_sig != NULL
                && call_lit->returned_func_sig->multi_return != NULL
                && e->index >= 0 && e->index < call_lit->returned_func_sig->multi_return->count) {
                extract_dt = call_lit->returned_func_sig->multi_return->types[e->index];
                if (extract_dt == DATA_ERROR) extract_dt = DATA_INT;
            }
            if (e->is_variadic_src) {
                load_val->data_type = DATA_INT;
                load_val->elem_size = 4;
            } else {
                load_val->data_type = extract_dt;
                int extract_elem_sz = 4;
                if (extract_dt == DATA_FLOAT || extract_dt == DATA_UFLOAT) {
                    extract_elem_sz = 4;
                } else if (extract_dt == DATA_CHAR || extract_dt == DATA_BOOL) {
                    extract_elem_sz = 1;
                }
                load_val->elem_size = extract_elem_sz;
            }
            tail->next = addr;
            if (e->is_variadic_src && e->sub_index >= 0) {
                /* 子索引分支自己从 addr 取指针，这里先不接 load_val */
                tail = addr;
            } else {
                addr->next = load_val;
                tail = load_val;
            }
            if (e->is_variadic_src && e->sub_index >= 0) {
                IR* ptrload = create_IR(IR_ARRAY_LOAD);
                ptrload->result = create_temp();
                assign_def_id(ptrload);
                ptrload->args1 = strdup(addr->result);
                ptrload->args1_type = DATA_INT;
                ptrload->data_type = DATA_INT;
                ptrload->elem_size = 4;
                tail->next = ptrload;
                tail = ptrload;

                int esz2 = 4;
                if (extract_dt == DATA_CHAR || extract_dt == DATA_BOOL
                    || extract_dt == DATA_INT8 || extract_dt == DATA_UINT8) esz2 = 1;
                else if (extract_dt == DATA_INT16 || extract_dt == DATA_UINT16) esz2 = 2;
                char sbuf2[24];
                snprintf(sbuf2, sizeof(sbuf2), "%d", e->sub_index * esz2);
                IR* sidx2 = create_IR(IR_LITERAL);
                sidx2->result = create_temp();
                assign_def_id(sidx2);
                sidx2->args1 = strdup(sbuf2);
                sidx2->args1_type = DATA_INT;
                sidx2->data_type = DATA_INT;
                tail->next = sidx2; tail = sidx2;

                IR* sadd2 = create_IR(IR_BINARY);
                sadd2->op = IR_ADD;
                sadd2->args1 = strdup(ptrload->result);
                sadd2->args1_type = DATA_INT;
                sadd2->args2 = strdup(sidx2->result);
                sadd2->args2_type = DATA_INT;
                sadd2->result = create_temp();
                assign_def_id(sadd2);
                sadd2->data_type = DATA_INT;
                tail->next = sadd2; tail = sadd2;

                IR* sld2 = create_IR(IR_ARRAY_LOAD);
                sld2->result = create_temp();
                assign_def_id(sld2);
                sld2->args1 = strdup(sadd2->result);
                sld2->args1_type = DATA_INT;
                sld2->data_type = extract_dt;
                sld2->elem_size = esz2;
                tail->next = sld2; tail = sld2;
            } else {
                DataType dt = extract_dt;
                if (dt == DATA_INT && call_lit->returned_func_sig != NULL
                    && call_lit->returned_func_sig->multi_return != NULL
                    && e->index >= 0
                    && e->index < call_lit->returned_func_sig->multi_return->count) {
                    dt = call_lit->returned_func_sig->multi_return->types[e->index];
                }
                load_val->data_type = dt;
            }
        }
        IR* pass = create_IR(IR_LITERAL);
        pass->result = create_temp();
        assign_def_id(pass);
        if (tail) {
            pass->args1 = strdup(tail->result);
            pass->args1_type = tail->data_type;
        } else {
            pass->args1 = strdup(tail->result);
            pass->args1_type = tail->data_type;
        }
        pass->data_type = e->result_type;
        if (tail) {
            tail->next = pass;
        } else {
            call_chain = connect_IR(call_chain, pass);
        }
        return call_chain;
    }

    case AST_VARARG_GROUP: {
        AST_VarargGroup* vg = (AST_VarargGroup*)ast_node;
        IR* chain = NULL;
        IR* tail = NULL;

        #define APPEND_VG(ir) do { \
        if (!chain) { chain = tail = (ir); } \
        else { tail->next = (ir); tail = (ir); } \
        } while(0)

        char cnt_str[16];
        snprintf(cnt_str, sizeof(cnt_str), "%d", vg->count);
        IR* alloc_ir = create_IR(IR_ALLOC_ARRAY);
        alloc_ir->result = create_temp();
        assign_def_id(alloc_ir);
        alloc_ir->args1 = strdup(cnt_str);
        alloc_ir->args1_type = DATA_INT;
        alloc_ir->elem_size = 4;
        alloc_ir->data_type = DATA_INT;
        APPEND_VG(alloc_ir);
        char* alloc_temp = alloc_ir->result;
        ASTNode* elem = vg->elements;
        int ei = 0;
        while (elem) {
            IR* elem_ir = generate_IR(elem);
            IR* elem_last = ir_tail(elem_ir);
            APPEND_VG(elem_ir);
            tail = elem_last;
            char ei_str[16];
            snprintf(ei_str, sizeof(ei_str), "%d", ei);
            IR* idx_lit = create_IR(IR_LITERAL);
            idx_lit->result = create_temp();
            assign_def_id(idx_lit);
            idx_lit->args1 = strdup(ei_str);
            idx_lit->args1_type = DATA_INT;
            idx_lit->data_type = DATA_INT;
            APPEND_VG(idx_lit);
            IR* mul_ir = create_IR(IR_BINARY);
            mul_ir->op = IR_MUL;
            mul_ir->args1 = strdup(idx_lit->result);
            mul_ir->args1_type = DATA_INT;
            mul_ir->args2 = strdup("4");
            mul_ir->args2_type = DATA_INT;
            mul_ir->result = create_temp();
            assign_def_id(mul_ir);
            mul_ir->data_type = DATA_INT;
            APPEND_VG(mul_ir);
            IR* add_ir = create_IR(IR_BINARY);
            add_ir->op = IR_ADD;
            add_ir->args1 = strdup(alloc_temp);
            add_ir->args1_type = DATA_INT;
            add_ir->args2 = strdup(mul_ir->result);
            add_ir->args2_type = DATA_INT;
            add_ir->result = create_temp();
            assign_def_id(add_ir);
            add_ir->data_type = DATA_INT;
            APPEND_VG(add_ir);
            if (vg->target_elem_type != DATA_ERROR
                && elem_last->data_type != DATA_ERROR
                && elem_last->data_type != vg->target_elem_type) {
                DataType src_type = elem_last->data_type;
                DataType dst_type = vg->target_elem_type;
                bool src_is_float = (src_type == DATA_FLOAT || src_type == DATA_UFLOAT);
                bool dst_is_int   = (dst_type == DATA_INT  || dst_type == DATA_UINT
                                     || dst_type == DATA_CHAR || dst_type == DATA_BOOL);
                if (src_is_float && dst_is_int) {
                    IR* conv = create_IR(IR_LITERAL);
                    conv->result = create_temp();
                    assign_def_id(conv);
                    conv->args1 = strdup(elem_last->result);
                    conv->args1_type = src_type;
                    conv->data_type = dst_type;
                    APPEND_VG(conv);
                    tail = conv;
                    elem_last = conv;
                }
            }
            IR* store_ir = create_IR(IR_ARRAY_STORE);
            store_ir->args1 = strdup(add_ir->result);
            store_ir->args1_type = DATA_INT;
            store_ir->args2 = strdup(elem_last->result);
            store_ir->args2_type = (elem_last->data_type == DATA_ERROR)
                                   ? DATA_INT : elem_last->data_type;
            store_ir->data_type = (elem_last->data_type == DATA_ERROR)
                                  ? DATA_INT : elem_last->data_type;
            store_ir->elem_size = 4;
            APPEND_VG(store_ir);
            elem = elem->next;
            ei++;
        }
        IR* fat_buf = create_IR(IR_ALLOC_ARRAY);
        fat_buf->result = create_temp();
        assign_def_id(fat_buf);
        fat_buf->args1 = strdup("1");
        fat_buf->args1_type = DATA_INT;
        fat_buf->elem_size = 8;
        fat_buf->stack_bytes = 8;
        fat_buf->data_type = DATA_INT;
        APPEND_VG(fat_buf);
        char* fat_temp = fat_buf->result;
        IR* store_ptr = create_IR(IR_PTR_STORE);
        store_ptr->args1 = strdup(fat_temp);
        store_ptr->args1_type = DATA_INT;
        store_ptr->args2 = strdup(alloc_temp);
        store_ptr->args2_type = DATA_INT;
        store_ptr->data_type = DATA_INT;
        APPEND_VG(store_ptr);
        IR* off4 = create_IR(IR_BINARY);
        off4->op = IR_ADD;
        off4->args1 = strdup(fat_temp);
        off4->args1_type = DATA_INT;
        off4->args2 = strdup("4");
        off4->args2_type = DATA_INT;
        off4->result = create_temp();
        assign_def_id(off4);
        off4->data_type = DATA_INT;
        APPEND_VG(off4);
        IR* len_lit = create_IR(IR_LITERAL);
        len_lit->result = create_temp();
        assign_def_id(len_lit);
        len_lit->args1 = strdup(cnt_str);
        len_lit->args1_type = DATA_INT;
        len_lit->data_type = DATA_INT;
        APPEND_VG(len_lit);
        IR* store_len = create_IR(IR_PTR_STORE);
        store_len->args1 = strdup(off4->result);
        store_len->args1_type = DATA_INT;
        store_len->args2 = strdup(len_lit->result);
        store_len->args2_type = DATA_INT;
        store_len->data_type = DATA_INT;
        APPEND_VG(store_len);
        IR* result = create_IR(IR_LITERAL);
        result->result = create_temp();
        assign_def_id(result);
        result->args1 = strdup(fat_temp);
        result->args1_type = DATA_INT;
        result->data_type = DATA_INT;
        APPEND_VG(result);
    #undef APPEND_VG
        return chain;
    }


    case AST_TRY: {
        AST_Try* tn = (AST_Try*)ast_node;
        IR* chain = NULL;

        char* L_body = create_label();
        char* L_dispatch = create_label();
        char* L_finally = create_label();
        char* L_after = create_label();
        char* L_return = create_label();
        char* L_rethrow = create_label();
        char** L_catch = malloc(sizeof(char*) * (size_t)(tn->catch_count > 0 ? tn->catch_count : 1));
        for (int i = 0; i < tn->catch_count; i++)
            L_catch[i] = create_label();

        for (int i = 0; i < tn->catch_count; i++) {
            if (tn->catches[i].internal_name) {
                IR* decl = create_IR(IR_VAR_UNINIT);
                decl->result = strdup(tn->catches[i].internal_name);
                decl->data_type = DATA_INT;
                decl->stack_bytes = tn->catches[i].total_bytes > 8
                                    ? tn->catches[i].total_bytes : 8;
                chain = connect_IR(chain, decl);
            }
        }

        char* reason = gen_exc_internal_name("reason");
        char* ret_slot = gen_exc_internal_name("retslot");

        IR* rdecl = create_IR(IR_VAR_UNINIT);
        rdecl->result = strdup(reason);
        rdecl->data_type = DATA_INT;
        rdecl->stack_bytes = 4;
        chain = connect_IR(chain, rdecl);

        IR* sdecl = create_IR(IR_VAR_UNINIT);
        sdecl->result = strdup(ret_slot);
        sdecl->data_type = DATA_INT;
        sdecl->stack_bytes = 4;
        chain = connect_IR(chain, sdecl);

        IR* rz = create_IR(IR_LITERAL);
        rz->result = strdup(reason);
        assign_def_id(rz);
        rz->args1 = strdup("0");
        rz->args1_type = DATA_INT;
        rz->data_type = DATA_INT;
        chain = connect_IR(chain, rz);

        IR* frame_alloc = create_IR(IR_ALLOC_ARRAY);
        frame_alloc->result = create_temp();
        assign_def_id(frame_alloc);
        frame_alloc->args1 = strdup("1");
        frame_alloc->args1_type = DATA_INT;
        frame_alloc->elem_size = EXC_FRAME_SIZE;
        frame_alloc->data_type = DATA_INT;
        frame_alloc->stack_bytes = EXC_FRAME_SIZE;
        chain = connect_IR(chain, frame_alloc);
        char* frame_t = frame_alloc->result;

        IR* pf = create_IR(IR_PUSH);
        pf->args1 = strdup(frame_t);
        pf->args1_type = DATA_INT;
        pf->push_bytes = 4;
        chain = connect_IR(chain, pf);

        IR* push_call = create_IR(IR_CALL);
        push_call->args1 = strdup("_exc_frame_push");
        push_call->data_type = DATA_VOID;
        push_call->call_func_args_num = 1;
        push_call->total_push_bytes = 4;
        chain = connect_IR(chain, push_call);

        IR* psj = create_IR(IR_PUSH);
        psj->args1 = strdup(frame_t);
        psj->args1_type = DATA_INT;
        psj->push_bytes = 4;
        chain = connect_IR(chain, psj);

        IR* setjmp_call = create_IR(IR_CALL);
        setjmp_call->args1 = strdup("_exc_setjmp");
        setjmp_call->result = create_temp();
        assign_def_id(setjmp_call);
        setjmp_call->data_type = DATA_INT;
        setjmp_call->call_func_args_num = 1;
        setjmp_call->total_push_bytes = 4;
        chain = connect_IR(chain, setjmp_call);
        char* sj_ret = setjmp_call->result;

        IR* jnz_disp = create_IR(IR_JMPNZ);
        jnz_disp->args1 = strdup(sj_ret);
        jnz_disp->args2 = strdup(L_dispatch);
        chain = connect_IR(chain, jnz_disp);

        IR* lbl_body = create_IR(IR_LABEL);
        lbl_body->args1 = strdup(L_body);
        chain = connect_IR(chain, lbl_body);

        push_finally(L_finally, reason, ret_slot);
        if (tn->code) {
            IR* body_ir = start_generate_IR(tn->code);
            chain = connect_IR(chain, body_ir);
        }

        IR* pop1 = create_IR(IR_CALL);
        pop1->args1 = strdup("_exc_frame_pop");
        pop1->data_type = DATA_VOID;
        pop1->call_func_args_num = 0;
        chain = connect_IR(chain, pop1);

        IR* jmp_fin = create_IR(IR_JMP);
        jmp_fin->args1 = strdup(L_finally);
        chain = connect_IR(chain, jmp_fin);

        IR* lbl_disp = create_IR(IR_LABEL);
        lbl_disp->args1 = strdup(L_dispatch);
        chain = connect_IR(chain, lbl_disp);

        IR* get_cur = create_IR(IR_CALL);
        get_cur->args1 = strdup("_exc_get_current");
        get_cur->result = create_temp();
        assign_def_id(get_cur);
        get_cur->data_type = DATA_INT;
        get_cur->call_func_args_num = 0;
        chain = connect_IR(chain, get_cur);
        char* exc_t = get_cur->result;

        IR* pop2 = create_IR(IR_CALL);
        pop2->args1 = strdup("_exc_frame_pop");
        pop2->data_type = DATA_VOID;
        pop2->call_func_args_num = 0;
        chain = connect_IR(chain, pop2);

        for (int i = 0; i < tn->catch_count; i++) {
            char cid[16];
            snprintf(cid, sizeof(cid), "%d", tn->catches[i].class_id);
            IR* p2 = create_IR(IR_PUSH);
            p2->args1 = strdup(cid);
            p2->args1_type = DATA_INT;
            p2->push_bytes = 4;
            chain = connect_IR(chain, p2);

            IR* p1 = create_IR(IR_PUSH);
            p1->args1 = strdup(exc_t);
            p1->args1_type = DATA_INT;
            p1->push_bytes = 4;
            chain = connect_IR(chain, p1);

            IR* inst = create_IR(IR_CALL);
            inst->args1 = strdup("_error_is_instance");
            inst->result = create_temp();
            assign_def_id(inst);
            inst->data_type = DATA_INT;
            inst->call_func_args_num = 2;
            inst->total_push_bytes = 8;
            chain = connect_IR(chain, inst);

            IR* jnz_c = create_IR(IR_JMPNZ);
            jnz_c->args1 = strdup(inst->result);
            jnz_c->args2 = strdup(L_catch[i]);
            chain = connect_IR(chain, jnz_c);
        }

        IR* sreason2 = create_IR(IR_LITERAL);
        sreason2->result = strdup(reason);
        assign_def_id(sreason2);
        sreason2->args1 = strdup("2");
        sreason2->args1_type = DATA_INT;
        sreason2->data_type = DATA_INT;
        chain = connect_IR(chain, sreason2);

        IR* jmp_fin2 = create_IR(IR_JMP);
        jmp_fin2->args1 = strdup(L_finally);
        chain = connect_IR(chain, jmp_fin2);

        for (int i = 0; i < tn->catch_count; i++) {
            IR* lbl_c = create_IR(IR_LABEL);
            lbl_c->args1 = strdup(L_catch[i]);
            chain = connect_IR(chain, lbl_c);

            IR* dst = create_IR(IR_ADDRESS_OF);
            dst->result = create_temp();
            assign_def_id(dst);
            dst->args1 = strdup(tn->catches[i].internal_name);
            dst->args1_type = DATA_INT;
            dst->data_type = DATA_INT;
            dst->is_function_addr = false;
            chain = connect_IR(chain, dst);

            IR* copy = create_IR(IR_COPY_BLOCK);
            copy->args1 = strdup(dst->result);
            copy->args1_type = DATA_INT;
            copy->args2 = strdup(exc_t);
            copy->args2_type = DATA_INT;
            char szb[16];
            snprintf(szb, sizeof(szb), "%d", tn->catches[i].total_bytes);
            copy->result = strdup(szb);
            copy->data_type = DATA_INT;
            copy->copy_bytes = tn->catches[i].total_bytes;
            assign_def_id(copy);
            chain = connect_IR(chain, copy);

            if (tn->catches[i].body) {
                IR* cbody = start_generate_IR(tn->catches[i].body);
                chain = connect_IR(chain, cbody);
            }

            IR* jf2 = create_IR(IR_JMP);
            jf2->args1 = strdup(L_finally);
            chain = connect_IR(chain, jf2);
        }

        bool has_val = false;
        DataType rt = DATA_INT;
        if (finally_top >= 0) {
            has_val = finally_stack[finally_top].has_value;
            rt = finally_stack[finally_top].ret_type;
        }
        bool has_outer = (finally_top >= 1);
        if (has_outer) {
            finally_stack[finally_top - 1].has_value = has_val;
            finally_stack[finally_top - 1].ret_type = rt;
        }
        char* outer_label = NULL, *outer_reason = NULL, *outer_ret = NULL;
        if (has_outer) {
            outer_label  = strdup(finally_stack[finally_top - 1].finally_label);
            outer_reason = strdup(finally_stack[finally_top - 1].reason_temp);
            outer_ret    = strdup(finally_stack[finally_top - 1].ret_slot_temp);
        }

        char* my_brk = NULL, *my_cnt = NULL;
        if (finally_top >= 0) {
            if (finally_stack[finally_top].break_label)
                my_brk = strdup(finally_stack[finally_top].break_label);
            if (finally_stack[finally_top].continue_label)
                my_cnt = strdup(finally_stack[finally_top].continue_label);
        }
        bool brk_chain = false, cnt_chain = false;
        char* outer_brk_reason = NULL, *outer_brk_finally = NULL;
        char* outer_cnt_reason = NULL, *outer_cnt_finally = NULL;
        if (finally_top >= 1 && my_brk
            && finally_stack[finally_top - 1].break_label
            && strcmp(finally_stack[finally_top - 1].break_label, my_brk) == 0) {
            brk_chain = true;
            outer_brk_reason = strdup(finally_stack[finally_top - 1].reason_temp);
            outer_brk_finally = strdup(finally_stack[finally_top - 1].finally_label);
        }
        if (finally_top >= 1 && my_cnt
            && finally_stack[finally_top - 1].continue_label
            && strcmp(finally_stack[finally_top - 1].continue_label, my_cnt) == 0) {
            cnt_chain = true;
            outer_cnt_reason = strdup(finally_stack[finally_top - 1].reason_temp);
            outer_cnt_finally = strdup(finally_stack[finally_top - 1].finally_label);
        }
        pop_finally();

        IR* lbl_fin = create_IR(IR_LABEL);
        lbl_fin->args1 = strdup(L_finally);
        chain = connect_IR(chain, lbl_fin);

        if (tn->has_finally && tn->finally_code) {
            IR* fin = start_generate_IR(tn->finally_code);
            chain = connect_IR(chain, fin);
        }

        IR* lr = create_IR(IR_LITERAL);
        lr->result = create_temp();
        assign_def_id(lr);
        lr->args1 = strdup(reason);
        lr->args1_type = DATA_INT;
        lr->data_type = DATA_INT;
        chain = connect_IR(chain, lr);

        IR* cmp1 = create_IR(IR_BINARY);
        cmp1->op = IR_JE;
        cmp1->args1 = strdup(lr->result);
        cmp1->args1_type = DATA_INT;
        cmp1->args2 = strdup("1");
        cmp1->args2_type = DATA_INT;
        cmp1->result = create_temp();
        assign_def_id(cmp1);
        cmp1->data_type = DATA_BOOL;
        chain = connect_IR(chain, cmp1);

        IR* j1 = create_IR(IR_JMPNZ);
        j1->args1 = strdup(cmp1->result);
        j1->args2 = strdup(L_return);
        chain = connect_IR(chain, j1);

        IR* cmp2 = create_IR(IR_BINARY);
        cmp2->op = IR_JE;
        cmp2->args1 = strdup(lr->result);
        cmp2->args1_type = DATA_INT;
        cmp2->args2 = strdup("2");
        cmp2->args2_type = DATA_INT;
        cmp2->result = create_temp();
        assign_def_id(cmp2);
        cmp2->data_type = DATA_BOOL;
        chain = connect_IR(chain, cmp2);

        IR* j2 = create_IR(IR_JMPNZ);
        j2->args1 = strdup(cmp2->result);
        j2->args2 = strdup(L_rethrow);
        chain = connect_IR(chain, j2);

        char* L_break = NULL;
        char* L_continue = NULL;
        if (my_brk) {
            L_break = create_label();
            IR* cmp3 = create_IR(IR_BINARY);
            cmp3->op = IR_JE;
            cmp3->args1 = strdup(lr->result);
            cmp3->args1_type = DATA_INT;
            cmp3->args2 = strdup("3");
            cmp3->args2_type = DATA_INT;
            cmp3->result = create_temp();
            assign_def_id(cmp3);
            cmp3->data_type = DATA_BOOL;
            chain = connect_IR(chain, cmp3);
            IR* j3 = create_IR(IR_JMPNZ);
            j3->args1 = strdup(cmp3->result);
            j3->args2 = strdup(L_break);
            chain = connect_IR(chain, j3);
        }
        if (my_cnt) {
            L_continue = create_label();
            IR* cmp4 = create_IR(IR_BINARY);
            cmp4->op = IR_JE;
            cmp4->args1 = strdup(lr->result);
            cmp4->args1_type = DATA_INT;
            cmp4->args2 = strdup("4");
            cmp4->args2_type = DATA_INT;
            cmp4->result = create_temp();
            assign_def_id(cmp4);
            cmp4->data_type = DATA_BOOL;
            chain = connect_IR(chain, cmp4);
            IR* j4 = create_IR(IR_JMPNZ);
            j4->args1 = strdup(cmp4->result);
            j4->args2 = strdup(L_continue);
            chain = connect_IR(chain, j4);
        }

        IR* jmp_after = create_IR(IR_JMP);
        jmp_after->args1 = strdup(L_after);
        chain = connect_IR(chain, jmp_after);

        IR* lbl_ret = create_IR(IR_LABEL);
        lbl_ret->args1 = strdup(L_return);
        chain = connect_IR(chain, lbl_ret);

        if (has_outer) {
            if (has_val) {
                IR* c1 = create_IR(IR_LITERAL);
                c1->result = strdup(outer_ret);
                assign_def_id(c1);
                c1->args1 = strdup(ret_slot);
                c1->args1_type = rt;
                c1->data_type = rt;
                if (c1->data_type == DATA_BOOL || c1->data_type == DATA_CHAR ||
                    c1->data_type == DATA_INT8  || c1->data_type == DATA_UINT8 ||
                    c1->data_type == DATA_INT16 || c1->data_type == DATA_UINT16) {
                    c1->data_type = DATA_INT;
                }
                chain = connect_IR(chain, c1);
            }
            IR* c2 = create_IR(IR_LITERAL);
            c2->result = strdup(outer_reason);
            assign_def_id(c2);
            c2->args1 = strdup("1");
            c2->args1_type = DATA_INT;
            c2->data_type = DATA_INT;
            chain = connect_IR(chain, c2);
            chain = connect_IR(chain, ir_exc_frame_pop());
            IR* c3 = create_IR(IR_JMP);
            c3->args1 = strdup(outer_label);
            chain = connect_IR(chain, c3);
        } else {
            IR* fret = create_IR(IR_RET);
            fret->has_return = has_val;
            fret->data_type = rt;
            if (has_val)
                fret->args1 = strdup(ret_slot);
            chain = connect_IR(chain, fret);
        }

        IR* lbl_rth = create_IR(IR_LABEL);
        lbl_rth->args1 = strdup(L_rethrow);
        chain = connect_IR(chain, lbl_rth);

        IR* rp = create_IR(IR_PUSH);
        rp->args1 = strdup(exc_t);
        rp->args1_type = DATA_INT;
        rp->push_bytes = 4;
        chain = connect_IR(chain, rp);

        IR* rraise = create_IR(IR_CALL);
        rraise->args1 = strdup("_error_raise");
        rraise->data_type = DATA_VOID;
        rraise->call_func_args_num = 1;
        rraise->total_push_bytes = 4;
        chain = connect_IR(chain, rraise);

        if (my_brk) {
            IR* lbl_b = create_IR(IR_LABEL);
            lbl_b->args1 = strdup(L_break);
            chain = connect_IR(chain, lbl_b);
            if (brk_chain) {
                IR* osr = create_IR(IR_LITERAL);
                osr->result = strdup(outer_brk_reason);
                assign_def_id(osr);
                osr->args1 = strdup("3");
                osr->args1_type = DATA_INT;
                osr->data_type = DATA_INT;
                chain = connect_IR(chain, osr);
                chain = connect_IR(chain, ir_exc_frame_pop());
                IR* oj = create_IR(IR_JMP);
                oj->args1 = strdup(outer_brk_finally);
                chain = connect_IR(chain, oj);
            } else {
                IR* bj = create_IR(IR_JMP);
                bj->args1 = strdup(my_brk);
                chain = connect_IR(chain, bj);
            }
        }
        if (my_cnt) {
            IR* lbl_c = create_IR(IR_LABEL);
            lbl_c->args1 = strdup(L_continue);
            chain = connect_IR(chain, lbl_c);
            if (cnt_chain) {
                IR* osr = create_IR(IR_LITERAL);
                osr->result = strdup(outer_cnt_reason);
                assign_def_id(osr);
                osr->args1 = strdup("4");
                osr->args1_type = DATA_INT;
                osr->data_type = DATA_INT;
                chain = connect_IR(chain, osr);
                chain = connect_IR(chain, ir_exc_frame_pop());
                IR* oj = create_IR(IR_JMP);
                oj->args1 = strdup(outer_cnt_finally);
                chain = connect_IR(chain, oj);
            } else {
                IR* cj = create_IR(IR_JMP);
                cj->args1 = strdup(my_cnt);
                chain = connect_IR(chain, cj);
            }
        }

        IR* lbl_after = create_IR(IR_LABEL);
        lbl_after->args1 = strdup(L_after);
        chain = connect_IR(chain, lbl_after);

        free(L_body);
        free(L_dispatch);
        free(L_finally);
        free(L_after);
        free(L_return);
        free(L_rethrow);
        for (int i = 0; i < tn->catch_count; i++) free(L_catch[i]);
        free(L_catch);
        free(reason);
        free(ret_slot);
        free(outer_label);
        free(outer_reason);
        free(outer_ret);
        free(L_break);
        free(L_continue);
        free(my_brk);
        free(my_cnt);
        free(outer_brk_reason);
        free(outer_brk_finally);
        free(outer_cnt_reason);
        free(outer_cnt_finally);
        return chain;
    }
    
    case AST_THROW: {
        AST_Throw* th = (AST_Throw*)ast_node;
        IR* val_ir = generate_IR(th->value);
        if (!val_ir) return NULL;
        IR* val_tail = val_ir;
        while (val_tail->next) val_tail = val_tail->next;

        if (th->value->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)th->value;
            if (!lit->is_call_function && lit->dataType == DATA_IDENTIFIER
                && lit->internal_name) {
                IR* addr = create_IR(IR_ADDRESS_OF);
                addr->result = create_temp();
                assign_def_id(addr);
                addr->args1 = strdup(lit->internal_name);
                addr->args1_type = DATA_INT;
                addr->data_type = DATA_INT;
                addr->is_function_addr = false;
                val_ir = connect_IR(val_ir, addr);
                val_tail = addr;
            }
        }

        IR* push_ir = create_IR(IR_PUSH);
        push_ir->args1 = strdup(val_tail->result);
        push_ir->args1_type = val_tail->data_type;
        push_ir->push_bytes = 4;

        IR* call_ir = create_IR(IR_CALL);
        call_ir->args1 = strdup("_error_raise");
        call_ir->data_type = DATA_VOID;
        call_ir->call_func_args_num = 1;
        call_ir->total_push_bytes = 4;

        IR* chain = connect_IR(val_ir, push_ir);
        chain = connect_IR(chain, call_ir);
        return chain;
    }

    case AST_ENUM_DEF:
    case AST_ENUM_MEMBER:
    case AST_ERROR:
        return NULL;

    default:
        INTERNAL_ERROR("IR: unhandled AST type %d", ast_node->type);
        return NULL;
    }
}

//注：这里为SSA扩展
IR* ir_insert_after(IR* after, IR* new_ir) {
    if (!new_ir) return NULL;
    if (!after) return new_ir;
    new_ir->next = after->next;
    after->next = new_ir;
    return new_ir;
}

IR* ir_create_phi(const char* result_name, int arg_count,
                  char** pred_labels, char** arg_names) {
    IR* ir = calloc(1, sizeof(IR));
    if (!ir) OUT_OF_MEMORY();
    ir->type = IR_PHI;
    ir->result = result_name ? strdup(result_name) : NULL;
    ir->data_type = DATA_INT;
    ir->phi_arg_count = arg_count;

    ir->label_count = arg_count;
    if (arg_count > 0) {
        ir->label = calloc((size_t)arg_count, sizeof(char*));
        for (int i = 0; i < arg_count; i++) {
            if (pred_labels)
                ir->label[i] = strdup(pred_labels[i]);
            else
                ir->label[i] = NULL;
        }
    }

    if (arg_count > 0) {
        ir->phi_arg_names = calloc((size_t)arg_count, sizeof(char*));
        if (arg_names) {
            for (int i = 0; i < arg_count; i++) {
                if (arg_names[i])
                    ir->phi_arg_names[i] = strdup(arg_names[i]);
                else
                    ir->phi_arg_names[i] = NULL;
            }
        }
    }
    return ir;
}

void ir_phi_set_arg(IR* phi, int pred_index, const char* ssa_name) {
    if (!phi || phi->type != IR_PHI) return;
    if (pred_index < 0 || pred_index >= phi->phi_arg_count) return;
    if (phi->phi_arg_names[pred_index])
        free(phi->phi_arg_names[pred_index]);
    phi->phi_arg_names[pred_index] = ssa_name ? strdup(ssa_name) : NULL;
}

char* IR_OP_to_string(IR_OP_Type type)
{
    switch (type)
    {
    case IR_ADD:  return "+";
    case IR_SUB:  return "-";
    case IR_MUL:  return "*";
    case IR_DIV:  return "/";
    case IR_MOD: return "%";
    case IR_JE:   return "==";
    case IR_JNE:   return "!=";
    case IR_JL:   return "<";
    case IR_JG:   return ">";
    case IR_JLE:   return "<=";
    case IR_JGE:   return ">=";
    case IR_UDIV: return "u/";
    case IR_UMOD: return "u%";
    case IR_ULT:  return "u<";
    case IR_ULE:  return "u<=";
    case IR_UGT:  return "u>";
    case IR_UGE:  return "u>=";
    case IR_SHL: return "<<";
    case IR_SHR: return ">>";
    case IR_SAR: return ">>>";
    case IR_BITWISE_XOR: return "^";
    default:      return "unknown";
    }
}

void print_IR(IR* ir) {
    if (ir == NULL) {
        INTERNAL_ERROR("IR: print_IR called with NULL");
    }

    IR* temp = ir;
    int ir_idx = 0;
    while (temp != NULL) {
        printf("IR[%d]: ", ir_idx++);
        switch (temp->type) {
        case IR_LITERAL:
            printf("Literal(type:%d,def:%d): %s = %s\n",temp->data_type,temp->def_id, temp->result, temp->args1);
            break;
        case IR_BINARY:
            printf("Binary(type:%d,def:%d): %s = %s(type:%d) %s %s(type:%d)\n",
                temp->data_type,temp->def_id, temp->result,
                   temp->args1, temp->args1_type,
                   IR_OP_to_string(temp->op), temp->args2,temp->args2_type);
            break;
        case IR_UNARY:
            printf("Unary: op=%d, %s = %s %s\n",
                   temp->op, temp->result, IR_OP_to_string(temp->op), temp->args1);
            break;
        case IR_ARRAY_DECL:
            printf("ArrayDecl(type:%d, size:%d, elem_bytes:%d): %s\n",
                   temp->data_type, temp->array_size, temp->elem_size, temp->result);
            break;
        case IR_ARRAY_LOAD:
            printf("ArrayLoad(type:%d, elem_bytes:%d, def:%d): %s = [%s]\n",
                   temp->data_type, temp->elem_size, temp->def_id, temp->result, temp->args1);
            break;
        case IR_ARRAY_STORE:
            printf("ArrayStore(type:%d, elem_bytes:%d): [%s] = %s\n",
                   temp->data_type, temp->elem_size, temp->args1, temp->args2);
            break;
        case IR_ALLOC_ARRAY:
            printf("AllocArray(type:%d, elem_size:%d, def:%d): %s = new [%s]\n",
                   temp->data_type, temp->elem_size, temp->def_id, temp->result, temp->args1);
            break;
        case IR_ARRAY_LENGTH:
            printf("ArrayLength(def:%d): %s = length(%s)\n", temp->def_id, temp->result, temp->args1);
            break;
        case IR_ARRAY_TOSTRING:
            printf("ArrayToString(def:%d): %s = array_to_string(%s, %s)\n", temp->def_id, temp->result, temp->args1, temp->args2);
            break;
        case IR_PRINT:
            printf("Print parameter: %s\n",temp->args1);
            break;
        case IR_FORMAT:
            printf("Format(def:%d): %s\n", temp->def_id, temp->result);
            break;
        case IR_INPUT:
            printf("Input: %s\n",temp->result);
            break;
        case IR_VAR:
            printf("Var(type:%d): %s = %s(type:%d)\n",temp->data_type, temp->result, temp->args1,temp->args1_type);
            break;
        case IR_VAR_ASS:
            printf("Var_ass(type:%d): %s = %s(type:%d)\n",temp->data_type,temp->result , temp->args1,temp->args1_type);
            break;
        case IR_VAR_UNINIT:
            printf("Var_uninit(type:%d): %s\n",temp->data_type,temp->result);
            break;
        case IR_IF:
            printf("If: label_count=%d, labels=[", temp->label_count);
            for (int i = 0; i < temp->label_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", temp->label[i]);
            }
            printf("]\n");
            break;
        case IR_PUSH:
            printf("push(type:%d, bytes:%d): %s\n", temp->data_type, temp->push_bytes, temp->args1);
            break;
        case IR_PARAM_DEF:
            printf("param_def: %s\n",temp->result);
            break;
        case IR_LABEL:
            printf("Label: %s\n", temp->args1);
            break;
        case IR_JMP:
            printf("JMP: %s\n", temp->args1);
            break;
        case IR_JMPZ:
            printf("JMPZ: if %s == 0 goto %s\n", temp->args1, temp->args2);
            break;
        case IR_JMPNZ:
            printf("JMPNZ: if %s != 0 goto %s\n", temp->args1, temp->args2);
            break;
        case IR_CALL:
            printf("Call(def:%d): %s\n", temp->def_id, temp->args1);
            break;
        case IR_CALL_INDIRECT:
            printf("CallIndirect(def:%d): %s = (*%s)()\n",
                   temp->def_id, temp->result ? temp->result : "?", temp->args1);
            break;
        case IR_RET:
            if (temp->has_return)
            {
                printf("Return: %s\n", temp->args1);
            } else
            {
                printf("Return:\n");
            }
            break;
        case IR_BOUNDS_CHECK:
            printf("BoundsCheck: %s < %s\n", temp->args1, temp->args2);
            break;
        case IR_COPY_BLOCK:
            printf("CopyBlock: [%s] ← [%s], %s bytes\n",
                   temp->args1, temp->args2, temp->result);
            break;
        case IR_BITWISE_NOT:
            printf("BitwiseNOT(def:%d): %s = ~%s (type:%d)\n", temp->def_id, temp->result, temp->args1, temp->data_type);
            break;
        case IR_ALLOC_CLASS:
            printf("AllocClass(size:%s, def:%d): %s\n", temp->args1, temp->def_id, temp->result);
            break;
        case IR_LOAD_MEMBER:
            printf("LoadMember(type:%d, offset:%s, def:%d): %s = [%s + %s]\n",
                   temp->data_type, temp->args2, temp->def_id, temp->result, temp->args1, temp->args2);
            break;
        case IR_LOAD_THIS:
            printf("LoadThis(def:%d): %s\n", temp->def_id, temp->result);
            break;

        case IR_STORE_MEMBER:
            printf("StoreMember(offset:%s): [%s + %s] = %s\n",
                   temp->args2, temp->result, temp->args1, temp->args2);
            break;
        case IR_PHI:
            printf("PHI(def:%d): %s = φ(", temp->def_id, temp->result);
            for (int i = 0; i < temp->phi_arg_count; i++) {
                if (i > 0) printf(", ");
                printf("%s:%s",
                       temp->label[i] ? temp->label[i] : "?",
                       temp->phi_arg_names[i] ? temp->phi_arg_names[i] : "?");
            }
            printf(")\n");
            break;
        case IR_STR_FROM_STRING:
            printf("StrFromString(def:%d): %s = str_from_string(%s)\n",
                   temp->def_id, temp->result, temp->args1);
            break;
        case IR_STR_LENGTH:
            printf("StrLength(def:%d): %s = strlen(%s)\n",
                   temp->def_id, temp->result, temp->args1);
            break;
        case IR_ADDRESS_OF:
            printf("AddressOf(def:%d): %s = &%s\n",
                   temp->def_id, temp->result, temp->args1);
            break;
        case IR_PTR_STORE:
            printf("PtrStore(type:%d): [%s] = %s\n",
                   temp->data_type, temp->args1, temp->args2);
            break;
        case IR_STR_FROM_LITERAL:
            printf("StrFromLiteral(def:%d): %s = str_from_literal(%s, len:%s)\n",
                   temp->def_id, temp->result, temp->args1,
                   temp->args2 ? temp->args2 : "?");
            break;
        case IR_STR_INDEX:
            printf("StrIndex(def:%d): %s = %s[%s]\n",
                   temp->def_id, temp->result, temp->args1, temp->args2);
            break;
        case IR_STR_SUBSTR:
            printf("StrSubstr(def:%d): %s = %s[%s..%s]\n",
                   temp->def_id, temp->result, temp->args1,
                   temp->args2, temp->result);
            break;
        case IR_STR_TO_STRING:
            printf("StrToString(def:%d): %s = str_to_string(%s)\n",
                   temp->def_id, temp->result, temp->args1);
            break;
        case IR_DEREF:
            printf("Deref(def:%d): %s = *%s (ptr_depth:%d)\n",
                   temp->def_id, temp->result, temp->args1, temp->ptr_depth);
            break;
        case IR_TRUNCATE:
            printf("Truncate(def:%d): %s = trunc(%s) to type %d\n",
                   temp->def_id, temp->result, temp->args1, temp->data_type);
            break;
        case IR_CAST:
            printf("Cast(def:%d, kind:%d): %s = cast(%s) from %d to %d\n",
                   temp->def_id, temp->cast_kind,
                   temp->result, temp->args1,
                   temp->args1_type, temp->data_type);
            break;
        default:
            fprintf(stderr,"Unknown IR type %d\n",temp->type);
            break;
        }
        temp = temp->next;
    }
}

void free_IR_chain(IR* ir)
{
    IR* current = ir;
    int a = 0;
    while (current != NULL) {
        IR* next = current->next;

        if (current->result)  { free(current->result);  current->result  = NULL; }
        if (current->args1)   { free(current->args1);   current->args1 = NULL; }
        if (current->args2)   { free(current->args2);   current->args2 = NULL; }

        if (current->label) {
            for (int i = 0; i < current->label_count; i++) {
                if (current->label[i])
                    free(current->label[i]);
            }
            free(current->label);
            current->label = NULL;
        }

        if (current->phi_arg_names) {
            for (int i = 0; i < current->phi_arg_count; i++) {
                if (current->phi_arg_names[i])
                    free(current->phi_arg_names[i]);
            }
            free(current->phi_arg_names);
            current->phi_arg_names = NULL;
        }

        if (current->call_func_args_type) {
            free(current->call_func_args_type);
            current->call_func_args_type = NULL;
        }

        if (current->dim_sizes) {
            free(current->dim_sizes);
            current->dim_sizes = NULL;
        }

        if (current->init_values) {
            free(current->init_values);
            current->init_values = NULL;
        }

        if (current->vtable_label) {
            free(current->vtable_label);
            current->vtable_label = NULL;
        }

        if (current->vararg_internal_name_ir) {
            free(current->vararg_internal_name_ir);
            current->vararg_internal_name_ir = NULL;
        }

        if (current->push_byte_sizes) {
            free(current->push_byte_sizes);
            current->push_byte_sizes = NULL;
        }

        if (current->vararg_bytes_array) {
            free(current->vararg_bytes_array);
            current->vararg_bytes_array = NULL;
        }

        if (current->vararg_internal_names_array) {
            for (int i = 0; i < current->vararg_count; i++)
                free(current->vararg_internal_names_array[i]);
            free(current->vararg_internal_names_array);
            current->vararg_internal_names_array = NULL;
        }

        free(current);
        current = next;
    }
}
