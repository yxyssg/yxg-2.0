#include "Semantics.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "Error.h"
#include "parser.h"

static ClassEntry* resolve_generic_parent(const char* name);
static ClassEntry* find_class_generic_base(const char* name);

typedef struct {
    char** names;
    int count;
    int cap;
} NameSet;


typedef struct StaticDecl {
    char* name;
    char* file;
    struct StaticDecl* next;
} StaticDecl;

static const char* semantic_filename = NULL;
static const Token ZERO_TOKEN = {.line = 0, .column = 0};
#define SEMLOC(tok) LOC((tok).file ? (tok).file : semantic_filename, (tok).line, (tok).column)

const char* current_semantic_file = NULL;

static DataType get_expression_type(ASTNode* node);
static void analyze_node(ASTNode* node);
static void resolve_class_members(AST_ClassDef* cd, ClassEntry* cls);
static void analyze_class_bodies(AST_ClassDef* cd, ClassEntry* cls);
static void flatten_init_list_sem(ASTNode* node, ASTNode*** out, int* count, int* capacity);
static bool block_always_returns(ASTNode* stmt_list);
static MultiReturnType* deep_copy_multi_return(const MultiReturnType* src);
static ASTNode* clone_arg_literal(AST_Literal* src);

static FuncEntry* func_table = NULL;
bool main_has_args = false;
static bool in_param_list = false;
static ClassEntry* class_table = NULL;
static ClassEntry* current_class = NULL;
static int lab_count = 0;
static char* current_class_name = NULL;

static TypeAlias* type_aliases = NULL;

static char** g_subst_type_arg_names = NULL;

static AST_Function* current_analyzing_function = NULL;
static int next_class_id = 1;

static char** native_symbols = NULL;
static int native_symbol_count = 0;

/* 2.5.1 FFI：用户用 native 声明的外部 C 函数，需要输出 extern 并参与链接 */
char* g_user_externs[256];
int   g_user_extern_count = 0;

void semantics_register_user_extern(const char* name) {
    if (!name) return;
    for (int i = 0; i < g_user_extern_count; i++)
        if (strcmp(g_user_externs[i], name) == 0) return;
    if (g_user_extern_count >= 256) return;
    g_user_externs[g_user_extern_count++] = strdup(name);
}

int semantics_user_extern_count(void) { return g_user_extern_count; }
const char* semantics_user_extern_at(int i) {
    return (i >= 0 && i < g_user_extern_count) ? g_user_externs[i] : NULL;
}
static int native_symbol_cap = 0;

static StaticDecl* g_static_decls = NULL;

static void make_static_mangled_name(const char* file_path, const char* name,
                                     char* out, int out_size) {
    if (!file_path || !name || !out || out_size <= 0) {
        if (out && out_size > 0) out[0] = '\0';
        return;
    }
    const char* slash1 = strrchr(file_path, '/');
    const char* slash2 = strrchr(file_path, '\\');
    const char* base = (slash1 > slash2) ? slash1 : slash2;
    base = base ? base + 1 : file_path;
    char base_buf[256];
    strncpy(base_buf, base, sizeof(base_buf) - 1);
    base_buf[sizeof(base_buf) - 1] = '\0';
    char* dot = strrchr(base_buf, '.');
    if (dot) *dot = '\0';
    for (char* p = base_buf; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') *p = '_';
    }
    snprintf(out, (size_t)out_size, "%s_%s", base_buf, name);
}

static void register_static_decl(const char* name, const char* file) {
    if (!name || !file) return;
    for (StaticDecl* d = g_static_decls; d; d = d->next) {
        if (strcmp(d->name, name) == 0 && strcmp(d->file, file) == 0) return;
    }
    StaticDecl* d = malloc(sizeof(StaticDecl));
    if (!d) OUT_OF_MEMORY();
    d->name = strdup(name);
    d->file = strdup(file);
    d->next = g_static_decls;
    g_static_decls = d;
}

static bool is_vararg_param(FuncSignature* sig, int pi) {
    if (!sig || !sig->vararg_param_indices) return false;
    for (int vi = 0; vi < sig->vararg_param_count; vi++) {
        if (sig->vararg_param_indices[vi] == pi) return true;
    }
    return false;
}

static bool is_array_param(FuncSignature* sig, int pi) {
    if (!sig || !sig->param_is_array) return false;
    if (pi < 0 || pi >= sig->param_count) return false;
    return sig->param_is_array[pi];
}


void semantics_register_native_symbol(const char* name) {
    if (!name) return;
    for (int i = 0; i < native_symbol_count; i++)
        if (strcmp(native_symbols[i], name) == 0) return;
    if (native_symbol_count >= native_symbol_cap) {
        native_symbol_cap = native_symbol_cap ? native_symbol_cap * 2 : 32;
        native_symbols = realloc(native_symbols,
                                 sizeof(char*) * (size_t)native_symbol_cap);
        if (!native_symbols) OUT_OF_MEMORY();
    }
    native_symbols[native_symbol_count++] = strdup(name);
}

bool semantics_is_native_symbol(const char* name) {
    for (int i = 0; i < native_symbol_count; i++)
        if (strcmp(native_symbols[i], name) == 0) return true;
    return false;
}

static ASTNode* build_error_info_field(void) {
    AST_Var* var = calloc(1, sizeof(AST_Var));
    if (!var) OUT_OF_MEMORY();
    var->base.type = AST_UNINIT_VAR;
    var->base.value = (Token){.value = strdup("info"), .type = TOKEN_IDENTIFIER};
    var->name = (Token){.value = strdup("info"), .type = TOKEN_IDENTIFIER};
    var->type = TOKEN_STRING;
    var->dataType = DATA_STRING;
    var->modifier = 0;
    var->type_param_index = -1;
    return (ASTNode*)var;
}

static ASTNode* build_error_getinfo_func(void) {
    AST_Function* func = calloc(1, sizeof(AST_Function));
    if (!func) OUT_OF_MEMORY();
    func->base.type = AST_FUNCTION;
    func->name = (Token){.value = strdup("getInfo"), .type = TOKEN_IDENTIFIER};
    func->Modifier = 0;
    func->max_parameters = 4;
    func->parameterType = malloc(sizeof(DataType) * 4);
    if (!func->parameterType) OUT_OF_MEMORY();
    func->parameterCount = 0;
    func->return_type = DATA_STRING;

    AST_This* tn = calloc(1, sizeof(AST_This));
    tn->base.type = AST_THIS;

    AST_MemberAccess* ma = calloc(1, sizeof(AST_MemberAccess));
    ma->base.type = AST_MEMBER_ACCESS;
    ma->member_name = strdup("info");
    ma->member_type = DATA_STRING;
    ma->member_offset = 8;
    ma->vtable_index = -1;
    ma->object = (ASTNode*)tn;

    AST_Return* ret = calloc(1, sizeof(AST_Return));
    ret->base.type = AST_RETURN;
    ret->returnType = DATA_STRING;
    ret->value = (ASTNode*)ma;

    func->Code = (ASTNode*)ret;
    func->Return = ret;
    func->return_Count = 1;
    return (ASTNode*)func;
}

static ASTNode* build_error_error_func(void) {
    AST_Function* func = calloc(1, sizeof(AST_Function));
    if (!func) OUT_OF_MEMORY();
    func->base.type = AST_FUNCTION;
    func->name = (Token){.value = strdup("Error"), .type = TOKEN_IDENTIFIER};
    func->Modifier = 0;
    func->max_parameters = 4;
    func->parameterType = malloc(sizeof(DataType) * 4);
    if (!func->parameterType) OUT_OF_MEMORY();
    func->parameterCount = 0;
    func->return_type = DATA_VOID;

    AST_This* tn = calloc(1, sizeof(AST_This));
    tn->base.type = AST_THIS;

    AST_MemberAccess* ma = calloc(1, sizeof(AST_MemberAccess));
    ma->base.type = AST_MEMBER_ACCESS;
    ma->member_name = strdup("info");
    ma->member_type = DATA_STRING;
    ma->member_offset = 8;
    ma->vtable_index = -1;
    ma->object = (ASTNode*)tn;

    AST_Print* prt = calloc(1, sizeof(AST_Print));
    prt->base.type = AST_PRINT;
    prt->Parameter = (ASTNode*)ma;

    func->Code = (ASTNode*)prt;
    return (ASTNode*)func;
}

static ASTNode* build_error_class_ast(void) {
    AST_ClassDef* cd = calloc(1, sizeof(AST_ClassDef));
    if (!cd) OUT_OF_MEMORY();
    cd->base.type = AST_CLASS_DEF;
    cd->name = strdup("ERROR");
    cd->modifier = 0;
    cd->parent_count = 0;
    cd->is_generic = false;

    ASTNode* info = build_error_info_field();
    ASTNode* getinfo = build_error_getinfo_func();
    ASTNode* err = build_error_error_func();
    info->next = getinfo;
    getinfo->next = err;
    cd->members = info;
    return (ASTNode*)cd;
}

static ASTNode* build_assert_error_class_ast(void) {
    AST_ClassDef* cd = calloc(1, sizeof(AST_ClassDef));
    if (!cd) OUT_OF_MEMORY();
    cd->base.type = AST_CLASS_DEF;
    cd->name = strdup("AssertError");
    cd->parent_count = 1;
    cd->parent_names = malloc(sizeof(char*));
    if (!cd->parent_names) OUT_OF_MEMORY();
    cd->parent_names[0] = strdup("ERROR");
    cd->modifier = 0;
    cd->is_generic = false;

    AST_Function* ctor = calloc(1, sizeof(AST_Function));
    if (!ctor) OUT_OF_MEMORY();
    ctor->base.type = AST_FUNCTION;
    ctor->name = (Token){.value = strdup("AssertError"), .type = TOKEN_IDENTIFIER};
    ctor->Modifier = 0;
    ctor->max_parameters = 4;
    ctor->parameterType = malloc(sizeof(DataType) * 4);
    if (!ctor->parameterType) OUT_OF_MEMORY();
    ctor->return_type = DATA_VOID;

    AST_Var* msg_param = calloc(1, sizeof(AST_Var));
    if (!msg_param) OUT_OF_MEMORY();
    msg_param->base.type = AST_VAR;
    msg_param->base.next = NULL;
    msg_param->base.value = (Token){.value = strdup("msg"), .type = TOKEN_IDENTIFIER};
    msg_param->name = (Token){.value = strdup("msg"), .type = TOKEN_IDENTIFIER};
    msg_param->type = TOKEN_STRING;
    msg_param->dataType = DATA_STRING;
    msg_param->internal_name = NULL;
    msg_param->modifier = 0;
    msg_param->type_param_index = -1;
    ctor->Parameter = (ASTNode*)msg_param;
    ctor->parameterCount = 1;
    ctor->parameterType[0] = DATA_STRING;

    AST_This* thisn = calloc(1, sizeof(AST_This));
    if (!thisn) OUT_OF_MEMORY();
    thisn->base.type = AST_THIS;

    AST_Literal* msg_ref = calloc(1, sizeof(AST_Literal));
    if (!msg_ref) OUT_OF_MEMORY();
    msg_ref->base.type = AST_LITERAL;
    msg_ref->base.value = (Token){.value = strdup("msg"), .type = TOKEN_IDENTIFIER};
    msg_ref->dataType = DATA_STRING;
    msg_ref->is_call_function = false;
    msg_ref->internal_name = NULL;

    AST_MemberAss* ma = calloc(1, sizeof(AST_MemberAss));
    if (!ma) OUT_OF_MEMORY();
    ma->base.type = AST_MEMBER_ASS;
    ma->member_name = strdup("info");
    ma->member_type = DATA_STRING;
    ma->member_offset = -1;
    ma->object = (ASTNode*)thisn;
    ma->value = (ASTNode*)msg_ref;

    ctor->Code = (ASTNode*)ma;
    cd->members = (ASTNode*)ctor;
    return (ASTNode*)cd;
}

ASTNode* semantics_prepare_program(ASTNode* ast_node) {
    ASTNode* err_cls = build_error_class_ast();
    ASTNode* assert_cls = build_assert_error_class_ast();
    assert_cls->next = ast_node;
    err_cls->next = assert_cls;
    return err_cls;
}

static FuncSignature* deep_copy_func_sig(const FuncSignature* src) {
    if (!src) return NULL;
    FuncSignature* dst = calloc(1, sizeof(FuncSignature));
    if (!dst) OUT_OF_MEMORY();
    dst->return_type = src->return_type;
    dst->return_type_param_index = src->return_type_param_index;
    dst->return_pointee_const = src->return_pointee_const;
    if (src->return_class_name != NULL) {
        dst->return_class_name = strdup(src->return_class_name);
    }
    dst->is_closure_sig = src->is_closure_sig;
    dst->returns_closure = src->returns_closure;
    dst->is_closure_ptr = src->is_closure_ptr;
    dst->param_count = src->param_count;
    if (src->param_count > 0 && src->param_types) {
        dst->param_types = malloc(sizeof(DataType) * src->param_count);
        memcpy(dst->param_types, src->param_types, sizeof(DataType) * src->param_count);
    }
    if (src->param_count > 0 && src->param_type_param_indices) {
        dst->param_type_param_indices = malloc(sizeof(int) * src->param_count);
        memcpy(dst->param_type_param_indices, src->param_type_param_indices,
               sizeof(int) * src->param_count);
    }
    if (src->param_count > 0 && src->param_pointee_const) {
        dst->param_pointee_const = malloc(sizeof(bool) * src->param_count);
        memcpy(dst->param_pointee_const, src->param_pointee_const,
               sizeof(bool) * src->param_count);
    }
    if (src->param_count > 0 && src->param_is_array) {
        dst->param_is_array = malloc(sizeof(bool) * src->param_count);
        memcpy(dst->param_is_array, src->param_is_array,
               sizeof(bool) * src->param_count);
    }
    if (src->param_count > 0 && src->param_class_names) {
        dst->param_class_names = calloc((size_t)src->param_count, sizeof(char*));
        for (int i = 0; i < src->param_count; i++) {
            if (src->param_class_names[i])
                dst->param_class_names[i] = strdup(src->param_class_names[i]);
        }
    }
    if (src->param_count > 0 && src->param_func_sigs) {
        dst->param_func_sigs = calloc((size_t)src->param_count, sizeof(FuncSignature*));
        for (int i = 0; i < src->param_count; i++) {
            if (src->param_func_sigs[i]) {
                dst->param_func_sigs[i] = deep_copy_func_sig(src->param_func_sigs[i]);
            }
        }
    }
    dst->has_varargs = src->has_varargs;
    dst->vararg_param_count = src->vararg_param_count;
    if (src->vararg_param_count > 0 && src->vararg_param_indices) {
        dst->vararg_param_indices = malloc(sizeof(int) * src->vararg_param_count);
        memcpy(dst->vararg_param_indices, src->vararg_param_indices,
               sizeof(int) * src->vararg_param_count);
    }
    dst->multi_return = deep_copy_multi_return(src->multi_return);
    dst->ret_func_sig = deep_copy_func_sig(src->ret_func_sig);
    return dst;
}

static void substitute_func_sig_type_params(
    FuncSignature* sig,
    TypeParam* tparams, int tp_count,
    DataType* type_args) {
    if (!sig) return;

    if (sig->return_type == DATA_IDENTIFIER && sig->return_type_param_index >= 0
        && sig->return_type_param_index < tp_count) {
        sig->return_type = type_args[sig->return_type_param_index];
    }

    for (int i = 0; i < sig->param_count; i++) {
        if (sig->param_type_param_indices
            && sig->param_type_param_indices[i] >= 0
            && sig->param_type_param_indices[i] < tp_count) {
            sig->param_types[i] = type_args[sig->param_type_param_indices[i]];
        } else if (sig->param_types[i] == DATA_IDENTIFIER
                   && tp_count > 0) {
            sig->param_types[i] = type_args[0];
        }
        if (sig->param_func_sigs && sig->param_func_sigs[i]) {
            substitute_func_sig_type_params(sig->param_func_sigs[i],
                                             tparams, tp_count, type_args);
        }
    }

    if (sig->multi_return) {
        for (int i = 0; i < sig->multi_return->count; i++) {
            if (sig->multi_return->type_param_indices
                && sig->multi_return->type_param_indices[i] >= 0
                && sig->multi_return->type_param_indices[i] < tp_count) {
                sig->multi_return->types[i] =
                    type_args[sig->multi_return->type_param_indices[i]];
            }
            if (sig->multi_return->func_sigs
                && sig->multi_return->func_sigs[i]) {
                substitute_func_sig_type_params(
                    sig->multi_return->func_sigs[i],
                    tparams, tp_count, type_args);
            }
        }
    }

    substitute_func_sig_type_params(sig->ret_func_sig, tparams, tp_count, type_args);
}

static MultiReturnType* deep_copy_multi_return(const MultiReturnType* src) {
    if (!src) return NULL;
    MultiReturnType* dst = calloc(1, sizeof(MultiReturnType));
    if (!dst) OUT_OF_MEMORY();
    dst->count = src->count;
    if (src->count > 0 && src->types) {
        dst->types = malloc(sizeof(DataType) * src->count);
        memcpy(dst->types, src->types, sizeof(DataType) * src->count);
    }
    if (src->count > 0 && src->type_param_indices) {
        dst->type_param_indices = malloc(sizeof(int) * src->count);
        memcpy(dst->type_param_indices, src->type_param_indices,
               sizeof(int) * src->count);
    }
    if (src->count > 0 && src->is_variadic) {
        dst->is_variadic = malloc(sizeof(bool) * src->count);
        memcpy(dst->is_variadic, src->is_variadic, sizeof(bool) * src->count);
    }
    if (src->count > 0) {
        dst->func_sigs = calloc(src->count, sizeof(FuncSignature*));
        for (int i = 0; i < src->count; i++) {
            if (src->func_sigs && src->func_sigs[i]) {
                dst->func_sigs[i] = deep_copy_func_sig(src->func_sigs[i]);
            }
        }
    }
    return dst;
}

static bool stmt_always_returns(ASTNode* stmt) {
    if (!stmt) return false;
    switch (stmt->type) {
    case AST_RETURN:
    case AST_MULTI_RETURN:
    case AST_THROW:
        return true;
    case AST_TRY: {
        AST_Try* tn = (AST_Try*)stmt;
        if (tn->has_finally && block_always_returns(tn->finally_code))
            return true;
        if (tn->catch_count == 0)
            return block_always_returns(tn->code);
        bool all = block_always_returns(tn->code);
        for (int i = 0; i < tn->catch_count; i++)
            if (!block_always_returns(tn->catches[i].body))
                all = false;
        return all;
    }
    case AST_IF: {
        AST_If* ifn = (AST_If*)stmt;
        bool all_paths = true;
        if (!block_always_returns(ifn->code))
            all_paths = false;
        for (int i = 0; i < ifn->elif_Count; i++) {
            if (!block_always_returns(ifn->Elif[i]))
                all_paths = false;
        }
        if (ifn->has_else) {
            if (!block_always_returns(ifn->Else))
                all_paths = false;
        } else {
            all_paths = false;
        }
        return all_paths;
    }
    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)stmt;
        if (!sw->has_default) return false;
        for (int i = 0; i < sw->case_count; i++) {
            if (!block_always_returns(sw->cases[i]->body))
                return false;
        }
        return block_always_returns(sw->default_body);
    }
    case AST_WHILE: {
        AST_While* w = (AST_While*)stmt;
        if (w->Condition != NULL
            && w->Condition->type == AST_LITERAL) {
            AST_Literal* cond = (AST_Literal*)w->Condition;
            if (!cond->is_call_function
                && cond->base.value.type == TOKEN_NUMBER
                && cond->base.value.value != NULL
                && strcmp(cond->base.value.value, "0") != 0) {
                return block_always_returns(w->code);
                }
            }
        return false;
    }
    case AST_FOR:
    case AST_FOREACH:
        return false;
    default:
        return false;
    }
}

static bool block_always_returns(ASTNode* stmt_list) {
    ASTNode* cur = stmt_list;
    while (cur) {
        if (stmt_always_returns(cur))
            return true;
        cur = cur->next;
    }
    return false;
}

static int get_value_type_bytes(Symbol* sym) {
    if (!sym) return 0;
    if (sym->is_value_type && sym->total_bytes > 0) {
        return sym->total_bytes;
    }
    if (sym->ptr_depth == 0 && sym->dim_count == 0) {
        return S_get_type_size(sym->type);
    }
    return 0;
}

static ASTNode* clone_vararg_group_elements(ASTNode* src) {
    ASTNode* head = NULL;
    ASTNode* tail = NULL;
    while (src) {
        ASTNode* copy = clone_arg_literal((AST_Literal*)src);
        if (!head) {
            head = tail = copy;
        } else {
            tail->next = copy;
            tail = copy;
        }
        src = src->next;
    }
    return head;
}

static ASTNode* clone_vararg_group(AST_VarargGroup* src) {
    if (!src) return NULL;
    AST_VarargGroup* dst = calloc(1, sizeof(AST_VarargGroup));
    if (!dst) OUT_OF_MEMORY();
    dst->base.type = AST_VARARG_GROUP;
    dst->base.next = NULL;
    dst->base.value.type = src->base.value.type;
    dst->base.value.line = src->base.value.line;
    dst->base.value.column = src->base.value.column;
    dst->base.value.value = src->base.value.value
                            ? strdup(src->base.value.value) : NULL;
    dst->count = src->count;
    dst->target_elem_type = src->target_elem_type;
    dst->elements = clone_vararg_group_elements(src->elements);
    return (ASTNode*)dst;
}

static ASTNode* clone_address_of(AST_AddressOf* src) {
    if (!src) return NULL;
    AST_AddressOf* dst = calloc(1, sizeof(AST_AddressOf));
    if (!dst) OUT_OF_MEMORY();
    dst->base.type = AST_ADDRESS_OF;
    dst->base.next = NULL;
    dst->base.value.type = src->base.value.type;
    dst->base.value.line = src->base.value.line;
    dst->base.value.column = src->base.value.column;
    dst->base.value.value = src->base.value.value
                            ? strdup(src->base.value.value) : NULL;
    if (src->operand && src->operand->type == AST_LITERAL) {
        dst->operand = clone_arg_literal((AST_Literal*)src->operand);
    }
    dst->resultType = src->resultType;
    dst->ptr_depth = src->ptr_depth;
    dst->level_const_mask = src->level_const_mask;
    dst->const_value_str = src->const_value_str
                           ? strdup(src->const_value_str) : NULL;
    if (src->target_func_sig) {
        dst->target_func_sig = deep_copy_func_sig(src->target_func_sig);
    }
    return (ASTNode*)dst;
}

static bool is_unsafe_alloc_call(ASTNode* node) {
    if (!node || node->type != AST_LITERAL) return false;
    AST_Literal* lit = (AST_Literal*)node;
    if (!lit->is_call_function) return false;
    if (!lit->internal_name) return false;
    return (strcmp(lit->internal_name, "_unsafe_alloc") == 0 ||
            strcmp(lit->internal_name, "_unsafe_address_alloc") == 0);
}

static const char* infer_static_class_name(ASTNode* node) {
    if (!node) return NULL;

    if (node->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) {
            return lit->call_result_class_name;
        }
        Symbol* s = lookup_symbol(lit->base.value.value);
        if (s && s->class_name) return s->class_name;
        return NULL;
    }

    if (node->type == AST_VAR || node->type == AST_VAR_ASS
        || node->type == AST_UNINIT_VAR) {
        AST_Var* v = (AST_Var*)node;
        Symbol* s = lookup_symbol(v->name.value);
        if (s && s->class_name) return s->class_name;
        return NULL;
        }

    if (node->type == AST_THIS) {
        Symbol* s = lookup_symbol("this");
        if (s && s->class_name) return s->class_name;
        return NULL;
    }
    if (node->type == AST_ADDRESS_OF) {
        /* &expr：类名取 operand 的类名（指针到泛型实例的变型检查需要） */
        return infer_static_class_name(((AST_AddressOf*)node)->operand);
    }


    if (node->type == AST_CAST) {
        AST_Cast* c = (AST_Cast*)node;
        return c->target_class_name;
    }

    if (node->type == AST_NEW_INSTANCE) {
        AST_NewInstance* ni = (AST_NewInstance*)node;
        return ni->class_name;
    }

    if (node->type == AST_MEMBER_ACCESS) {
        AST_MemberAccess* ma = (AST_MemberAccess*)node;
        if (ma->member_type != DATA_IDENTIFIER || ma->member_name == NULL)
            return NULL;
        if (!ma->is_method && ma->method_internal != NULL
            && ma->method_internal[0] != '\0') {
            return ma->method_internal;
        }
        const char* obj_cls_name = infer_static_class_name(ma->object);
        ClassEntry* obj_cls = obj_cls_name ? find_class(obj_cls_name) : NULL;
        if (obj_cls == NULL) return NULL;
        for (int i = 0; i < obj_cls->member_count; i++) {
            MemberInfo* mi = obj_cls->members[i];
            if (!mi->is_method
                && strcmp(mi->name, ma->member_name) == 0) {
                return mi->class_name;
            }
        }
        return NULL;
    }

    if (node->type == AST_CONDITIONAL) {
        AST_Conditional* tern = (AST_Conditional*)node;
        const char* t_then = infer_static_class_name(tern->then_expr);
        const char* t_else = infer_static_class_name(tern->else_expr);
        if (t_then == NULL) return t_else;
        if (t_else == NULL) return t_then;
        ClassEntry* c_then = find_class(t_then);
        ClassEntry* c_else = find_class(t_else);
        if (c_then == NULL || c_else == NULL) return NULL;
        if (is_subclass_of(c_then, c_else)) return t_then;
        if (is_subclass_of(c_else, c_then)) return t_else;
        return NULL;
    }

    if (node->type == AST_ARRAY_ACCESS) {
        AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
        if (acc->index_count < acc->full_dim_count) return NULL;
        Symbol* arr_sym = NULL;
        if (acc->array->type == AST_LITERAL) {
            AST_Literal* al = (AST_Literal*)acc->array;
            if (!al->is_call_function)
                arr_sym = lookup_symbol(al->base.value.value);
        } else if (acc->array->type == AST_VAR
                   || acc->array->type == AST_VAR_ASS
                   || acc->array->type == AST_UNINIT_VAR) {
            arr_sym = lookup_symbol(((AST_Var*)acc->array)->name.value);
                   } else if (acc->array->type == AST_MEMBER_ACCESS) {
                       return infer_static_class_name(acc->array);
                   }
        if (arr_sym && arr_sym->class_name) return arr_sym->class_name;
        return NULL;
    }

    if (node->type == AST_DEREF) {
        AST_Deref* d = (AST_Deref*)node;
        Symbol* ptr_sym = NULL;
        if (d->operand->type == AST_LITERAL) {
            AST_Literal* pl = (AST_Literal*)d->operand;
            if (!pl->is_call_function)
                ptr_sym = lookup_symbol(pl->base.value.value);
        } else if (d->operand->type == AST_VAR
                   || d->operand->type == AST_VAR_ASS
                   || d->operand->type == AST_UNINIT_VAR) {
            ptr_sym = lookup_symbol(((AST_Var*)d->operand)->name.value);
                   }
        if (ptr_sym && ptr_sym->ptr_depth > 0 && ptr_sym->class_name)
            return ptr_sym->class_name;
        return NULL;
    }

    return NULL;
}

static void check_const_indices_bounds(AST_ArrayAccess* acc) {
    if (!acc || !acc->full_dim_sizes || acc->full_dim_count <= 0) return;
    for (int i = 0; i < acc->index_count && i < acc->full_dim_count; i++) {
        ASTNode* idxNode = acc->indices[i];
        if (!idxNode || idxNode->type != AST_LITERAL) continue;
        AST_Literal* idxLit = (AST_Literal*)idxNode;
        if (idxLit->base.value.type != TOKEN_NUMBER) continue;
        int idxVal = atoi(idxLit->base.value.value);
        int dimSize = acc->full_dim_sizes[i];
        if (dimSize > 0 && (idxVal < 0 || idxVal >= dimSize)) {
            ERROR(ERR_SEM_INVALID_INDEX,
                  SEMLOC(idxLit->base.value),
                  "array index %d out of bounds (size %d)",
                  idxVal, dimSize);
        }
    }
}

static bool cast_operand_is_unsafe(ASTNode* operand) {
    if (!operand) return false;

    if (operand->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)operand;
        if (lit->is_call_function) return false;
        Symbol* s = lookup_symbol(lit->base.value.value);
        return (s != NULL) && s->is_unsafe;
    }

    if (operand->type == AST_VAR || operand->type == AST_VAR_ASS
        || operand->type == AST_UNINIT_VAR) {
        AST_Var* v = (AST_Var*)operand;
        if (v->is_unsafe) return true;
        Symbol* s = lookup_symbol(v->name.value);
        return (s != NULL) && s->is_unsafe;
        }

    if (operand->type == AST_CAST) {
        AST_Cast* c = (AST_Cast*)operand;
        return cast_operand_is_unsafe(c->operand);
    }

    return false;
}

static void add_type_alias(const char* name, DataType actual_type) {
    for (TypeAlias* a = type_aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            return;
        }
    }
    TypeAlias* alias = malloc(sizeof(TypeAlias));
    alias->name = strdup(name);
    alias->actual_type = actual_type;
    alias->next = type_aliases;
    type_aliases = alias;
}

static DataType resolve_type_alias(const char* name) {
    for (TypeAlias* a = type_aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0)
            return a->actual_type;
    }
    return DATA_ERROR;
}

static char* make_getter_name(const char* field_name) {
    int len = strlen(field_name) + 4;
    char* name = malloc(len + 1);
    if (name) {
        name[0] = 'g'; name[1] = 'e'; name[2] = 't';
        name[3] = toupper((unsigned char)field_name[0]);
        strcpy(name + 4, field_name + 1);
    }
    return name;
}

static char* make_setter_name(const char* field_name) {
    int len = strlen(field_name) + 4;
    char* name = malloc(len + 1);
    if (name) {
        name[0] = 's'; name[1] = 'e'; name[2] = 't';
        name[3] = toupper((unsigned char)field_name[0]);
        strcpy(name + 4, field_name + 1);
    }
    return name;
}

static bool is_subclass_of(ClassEntry* class_entry, ClassEntry* parent_entry) {
    if (!class_entry || !parent_entry) return false;
    if (class_entry == parent_entry) return true;

    static int recursion_depth = 0;
    if (recursion_depth > 100) return false;
    recursion_depth++;

    for (int i = 0; i < class_entry->parent_count; i++) {
        ClassEntry* p = find_class(class_entry->parent_names[i]);
        if (p == NULL) {
            p = find_class_generic_base(class_entry->parent_names[i]);
        }
        if (p && is_subclass_of(p, parent_entry)) {
            recursion_depth--;
            return true;
        }
    }

    recursion_depth--;
    return false;
}

static int get_expr_ptr_depth(ASTNode* node) {
    if (!node) return 0;
    switch (node->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) {
            return lit->ptr_depth;
        }
        Symbol* sym = lookup_symbol(lit->base.value.value);
        if (sym) {
            if (sym->func_sig != NULL) return 1;
            return sym->ptr_depth;
        }
        return lit->ptr_depth;
    }
    case AST_DEREF: {
        AST_Deref* d = (AST_Deref*)node;
        return d->ptr_depth;
    }
    case AST_ADDRESS_OF: {
        AST_AddressOf* a = (AST_AddressOf*)node;
        return a->ptr_depth;
    }
    case AST_NEW_ARRAY:
        return 1;
    case AST_BINARY: {
        AST_Binary* b = (AST_Binary*)node;
        return b->result_ptr_depth;
    }
    case AST_VAR:
    case AST_VAR_ASS: {
        AST_Var* v = (AST_Var*)node;
        Symbol* sym = lookup_symbol(v->name.value);
        if (sym) return sym->ptr_depth;
        return v->ptr_depth;
    }
    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)node;
        return ma->member_ptr_depth;
    }
    case AST_CAST: {
        AST_Cast* cast = (AST_Cast*)node;
        return cast->target_ptr_depth;
    }
    case AST_NULL: {
        AST_Null* n = (AST_Null*)node;
        return n->ptr_depth;
    }
    default:
        return 0;
    }
}

static int get_pointee_size(ASTNode* ptr_expr) {
    if (!ptr_expr) return 4;
    int pd = get_expr_ptr_depth(ptr_expr);
    if (pd <= 0) return 1;
    if (pd == 1) {
        DataType dt = DATA_INT;
        if (ptr_expr->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)ptr_expr;
            Symbol* sym = lookup_symbol(lit->base.value.value);
            if (sym) dt = sym->type;
            else dt = lit->dataType;
        } else if (ptr_expr->type == AST_VAR || ptr_expr->type == AST_VAR_ASS) {
            AST_Var* v = (AST_Var*)ptr_expr;
            Symbol* sym = lookup_symbol(v->name.value);
            if (sym) dt = sym->type;
            else dt = v->dataType;
        } else if (ptr_expr->type == AST_DEREF) {
            AST_Deref* d = (AST_Deref*)ptr_expr;
            dt = d->resultType;
        }
        return S_get_type_size(dt);
    }
    return 4;
}

static AST_Function* create_getter_ast(const char* class_name,
                                        const char* field_name,
                                        DataType field_type,
                                        int field_offset,
                                        const char* field_class_name) {
    AST_Function* func = calloc(1, sizeof(AST_Function));
    func->base.type = AST_FUNCTION;
    func->base.next = NULL;
    func->name.value = strdup(make_getter_name(field_name));
    func->name.type = TOKEN_IDENTIFIER;
    func->Modifier = 0;
    func->max_parameters = 8;
    func->parameterType = malloc(sizeof(DataType) * 8);
    func->parameterCount = 0;
    func->Parameter = NULL;
    func->return_Count = 1;
    func->return_type = field_type;

    if (field_class_name) {
        func->return_class_name = strdup(field_class_name);
    }

    int len = strlen(class_name) + 1 + strlen(func->name.value) + 1;
    char* internal = malloc(len);
    snprintf(internal, len, "%s_%s", class_name, func->name.value);
    func->internal_name = internal;

    AST_Var* this_param = calloc(1, sizeof(AST_Var));
    this_param->base.type = AST_VAR;
    this_param->base.next = NULL;
    this_param->base.value = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
    this_param->type = TOKEN_IDENTIFIER;
    this_param->name = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
    this_param->dataType = DATA_IDENTIFIER;
    this_param->internal_name = strdup("_this");
    this_param->type_param_index = -1;
    func->Parameter = (ASTNode*)this_param;
    func->parameterCount = 1;
    func->parameterType[0] = DATA_IDENTIFIER;

    AST_Return* ret = calloc(1, sizeof(AST_Return));
    ret->base.type = AST_RETURN;
    ret->returnType = field_type;

    AST_MemberAccess* ma = calloc(1, sizeof(AST_MemberAccess));
    ma->base.type = AST_MEMBER_ACCESS;
    ma->member_name = strdup(field_name);
    ma->member_type = field_type;
    ma->member_ptr_depth = 0;
    ma->member_offset = field_offset;
    ma->is_method = false;
    ma->vtable_index = -1;

    AST_This* this_node = calloc(1, sizeof(AST_This));
    this_node->base.type = AST_THIS;
    ma->object = (ASTNode*)this_node;

    ret->value = (ASTNode*)ma;
    func->Return = ret;
    func->Code = NULL;

    return func;
}

static AST_Function* create_setter_ast(const char* class_name,
                                        const char* field_name,
                                        DataType field_type,
                                        int field_offset,
                                        const char* field_class_name) {
    AST_Function* func = calloc(1, sizeof(AST_Function));
    func->base.type = AST_FUNCTION;
    func->base.next = NULL;
    func->name.value = strdup(make_setter_name(field_name));
    func->name.type = TOKEN_IDENTIFIER;
    func->Modifier = 0;
    func->max_parameters = 8;
    func->parameterType = malloc(sizeof(DataType) * 8);
    func->parameterCount = 0;
    func->Parameter = NULL;
    func->return_Count = 1;
    func->return_type = DATA_VOID;

    int len = strlen(class_name) + 1 + strlen(func->name.value) + 1;
    char* internal = malloc(len);
    snprintf(internal, len, "%s_%s", class_name, func->name.value);
    func->internal_name = internal;

    AST_Return* ret = calloc(1, sizeof(AST_Return));
    ret->base.type = AST_RETURN;
    ret->returnType = DATA_VOID;
    ret->value = NULL;
    func->Return = ret;

    AST_Var* this_param = calloc(1, sizeof(AST_Var));
    this_param->base.type = AST_VAR;
    this_param->base.next = NULL;
    this_param->base.value = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
    this_param->type = TOKEN_IDENTIFIER;
    this_param->name = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
    this_param->dataType = DATA_IDENTIFIER;
    this_param->internal_name = strdup("_this");
    this_param->type_param_index = -1;

    AST_Var* param = calloc(1, sizeof(AST_Var));
    param->base.type = AST_VAR;
    param->base.next = NULL;
    param->name.value = strdup("value");
    param->name.type = TOKEN_IDENTIFIER;
    param->type = (field_type == DATA_INT) ? TOKEN_INT :
                  (field_type == DATA_FLOAT) ? TOKEN_FLOAT :
                  (field_type == DATA_STRING) ? TOKEN_STRING :
                  (field_type == DATA_BOOL) ? TOKEN_BOOL :
                  (field_type == DATA_CHAR) ? TOKEN_CHAR : TOKEN_INT;
    param->dataType = field_type;
    if (field_class_name) {
        param->internal_name = strdup(field_class_name);
    } else {
        param->internal_name = NULL;
    }
    param->modifier = -1;

    this_param->base.next = (ASTNode*)param;
    func->Parameter = (ASTNode*)this_param;
    func->parameterCount = 2;
    func->parameterType[0] = DATA_IDENTIFIER;
    func->parameterType[1] = field_type;

    AST_MemberAss* mass = calloc(1, sizeof(AST_MemberAss));
    mass->base.type = AST_MEMBER_ASS;
    mass->member_name = strdup(field_name);
    mass->member_type = field_type;
    mass->member_offset = field_offset;

    AST_This* this_node = calloc(1, sizeof(AST_This));
    this_node->base.type = AST_THIS;
    mass->object = (ASTNode*)this_node;

    AST_Literal* val_lit = calloc(1, sizeof(AST_Literal));
    val_lit->base.type = AST_LITERAL;
    val_lit->base.value.type = TOKEN_IDENTIFIER;
    val_lit->base.value.value = strdup("value");
    val_lit->dataType = field_type;
    mass->value = (ASTNode*)val_lit;

    func->Code = (ASTNode*)mass;

    return func;
}

static bool method_signature_match(AST_Function* a, AST_Function* b) {
    if (!a || !b) return false;
    if (strcmp(a->name.value, b->name.value) != 0) return false;
    int a_params = a->parameterCount;
    int b_params = b->parameterCount;
    if (a_params != b_params) return false;
    for (int i = 0; i < a_params; i++) {
        if (a->parameterType[i] != b->parameterType[i]) return false;
    }
    return true;
}

static int inherit_from_parent(ClassEntry* child, ClassEntry* parent, int offset)
{
    if (!parent) return offset;
    int current_offset = offset;
    for (int i = 0; i < parent->member_count; i++)
    {
        MemberInfo* pmi = parent->members[i];
        if (pmi->is_method) continue;
        bool conflict = false;
        for (int j = 0; j < child->member_count; j++) {
            if (!child->members[j]->is_method &&
                strcmp(child->members[j]->name, pmi->name) == 0) {
                ERROR(ERR_SEM_DUPLICATE_VAR,
                      SEMLOC(ZERO_TOKEN),
                      "field '%s' conflicts with inherited field",
                      pmi->name);
                conflict = true;
                break;
                }
        }
        if (conflict) continue;
        if (child->member_count >= child->member_capacity) {
            child->member_capacity = child->member_capacity ? child->member_capacity * 2 : 8;
            child->members = realloc(child->members,
                                     sizeof(MemberInfo*) * child->member_capacity);
        }
        MemberInfo* mi = calloc(1, sizeof(MemberInfo));
        mi->name = strdup(pmi->name);
        mi->type = pmi->type;
        mi->is_method = false;
        mi->offset = current_offset;
        mi->inherited_from = parent;
        mi->field_modifier = pmi->field_modifier;
        mi->is_unsafe = pmi->is_unsafe;
        mi->ptr_depth = pmi->ptr_depth;
        mi->dim_count = pmi->dim_count;
        if (pmi->dim_count > 0 && pmi->dim_sizes) {
            mi->dim_sizes = malloc(sizeof(int) * (size_t)pmi->dim_count);
            memcpy(mi->dim_sizes, pmi->dim_sizes,
                   sizeof(int) * (size_t)pmi->dim_count);
        }
        mi->is_dynamic_array = pmi->is_dynamic_array;
        mi->field_class_name = pmi->field_class_name
                               ? strdup(pmi->field_class_name) : NULL;
        mi->class_name = pmi->class_name
                               ? strdup(pmi->class_name) : NULL;
        child->members[child->member_count++] = mi;
        if (pmi->is_dynamic_array) current_offset += 8;
        else current_offset += S_get_type_size(pmi->type);
    }
    return current_offset;
}

static bool types_compatible(DataType declared, DataType actual) {
    if (declared == actual) return true;
    if (declared == DATA_STR && actual == DATA_STRING) return true;
    if (declared == DATA_STRING && actual == DATA_STR) return true;
    if (declared == DATA_IDENTIFIER && (actual == DATA_INT || actual == DATA_UINT))
        return true;
    if (actual == DATA_IDENTIFIER && (declared == DATA_INT || declared == DATA_UINT))
        return true;
    if (declared == DATA_IDENTIFIER || actual == DATA_IDENTIFIER) return true;
    bool decl_is_int = (declared == DATA_INT  || declared == DATA_INT8  ||
                        declared == DATA_INT16 || declared == DATA_INT32 ||
                        declared == DATA_INT64 || declared == DATA_INT128 ||
                        declared == DATA_CHAR || declared == DATA_BOOL);
    bool act_is_int  = (actual  == DATA_INT  || actual  == DATA_INT8  ||
                        actual  == DATA_INT16 || actual  == DATA_INT32 ||
                        actual  == DATA_INT64 || actual  == DATA_INT128 ||
                        actual  == DATA_CHAR || actual  == DATA_BOOL);
    if (decl_is_int && act_is_int) return true;
    bool decl_is_uint = (declared == DATA_UINT  || declared == DATA_UINT8  ||
                         declared == DATA_UINT16 || declared == DATA_UINT32 ||
                         declared == DATA_UINT64 || declared == DATA_UINT128);
    bool act_is_uint  = (actual  == DATA_UINT  || actual  == DATA_UINT8  ||
                         actual  == DATA_UINT16 || actual  == DATA_UINT32 ||
                         actual  == DATA_UINT64 || actual  == DATA_UINT128);
    if (decl_is_uint && act_is_uint) return true;
    if ((decl_is_int || decl_is_uint) && (act_is_int || act_is_uint)) return true;
    bool decl_is_float = (declared == DATA_FLOAT  || declared == DATA_FLOAT8  ||
                          declared == DATA_FLOAT16 || declared == DATA_FLOAT32 ||
                          declared == DATA_FLOAT64 || declared == DATA_FLOAT128);
    bool act_is_float  = (actual  == DATA_FLOAT  || actual  == DATA_FLOAT8  ||
                          actual  == DATA_FLOAT16 || actual  == DATA_FLOAT32 ||
                          actual  == DATA_FLOAT64 || actual  == DATA_FLOAT128);
    if (decl_is_float && act_is_float) return true;
    bool decl_is_ufloat = (declared == DATA_UFLOAT  || declared == DATA_UFLOAT8  ||
                           declared == DATA_UFLOAT16 || declared == DATA_UFLOAT32 ||
                           declared == DATA_UFLOAT64 || declared == DATA_UFLOAT128);
    bool act_is_ufloat  = (actual  == DATA_UFLOAT  || actual  == DATA_UFLOAT8  ||
                           actual  == DATA_UFLOAT16 || actual  == DATA_UFLOAT32 ||
                           actual  == DATA_UFLOAT64 || actual  == DATA_UFLOAT128);
    if (decl_is_ufloat && act_is_ufloat) return true;
    if ((decl_is_float || decl_is_ufloat) && (act_is_float || act_is_ufloat)) return true;
    if ((decl_is_int || decl_is_uint) && (act_is_float || act_is_ufloat)) return true;
    if ((decl_is_float || decl_is_ufloat) && (act_is_int || act_is_uint)) return true;
    if ((declared == DATA_STRING && actual == DATA_CHAR) ||
        (declared == DATA_CHAR && actual == DATA_STRING)) return true;
    return false;
}

static bool function_signature_identical(AST_Function* a, AST_Function* b) {
    if (strcmp(a->name.value, b->name.value) != 0) return false;
    if (a->parameterCount != b->parameterCount) return false;
    for (int i = 0; i < a->parameterCount; i++) {
        if (a->parameterType[i] != b->parameterType[i]) return false;
    }
    DataType retA = a->Return ? a->Return->returnType : DATA_VOID;
    DataType retB = b->Return ? b->Return->returnType : DATA_VOID;
    if (retA != retB) return false;
    return true;
}

static bool function_signature_conflict(AST_Function* a, AST_Function* b) {
    if (strcmp(a->name.value, b->name.value) != 0) return false;
    if (a->parameterCount != b->parameterCount) return false;
    for (int i = 0; i < a->parameterCount; i++) {
        if (a->parameterType[i] != b->parameterType[i]) return false;
    }
    DataType retA = a->Return ? a->Return->returnType : DATA_VOID;
    DataType retB = b->Return ? b->Return->returnType : DATA_VOID;
    return retA != retB;
}

static FuncEntry* find_duplicate_function(const char* name, AST_Function* func) {
    if (!name || !func) return NULL;
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (strcmp(e->name, name) != 0) continue;
        if (e->func == func) continue;
        if (e->func->parameterCount != func->parameterCount) continue;

        bool same_params = true;
        for (int i = 0; i < func->parameterCount; i++) {
            if (e->func->parameterType[i] != func->parameterType[i]) {
                same_params = false;
                break;
            }
        }
        if (same_params) return e;
    }
    return NULL;
}

static bool func_has_env_param(AST_Function* f) {
    if (!f || !f->Parameter) return false;
    ASTNode* p = f->Parameter;
    if (p->type != AST_VAR && p->type != AST_UNINIT_VAR
        && p->type != AST_VAR_ASS)
        return false;
    AST_Var* v = (AST_Var*)p;
    return (v->internal_name && strcmp(v->internal_name, "__env") == 0)
        || (v->name.value && strcmp(v->name.value, "__env") == 0);
}

static void add_function_with_params(const char* name, AST_Function* func) {
    FuncEntry* entry = malloc(sizeof(FuncEntry));
    entry->name = strdup(name);
    entry->func = func;
    int skip = func_has_env_param(func) ? 1 : 0;
    if (func->has_varargs) {
        entry->param_count = func->vararg_param_index - skip;
    } else {
        entry->param_count = func->parameterCount - skip;
    }
    if (entry->param_count > 0 && func->parameterType != NULL) {
        entry->param_types = malloc(sizeof(DataType) * entry->param_count);
        for (int i = 0; i < entry->param_count; i++) {
            entry->param_types[i] = func->parameterType[skip + i];
        }
    } else {
        entry->param_types = NULL;
    }
    entry->next = func_table;
    func_table = entry;
}

static bool class_owns_method(ClassEntry* cls, const AST_Function* func) {
    if (!cls || !func) return false;
    for (int i = 0; i < cls->member_count; i++) {
        MemberInfo* mi = cls->members[i];
        if (mi->is_method && mi->func == func) return true;
    }
    return false;
}

static bool func_has_this_param(AST_Function* f) {
    if (!f || !f->Parameter) return false;
    ASTNode* p = f->Parameter;
    if (p->type != AST_VAR && p->type != AST_UNINIT_VAR
        && p->type != AST_VAR_ASS)
        return false;
    AST_Var* v = (AST_Var*)p;
    return (v->name.value && strcmp(v->name.value, "this") == 0)
        || (v->internal_name && strcmp(v->internal_name, "_this") == 0);
}

AST_Function* find_function_by_signature(const char* name, int arg_count, DataType* arg_types) {
    if (name == NULL) return NULL;

    if (current_class != NULL) {
        AST_Function* best_match = NULL;
        int best_score = -1;
        for (FuncEntry* e = func_table; e; e = e->next) {
            if (strcmp(e->name, name) != 0) continue;
            if (!class_owns_method(current_class, e->func)) continue;

            if (e->func->has_varargs) {
                int params = e->func->vararg_param_index;
                if (arg_count >= params) {
                    bool match = true;
                    for (int i = 0; i < params && i < arg_count; i++) {
                        if (!types_compatible(e->param_types[i], arg_types[i])) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        best_match = e->func;
                        break;
                    }
                }
                continue;
            }

            if (e->param_types == NULL) {
                if (e->param_count == 0 && arg_count == 0) {
                    best_match = e->func;
                    break;
                }
                continue;
            }

            if (!func_has_this_param(e->func)) {
                if (e->param_count != arg_count) continue;
                bool match = true;
                int score = 0;
                for (int i = 0; i < arg_count; i++) {
                    if (e->param_types[i] == arg_types[i]) score += 2;
                    else if (types_compatible(e->param_types[i], arg_types[i])) score += 1;
                    else { match = false; break; }
                }
                if (match && score > best_score) {
                    best_score = score;
                    best_match = e->func;
                }
                continue;
            }

            for (int interp = 0; interp < 2; interp++) {
                int off = (interp == 0) ? 0 : 1;
                if (e->param_count - off != arg_count) continue;
                bool match = true;
                int score = 0;
                for (int i = 0; i < arg_count; i++) {
                    DataType pt = e->param_types[i + off];
                    if (pt == arg_types[i]) score += 2;
                    else if (types_compatible(pt, arg_types[i])) score += 1;
                    else { match = false; break; }
                }
                if (match && score > best_score) {
                    best_score = score;
                    best_match = e->func;
                }
            }
        }
        if (best_match != NULL) return best_match;
    }

    AST_Function* best_match = NULL;
    int best_score = -1;
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (strcmp(e->name, name) != 0) continue;

        if (e->func->has_varargs) {
            int params = e->func->vararg_param_index;
            if (arg_count >= params) {
                bool match = true;
                for (int i = 0; i < params && i < arg_count; i++) {
                    if (!types_compatible(e->param_types[i], arg_types[i])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    best_match = e->func;
                    break;
                }
            }
            continue;
        }

        if (e->param_types == NULL) {
            if (e->param_count == 0 && arg_count == 0) {
                best_match = e->func;
                break;
            }
            continue;
        }

        if (func_has_this_param(e->func)) {
            continue;
        }

        if (e->param_count != arg_count) continue;
        bool match = true;
        int score = 0;
        for (int i = 0; i < arg_count; i++) {
            if (e->param_types[i] == arg_types[i]) score += 2;
            else if (types_compatible(e->param_types[i], arg_types[i])) score += 1;
            else { match = false; break; }
        }
        if (match && score > best_score) {
            best_score = score;
            best_match = e->func;
        }
    }

    return best_match;
}

bool function_signature_exists(const char* name, int param_count, DataType* param_types) {
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (strcmp(e->name, name) != 0) continue;
        if (e->param_count != param_count) continue;
        bool match = true;
        for (int i = 0; i < param_count; i++) {
            if (e->param_types[i] != param_types[i]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static char* generate_method_mangled_name(const char* class_name, AST_Function* func) {
    int len = strlen(class_name) + 1 + strlen(func->name.value);
    for (int i = 0; i < func->parameterCount; i++) {
        if (i == 0 && func->parameterType[0] == DATA_IDENTIFIER) continue;
        len += 1;
        const char* tname = data_type_to_string(func->parameterType[i]);
        len += strlen(tname);
    }
    char* mangled = malloc(len + 1);
    if (!mangled) return NULL;
    sprintf(mangled, "%s_%s", class_name, func->name.value);
    char* ptr = mangled + strlen(mangled);
    for (int i = 0; i < func->parameterCount; i++) {
        if (i == 0 && func->parameterType[0] == DATA_IDENTIFIER) continue;
        *ptr++ = '_';
        const char* tname = data_type_to_string(func->parameterType[i]);
        strcpy(ptr, tname);
        ptr += strlen(tname);
    }
    *ptr = '\0';
    return mangled;
}

ClassEntry* find_class(const char* name) {
    for (ClassEntry* e = class_table; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

void add_class(ClassEntry* entry) {
    entry->next = class_table;
    class_table = entry;
}

static void free_function_table(void) {
    FuncEntry* e = func_table;
    while (e) {
        FuncEntry* next = e->next;
        if (e->param_types) free(e->param_types);
        free(e);
        e = next;
    }
    func_table = NULL;
}

Scope* current_scope = NULL;
static int internal_name_counter = 0;

char* generate_internal_name(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "_var%d", internal_name_counter++);
    return strdup(buf);
}

unsigned int hash_function(const char* str, int table_size) {
    if (str == NULL) {
        return 0;
    }
    const unsigned int seed = 131;
    unsigned int hash_value = 0;
    while (*str != '\0') {
        hash_value = hash_value * seed + (unsigned char)*str;
        str++;
    }
    return hash_value % table_size;
}

Symbol* create_symbol(Token value, DataType type, char* key, char* internal_name) {
    Symbol* symbol = calloc(1, sizeof(Symbol));
    if (symbol == NULL) {
        OUT_OF_MEMORY();
    }
    symbol->value = value;
    symbol->key = key ? strdup(key) : NULL;
    symbol->type = type;
    symbol->internal_name = internal_name ? strdup(internal_name) : NULL;
    symbol->next = NULL;
    symbol->dim_count = 0;
    symbol->dim_sizes = NULL;
    return symbol;
}

void resize_hash_table(Symbol_Table* table) {
    if (table->count < table->size) return;

    int new_size = table->size * 2;
    Symbol** new_buckets = calloc(new_size, sizeof(Symbol*));
    if (new_buckets == NULL) {
        OUT_OF_MEMORY();
    }

    for (int i = 0; i < table->size; i++) {
        Symbol* current = table->buckets[i];
        while (current != NULL) {
            Symbol* next = current->next;
            int new_index = hash_function(current->key, new_size);
            current->next = new_buckets[new_index];
            new_buckets[new_index] = current;
            current = next;
        }
    }

    free(table->buckets);
    table->buckets = new_buckets;
    table->size = new_size;
}

void enter_scope(void) {
    Scope* new_scope = malloc(sizeof(Scope));
    if (new_scope == NULL) {
        OUT_OF_MEMORY();
    }

    new_scope->table = malloc(sizeof(Symbol_Table));
    if (new_scope->table == NULL) {
        OUT_OF_MEMORY();
    }

    new_scope->table->size = 16;
    new_scope->table->count = 0;
    new_scope->table->buckets = calloc(new_scope->table->size, sizeof(Symbol*));
    if (new_scope->table->buckets == NULL) {
        OUT_OF_MEMORY();
    }

    new_scope->parent = current_scope;
    new_scope->depth = current_scope ? current_scope->depth + 1 : 0;
    current_scope = new_scope;
}

void exit_scope(void) {
    if (!current_scope) return;

    for (int i = 0; i < current_scope->table->size; i++) {
        Symbol* sym = current_scope->table->buckets[i];
        while (sym) {
            Symbol* next = sym->next;
            free_symbol(sym);
            sym = next;
        }
    }

    free(current_scope->table->buckets);
    free(current_scope->table);

    Scope* parent = current_scope->parent;
    free(current_scope);
    current_scope = parent;
}

Symbol* lookup_symbol(const char* name) {
    if (!name) return NULL;

    for (Scope* scope = current_scope; scope != NULL; scope = scope->parent) {
        int index = hash_function(name, scope->table->size);
        Symbol* sym = scope->table->buckets[index];

        while (sym) {
            if (sym->key && strcmp(sym->key, name) == 0) {
                return sym;
            }
            sym = sym->next;
        }
    }
    return NULL;
}

Symbol* lookup_symbol_with_file(const char* name, const char* file) {
    if (!name) return NULL;
    if (file != NULL) {
        for (StaticDecl* d = g_static_decls; d; d = d->next) {
            if (strcmp(d->name, name) == 0 && strcmp(d->file, file) == 0) {
                char mangled[512];
                make_static_mangled_name(file, name, mangled, sizeof(mangled));
                if (mangled[0] != '\0') {
                    Symbol* s = lookup_symbol(mangled);
                    if (s) return s;
                }
                break;
            }
        }
    }
    return lookup_symbol(name);
}

void insert_symbol(Symbol* sym) {
    if (!current_scope || !sym || !sym->key) return;

    int index = hash_function(sym->key, current_scope->table->size);

    Symbol* cur = current_scope->table->buckets[index];
    while (cur) {
        if (cur->key && strcmp(cur->key, sym->key) == 0) {
            if (cur->is_function && sym->is_function) {
                break;
            }
            ERROR(ERR_SEM_DUPLICATE_VAR,
                  SEMLOC(sym->value),
                  "duplicate variable '%s'",
                  sym->key);
            free_symbol(sym);
            return;
        }
        cur = cur->next;
    }

    sym->next = current_scope->table->buckets[index];
    current_scope->table->buckets[index] = sym;
    current_scope->table->count++;

    resize_hash_table(current_scope->table);
}

static void symbol_copy_generic_args(Symbol* sym, const AST_Var* var) {
    if (!sym || !var || var->generic_type_arg_count <= 0) return;
    sym->generic_type_arg_count = var->generic_type_arg_count;
    sym->generic_type_args = malloc(sizeof(DataType) * (size_t)var->generic_type_arg_count);
    if (!sym->generic_type_args) OUT_OF_MEMORY();
    memcpy(sym->generic_type_args, var->generic_type_args,
           sizeof(DataType) * (size_t)var->generic_type_arg_count);
    sym->generic_type_arg_names = calloc((size_t)var->generic_type_arg_count, sizeof(char*));
    if (!sym->generic_type_arg_names) OUT_OF_MEMORY();
    for (int i = 0; i < var->generic_type_arg_count; i++)
        sym->generic_type_arg_names[i] =
            (var->generic_type_arg_names && var->generic_type_arg_names[i])
            ? strdup(var->generic_type_arg_names[i]) : NULL;
}

bool has_main_function(void) {
    if (!current_scope) return false;

    Scope* global_scope = current_scope;
    while (global_scope->parent != NULL) {
        global_scope = global_scope->parent;
    }

    int index = hash_function("main", global_scope->table->size);
    Symbol* sym = global_scope->table->buckets[index];

    while (sym) {
        if (sym->key && strcmp(sym->key, "main") == 0) {
            return true;
        }
        sym = sym->next;
    }

    return false;
}

static void check_unsigned_assignment(DataType target_type, ASTNode* value,
                                      SourceLoc loc) {
    if (target_type != DATA_UINT && target_type != DATA_UFLOAT) {
        return;
    }
    if (value == NULL) return;

    if (loc.file != NULL
        && (strstr(loc.file, "lib/") != NULL
            || strstr(loc.file, "lib\\") != NULL)) {
        return;
    }

    if (value->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)value;
        if ((lit->base.value.type == TOKEN_NUMBER ||
             lit->base.value.type == TOKEN_FLOAT_LITERAL) &&
            lit->base.value.value != NULL &&
            lit->base.value.value[0] != '-') {
            return;
        }
    }

    DataType expr_type = get_expression_type(value);
    if (value->type == AST_UNARY) {
        AST_Unary* unary = (AST_Unary*)value;
        if (unary->op.type == TOKEN_SUB) {
            ERROR(ERR_SEM_CANNOT_ASSIGN_NEG, loc,
                  "cannot assign negative value to unsigned type '%s'",
                  data_type_to_string(target_type));
            return;
        }
    }

    if (value->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)value;
        if (lit->base.value.type == TOKEN_NUMBER) {
            int val = atoi(lit->base.value.value);
            if (val < 0) {
                ERROR(ERR_SEM_CANNOT_ASSIGN_NEG, loc,
                      "cannot assign negative constant %d to unsigned type '%s'",
                      val, data_type_to_string(target_type));
                return;
            }
        }
        if (lit->base.value.type == TOKEN_FLOAT_LITERAL) {
            double val = atof(lit->base.value.value);
            if (val < 0.0) {
                ERROR(ERR_SEM_CANNOT_ASSIGN_NEG, loc,
                      "cannot assign negative constant %g to unsigned type '%s'",
                      val, data_type_to_string(target_type));
                return;
            }
        }
    }

    if (expr_type == DATA_INT || expr_type == DATA_FLOAT) {
        WARNING(ERR_SEM_CANNOT_ASSIGN_NEG, loc,
                "assigning signed '%s' to unsigned '%s' may lose sign",
                data_type_to_string(expr_type),
                data_type_to_string(target_type));
    }
}

void count_flat(ASTNode* node, int* cnt) {
    if (node->type == AST_INIT_LIST) {
        AST_InitList* l = (AST_InitList*)node;
        for (int i = 0; i < l->count; i++) count_flat(l->elements[i], cnt);
    } else (*cnt)++;
}

static void flatten_init_list_sem(ASTNode* node, ASTNode*** out, int* count, int* capacity) {
    if (node->type == AST_INIT_LIST) {
        AST_InitList* list = (AST_InitList*)node;
        for (int i = 0; i < list->count; i++) {
            flatten_init_list_sem(list->elements[i], out, count, capacity);
        }
    } else {
        if (*count >= *capacity) {
            if (*capacity == 0) *capacity = 16;
            else *capacity *= 2;
            *out = realloc(*out, sizeof(ASTNode*) * (*capacity));
        }
        (*out)[(*count)++] = node;
    }
}

char* data_type_to_string(DataType type) {
    switch (type) {
        case DATA_INT: return "int";
        case DATA_FLOAT: return "float";
        case DATA_UINT: return "uint";
        case DATA_UFLOAT: return "ufloat";
        case DATA_INT8:   return "int8";
        case DATA_INT16:  return "int16";
        case DATA_INT32:  return "int32";
        case DATA_INT64:  return "int64";
        case DATA_INT128: return "int128";
        case DATA_UINT8:   return "uint8";
        case DATA_UINT16:  return "uint16";
        case DATA_UINT32:  return "uint32";
        case DATA_UINT64:  return "uint64";
        case DATA_UINT128: return "uint128";
        case DATA_FLOAT8:   return "float8";
        case DATA_FLOAT16:  return "float16";
        case DATA_FLOAT32:  return "float32";
        case DATA_FLOAT64:  return "float64";
        case DATA_FLOAT128: return "float128";
        case DATA_UFLOAT8:   return "ufloat8";
        case DATA_UFLOAT16:  return "ufloat16";
        case DATA_UFLOAT32:  return "ufloat32";
        case DATA_UFLOAT64:  return "ufloat64";
        case DATA_UFLOAT128: return "ufloat128";
        case DATA_STRING: return "string";
        case DATA_STR: return "str";
        case DATA_BOOL: return "bool";
        case DATA_CHAR: return "char";
        case DATA_IDENTIFIER: return "identifier";
        case DATA_VOID: return "void";
        default: return "unknown";
    }
}

DataType Get_Datatype(TokenType type) {
    switch (type) {
        case TOKEN_NUMBER:
        case TOKEN_INT:
            return DATA_INT;
        case TOKEN_UINT:
            return DATA_UINT;
        case TOKEN_UFLOAT:
            return DATA_UFLOAT;
        case TOKEN_INT8:
            return DATA_INT8;
        case TOKEN_INT16:
            return DATA_INT16;
        case TOKEN_INT32:
            return DATA_INT32;
        case TOKEN_INT64:
            return DATA_INT64;
        case TOKEN_INT128:
            return DATA_INT128;
        case TOKEN_UINT8:
            return DATA_UINT8;
        case TOKEN_UINT16:
            return DATA_UINT16;
        case TOKEN_UINT32:
            return DATA_UINT32;
        case TOKEN_UINT64:
            return DATA_UINT64;
        case TOKEN_UINT128:
            return DATA_UINT128;
        case TOKEN_FLOAT8:
            return DATA_FLOAT8;
        case TOKEN_FLOAT16:
            return DATA_FLOAT16;
        case TOKEN_FLOAT32:
            return DATA_FLOAT32;
        case TOKEN_FLOAT64:
            return DATA_FLOAT64;
        case TOKEN_FLOAT128:
            return DATA_FLOAT128;
        case TOKEN_UFLOAT8:
            return DATA_UFLOAT8;
        case TOKEN_UFLOAT16:
            return DATA_UFLOAT16;
        case TOKEN_UFLOAT32:
            return DATA_UFLOAT32;
        case TOKEN_UFLOAT64:
            return DATA_UFLOAT64;
        case TOKEN_UFLOAT128:
            return DATA_UFLOAT128;
        case TOKEN_STRING_LITERAL:
        case TOKEN_STRING:
            return DATA_STRING;
        case TOKEN_STR:
            return DATA_STR;
        case TOKEN_CHAR_LITERAL:
        case TOKEN_CHAR:
            return DATA_CHAR;
        case TOKEN_BOOL_LITERAL:
        case TOKEN_BOOL:
            return DATA_BOOL;
        case TOKEN_FLOAT_LITERAL:
        case TOKEN_FLOAT:
            return DATA_FLOAT;
        case TOKEN_VOID:
            return DATA_VOID;
        case TOKEN_IDENTIFIER:
            return DATA_IDENTIFIER;
        default:
            return DATA_VOID;
    }
}

static DataType get_expression_type(ASTNode* node) {
    if (!node) return DATA_ERROR;

    switch (node->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)node;
        return lit->dataType;
    }
    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)node;
        return bin->resultType;
    }
    case AST_UNARY: {
        AST_Unary* unary = (AST_Unary*)node;
        DataType operand_type = get_expression_type(unary->right);
        if (unary->op.type == TOKEN_OPPOSE) {
            return DATA_BOOL;
        }
        return operand_type;
    }
    case AST_CAST: {
        AST_Cast* cast = (AST_Cast*)node;
        return cast->target_type;
    }
    case AST_STR_SLICE: {
            return DATA_STR;
    }
    case AST_DEREF: {
        AST_Deref* d = (AST_Deref*)node;
        return d->resultType;
    }
    case AST_ADDRESS_OF: {
        return DATA_INT;
    }
    case AST_STR_FROM_STRING: {
        return DATA_STR;
    }
    case AST_NULL: {
        return DATA_INT;
    }
    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)node;
        return tern->resultType;
    }
    case AST_VAR:
    case AST_VAR_ASS: {
        AST_Var* var = (AST_Var*)node;
        return var->dataType;
    }
    case AST_ARRAY_ACCESS: {
        AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
        return acc->elementType;
    }
    case AST_ARRAY_LENGTH: {
        return DATA_INT;
    }
    case AST_FUNCTION: {
        AST_Function* func = (AST_Function*)node;
        if (func->Return != NULL) {
            return func->Return->returnType;
        }
        return DATA_VOID;
    }
    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)node;
        return ma->member_type;
    }
    case AST_THIS:
    case AST_NEW_INSTANCE:
        return DATA_INT;
    case AST_MEMBER_ASS: {
        AST_MemberAss* mass = (AST_MemberAss*)node;
        return mass->member_type;
    }
    case AST_RETURN: {
        AST_Return* ret = (AST_Return*)node;
        return ret->returnType;
    }
    default:
        return DATA_ERROR;
    }
}

static int type_width(DataType t) {
    switch (t) {
    case DATA_CHAR: case DATA_BOOL: case DATA_INT8:  case DATA_UINT8:
    case DATA_FLOAT8: case DATA_UFLOAT8:   return 8;
    case DATA_INT16:  case DATA_UINT16:
    case DATA_FLOAT16: case DATA_UFLOAT16: return 16;
    case DATA_INT:    case DATA_UINT:
    case DATA_FLOAT:  case DATA_UFLOAT:
    case DATA_INT32:  case DATA_UINT32:
    case DATA_FLOAT32: case DATA_UFLOAT32: return 32;
    case DATA_INT64:  case DATA_UINT64:
    case DATA_FLOAT64: case DATA_UFLOAT64: return 64;
    case DATA_INT128: case DATA_UINT128:
    case DATA_FLOAT128: case DATA_UFLOAT128: return 128;
    default: return 32;
    }
}

static bool is_int_type(DataType t) {
    switch (t) {
    case DATA_INT: case DATA_INT8: case DATA_INT16: case DATA_INT32:
    case DATA_INT64: case DATA_INT128:
    case DATA_CHAR: case DATA_BOOL:
        return true;
    default: return false;
    }
}

static bool is_uint_type(DataType t) {
    switch (t) {
    case DATA_UINT: case DATA_UINT8: case DATA_UINT16:
    case DATA_UINT32: case DATA_UINT64: case DATA_UINT128:
        return true;
    default: return false;
    }
}

static bool is_float_type(DataType t) {
    switch (t) {
    case DATA_FLOAT: case DATA_FLOAT8: case DATA_FLOAT16:
    case DATA_FLOAT32: case DATA_FLOAT64: case DATA_FLOAT128:
        return true;
    default: return false;
    }
}

static bool is_ufloat_type(DataType t) {
    switch (t) {
    case DATA_UFLOAT: case DATA_UFLOAT8: case DATA_UFLOAT16:
    case DATA_UFLOAT32: case DATA_UFLOAT64: case DATA_UFLOAT128:
        return true;
    default: return false;
    }
}

DataType L_A_R_get_Datatype(DataType left_type, DataType right_type, int line, int column) {
    if (left_type == right_type) return left_type;

    if (left_type == DATA_IDENTIFIER && right_type != DATA_ERROR)
        return right_type;
    if (right_type == DATA_IDENTIFIER && left_type != DATA_ERROR)
        return left_type;
    if ((left_type == DATA_STR && right_type == DATA_STR) ||
        (left_type == DATA_STRING && right_type == DATA_STR) ||
        (left_type == DATA_STR && right_type == DATA_STRING))
        return DATA_STRING;
    if ((left_type == DATA_STR && right_type == DATA_CHAR) ||
        (left_type == DATA_CHAR && right_type == DATA_STR))
        return DATA_STRING;
    if (left_type == DATA_STRING && right_type == DATA_CHAR) return DATA_STRING;
    if (left_type == DATA_CHAR && right_type == DATA_STRING) return DATA_STRING;

    if (is_int_type(left_type) && is_int_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8:  return DATA_INT8;
        case 16: return DATA_INT16;
        case 32: return DATA_INT32;
        case 64: return DATA_INT64;
        case 128: return DATA_INT128;
        default: return DATA_INT32;
        }
    }
    if (is_uint_type(left_type) && is_uint_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8:  return DATA_UINT8;
        case 16: return DATA_UINT16;
        case 32: return DATA_UINT32;
        case 64: return DATA_UINT64;
        case 128: return DATA_UINT128;
        default: return DATA_UINT32;
        }
    }
    if ((is_int_type(left_type) && is_uint_type(right_type)) ||
        (is_uint_type(left_type) && is_int_type(right_type))) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8:  return DATA_INT8;
        case 16: return DATA_INT16;
        case 32: return DATA_INT32;
        case 64: return DATA_INT64;
        case 128: return DATA_INT128;
        default: return DATA_INT32;
        }
    }

    if (is_float_type(left_type) && is_float_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8:  return DATA_FLOAT8;
        case 16: return DATA_FLOAT16;
        case 32: return DATA_FLOAT32;
        case 64: return DATA_FLOAT64;
        case 128: return DATA_FLOAT128;
        default: return DATA_FLOAT32;
        }
    }
    if (is_ufloat_type(left_type) && is_ufloat_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8:  return DATA_UFLOAT8;
        case 16: return DATA_UFLOAT16;
        case 32: return DATA_UFLOAT32;
        case 64: return DATA_UFLOAT64;
        case 128: return DATA_UFLOAT128;
        default: return DATA_UFLOAT32;
        }
    }

    if ((is_int_type(left_type) || is_uint_type(left_type)) && is_float_type(right_type)) {
        int w = type_width(right_type);
        switch (w) {
        case 8: return DATA_FLOAT8;
        case 16: return DATA_FLOAT16;
        case 32: return DATA_FLOAT32;
        case 64: return DATA_FLOAT64;
        case 128: return DATA_FLOAT128;
        default: return DATA_FLOAT32;
        }
    }
    if (is_float_type(left_type) && (is_int_type(right_type) || is_uint_type(right_type))) {
        int w = type_width(left_type);
        switch (w) {
        case 8: return DATA_FLOAT8;
        case 16: return DATA_FLOAT16;
        case 32: return DATA_FLOAT32;
        case 64: return DATA_FLOAT64;
        case 128: return DATA_FLOAT128;
        default: return DATA_FLOAT32;
        }
    }

    if ((is_int_type(left_type) || is_uint_type(left_type)) && is_ufloat_type(right_type)) {
        int w = type_width(right_type);
        switch (w) {
        case 8: return DATA_UFLOAT8;
        case 16: return DATA_UFLOAT16;
        case 32: return DATA_UFLOAT32;
        case 64: return DATA_UFLOAT64;
        case 128: return DATA_UFLOAT128;
        default: return DATA_UFLOAT32;
        }
    }
    if (is_ufloat_type(left_type) && (is_int_type(right_type) || is_uint_type(right_type))) {
        int w = type_width(left_type);
        switch (w) {
        case 8: return DATA_UFLOAT8;
        case 16: return DATA_UFLOAT16;
        case 32: return DATA_UFLOAT32;
        case 64: return DATA_UFLOAT64;
        case 128: return DATA_UFLOAT128;
        default: return DATA_UFLOAT32;
        }
    }

    if (is_float_type(left_type) && is_ufloat_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8: return DATA_UFLOAT8;
        case 16: return DATA_UFLOAT16;
        case 32: return DATA_UFLOAT32;
        case 64: return DATA_UFLOAT64;
        case 128: return DATA_UFLOAT128;
        default: return DATA_UFLOAT32;
        }
    }
    if (is_ufloat_type(left_type) && is_float_type(right_type)) {
        int w = type_width(left_type) > type_width(right_type) ? type_width(left_type) : type_width(right_type);
        switch (w) {
        case 8: return DATA_UFLOAT8;
        case 16: return DATA_UFLOAT16;
        case 32: return DATA_UFLOAT32;
        case 64: return DATA_UFLOAT64;
        case 128: return DATA_UFLOAT128;
        default: return DATA_UFLOAT32;
        }
    }

    if (left_type == DATA_STRING && right_type == DATA_CHAR) return DATA_STRING;
    if (left_type == DATA_CHAR && right_type == DATA_STRING) return DATA_STRING;
    if (left_type == DATA_CHAR && right_type == DATA_CHAR) return DATA_STRING;

    ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
          LOC(semantic_filename, line, column),
          "incompatible types: '%s' and '%s'",
          data_type_to_string(left_type), data_type_to_string(right_type));
    return DATA_ERROR;
}

static bool is_unsigned_type(DataType t) {
    switch (t) {
    case DATA_UINT: case DATA_UINT8: case DATA_UINT16:
    case DATA_UINT32: case DATA_UINT64: case DATA_UINT128:
    case DATA_UFLOAT: case DATA_UFLOAT8: case DATA_UFLOAT16:
    case DATA_UFLOAT32: case DATA_UFLOAT64: case DATA_UFLOAT128:
        return true;
    default: return false;
    }
}
static bool expr_is_array_ref(ASTNode* node) {
    if (node == NULL) return false;
    if (node->type == AST_VAR || node->type == AST_VAR_ASS
        || node->type == AST_UNINIT_VAR) {
        AST_Var* v = (AST_Var*)node;
        if (v->dim_count > 0) return true;
        Symbol* s = lookup_symbol(v->name.value);
        return (s != NULL && s->dim_count > 0);
    }
    if (node->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) return false;
        if (lit->is_dynamic_array) return true;
        if (lit->base.value.value == NULL) return false;
        Symbol* s = lookup_symbol(lit->base.value.value);
        return (s != NULL && s->dim_count > 0);
    }
    return false;
}

bool Operation_Check(AST_Binary* binary) {
    if ((binary->op.type == TOKEN_ADD || binary->op.type == TOKEN_SUB)
        && (expr_is_array_ref(binary->left) || expr_is_array_ref(binary->right))) {
        ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(binary->op),
              "array cannot be used with '+' or '-': fixed-size arrays have no append; "
              "declare the needed length up front, or use List<T> from yxg.Collections");
        return false;
    }

    DataType left_type = get_expression_type(binary->left);
    DataType right_type = get_expression_type(binary->right);
    binary->is_unsigned = is_unsigned_type(left_type) || is_unsigned_type(right_type);

    int left_pd  = get_expr_ptr_depth(binary->left);
    int right_pd = get_expr_ptr_depth(binary->right);

    if (binary->op.type == TOKEN_ADD) {
        DataType lt = get_expression_type(binary->left);
        DataType rt = get_expression_type(binary->right);
        if (lt == DATA_STR && rt == DATA_STR) {
            binary->resultType = DATA_STRING;
            binary->result_ptr_depth = 0;
            binary->elem_size = 0;
            return true;
        }
        if (lt == DATA_STRING && rt == DATA_STR) {
            binary->resultType = DATA_STRING;
            binary->result_ptr_depth = 0;
            binary->elem_size = 0;
            return true;
        }
        if (lt == DATA_STR && rt == DATA_STRING) {
            binary->resultType = DATA_STRING;
            binary->result_ptr_depth = 0;
            binary->elem_size = 0;
            return true;
        }
        if (lt == DATA_STR && (rt == DATA_CHAR || rt == DATA_INT)) {
            binary->resultType = DATA_STRING;
            return true;
        }
        if ((lt == DATA_CHAR || lt == DATA_INT) && rt == DATA_STR) {
            binary->resultType = DATA_STRING;
            return true;
        }
    }
    if (binary->op.type == TOKEN_ADD) {
        if (left_pd > 0 && right_pd == 0) {
            binary->result_ptr_depth = left_pd;
            binary->elem_size = get_pointee_size(binary->left);
            binary->resultType = left_type;
            if (!types_compatible(right_type, DATA_INT) && right_type != DATA_UINT) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(binary->op),
                      "pointer arithmetic requires integer operand");
                return false;
            }
            return true;
        }
        else if (right_pd > 0 && left_pd == 0) {
            ASTNode* tmp = binary->left;
            binary->left = binary->right;
            binary->right = tmp;
            return Operation_Check(binary);
        }
    }
    else if (binary->op.type == TOKEN_SUB) {
        if (left_pd > 0 && right_pd == 0) {
            binary->result_ptr_depth = left_pd;
            binary->elem_size = get_pointee_size(binary->left);
            binary->resultType = left_type;
            if (!types_compatible(right_type, DATA_INT) && right_type != DATA_UINT) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(binary->op),
                      "pointer arithmetic requires integer operand");
                return false;
            }
            return true;
        }
        else if (left_pd > 0 && right_pd > 0 && left_pd == right_pd) {
            binary->result_ptr_depth = 0;
            binary->elem_size = get_pointee_size(binary->left);
            binary->resultType = DATA_INT;
            return true;
        }
    }
    else if (binary->op.type == TOKEN_ADD_ASSIGN) {
        if (left_pd > 0) {
            binary->result_ptr_depth = left_pd;
            binary->elem_size = get_pointee_size(binary->left);
            binary->resultType = left_type;
            return true;
        }
    }
    else if (binary->op.type == TOKEN_SUB_ASSIGN) {
        if (left_pd > 0) {
            binary->result_ptr_depth = left_pd;
            binary->elem_size = get_pointee_size(binary->left);
            binary->resultType = left_type;
            return true;
        }
    }

    if (!types_compatible(left_type, right_type) &&
        !types_compatible(right_type, left_type)) {
        ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
              LOC(semantic_filename, binary->base.value.line, binary->base.value.column),
              "incompatible types: '%s' and '%s'",
              data_type_to_string(left_type), data_type_to_string(right_type));
        return false;
    }
    if (binary->op.type == TOKEN_SHIFT_LEFT ||
        binary->op.type == TOKEN_SHIFT_RIGHT) {
        DataType lt = get_expression_type(binary->left);
        DataType rt = get_expression_type(binary->right);
        if (!is_int_type(lt) && !is_uint_type(lt)) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  LOC(semantic_filename, binary->base.value.line, binary->base.value.column),
                  "shift operator requires integer left operand, got '%s'",
                  data_type_to_string(lt));
            return false;
        }
        if (!is_int_type(rt) && !is_uint_type(rt)) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  LOC(semantic_filename, binary->base.value.line, binary->base.value.column),
                  "shift amount must be integer, got '%s'",
                  data_type_to_string(rt));
            return false;
        }
        binary->is_unsigned = is_uint_type(lt);
        binary->resultType  = lt;
        return true;
    }

    if (binary->op.type == TOKEN_BITWISE_AND ||
        binary->op.type == TOKEN_BITWISE_OR  ||
        binary->op.type == TOKEN_BITWISE_XOR) {
        DataType lt = get_expression_type(binary->left);
        DataType rt = get_expression_type(binary->right);
        if ((!is_int_type(lt) && !is_uint_type(lt)) ||
            (!is_int_type(rt) && !is_uint_type(rt))) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  LOC(semantic_filename, binary->base.value.line,
                      binary->base.value.column),
                  "bitwise operator requires integer operands, got '%s' and '%s'",
                  data_type_to_string(lt), data_type_to_string(rt));
            return false;
        }
        binary->is_unsigned = is_uint_type(lt) || is_uint_type(rt);
        return true;
    }
    return true;
}

DataType Binary_get_Datatype(AST_Binary* binary) {
    if (binary->result_ptr_depth > 0 && binary->elem_size > 0) {
        return binary->resultType;
    }

    DataType lt = get_expression_type(binary->left);
    DataType rt = get_expression_type(binary->right);
    if (binary->op.type == TOKEN_ADD) {
        if ((lt == DATA_STR && rt == DATA_STR) ||
            (lt == DATA_STRING && rt == DATA_STR) ||
            (lt == DATA_STR && rt == DATA_STRING) ||
            (lt == DATA_STR && (rt == DATA_CHAR || rt == DATA_INT)) ||
            ((lt == DATA_CHAR || lt == DATA_INT) && rt == DATA_STR)) {
            return DATA_STRING;
            }
    }

    if (binary->op.type == TOKEN_EQ || binary->op.type == TOKEN_NE ||
        binary->op.type == TOKEN_LT || binary->op.type == TOKEN_GT ||
        binary->op.type == TOKEN_LE || binary->op.type == TOKEN_GE) {
        return DATA_BOOL;
    }

    if (binary->op.type == TOKEN_LOGICAL_AND || binary->op.type == TOKEN_LOGICAL_OR)
        return DATA_BOOL;

    if (binary->op.type == TOKEN_BITWISE_AND||
        binary->op.type == TOKEN_BITWISE_OR ||
        binary->op.type == TOKEN_BITWISE_XOR) {
        DataType left_type = get_expression_type(binary->left);
        DataType right_type = get_expression_type(binary->right);
        return L_A_R_get_Datatype(left_type, right_type,
                                  binary->base.value.line, binary->base.value.column);
    }

    if (binary->op.type == TOKEN_SHIFT_LEFT ||
        binary->op.type == TOKEN_SHIFT_RIGHT) {
        return binary->resultType;
    }

    DataType left_type = get_expression_type(binary->left);
    DataType right_type = get_expression_type(binary->right);

    return L_A_R_get_Datatype(left_type, right_type,
        binary->base.value.line, binary->base.value.column);
}

bool check_division_by_zero(AST_Binary* binary) {
    if (binary->op.type != TOKEN_DIV) return true;

    if (binary->right->type == AST_LITERAL) {
        AST_Literal* lit = (AST_Literal*)binary->right;
        if (lit->base.value.type == TOKEN_NUMBER &&
            strcmp(lit->base.value.value, "0") == 0) {
            DataType lt = get_expression_type(binary->left);
            if (is_int_type(lt) || is_uint_type(lt)) {
                ERROR(ERR_SEM_DIV_BY_ZERO,
                      LOC(semantic_filename, binary->base.value.line, binary->base.value.column),
                      "division by zero");
                return false;
            }
        }
    }
    return true;
}

static int S_get_type_size(DataType type) {
    switch (type) {
    case DATA_CHAR:
    case DATA_BOOL:
    case DATA_INT8:
    case DATA_UINT8:
    case DATA_FLOAT8:
    case DATA_UFLOAT8:  return 1;
    case DATA_INT16:
    case DATA_UINT16:
    case DATA_FLOAT16:
    case DATA_UFLOAT16: return 2;
    case DATA_INT:
    case DATA_UINT:
    case DATA_INT32:
    case DATA_UINT32:
    case DATA_FLOAT:
    case DATA_UFLOAT:
    case DATA_FLOAT32:
    case DATA_UFLOAT32:
    case DATA_STRING:   return 4;
    case DATA_INT64:
    case DATA_UINT64:
    case DATA_FLOAT64:
    case DATA_UFLOAT64: return 8;
    case DATA_INT128:
    case DATA_UINT128:
    case DATA_FLOAT128:
    case DATA_UFLOAT128: return 16;
    case DATA_STR:      return 8;
    default:            return 4;
    }
}

static int compute_array_total_bytes(DataType elem_type, int dim_count,
                                     const int* dim_sizes,
                                     int* out_elements) {
    long elements = 1;
    for (int i = 0; i < dim_count; i++) {
        if (dim_sizes[i] <= 0) {
            if (out_elements) *out_elements = 0;
            return -1;
        }
        if (elements > 2147483647L / dim_sizes[i]) {
            if (out_elements) *out_elements = 0;
            return -2;
        }
        elements *= dim_sizes[i];
    }
    long bytes = (long)S_get_type_size(elem_type) * elements;
    if (bytes > 2147483647L) {
        if (out_elements) *out_elements = 0;
        return -2;
    }
    if (out_elements) *out_elements = (int)elements;
    return (int)bytes;
}

static bool func_sig_match(FuncSignature* a, FuncSignature* b) {
    if (!a || !b) return false;
    if (a->return_type != b->return_type) return false;
    if (a->param_count != b->param_count) return false;
    for (int i = 0; i < a->param_count; i++) {
        if (a->param_types[i] != b->param_types[i]) return false;
        bool a_c = a->param_pointee_const ? a->param_pointee_const[i] : false;
        bool b_c = b->param_pointee_const ? b->param_pointee_const[i] : false;
        if (a_c != b_c) return false;
    }
    if ((a->multi_return == NULL) != (b->multi_return == NULL)) return false;
    if (a->multi_return && b->multi_return) {
        if (a->multi_return->count != b->multi_return->count) return false;
        for (int i = 0; i < a->multi_return->count; i++) {
            if (a->multi_return->types[i] != b->multi_return->types[i]) return false;
            if (a->multi_return->is_variadic[i] != b->multi_return->is_variadic[i]) return false;
        }
    }
    return true;
}

static FuncSignature* func_sig_from_ast(AST_Function* func) {
    if (!func) return NULL;
    FuncSignature* sig = calloc(1, sizeof(FuncSignature));
    if (!sig) OUT_OF_MEMORY();
    if (func->multi_return_node) {
        sig->return_type = DATA_VOID;
    } else if (func->Return) {
        sig->return_type = func->Return->returnType;
    } else {
        sig->return_type = DATA_VOID;
    }
    sig->return_pointee_const = func->return_pointee_const;
    if (func->return_class_name != NULL) {
        sig->return_class_name = strdup(func->return_class_name);
    }
    sig->is_closure_sig = func->is_closure;
    int skip = func_has_env_param(func) ? 1 : 0;
    sig->param_count = func->parameterCount - skip;
    if (sig->param_count > 0) {
        sig->param_types = malloc(sizeof(DataType) * sig->param_count);
        for (int i = 0; i < sig->param_count; i++) {
            sig->param_types[i] = func->parameterType[skip + i];
        }
        sig->param_pointee_const = calloc((size_t)sig->param_count, sizeof(bool));
        sig->param_func_sigs = calloc((size_t)sig->param_count, sizeof(FuncSignature*));
        sig->param_class_names = calloc((size_t)sig->param_count, sizeof(char*));
        sig->param_is_array = calloc((size_t)sig->param_count, sizeof(bool));
        ASTNode* pn = func->Parameter;
        for (int si = 0; si < skip && pn; si++) pn = pn->next;
        for (int pi = 0; pi < sig->param_count && pn; pi++) {
            if (pn->type == AST_VAR || pn->type == AST_UNINIT_VAR
                || pn->type == AST_VAR_ASS) {
                AST_Var* pv = (AST_Var*)pn;
                sig->param_pointee_const[pi] = (pv->level_const_mask & 1u) != 0;
                if (pv->func_ptr_sig != NULL) {
                    sig->param_func_sigs[pi] = deep_copy_func_sig(pv->func_ptr_sig);
                }
                if (pv->dataType == DATA_IDENTIFIER && pv->internal_name != NULL) {
                    sig->param_class_names[pi] = strdup(pv->internal_name);
                }
                if (pv->dim_count > 0 || pv->total_bytes >= 8) {
                    sig->param_is_array[pi] = true;
                }
            } else if (pn->type == AST_ARRAY_DECL) {
                AST_ArrayDecl* pa = (AST_ArrayDecl*)pn;
                sig->param_pointee_const[pi] = pa->is_const;
                sig->param_is_array[pi] = true;
            }
            pn = pn->next;
        }
    }
    sig->has_varargs = func->has_varargs;
    sig->vararg_param_count = func->vararg_param_count;
    if (func->vararg_param_count > 0) {
        sig->vararg_param_indices = malloc(sizeof(int) * func->vararg_param_count);
        memcpy(sig->vararg_param_indices, func->vararg_param_indices,
               sizeof(int) * func->vararg_param_count);
    }
    sig->multi_return = deep_copy_multi_return(func->multi_return);
    sig->ret_func_sig = deep_copy_func_sig(func->ret_func_sig);
    return sig;
}

static bool eval_constant_expr(ASTNode* expr, int64_t* result, bool* overflow) {
    if (!expr) return false;
    switch (expr->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)expr;
        if (lit->is_call_function) return false;
        if (lit->base.value.type == TOKEN_NUMBER) {
            *result = (int64_t)parse_integer64(lit->base.value.value);
            return true;
        }
        if (lit->internal_name != NULL) {
            ClassEntry* cls = find_class(lit->internal_name);
            if (cls) { *result = cls->total_size; return true; }
            DataType alias = resolve_type_alias(lit->internal_name);
            if (alias != DATA_ERROR) {
                *result = S_get_type_size(alias);
                return true;
            }
        }
        Symbol* sym = lookup_symbol(lit->base.value.value);
        if (sym && sym->is_const && sym->const_value_str) {
            if (sym->type == DATA_INT || sym->type == DATA_UINT ||
                sym->type == DATA_CHAR || sym->type == DATA_BOOL ||
                sym->type == DATA_INT8 || sym->type == DATA_INT16 ||
                sym->type == DATA_INT32 || sym->type == DATA_INT64 ||
                sym->type == DATA_UINT8 || sym->type == DATA_UINT16 ||
                sym->type == DATA_UINT32 || sym->type == DATA_UINT64) {
                *result = (int64_t)parse_integer64(sym->const_value_str);
                return true;
            }
            return false;
        }
        if (sym && sym->type == DATA_INT && sym->internal_name) {
            char* end;
            long val = strtol(sym->internal_name, &end, 10);
            if (*end == '\0') { *result = (int64_t)val; return true; }
        }
        return false;
    }
    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)expr;
        if (ma->const_value_str != NULL) {
            *result = (int64_t)parse_integer64(ma->const_value_str);
            return true;
        }
        return false;
    }
    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)expr;
        int64_t l, r;
        if (!eval_constant_expr(bin->left,  &l, overflow)) return false;
        if (!eval_constant_expr(bin->right, &r, overflow)) return false;
        int64_t v;
        switch (bin->op.type) {
        case TOKEN_ADD: v = l + r; break;
        case TOKEN_SUB: v = l - r; break;
        case TOKEN_MUL: v = l * r; break;
        case TOKEN_DIV:
            if (r == 0) return false;
            if (l == INT32_MIN && r == -1) return false;
            v = l / r;
            break;
        case TOKEN_MOD:
            if (r == 0) return false;
            if (l == INT32_MIN && r == -1) return false;
            v = l % r;
            break;
        case TOKEN_SHIFT_LEFT:
            if (r < 0 || r >= 32) return false;
            v = l << r;
            break;
        case TOKEN_SHIFT_RIGHT:
            if (r < 0 || r >= 32) return false;
            v = l >> r;
            break;
        default: return false;
        }
        if (v < INT32_MIN || v > INT32_MAX) {
            if (overflow) *overflow = true;
        }
        *result = v;
        return true;
    }
    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)expr;
        int64_t v;
        if (!eval_constant_expr(un->right, &v, overflow)) return false;
        if (un->op.type == TOKEN_SUB) {
            v = -v;
            if (v < INT32_MIN || v > INT32_MAX) {
                if (overflow) *overflow = true;
            }
            *result = v;
            return true;
        }
        return false;
    }
    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)expr;
        int64_t cond;
        if (!eval_constant_expr(tern->condition, &cond, overflow)) return false;
        return eval_constant_expr(tern->then_expr, result, overflow) ||
               eval_constant_expr(tern->else_expr, result, overflow);
    }
    default: return false;
    }
}

static bool eval_constant_expr_as_double(ASTNode* expr, double* out) {
    if (!expr) return false;
    switch (expr->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)expr;
        if (lit->is_call_function) return false;
        if (lit->base.value.type == TOKEN_FLOAT_LITERAL) {
            char* end = NULL;
            double v = strtod(lit->base.value.value, &end);
            if (!end || *end != '\0') return false;
            *out = v;
            return true;
        }
        if (lit->base.value.type == TOKEN_NUMBER) {
            *out = (double)parse_integer64(lit->base.value.value);
            return true;
        }
        if (lit->internal_name != NULL) {
            ClassEntry* cls = find_class(lit->internal_name);
            if (cls) { *out = (double)cls->total_size; return true; }
            DataType alias = resolve_type_alias(lit->internal_name);
            if (alias != DATA_ERROR) {
                *out = (double)S_get_type_size(alias);
                return true;
            }
        }
        Symbol* sym = lookup_symbol(lit->base.value.value);
        if (sym && sym->is_const && sym->const_value_str) {
            if (is_float_type(sym->type) || is_ufloat_type(sym->type)) {
                *out = strtod(sym->const_value_str, NULL);
                return true;
            }
            if (is_int_type(sym->type) || is_uint_type(sym->type)) {
                *out = (double)parse_integer64(sym->const_value_str);
                return true;
            }
        }
        return false;
    }
    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)expr;
        if (ma->const_value_str != NULL) {
            *out = strtod(ma->const_value_str, NULL);
            return true;
        }
        return false;
    }
    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)expr;
        double l, r;
        if (!eval_constant_expr_as_double(bin->left,  &l)) return false;
        if (!eval_constant_expr_as_double(bin->right, &r)) return false;
        switch (bin->op.type) {
        case TOKEN_ADD: *out = l + r; return true;
        case TOKEN_SUB: *out = l - r; return true;
        case TOKEN_MUL: *out = l * r; return true;
        case TOKEN_DIV:
            if (r == 0.0) return false;
            *out = l / r;
            return true;
        default: return false;
        }
    }
    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)expr;
        if (un->op.type != TOKEN_SUB) return false;
        double r;
        if (!eval_constant_expr_as_double(un->right, &r)) return false;
        *out = -r;
        return true;
    }
    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)expr;
        double cond;
        if (!eval_constant_expr_as_double(tern->condition, &cond)) return false;
        return eval_constant_expr_as_double(tern->then_expr, out) ||
               eval_constant_expr_as_double(tern->else_expr, out);
    }
    default: return false;
    }
}

static void register_socket_builtin(const char* name, DataType ret_type,
                                    DataType* param_types, int param_count) {
    Symbol* sym = create_symbol(
        (Token){.value = (char*)name, .type = TOKEN_IDENTIFIER},
        ret_type, (char*)name, (char*)name);
    sym->is_function = true;
    sym->func_sig = calloc(1, sizeof(FuncSignature));
    if (!sym->func_sig) OUT_OF_MEMORY();
    sym->func_sig->return_type = ret_type;
    sym->func_sig->param_count = param_count;
    if (param_count > 0) {
        sym->func_sig->param_types = malloc(sizeof(DataType) * param_count);
        if (!sym->func_sig->param_types) OUT_OF_MEMORY();
        memcpy(sym->func_sig->param_types, param_types,
               sizeof(DataType) * param_count);
    }
    insert_symbol(sym);

    AST_Function* func = calloc(1, sizeof(AST_Function));
    if (!func) OUT_OF_MEMORY();
    func->base.type = AST_FUNCTION;
    func->name.value = strdup(name);
    func->name.type = TOKEN_IDENTIFIER;
    func->internal_name = strdup(name);
    func->parameterCount = param_count;
    if (param_count > 0) {
        func->parameterType = malloc(sizeof(DataType) * param_count);
        if (!func->parameterType) OUT_OF_MEMORY();
        memcpy(func->parameterType, param_types,
               sizeof(DataType) * param_count);
    }
    func->return_Count = 1;
    func->Return = calloc(1, sizeof(AST_Return));
    if (!func->Return) OUT_OF_MEMORY();
    func->Return->returnType = ret_type;
    if (ret_type == DATA_STR) {
        func->returns_struct = true;
        func->return_struct_bytes = 8;
    }
    add_function_with_params(name, func);
}

static void add_function() {
    {
        AST_Function* fmt_func = calloc(1, sizeof(AST_Function));
        fmt_func->base.type = AST_FUNCTION;
        fmt_func->name.value = strdup("format");
        fmt_func->name.type = TOKEN_IDENTIFIER;
        fmt_func->internal_name = strdup("_format_string");
        fmt_func->parameterCount = 1;
        fmt_func->parameterType = malloc(sizeof(DataType));
        fmt_func->parameterType[0] = DATA_STRING;
        fmt_func->return_Count = 1;
        fmt_func->Return = calloc(1, sizeof(AST_Return));
        fmt_func->Return->returnType = DATA_STRING;
        add_function_with_params("format", fmt_func);

        Symbol* sym = lookup_symbol("format");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_STRING;
            sym->func_sig->param_count = 1;
            sym->func_sig->param_types = malloc(sizeof(DataType));
            sym->func_sig->param_types[0] = DATA_STRING;
        }
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_concat", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_str_concat", "_str_concat");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_STRING;
        sym->func_sig->param_types[1] = DATA_STRING;
        insert_symbol(sym);
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_concat");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_concat");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_str_concat", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_length", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_str_length", "_str_length");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_length");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_length");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_str_length", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_char_at", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_str_char_at", "_str_char_at");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_STRING;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_char_at");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_char_at");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_str_char_at", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_substring", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_str_substring", "_str_substring");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_STRING;
        sym->func_sig->param_types[1] = DATA_INT;
        sym->func_sig->param_types[2] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_substring");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_substring");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_INT;
        func->parameterType[2] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_str_substring", func);
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_char_to_str");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_char_to_str");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_char_to_str", func);

        Symbol* sym = lookup_symbol("_char_to_str");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_STRING;
            sym->func_sig->param_count = 1;
            sym->func_sig->param_types = malloc(sizeof(DataType));
            sym->func_sig->param_types[0] = DATA_INT;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_plus_char");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_plus_char_2");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_str_plus_char", func);

        Symbol* sym = lookup_symbol("_str_plus_char");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_STRING;
            sym->func_sig->param_count = 2;
            sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
            sym->func_sig->param_types[0] = DATA_STRING;
            sym->func_sig->param_types[1] = DATA_INT;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_char_plus_str");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_char_plus_str_2");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_char_plus_str", func);

        Symbol* sym = lookup_symbol("_char_plus_str");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_STRING;
            sym->func_sig->param_count = 2;
            sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
            sym->func_sig->param_types[0] = DATA_INT;
            sym->func_sig->param_types[1] = DATA_STRING;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_concat");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_concat");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_str_concat", func);

        Symbol* sym = lookup_symbol("_str_concat");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_STRING;
            sym->func_sig->param_count = 2;
            sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
            sym->func_sig->param_types[0] = DATA_STRING;
            sym->func_sig->param_types[1] = DATA_STRING;
        }
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_GetCommandLineArgc", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_GetCommandLineArgc", "_GetCommandLineArgc");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 0;
        sym->func_sig->param_types = NULL;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_GetCommandLineArgc");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_GetCommandLineArgc");
        func->parameterCount = 0;
        func->parameterType = NULL;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_GetCommandLineArgc", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sys_argv_at", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_sys_argv_at", "_sys_argv_at");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sys_argv_at");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sys_argv_at");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_sys_argv_at", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_system_exit", .type = TOKEN_IDENTIFIER},
            DATA_VOID, "_system_exit", "_system_exit");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_VOID;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_system_exit");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_system_exit");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("_system_exit", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_system_sleep", .type = TOKEN_IDENTIFIER},
            DATA_VOID, "_system_sleep", "_system_sleep");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_VOID;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_system_sleep");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_system_sleep");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("_system_sleep", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_system_time", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_system_time", "_system_time");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 0;
        sym->func_sig->param_types = NULL;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_system_time");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_system_time");
        func->parameterCount = 0;
        func->parameterType = NULL;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_system_time", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_system_getenv", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_system_getenv", "_system_getenv");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_system_getenv");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_system_getenv");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_system_getenv", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_GetCommandLineArgv", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_GetCommandLineArgv", "_GetCommandLineArgv");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_GetCommandLineArgv");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_GetCommandLineArgv");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_GetCommandLineArgv", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_system_call", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_system_call", "_system_call");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_system_call");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_system_call");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_system_call", func);
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("gc");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_gc_force");
        func->parameterCount = 0;
        func->parameterType = NULL;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("gc", func);

        Symbol* sym = lookup_symbol("gc");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_VOID;
            sym->func_sig->param_count = 0;
            sym->func_sig->param_types = NULL;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_gc_status");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_gc_status");
        func->parameterCount = 0;
        func->parameterType = NULL;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("_gc_status", func);

        Symbol* sym = lookup_symbol("_gc_status");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_VOID;
            sym->func_sig->param_count = 0;
            sym->func_sig->param_types = NULL;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_gc_alloc_ptr_array");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_gc_alloc_ptr_array");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->return_ptr_depth = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_gc_alloc_ptr_array", func);

        Symbol* sym = lookup_symbol("_gc_alloc_ptr_array");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_INT;
            sym->func_sig->param_count = 1;
            sym->func_sig->param_types = malloc(sizeof(DataType));
            sym->func_sig->param_types[0] = DATA_INT;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("allocation");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_unsafe_alloc");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        func->return_ptr_depth = 1;
        add_function_with_params("allocation", func);

        Symbol* sym = lookup_symbol("allocation");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_INT;
            sym->func_sig->param_count = 2;
            sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
            sym->func_sig->param_types[0] = DATA_INT;
            sym->func_sig->param_types[1] = DATA_INT;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("address");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_unsafe_address_alloc");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        func->return_ptr_depth = 1;
        add_function_with_params("address", func);

        Symbol* sym = lookup_symbol("address");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_INT;
            sym->func_sig->param_count = 2;
            sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
            sym->func_sig->param_types[0] = DATA_INT;
            sym->func_sig->param_types[1] = DATA_INT;
        }
    }

    {
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("release");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_unsafe_free");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("release", func);

        Symbol* sym = lookup_symbol("release");
        if (sym && !sym->func_sig) {
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = DATA_VOID;
            sym->func_sig->param_count = 1;
            sym->func_sig->param_types = malloc(sizeof(DataType));
            sym->func_sig->param_types[0] = DATA_INT;
        }
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_to_string", .type = TOKEN_IDENTIFIER},
            DATA_STRING, "_str_to_string", "_str_to_string");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STRING;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_STR;
        insert_symbol(sym);
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_to_string");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_to_string");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_STR;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STRING;
        add_function_with_params("_str_to_string", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_str_substring_str", .type = TOKEN_IDENTIFIER},
            DATA_STR, "_str_substring_str", "_str_substring_str");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_STR;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_STR;
        sym->func_sig->param_types[1] = DATA_INT;
        sym->func_sig->param_types[2] = DATA_INT;
        insert_symbol(sym);
        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_str_substring_str");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_str_substring_str");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_STR;
        func->parameterType[1] = DATA_INT;
        func->parameterType[2] = DATA_INT;
        func->returns_array = true;
        func->return_element_type = DATA_INT;
        func->return_dim_count = 1;
        func->return_dim_sizes = malloc(sizeof(int));
        func->return_dim_sizes[0] = 8;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_STR;
        add_function_with_params("_str_substring_str", func);
    }

    {
        DataType socket_pt3[] = { DATA_INT, DATA_INT, DATA_INT };
        register_socket_builtin("_socket_create", DATA_INT, socket_pt3, 3);

        DataType socket_pt_c[] = { DATA_INT, DATA_STRING, DATA_INT };
        register_socket_builtin("_socket_connect", DATA_INT, socket_pt_c, 3);

        DataType socket_pt2[] = { DATA_INT, DATA_INT };
        register_socket_builtin("_socket_bind",   DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_listen", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_set_timeout", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_set_nonblock", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_set_reuseaddr", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_set_nodelay", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_select_read", DATA_INT, socket_pt2, 2);
        register_socket_builtin("_socket_select_write", DATA_INT, socket_pt2, 2);

        DataType socket_pt1[] = { DATA_INT };
        register_socket_builtin("_socket_accept", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_close",  DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_error_string", DATA_STRING, socket_pt1, 1);
        register_socket_builtin("_socket_get_error", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_peer_ip", DATA_STRING, socket_pt1, 1);
        register_socket_builtin("_socket_peer_port", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_local_port", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_ntohl", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_ntohs", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_htonl", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_htons", DATA_INT, socket_pt1, 1);
        register_socket_builtin("_socket_inet_ntoa", DATA_STRING, socket_pt1, 1);

        DataType socket_pt_s[] = { DATA_INT, DATA_STRING, DATA_INT };
        register_socket_builtin("_socket_send", DATA_INT, socket_pt_s, 3);
        register_socket_builtin("_socket_send_all", DATA_INT, socket_pt_s, 3);

        DataType socket_pt_r[] = { DATA_INT, DATA_INT };
        register_socket_builtin("_socket_recv", DATA_STRING, socket_pt_r, 2);
        register_socket_builtin("_socket_recv_str", DATA_STR, socket_pt_r, 2);
        register_socket_builtin("_socket_recv_line", DATA_STRING, socket_pt_r, 2);
        register_socket_builtin("_socket_recv_exact", DATA_STRING, socket_pt_r, 2);

        DataType socket_pt_sendstr[] = { DATA_INT, DATA_STR };
        register_socket_builtin("_socket_send_str", DATA_INT, socket_pt_sendstr, 2);

        DataType socket_pt_st[] = { DATA_INT, DATA_STRING, DATA_INT,
                                    DATA_STRING, DATA_INT };
        register_socket_builtin("_socket_sendto", DATA_INT, socket_pt_st, 5);

        register_socket_builtin("_socket_recvfrom", DATA_STRING, socket_pt_r, 2);

        DataType socket_pt_str1[] = { DATA_STRING };
        register_socket_builtin("_socket_inet_addr", DATA_INT, socket_pt_str1, 1);

        register_socket_builtin("_socket_sender_addr", DATA_INT, NULL, 0);
        register_socket_builtin("_socket_sender_port", DATA_INT, NULL, 0);
        register_socket_builtin("_socket_sender_ip", DATA_STRING, NULL, 0);
        register_socket_builtin("_socket_last_error", DATA_INT, NULL, 0);

        DataType iocp_pt1[] = { DATA_INT };
        DataType iocp_pt2[] = { DATA_INT, DATA_INT };
        DataType iocp_cb[] = { DATA_INT };
        DataType iocp_acc[] = { DATA_INT, DATA_INT };
        DataType iocp_recv[] = { DATA_INT, DATA_INT, DATA_INT };
        DataType iocp_send[] = { DATA_INT, DATA_INT, DATA_INT, DATA_INT };
        register_socket_builtin("_iocp_init", DATA_INT, NULL, 0);
        register_socket_builtin("_iocp_start_worker", DATA_INT, iocp_cb, 1);
        register_socket_builtin("_socket_accept_async", DATA_INT, iocp_acc, 2);
        register_socket_builtin("_socket_recv_async", DATA_INT, iocp_recv, 3);
        register_socket_builtin("_socket_send_async", DATA_INT, iocp_send, 4);
        register_socket_builtin("_ov_get_fd", DATA_INT, iocp_pt1, 1);
        register_socket_builtin("_ov_get_buffer", DATA_INT, iocp_pt1, 1);
        register_socket_builtin("_ov_get_capacity", DATA_INT, iocp_pt1, 1);
        register_socket_builtin("_ov_get_user", DATA_INT, iocp_pt1, 1);
        register_socket_builtin("_ov_set_user", DATA_VOID, iocp_pt2, 2);
        register_socket_builtin("_ov_set_callback", DATA_VOID, iocp_pt2, 2);
        register_socket_builtin("_iocp_get_handle", DATA_INT, NULL, 0);
        register_socket_builtin("_iocp_associate", DATA_INT, iocp_pt2, 2);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_float_to_bits", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_float_to_bits", "_float_to_bits");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_FLOAT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_float_to_bits");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_float_to_bits");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_FLOAT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_float_to_bits", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_bits_to_float", .type = TOKEN_IDENTIFIER},
            DATA_FLOAT, "_bits_to_float", "_bits_to_float");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_FLOAT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_bits_to_float");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_bits_to_float");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_FLOAT;
        add_function_with_params("_bits_to_float", func);
    }

        {
        Symbol* sym = create_symbol(
            (Token){.value = "_thread_create", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_thread_create", "_thread_create");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_thread_create");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_thread_create");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_thread_create", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_thread_join", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_thread_join", "_thread_join");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_thread_join");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_thread_join");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_thread_join", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_thread_self", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_thread_self", "_thread_self");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 0;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_thread_self");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_thread_self");
        func->parameterCount = 0;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_thread_self", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_thread_sleep", .type = TOKEN_IDENTIFIER},
            DATA_VOID, "_thread_sleep", "_thread_sleep");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_VOID;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_thread_sleep");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_thread_sleep");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_VOID;
        add_function_with_params("_thread_sleep", func);
    }

        {
        const char* sync_names[] = {
            "_sync_mutex_create", "_sync_mutex_lock",
            "_sync_mutex_unlock", "_sync_mutex_destroy",
            "_sync_event_create", "_sync_event_set",
            "_sync_event_reset", "_sync_event_wait",
            "_sync_event_wait_timeout", "_sync_event_destroy",
            "_sync_semaphore_create", "_sync_semaphore_acquire",
            "_sync_semaphore_release", "_sync_semaphore_destroy"
        };
        DataType sync_returns[] = {
            DATA_INT, DATA_VOID,
            DATA_VOID, DATA_VOID,
            DATA_INT, DATA_VOID,
            DATA_VOID, DATA_VOID,
            DATA_INT, DATA_VOID,
            DATA_INT, DATA_VOID,
            DATA_VOID, DATA_VOID
        };
        int sync_pcount[] = { 0, 1, 1, 1, 0, 1, 1, 1, 2, 1, 2, 1, 1, 1 };
        DataType sync_p0[] = { DATA_ERROR, DATA_INT, DATA_INT, DATA_INT,
                               DATA_ERROR, DATA_INT, DATA_INT, DATA_INT,
                               DATA_INT, DATA_INT, DATA_INT, DATA_INT,
                               DATA_INT, DATA_INT };
        DataType sync_p1[] = { DATA_ERROR, DATA_ERROR, DATA_ERROR, DATA_ERROR,
                               DATA_ERROR, DATA_ERROR, DATA_ERROR, DATA_ERROR,
                               DATA_INT, DATA_ERROR, DATA_INT, DATA_ERROR,
                               DATA_ERROR, DATA_ERROR };

        for (int si = 0; si < 14; si++) {
            Symbol* sym = create_symbol(
                (Token){.value = (char*)sync_names[si], .type = TOKEN_IDENTIFIER},
                sync_returns[si], (char*)sync_names[si], (char*)sync_names[si]);
            sym->is_function = true;
            sym->func_sig = calloc(1, sizeof(FuncSignature));
            sym->func_sig->return_type = sync_returns[si];
            sym->func_sig->param_count = sync_pcount[si];
            if (sync_pcount[si] > 0) {
                sym->func_sig->param_types = malloc(sizeof(DataType) * sync_pcount[si]);
                sym->func_sig->param_types[0] = sync_p0[si];
                if (sync_pcount[si] > 1) {
                    sym->func_sig->param_types[1] = sync_p1[si];
                }
            }
            insert_symbol(sym);

            AST_Function* func = calloc(1, sizeof(AST_Function));
            func->base.type = AST_FUNCTION;
            func->name.value = strdup(sync_names[si]);
            func->name.type = TOKEN_IDENTIFIER;
            func->internal_name = strdup(sync_names[si]);
            func->parameterCount = sync_pcount[si];
            if (sync_pcount[si] > 0) {
                func->parameterType = malloc(sizeof(DataType) * sync_pcount[si]);
                func->parameterType[0] = sync_p0[si];
                if (sync_pcount[si] > 1) {
                    func->parameterType[1] = sync_p1[si];
                }
            }
            func->return_Count = 1;
            func->Return = calloc(1, sizeof(AST_Return));
            func->Return->returnType = sync_returns[si];
            add_function_with_params(sync_names[si], func);
        }
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_socket_bind_addr", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_socket_bind_addr", "_socket_bind_addr");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_STRING;
        sym->func_sig->param_types[2] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_socket_bind_addr");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_socket_bind_addr");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_STRING;
        func->parameterType[2] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_socket_bind_addr", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_open", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_open", "_sqlite_open");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_STRING;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_open");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_open");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_STRING;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_open", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_close", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_close", "_sqlite_close");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_close");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_close");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_close", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_exec", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_exec", "_sqlite_exec");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_exec");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_exec");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_exec", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_prepare", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_prepare", "_sqlite_prepare");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_prepare");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_prepare");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_prepare", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_step", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_step", "_sqlite_step");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_step");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_step");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_step", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_finalize", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_finalize", "_sqlite_finalize");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_finalize");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_finalize");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_finalize", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_reset", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_reset", "_sqlite_reset");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_reset");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_reset");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_reset", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_bind_int", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_bind_int", "_sqlite_bind_int");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        sym->func_sig->param_types[2] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_bind_int");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_bind_int");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->parameterType[2] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_bind_int", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_bind_int64", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_bind_int64", "_sqlite_bind_int64");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        sym->func_sig->param_types[2] = DATA_INT64;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_bind_int64");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_bind_int64");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->parameterType[2] = DATA_INT64;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_bind_int64", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_bind_text", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_bind_text", "_sqlite_bind_text");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 3;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 3);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        sym->func_sig->param_types[2] = DATA_STRING;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_bind_text");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_bind_text");
        func->parameterCount = 3;
        func->parameterType = malloc(sizeof(DataType) * 3);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->parameterType[2] = DATA_STRING;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_bind_text", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_bind_null", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_bind_null", "_sqlite_bind_null");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_bind_null");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_bind_null");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_bind_null", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_column_int", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_column_int", "_sqlite_column_int");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_column_int");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_column_int");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_column_int", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_column_int64", .type = TOKEN_IDENTIFIER},
            DATA_INT64, "_sqlite_column_int64", "_sqlite_column_int64");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT64;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_column_int64");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_column_int64");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT64;
        add_function_with_params("_sqlite_column_int64", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_column_text", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_column_text", "_sqlite_column_text");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_column_text");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_column_text");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_column_text", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_column_bytes", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_column_bytes", "_sqlite_column_bytes");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_column_bytes");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_column_bytes");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_column_bytes", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_column_type", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_column_type", "_sqlite_column_type");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 2;
        sym->func_sig->param_types = malloc(sizeof(DataType) * 2);
        sym->func_sig->param_types[0] = DATA_INT;
        sym->func_sig->param_types[1] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_column_type");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_column_type");
        func->parameterCount = 2;
        func->parameterType = malloc(sizeof(DataType) * 2);
        func->parameterType[0] = DATA_INT;
        func->parameterType[1] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_column_type", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_last_insert_rowid", .type = TOKEN_IDENTIFIER},
            DATA_INT64, "_sqlite_last_insert_rowid", "_sqlite_last_insert_rowid");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT64;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_last_insert_rowid");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_last_insert_rowid");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT64;
        add_function_with_params("_sqlite_last_insert_rowid", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_changes", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_changes", "_sqlite_changes");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_changes");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_changes");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_changes", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_errcode", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_errcode", "_sqlite_errcode");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_errcode");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_errcode");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_errcode", func);
    }

    {
        Symbol* sym = create_symbol(
            (Token){.value = "_sqlite_errmsg", .type = TOKEN_IDENTIFIER},
            DATA_INT, "_sqlite_errmsg", "_sqlite_errmsg");
        sym->is_function = true;
        sym->func_sig = calloc(1, sizeof(FuncSignature));
        sym->func_sig->return_type = DATA_INT;
        sym->func_sig->param_count = 1;
        sym->func_sig->param_types = malloc(sizeof(DataType));
        sym->func_sig->param_types[0] = DATA_INT;
        insert_symbol(sym);

        AST_Function* func = calloc(1, sizeof(AST_Function));
        func->base.type = AST_FUNCTION;
        func->name.value = strdup("_sqlite_errmsg");
        func->name.type = TOKEN_IDENTIFIER;
        func->internal_name = strdup("_sqlite_errmsg");
        func->parameterCount = 1;
        func->parameterType = malloc(sizeof(DataType));
        func->parameterType[0] = DATA_INT;
        func->return_Count = 1;
        func->Return = calloc(1, sizeof(AST_Return));
        func->Return->returnType = DATA_INT;
        add_function_with_params("_sqlite_errmsg", func);
    }
}

static void predeclare_class_recursive(AST_ClassDef* cd, const char* outer_name,
                                        TypeParam* outer_tparams, int outer_tp_count) {
    if (cd == NULL || cd->name == NULL) return;

    char* short_name = strdup(cd->name);
    char* full_name;
    if (outer_name != NULL) {
        int len = strlen(outer_name) + 1 + strlen(cd->name) + 1;
        full_name = malloc(len);
        if (!full_name) OUT_OF_MEMORY();
        snprintf(full_name, len, "%s_%s", outer_name, cd->name);
    } else {
        full_name = strdup(cd->name);
    }

    ClassEntry* existing = find_class(full_name);
    if (existing != NULL) {
        ERROR(ERR_SEM_DUPLICATE_VAR,
              SEMLOC(cd->base.value),
              "duplicate class '%s'", full_name);
        free(full_name);
        free(short_name);
        return;
    }

    free(cd->name);
    cd->name = full_name;

    if (outer_tp_count > 0) {
        if (cd->type_param_count > 0) {
            ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cd->base.value),
                  "nested class '%s' cannot declare its own type parameters "
                  "when outer class is generic", cd->name);
        } else {
            TypeParam* merged = malloc(sizeof(TypeParam) * outer_tp_count);
            if (!merged) OUT_OF_MEMORY();
            for (int i = 0; i < outer_tp_count; i++) {
                merged[i].name = strdup(outer_tparams[i].name);
                merged[i].index = i;
                merged[i].constraint_count = outer_tparams[i].constraint_count;
                if (outer_tparams[i].constraint_count > 0) {
                    merged[i].constraints = malloc(sizeof(TypeConstraint) * outer_tparams[i].constraint_count);
                    if (!merged[i].constraints) OUT_OF_MEMORY();
                    for (int j = 0; j < outer_tparams[i].constraint_count; j++) {
                        merged[i].constraints[j].kind = outer_tparams[i].constraints[j].kind;
                        merged[i].constraints[j].class_name = strdup(outer_tparams[i].constraints[j].class_name);
                    }
                } else {
                    merged[i].constraints = NULL;
                }
                merged[i].has_default_type = outer_tparams[i].has_default_type;
                merged[i].default_type = outer_tparams[i].default_type;
            }
            if (cd->type_params != NULL) {
                free_type_params(cd->type_params, cd->type_param_count);
            }
            cd->type_params = merged;
            cd->type_param_count = outer_tp_count;
            cd->is_generic = true;
        }
    }

    ClassEntry* cls = calloc(1, sizeof(ClassEntry));
    if (!cls) OUT_OF_MEMORY();
    cls->name = strdup(cd->name);
    cls->short_name = short_name;
    cls->outer_class_name = outer_name ? strdup(outer_name) : NULL;
    cls->parent_count = cd->parent_count;
    cls->parent_names = malloc(sizeof(char*) * cd->parent_count);
    for (int i = 0; i < cd->parent_count; i++) {
        if (outer_name != NULL) {
            char full[256];
            snprintf(full, sizeof(full), "%s_%s",
                     outer_name, cd->parent_names[i]);
            if (find_class(full) != NULL) {
                cls->parent_names[i] = strdup(full);
                free(cd->parent_names[i]);
                cd->parent_names[i] = strdup(full);
                continue;
            }
        }
        cls->parent_names[i] = strdup(cd->parent_names[i]);
    }
    cls->modifier = cd->modifier;
    cls->type_param_count = cd->type_param_count;
    cls->type_params = cd->type_params;    /* 借用 AST 上的类型参数（含约束） */
    cls->is_generic = cd->is_generic;
    cls->generic_ast = cd;
    cls->class_id = next_class_id++;
    add_class(cls);

    ASTNode* m = cd->members;
    while (m) {
        if (m->type == AST_FUNCTION) {
            add_function_with_params(((AST_Function*)m)->name.value,
                                    (AST_Function*)m);
        } else if (m->type == AST_CLASS_DEF) {
            predeclare_class_recursive((AST_ClassDef*)m, cd->name,
                                        cd->type_params, cd->type_param_count);
        }
        m = m->next;
    }
}

static void predeclare_global_vars(ASTNode* node) {
    while (node) {
        if ((node->type == AST_VAR || node->type == AST_UNINIT_VAR)
            && current_class == NULL && current_analyzing_function == NULL) {
            AST_Var* var = (AST_Var*)node;
            if (var->name.value != NULL && lookup_symbol(var->name.value) == NULL) {
                char* gin = generate_internal_name();
                Symbol* gsym = create_symbol(var->name, var->dataType,
                                             var->name.value, gin);
                gsym->is_global_predecl = true;
                gsym->ptr_depth = var->ptr_depth;
                gsym->is_static = var->is_static;
                insert_symbol(gsym);
            }
        }
        node = node->next;
    }
}

static void predeclare_all_types(ASTNode* node) {
    while (node) {
        switch (node->type) {
            case AST_FUNCTION: {
                AST_Function* func = (AST_Function*)node;
                if (func->is_static && func->base.value.file != NULL
                    && func->name.value != NULL) {
                    char mangled[512];
                    make_static_mangled_name(func->base.value.file,
                                             func->name.value,
                                             mangled, sizeof(mangled));
                    if (mangled[0] != '\0') {
                        free(func->name.value);
                        func->name.value = strdup(mangled);
                        if (func->base.value.value) {
                            free(func->base.value.value);
                            func->base.value.value = strdup(mangled);
                        }
                    }
                }
                FuncEntry* dup = find_duplicate_function(func->name.value, func);
                if (dup != NULL) {
                    DataType ret_a = dup->func->Return
                                     ? dup->func->Return->returnType
                                     : dup->func->return_type;
                    DataType ret_b = func->Return
                                     ? func->Return->returnType
                                     : func->return_type;
                    if (ret_a == ret_b) {
                        ERROR(ERR_SEM_DUPLICATE_VAR,
                              SEMLOC(func->name),
                              "duplicate function '%s'", func->name.value);
                    } else {
                        ERROR(ERR_SEM_DUPLICATE_VAR,
                              SEMLOC(func->name),
                              "function '%s' redeclared with different return type",
                              func->name.value);
                    }
                    break;
                }
                if (func->internal_name == NULL && !func->is_generic) {
                    int len = strlen(func->name.value) + 1;
                    for (int i = 0; i < func->parameterCount; i++) {
                        len += 1 + strlen(data_type_to_string(func->parameterType[i]));
                    }
                    char* sig_name = malloc(len + 1);
                    if (sig_name) {
                        sprintf(sig_name, "%s", func->name.value);
                        char* p = sig_name + strlen(sig_name);
                        for (int i = 0; i < func->parameterCount; i++) {
                            *p++ = '_';
                            const char* tn = data_type_to_string(func->parameterType[i]);
                            strcpy(p, tn);
                            p += strlen(tn);
                        }
                        *p = '\0';
                        func->internal_name = sig_name;
                    }
                }
                add_function_with_params(func->name.value, func);
                Symbol* sym = lookup_symbol(func->name.value);
                if (sym == NULL) {
                    sym = create_symbol(
                        (Token){.value = func->name.value, .type = TOKEN_IDENTIFIER},
                        func->return_type,
                        func->name.value,
                        func->internal_name ? func->internal_name : func->name.value);
                    sym->is_function = true;
                    sym->func_sig = func_sig_from_ast(func);
                    insert_symbol(sym);
                } else if (!sym->func_sig) {
                    sym->func_sig = func_sig_from_ast(func);
                }
                break;
            }
            case AST_CLASS_DEF: {
                AST_ClassDef* cd = (AST_ClassDef*)node;
                predeclare_class_recursive(cd, NULL, NULL, 0);
                break;
            }
            case AST_VAR:
            case AST_UNINIT_VAR: {
                AST_Var* var = (AST_Var*)node;
                if (var->is_static && var->base.value.file != NULL
                    && var->name.value != NULL) {
                    register_static_decl(var->name.value, var->base.value.file);
                    }
                break;
            }
            case AST_ARRAY_DECL: {
                AST_ArrayDecl* arr = (AST_ArrayDecl*)node;
                if (arr->modifier == 3 && arr->base.value.file != NULL
                    && arr->name != NULL) {
                    register_static_decl(arr->name, arr->base.value.file);
                    }
                break;
            }
            default:
                break;
        }
        node = node->next;
    }
    add_function();
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

/* 泛型变型（协变/逆变）的声明处检查。
   协变 <+T>：T 只能出现在返回位置（生产者），不能作为方法参数（消费者）。
   逆变 <-T>：T 只能出现在参数位置，不能作为返回类型。
   字段允许使用（内部存储），但方法签名必须满足上述方向。 */
static void check_variance_usage(AST_ClassDef* cd) {
    if (cd == NULL || !cd->is_generic) return;
    if (cd->type_param_count <= 0 || cd->type_params == NULL) return;

    for (int i = 0; i < cd->type_param_count; i++) {
        TypeParam* tp = &cd->type_params[i];
        if (tp->variance == 0 || tp->name == NULL) continue;

        for (ASTNode* m = cd->members; m != NULL; m = m->next) {
            if (m->type != AST_FUNCTION) continue;
            AST_Function* f = (AST_Function*)m;
            if (f->name.value == NULL) continue;
            /* 构造函数不受变型约束：创建具体类型时必须能接收 T */
            if (cd->name != NULL && strcmp(f->name.value, cd->name) == 0) continue;

            if (tp->variance > 0) {
                bool used_as_param = false;
                for (ASTNode* p = f->Parameter; p != NULL; p = p->next) {
                    if (p->type != AST_VAR && p->type != AST_UNINIT_VAR) continue;
                    AST_Var* pv = (AST_Var*)p;
                    if (pv->dataType == DATA_IDENTIFIER && pv->internal_name != NULL
                        && strcmp(pv->internal_name, tp->name) == 0) {
                        used_as_param = true;
                        break;
                    }
                }
                if (used_as_param) {
                    ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                          SEMLOC(f->name),
                          "covariant type parameter '%s' cannot be used as a parameter type of '%s' (a covariant parameter only produces values)",
                          tp->name, f->name.value);
                }
            } else {
                if (f->return_type == DATA_IDENTIFIER
                    && f->return_class_name != NULL
                    && strcmp(f->return_class_name, tp->name) == 0) {
                    ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                          SEMLOC(f->name),
                          "contravariant type parameter '%s' cannot be the return type of '%s' (a contravariant parameter only consumes values)",
                          tp->name, f->name.value);
                }
            }
        }
    }
}

static void resolve_class_members(AST_ClassDef* cd, ClassEntry* cls) {
    if (cls->members_resolved) return;

    {
        ASTNode* pre = cd->members;
        while (pre) {
            if (pre->type == AST_FUNCTION) {
                AST_Function* f = (AST_Function*)pre;
                if (f->Code != NULL && f->Return == NULL) {
                    collect_function_return(f->Code, f);
                }
            }
            pre = pre->next;
        }
    }

    char* saved_class_name = current_class_name;
    ClassEntry* saved_class = current_class;
    current_class_name = cls->name;
    current_class = cls;

    Symbol* cls_sym = create_symbol(
        (Token){.value = cd->name, .type = TOKEN_IDENTIFIER},
        DATA_IDENTIFIER, cd->name, generate_internal_name());
    cls_sym->class_name = strdup(cd->name);
    insert_symbol(cls_sym);

    {
        ASTNode* m = cd->members;
        while (m) {
            if (m->type == AST_ENUM_DEF) {
                AST_EnumDef* ed = (AST_EnumDef*)m;
                if (ed->name != NULL) {
                    if (cls->enum_count >= cls->enum_capacity) {
                        cls->enum_capacity = cls->enum_capacity ? cls->enum_capacity * 2 : 8;
                        cls->enum_names = realloc(cls->enum_names,
                                                  sizeof(char*) * cls->enum_capacity);
                        if (!cls->enum_names) OUT_OF_MEMORY();
                    }
                    cls->enum_names[cls->enum_count++] = strdup(ed->name);
                }
                ASTNode* saved_next = m->next;
                m->next = NULL;
                analyze_node(m);
                m->next = saved_next;
            } else if (m->type == AST_CLASS_DEF) {
                AST_ClassDef* inner = (AST_ClassDef*)m;
                ClassEntry* inner_cls = find_class(inner->name);
                if (inner_cls) {
                    resolve_class_members(inner, inner_cls);
                }
            }
            m = m->next;
        }
    }

    enter_scope();

    Symbol* this_sym = create_symbol(
        (Token){.value = "this", .type = TOKEN_IDENTIFIER},
        DATA_IDENTIFIER, "this", strdup("_this"));
    this_sym->class_name = strdup(cd->name);
    insert_symbol(this_sym);

    cls->has_outer = (cls->outer_class_name != NULL);
    if (cls->has_outer) {
        Symbol* outer_sym = create_symbol(
            (Token){.value = "_outer", .type = TOKEN_IDENTIFIER},
            DATA_IDENTIFIER, "_outer", strdup("_outer"));
        outer_sym->class_name = strdup(cls->outer_class_name);
        insert_symbol(outer_sym);
    }

    for (int i = 0; i < cls->member_count; i++) {
        MemberInfo* mi = cls->members[i];
        if (mi->is_method) continue;

        Symbol* field_sym = create_symbol(
            (Token){.value = mi->name, .type = TOKEN_IDENTIFIER},
            mi->type, mi->name, strdup(mi->name));
        field_sym->is_field = true;
        field_sym->field_offset = mi->offset;
        field_sym->field_modifier = mi->field_modifier;
        field_sym->is_static = mi->is_static;
        insert_symbol(field_sym);
    }

    if (cls->has_outer) {
        ClassEntry* outer_cls = find_class(cls->outer_class_name);
        if (outer_cls) {
            for (int oi = 0; oi < outer_cls->member_count; oi++) {
                MemberInfo* omi = outer_cls->members[oi];
                if (omi->is_method || omi->is_static) continue;
                Symbol* existing = NULL;
                int h = hash_function(omi->name, current_scope->table->size);
                for (Symbol* s = current_scope->table->buckets[h]; s; s = s->next) {
                    if (s->key && strcmp(s->key, omi->name) == 0) {
                        existing = s;
                        break;
                    }
                }
                if (existing) continue;
                Symbol* fallback = create_symbol(
                    (Token){.value = omi->name, .type = TOKEN_IDENTIFIER},
                    omi->type, omi->name, strdup(omi->name));
                fallback->is_field = true;
                fallback->field_offset = omi->offset;
                fallback->is_outer_field_ref = true;
                insert_symbol(fallback);
            }
        }
    }

    ClassEntry** parent_entries = malloc(sizeof(ClassEntry*) * (size_t)cd->parent_count);
    bool all_parents_valid = true;
    for (int i = 0; i < cd->parent_count; i++) {
        bool p_deferred = false;
        if (cd->type_param_count > 0 && cd->parent_names[i]) {
            const char* dp = strchr(cd->parent_names[i], '$');
            if (dp) {
                char* ap = strdup(dp + 1);
                char* st = ap;
                for (char* q = ap; ; q++) {
                    if (*q == '_' || *q == '\0') {
                        char sv = *q;
                        *q = '\0';
                        for (int tp = 0; tp < cd->type_param_count; tp++) {
                            if (cd->type_params[tp].name
                                && strcmp(st, cd->type_params[tp].name) == 0) {
                                p_deferred = true;
                                break;
                            }
                        }
                        *q = sv;
                        if (p_deferred || sv == '\0') break;
                        st = q + 1;
                    }
                }
                free(ap);
            }
        }
        if (p_deferred) {
            /* 鐖剁被瀹炲弬鏄瓙绫荤被鍨嬪弬鏁帮紙濡? Layer<T> : Box<T>锛夈?傜湡瀹炲疄渚嬪寲鐖剁被鍚?
               瑕佸埌 Layer$int 鎵嶇煡閬擄紝杩欓噷鍏堟寕鍒版硾鍨嬪熀绫? Box 涓婏紝璁╂硾鍨嬬被浣撻噷鐨?
               this.v 鑳芥煡鍒扮户鎵垮瓧娈碉紱瀛楁鍋忕Щ鎸夌埗绫诲竷灞?绠楋紙鐖剁被瀛楁鍦ㄥ墠锛夛紝
               涓庡疄渚嬪寲鍚庝竴鑷淬?? */
            parent_entries[i] = find_class_generic_base(cd->parent_names[i]);
            if (!parent_entries[i]) all_parents_valid = false;
            continue;
        }
        parent_entries[i] = resolve_generic_parent(cd->parent_names[i]);
        if (!parent_entries[i]) {
            Token err_tok = {.line = cd->base.value.line, .column = cd->base.value.column};
            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                  SEMLOC(err_tok),
                  "parent class '%s' not found",
                  cd->parent_names[i]);
            all_parents_valid = false;
        }
    }

    bool header = (strcmp(cd->name, "ERROR") == 0);
    if (!header) {
        for (int i = 0; i < cd->parent_count; i++) {
            ClassEntry* p = parent_entries[i];
            if (p && p->has_type_header) { header = true; break; }
        }
    }
    cls->has_type_header = header;

    cls->is_function_class = cd->is_function_class;

    ClassEntry* vparent = NULL;
    for (int i = 0; i < cd->parent_count; i++) {
        if (parent_entries[i] && parent_entries[i]->vtable_size > 0) {
            vparent = parent_entries[i];
            break;
        }
    }
    if (vparent) {
        cls->vtable_size = vparent->vtable_size;
        cls->vtable_method_names = malloc(
            sizeof(char*) * (size_t)cls->vtable_size);
        if (!cls->vtable_method_names) OUT_OF_MEMORY();
        for (int i = 0; i < cls->vtable_size; i++) {
            cls->vtable_method_names[i] =
                vparent->vtable_method_names[i]
                ? strdup(vparent->vtable_method_names[i]) : NULL;
        }
        cls->next_vtable_index = cls->vtable_size;
    } else {
        cls->vtable_size = 0;
        cls->vtable_method_names = NULL;
        cls->next_vtable_index = 0;
    }

    for (ASTNode* n = cd->members; n; n = n->next) {
        if (n->type != AST_FUNCTION) continue;
        AST_Function* f = (AST_Function*)n;
        if (!f->is_override && !f->is_virtual && !f->is_static && !f->is_native
            && cd->name && f->name.value
            && strcmp(f->name.value, cd->name) != 0) {
            for (int pi = 0; pi < cd->parent_count; pi++) {
                if (!parent_entries[pi] || !parent_entries[pi]->generic_ast) continue;
                AST_ClassDef* pcd = parent_entries[pi]->generic_ast;
                bool parent_has = false;
                for (ASTNode* pm = pcd->members; pm; pm = pm->next) {
                    if (pm->type != AST_FUNCTION) continue;
                    AST_Function* pf = (AST_Function*)pm;
                    if (!pf->name.value) continue;
                    if (pcd->name && strcmp(pf->name.value, pcd->name) == 0) continue;
                    if (strcmp(pf->name.value, f->name.value) == 0) { parent_has = true; break; }
                }
                if (parent_has) {
                    WARNING(ERR_SEM_TYPE_MISMATCH, SEMLOC(f->name),
                            "method '%s' hides parent method '%s' but neither is marked "
                            "'virtual'/'override'; calls through a parent-typed variable "
                            "will use the parent version. Add 'virtual' to the parent "
                            "method and 'override' here to enable dynamic dispatch",
                            f->name.value, f->name.value);
                    break;
                }
            }
        }
        if (f->is_override) {
            int slot = -1;
            for (int i = 0; i < cls->vtable_size && slot < 0; i++) {
                if (cls->vtable_method_names[i] &&
                    strcmp(cls->vtable_method_names[i],
                           f->name.value) == 0) {
                    slot = i;
                }
            }
            if (slot < 0) {
                ERROR(ERR_SEM_METHOD_NOT_FOUND, SEMLOC(f->name),
                      "override method '%s' has no matching virtual "
                      "method in parent class", f->name.value);
                f->vtable_index = -1;
            } else {
                f->vtable_index = slot;
            }
        } else if (f->is_virtual) {
            if (f->is_native) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(f->name),
                      "method '%s' cannot be both 'native' and 'virtual'",
                      f->name.value);
            }
            if (f->is_static) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(f->name),
                      "method '%s' cannot be both 'static' and 'virtual'",
                      f->name.value);
            }
            f->vtable_index = cls->next_vtable_index++;
            cls->vtable_method_names = realloc(
                cls->vtable_method_names,
                sizeof(char*) * (size_t)cls->next_vtable_index);
            if (!cls->vtable_method_names) OUT_OF_MEMORY();
            cls->vtable_method_names[cls->next_vtable_index - 1] =
                strdup(f->name.value);
            cls->vtable_size = cls->next_vtable_index;
        }
    }

    int off = 8;
    if (all_parents_valid) {
        for (int i = 0; i < cd->parent_count; i++) {
            off = inherit_from_parent(cls, parent_entries[i], off);
        }
    }

    int mcount = 0;
    for (ASTNode* n = cd->members; n; n = n->next) mcount++;

    ASTNode** marray = malloc(sizeof(ASTNode*) * (size_t)(mcount + 1));
    if (!marray) OUT_OF_MEMORY();
    {
        int i = 0;
        ASTNode* n = cd->members;
        while (n) {
            ASTNode* saved_next = n->next;
            n->next = NULL;
            marray[i++] = n;
            n = saved_next;
        }
        marray[mcount] = NULL;
    }

    ASTNode* extra_head = NULL;
    ASTNode* extra_tail = NULL;

    #define APPEND_EXTRA(node) do {            \
        (node)->next = NULL;                   \
        if (!extra_head) {                      \
            extra_head = extra_tail = (node);   \
        } else {                                \
            extra_tail->next = (node);          \
            extra_tail = (node);                \
        }                                       \
    } while(0)

    for (int mi = 0; mi < mcount; mi++) {
        ASTNode* m = marray[mi];
        if (!m) continue;

        if (m->type == AST_VAR || m->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)m;
            char* internal = generate_internal_name();
            int type_param_idx = -1;
            if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
                bool is_type_param = false;
                type_param_idx = -1;
                for (int i = 0; i < cd->type_param_count; i++) {
                    if (strcmp(var->internal_name, cd->type_params[i].name) == 0) {
                        is_type_param = true;
                        type_param_idx = i;
                        var->dataType = DATA_IDENTIFIER;
                        break;
                    }
                }
                if (!is_type_param) {
                    ClassEntry* field_cls = find_class(var->internal_name);
                    if (!field_cls && cls->outer_class_name != NULL) {
                        char full[256];
                        snprintf(full, sizeof(full), "%s_%s",
                                 cls->outer_class_name, var->internal_name);
                        field_cls = find_class(full);
                        if (field_cls != NULL) {
                            free(var->internal_name);
                            var->internal_name = strdup(full);
                        }
                    }
                    if (!field_cls) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(var->name),
                              "unknown type '%s' for field '%s'",
                              var->internal_name, var->name.value);
                    }
                }
            }
            Symbol* sym = create_symbol(var->name, var->dataType,
                                        var->name.value, internal);
            sym->ptr_depth = var->ptr_depth;
            sym->is_unsafe = var->is_unsafe;
            sym->is_const = var->is_const;
            if (var->is_const && var->Value) {
                bool is_int_const =
                    (var->dataType == DATA_INT  || var->dataType == DATA_UINT ||
                     var->dataType == DATA_CHAR || var->dataType == DATA_BOOL ||
                     var->dataType == DATA_INT8 || var->dataType == DATA_INT16 ||
                     var->dataType == DATA_INT32 || var->dataType == DATA_INT64 ||
                     var->dataType == DATA_UINT8 || var->dataType == DATA_UINT16 ||
                     var->dataType == DATA_UINT32 || var->dataType == DATA_UINT64);
                if (is_int_const) {
                    int64_t cval = 0;
                    bool of = false;
                    if (eval_constant_expr(var->Value, &cval, &of)) {
                        if (of) {
                            ERROR(ERR_SEM_CONST_OVERFLOW,
                                  SEMLOC(var->name),
                                  "'const' field '%s' overflows 32-bit integer range",
                                  var->name.value);
                        } else {
                            char cbuf[32];
                            snprintf(cbuf, sizeof(cbuf), "%lld", (long long)cval);
                            sym->const_value_str = strdup(cbuf);
                        }
                    }
                }
            }
            if (var->func_ptr_sig) {
                sym->func_sig = deep_copy_func_sig(var->func_ptr_sig);
            }
            if (var->internal_name != NULL) {
                ClassEntry* cls_entry = find_class(var->internal_name);
                if (cls_entry) {
                    sym->class_name = strdup(var->internal_name);
                }
            }
            insert_symbol(sym);
            symbol_copy_generic_args(sym, var);
            if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
                ClassEntry* cls = find_class(var->internal_name);
                if (cls && cls->is_generic && var->generic_type_arg_count > 0) {
                    ClassEntry* inst_cls = instantiate_generic_class(
                        cls, var->generic_type_args, var->generic_type_arg_names,
                        var->generic_type_arg_count);
                    if (inst_cls) {
                        free(var->internal_name);
                        var->internal_name = strdup(inst_cls->name);
                        var->dataType = DATA_IDENTIFIER;
                        var->total_bytes = inst_cls->total_size;

                        free(sym->internal_name);
                        sym->internal_name = strdup(inst_cls->name);
                        sym->total_bytes = inst_cls->total_size;
                        sym->is_value_type = true;
                        if (sym->class_name) free(sym->class_name);
                        sym->class_name = strdup(inst_cls->name);
                    }
                }
            }
            char* field_class_name = NULL;
            if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
                ClassEntry* fc = find_class(var->internal_name);
                if (fc) field_class_name = strdup(var->internal_name);
            }
            var->internal_name = strdup(internal);
            var->type_param_index = type_param_idx;
            free(internal);

            bool field_conflict = false;
            for (int j = 0; j < cls->member_count; j++) {
                if (!cls->members[j]->is_method &&
                    strcmp(cls->members[j]->name, var->name.value) == 0) {
                    ERROR(ERR_SEM_DUPLICATE_VAR,
                          SEMLOC(var->name),
                          "field '%s' conflicts with inherited field",
                          var->name.value);
                    field_conflict = true;
                    break;
                }
            }
            if (field_conflict) continue;
            DataType dt = var->dataType;
            int sz = S_get_type_size(dt);
            if (cls->member_count >= cls->member_capacity) {
                cls->member_capacity = cls->member_capacity ? cls->member_capacity * 2 : 8;
                cls->members = realloc(cls->members,
                                       sizeof(MemberInfo*) * (size_t)cls->member_capacity);
            }
            MemberInfo* mem_info = calloc(1, sizeof(MemberInfo));
            mem_info->name = strdup(var->name.value);
            mem_info->type = dt;
            mem_info->is_method = false;
            mem_info->inherited_from = NULL;
            mem_info->field_modifier = var->modifier;
            mem_info->type_param_index = type_param_idx;
            mem_info->is_unsafe = var->is_unsafe;
            mem_info->ptr_depth = var->ptr_depth;
            if (var->Value && var->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)var->Value;
                if (lit->base.value.value) {
                    mem_info->init_value_str = strdup(lit->base.value.value);
                }
            }
            if (var->func_ptr_sig) {
                mem_info->func_sig = deep_copy_func_sig(var->func_ptr_sig);
            }

            if (var->is_static) {
                mem_info->is_static = true;
                mem_info->is_const = var->is_const;
                if (var->is_const && var->Value) {
                    bool is_int_const =
                        (var->dataType == DATA_INT  || var->dataType == DATA_UINT ||
                         var->dataType == DATA_CHAR || var->dataType == DATA_BOOL ||
                         var->dataType == DATA_INT8 || var->dataType == DATA_INT16 ||
                         var->dataType == DATA_INT32 || var->dataType == DATA_INT64 ||
                         var->dataType == DATA_UINT8 || var->dataType == DATA_UINT16 ||
                         var->dataType == DATA_UINT32 || var->dataType == DATA_UINT64);
                    if (is_int_const) {
                        int64_t cval = 0;
                        bool of = false;
                        if (eval_constant_expr(var->Value, &cval, &of)) {
                            if (of) {
                                ERROR(ERR_SEM_CONST_OVERFLOW,
                                      SEMLOC(var->name),
                                      "'const' field '%s' overflows 32-bit integer range",
                                      var->name.value);
                            } else {
                                char cbuf[32];
                                snprintf(cbuf, sizeof(cbuf), "%lld", (long long)cval);
                                mem_info->const_value_str = strdup(cbuf);
                            }
                        } else {
                            ERROR(ERR_SEM_INVALID_INDEX,
                                  SEMLOC(var->name),
                                  "'const' field '%s' must be initialized with "
                                  "a compile-time constant expression",
                                  var->name.value);
                        }
                    }
                }
                mem_info->offset = -1;
                mem_info->class_name = field_class_name;
                int len = strlen(cd->name) + 1 + strlen(var->name.value) + 1;
                char* global_name = malloc(len);
                snprintf(global_name, len, "%s_%s", cd->name, var->name.value);
                mem_info->field_class_name = strdup(global_name);
                free(var->internal_name);
                var->internal_name = strdup(global_name);

                Symbol* global_sym = create_symbol(
                    var->name, dt,
                    var->name.value, strdup(global_name));
                global_sym->is_static = true;
                global_sym->is_const = var->is_const;
                if (mem_info->const_value_str) {
                    global_sym->const_value_str =
                        strdup(mem_info->const_value_str);
                }
                Scope* saved = current_scope;
                while (current_scope->parent) current_scope = current_scope->parent;
                insert_symbol(global_sym);
                current_scope = saved;
                free(global_name);
            } else {
                mem_info->is_static = false;
                mem_info->offset = off;
                mem_info->field_class_name = NULL;
                mem_info->class_name = field_class_name;
                off += sz;
            }

            mem_info->generic_type_arg_count = var->generic_type_arg_count;
            if (var->generic_type_arg_count > 0) {
                mem_info->generic_type_args = malloc(
                    sizeof(DataType) * (size_t)var->generic_type_arg_count);
                if (!mem_info->generic_type_args) OUT_OF_MEMORY();
                memcpy(mem_info->generic_type_args, var->generic_type_args,
                       sizeof(DataType) * (size_t)var->generic_type_arg_count);
                mem_info->generic_type_arg_names = calloc(
                    (size_t)var->generic_type_arg_count, sizeof(char*));
                if (!mem_info->generic_type_arg_names) OUT_OF_MEMORY();
                for (int gi = 0; gi < var->generic_type_arg_count; gi++)
                    mem_info->generic_type_arg_names[gi] =
                        (var->generic_type_arg_names && var->generic_type_arg_names[gi])
                        ? strdup(var->generic_type_arg_names[gi]) : NULL;
            }

            cls->members[cls->member_count++] = mem_info;

            Symbol* field_sym = lookup_symbol(var->name.value);
            if (field_sym) {
                field_sym->is_field = true;
                field_sym->type_param_index = type_param_idx;
                if (var->is_static) {
                    field_sym->is_static = true;
                    field_sym->field_offset = -1;
                    free(field_sym->internal_name);
                    field_sym->internal_name = strdup(var->internal_name);
                } else {
                    field_sym->is_static = false;
                    field_sym->field_offset = mem_info->offset;
                }
                field_sym->field_modifier = var->modifier;
            }

            if (var->modifier == 2) {
                const char* fname = var->name.value;
                int off_val = mem_info->offset;

                char* gname = make_getter_name(fname);
                char* sname = make_setter_name(fname);

                bool has_getter = false;
                bool has_setter = false;
                for (int j = 0; j < cls->member_count; j++) {
                    if (cls->members[j]->is_method) {
                        if (strcmp(cls->members[j]->name, gname) == 0)
                            has_getter = true;
                        if (strcmp(cls->members[j]->name, sname) == 0)
                            has_setter = true;
                    }
                }

                for (int j = mi + 1; j < mcount; j++) {
                    ASTNode* mn = marray[j];
                    if (mn && mn->type == AST_FUNCTION) {
                        AST_Function* mf = (AST_Function*)mn;
                        if (strcmp(mf->name.value, gname) == 0)
                            has_getter = true;
                        if (strcmp(mf->name.value, sname) == 0)
                            has_setter = true;
                    }
                }

                if (!has_getter) {
                    AST_Function* getter = create_getter_ast(
                        cd->name, fname, dt, off_val, field_class_name);
                    if (getter) {
                        APPEND_EXTRA((ASTNode*)getter);
                        add_function_with_params(getter->name.value, getter);
                        if (cls->member_count >= cls->member_capacity) {
                            cls->member_capacity = cls->member_capacity ? cls->member_capacity * 2 : 8;
                            cls->members = realloc(cls->members,
                                sizeof(MemberInfo*) * (size_t)cls->member_capacity);
                        }
                        MemberInfo* gmi = calloc(1, sizeof(MemberInfo));
                        gmi->name = strdup(getter->name.value);
                        gmi->type = dt;
                        gmi->is_method = true;
                        gmi->func = getter;
                        gmi->offset = -1;
                        gmi->inherited_from = NULL;
                        gmi->is_static = false;
                        cls->members[cls->member_count++] = gmi;
                    }
                }
                if (!has_setter) {
                    AST_Function* setter = create_setter_ast(
                        cd->name, fname, dt, off_val, field_class_name);
                    if (setter) {
                        APPEND_EXTRA((ASTNode*)setter);
                        add_function_with_params(setter->name.value, setter);
                        if (cls->member_count >= cls->member_capacity) {
                            cls->member_capacity = cls->member_capacity ? cls->member_capacity * 2 : 8;
                            cls->members = realloc(cls->members,
                                sizeof(MemberInfo*) * (size_t)cls->member_capacity);
                        }
                        MemberInfo* smi = calloc(1, sizeof(MemberInfo));
                        smi->name = strdup(setter->name.value);
                        smi->type = DATA_VOID;
                        smi->is_method = true;
                        smi->func = setter;
                        smi->offset = -1;
                        smi->inherited_from = NULL;
                        smi->is_static = false;
                        cls->members[cls->member_count++] = smi;
                    }
                }

                free(gname);
                free(sname);
            }
        } else if (m->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)m;

            bool all_dynamic = true;
            int total_elements = 1;
            for (int i = 0; i < arr->dim_count; i++) {
                if (arr->dim_sizes[i] > 0) {
                    all_dynamic = false;
                    total_elements *= arr->dim_sizes[i];
                }
            }

            char* internal = generate_internal_name();
            Symbol* sym = create_symbol(
                (Token){ .value = arr->name, .type = TOKEN_IDENTIFIER },
                arr->elementType, arr->name, internal);
            sym->dim_count = arr->dim_count;
            if (arr->dim_count > 0) {
                sym->dim_sizes = malloc(sizeof(int) * (size_t)arr->dim_count);
                memcpy(sym->dim_sizes, arr->dim_sizes,
                       sizeof(int) * (size_t)arr->dim_count);
            }
            if (all_dynamic) {
                sym->is_dynamic_array = true;
                sym->ptr_depth = 1;
                sym->total_bytes = 8;
            } else {
                sym->total_bytes = total_elements
                                   * get_type_size(arr->elementType);
                sym->is_value_type = true;
            }
            sym->is_field = true;
            sym->is_const = arr->is_const;
            if (arr->elementType == DATA_IDENTIFIER
                && arr->element_class_name != NULL) {
                sym->class_name = strdup(arr->element_class_name);
            }

            MemberInfo* mem_info = calloc(1, sizeof(MemberInfo));
            mem_info->name = strdup(arr->name);
            mem_info->type = arr->elementType;
            mem_info->is_method = false;
            mem_info->inherited_from = NULL;
            mem_info->field_modifier = arr->modifier;
            mem_info->is_const = arr->is_const;
            mem_info->dim_count = arr->dim_count;
            if (arr->elementType == DATA_IDENTIFIER
                && arr->element_class_name != NULL) {
                mem_info->class_name = strdup(arr->element_class_name);
            }
            if (arr->dim_count > 0) {
                mem_info->dim_sizes = malloc(sizeof(int) * (size_t)arr->dim_count);
                memcpy(mem_info->dim_sizes, arr->dim_sizes,
                       sizeof(int) * (size_t)arr->dim_count);
            }
            if (arr->modifier == 3) {
                mem_info->is_static = true;
                mem_info->offset = -1;
                int gname_len = (int)strlen(cd->name) + 1
                              + (int)strlen(arr->name) + 1;
                char* global_name = malloc((size_t)gname_len);
                if (!global_name) OUT_OF_MEMORY();
                snprintf(global_name, (size_t)gname_len,
                         "%s_%s", cd->name, arr->name);
                mem_info->field_class_name = strdup(global_name);
                if (arr->internal_name) free(arr->internal_name);
                arr->internal_name = strdup(global_name);
                if (sym->internal_name) free(sym->internal_name);
                sym->internal_name = strdup(global_name);
                sym->is_static = true;
                Scope* saved_scope = current_scope;
                while (current_scope->parent) current_scope = current_scope->parent;
                insert_symbol(sym);
                current_scope = saved_scope;
                free(global_name);
            } else {
                insert_symbol(sym);
                if (all_dynamic) {
                    mem_info->is_dynamic_array = true;
                    mem_info->ptr_depth = 1;
                    mem_info->offset = off;
                    off += 8;
                } else {
                    mem_info->is_dynamic_array = false;
                    mem_info->ptr_depth = 0;
                    mem_info->offset = off;
                    off += sym->total_bytes;
                }
                arr->internal_name = strdup(internal);
            }
            free(internal);

            if (cls->member_count >= cls->member_capacity) {
                cls->member_capacity = cls->member_capacity
                                       ? cls->member_capacity * 2 : 8;
                cls->members = realloc(cls->members,
                    sizeof(MemberInfo*) * (size_t)cls->member_capacity);
            }
            cls->members[cls->member_count++] = mem_info;
        } else if (m->type == AST_FUNCTION) {
            AST_Function* func = (AST_Function*)m;

            if (!func->is_static)
            {
                AST_Var* this_param = calloc(1, sizeof(AST_Var));
                this_param->base.type = AST_VAR;
                this_param->base.value = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
                this_param->type = TOKEN_IDENTIFIER;
                this_param->name = (Token){.value = "this", .type = TOKEN_IDENTIFIER};
                this_param->dataType = DATA_IDENTIFIER;
                this_param->internal_name = strdup("_this");
                this_param->type_param_index = -1;
                this_param->base.next = func->Parameter;
                func->Parameter = (ASTNode*)this_param;
                func->parameterCount++;
                if (func->parameterCount >= func->max_parameters) {
                    func->max_parameters = (func->max_parameters == 0) ? 8 : func->max_parameters * 2;
                    func->parameterType = realloc(func->parameterType,
                        sizeof(DataType) * (size_t)func->max_parameters);
                }
                memmove(func->parameterType + 1, func->parameterType,
                        sizeof(DataType) * (size_t)(func->parameterCount - 1));
                func->parameterType[0] = DATA_IDENTIFIER;

                if (cls->has_outer) {
                    AST_Var* outer_param = calloc(1, sizeof(AST_Var));
                    outer_param->base.type = AST_VAR;
                    outer_param->base.value = (Token){.value = "_outer", .type = TOKEN_IDENTIFIER};
                    outer_param->type = TOKEN_IDENTIFIER;
                    outer_param->name = (Token){.value = "_outer", .type = TOKEN_IDENTIFIER};
                    outer_param->dataType = DATA_IDENTIFIER;
                    outer_param->internal_name = strdup("_outer");
                    outer_param->type_param_index = -1;
                    outer_param->base.next = this_param->base.next;
                    this_param->base.next = (ASTNode*)outer_param;
                    func->parameterCount++;
                    if (func->parameterCount >= func->max_parameters) {
                        func->max_parameters = (func->max_parameters == 0) ? 8 : func->max_parameters * 2;
                        func->parameterType = realloc(func->parameterType,
                            sizeof(DataType) * (size_t)func->max_parameters);
                    }
                    memmove(func->parameterType + 2, func->parameterType + 1,
                            sizeof(DataType) * (size_t)(func->parameterCount - 2));
                    func->parameterType[1] = DATA_IDENTIFIER;
                }
            }

            for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                if (fe->func != func) continue;
                if (func->has_varargs) {
                    fe->param_count = func->vararg_param_index;
                } else {
                    fe->param_count = func->parameterCount;
                }
                if (fe->param_types) {
                    free(fe->param_types);
                    fe->param_types = NULL;
                }
                if (fe->param_count > 0 && func->parameterType) {
                    fe->param_types = malloc(sizeof(DataType) * (size_t)fe->param_count);
                    if (!fe->param_types) OUT_OF_MEMORY();
                    memcpy(fe->param_types, func->parameterType,
                           sizeof(DataType) * (size_t)fe->param_count);
                }
            }

            if (!func->internal_name) {
                func->internal_name = generate_method_mangled_name(cd->name, func);
            }

            if (func->is_native) {
                semantics_register_native_symbol(func->internal_name);
            }

            for (int j = 0; j < cls->member_count; j++) {
                if (cls->members[j]->is_method &&
                    strcmp(cls->members[j]->name, func->name.value) == 0) {
                    if (function_signature_identical(cls->members[j]->func, func)) {
                        ERROR(ERR_SEM_DUPLICATE_VAR,
                              SEMLOC(func->name),
                              "duplicate method '%s'", func->name.value);
                    } else if (function_signature_conflict(cls->members[j]->func, func)) {
                        ERROR(ERR_SEM_DUPLICATE_VAR,
                              SEMLOC(func->name),
                              "duplicate method '%s'", func->name.value);
                    }
                }
            }

            if (cls->member_count >= cls->member_capacity) {
                cls->member_capacity = cls->member_capacity ? cls->member_capacity * 2 : 8;
                cls->members = realloc(cls->members,
                                       sizeof(MemberInfo*) * (size_t)cls->member_capacity);
            }
            ASTNode* pn = func->Parameter;
            while (pn) {
                if (pn->type == AST_VAR || pn->type == AST_UNINIT_VAR
                    || pn->type == AST_VAR_ASS) {
                    AST_Var* pv = (AST_Var*)pn;
                    if (pv->dataType == DATA_IDENTIFIER && pv->internal_name) {
                        for (int i = 0; i < cd->type_param_count; i++) {
                            if (strcmp(pv->internal_name,
                                       cd->type_params[i].name) == 0) {
                                pv->type_param_index = i;
                                break;
                            }
                        }
                    }
                }
                pn = pn->next;
            }
            MemberInfo* mi = calloc(1, sizeof(MemberInfo));
            mi->name = strdup(func->name.value);
            mi->type = func->Return ? func->Return->returnType : DATA_VOID;
            mi->is_method = true;
            mi->func = func;
            mi->is_const_method = func->is_const_method;
            mi->offset = -1;
            mi->inherited_from = NULL;
            mi->is_static = func->is_static;
            /* 2.6 诊断：toString 是保留名 */
            if (!mi->is_static && func->name.value
                && strcmp(func->name.value, "toString") == 0) {
                WARNING(ERR_SEM_TYPE_MISMATCH, SEMLOC(func->name),
                        "toString is a reserved name; the compiler rewrites .toString() into array-to-string conversion, so this method is unreachable. Rename it (e.g. asText).");
            }
            cls->members[cls->member_count++] = mi;

            if (cls->short_name != NULL
                && strcmp(func->name.value, cls->short_name) == 0) {
                cls->constructor = func;
                if (cd->parent_count > 0) {
                    ClassEntry* pcls = find_class(cd->parent_names[0]);
                    if (pcls && pcls->constructor) {
                        /* 隐式调用父类构造：第一个参数是 this，其余要转发
                           派生类构造自己的形参。以前只传了 this，父类构造
                           拿到的 msg 就是 this 指针，父类字段被写坏。 */
                        int dcount = func->parameterCount - 1;
                        int pneed = pcls->constructor->parameterCount - 1;
                        if (pneed < 0) pneed = 0;
                        if (pneed <= dcount) {
                            AST_Literal* supercall = calloc(1, sizeof(AST_Literal));
                            supercall->base.type = AST_LITERAL;
                            supercall->base.next = func->Code;
                            supercall->base.value.type = TOKEN_IDENTIFIER;
                            supercall->base.value.value =
                                strdup(pcls->constructor->internal_name);
                            supercall->dataType = DATA_VOID;
                            supercall->internal_name =
                                strdup(pcls->constructor->internal_name);
                            supercall->is_call_function = true;
                            supercall->call_function_Parameter = NULL;
                            supercall->call_function_ParameterCount = 0;
                            supercall->call_result_class_name = NULL;

                            AST_This* this_node = calloc(1, sizeof(AST_This));
                            this_node->base.type = AST_THIS;
                            this_node->base.next = NULL;
                            ASTNode* arg_head = (ASTNode*)this_node;
                            ASTNode* arg_tail = arg_head;

                            ASTNode* dp = func->Parameter;
                            if (dp) dp = dp->next;
                            for (int ai = 0; ai < pneed && dp; ai++) {
                                AST_Literal* arg = calloc(1, sizeof(AST_Literal));
                                arg->base.type = AST_LITERAL;
                                arg->base.value.type = TOKEN_IDENTIFIER;
                                arg->dataType = DATA_INT;
                                if (dp->type == AST_VAR
                                    || dp->type == AST_UNINIT_VAR
                                    || dp->type == AST_VAR_ASS) {
                                    AST_Var* dv = (AST_Var*)dp;
                                    const char* pname = dv->name.value
                                        ? dv->name.value
                                        : dv->base.value.value;
                                    arg->base.value.value = strdup(pname ? pname : "");
                                    arg->dataType = dv->dataType;
                                } else {
                                    arg->base.value.value = strdup("");
                                }
                                arg_tail->next = (ASTNode*)arg;
                                arg_tail = (ASTNode*)arg;
                                dp = dp->next;
                            }

                            supercall->call_function_Parameter = arg_head;
                            supercall->call_function_ParameterCount = 1 + pneed;

                            func->Code = (ASTNode*)supercall;
                        }
                    }
                }
            }

            Symbol* func_sym = lookup_symbol(func->name.value);
            if (!func_sym) {
                func_sym = calloc(1, sizeof(Symbol));
                func_sym->key = strdup(func->name.value);
                func_sym->value.type = TOKEN_IDENTIFIER;
                func_sym->value.value = strdup(func->name.value);
                func_sym->type = DATA_INT;
                func_sym->internal_name = strdup(func->internal_name);
                func_sym->is_function = false;
                insert_symbol(func_sym);
            }
        }
    }
    skip_instance_field:

    ASTNode* new_head = NULL;
    ASTNode* new_tail = NULL;

    for (int i = 0; i < mcount; i++) {
        if (!new_head) {
            new_head = new_tail = marray[i];
        } else {
            new_tail->next = marray[i];
            new_tail = marray[i];
        }
    }

    if (extra_head) {
        if (!new_head) {
            new_head = extra_head;
        } else {
            new_tail->next = extra_head;
        }
    }

    cd->members = new_head;
    free(marray);
    #undef APPEND_EXTRA

    if (all_parents_valid) {
        for (int p = 0; p < cd->parent_count; p++) {
            ClassEntry* parent = parent_entries[p];
            if (!parent) continue;

            for (int i = 0; i < parent->member_count; i++) {
                MemberInfo* pmi = parent->members[i];
                if (!pmi->is_method) continue;

                bool overridden = false;
                for (int j = 0; j < cls->member_count; j++) {
                    if (cls->members[j]->is_method &&
                        strcmp(cls->members[j]->name, pmi->name) == 0 &&
                        method_signature_match(cls->members[j]->func, pmi->func)) {
                        overridden = true;
                        break;
                    }
                }
                if (overridden) continue;

                bool diamond_conflict = false;
                for (int j = 0; j < cls->member_count; j++) {
                    if (cls->members[j]->is_method &&
                        cls->members[j]->inherited_from != NULL &&
                        strcmp(cls->members[j]->name, pmi->name) == 0 &&
                        method_signature_match(cls->members[j]->func, pmi->func)) {
                        ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                              SEMLOC(ZERO_TOKEN),
                              "diamond inheritance conflict for method '%s'",
                              pmi->name);
                        diamond_conflict = true;
                        break;
                    }
                }
                if (diamond_conflict) continue;

                if (cls->member_count >= cls->member_capacity) {
                    cls->member_capacity = cls->member_capacity ? cls->member_capacity * 2 : 8;
                    cls->members = realloc(cls->members,
                                           sizeof(MemberInfo*) * (size_t)cls->member_capacity);
                }
                MemberInfo* mi = calloc(1, sizeof(MemberInfo));
                mi->name = strdup(pmi->name);
                mi->type = pmi->type;
                mi->is_method = true;
                mi->func = pmi->func;
                mi->offset = -1;
                mi->inherited_from = parent;
                cls->members[cls->member_count++] = mi;

                Symbol* func_sym = lookup_symbol(pmi->name);
                if (!func_sym) {
                    func_sym = calloc(1, sizeof(Symbol));
                    func_sym->key = strdup(pmi->name);
                    func_sym->value.type = TOKEN_IDENTIFIER;
                    func_sym->value.value = strdup(pmi->name);
                    func_sym->type = DATA_INT;
                    func_sym->internal_name = strdup(pmi->func->internal_name);
                    insert_symbol(func_sym);
                }
            }
        }
    }

    if (!cls->constructor && cls->parent_count > 0) {
        for (int i = 0; i < cls->parent_count; i++) {
            ClassEntry* parent = find_class(cls->parent_names[i]);
            if (parent && parent->constructor) {
                cls->constructor = parent->constructor;
                break;
            }
        }
    }

    cls->total_size = off;
    if (cls->total_size == 0) cls->total_size = 4;
    check_variance_usage(cd);
    cls->members_resolved = true;

    exit_scope();
    current_class = saved_class;
    current_class_name = saved_class_name;
    free(parent_entries);
}

static void analyze_class_bodies(AST_ClassDef* cd, ClassEntry* cls) {
    char* saved_class_name = current_class_name;
    ClassEntry* saved_class = current_class;
    current_class_name = cls->name;
    current_class = cls;

    Symbol* cls_sym = lookup_symbol(cd->name);
    if (!cls_sym) {
        cls_sym = create_symbol(
            (Token){.value = cd->name, .type = TOKEN_IDENTIFIER},
            DATA_IDENTIFIER, cd->name, generate_internal_name());
        insert_symbol(cls_sym);
    }

    enter_scope();
    Symbol* this_sym = create_symbol(
        (Token){.value = "this", .type = TOKEN_IDENTIFIER},
        DATA_IDENTIFIER, "this", strdup("_this"));
    this_sym->class_name = strdup(cd->name);
    insert_symbol(this_sym);

    if (cls->has_outer) {
        ClassEntry* outer_cls = find_class(cls->outer_class_name);
        if (outer_cls) {
            for (int oi = 0; oi < outer_cls->member_count; oi++) {
                MemberInfo* omi = outer_cls->members[oi];
                if (omi->is_method || omi->is_static) continue;
                Symbol* fallback = create_symbol(
                    (Token){.value = omi->name, .type = TOKEN_IDENTIFIER},
                    omi->type, omi->name, strdup(omi->name));
                fallback->is_field = true;
                fallback->field_offset = omi->offset;
                fallback->is_outer_field_ref = true;
                insert_symbol(fallback);
            }
        }
    }

    for (int i = 0; i < cls->member_count; i++) {
        MemberInfo* mi = cls->members[i];
        if (mi->is_method) continue;

        char* field_internal;
        if (mi->is_static) {
            if (mi->field_class_name) {
                field_internal = strdup(mi->field_class_name);
            } else {
                int nlen = (int)strlen(cls->name) + 1 + (int)strlen(mi->name) + 1;
                field_internal = malloc((size_t)nlen);
                if (!field_internal) OUT_OF_MEMORY();
                snprintf(field_internal, (size_t)nlen, "%s_%s", cls->name, mi->name);
            }
        } else {
            field_internal = strdup(mi->name);
        }

        Symbol* field_sym = create_symbol(
            (Token){.value = mi->name, .type = TOKEN_IDENTIFIER},
            mi->type,
            mi->name,
            field_internal
        );
        field_sym->is_field = true;
        field_sym->field_offset = mi->offset;
        field_sym->field_modifier = mi->field_modifier;
        if (mi->field_class_name) {
            field_sym->class_name = strdup(mi->field_class_name);
        } else if (mi->class_name) {
            field_sym->class_name = strdup(mi->class_name);
        }
        field_sym->is_static = mi->is_static;
        field_sym->is_const = mi->is_const;
        field_sym->is_unsafe = mi->is_unsafe;
        field_sym->ptr_depth = mi->ptr_depth;
        field_sym->dim_count = mi->dim_count;
        if (mi->dim_count > 0 && mi->dim_sizes) {
            field_sym->dim_sizes = malloc(sizeof(int) * (size_t)mi->dim_count);
            memcpy(field_sym->dim_sizes, mi->dim_sizes,
                   sizeof(int) * (size_t)mi->dim_count);
        }
        field_sym->is_dynamic_array = mi->is_dynamic_array;
        if (mi->is_dynamic_array && field_sym->ptr_depth == 0) {
            field_sym->ptr_depth = 1;
        }
        if (mi->func_sig) {
            field_sym->func_sig = deep_copy_func_sig(mi->func_sig);
        }
        insert_symbol(field_sym);

        if (mi->generic_type_arg_count > 0) {
            field_sym->generic_type_arg_count = mi->generic_type_arg_count;
            field_sym->generic_type_args = malloc(
                sizeof(DataType) * (size_t)mi->generic_type_arg_count);
            if (!field_sym->generic_type_args) OUT_OF_MEMORY();
            memcpy(field_sym->generic_type_args, mi->generic_type_args,
                   sizeof(DataType) * (size_t)mi->generic_type_arg_count);
            field_sym->generic_type_arg_names = calloc(
                (size_t)mi->generic_type_arg_count, sizeof(char*));
            if (!field_sym->generic_type_arg_names) OUT_OF_MEMORY();
            for (int gi = 0; gi < mi->generic_type_arg_count; gi++)
                field_sym->generic_type_arg_names[gi] =
                    (mi->generic_type_arg_names && mi->generic_type_arg_names[gi])
                    ? strdup(mi->generic_type_arg_names[gi]) : NULL;
        }
    }

    ASTNode* m = cd->members;
    while (m) {
        ASTNode* saved_next = m->next;
        m->next = NULL;

        if (m->type == AST_FUNCTION) {
            analyze_node(m);
        }
        else if (m->type == AST_CLASS_DEF) {
            AST_ClassDef* inner = (AST_ClassDef*)m;
            ClassEntry* inner_cls = find_class(inner->name);
            if (inner_cls) {
                if (inner_cls->short_name != NULL) {
                    Symbol* inner_sym = create_symbol(
                        (Token){.value = inner_cls->short_name, .type = TOKEN_IDENTIFIER},
                        DATA_IDENTIFIER, inner_cls->short_name, generate_internal_name());
                    inner_sym->class_name = strdup(inner_cls->name);
                    inner_sym->is_type_name = true;
                    insert_symbol(inner_sym);
                }
                analyze_class_bodies(inner, inner_cls);
            }
        }

        m->next = saved_next;
        m = m->next;
    }

    exit_scope();
    current_class = saved_class;
    current_class_name = saved_class_name;
}

static void analyze_node_list(ASTNode* node) {
    while (node) {
        analyze_node(node);
        node = node->next;
    }
}

static void collect_function_return(ASTNode* node, AST_Function* func) {
    if (!node) return;
    if (node->type == AST_RETURN) {
        if (func->Return == NULL) {
            func->Return = (AST_Return*)node;
            func->return_Count = 1;
        }
        return;
    }
    if (node->type == AST_MULTI_RETURN) {
        if (func->multi_return_node == NULL) {
            func->multi_return_node = (AST_MultiReturn*)node;
        }
        return;
    }
    if (node->type == AST_IF) {
        AST_If* if_node = (AST_If*)node;
        collect_function_return(if_node->code, func);
        for (int i = 0; i < if_node->elif_Count; i++)
            collect_function_return(if_node->Elif[i], func);
        if (if_node->has_else)
            collect_function_return(if_node->Else, func);
    }
    if (node->type == AST_WHILE) {
        collect_function_return(((AST_While*)node)->code, func);
    }
    if (node->type == AST_FOR) {
        collect_function_return(((AST_For*)node)->code, func);
    }
    if (node->type == AST_FOREACH) {
        collect_function_return(((AST_ForEach*)node)->code, func);
    }
    if (node->type == AST_SWITCH) {
        AST_Switch* sw = (AST_Switch*)node;
        for (int i = 0; i < sw->case_count; i++)
            collect_function_return(sw->cases[i]->body, func);
        if (sw->has_default)
            collect_function_return(sw->default_body, func);
    }
    if (node->type == AST_CONDITIONAL) {
        AST_Conditional* tern = (AST_Conditional*)node;
        collect_function_return(tern->then_expr, func);
        collect_function_return(tern->else_expr, func);
    }
    if (node->type == AST_TRY) {
        AST_Try* tn = (AST_Try*)node;
        collect_function_return(tn->code, func);
        for (int i = 0; i < tn->catch_count; i++)
            collect_function_return(tn->catches[i].body, func);
        if (tn->finally_code)
            collect_function_return(tn->finally_code, func);
    }
    collect_function_return(node->next, func);
}

static FuncSignature* get_func_ptr_return_sig(const char* name) {
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (strcmp(e->name, name) != 0) continue;
        AST_Function* func = e->func;
        if (!func || !func->ret_func_sig) return NULL;
        return deep_copy_func_sig(func->ret_func_sig);
    }
    return NULL;
}

static void substitute_type_params_in_node(ASTNode* node, TypeParam* tparams,
                                            int tp_count, DataType* type_args) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL: {
            AST_Literal* lit = (AST_Literal*)node;
            if (!lit->is_call_function) {
                if (lit->internal_name != NULL
                    && lit->base.value.type == TOKEN_NUMBER
                    && lit->base.value.value != NULL
                    && strcmp(lit->base.value.value, "0") == 0) {
                    for (int ti = 0; ti < tp_count; ti++) {
                        if (strcmp(lit->internal_name, tparams[ti].name) == 0) {
                            int bytes = S_get_type_size(type_args[ti]);
                            if (type_args[ti] == DATA_IDENTIFIER) {
                                bytes = 4;
                            }
                            char szbuf[16];
                            snprintf(szbuf, sizeof(szbuf), "%d", bytes);
                            free(lit->base.value.value);
                            lit->base.value.value = strdup(szbuf);
                            free(lit->internal_name);
                            lit->internal_name = NULL;
                            break;
                        }
                    }
                }
                if (lit->dataType == DATA_IDENTIFIER && lit->base.value.value != NULL) {
                    for (int i = 0; i < tp_count; i++) {
                        if (strcmp(lit->base.value.value, tparams[i].name) == 0) {
                            lit->dataType = type_args[i];
                            if (lit->internal_name
                                && strcmp(lit->internal_name, tparams[i].name) == 0) {
                                free(lit->internal_name);
                                lit->internal_name = NULL;
                            }
                            break;
                        }
                    }
                }
            }
            if (lit->call_function_Parameter)
                substitute_type_params_in_node(lit->call_function_Parameter,
                                               tparams, tp_count, type_args);
            if (lit->method_arguments)
                substitute_type_params_in_node(lit->method_arguments,
                                               tparams, tp_count, type_args);
            break;
        }
        case AST_PRINT: {
            AST_Print* p = (AST_Print*)node;
            if (p->Parameter)
                substitute_type_params_in_node(p->Parameter, tparams, tp_count, type_args);
            break;
        }

        case AST_VAR:
        case AST_VAR_ASS:
        case AST_UNINIT_VAR: {
            AST_Var* var = (AST_Var*)node;
            if (var->dataType == DATA_IDENTIFIER) {
                for (int i = 0; i < tp_count; i++) {
                    if (var->internal_name
                        && strcmp(var->internal_name, tparams[i].name) == 0) {
                        var->dataType = type_args[i];
                        free(var->internal_name);
                        var->internal_name = NULL;
                        break;
                        }
                }
            }

            if (var->func_ptr_sig) {
                substitute_func_sig_type_params(
                    var->func_ptr_sig, tparams, tp_count, type_args);
            }
            if (var->ptr_array_info) {
                if (var->ptr_array_info->element_type == DATA_IDENTIFIER) {
                    for (int i = 0; i < tp_count; i++) {
                        if (strcmp(data_type_to_string(var->ptr_array_info->element_type),
                                   tparams[i].name) == 0) {
                            var->ptr_array_info->element_type = type_args[i];
                            break;
                                   }
                    }
                }
            }
            if (var->Value)
                substitute_type_params_in_node(var->Value, tparams, tp_count, type_args);
            break;
        }

    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)node;
        if (bin->resultType == DATA_IDENTIFIER) bin->resultType = DATA_INT;
        if (bin->left)
            substitute_type_params_in_node(bin->left, tparams, tp_count, type_args);
        if (bin->right)
            substitute_type_params_in_node(bin->right, tparams, tp_count, type_args);
        break;
    }

    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)node;
        if (un->dataType == DATA_IDENTIFIER) un->dataType = DATA_INT;
        if (un->right)
            substitute_type_params_in_node(un->right, tparams, tp_count, type_args);
        break;
    }

    case AST_RETURN: {
        AST_Return* ret = (AST_Return*)node;
        if (ret->value)
            substitute_type_params_in_node(ret->value, tparams, tp_count, type_args);
        break;
    }

    case AST_IF: {
        AST_If* ifn = (AST_If*)node;
        if (ifn->Condition)
            substitute_type_params_in_node(ifn->Condition, tparams, tp_count, type_args);
        if (ifn->code)
            substitute_type_params_in_node(ifn->code, tparams, tp_count, type_args);
        for (int i = 0; i < ifn->elif_Count; i++) {
            if (ifn->elif_condition[i])
                substitute_type_params_in_node(ifn->elif_condition[i],
                                               tparams, tp_count, type_args);
            if (ifn->Elif[i])
                substitute_type_params_in_node(ifn->Elif[i],
                                               tparams, tp_count, type_args);
        }
        if (ifn->Else)
            substitute_type_params_in_node(ifn->Else, tparams, tp_count, type_args);
        break;
    }

    case AST_WHILE: {
        AST_While* w = (AST_While*)node;
        if (w->Condition)
            substitute_type_params_in_node(w->Condition, tparams, tp_count, type_args);
        if (w->code)
            substitute_type_params_in_node(w->code, tparams, tp_count, type_args);
        break;
    }

    case AST_FOR: {
        AST_For* f = (AST_For*)node;
        if (f->init)
            substitute_type_params_in_node(f->init, tparams, tp_count, type_args);
        if (f->condition)
            substitute_type_params_in_node(f->condition, tparams, tp_count, type_args);
        if (f->increment)
            substitute_type_params_in_node(f->increment, tparams, tp_count, type_args);
        if (f->code)
            substitute_type_params_in_node(f->code, tparams, tp_count, type_args);
        break;
    }

    case AST_ARRAY_DECL: {
        AST_ArrayDecl* arr = (AST_ArrayDecl*)node;
        if (arr->elementType == DATA_IDENTIFIER) {
            for (int i = 0; i < tp_count; i++) {
                arr->elementType = type_args[i];
                break;
            }
        }
        if (arr->init_list)
            substitute_type_params_in_node(arr->init_list, tparams, tp_count, type_args);
        if (arr->dim_exprs) {
            for (int i = 0; i < arr->dim_count; i++) {
                if (arr->dim_exprs[i])
                    substitute_type_params_in_node(arr->dim_exprs[i],
                                                   tparams, tp_count, type_args);
            }
        }
        break;
    }

    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)node;
        if (tern->resultType == DATA_IDENTIFIER) tern->resultType = DATA_INT;
        if (tern->condition)
            substitute_type_params_in_node(tern->condition, tparams, tp_count, type_args);
        if (tern->then_expr)
            substitute_type_params_in_node(tern->then_expr, tparams, tp_count, type_args);
        if (tern->else_expr)
            substitute_type_params_in_node(tern->else_expr, tparams, tp_count, type_args);
        break;
    }

    case AST_MULTI_RETURN: {
        AST_MultiReturn* mr = (AST_MultiReturn*)node;
        for (int i = 0; i < mr->value_count; i++) {
            if (mr->values[i])
                substitute_type_params_in_node(mr->values[i],
                                               tparams, tp_count, type_args);
        }
        break;
    }

    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)node;
        if (sw->condition)
            substitute_type_params_in_node(sw->condition, tparams, tp_count, type_args);
        for (int i = 0; i < sw->case_count; i++) {
            if (sw->cases[i]->value)
                substitute_type_params_in_node(sw->cases[i]->value,
                                               tparams, tp_count, type_args);
            if (sw->cases[i]->body)
                substitute_type_params_in_node(sw->cases[i]->body,
                                               tparams, tp_count, type_args);
        }
        if (sw->default_body)
            substitute_type_params_in_node(sw->default_body, tparams, tp_count, type_args);
        break;
    }

    case AST_FUNCTION: {
        AST_Function* func = (AST_Function*)node;
        if (func->return_type == DATA_IDENTIFIER) {
            for (int i = 0; i < tp_count; i++) {
                if (func->return_class_name
                    && strcmp(func->return_class_name, tparams[i].name) == 0) {
                    func->return_type = type_args[i];
                    break;
                    }
            }
        }
        if (func->Parameter)
            substitute_type_params_in_node(func->Parameter, tparams, tp_count, type_args);
        if (func->parameterType && func->parameterCount > 0) {
            ASTNode* p = func->Parameter;
            for (int i = 0; i < func->parameterCount && p; i++) {
                if (p->type == AST_VAR || p->type == AST_UNINIT_VAR
                    || p->type == AST_VAR_ASS) {
                    AST_Var* pv = (AST_Var*)p;
                    if (pv->dataType != DATA_IDENTIFIER
                        && func->parameterType[i] == DATA_IDENTIFIER) {
                        func->parameterType[i] = pv->dataType;
                        }
                    }
                p = p->next;
            }
        }
        if (func->Code)
            substitute_type_params_in_node(func->Code, tparams, tp_count, type_args);
        if (func->multi_return) {
            for (int i = 0; i < func->multi_return->count; i++) {
                if (func->multi_return->type_param_indices
                    && func->multi_return->type_param_indices[i] >= 0
                    && func->multi_return->type_param_indices[i] < tp_count) {
                    func->multi_return->types[i] =
                        type_args[func->multi_return->type_param_indices[i]];
                    }
                if (func->multi_return->func_sigs
                    && func->multi_return->func_sigs[i]) {
                    substitute_func_sig_type_params(
                        func->multi_return->func_sigs[i],
                        tparams, tp_count, type_args);
                    }
            }
        }
        if (func->ret_func_sig) {
            substitute_func_sig_type_params(func->ret_func_sig,
                                            tparams, tp_count, type_args);
        }
        break;
    }

    case AST_TRY: {
        AST_Try* tn = (AST_Try*)node;
        if (tn->code)
            substitute_type_params_in_node(tn->code, tparams, tp_count, type_args);
        for (int i = 0; i < tn->catch_count; i++)
            if (tn->catches[i].body)
                substitute_type_params_in_node(tn->catches[i].body,
                                               tparams, tp_count, type_args);
        if (tn->finally_code)
            substitute_type_params_in_node(tn->finally_code,
                                           tparams, tp_count, type_args);
        break;
    }

    case AST_THROW: {
        AST_Throw* th = (AST_Throw*)node;
        if (th->value)
            substitute_type_params_in_node(th->value, tparams, tp_count, type_args);
        break;
    }

    default:
        break;
    }

    if (node->next)
        substitute_type_params_in_node(node->next, tparams, tp_count, type_args);
}

static void replace_call_target(ASTNode* node, const char* old_name, const char* new_name) {
    while (node) {
        switch (node->type) {
        case AST_LITERAL: {
            AST_Literal* lit = (AST_Literal*)node;
            if (lit->is_call_function && lit->internal_name
                && strcmp(lit->internal_name, old_name) == 0) {
                free(lit->internal_name);
                lit->internal_name = strdup(new_name);
            }
            if (lit->call_function_Parameter)
                replace_call_target(lit->call_function_Parameter, old_name, new_name);
            if (lit->method_arguments)
                replace_call_target(lit->method_arguments, old_name, new_name);
            break;
        }
        case AST_RETURN:
            replace_call_target(((AST_Return*)node)->value, old_name, new_name);
            break;
        case AST_BINARY:
            replace_call_target(((AST_Binary*)node)->left, old_name, new_name);
            replace_call_target(((AST_Binary*)node)->right, old_name, new_name);
            break;
        case AST_UNARY:
            replace_call_target(((AST_Unary*)node)->right, old_name, new_name);
            break;
        case AST_VAR:
        case AST_VAR_ASS:
        case AST_UNINIT_VAR:
            replace_call_target(((AST_Var*)node)->Value, old_name, new_name);
            break;
        case AST_IF: {
            AST_If* ifn = (AST_If*)node;
            replace_call_target(ifn->Condition, old_name, new_name);
            replace_call_target(ifn->code, old_name, new_name);
            for (int i = 0; i < ifn->elif_Count; i++) {
                replace_call_target(ifn->elif_condition[i], old_name, new_name);
                replace_call_target(ifn->Elif[i], old_name, new_name);
            }
            replace_call_target(ifn->Else, old_name, new_name);
            break;
        }
        case AST_WHILE:
            replace_call_target(((AST_While*)node)->Condition, old_name, new_name);
            replace_call_target(((AST_While*)node)->code, old_name, new_name);
            break;
        case AST_FOR:
            replace_call_target(((AST_For*)node)->init, old_name, new_name);
            replace_call_target(((AST_For*)node)->condition, old_name, new_name);
            replace_call_target(((AST_For*)node)->increment, old_name, new_name);
            replace_call_target(((AST_For*)node)->code, old_name, new_name);
            break;
        case AST_FOREACH:
            replace_call_target(((AST_ForEach*)node)->array, old_name, new_name);
            replace_call_target(((AST_ForEach*)node)->code, old_name, new_name);
            break;
        case AST_SWITCH: {
            AST_Switch* sw = (AST_Switch*)node;
            replace_call_target(sw->condition, old_name, new_name);
            for (int i = 0; i < sw->case_count; i++) {
                replace_call_target(sw->cases[i]->value, old_name, new_name);
                replace_call_target(sw->cases[i]->body, old_name, new_name);
            }
            replace_call_target(sw->default_body, old_name, new_name);
            break;
        }
        case AST_CONDITIONAL:
            replace_call_target(((AST_Conditional*)node)->condition, old_name, new_name);
            replace_call_target(((AST_Conditional*)node)->then_expr, old_name, new_name);
            replace_call_target(((AST_Conditional*)node)->else_expr, old_name, new_name);
            break;
        case AST_PRINT:
            replace_call_target(((AST_Print*)node)->Parameter, old_name, new_name);
            break;
        case AST_MEMBER_ACCESS:
            replace_call_target(((AST_MemberAccess*)node)->object, old_name, new_name);
            break;
        case AST_MEMBER_ASS:
            replace_call_target(((AST_MemberAss*)node)->object, old_name, new_name);
            replace_call_target(((AST_MemberAss*)node)->value, old_name, new_name);
            break;
        case AST_ARRAY_ACCESS:
            replace_call_target(((AST_ArrayAccess*)node)->array, old_name, new_name);
            for (int i = 0; i < ((AST_ArrayAccess*)node)->index_count; i++)
                replace_call_target(((AST_ArrayAccess*)node)->indices[i], old_name, new_name);
            break;
        case AST_ARRAY_ASS:
            replace_call_target(((AST_ArrayAss*)node)->target, old_name, new_name);
            replace_call_target(((AST_ArrayAss*)node)->value, old_name, new_name);
            break;
        case AST_DEREF:
            replace_call_target(((AST_Deref*)node)->operand, old_name, new_name);
            break;
        case AST_ADDRESS_OF:
            replace_call_target(((AST_AddressOf*)node)->operand, old_name, new_name);
            break;
        case AST_VARARG_GROUP:
            replace_call_target(((AST_VarargGroup*)node)->elements, old_name, new_name);
            break;
        case AST_MULTI_RETURN:
            for (int i = 0; i < ((AST_MultiReturn*)node)->value_count; i++)
                replace_call_target(((AST_MultiReturn*)node)->values[i], old_name, new_name);
            break;
        case AST_RETURN_EXTRACT:
            replace_call_target(((AST_ReturnExtract*)node)->object, old_name, new_name);
            break;
        case AST_NEW_INSTANCE:
            replace_call_target(((AST_NewInstance*)node)->arguments, old_name, new_name);
            break;
        case AST_ARRAY_DECL:
            replace_call_target(((AST_ArrayDecl*)node)->init_list, old_name, new_name);
            break;
        case AST_TRY: {
            AST_Try* tn = (AST_Try*)node;
            replace_call_target(tn->code, old_name, new_name);
            for (int i = 0; i < tn->catch_count; i++)
                replace_call_target(tn->catches[i].body, old_name, new_name);
            replace_call_target(tn->finally_code, old_name, new_name);
            break;
        }
        case AST_THROW:
            replace_call_target(((AST_Throw*)node)->value, old_name, new_name);
            break;
        default:
            break;
        }
        node = node->next;
    }
}

static ASTNode* shallow_clone_node(ASTNode* node);

/* 判断一个实际类型实参是否满足某条泛型约束。
   dt 是实参类型；name 在 dt == DATA_IDENTIFIER 时是类名。
   约束语义：
     <T :  Base>   实际类型必须是 Base 或 Base 的子类
     <T >: Base>   Base 必须是实际类型或实际类型的子类（实际类型是 Base 的父类） */
static bool type_satisfies_constraint(DataType dt, const char* name,
                                      const TypeConstraint* c) {
    if (c == NULL || c->class_name == NULL) return true;
    /* 基本类型不是类，任何类约束都不满足 */
    if (dt != DATA_IDENTIFIER || name == NULL) return false;

    ClassEntry* actual = find_class(name);
    if (actual == NULL) actual = find_class_generic_base(name);
    ClassEntry* want = find_class(c->class_name);
    if (want == NULL) want = find_class_generic_base(c->class_name);

    /* 约束类还没注册（前向引用），先放过，等实例化时再看 */
    if (want == NULL) return true;
    if (actual == NULL) return false;

    if (c->kind == CONSTRAINT_SUPERTYPE) {
        return is_subclass_of(want, actual);
    }
    return is_subclass_of(actual, want);
}

/* 校验一个类型参数上的所有约束，返回是否全部满足（失败时已报错） */
/* 把 "Base$A_B" 拆成 base 和参数列表。成功返回 true。
   参数名用 '_' 分隔（嵌套泛型用 '$'，所以 List$List$int 是单参数）。 */
static bool split_generic_inst_name(const char* name, char* base_out,
                                    size_t base_sz, char** args_out,
                                    int* argc_out, int max_args) {
    if (name == NULL) return false;
    const char* dollar = strchr(name, '$');
    if (dollar == NULL) return false;
    size_t bl = (size_t)(dollar - name);
    if (bl + 1 > base_sz) return false;
    memcpy(base_out, name, bl);
    base_out[bl] = '\0';

    int n = 0;
    const char* p = dollar + 1;
    char buf[256];
    while (*p != '\0' && n < max_args) {
        const char* q = p;
        while (*q != '\0' && *q != '_') q++;
        size_t len = (size_t)(q - p);
        if (len + 1 > sizeof(buf)) return false;
        memcpy(buf, p, len);
        buf[len] = '\0';
        args_out[n++] = strdup(buf);
        p = (*q == '_') ? q + 1 : q;
    }
    *argc_out = n;
    return true;
}

/* 变型兼容性：声明类型 decl_cls 能否接收实际类型 actual_cls。
     - 名字完全相同 → 允许
     - 基类不同      → 放行（由其它检查负责继承关系）
     - 基类相同      → 逐参数按变型判定：
         不变(0)：参数名必须相同
         协变(+1)：actual 的参数必须是 decl 参数的子类
         逆变(-1)：decl 的参数必须是 actual 参数的子类
     - 参数个数不同  → 拒绝 */
static bool generic_variance_assign_ok(const char* decl_cls,
                                       const char* actual_cls) {
    if (decl_cls == NULL || actual_cls == NULL) return true;
    if (strcmp(decl_cls, actual_cls) == 0) return true;

    ClassEntry* dcls = find_class(decl_cls);
    ClassEntry* acls = find_class(actual_cls);
    if (dcls == NULL || acls == NULL) {
        /* 实例类还没建立（例如 List<Animal> 从未被实例化）。
           退回到名字比较：拆出基类与参数，用基类的 type_params 判定。 */
        const char* db2 = strchr(decl_cls, '$');
        const char* ab2 = strchr(actual_cls, '$');
        if (db2 == NULL || ab2 == NULL) return true;
        size_t dbl2 = (size_t)(db2 - decl_cls);
        size_t abl2 = (size_t)(ab2 - actual_cls);
        if (dbl2 != abl2 || strncmp(decl_cls, actual_cls, dbl2) != 0) return true;

        char base2[128];
        if (dbl2 + 1 > sizeof(base2)) return true;
        memcpy(base2, decl_cls, dbl2);
        base2[dbl2] = '\0';

        char dargs2[8][128];
        char aargs2[8][128];
        int dac2 = 0, aac2 = 0;
        /* 拆参数：按 '_' 分段 */
        {
            const char* p = db2 + 1;
            while (*p && dac2 < 8) {
                const char* q = p;
                while (*q && *q != '_') q++;
                size_t ln = (size_t)(q - p);
                if (ln + 1 > sizeof(dargs2[0])) return true;
                memcpy(dargs2[dac2], p, ln);
                dargs2[dac2][ln] = '\0';
                dac2++;
                p = (*q == '_') ? q + 1 : q;
            }
            p = ab2 + 1;
            while (*p && aac2 < 8) {
                const char* q = p;
                while (*q && *q != '_') q++;
                size_t ln = (size_t)(q - p);
                if (ln + 1 > sizeof(aargs2[0])) return true;
                memcpy(aargs2[aac2], p, ln);
                aargs2[aac2][ln] = '\0';
                aac2++;
                p = (*q == '_') ? q + 1 : q;
            }
        }
        if (dac2 != aac2) return false;

        ClassEntry* gen2 = find_class(base2);
        if (gen2 == NULL || !gen2->is_generic || gen2->type_params == NULL
            || gen2->type_param_count < dac2) {
            return true;
        }
        for (int i = 0; i < dac2; i++) {
            if (strcmp(dargs2[i], aargs2[i]) == 0) continue;
            int v2 = gen2->type_params[i].variance;
            if (v2 == 0) return false;
            ClassEntry* dc2 = find_class(dargs2[i]);
            if (dc2 == NULL) dc2 = find_class_generic_base(dargs2[i]);
            ClassEntry* ac2 = find_class(aargs2[i]);
            if (ac2 == NULL) ac2 = find_class_generic_base(aargs2[i]);
            if (dc2 == NULL || ac2 == NULL) return false;
            if (v2 > 0) {
                if (!is_subclass_of(ac2, dc2)) return false;
            } else {
                if (!is_subclass_of(dc2, ac2)) return false;
            }
        }
        return true;
    }


    int dac = dcls->generic_type_arg_count;
    int aac = acls->generic_type_arg_count;
    if (dac <= 0 || aac <= 0) return true;          /* 不是泛型实例 */
    if (dac != aac) return false;

    /* 基类必须相同，否则交给继承检查 */
    const char* db = strrchr(dcls->name, '$');
    const char* ab = strrchr(acls->name, '$');
    if (db == NULL || ab == NULL) return true;
    {
        size_t dbl = (size_t)(db - dcls->name);
        size_t abl = (size_t)(ab - acls->name);
        if (dbl != abl || strncmp(dcls->name, acls->name, dbl) != 0) return true;
    }

    TypeParam* vparams = NULL;
    int vparam_count = 0;
    if (dcls->generic_ast != NULL && dcls->generic_ast->type_params != NULL) {
        vparams = dcls->generic_ast->type_params;
        vparam_count = dcls->generic_ast->type_param_count;
    }

    for (int i = 0; i < dac; i++) {
        const char* dn = (dcls->generic_type_arg_names != NULL)
                         ? dcls->generic_type_arg_names[i] : NULL;
        const char* an = (acls->generic_type_arg_names != NULL)
                         ? acls->generic_type_arg_names[i] : NULL;

        if (dn == NULL || an == NULL) {
            if (dcls->generic_type_args[i] != acls->generic_type_args[i])
                return false;
            continue;
        }
        if (strcmp(dn, an) == 0) continue;

        int v = (i < vparam_count) ? vparams[i].variance : 0;
        if (v == 0) return false;

        ClassEntry* dc = find_class(dn);
        if (dc == NULL) dc = find_class_generic_base(dn);
        ClassEntry* ac = find_class(an);
        if (ac == NULL) ac = find_class_generic_base(an);
        if (dc == NULL || ac == NULL) return false;

        if (v > 0) {
            if (!is_subclass_of(ac, dc)) return false;
        } else {
            if (!is_subclass_of(dc, ac)) return false;
        }
    }
    return true;
}

static bool check_type_param_constraints(const TypeParam* tp, DataType dt,
                                         const char* name, Token loc_tok) {
    if (tp == NULL || tp->constraint_count == 0) return true;
    bool ok = true;
    for (int j = 0; j < tp->constraint_count; j++) {
        const TypeConstraint* c = &tp->constraints[j];
        if (type_satisfies_constraint(dt, name, c)) continue;
        ok = false;
        const char* shown = (name != NULL) ? name : data_type_to_string(dt);
        ERROR(ERR_SEM_TYPE_MISMATCH,
              SEMLOC(loc_tok),
              "type argument '%s' for '%s' violates constraint '%s%s'",
              shown ? shown : "?",
              tp->name ? tp->name : "T",
              c->kind == CONSTRAINT_SUPERTYPE ? ">:" : ":",
              c->class_name ? c->class_name : "?");
    }
    return ok;
}

static AST_Function* instantiate_generic_function(AST_Function* generic_func,
                                                   DataType* type_args,
                                                   int type_arg_count,
                                                   char** type_arg_names) {
    if (!generic_func || !generic_func->is_generic) return NULL;
    if (type_arg_count != generic_func->type_param_count) return NULL;

    /* 泛型约束校验：<T : Base> / <T >: Base> */
    for (int ci = 0; ci < generic_func->type_param_count && ci < type_arg_count; ci++) {
        TypeParam* ctp = &generic_func->type_params[ci];
        if (ctp->constraint_count == 0) continue;
        const char* cname = (type_arg_names != NULL) ? type_arg_names[ci] : NULL;
        check_type_param_constraints(ctp, type_args[ci], cname,
                                     generic_func->name);
    }

    AST_Function* inst = calloc(1, sizeof(AST_Function));
    if (!inst) OUT_OF_MEMORY();

    inst->base.type = AST_FUNCTION;
    inst->base.next = NULL;
    inst->name = generic_func->name;
    inst->name.value = strdup(generic_func->name.value);

    const char* base_name = generic_func->internal_name
                            ? generic_func->internal_name
                            : generic_func->name.value;

    int nl = (int)strlen(base_name) + 2;
    for (int i = 0; i < type_arg_count; i++) {
        const char* tn;
        if (type_args[i] == DATA_IDENTIFIER) {
            if (type_arg_names && type_arg_names[i]) {
                tn = type_arg_names[i];
            } else if (i < generic_func->type_param_count) {
                TypeParam* tp = &generic_func->type_params[i];
                if (tp->constraint_count > 0
                    && tp->constraints[0].kind == CONSTRAINT_SUBTYPE) {
                    tn = tp->constraints[0].class_name;
                } else {
                    tn = "identifier";
                }
            } else {
                tn = "identifier";
            }
        } else {
            tn = data_type_to_string(type_args[i]);
        }
        nl += (int)strlen(tn) + 1;
    }
    char* inst_name = malloc((size_t)nl + 1);
    if (!inst_name) OUT_OF_MEMORY();
    sprintf(inst_name, "%s$", base_name);
    for (int i = 0; i < type_arg_count; i++) {
        const char* tn;
        if (type_args[i] == DATA_IDENTIFIER) {
            if (type_arg_names && type_arg_names[i]) {
                tn = type_arg_names[i];
            } else if (i < generic_func->type_param_count) {
                TypeParam* tp = &generic_func->type_params[i];
                if (tp->constraint_count > 0
                    && tp->constraints[0].kind == CONSTRAINT_SUBTYPE) {
                    tn = tp->constraints[0].class_name;
                } else {
                    tn = "identifier";
                }
            } else {
                tn = "identifier";
            }
        } else {
            tn = data_type_to_string(type_args[i]);
        }
        strcat(inst_name, tn);
        if (i < type_arg_count - 1) strcat(inst_name, "_");
    }
    inst->internal_name = inst_name;

    inst->return_type = generic_func->return_type;
    inst->Modifier = generic_func->Modifier;
    inst->returns_array = generic_func->returns_array;
    inst->returns_struct = generic_func->returns_struct;
    inst->ret_func_sig = deep_copy_func_sig(generic_func->ret_func_sig);
    inst->multi_return = deep_copy_multi_return(generic_func->multi_return);
    if (inst->multi_return) {
        for (int i = 0; i < inst->multi_return->count; i++) {
            if (inst->multi_return->type_param_indices
                && inst->multi_return->type_param_indices[i] >= 0
                && inst->multi_return->type_param_indices[i] < type_arg_count) {
                inst->multi_return->types[i] =
                    type_args[inst->multi_return->type_param_indices[i]];
            }
            if (inst->multi_return->func_sigs
                && inst->multi_return->func_sigs[i]) {
                substitute_func_sig_type_params(
                    inst->multi_return->func_sigs[i],
                    generic_func->type_params,
                    generic_func->type_param_count,
                    type_args);
            }
        }
    }
    if (inst->ret_func_sig) {
        substitute_func_sig_type_params(inst->ret_func_sig,
                                        generic_func->type_params,
                                        generic_func->type_param_count,
                                        type_args);
    }
    inst->is_generic = false;
    inst->type_param_count = 0;
    inst->type_params = NULL;

    if (inst->return_type == DATA_IDENTIFIER) {
        for (int i = 0; i < type_arg_count; i++) {
            if (generic_func->return_class_name
                && strcmp(generic_func->return_class_name,
                          generic_func->type_params[i].name) == 0) {
                inst->return_type = type_args[i];
                inst->return_class_name = NULL;
                break;
            }
        }
    }

    inst->max_parameters = generic_func->max_parameters;
    inst->parameterCount = generic_func->parameterCount;
    if (inst->parameterCount > 0) {
        inst->parameterType = malloc(sizeof(DataType) * (size_t)inst->parameterCount);
        memcpy(inst->parameterType, generic_func->parameterType,
               sizeof(DataType) * (size_t)inst->parameterCount);
        for (int i = 0; i < inst->parameterCount; i++) {
            if (inst->parameterType[i] == DATA_IDENTIFIER) {
                if (generic_func->Parameter) {
                    ASTNode* pnode = generic_func->Parameter;
                    int pi = 0;
                    while (pnode && pi < i) { pnode = pnode->next; pi++; }
                    if (pnode && (pnode->type == AST_VAR || pnode->type == AST_UNINIT_VAR)) {
                        AST_Var* pvar = (AST_Var*)pnode;
                        if (pvar->internal_name) {
                            for (int t = 0; t < generic_func->type_param_count; t++) {
                                if (strcmp(pvar->internal_name,
                                           generic_func->type_params[t].name) == 0) {
                                    inst->parameterType[i] = type_args[t];
                                    break;
                                }
                            }
                        }
                    }
                }
                if (inst->parameterType[i] == DATA_IDENTIFIER) {
                    inst->parameterType[i] = type_args[0];
                }
            }
        }
    }

    char** saved_targ_names = g_subst_type_arg_names;
    g_subst_type_arg_names = type_arg_names;

    if (generic_func->Code) {
        inst->Code = shallow_clone_node(generic_func->Code);
        substitute_type_params_in_node(inst->Code,
                                       generic_func->type_params,
                                       generic_func->type_param_count,
                                       type_args);
    }
    if (generic_func->Parameter) {
        inst->Parameter = shallow_clone_node(generic_func->Parameter);
        substitute_type_params_in_node(inst->Parameter,
                                       generic_func->type_params,
                                       generic_func->type_param_count,
                                       type_args);
    }

    g_subst_type_arg_names = saved_targ_names;

    ASTNode* p = inst->Parameter;
    int gen_pidx = 0;
    while (p && gen_pidx < generic_func->parameterCount) {
        if (p->type == AST_VAR || p->type == AST_VAR_ASS
            || p->type == AST_UNINIT_VAR) {
            AST_Var* pvar = (AST_Var*)p;
            if (pvar->type_param_index >= 0
                && pvar->type_param_index < type_arg_count) {
                pvar->dataType = type_args[pvar->type_param_index];
                if (type_args[pvar->type_param_index] == DATA_IDENTIFIER
                    && pvar->type_param_index < generic_func->type_param_count) {
                    TypeParam* tp = &generic_func->type_params[pvar->type_param_index];
                    for (int c = 0; c < tp->constraint_count; c++) {
                        if (tp->constraints[c].kind == CONSTRAINT_SUBTYPE) {
                            free(pvar->internal_name);
                            pvar->internal_name = strdup(tp->constraints[c].class_name);
                            break;
                        }
                    }
                }
                } else if (generic_func->parameterType[gen_pidx] == DATA_IDENTIFIER) {
                    for (int t = 0; t < generic_func->type_param_count; t++) {
                        if (pvar->internal_name
                            && strcmp(pvar->internal_name,
                                      generic_func->type_params[t].name) == 0) {
                            pvar->dataType = type_args[t];
                            if (type_args[t] == DATA_IDENTIFIER) {
                                TypeParam* tp = &generic_func->type_params[t];
                                for (int c = 0; c < tp->constraint_count; c++) {
                                    if (tp->constraints[c].kind == CONSTRAINT_SUBTYPE) {
                                        free(pvar->internal_name);
                                        pvar->internal_name = strdup(tp->constraints[c].class_name);
                                        break;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
        p = p->next;
        gen_pidx++;
    }

    inst->has_varargs = generic_func->has_varargs;
    inst->vararg_param_count = generic_func->vararg_param_count;
    if (generic_func->vararg_param_count > 0) {
        inst->vararg_param_indices = malloc(sizeof(int) * (size_t)generic_func->vararg_param_count);
        memcpy(inst->vararg_param_indices, generic_func->vararg_param_indices,
               sizeof(int) * (size_t)generic_func->vararg_param_count);
        inst->vararg_internal_names = malloc(sizeof(char*) * (size_t)generic_func->vararg_param_count);
        for (int i = 0; i < generic_func->vararg_param_count; i++) {
            inst->vararg_internal_names[i] =
                generic_func->vararg_internal_names[i]
                ? strdup(generic_func->vararg_internal_names[i]) : NULL;
        }
        inst->vararg_internal_name = generic_func->vararg_internal_name
            ? strdup(generic_func->vararg_internal_name) : NULL;
    }

    add_function_with_params(inst->internal_name, inst);

    AST_Function* saved_current = current_analyzing_function;
    current_analyzing_function = inst;

    enter_scope();
    if (inst->Parameter) analyze_node_list(inst->Parameter);
    if (inst->Code) analyze_node_list(inst->Code);
    exit_scope();

    if (type_arg_names && inst->Code) {
        for (int i = 0; i < type_arg_count; i++) {
            if (!type_arg_names[i] || type_args[i] != DATA_IDENTIFIER) continue;
            for (int c = 0; c < generic_func->type_params[i].constraint_count; c++) {
                TypeConstraint* tc = &generic_func->type_params[i].constraints[c];
                if (tc->kind != CONSTRAINT_SUBTYPE) continue;
                ClassEntry* constraint_cls = find_class(tc->class_name);
                ClassEntry* actual_cls = find_class(type_arg_names[i]);
                if (!constraint_cls || !actual_cls) continue;
                for (int mi = 0; mi < constraint_cls->member_count; mi++) {
                    MemberInfo* cm = constraint_cls->members[mi];
                    if (!cm->is_method) continue;
                    const char* old_name = cm->func->internal_name;
                    for (int mj = 0; mj < actual_cls->member_count; mj++) {
                        MemberInfo* am = actual_cls->members[mj];
                        if (!am->is_method) continue;
                        if (strcmp(cm->name, am->name) == 0) {
                            replace_call_target(inst->Code, old_name, am->func->internal_name);
                            break;
                        }
                    }
                }
            }
        }
    }

    current_analyzing_function = saved_current;

    if (inst->Code) {
        ASTNode* scan = inst->Code;
        while (scan) {
            if (scan->type == AST_MULTI_RETURN) {
                AST_MultiReturn* mr = (AST_MultiReturn*)scan;
                for (int i = 0; i < mr->value_count; i++) {
                    if (mr->values[i]
                        && mr->values[i]->type == AST_LITERAL) {
                        AST_Literal* lit = (AST_Literal*)mr->values[i];
                        if (!lit->is_call_function
                            && lit->base.value.type == TOKEN_IDENTIFIER) {
                            ASTNode* nf = inst->Code;
                            while (nf) {
                                if (nf->type == AST_FUNCTION) {
                                    AST_Function* nff = (AST_Function*)nf;
                                    if (strcmp(nff->name.value,
                                               lit->base.value.value) == 0) {
                                        if (nff->internal_name) {
                                            free(lit->internal_name);
                                            lit->internal_name =
                                                strdup(nff->internal_name);
                                        }
                                        break;
                                    }
                                }
                                nf = nf->next;
                            }
                        }
                    }
                }
            }
            scan = scan->next;
        }
    }

    if (inst->multi_return == NULL && generic_func->multi_return != NULL) {
        inst->multi_return = deep_copy_multi_return(generic_func->multi_return);
    }

    if (inst->Code) {
        ASTNode* scan = inst->Code;
        while (scan) {
            if (scan->type == AST_RETURN) {
                if (!inst->Return) {
                    inst->Return = (AST_Return*)scan;
                    inst->return_Count = 1;
                }
            } else if (scan->type == AST_MULTI_RETURN) {
                if (!inst->multi_return_node) {
                    inst->multi_return_node = (AST_MultiReturn*)scan;
                }
            }
            scan = scan->next;
        }
    }
    if (inst->return_type == DATA_IDENTIFIER && type_arg_count > 0) {
        for (int i = 0; i < type_arg_count; i++) {
            if (generic_func->return_class_name
                && strcmp(generic_func->return_class_name,
                          generic_func->type_params[i].name) == 0) {
                inst->return_type = type_args[i];
                break;
             }
        }
        if (inst->return_type == DATA_IDENTIFIER) {
            inst->return_type = type_args[0];
        }
    }

    for (int i = 0; i < type_arg_count && i < generic_func->type_param_count; i++) {
        TypeParam* tp = &generic_func->type_params[i];
        for (int j = 0; j < tp->constraint_count; j++) {
            TypeConstraint* tc = &tp->constraints[j];
            ClassEntry* constraint_cls = find_class(tc->class_name);
            if (!constraint_cls) {
                WARNING(ERR_SEM_CLASS_NOT_FOUND, LOC_NONE,
                        "constraint class '%s' not found for type parameter '%s'",
                        tc->class_name, tp->name);
                continue;
            }

            if (tc->kind == CONSTRAINT_SUBTYPE) {
                if (type_args[i] == DATA_IDENTIFIER) {
                } else if (is_int_type(type_args[i]) || is_uint_type(type_args[i])
                           || is_float_type(type_args[i]) || is_ufloat_type(type_args[i])) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
                          "type argument '%s' for '%s' cannot satisfy ':%s' constraint "
                          "(primitive type cannot be subclass of '%s')",
                          data_type_to_string(type_args[i]), tp->name,
                          tc->class_name, tc->class_name);
                }
            } else if (tc->kind == CONSTRAINT_SUPERTYPE) {
                if (type_args[i] != DATA_IDENTIFIER) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
                          "type argument '%s' for '%s' cannot satisfy '>:%s' constraint",
                          data_type_to_string(type_args[i]), tp->name,
                          tc->class_name);
                }
            }
        }
    }

    if (generic_func->type_param_count > 0 && type_arg_count > 0) {
        for (int i = 0; i < type_arg_count
                        && i < generic_func->type_param_count; i++) {
            TypeParam* tp = &generic_func->type_params[i];
            for (int j = 0; j < tp->constraint_count; j++) {
                TypeConstraint* tc = &tp->constraints[j];
                ClassEntry* constraint_cls = find_class(tc->class_name);
                if (!constraint_cls) {
                    WARNING(ERR_SEM_CLASS_NOT_FOUND, LOC_NONE,
                            "constraint class '%s' not found for '%s'",
                            tc->class_name, tp->name);
                    continue;
                }
                if (tc->kind == CONSTRAINT_SUBTYPE) {
                    if (type_args[i] != DATA_IDENTIFIER
                        && !is_int_type(type_args[i])
                        && !is_uint_type(type_args[i])
                        && !is_float_type(type_args[i])
                        && !is_ufloat_type(type_args[i])) {
                        ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
                              "type argument '%s' for '%s' cannot satisfy "
                              "':%s' constraint (primitive type)",
                              data_type_to_string(type_args[i]),
                              tp->name, tc->class_name);
                    }
                }
            }
        }
    }

    if (inst->Code && !inst->Return) {
        ASTNode* scan = inst->Code;
        while (scan) {
            if (scan->type == AST_RETURN) {
                if (!inst->Return) {
                    inst->Return = (AST_Return*)scan;
                    inst->return_Count = 1;
                }
            } else if (scan->type == AST_MULTI_RETURN) {
                if (!inst->multi_return_node) {
                    inst->multi_return_node = (AST_MultiReturn*)scan;
                }
            }
            scan = scan->next;
        }
    }

    if (inst->return_type != DATA_IDENTIFIER && inst->return_type != DATA_VOID) {
        if (inst->Return) {
            inst->Return->returnType = inst->return_type;
        }
        ASTNode* scan = inst->Code;
        while (scan) {
            if (scan->type == AST_RETURN) {
                AST_Return* ret = (AST_Return*)scan;
                ret->returnType = inst->return_type;
            }
            scan = scan->next;
        }
    }

    if (inst->ret_func_sig) {
        substitute_func_sig_type_params(inst->ret_func_sig,
                                        generic_func->type_params,
                                        generic_func->type_param_count,
                                        type_args);
    }
    if (inst->multi_return) {
        for (int i = 0; i < inst->multi_return->count; i++) {
            if (inst->multi_return->type_param_indices
                && inst->multi_return->type_param_indices[i] >= 0
                && inst->multi_return->type_param_indices[i] < type_arg_count) {
                inst->multi_return->types[i] =
                    type_args[inst->multi_return->type_param_indices[i]];
            }
            if (inst->multi_return->func_sigs
                && inst->multi_return->func_sigs[i]) {
                substitute_func_sig_type_params(
                    inst->multi_return->func_sigs[i],
                    generic_func->type_params,
                    generic_func->type_param_count,
                    type_args);
            }
        }
    }

    if (inst->Code) {
        ASTNode* scan = inst->Code;
        while (scan) {
            if (scan->type == AST_MULTI_RETURN) {
                AST_MultiReturn* mr = (AST_MultiReturn*)scan;
                for (int i = 0; i < mr->value_count; i++) {
                    if (mr->values[i]->type == AST_LITERAL) {
                        AST_Literal* lit = (AST_Literal*)mr->values[i];
                        if (!lit->is_call_function
                            && lit->base.value.type == TOKEN_IDENTIFIER) {
                            Symbol* sym = lookup_symbol(lit->base.value.value);
                            if (sym && sym->is_function) {
                                for (FuncEntry* e = func_table; e; e = e->next) {
                                    if (e->func->is_nested
                                        && e->func->is_closure
                                        && strcmp(e->func->name.value,
                                                   sym->key) == 0) {
                                        lit->closure_func = e->func;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            scan = scan->next;
        }
    }

    return inst;
}

static ASTNode* shallow_clone_node(ASTNode* node) {
    if (!node) return NULL;

    ASTNode* clone = NULL;

    switch (node->type) {
        case AST_LITERAL: {
            AST_Literal* src = (AST_Literal*)node;
            AST_Literal* dst = calloc(1, sizeof(AST_Literal));
            dst->base.type = AST_LITERAL;
            dst->base.next = shallow_clone_node(src->base.next);
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->base.value.type = src->base.value.type;
            dst->base.value.line = src->base.value.line;
            dst->base.value.column = src->base.value.column;
            dst->dataType = src->dataType;
            if (src->internal_name) dst->internal_name = strdup(src->internal_name);
            if (src->call_result_class_name)
                dst->call_result_class_name = strdup(src->call_result_class_name);
            dst->is_call_function = src->is_call_function;
            dst->call_function_ParameterCount = src->call_function_ParameterCount;
            dst->call_function_Parameter = shallow_clone_node(src->call_function_Parameter);
            dst->method_arguments = shallow_clone_node(src->method_arguments);
            dst->method_arg_count = src->method_arg_count;
            dst->is_field_ref = src->is_field_ref;
            dst->field_offset = src->field_offset;
            dst->is_field_array = src->is_field_array;
            dst->is_outer_field_ref = src->is_outer_field_ref;
            dst->is_return_pointee_const = src->is_return_pointee_const;
            dst->ptr_depth = src->ptr_depth;
            dst->is_indirect_call = src->is_indirect_call;
            dst->returns_array = src->returns_array;
            dst->return_total_bytes = src->return_total_bytes;
            dst->is_method_call = src->is_method_call;
            dst->method_invoke = src->method_invoke;
            if (src->indirect_callee)
                dst->indirect_callee = shallow_clone_node(src->indirect_callee);
            if (src->returned_func_sig)
                dst->returned_func_sig = deep_copy_func_sig(src->returned_func_sig);
            if (src->multi_return_type)
                dst->multi_return_type = deep_copy_multi_return(src->multi_return_type);
            if (src->multi_ret_buf)
                dst->multi_ret_buf = strdup(src->multi_ret_buf);
            dst->is_closure_call = src->is_closure_call;
            dst->is_closure_ptr = src->is_closure_ptr;
            dst->closure_func = src->closure_func;
            if (src->param_byte_sizes && src->call_function_ParameterCount > 0) {
                dst->param_byte_sizes = malloc(sizeof(int) * (size_t)src->call_function_ParameterCount);
                memcpy(dst->param_byte_sizes, src->param_byte_sizes,
                       sizeof(int) * (size_t)src->call_function_ParameterCount);
            }
            if (src->generic_type_args && src->generic_type_arg_count > 0) {
                dst->generic_type_arg_count = src->generic_type_arg_count;
                dst->generic_type_args = malloc(sizeof(DataType) * (size_t)src->generic_type_arg_count);
                memcpy(dst->generic_type_args, src->generic_type_args,
                       sizeof(DataType) * (size_t)src->generic_type_arg_count);
            }
            if (src->generic_type_arg_names && src->generic_type_arg_count > 0) {
                dst->generic_type_arg_names = malloc(sizeof(char*) * (size_t)src->generic_type_arg_count);
                for (int i = 0; i < src->generic_type_arg_count; i++) {
                    dst->generic_type_arg_names[i] = src->generic_type_arg_names[i]
                        ? strdup(src->generic_type_arg_names[i]) : NULL;
                }
            }
            dst->is_generic_instantiation = src->is_generic_instantiation;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_BINARY: {
            AST_Binary* src = (AST_Binary*)node;
            AST_Binary* dst = calloc(1, sizeof(AST_Binary));
            dst->base.type = AST_BINARY;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->left = shallow_clone_node(src->left);
            dst->op = src->op;
            if (src->op.value) dst->op.value = strdup(src->op.value);
            dst->right = shallow_clone_node(src->right);
            dst->resultType = src->resultType;
            dst->is_unsigned = src->is_unsigned;
            dst->result_ptr_depth = src->result_ptr_depth;
            dst->elem_size = src->elem_size;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_UNARY: {
            AST_Unary* src = (AST_Unary*)node;
            AST_Unary* dst = calloc(1, sizeof(AST_Unary));
            dst->base.type = AST_UNARY;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->op = src->op;
            if (src->op.value) dst->op.value = strdup(src->op.value);
            dst->right = shallow_clone_node(src->right);
            dst->dataType = src->dataType;
            dst->ptr_step = src->ptr_step;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_VAR:
        case AST_VAR_ASS:
        case AST_UNINIT_VAR: {
            AST_Var* src = (AST_Var*)node;
            AST_Var* dst = calloc(1, sizeof(AST_Var));
            dst->base.type = src->base.type;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->name = src->name;
            if (src->name.value) dst->name.value = strdup(src->name.value);
            dst->Value = shallow_clone_node(src->Value);
            dst->type = src->type;
            dst->dataType = src->dataType;
            if (src->internal_name) dst->internal_name = strdup(src->internal_name);
            dst->dim_count = src->dim_count;
            if (src->dim_sizes && src->dim_count > 0) {
                dst->dim_sizes = malloc(sizeof(int) * (size_t)src->dim_count);
                memcpy(dst->dim_sizes, src->dim_sizes, sizeof(int) * (size_t)src->dim_count);
            }
            dst->ptr_depth = src->ptr_depth;
            dst->is_field_ref = src->is_field_ref;
            dst->field_offset = src->field_offset;
            dst->total_bytes = src->total_bytes;
            dst->modifier = src->modifier;
            dst->type_param_index = src->type_param_index;
            dst->is_closure_ptr = src->is_closure_ptr;
            dst->is_unsafe = src->is_unsafe;
            dst->is_static = src->is_static;
            if (src->func_ptr_sig) {
                dst->func_ptr_sig = deep_copy_func_sig(src->func_ptr_sig);
            }
            if (src->ptr_array_info) {
                dst->ptr_array_info = malloc(sizeof(PtrArrayInfo));
                dst->ptr_array_info->element_type = src->ptr_array_info->element_type;
                dst->ptr_array_info->dim_count = src->ptr_array_info->dim_count;
                if (src->ptr_array_info->dim_sizes && src->ptr_array_info->dim_count > 0) {
                    dst->ptr_array_info->dim_sizes = malloc(sizeof(int) * (size_t)src->ptr_array_info->dim_count);
                    memcpy(dst->ptr_array_info->dim_sizes, src->ptr_array_info->dim_sizes,
                           sizeof(int) * (size_t)src->ptr_array_info->dim_count);
                }
            }
            if (src->generic_type_args && src->generic_type_arg_count > 0) {
                dst->generic_type_arg_count = src->generic_type_arg_count;
                dst->generic_type_args = malloc(sizeof(DataType) * (size_t)src->generic_type_arg_count);
                memcpy(dst->generic_type_args, src->generic_type_args,
                       sizeof(DataType) * (size_t)src->generic_type_arg_count);
                if (src->generic_type_arg_names) {
                    dst->generic_type_arg_names = malloc(sizeof(char*) * (size_t)src->generic_type_arg_count);
                    for (int gni = 0; gni < src->generic_type_arg_count; gni++) {
                        dst->generic_type_arg_names[gni] = src->generic_type_arg_names[gni]
                            ? strdup(src->generic_type_arg_names[gni]) : NULL;
                    }
                }
            }
            clone = (ASTNode*)dst;
            break;
        }
        case AST_RETURN: {
            AST_Return* src = (AST_Return*)node;
            AST_Return* dst = calloc(1, sizeof(AST_Return));
            dst->base.type = AST_RETURN;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->returnType = src->returnType;
            dst->value = shallow_clone_node(src->value);
            dst->return_bytes = src->return_bytes;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_IF: {
            AST_If* src = (AST_If*)node;
            AST_If* dst = calloc(1, sizeof(AST_If));
            dst->base.type = AST_IF;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->Condition = shallow_clone_node(src->Condition);
            dst->code = shallow_clone_node(src->code);
            dst->elif_Count = src->elif_Count;
            if (src->elif_Count > 0) {
                dst->elif_condition = malloc(sizeof(ASTNode*) * (size_t)src->elif_Count);
                dst->Elif = malloc(sizeof(ASTNode*) * (size_t)src->elif_Count);
                for (int i = 0; i < src->elif_Count; i++) {
                    dst->elif_condition[i] = shallow_clone_node(src->elif_condition[i]);
                    dst->Elif[i] = shallow_clone_node(src->Elif[i]);
                }
            }
            dst->has_else = src->has_else;
            dst->Else = shallow_clone_node(src->Else);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_WHILE: {
            AST_While* src = (AST_While*)node;
            AST_While* dst = calloc(1, sizeof(AST_While));
            dst->base.type = AST_WHILE;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->Condition = shallow_clone_node(src->Condition);
            dst->code = shallow_clone_node(src->code);
            dst->has_do = src->has_do;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_FOR: {
            AST_For* src = (AST_For*)node;
            AST_For* dst = calloc(1, sizeof(AST_For));
            dst->base.type = AST_FOR;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->init = shallow_clone_node(src->init);
            dst->condition = shallow_clone_node(src->condition);
            dst->increment = shallow_clone_node(src->increment);
            dst->code = shallow_clone_node(src->code);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_CONDITIONAL: {
            AST_Conditional* src = (AST_Conditional*)node;
            AST_Conditional* dst = calloc(1, sizeof(AST_Conditional));
            dst->base.type = AST_CONDITIONAL;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->condition = shallow_clone_node(src->condition);
            dst->then_expr = shallow_clone_node(src->then_expr);
            dst->else_expr = shallow_clone_node(src->else_expr);
            dst->resultType = src->resultType;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_PRINT: {
            AST_Print* src = (AST_Print*)node;
            AST_Print* dst = calloc(1, sizeof(AST_Print));
            dst->base.type = AST_PRINT;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->Parameter = shallow_clone_node(src->Parameter);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_ARRAY_DECL: {
            AST_ArrayDecl* src = (AST_ArrayDecl*)node;
            AST_ArrayDecl* dst = calloc(1, sizeof(AST_ArrayDecl));
            dst->base.type = AST_ARRAY_DECL;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->elementType = src->elementType;
            if (src->element_class_name) dst->element_class_name = strdup(src->element_class_name);
            if (src->name) dst->name = strdup(src->name);
            dst->dim_count = src->dim_count;
            if (src->dim_sizes && src->dim_count > 0) {
                dst->dim_sizes = malloc(sizeof(int) * (size_t)src->dim_count);
                memcpy(dst->dim_sizes, src->dim_sizes, sizeof(int) * (size_t)src->dim_count);
            }
            if (src->internal_name) dst->internal_name = strdup(src->internal_name);
            dst->init_list = shallow_clone_node(src->init_list);
            dst->total_bytes = src->total_bytes;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_SWITCH: {
            AST_Switch* src = (AST_Switch*)node;
            AST_Switch* dst = calloc(1, sizeof(AST_Switch));
            dst->base.type = AST_SWITCH;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->condition = shallow_clone_node(src->condition);
            dst->case_count = src->case_count;
            dst->case_capacity = src->case_count;
            if (src->case_count > 0) {
                dst->cases = malloc(sizeof(AST_Case*) * (size_t)src->case_count);
                for (int i = 0; i < src->case_count; i++) {
                    dst->cases[i] = calloc(1, sizeof(AST_Case));
                    dst->cases[i]->base.type = AST_CASE;
                    dst->cases[i]->value = shallow_clone_node(src->cases[i]->value);
                    dst->cases[i]->body = shallow_clone_node(src->cases[i]->body);
                    dst->cases[i]->is_default = src->cases[i]->is_default;
                }
            }
            dst->has_default = src->has_default;
            dst->default_body = shallow_clone_node(src->default_body);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_ARRAY_ACCESS: {
            AST_ArrayAccess* src = (AST_ArrayAccess*)node;
            AST_ArrayAccess* dst = calloc(1, sizeof(AST_ArrayAccess));
            dst->base.type = AST_ARRAY_ACCESS;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->array = shallow_clone_node(src->array);
            dst->index_count = src->index_count;
            if (src->index_count > 0) {
                dst->indices = malloc(sizeof(ASTNode*) * (size_t)src->index_count);
                for (int i = 0; i < src->index_count; i++)
                    dst->indices[i] = shallow_clone_node(src->indices[i]);
            }
            dst->elementType = src->elementType;
            dst->dim_count = src->dim_count;
            dst->full_dim_count = src->full_dim_count;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_ARRAY_ASS: {
            AST_ArrayAss* src = (AST_ArrayAss*)node;
            AST_ArrayAss* dst = calloc(1, sizeof(AST_ArrayAss));
            dst->base.type = AST_ARRAY_ASS;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->target = shallow_clone_node(src->target);
            dst->value = shallow_clone_node(src->value);
            dst->elementType = src->elementType;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_MULTI_RETURN: {
            AST_MultiReturn* src = (AST_MultiReturn*)node;
            AST_MultiReturn* dst = calloc(1, sizeof(AST_MultiReturn));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_MULTI_RETURN;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->value_count = src->value_count;
            if (src->value_count > 0) {
                dst->values = malloc(sizeof(ASTNode*) * (size_t)src->value_count);
                for (int i = 0; i < src->value_count; i++)
                    dst->values[i] = shallow_clone_node(src->values[i]);
            }
            clone = (ASTNode*)dst;
            break;
        }
        case AST_VARARG_GROUP: {
            AST_VarargGroup* src = (AST_VarargGroup*)node;
            AST_VarargGroup* dst = calloc(1, sizeof(AST_VarargGroup));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_VARARG_GROUP;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->elements = shallow_clone_node(src->elements);
            dst->count = src->count;
            dst->target_elem_type = src->target_elem_type;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_RETURN_EXTRACT: {
            AST_ReturnExtract* src = (AST_ReturnExtract*)node;
            AST_ReturnExtract* dst = calloc(1, sizeof(AST_ReturnExtract));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_RETURN_EXTRACT;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->object = shallow_clone_node(src->object);
            dst->index = src->index;
            dst->sub_index = src->sub_index;
            dst->result_type = src->result_type;
            dst->is_array = src->is_array;
            dst->total_bytes = src->total_bytes;
            dst->offset = src->offset;
            dst->is_variadic_src = src->is_variadic_src;
            if (src->extracted_func_sig)
                dst->extracted_func_sig = deep_copy_func_sig(src->extracted_func_sig);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_FOREACH: {
            AST_ForEach* src = (AST_ForEach*)node;
            AST_ForEach* dst = calloc(1, sizeof(AST_ForEach));
            dst->base.type = AST_FOREACH;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->iterator = src->iterator;
            if (src->iterator.value) dst->iterator.value = strdup(src->iterator.value);
            dst->array = shallow_clone_node(src->array);
            dst->code = shallow_clone_node(src->code);
            if (src->internal_iterator) dst->internal_iterator = strdup(src->internal_iterator);
            dst->elemType = src->elemType;
            dst->has_explicit_type = src->has_explicit_type;
            dst->is_dynamic_array = src->is_dynamic_array;
            dst->total_elements = src->total_elements;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_STR_FROM_STRING: {
            AST_StrFromString* src = (AST_StrFromString*)node;
            AST_StrFromString* dst = calloc(1, sizeof(AST_StrFromString));
            dst->base.type = AST_STR_FROM_STRING;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->source = shallow_clone_node(src->source);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_DEREF: {
            AST_Deref* src = (AST_Deref*)node;
            AST_Deref* dst = calloc(1, sizeof(AST_Deref));
            dst->base.type = AST_DEREF;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->operand = shallow_clone_node(src->operand);
            dst->resultType = src->resultType;
            dst->ptr_depth = src->ptr_depth;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_ADDRESS_OF: {
            AST_AddressOf* src = (AST_AddressOf*)node;
            AST_AddressOf* dst = calloc(1, sizeof(AST_AddressOf));
            dst->base.type = AST_ADDRESS_OF;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->operand = shallow_clone_node(src->operand);
            dst->resultType = src->resultType;
            dst->ptr_depth = src->ptr_depth;
            dst->level_const_mask = src->level_const_mask;
            dst->const_value_str = src->const_value_str
                                   ? strdup(src->const_value_str) : NULL;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_MEMBER_ACCESS: {
            AST_MemberAccess* src = (AST_MemberAccess*)node;
            AST_MemberAccess* dst = calloc(1, sizeof(AST_MemberAccess));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_MEMBER_ACCESS;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->object = shallow_clone_node(src->object);
            dst->member_name = src->member_name ? strdup(src->member_name) : NULL;
            dst->member_offset = src->member_offset;
            dst->member_type = src->member_type;
            dst->member_ptr_depth = src->member_ptr_depth;
            dst->member_return_type = src->member_return_type;
            dst->is_method = src->is_method;
            dst->is_method_ptr = src->is_method_ptr;
            dst->is_field_array = src->is_field_array;
            dst->vtable_index = src->vtable_index;
            dst->method_internal = src->method_internal ? strdup(src->method_internal) : NULL;
            dst->method_param_count = src->method_param_count;
            if (src->method_param_count > 0 && src->method_param_types) {
                dst->method_param_types = malloc(sizeof(DataType) * (size_t)src->method_param_count);
                memcpy(dst->method_param_types, src->method_param_types,
                       sizeof(DataType) * (size_t)src->method_param_count);
            }
            clone = (ASTNode*)dst;
            break;
        }
        case AST_MEMBER_ASS: {
            AST_MemberAss* src = (AST_MemberAss*)node;
            AST_MemberAss* dst = calloc(1, sizeof(AST_MemberAss));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_MEMBER_ASS;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->object = shallow_clone_node(src->object);
            dst->member_name = src->member_name ? strdup(src->member_name) : NULL;
            dst->op = src->op;
            dst->value = shallow_clone_node(src->value);
            dst->member_type = src->member_type;
            dst->member_offset = src->member_offset;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_THIS: {
            AST_This* src = (AST_This*)node;
            AST_This* dst = calloc(1, sizeof(AST_This));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_THIS;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->instance_size = src->instance_size;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_NEW_INSTANCE: {
            AST_NewInstance* src = (AST_NewInstance*)node;
            AST_NewInstance* dst = calloc(1, sizeof(AST_NewInstance));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_NEW_INSTANCE;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->class_name = src->class_name ? strdup(src->class_name) : NULL;
            dst->arguments = shallow_clone_node(src->arguments);
            dst->instance_size = src->instance_size;
            dst->class_id = src->class_id;
            if (src->vtable_label)
                dst->vtable_label = strdup(src->vtable_label);
            dst->has_constructor = src->has_constructor;
            dst->constructor_name = src->constructor_name ? strdup(src->constructor_name) : NULL;
            dst->constructor_arg_count = src->constructor_arg_count;
            dst->field_init_count = src->field_init_count;
            dst->generic_type_arg_count = src->generic_type_arg_count;
            if (src->generic_type_arg_count > 0 && src->generic_type_args) {
                dst->generic_type_args = malloc(sizeof(DataType) * (size_t)src->generic_type_arg_count);
                memcpy(dst->generic_type_args, src->generic_type_args,
                       sizeof(DataType) * (size_t)src->generic_type_arg_count);
                if (src->generic_type_arg_names) {
                    dst->generic_type_arg_names = malloc(sizeof(char*) * (size_t)src->generic_type_arg_count);
                    for (int gni = 0; gni < src->generic_type_arg_count; gni++) {
                        dst->generic_type_arg_names[gni] = src->generic_type_arg_names[gni]
                            ? strdup(src->generic_type_arg_names[gni]) : NULL;
                    }
                }
            }
            if (src->field_init_count > 0 && src->field_inits) {
                dst->field_inits = malloc(sizeof(FieldInit) * (size_t)src->field_init_count);
                if (!dst->field_inits) OUT_OF_MEMORY();
                for (int fi_i = 0; fi_i < src->field_init_count; fi_i++) {
                    dst->field_inits[fi_i].offset = src->field_inits[fi_i].offset;
                    dst->field_inits[fi_i].type   = src->field_inits[fi_i].type;
                    dst->field_inits[fi_i].value_str =
                        src->field_inits[fi_i].value_str
                            ? strdup(src->field_inits[fi_i].value_str) : NULL;
                }
            }
            clone = (ASTNode*)dst;
            break;
        }
        case AST_NEW_ARRAY: {
            AST_NewArray* src = (AST_NewArray*)node;
            AST_NewArray* dst = calloc(1, sizeof(AST_NewArray));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_NEW_ARRAY;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->elementType = src->elementType;
            if (src->element_class_name)
                dst->element_class_name = strdup(src->element_class_name);
            dst->element_total_bytes = src->element_total_bytes;
            dst->dim_count = src->dim_count;
            if (src->dim_sizes && src->dim_count > 0) {
                dst->dim_sizes = malloc(sizeof(int) * (size_t)src->dim_count);
                memcpy(dst->dim_sizes, src->dim_sizes, sizeof(int) * (size_t)src->dim_count);
            }
            if (src->dim_exprs && src->dim_count > 0) {
                dst->dim_exprs = calloc((size_t)src->dim_count, sizeof(ASTNode*));
                for (int i = 0; i < src->dim_count; i++)
                    dst->dim_exprs[i] = shallow_clone_node(src->dim_exprs[i]);
            }
            clone = (ASTNode*)dst;
            break;
        }
        case AST_NULL: {
            AST_Null* src = (AST_Null*)node;
            AST_Null* dst = calloc(1, sizeof(AST_Null));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_NULL;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->ptr_depth = src->ptr_depth;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_FUNCTION: {
            AST_Function* src = (AST_Function*)node;
            AST_Function* dst = calloc(1, sizeof(AST_Function));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_FUNCTION;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            if (src->base.value.value)
                dst->base.value.value = strdup(src->base.value.value);
            dst->name = src->name;
            if (src->name.value) dst->name.value = strdup(src->name.value);
            dst->internal_name = src->internal_name ? strdup(src->internal_name) : NULL;
            dst->Modifier = src->Modifier;
            dst->is_static = src->is_static;
            dst->is_virtual = src->is_virtual;
            dst->is_override = src->is_override;
            dst->is_const_method = src->is_const_method;
            dst->return_type = src->return_type;
            dst->return_pointee_const = src->return_pointee_const;
            dst->returns_array = src->returns_array;
            dst->returns_struct = src->returns_struct;
            dst->return_ptr_depth = src->return_ptr_depth;
            dst->return_element_type = src->return_element_type;
            dst->return_dim_count = src->return_dim_count;
            dst->return_struct_bytes = src->return_struct_bytes;
            dst->return_class_name = src->return_class_name ? strdup(src->return_class_name) : NULL;
            dst->is_generic = false;
            dst->type_param_count = 0;
            dst->type_params = NULL;
            dst->has_varargs = src->has_varargs;
            dst->vararg_param_count = src->vararg_param_count;
            dst->vararg_param_index = src->vararg_param_index;
            dst->vararg_internal_name = src->vararg_internal_name ? strdup(src->vararg_internal_name) : NULL;
            if (src->vararg_param_count > 0 && src->vararg_param_indices) {
                dst->vararg_param_indices = malloc(sizeof(int) * src->vararg_param_count);
                memcpy(dst->vararg_param_indices, src->vararg_param_indices,
                       sizeof(int) * src->vararg_param_count);
            }
            if (src->vararg_param_count > 0 && src->vararg_internal_names) {
                dst->vararg_internal_names = malloc(sizeof(char*) * src->vararg_param_count);
                for (int i = 0; i < src->vararg_param_count; i++)
                    dst->vararg_internal_names[i] = src->vararg_internal_names[i]
                        ? strdup(src->vararg_internal_names[i]) : NULL;
            }
            if (src->return_dim_count > 0 && src->return_dim_sizes) {
                dst->return_dim_sizes = malloc(sizeof(int) * src->return_dim_count);
                memcpy(dst->return_dim_sizes, src->return_dim_sizes,
                       sizeof(int) * src->return_dim_count);
            }
            dst->ret_func_sig = deep_copy_func_sig(src->ret_func_sig);
            dst->multi_return = deep_copy_multi_return(src->multi_return);
            dst->max_parameters = src->max_parameters;
            if (src->parameterCount > 0) {
                dst->parameterType = malloc(sizeof(DataType) * src->parameterCount);
                memcpy(dst->parameterType, src->parameterType,
                       sizeof(DataType) * src->parameterCount);
            }
            dst->parameterCount = src->parameterCount;
            dst->Parameter = shallow_clone_node(src->Parameter);
            dst->Code = shallow_clone_node(src->Code);
            dst->Return = NULL;
            dst->multi_return_node = NULL;
            dst->return_Count = 0;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_TRY: {
            AST_Try* src = (AST_Try*)node;
            AST_Try* dst = calloc(1, sizeof(AST_Try));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_TRY;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->code = shallow_clone_node(src->code);
            dst->catch_count = src->catch_count;
            dst->catch_capacity = src->catch_count;
            if (src->catch_count > 0) {
                dst->catches = malloc(sizeof(CatchClause) * (size_t)src->catch_count);
                for (int i = 0; i < src->catch_count; i++) {
                    dst->catches[i].type_name = strdup(src->catches[i].type_name);
                    dst->catches[i].var_name = src->catches[i].var_name;
                    dst->catches[i].body = shallow_clone_node(src->catches[i].body);
                    dst->catches[i].internal_name = src->catches[i].internal_name
                        ? strdup(src->catches[i].internal_name) : NULL;
                    dst->catches[i].total_bytes = src->catches[i].total_bytes;
                    dst->catches[i].class_id = src->catches[i].class_id;
                }
            }
            dst->has_finally = src->has_finally;
            dst->finally_code = shallow_clone_node(src->finally_code);
            clone = (ASTNode*)dst;
            break;
        }
        case AST_THROW: {
            AST_Throw* src = (AST_Throw*)node;
            AST_Throw* dst = calloc(1, sizeof(AST_Throw));
            if (!dst) OUT_OF_MEMORY();
            dst->base.type = AST_THROW;
            dst->base.next = shallow_clone_node(src->base.next);
            dst->base.value = src->base.value;
            dst->value = shallow_clone_node(src->value);
            dst->thrown_class_name = src->thrown_class_name
                ? strdup(src->thrown_class_name) : NULL;
            clone = (ASTNode*)dst;
            break;
        }
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_ENUM_DEF:
        case AST_ENUM_MEMBER:
        default: {
            AST_Literal* fallback = calloc(1, sizeof(AST_Literal));
            fallback->base.type = AST_LITERAL;
            fallback->base.next = shallow_clone_node(node->next);
            fallback->base.value.type = TOKEN_NUMBER;
            fallback->base.value.value = strdup("0");
            fallback->dataType = DATA_INT;
            clone = (ASTNode*)fallback;
            break;
        }
    }

    return clone;
}

static DataType* infer_type_args(AST_Function* generic_func,
                                  ASTNode* call_args,
                                  int arg_count,
                                  int* out_type_arg_count) {
    if (!generic_func || !generic_func->is_generic) {
        *out_type_arg_count = 0;
        return NULL;
    }

    int tp_count = generic_func->type_param_count;
    DataType* type_args = malloc(sizeof(DataType) * (size_t)tp_count);
    for (int i = 0; i < tp_count; i++)
        type_args[i] = DATA_ERROR;

    ASTNode* arg = call_args;
    int param_idx = 0;

    while (arg && param_idx < generic_func->parameterCount) {
        DataType param_type = generic_func->parameterType[param_idx];
        DataType arg_type = get_expression_type(arg);

        for (int t = 0; t < tp_count; t++) {
            if (param_type == DATA_IDENTIFIER) {
                if (type_args[t] == DATA_ERROR && arg_type != DATA_ERROR) {
                    type_args[t] = arg_type;
                    break;
                }
            }
        }

        arg = arg->next;
        param_idx++;
    }

    for (int i = 0; i < tp_count; i++) {
        if (type_args[i] == DATA_ERROR)
            type_args[i] = DATA_IDENTIFIER;
    }

    *out_type_arg_count = tp_count;
    return type_args;
}

typedef struct INSTANTIATEDFUNC {
    char* instantiated_name;
    char* generic_name;
    DataType* type_args;
    int type_arg_count;
    char** type_arg_names;
    AST_Function* func;
    struct INSTANTIATEDFUNC* next;
} InstantiatedFunc;

static InstantiatedFunc* instantiated_funcs = NULL;

static AST_Function* find_instantiated(const char* generic_name,
                                         DataType* type_args, int count,
                                         char** type_arg_names) {
    for (InstantiatedFunc* ifn = instantiated_funcs; ifn; ifn = ifn->next) {
        if (strcmp(ifn->generic_name, generic_name) != 0) continue;
        if (ifn->type_arg_count != count) continue;
        bool match = true;
        for (int i = 0; i < count; i++) {
            if (ifn->type_args[i] != type_args[i]) {
                match = false;
                break;
            }
            if (type_args[i] == DATA_IDENTIFIER) {
                const char* a = (ifn->type_arg_names && ifn->type_arg_names[i])
                                ? ifn->type_arg_names[i] : "";
                const char* b = (type_arg_names && type_arg_names[i])
                                ? type_arg_names[i] : "";
                if (strcmp(a, b) != 0) {
                    match = false;
                    break;
                }
            }
        }
        if (match) {
            return ifn->func;
        }
    }
    return NULL;
}

static void record_instantiation(const char* generic_name,
                                  const char* inst_name,
                                  DataType* type_args, int count,
                                  char** type_arg_names,
                                  AST_Function* func) {
    InstantiatedFunc* ifn = malloc(sizeof(InstantiatedFunc));
    ifn->instantiated_name = strdup(inst_name);
    ifn->generic_name = strdup(generic_name);
    ifn->type_arg_count = count;
    ifn->type_args = malloc(sizeof(DataType) * (size_t)count);
    memcpy(ifn->type_args, type_args, sizeof(DataType) * (size_t)count);
    ifn->func = func;
    if (type_arg_names && count > 0) {
        ifn->type_arg_names = malloc(sizeof(char*) * (size_t)count);
        for (int i = 0; i < count; i++)
            ifn->type_arg_names[i] = type_arg_names[i] ? strdup(type_arg_names[i]) : NULL;
    } else {
        ifn->type_arg_names = NULL;
    }
    ifn->next = instantiated_funcs;
    instantiated_funcs = ifn;
}

static ClassEntry* instantiate_generic_class(ClassEntry* generic_cls,
                                             DataType* type_args,
                                             char** type_arg_names,
                                             int type_arg_count);

static void instantiate_generic_parent_list(AST_ClassDef* cd,
                                            DataType* type_args,
                                            char** type_arg_names,
                                            int type_arg_count,
                                            char*** out_names) {
    if (cd->parent_count <= 0) return;
    char** names = malloc(sizeof(char*) * (size_t)cd->parent_count);
    if (!names) OUT_OF_MEMORY();

    for (int i = 0; i < cd->parent_count; i++) {
        const char* orig = cd->parent_names[i];
        char* result = strdup(orig);
        if (orig && strchr(orig, '$') != NULL && cd->type_param_count > 0) {
            const char* dollar = strchr(orig, '$');
            int base_len = (int)(dollar - orig);
            char* base = malloc((size_t)base_len + 1);
            if (!base) OUT_OF_MEMORY();
            memcpy(base, orig, (size_t)base_len);
            base[base_len] = '\0';

            char* argbuf = strdup(dollar + 1);
            int piece_count = 1;
            for (char* p = argbuf; *p; p++) if (*p == '_') piece_count++;
            char** pieces = malloc(sizeof(char*) * (size_t)piece_count);
            if (!pieces) OUT_OF_MEMORY();
            int pc = 0;
            char* start = argbuf;
            for (char* p = argbuf; ; p++) {
                if (*p == '_' || *p == '\0') {
                    pieces[pc++] = start;
                    if (*p == '\0') break;
                    *p = '\0';
                    start = p + 1;
                }
            }

            bool changed = false;
            DataType* piece_dt = calloc((size_t)pc, sizeof(DataType));
            if (!piece_dt) OUT_OF_MEMORY();
            for (int k = 0; k < pc; k++) {
                piece_dt[k] = DATA_IDENTIFIER;
                for (int tp = 0; tp < cd->type_param_count; tp++) {
                    if (!cd->type_params[tp].name
                        || strcmp(pieces[k], cd->type_params[tp].name) != 0)
                        continue;
                    if (tp >= type_arg_count) continue;
                    if (type_arg_names && type_arg_names[tp]) {
                        pieces[k] = type_arg_names[tp];
                        changed = true;
                    } else if (type_args) {
                        pieces[k] = (char*)data_type_to_string(type_args[tp]);
                        piece_dt[k] = type_args[tp];
                        changed = true;
                    }
                    break;
                }
            }

            if (changed) {

                int total = (int)strlen(base) + 2;
                for (int k = 0; k < pc; k++)
                    total += (int)strlen(pieces[k]) + 1;
                char* rebuilt = malloc((size_t)total + 1);
                if (!rebuilt) OUT_OF_MEMORY();
                strcpy(rebuilt, base);
                strcat(rebuilt, "$");
                for (int k = 0; k < pc; k++) {
                    strcat(rebuilt, pieces[k]);
                    if (k < pc - 1) strcat(rebuilt, "_");
                }
                free(result);
                result = rebuilt;

                ClassEntry* pbase = find_class(base);
                if (pbase && pbase->is_generic
                    && find_class(result) == NULL) {
                    DataType* dts = malloc(sizeof(DataType) * (size_t)pc);
                    char** nms = calloc((size_t)pc, sizeof(char*));
                    if (!dts || !nms) OUT_OF_MEMORY();
                    for (int k = 0; k < pc; k++) {
                        dts[k] = piece_dt[k];
                        nms[k] = (dts[k] == DATA_IDENTIFIER)
                                 ? strdup(pieces[k]) : NULL;
                    }
                    instantiate_generic_class(pbase, dts, nms, pc);
                    for (int k = 0; k < pc; k++) free(nms[k]);
                    free(nms);
                    free(dts);
                }
            }
            free(piece_dt);
            free(pieces);
            free(argbuf);
            free(base);
        }
        names[i] = result;
    }
    *out_names = names;
}

static ClassEntry* find_class_generic_base(const char* name) {
    if (name == NULL) return NULL;
    const char* dollar = strchr(name, '$');
    if (dollar == NULL) return find_class(name);
    size_t blen = (size_t)(dollar - name);
    char* b = malloc(blen + 1);
    if (!b) OUT_OF_MEMORY();
    memcpy(b, name, blen);
    b[blen] = '\0';
    ClassEntry* c = find_class(b);
    free(b);
    return (c && c->is_generic) ? c : NULL;
}

static ClassEntry* resolve_generic_parent(const char* name) {
    ClassEntry* c = find_class(name);
    if (c) return c;
    if (name == NULL || strchr(name, '$') == NULL) return NULL;
    char* n_base = NULL;
    DataType* n_args = NULL;
    char** n_names = NULL;
    int n_argc = 0;
    if (!parser_lookup_nested_generic(name, &n_base, &n_args, &n_names, &n_argc))
        return NULL;
    ClassEntry* base = find_class(n_base);
    if (base && base->is_generic) {
        instantiate_generic_class(base, n_args, n_names, n_argc);
    }
    return find_class(name);
}

static ClassEntry* find_instantiated_class(const char* generic_name,
                                            DataType* type_args,
                                            char** type_arg_names,
                                            int count) {
    int len = (int)strlen(generic_name) + 2;
    for (int i = 0; i < count; i++) {
        if (type_args[i] == DATA_IDENTIFIER
            && type_arg_names && type_arg_names[i]) {
            len += (int)strlen(type_arg_names[i]) + 1;
            } else {
                len += (int)strlen(data_type_to_string(type_args[i])) + 1;
            }
    }

    char* inst_name = malloc((size_t)len + 1);
    if (!inst_name) OUT_OF_MEMORY();

    strcpy(inst_name, generic_name);
    strcat(inst_name, "$");
    for (int i = 0; i < count; i++) {
        if (type_args[i] == DATA_IDENTIFIER
            && type_arg_names && type_arg_names[i]) {
            strcat(inst_name, type_arg_names[i]);
            } else {
                strcat(inst_name, data_type_to_string(type_args[i]));
            }
        if (i < count - 1) strcat(inst_name, "_");
    }

    ClassEntry* result = find_class(inst_name);
    free(inst_name);
    return result;
}

static void fix_return_type_recursive(ASTNode* node, DataType rt) {
    while (node) {
        switch (node->type) {
        case AST_RETURN:
            ((AST_Return*)node)->returnType = rt;
            break;
        case AST_IF: {
            AST_If* n = (AST_If*)node;
            fix_return_type_recursive(n->code, rt);
            for (int i = 0; i < n->elif_Count; i++)
                fix_return_type_recursive(n->Elif[i], rt);
            fix_return_type_recursive(n->Else, rt);
            break;
        }
        case AST_WHILE:
            fix_return_type_recursive(((AST_While*)node)->code, rt);
            break;
        case AST_FOR:
            fix_return_type_recursive(((AST_For*)node)->code, rt);
            break;
        case AST_FOREACH:
            fix_return_type_recursive(((AST_ForEach*)node)->code, rt);
            break;
        case AST_SWITCH: {
            AST_Switch* sw = (AST_Switch*)node;
            for (int i = 0; i < sw->case_count; i++)
                fix_return_type_recursive(sw->cases[i]->body, rt);
            fix_return_type_recursive(sw->default_body, rt);
            break;
        }
        case AST_TRY: {
            AST_Try* tn = (AST_Try*)node;
            for (int i = 0; i < tn->catch_count; i++)
                fix_return_type_recursive(tn->catches[i].body, rt);
            fix_return_type_recursive(tn->finally_code, rt);
            break;
        }
        default:
            break;
        }
        node = node->next;
    }
}

static void analyze_instantiated_methods(ClassEntry* inst) {
    if (!inst) return;

    ClassEntry* saved_class = current_class;
    char* saved_class_name = current_class_name;
    AST_Function* saved_func = current_analyzing_function;

    current_class = inst;
    current_class_name = inst->name;

    enter_scope();
    Symbol* this_sym = create_symbol(
        (Token){.value = "this", .type = TOKEN_IDENTIFIER},
        DATA_IDENTIFIER, "this", strdup("_this"));
    this_sym->class_name = strdup(inst->name);
    insert_symbol(this_sym);

    for (int i = 0; i < inst->member_count; i++) {
        MemberInfo* mi = inst->members[i];
        if (mi->is_method || mi->is_static) continue;
        Symbol* fs = create_symbol(
            (Token){.value = mi->name, .type = TOKEN_IDENTIFIER},
            mi->type, mi->name, strdup(mi->name));
        fs->is_field = true;
        fs->field_offset = mi->offset;
        fs->field_modifier = mi->field_modifier;
        fs->is_unsafe = mi->is_unsafe;
        fs->ptr_depth = mi->ptr_depth;
        if (mi->field_class_name)
            fs->class_name = strdup(mi->field_class_name);
        else if (mi->class_name)
            fs->class_name = strdup(mi->class_name);
        insert_symbol(fs);
    }

    for (int i = 0; i < inst->member_count; i++) {
        MemberInfo* mi = inst->members[i];
        if (!mi->is_method || !mi->func || mi->inherited_from != NULL) continue;
        AST_Function* f = mi->func;
        f->Return = NULL;
        f->return_Count = 0;
        collect_function_return(f->Code, f);
        if (f->return_type != DATA_VOID && f->return_type != DATA_IDENTIFIER)
            fix_return_type_recursive(f->Code, f->return_type);
    }

    for (int i = 0; i < inst->member_count; i++) {
        MemberInfo* mi = inst->members[i];
        if (!mi->is_method || !mi->func || mi->inherited_from != NULL) continue;
        AST_Function* f = mi->func;
        current_analyzing_function = f;

        enter_scope();
        if (f->Parameter) analyze_node_list(f->Parameter);
        if (f->Code) analyze_node_list(f->Code);
        exit_scope();

        if (f->Return && f->return_type != DATA_VOID
            && f->return_type != DATA_IDENTIFIER)
            f->Return->returnType = f->return_type;
    }

    exit_scope();
    current_class = saved_class;
    current_class_name = saved_class_name;
    current_analyzing_function = saved_func;
}

static ClassEntry* instantiate_generic_class(ClassEntry* generic_cls,
                                               DataType* type_args,
                                               char** type_arg_names,
                                               int type_arg_count) {
    if (!generic_cls || !generic_cls->is_generic) return NULL;
    if (!generic_cls->generic_ast) return NULL;

    for (int nai = 0; nai < type_arg_count; nai++) {
        if (type_args[nai] != DATA_IDENTIFIER) continue;
        if (!type_arg_names || !type_arg_names[nai]) continue;
        if (!strchr(type_arg_names[nai], '$')) continue;
        if (find_class(type_arg_names[nai]) != NULL) continue;
        char* n_base = NULL;
        DataType* n_args = NULL;
        char** n_names = NULL;
        int n_argc = 0;
        if (!parser_lookup_nested_generic(type_arg_names[nai], &n_base,
                                          &n_args, &n_names, &n_argc)) continue;
        ClassEntry* n_inner = find_class(n_base);
        if (n_inner && n_inner->is_generic) {
            instantiate_generic_class(n_inner, n_args, n_names, n_argc);
        }
    }

    if (type_arg_count != generic_cls->type_param_count) {
        ERROR(ERR_SEM_ARG_COUNT_MISMATCH, LOC_NONE,
              "generic class '%s' expects %d type argument(s), got %d",
              generic_cls->name, generic_cls->type_param_count,
              type_arg_count);
        return NULL;
    }

    AST_ClassDef* cd = generic_cls->generic_ast;

    ClassEntry* existing = find_instantiated_class(
        cd->name, type_args, type_arg_names, type_arg_count);
    if (existing) return existing;

    /* 泛型类约束校验：<T : Base> / <T >: Base> */
    if (generic_cls->type_params != NULL) {
        for (int ci = 0; ci < generic_cls->type_param_count && ci < type_arg_count; ci++) {
            TypeParam* ctp = &generic_cls->type_params[ci];
            if (ctp->constraint_count == 0) continue;
            const char* cname = (type_arg_names != NULL) ? type_arg_names[ci] : NULL;
            check_type_param_constraints(ctp, type_args[ci], cname,
                                         generic_cls->generic_ast->base.value);
        }
    }

    int name_len = (int)strlen(cd->name) + 2;
    for (int i = 0; i < type_arg_count; i++) {
        if (type_args[i] == DATA_IDENTIFIER
            && type_arg_names && type_arg_names[i]) {
            name_len += (int)strlen(type_arg_names[i]) + 1;
        } else {
            name_len += (int)strlen(data_type_to_string(type_args[i])) + 1;
        }
    }

    char* inst_name = malloc((size_t)name_len + 1);
    if (!inst_name) OUT_OF_MEMORY();
    strcpy(inst_name, cd->name);
    strcat(inst_name, "$");
    for (int i = 0; i < type_arg_count; i++) {
        if (type_args[i] == DATA_IDENTIFIER
            && type_arg_names && type_arg_names[i]) {
            strcat(inst_name, type_arg_names[i]);
        } else {
            strcat(inst_name, data_type_to_string(type_args[i]));
        }
        if (i < type_arg_count - 1) strcat(inst_name, "_");
    }

    ClassEntry* inst = calloc(1, sizeof(ClassEntry));
    inst->name = inst_name;
    inst->parent_count = cd->parent_count;
    if (cd->parent_count > 0) {
        inst->parent_names = NULL;
        instantiate_generic_parent_list(cd, type_args, type_arg_names,
                                        type_arg_count, &inst->parent_names);
    }
    inst->modifier = cd->modifier;
    inst->is_generic = false;
    inst->generic_ast = cd;   /* 供变型检查读取 type_params */
    /* 保存类型实参，供变型比较使用（不依赖名字字符串拆分） */
    if (type_arg_count > 0) {
        inst->generic_type_arg_count = type_arg_count;
        inst->generic_type_args = malloc(sizeof(DataType) * (size_t)type_arg_count);
        inst->generic_type_arg_names = calloc((size_t)type_arg_count, sizeof(char*));
        if (inst->generic_type_args && inst->generic_type_arg_names) {
            for (int gi = 0; gi < type_arg_count; gi++) {
                inst->generic_type_args[gi] = type_args[gi];
                if (type_arg_names && type_arg_names[gi]) {
                    inst->generic_type_arg_names[gi] = strdup(type_arg_names[gi]);
                }
            }
        }
    }
    inst->members_resolved = true;
    inst->class_id = next_class_id++;

    bool hdr = false;
    for (int i = 0; i < cd->parent_count; i++) {
        ClassEntry* p = find_class(inst->parent_names[i] ? inst->parent_names[i] : cd->parent_names[i]);
        if (p && p->has_type_header) { hdr = true; break; }
    }
    inst->has_type_header = hdr;

    int mcount = 0;
    for (ASTNode* n = cd->members; n; n = n->next) mcount++;
    inst->member_capacity = mcount + cd->parent_count * 4 + 8;
    inst->members = calloc((size_t)inst->member_capacity, sizeof(MemberInfo*));

    int offset = 8;
    if (cd->parent_count > 0) {
        for (int i = 0; i < cd->parent_count; i++) {
            ClassEntry* parent = find_class(inst->parent_names[i] ? inst->parent_names[i] : cd->parent_names[i]);

            if (parent && parent->is_generic) {
                int nargs = parent->type_param_count < type_arg_count
                            ? parent->type_param_count : type_arg_count;
                ClassEntry* inst_parent = find_instantiated_class(
                    parent->name, type_args, type_arg_names, nargs);
                if (!inst_parent) {
                    inst_parent = instantiate_generic_class(
                        parent, type_args, type_arg_names, nargs);
                }
                if (inst_parent) parent = inst_parent;
            }

            if (parent && parent->members_resolved) {
                offset = inherit_from_parent(inst, parent, offset);
            } else if (parent) {
                WARNING(ERR_SEM_CLASS_NOT_FOUND, LOC_NONE,
                        "parent class '%s' members not resolved, skipping",
                        parent->name);
            }
        }

        for (int i = 0; i < cd->parent_count; i++) {
            ClassEntry* parent = find_class(inst->parent_names[i] ? inst->parent_names[i] : cd->parent_names[i]);
            if (parent && parent->is_generic) {
                int nargs = parent->type_param_count < type_arg_count
                            ? parent->type_param_count : type_arg_count;
                ClassEntry* inst_parent = find_instantiated_class(
                    parent->name, type_args, type_arg_names, nargs);
                if (!inst_parent) {
                    inst_parent = instantiate_generic_class(
                        parent, type_args, type_arg_names, nargs);
                }
                if (inst_parent) parent = inst_parent;
            }
            if (!parent) continue;

            for (int j = 0; j < parent->member_count; j++) {
                MemberInfo* pmi = parent->members[j];
                if (!pmi->is_method) continue;

                bool overridden = false;
                for (int k = 0; k < inst->member_count; k++) {
                    if (inst->members[k]->is_method &&
                        strcmp(inst->members[k]->name, pmi->name) == 0 &&
                        method_signature_match(inst->members[k]->func, pmi->func)) {
                        overridden = true;
                        break;
                    }
                }
                if (overridden) continue;

                if (inst->member_count >= inst->member_capacity) {
                    inst->member_capacity = inst->member_capacity
                                            ? inst->member_capacity * 2 : 8;
                    inst->members = realloc(inst->members,
                        sizeof(MemberInfo*) * (size_t)inst->member_capacity);
                    if (!inst->members) OUT_OF_MEMORY();
                }

                MemberInfo* mi = calloc(1, sizeof(MemberInfo));
                mi->name = strdup(pmi->name);
                mi->type = pmi->type;
                mi->is_method = true;
                mi->func = pmi->func;
                mi->offset = -1;
                mi->inherited_from = parent;
                inst->members[inst->member_count++] = mi;
            }
        }
    }

    for (int i = 0; i < type_arg_count && i < cd->type_param_count; i++) {
        TypeParam* tp = &cd->type_params[i];
        for (int j = 0; j < tp->constraint_count; j++) {
            TypeConstraint* tc = &tp->constraints[j];
            ClassEntry* constraint_cls = find_class(tc->class_name);
            if (!constraint_cls) {
                WARNING(ERR_SEM_CLASS_NOT_FOUND, LOC_NONE,
                        "constraint class '%s' not found for '%s'",
                        tc->class_name, tp->name);
                continue;
            }
            if (tc->kind == CONSTRAINT_SUBTYPE) {
                if (type_args[i] == DATA_IDENTIFIER) {
                } else {
                    ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
                          "type argument '%s' for '%s' cannot satisfy "
                          "':%s' constraint (not a class type)",
                          data_type_to_string(type_args[i]),
                          tp->name, tc->class_name);
                }
            } else if (tc->kind == CONSTRAINT_SUPERTYPE) {
                if (type_args[i] == DATA_IDENTIFIER) {
                } else {
                    ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
                          "type argument '%s' for '%s' cannot satisfy "
                          "'>:%s' constraint",
                          data_type_to_string(type_args[i]),
                          tp->name, tc->class_name);
                }
            }
        }
    }

    typedef struct {
        const char* old_name;
        const char* new_name;
    } MethodRename;
    MethodRename* renames = NULL;
    int rename_count = 0;
    int rename_cap = 0;

    ASTNode* m = cd->members;
    while (m) {
        if (m->type == AST_VAR || m->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)m;
            MemberInfo* mi = calloc(1, sizeof(MemberInfo));
            mi->name = strdup(var->name.value);
            mi->is_method = false;
            mi->is_static = var->is_static;
            mi->field_modifier = var->modifier;
            mi->inherited_from = NULL;
            mi->type_param_index = var->type_param_index;
            mi->is_unsafe = var->is_unsafe;
            mi->ptr_depth = var->ptr_depth;


            DataType field_type = Get_Datatype(var->type);
            if (mi->type_param_index >= 0 && mi->type_param_index < type_arg_count) {
                field_type = type_args[mi->type_param_index];
            } else if (field_type == DATA_IDENTIFIER && var->internal_name) {
                bool replaced = false;
                for (int t = 0; t < cd->type_param_count; t++) {
                    if (strcmp(var->internal_name, cd->type_params[t].name) == 0) {
                        field_type = type_args[t];
                        replaced = true;
                        break;
                    }
                }
            }
            mi->type = field_type;

            if (field_type == DATA_IDENTIFIER) {
                int tpi = mi->type_param_index;
                if (tpi < 0) {
                    for (int t = 0; t < cd->type_param_count; t++) {
                        if (var->internal_name
                            && strcmp(var->internal_name,
                                      cd->type_params[t].name) == 0) {
                            tpi = t;
                            break;
                        }
                    }
                }
                if (tpi >= 0 && tpi < type_arg_count
                    && type_arg_names && type_arg_names[tpi]) {
                    mi->class_name = strdup(type_arg_names[tpi]);
                }
            }

            if (var->Value && var->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)var->Value;
                if (lit->base.value.value) {
                    mi->init_value_str = strdup(lit->base.value.value);
                }
            }

            if (!mi->is_static) {
                mi->offset = offset;
                int sz = S_get_type_size(field_type);
                if (sz < 4) sz = 4;
                offset += sz;
            } else {
                mi->offset = -1;
            }

            inst->members[inst->member_count++] = mi;
        } else if (m->type == AST_FUNCTION) {
            AST_Function* func = (AST_Function*)m;
            MemberInfo* mi = calloc(1, sizeof(MemberInfo));
            mi->name = strdup(func->name.value);
            mi->is_method = true;
            mi->is_static = func->is_static;
            mi->inherited_from = NULL;
            mi->offset = -1;

            AST_Function* cloned = (AST_Function*)shallow_clone_node((ASTNode*)func);
            if (cloned) {
                cloned->is_generic = false;
                cloned->type_param_count = 0;
                cloned->type_params = NULL;
                int blen = (int)strlen(cd->name) + 1
                         + (int)strlen(cloned->name.value) + 1;
                char* clean_base = malloc((size_t)blen);
                snprintf(clean_base, blen, "%s_%s",
                         cd->name, cloned->name.value);
                int nl = (int)strlen(clean_base) + 2;
                for (int i = 0; i < type_arg_count; i++) {
                    const char* tn;
                    if (type_args[i] == DATA_IDENTIFIER
                        && type_arg_names && type_arg_names[i]) {
                        tn = type_arg_names[i];
                    } else if (type_args[i] == DATA_IDENTIFIER
                        && i < cd->type_param_count) {
                        TypeParam* tp = &cd->type_params[i];
                        if (tp->constraint_count > 0
                            && tp->constraints[0].kind == CONSTRAINT_SUBTYPE) {
                            tn = tp->constraints[0].class_name;
                        } else {
                            tn = "identifier";
                        }
                    } else {
                        tn = data_type_to_string(type_args[i]);
                    }
                    nl += (int)strlen(tn) + 1;
                }
                char* inst_name = malloc((size_t)nl + 1);
                if (!inst_name) OUT_OF_MEMORY();
                sprintf(inst_name, "%s$", clean_base);
                for (int i = 0; i < type_arg_count; i++) {
                    const char* tn;
                    if (type_args[i] == DATA_IDENTIFIER
                        && type_arg_names && type_arg_names[i]) {
                        tn = type_arg_names[i];
                    } else if (type_args[i] == DATA_IDENTIFIER
                        && i < cd->type_param_count) {
                        TypeParam* tp = &cd->type_params[i];
                        if (tp->constraint_count > 0
                            && tp->constraints[0].kind == CONSTRAINT_SUBTYPE) {
                            tn = tp->constraints[0].class_name;
                        } else {
                            tn = "identifier";
                        }
                    } else {
                        tn = data_type_to_string(type_args[i]);
                    }
                    strcat(inst_name, tn);
                    if (i < type_arg_count - 1) strcat(inst_name, "_");
                }
                free(clean_base);
                free(cloned->internal_name);
                cloned->internal_name = inst_name;

                if (func->internal_name
                    && strcmp(func->internal_name, cloned->internal_name) != 0) {
                    if (rename_count >= rename_cap) {
                        rename_cap = rename_cap ? rename_cap * 2 : 16;
                        renames = realloc(renames,
                                          sizeof(MethodRename) * (size_t)rename_cap);
                        if (!renames) OUT_OF_MEMORY();
                    }
                    renames[rename_count].old_name = func->internal_name;
                    renames[rename_count].new_name = cloned->internal_name;
                    rename_count++;
                }

                if (!func->is_static) {
                    if (cloned->parameterCount == 0 ||
                        cloned->parameterType[0] != DATA_IDENTIFIER) {
                        AST_Var* this_param = calloc(1, sizeof(AST_Var));
                        this_param->base.type = AST_VAR;
                        this_param->base.value = (Token){
                            .value = "this", .type = TOKEN_IDENTIFIER};
                        this_param->type = TOKEN_IDENTIFIER;
                        this_param->name = (Token){
                            .value = "this", .type = TOKEN_IDENTIFIER};
                        this_param->dataType = DATA_IDENTIFIER;
                        this_param->internal_name = strdup("_this");
                        this_param->base.next = cloned->Parameter;
                        cloned->Parameter = (ASTNode*)this_param;
                        cloned->parameterCount++;
                        memmove(cloned->parameterType + 1, cloned->parameterType,
                            sizeof(DataType) * (size_t)(cloned->parameterCount - 1));
                        cloned->parameterType[0] = DATA_IDENTIFIER;
                    }
                }
                char** saved_targ_names = g_subst_type_arg_names;
                g_subst_type_arg_names = type_arg_names;
                substitute_type_params_in_node(cloned->Code,
                    cd->type_params, cd->type_param_count, type_args);
                substitute_type_params_in_node(cloned->Parameter,
                    cd->type_params, cd->type_param_count, type_args);
                g_subst_type_arg_names = saved_targ_names;

                {
                    ASTNode* p = cloned->Parameter;
                    while (p) {
                        if (p->type == AST_VAR || p->type == AST_VAR_ASS
                            || p->type == AST_UNINIT_VAR) {
                            AST_Var* pv = (AST_Var*)p;
                            if (pv->internal_name
                                && strcmp(pv->internal_name, "_this") == 0) {
                                pv->dataType = DATA_IDENTIFIER;
                                pv->total_bytes = 4;
                                }
                            }
                        p = p->next;
                    }
                }

                ASTNode* p = cloned->Parameter;
                int pi = 0;
                while (p && pi < cloned->parameterCount) {
                    if (p->type == AST_VAR || p->type == AST_VAR_ASS
                        || p->type == AST_UNINIT_VAR) {
                        AST_Var* pv = (AST_Var*)p;
                        bool is_this_param =
                            (pv->internal_name &&
                             strcmp(pv->internal_name, "_this") == 0);
                        if (is_this_param) {
                            pv->dataType = DATA_IDENTIFIER;
                            pv->total_bytes = 4;
                            cloned->parameterType[pi] = DATA_IDENTIFIER;
                        } else if (pv->dataType == DATA_IDENTIFIER) {
                            for (int t = 0; t < cd->type_param_count; t++) {
                                if (pv->type_param_index == t) {
                                    pv->dataType = type_args[t];
                                    cloned->parameterType[pi] = type_args[t];
                                    if (type_args[t] == DATA_IDENTIFIER
                                        && type_arg_names
                                        && type_arg_names[t]) {
                                        if (pv->internal_name)
                                            free(pv->internal_name);
                                        pv->internal_name =
                                            strdup(type_arg_names[t]);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    p = p->next;
                    pi++;
                }
                if (cloned->return_type == DATA_IDENTIFIER
                    && cloned->return_class_name != NULL) {
                    for (int ti = 0; ti < cd->type_param_count; ti++) {
                        if (strcmp(cloned->return_class_name,
                                   cd->type_params[ti].name) == 0) {
                            cloned->return_type = type_args[ti];
                            free(cloned->return_class_name);
                            cloned->return_class_name =
                                (type_args[ti] == DATA_IDENTIFIER
                                 && type_arg_names && type_arg_names[ti])
                                ? strdup(type_arg_names[ti])
                                : NULL;
                            break;
                        }
                    }
                }
                if (cloned->return_type == DATA_IDENTIFIER
                    && cloned->return_class_name == NULL
                    && type_arg_count > 0) {
                    cloned->return_type = type_args[0];
                }
                add_function_with_params(cloned->name.value, cloned);
                add_function_with_params(cloned->internal_name, cloned);
                ASTNode* scan = cloned->Code;
                while (scan) {
                    if (scan->type == AST_RETURN) {
                        if (!cloned->Return) {
                            cloned->Return = (AST_Return*)scan;
                            cloned->return_Count = 1;
                        }
                    } else if (scan->type == AST_MULTI_RETURN) {
                        if (!cloned->multi_return_node) {
                            cloned->multi_return_node = (AST_MultiReturn*)scan;
                        }
                    }
                    scan = scan->next;
                }
                if (cloned->return_type != DATA_IDENTIFIER
                    && cloned->return_type != DATA_VOID) {
                    if (cloned->Return) {
                        cloned->Return->returnType = cloned->return_type;
                    }
                    scan = cloned->Code;
                    while (scan) {
                        if (scan->type == AST_RETURN) {
                            ((AST_Return*)scan)->returnType = cloned->return_type;
                        }
                        scan = scan->next;
                    }
                    }
                mi->func = cloned;
            } else {
                mi->func = func;
            }

            mi->type = mi->func->Return
                       ? mi->func->Return->returnType : DATA_VOID;

            inst->members[inst->member_count++] = mi;
        }
        m = m->next;
    }

    for (int r = 0; r < rename_count; r++) {
        for (int i = 0; i < inst->member_count; i++) {
            MemberInfo* mi = inst->members[i];
            if (!mi->is_method || !mi->func || !mi->func->Code) continue;
            if (mi->inherited_from != NULL) continue;
            replace_call_target(mi->func->Code, renames[r].old_name,
                                renames[r].new_name);
        }
    }
    free(renames);

    for (int i = 0; i < inst->member_count; i++) {
        MemberInfo* mi = inst->members[i];
        if (mi->is_method && strcmp(mi->name, cd->name) == 0) {
            inst->constructor = mi->func;
            break;
        }
    }
    if (!inst->constructor && cd->parent_count > 0) {
        for (int i = 0; i < cd->parent_count; i++) {
            ClassEntry* parent = find_class(inst->parent_names
                                             && inst->parent_names[i]
                                             ? inst->parent_names[i]
                                             : cd->parent_names[i]);
            if (parent && parent->constructor) {
                inst->constructor = parent->constructor;
                break;
            }
        }
    }

    inst->total_size = offset > 0 ? offset : 4;
    if (inst->total_size < 4) inst->total_size = 4;

    add_class(inst);

    analyze_instantiated_methods(inst);

    Symbol* sym = create_symbol(
        (Token){.value = (char*)inst_name, .type = TOKEN_IDENTIFIER},
        DATA_IDENTIFIER, (char*)inst_name, strdup(inst_name));
    insert_symbol(sym);

    return inst;
}

static bool sym_is_global(Symbol* sym) {
    if (!sym || !current_scope) return false;
    Scope* root = current_scope;
    while (root->parent) root = root->parent;
    int idx = hash_function(sym->key, root->table->size);
    for (Symbol* s = root->table->buckets[idx]; s; s = s->next) {
        if (s == sym) return true;
    }
    return false;
}

static void collect_free_vars_from_ast(ASTNode* node,
                                        char*** names, DataType** types,
                                        int* count, int* capacity) {
    if (!node) return;

    switch (node->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) {
            collect_free_vars_from_ast(lit->call_function_Parameter,
                                       names, types, count, capacity);
            collect_free_vars_from_ast(lit->method_arguments,
                                       names, types, count, capacity);
            if (lit->indirect_callee)
                collect_free_vars_from_ast(lit->indirect_callee,
                                           names, types, count, capacity);
        } else if (lit->base.value.type == TOKEN_IDENTIFIER) {
            Symbol* sym = lookup_symbol(lit->base.value.value);
            if (sym && !sym->is_function && sym->key) {
                bool is_in_current = false;
                int idx = hash_function(sym->key,
                                        current_scope->table->size);
                Symbol* cur = current_scope->table->buckets[idx];
                while (cur) {
                    if (cur == sym) { is_in_current = true; break; }
                    cur = cur->next;
                }
                if (!is_in_current && !sym_is_global(sym)) {
                    bool dup = false;
                    for (int i = 0; i < *count; i++)
                        if (strcmp((*names)[i], sym->key) == 0)
                        { dup = true; break; }
                    if (!dup) {
                        if (*count >= *capacity) {
                            *capacity = *capacity ? *capacity * 2 : 4;
                            *names = realloc(*names,
                                             sizeof(char*) * *capacity);
                            *types = realloc(*types,
                                             sizeof(DataType) * *capacity);
                        }
                        (*names)[*count] = strdup(sym->key);
                        (*types)[*count] = sym->type;
                        (*count)++;
                    }
                }
            }
        }
        break;
    }
    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)node;
        collect_free_vars_from_ast(bin->left,  names, types, count, capacity);
        collect_free_vars_from_ast(bin->right, names, types, count, capacity);
        break;
    }
    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)node;
        collect_free_vars_from_ast(un->right, names, types, count, capacity);
        break;
    }
    case AST_RETURN: {
        AST_Return* ret = (AST_Return*)node;
        collect_free_vars_from_ast(ret->value, names, types, count, capacity);
        break;
    }
    case AST_IF: {
        AST_If* ifn = (AST_If*)node;
        collect_free_vars_from_ast(ifn->Condition, names, types, count, capacity);
        collect_free_vars_from_ast(ifn->code, names, types, count, capacity);
        for (int i = 0; i < ifn->elif_Count; i++) {
            collect_free_vars_from_ast(ifn->elif_condition[i],
                                       names, types, count, capacity);
            collect_free_vars_from_ast(ifn->Elif[i],
                                       names, types, count, capacity);
        }
        collect_free_vars_from_ast(ifn->Else, names, types, count, capacity);
        break;
    }
    case AST_WHILE: {
        AST_While* w = (AST_While*)node;
        collect_free_vars_from_ast(w->Condition, names, types, count, capacity);
        collect_free_vars_from_ast(w->code, names, types, count, capacity);
        break;
    }
    case AST_FOR: {
        AST_For* f = (AST_For*)node;
        collect_free_vars_from_ast(f->init, names, types, count, capacity);
        collect_free_vars_from_ast(f->condition, names, types, count, capacity);
        collect_free_vars_from_ast(f->increment, names, types, count, capacity);
        collect_free_vars_from_ast(f->code, names, types, count, capacity);
        break;
    }
    case AST_FOREACH: {
        AST_ForEach* fe = (AST_ForEach*)node;
        collect_free_vars_from_ast(fe->array, names, types, count, capacity);
        collect_free_vars_from_ast(fe->code, names, types, count, capacity);
        break;
    }
    case AST_VAR:
    case AST_VAR_ASS:
    case AST_UNINIT_VAR: {
        AST_Var* v = (AST_Var*)node;
        collect_free_vars_from_ast(v->Value, names, types, count, capacity);
        break;
    }
    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)node;
        collect_free_vars_from_ast(tern->condition, names, types, count, capacity);
        collect_free_vars_from_ast(tern->then_expr, names, types, count, capacity);
        collect_free_vars_from_ast(tern->else_expr, names, types, count, capacity);
        break;
    }
    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)node;
        collect_free_vars_from_ast(sw->condition, names, types, count, capacity);
        for (int i = 0; i < sw->case_count; i++) {
            collect_free_vars_from_ast(sw->cases[i]->value,
                                       names, types, count, capacity);
            collect_free_vars_from_ast(sw->cases[i]->body,
                                       names, types, count, capacity);
        }
        collect_free_vars_from_ast(sw->default_body, names, types, count, capacity);
        break;
    }
    case AST_PRINT: {
        AST_Print* p = (AST_Print*)node;
        collect_free_vars_from_ast(p->Parameter, names, types, count, capacity);
        break;
    }
    case AST_ARRAY_ACCESS: {
        AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
        collect_free_vars_from_ast(acc->array, names, types, count, capacity);
        for (int i = 0; i < acc->index_count; i++)
            collect_free_vars_from_ast(acc->indices[i],
                                       names, types, count, capacity);
        break;
    }
    case AST_ARRAY_ASS: {
        AST_ArrayAss* ass = (AST_ArrayAss*)node;
        collect_free_vars_from_ast(ass->target, names, types, count, capacity);
        collect_free_vars_from_ast(ass->value, names, types, count, capacity);
        break;
    }
    case AST_DEREF: {
        AST_Deref* d = (AST_Deref*)node;
        collect_free_vars_from_ast(d->operand, names, types, count, capacity);
        break;
    }
    case AST_ADDRESS_OF: {
        AST_AddressOf* a = (AST_AddressOf*)node;
        collect_free_vars_from_ast(a->operand, names, types, count, capacity);
        break;
    }
    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)node;
        collect_free_vars_from_ast(ma->object, names, types, count, capacity);
        break;
    }
    case AST_MEMBER_ASS: {
        AST_MemberAss* mass = (AST_MemberAss*)node;
        collect_free_vars_from_ast(mass->object, names, types, count, capacity);
        collect_free_vars_from_ast(mass->value, names, types, count, capacity);
        break;
    }
    case AST_NEW_INSTANCE: {
        AST_NewInstance* ni = (AST_NewInstance*)node;
        collect_free_vars_from_ast(ni->arguments, names, types, count, capacity);
        break;
    }
    case AST_ARRAY_DECL: {
        AST_ArrayDecl* arr = (AST_ArrayDecl*)node;
        collect_free_vars_from_ast(arr->init_list, names, types, count, capacity);
        break;
    }
    case AST_MULTI_RETURN: {
        AST_MultiReturn* mr = (AST_MultiReturn*)node;
        for (int i = 0; i < mr->value_count; i++)
            collect_free_vars_from_ast(mr->values[i],
                                       names, types, count, capacity);
        break;
    }
    case AST_RETURN_EXTRACT: {
        AST_ReturnExtract* e = (AST_ReturnExtract*)node;
        collect_free_vars_from_ast(e->object, names, types, count, capacity);
        break;
    }
    case AST_VARARG_GROUP: {
        AST_VarargGroup* vg = (AST_VarargGroup*)node;
        collect_free_vars_from_ast(vg->elements, names, types, count, capacity);
        break;
    }
    case AST_STR_FROM_STRING: {
        AST_StrFromString* conv = (AST_StrFromString*)node;
        collect_free_vars_from_ast(conv->source, names, types, count, capacity);
        break;
    }
    case AST_STR_SLICE: {
        AST_StrSlice* slice = (AST_StrSlice*)node;
        collect_free_vars_from_ast(slice->source, names, types, count, capacity);
        collect_free_vars_from_ast(slice->start,  names, types, count, capacity);
        collect_free_vars_from_ast(slice->end,    names, types, count, capacity);
        break;
    }
    case AST_ARRAY_LENGTH: {
        AST_ArrayLength* len = (AST_ArrayLength*)node;
        collect_free_vars_from_ast(len->array, names, types, count, capacity);
        break;
    }
    case AST_TRY: {
        AST_Try* tn = (AST_Try*)node;
        collect_free_vars_from_ast(tn->code, names, types, count, capacity);
        for (int i = 0; i < tn->catch_count; i++)
            collect_free_vars_from_ast(tn->catches[i].body,
                                       names, types, count, capacity);
        if (tn->finally_code)
            collect_free_vars_from_ast(tn->finally_code,
                                       names, types, count, capacity);
        break;
    }
    case AST_THROW: {
        collect_free_vars_from_ast(((AST_Throw*)node)->value,
                                   names, types, count, capacity);
        break;
    }
    default:
        break;
    }

    collect_free_vars_from_ast(node->next, names, types, count, capacity);
}

static void rewrite_upvalue_refs(ASTNode* node,
                                  char** upval_names, int upval_cnt,
                                  const char* env_internal_name) {
    if (!node) return;

    switch (node->type) {
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) {
            rewrite_upvalue_refs(lit->call_function_Parameter,
                                 upval_names, upval_cnt,
                                 env_internal_name);
            rewrite_upvalue_refs(lit->method_arguments,
                                 upval_names, upval_cnt,
                                 env_internal_name);
            if (lit->indirect_callee)
                rewrite_upvalue_refs(lit->indirect_callee,
                                     upval_names, upval_cnt,
                                     env_internal_name);
            break;
        }
        if (lit->base.value.type != TOKEN_IDENTIFIER) break;
        if (lit->is_field_ref) break;

        int upval_idx = -1;
        for (int i = 0; i < upval_cnt; i++) {
            if (strcmp(lit->base.value.value, upval_names[i]) == 0) {
                upval_idx = i;
                break;
            }
        }
        if (upval_idx < 0) break;

        int offset = 4 + upval_idx * 4;

        if (lit->internal_name) free(lit->internal_name);
        lit->internal_name = strdup(env_internal_name);
        lit->is_field_ref = true;
        lit->field_offset = offset;
        lit->base.value.value = strdup(env_internal_name);
        break;
    }
    case AST_BINARY: {
        AST_Binary* bin = (AST_Binary*)node;
        rewrite_upvalue_refs(bin->left,  upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(bin->right, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_UNARY: {
        AST_Unary* un = (AST_Unary*)node;
        rewrite_upvalue_refs(un->right, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_RETURN:
        rewrite_upvalue_refs(((AST_Return*)node)->value,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_IF: {
        AST_If* ifn = (AST_If*)node;
        rewrite_upvalue_refs(ifn->Condition, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(ifn->code, upval_names, upval_cnt, env_internal_name);
        for (int i = 0; i < ifn->elif_Count; i++) {
            rewrite_upvalue_refs(ifn->elif_condition[i],
                                 upval_names, upval_cnt, env_internal_name);
            rewrite_upvalue_refs(ifn->Elif[i],
                                 upval_names, upval_cnt, env_internal_name);
        }
        rewrite_upvalue_refs(ifn->Else, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_WHILE: {
        AST_While* w = (AST_While*)node;
        rewrite_upvalue_refs(w->Condition, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(w->code, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_FOR: {
        AST_For* f = (AST_For*)node;
        rewrite_upvalue_refs(f->init, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(f->condition, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(f->increment, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(f->code, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_FOREACH: {
        AST_ForEach* fe = (AST_ForEach*)node;
        rewrite_upvalue_refs(fe->array, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(fe->code, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_VAR:
    case AST_VAR_ASS:
    case AST_UNINIT_VAR:
        rewrite_upvalue_refs(((AST_Var*)node)->Value,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_CONDITIONAL: {
        AST_Conditional* tern = (AST_Conditional*)node;
        rewrite_upvalue_refs(tern->condition, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(tern->then_expr, upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(tern->else_expr, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)node;
        rewrite_upvalue_refs(sw->condition, upval_names, upval_cnt, env_internal_name);
        for (int i = 0; i < sw->case_count; i++) {
            rewrite_upvalue_refs(sw->cases[i]->value,
                                 upval_names, upval_cnt, env_internal_name);
            rewrite_upvalue_refs(sw->cases[i]->body,
                                 upval_names, upval_cnt, env_internal_name);
        }
        rewrite_upvalue_refs(sw->default_body, upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_PRINT:
        rewrite_upvalue_refs(((AST_Print*)node)->Parameter,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_ARRAY_ACCESS: {
        AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
        rewrite_upvalue_refs(acc->array, upval_names, upval_cnt, env_internal_name);
        for (int i = 0; i < acc->index_count; i++)
            rewrite_upvalue_refs(acc->indices[i],
                                 upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_ARRAY_ASS:
        rewrite_upvalue_refs(((AST_ArrayAss*)node)->target,
                             upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(((AST_ArrayAss*)node)->value,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_DEREF:
        rewrite_upvalue_refs(((AST_Deref*)node)->operand,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_ADDRESS_OF:
        rewrite_upvalue_refs(((AST_AddressOf*)node)->operand,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_MEMBER_ACCESS:
        rewrite_upvalue_refs(((AST_MemberAccess*)node)->object,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_MEMBER_ASS:
        rewrite_upvalue_refs(((AST_MemberAss*)node)->object,
                             upval_names, upval_cnt, env_internal_name);
        rewrite_upvalue_refs(((AST_MemberAss*)node)->value,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_NEW_INSTANCE:
        rewrite_upvalue_refs(((AST_NewInstance*)node)->arguments,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_ARRAY_DECL:
        rewrite_upvalue_refs(((AST_ArrayDecl*)node)->init_list,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_MULTI_RETURN: {
        AST_MultiReturn* mr = (AST_MultiReturn*)node;
        for (int i = 0; i < mr->value_count; i++)
            rewrite_upvalue_refs(mr->values[i],
                                 upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_RETURN_EXTRACT:
        rewrite_upvalue_refs(((AST_ReturnExtract*)node)->object,
                             upval_names, upval_cnt, env_internal_name);
        break;
    case AST_TRY: {
        AST_Try* tn = (AST_Try*)node;
        rewrite_upvalue_refs(tn->code, upval_names, upval_cnt, env_internal_name);
        for (int i = 0; i < tn->catch_count; i++)
            rewrite_upvalue_refs(tn->catches[i].body,
                                 upval_names, upval_cnt, env_internal_name);
        if (tn->finally_code)
            rewrite_upvalue_refs(tn->finally_code,
                                 upval_names, upval_cnt, env_internal_name);
        break;
    }
    case AST_THROW:
        rewrite_upvalue_refs(((AST_Throw*)node)->value,
                             upval_names, upval_cnt, env_internal_name);
        break;
    default:
        break;
    }

    rewrite_upvalue_refs(node->next, upval_names, upval_cnt, env_internal_name);
}

static DataType try_container_iterable(Symbol* sym, AST_ForEach* fe) {
    if (!sym || !sym->class_name) return DATA_ERROR;
    ClassEntry* cls = find_class(sym->class_name);
    ClassEntry* base = find_class("Container");
    if (!cls || !base || !is_subclass_of(cls, base)) return DATA_ERROR;

    MemberInfo* size_m = NULL;
    MemberInfo* get_m = NULL;
    ClassEntry* walk = cls;
    while (walk) {
        for (int i = 0; i < walk->member_count; i++) {
            MemberInfo* mi = walk->members[i];
            if (!mi->is_method || !mi->func) continue;
            if (size_m == NULL && strcmp(mi->name, "size") == 0) size_m = mi;
            if (get_m == NULL && strcmp(mi->name, "get") == 0) get_m = mi;
        }
        if (walk->parent_count > 0) {
            walk = find_class(walk->parent_names[0]);
        } else {
            walk = NULL;
        }
    }
    if (!size_m || !get_m) {
        ERROR(ERR_SEM_METHOD_NOT_FOUND, LOC_NONE,
              "class '%s' inherits Container but lacks size()/get(int)",
              cls->name);
        return DATA_ERROR;
    }
    if (get_m->func->returns_struct || get_m->func->returns_array) {
        ERROR(ERR_SEM_TYPE_MISMATCH, LOC_NONE,
              "for-each on '%s' does not yet support large value-type elements",
              cls->name);
        return DATA_ERROR;
    }
    DataType get_ret = get_m->func->Return
                       ? get_m->func->Return->returnType : DATA_ERROR;

    if (get_ret == DATA_IDENTIFIER && sym->generic_type_arg_count > 0) {
        get_ret = sym->generic_type_args[0];
    }

    fe->is_container = true;
    fe->container_class_name = strdup(cls->name);
    fe->container_size_method = strdup(size_m->func->internal_name);
    fe->container_get_method = strdup(get_m->func->internal_name);
    return get_ret;
}

static void check_call_arg_const(ASTNode* arg, ASTNode* param,
                                  SourceLoc loc) {
    if (!arg || !param) return;

    if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
        AST_Var* pv = (AST_Var*)param;
        if (pv->name.value && strcmp(pv->name.value, "this") == 0) return;
        if (pv->internal_name && strcmp(pv->internal_name, "_this") == 0) return;
    }

    bool arg_is_pointee_const = false;
    if (arg->type == AST_THIS) {
        if (current_analyzing_function
            && current_analyzing_function->is_const_method) {
            arg_is_pointee_const = true;
        }
    } else if (arg->type == AST_LITERAL) {
        AST_Literal* al = (AST_Literal*)arg;
        if (al->is_call_function && al->is_return_pointee_const) {
            arg_is_pointee_const = true;
        }
        if (al->base.value.type == TOKEN_IDENTIFIER) {
            Symbol* as = lookup_symbol(al->base.value.value);
            if (as) {
                if (as->level_const_mask & 1u) arg_is_pointee_const = true;
                if (as->is_const && as->dim_count > 0) arg_is_pointee_const = true;
            }
        }
    }

    if (!arg_is_pointee_const) return;

    bool param_is_pointee_const = false;
    if (param->type == AST_ARRAY_DECL) {
        param_is_pointee_const = ((AST_ArrayDecl*)param)->is_const;
    } else if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
        AST_Var* pv = (AST_Var*)param;
        param_is_pointee_const = (pv->level_const_mask & 1u) != 0;
    }

    if (!param_is_pointee_const) {
        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST, loc,
              "cannot pass 'const' value to non-const parameter");
    }
}

static bool expr_is_const_object(ASTNode* node) {
    if (!node) return false;
    switch (node->type) {
        case AST_LITERAL: {
            AST_Literal* lit = (AST_Literal*)node;
            if (lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* sym = lookup_symbol(lit->base.value.value);
                if (sym && (sym->is_const || (sym->level_const_mask & 1u)))
                    return true;
            }
            return false;
        }
        case AST_DEREF: {
            AST_Deref* dd = (AST_Deref*)node;
            if (dd->operand->type == AST_LITERAL) {
                Symbol* sym = lookup_symbol(
                    ((AST_Literal*)dd->operand)->base.value.value);
                if (sym && (sym->level_const_mask & 1u)) return true;
            }

            if (dd->operand->type == AST_DEREF) {
                return expr_is_const_object(dd->operand);
            }
            return false;
        }
        case AST_MEMBER_ACCESS: {
            AST_MemberAccess* ma = (AST_MemberAccess*)node;
            return expr_is_const_object(ma->object);
        }
        default:
            return false;
    }
}

static ASTNode* clone_arg_literal(AST_Literal* src) {
    if (!src) return NULL;
    AST_Literal* dst = calloc(1, sizeof(AST_Literal));
    if (!dst) OUT_OF_MEMORY();
    dst->base.type = AST_LITERAL;
    dst->base.next = NULL;
    dst->base.value.type = src->base.value.type;
    dst->base.value.line = src->base.value.line;
    dst->base.value.column = src->base.value.column;
    dst->base.value.value = src->base.value.value
                            ? strdup(src->base.value.value) : NULL;
    dst->dataType = src->dataType;
    dst->internal_name = src->internal_name
                         ? strdup(src->internal_name) : NULL;
    dst->is_call_function = src->is_call_function;
    dst->is_field_ref = src->is_field_ref;
    dst->field_offset = src->field_offset;
    dst->is_field_array = src->is_field_array;
    dst->is_dynamic_array = src->is_dynamic_array;
    return (ASTNode*)dst;
}

static int thread_param_width(DataType ptype, const char* class_name) {
    if (ptype == DATA_IDENTIFIER) {
        return 4;
    }
    int w = S_get_type_size(ptype);
    if (w < 4) w = 4;
    if (w % 4 != 0) w = ((w + 3) / 4) * 4;
    return w;
}

static ASTNode* sem_wrap_function_value(ASTNode* arg) {
    if (arg == NULL) return NULL;

    AST_AddressOf* src_addr = NULL;
    AST_Literal* lit = NULL;

    if (arg->type == AST_ADDRESS_OF) {
        src_addr = (AST_AddressOf*)arg;
        if (src_addr->target_func_sig == NULL) return NULL;
        if (src_addr->operand == NULL || src_addr->operand->type != AST_LITERAL) return NULL;
        lit = (AST_Literal*)src_addr->operand;
    } else if (arg->type == AST_LITERAL) {
        lit = (AST_Literal*)arg;
    } else if (arg->type == AST_MEMBER_ACCESS) {
        AST_MemberAccess* ma = (AST_MemberAccess*)arg;
        if (!ma->is_method || ma->member_name == NULL) return NULL;

        AST_Function* target_func = NULL;
        for (FuncEntry* fe = func_table; fe; fe = fe->next) {
            if (fe->func != NULL && fe->func->internal_name != NULL
                && strcmp(fe->func->internal_name, ma->member_name) == 0) {
                target_func = fe->func;
                break;
            }
        }
        if (target_func == NULL) return NULL;

        FuncSignature* sig = func_sig_from_ast(target_func);
        if (sig == NULL) return NULL;

        AST_Literal* syn = calloc(1, sizeof(AST_Literal));
        if (!syn) OUT_OF_MEMORY();
        syn->base.type = AST_LITERAL;
        syn->base.next = NULL;
        syn->base.value.type = TOKEN_IDENTIFIER;
        syn->base.value.value = strdup(ma->member_name);
        syn->base.value.line = ma->base.value.line;
        syn->base.value.column = ma->base.value.column;
        syn->base.value.file = ma->base.value.file;
        syn->dataType = DATA_INT;
        syn->internal_name = strdup(ma->member_name);
        syn->is_call_function = false;
        lit = syn;

        AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
        if (!addr) OUT_OF_MEMORY();
        addr->base.type = AST_ADDRESS_OF;
        addr->base.next = NULL;
        addr->base.value = ma->base.value;
        addr->operand = (ASTNode*)lit;
        addr->resultType = DATA_INT;
        addr->ptr_depth = 1;
        addr->target_func_sig = sig;
        src_addr = addr;
    } else {
        return NULL;
    }

    if (lit->is_call_function) return NULL;
    if (lit->base.value.type != TOKEN_IDENTIFIER) return NULL;
    if (lit->base.value.value == NULL) return NULL;

    AST_AddressOf* addr = src_addr;
    if (addr == NULL) {
        const char* name = lit->base.value.value;

        Symbol* sym = lookup_symbol(name);
        if (sym == NULL || !sym->is_function || sym->func_sig == NULL) {
            return NULL;
        }

        AST_Function* target_func = NULL;
        for (FuncEntry* fe = func_table; fe; fe = fe->next) {
            if (fe->func != NULL && strcmp(fe->name, name) == 0) {
                target_func = fe->func;
                break;
            }
        }
        if (target_func == NULL) return NULL;

        const char* internal = target_func->internal_name;
        if (internal == NULL) internal = target_func->name.value;

        FuncSignature* sig = func_sig_from_ast(target_func);
        if (sig == NULL) return NULL;

        if (lit->internal_name) free(lit->internal_name);
        lit->internal_name = strdup(internal);

        addr = calloc(1, sizeof(AST_AddressOf));
        if (!addr) OUT_OF_MEMORY();
        addr->base.type = AST_ADDRESS_OF;
        addr->base.next = NULL;
        addr->base.value = lit->base.value;
        addr->operand = (ASTNode*)lit;
        addr->resultType = DATA_INT;
        addr->ptr_depth = 1;
        addr->target_func_sig = sig;
    }

    ClassEntry* fn_cls = find_class("Function");
    if (fn_cls == NULL) {
        return NULL;
    }
    const char* ctor_name = NULL;
    if (fn_cls->constructor != NULL
        && fn_cls->constructor->internal_name != NULL) {
        ctor_name = fn_cls->constructor->internal_name;
    }

    AST_NewInstance* ni = calloc(1, sizeof(AST_NewInstance));
    if (!ni) OUT_OF_MEMORY();
    ni->base.type = AST_NEW_INSTANCE;
    ni->base.value = lit->base.value;
    ni->class_name = strdup("Function");
    ni->arguments = (ASTNode*)addr;
    ni->constructor_arg_count = 1;
    ni->has_constructor = (ctor_name != NULL);
    ni->constructor_name = ctor_name ? strdup(ctor_name) : NULL;
    ni->generic_infer = false;
    ni->instance_size = fn_cls->total_size > 0 ? fn_cls->total_size : 20;
    ni->class_id = fn_cls->class_id;
    if (fn_cls->vtable_size > 0) {
        ni->vtable_label = class_vtable_label(fn_cls->name);
    }

    return (ASTNode*)ni;
}

static void sem_wrap_str_if_needed(ASTNode** slot, DataType param_type) {
    if (!slot || !*slot) return;
    if (param_type != DATA_STR) return;
    ASTNode* arg = *slot;
    if (arg->type == AST_STR_FROM_STRING) return;
    DataType arg_type = get_expression_type(arg);
    if (arg_type != DATA_STRING) return;
    AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
    if (!conv) OUT_OF_MEMORY();
    conv->base.type = AST_STR_FROM_STRING;
    conv->base.next = arg->next;
    conv->base.value = arg->value;
    conv->source = arg;
    arg->next = NULL;
    *slot = (ASTNode*)conv;
}

static void analyze_node(ASTNode* temp) {
    if (!temp) return;

    switch (temp->type) {
    case AST_LITERAL: {
        AST_Literal* literal = (AST_Literal*)temp;

        if (literal->base.value.type == TOKEN_IDENTIFIER
            && literal->base.value.value != NULL
            && literal->base.value.file != NULL) {
            char mangled[512];
            make_static_mangled_name(literal->base.value.file,
                                     literal->base.value.value,
                                     mangled, sizeof(mangled));
            if (mangled[0] != '\0'
                && strcmp(mangled, literal->base.value.value) != 0) {
                for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                    if (strcmp(fe->name, mangled) == 0) {
                        free(literal->base.value.value);
                        literal->base.value.value = strdup(mangled);
                        if (literal->internal_name) {
                            free(literal->internal_name);
                            literal->internal_name = NULL;
                        }
                        break;
                    }
                }
            }
        }

        if (literal->is_call_function
            && !literal->is_indirect_call
            && literal->base.value.value != NULL) {
            Symbol* callee_sym = lookup_symbol(literal->base.value.value);
            if (callee_sym != NULL
                && callee_sym->class_name != NULL
                && strcmp(callee_sym->class_name, "Function") == 0) {
                if (callee_sym->is_closure_ptr || callee_sym->is_closure_holder) {
                    literal->is_indirect_call = true;
                    literal->is_closure_call = true;
                    literal->dataType = callee_sym->func_sig
                                        ? callee_sym->func_sig->return_type
                                        : DATA_INT;
                    AST_Literal* self_lit = calloc(1, sizeof(AST_Literal));
                    if (!self_lit) OUT_OF_MEMORY();
                    self_lit->base.type = AST_LITERAL;
                    self_lit->base.next = NULL;
                    self_lit->base.value = literal->base.value;
                    self_lit->dataType = DATA_IDENTIFIER;
                    self_lit->internal_name = strdup(callee_sym->internal_name);
                    self_lit->is_call_function = false;
                    self_lit->is_closure_ptr = true;
                    literal->indirect_callee = (ASTNode*)self_lit;

                    int n = literal->call_function_ParameterCount;
                    if (n > 0) {
                        literal->param_byte_sizes = malloc(sizeof(int) * n);
                        if (!literal->param_byte_sizes) OUT_OF_MEMORY();
                        for (int i = 0; i < n; i++) {
                            literal->param_byte_sizes[i] = 4;
                        }
                    }
                } else if (callee_sym->func_sig != NULL) {
                    literal->is_indirect_call = true;
                    literal->dataType = callee_sym->func_sig->return_type;

                    if (callee_sym->func_sig->multi_return != NULL) {
                        literal->multi_return_type =
                            deep_copy_multi_return(callee_sym->func_sig->multi_return);
                        literal->returns_array = true;
                        literal->dataType = DATA_VOID;
                        int mr_total = 0;
                        for (int mi = 0; mi < callee_sym->func_sig->multi_return->count; mi++) {
                            mr_total += callee_sym->func_sig->multi_return->is_variadic[mi] ? 8 : 4;
                        }
                        if (mr_total < 4) mr_total = 4;
                        literal->return_total_bytes = mr_total;
                    }

                    AST_Literal* self_lit = calloc(1, sizeof(AST_Literal));
                    if (!self_lit) OUT_OF_MEMORY();
                    self_lit->base.type = AST_LITERAL;
                    self_lit->base.next = NULL;
                    self_lit->base.value = literal->base.value;
                    self_lit->dataType = DATA_IDENTIFIER;
                    self_lit->internal_name = strdup(callee_sym->internal_name);
                    self_lit->is_call_function = false;
                    self_lit->is_field_ref = callee_sym->is_field;
                    self_lit->field_offset = callee_sym->field_offset;

                    AST_MemberAccess* raw_ma = calloc(1, sizeof(AST_MemberAccess));
                    if (!raw_ma) OUT_OF_MEMORY();
                    raw_ma->base.type = AST_MEMBER_ACCESS;
                    raw_ma->base.next = NULL;
                    raw_ma->base.value = literal->base.value;
                    raw_ma->object = (ASTNode*)self_lit;
                    raw_ma->member_name = strdup("_raw");
                    raw_ma->member_offset = 8;
                    raw_ma->member_type = DATA_INT;
                    raw_ma->vtable_index = -1;

                    literal->indirect_callee = (ASTNode*)raw_ma;

                    int n = literal->call_function_ParameterCount;
                    if (n > 0 && callee_sym->func_sig->param_count == n) {
                        literal->param_byte_sizes = malloc(sizeof(int) * n);
                        if (!literal->param_byte_sizes) OUT_OF_MEMORY();
                        for (int i = 0; i < n; i++) {
                            int sz = S_get_type_size(callee_sym->func_sig->param_types[i]);
                            if (sz < 4) sz = 4;
                            literal->param_byte_sizes[i] = sz;
                        }
                    }
                } else {
                    AST_Literal* self_lit = calloc(1, sizeof(AST_Literal));
                    if (!self_lit) OUT_OF_MEMORY();
                    self_lit->base.type = AST_LITERAL;
                    self_lit->base.next = NULL;
                    self_lit->base.value = literal->base.value;
                    self_lit->dataType = DATA_IDENTIFIER;
                    self_lit->internal_name = strdup(callee_sym->internal_name);
                    self_lit->is_call_function = false;
                    self_lit->is_field_ref = callee_sym->is_field;
                    self_lit->field_offset = callee_sym->field_offset;

                    AST_MemberAccess* ma = calloc(1, sizeof(AST_MemberAccess));
                    if (!ma) OUT_OF_MEMORY();
                    ma->base.type = AST_MEMBER_ACCESS;
                    ma->base.next = NULL;
                    ma->base.value = literal->base.value;
                    ma->object = (ASTNode*)self_lit;
                    ma->member_name = strdup("call");
                    ma->member_offset = -1;
                    ma->member_type = DATA_ERROR;
                    ma->vtable_index = -1;

                    literal->is_indirect_call = false;
                    literal->method_invoke = true;
                    literal->method_arguments = literal->call_function_Parameter;
                    literal->method_arg_count = literal->call_function_ParameterCount;
                    literal->call_function_Parameter = (ASTNode*)ma;
                    literal->call_function_ParameterCount = literal->method_arg_count + 1;
                    literal->base.value.value = strdup("call");
                }
            }
        }

        if (!literal->is_call_function
            && literal->base.value.type == TOKEN_IDENTIFIER) {
            Symbol* sym = lookup_symbol(literal->base.value.value);
            if (sym && sym->is_const && sym->const_value_str) {
                free(literal->base.value.value);
                literal->base.value.value = strdup(sym->const_value_str);
                if (is_float_type(sym->type) || is_ufloat_type(sym->type)) {
                    literal->base.value.type = TOKEN_FLOAT_LITERAL;
                } else {
                    literal->base.value.type = TOKEN_NUMBER;
                }
                literal->dataType = sym->type;
                if (literal->internal_name) {
                    free(literal->internal_name);
                    literal->internal_name = NULL;
                }
                break;
            }
        }

        if (literal->internal_name != NULL &&
            literal->base.value.type == TOKEN_NUMBER &&
            strcmp(literal->base.value.value, "0") == 0) {
            ClassEntry* cls = find_class(literal->internal_name);
            if (cls) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", 4);
                free(literal->base.value.value);
                literal->base.value.value = strdup(buf);
            } else {
                bool is_type_param = false;
                if (current_analyzing_function
                    && current_analyzing_function->is_generic) {
                    for (int t = 0; t < current_analyzing_function->type_param_count; t++) {
                        if (strcmp(literal->internal_name,
                                   current_analyzing_function->type_params[t].name) == 0) {
                            is_type_param = true;
                            break;
                                   }
                    }
                    }
                if (!is_type_param && current_class && current_class->is_generic
                    && current_class->generic_ast) {
                    AST_ClassDef* gcd = current_class->generic_ast;
                    for (int t = 0; t < gcd->type_param_count; t++) {
                        if (strcmp(literal->internal_name,
                                   gcd->type_params[t].name) == 0) {
                            is_type_param = true;
                            break;
                                   }
                    }
                    }
                if (!is_type_param) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(literal->base.value),
                          "unknown class '%s' in sizeof",
                          literal->internal_name);
                }
            }
            literal->dataType = DATA_INT;
            break;
        }

        if (literal->is_call_function) {
            if (literal->is_indirect_call && literal->indirect_callee != NULL) {
                if (literal->base.value.value != NULL) {
                    Symbol* ind_sym = lookup_symbol(literal->base.value.value);
                    if (ind_sym && ind_sym->func_sig && ind_sym->func_sig->param_func_sigs) {
                        ASTNode** fp_prev = &literal->call_function_Parameter;
                        ASTNode* fp_arg = literal->call_function_Parameter;
                        int fp_idx = 0;
                        while (fp_arg && fp_idx < ind_sym->func_sig->param_count) {
                            if (ind_sym->func_sig->param_func_sigs[fp_idx] != NULL
                                && fp_arg->type == AST_LITERAL) {
                                AST_Literal* fp_lit = (AST_Literal*)fp_arg;
                                if (!fp_lit->is_call_function
                                    && fp_lit->base.value.type == TOKEN_IDENTIFIER) {
                                    Symbol* fp_sym = lookup_symbol(fp_lit->base.value.value);
                                    if (fp_sym && fp_sym->is_function && fp_sym->func_sig) {
                                        if (fp_lit->internal_name) free(fp_lit->internal_name);
                                        fp_lit->internal_name = fp_sym->internal_name
                                            ? strdup(fp_sym->internal_name)
                                            : strdup(fp_sym->key);
                                        AST_AddressOf* fp_addr = calloc(1, sizeof(AST_AddressOf));
                                        if (!fp_addr) OUT_OF_MEMORY();
                                        fp_addr->base.type = AST_ADDRESS_OF;
                                        fp_addr->base.next = fp_arg->next;
                                        fp_addr->base.value = fp_lit->base.value;
                                        fp_addr->operand = fp_arg;
                                        fp_addr->resultType = DATA_INT;
                                        fp_addr->ptr_depth = 1;
                                        fp_addr->target_func_sig = deep_copy_func_sig(
                                            ind_sym->func_sig->param_func_sigs[fp_idx]);
                                        fp_arg->next = NULL;
                                        *fp_prev = (ASTNode*)fp_addr;
                                        fp_prev = &fp_addr->base.next;
                                        fp_arg = fp_addr->base.next;
                                        fp_idx++;
                                        continue;
                                    }
                                }
                            }
                            fp_prev = &fp_arg->next;
                            fp_arg = fp_arg->next;
                            fp_idx++;
                        }
                    }
                }
                if (literal->indirect_callee->type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* ind_acc = (AST_ArrayAccess*)literal->indirect_callee;
                    Symbol* ind_arr_sym = NULL;
                    if (ind_acc->array != NULL) {
                        if (ind_acc->array->type == AST_LITERAL) {
                            AST_Literal* ind_al = (AST_Literal*)ind_acc->array;
                            if (!ind_al->is_call_function)
                                ind_arr_sym = lookup_symbol(ind_al->base.value.value);
                        } else if (ind_acc->array->type == AST_VAR
                                   || ind_acc->array->type == AST_VAR_ASS) {
                            ind_arr_sym = lookup_symbol(((AST_Var*)ind_acc->array)->name.value);
                        }
                    }
                    if (ind_arr_sym != NULL
                        && ind_arr_sym->class_name != NULL
                        && strcmp(ind_arr_sym->class_name, "Function") == 0) {
                        AST_MemberAccess* raw_ma = calloc(1, sizeof(AST_MemberAccess));
                        if (!raw_ma) OUT_OF_MEMORY();
                        raw_ma->base.type = AST_MEMBER_ACCESS;
                        raw_ma->base.next = NULL;
                        raw_ma->base.value = literal->base.value;
                        raw_ma->object = literal->indirect_callee;
                        raw_ma->member_name = strdup("_raw");
                        raw_ma->member_offset = 8;
                        raw_ma->member_type = DATA_INT;
                        raw_ma->vtable_index = -1;
                        literal->indirect_callee = (ASTNode*)raw_ma;
                    }
                }
                analyze_node(literal->indirect_callee);
                if (literal->indirect_callee->type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* acc = (AST_ArrayAccess*)literal->indirect_callee;
                    Symbol* orig_sym = NULL;
                    if (acc->array->type == AST_LITERAL) {
                        orig_sym = lookup_symbol(((AST_Literal*)acc->array)->base.value.value);
                    } else if (acc->array->type == AST_VAR || acc->array->type == AST_VAR_ASS) {
                        orig_sym = lookup_symbol(((AST_Var*)acc->array)->name.value);
                    }
                    if (orig_sym && orig_sym->func_sig) {
                        literal->dataType = orig_sym->func_sig->return_type;
                        if (orig_sym->func_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(orig_sym->func_sig->multi_return);
                        }
                        goto indirect_sig_found;
                    }
                    if (orig_sym && orig_sym->ptr_depth > 0) {
                        ASTNode* arg = literal->call_function_Parameter;
                        bool has_vg = false;
                        while (arg) {
                            if (arg->type == AST_VARARG_GROUP) {
                                has_vg = true;
                                break;
                            }
                            arg = arg->next;
                        }
                        if (has_vg) {
                            MultiReturnType* inferred = calloc(1, sizeof(MultiReturnType));
                            inferred->count = 1;
                            inferred->types = malloc(sizeof(DataType));
                            inferred->types[0] = DATA_INT;
                            inferred->is_variadic = malloc(sizeof(bool));
                            inferred->is_variadic[0] = true;
                            literal->multi_return_type = inferred;
                            literal->dataType = DATA_INT;
                            literal->returns_array = true;
                            literal->return_total_bytes = 8;
                            goto indirect_sig_found;
                        }
                    }
                }
                if (literal->indirect_callee->type == AST_MEMBER_ACCESS) {
                    AST_MemberAccess* ma = (AST_MemberAccess*)literal->indirect_callee;
                    if (ma->is_method && ma->method_internal) {
                        AST_Function* mf = find_function_by_signature(ma->method_internal, 0, NULL);
                        if (mf) {
                            literal->dataType = mf->Return ? mf->Return->returnType : DATA_VOID;
                            if (mf->multi_return) {
                                literal->multi_return_type = deep_copy_multi_return(mf->multi_return);
                            }
                            if (mf->ret_func_sig != NULL) {
                                literal->returned_func_sig = deep_copy_func_sig(mf->ret_func_sig);
                            }
                            goto indirect_sig_found;
                        }
                    }
                    if (literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                        AST_ReturnExtract* ext = (AST_ReturnExtract*)literal->indirect_callee;
                        if (ext->extracted_func_sig) {
                            literal->dataType = ext->extracted_func_sig->return_type;
                            if (ext->extracted_func_sig->multi_return) {
                                literal->multi_return_type =
                                    deep_copy_multi_return(ext->extracted_func_sig->multi_return);
                            }
                            literal->returned_func_sig = deep_copy_func_sig(ext->extracted_func_sig);
                        } else {
                            literal->dataType = DATA_INT;
                            if (ext->is_variadic_src && ext->total_bytes > 0) {
                                literal->returns_array = true;
                                literal->return_total_bytes = ext->total_bytes;
                            }
                        }
                        goto indirect_sig_found;
                    }
                }
                indirect_sig_found:;
                if (literal->multi_return_type == NULL
                    && literal->indirect_callee != NULL
                    && literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* icall = (AST_Literal*)literal->indirect_callee;
                    if (icall->is_call_function) {
                        ASTNode* walk = (ASTNode*)icall;
                        while (walk && walk->type == AST_LITERAL
                               && ((AST_Literal*)walk)->is_call_function
                               && ((AST_Literal*)walk)->is_indirect_call) {
                            walk = ((AST_Literal*)walk)->indirect_callee;
                               }
                        if (walk && walk->type == AST_RETURN_EXTRACT) {
                            AST_ReturnExtract* re = (AST_ReturnExtract*)walk;
                            if (re->extracted_func_sig) {
                                literal->dataType = re->extracted_func_sig->return_type;
                                if (re->extracted_func_sig->multi_return) {
                                    literal->multi_return_type =
                                        deep_copy_multi_return(re->extracted_func_sig->multi_return);
                                }
                            }
                        }
                        if (literal->multi_return_type == NULL && icall->multi_return_type) {
                            literal->dataType = icall->dataType;
                            literal->multi_return_type =
                                deep_copy_multi_return(icall->multi_return_type);
                        }
                        if (literal->multi_return_type == NULL
                            && icall->returned_func_sig
                            && icall->returned_func_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(
                                    icall->returned_func_sig->multi_return);
                        }
                    }
                }
                 if (literal->returned_func_sig == NULL
                    && literal->indirect_callee != NULL
                    && literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                    AST_ReturnExtract* ext = (AST_ReturnExtract*)literal->indirect_callee;
                    if (ext->extracted_func_sig) {
                        FuncSignature* sig = ext->extracted_func_sig;
                        literal->returned_func_sig = deep_copy_func_sig(sig);
                        if (sig->multi_return && !literal->multi_return_type) {
                            literal->multi_return_type = deep_copy_multi_return(sig->multi_return);
                        }
                    }
                }
                ASTNode* arg = literal->call_function_Parameter;
                int arg_count = 0;
                while (arg) {
                    analyze_node(arg);
                    arg_count++;
                    arg = arg->next;
                }
                if (literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* callee_lit = (AST_Literal*)literal->indirect_callee;
                    if (callee_lit->is_call_function && callee_lit->internal_name) {
                        Symbol* callee_sym = lookup_symbol(callee_lit->base.value.value);
                        if (callee_sym && callee_sym->is_function && callee_sym->func_sig) {
                            if (callee_sym->func_sig->return_type == DATA_INT
                                && callee_sym->func_sig->multi_return) {
                                literal->dataType = DATA_INT;
                                literal->multi_return_type =
                                    deep_copy_multi_return(callee_sym->func_sig->multi_return);
                                break;
                            }
                        }
                        if (callee_lit->multi_return_type) {
                            literal->dataType = DATA_INT;
                            literal->multi_return_type =
                                deep_copy_multi_return(callee_lit->multi_return_type);
                            break;
                        }
                    }
                    Symbol* sym = lookup_symbol(callee_lit->base.value.value);
                    if (sym && sym->func_sig) {
                        FuncSignature* effective_sig = sym->func_sig;
                        if (callee_lit->is_call_function
                            && sym->func_sig->ret_func_sig != NULL) {
                            effective_sig = sym->func_sig->ret_func_sig;
                            }

                        literal->dataType = effective_sig->return_type;
                        if (effective_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(effective_sig->multi_return);
                        }
                        if (effective_sig->param_count != arg_count) {
                            ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                                  SEMLOC(literal->base.value),
                                  "function pointer expects %d arguments, got %d",
                                  effective_sig->param_count, arg_count);
                        }
                        break;
                    }
                }
                if (literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* ic = (AST_Literal*)literal->indirect_callee;
                    if (ic->returned_func_sig) {
                        if (!literal->returned_func_sig
                            && ic->returned_func_sig->ret_func_sig != NULL) {
                            literal->returned_func_sig =
                                deep_copy_func_sig(ic->returned_func_sig);
                            }
                        if (!literal->multi_return_type
                            && ic->returned_func_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(
                                    ic->returned_func_sig->multi_return);
                            }
                    }
                    if (!literal->multi_return_type && ic->multi_return_type) {
                        literal->multi_return_type =
                            deep_copy_multi_return(ic->multi_return_type);
                    }
                }
                if (literal->multi_return_type == NULL
                    && literal->indirect_callee->type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* acc = (AST_ArrayAccess*)literal->indirect_callee;
                    Symbol* arr_sym = NULL;
                    if (acc->array->type == AST_LITERAL) {
                        AST_Literal* arr_lit = (AST_Literal*)acc->array;
                        arr_sym = lookup_symbol(arr_lit->base.value.value);
                        if (!arr_sym && arr_lit->internal_name)
                            arr_sym = lookup_symbol(arr_lit->internal_name);
                    } else if (acc->array->type == AST_VAR || acc->array->type == AST_VAR_ASS) {
                        AST_Var* arr_var = (AST_Var*)acc->array;
                        arr_sym = lookup_symbol(arr_var->name.value);
                        if (!arr_sym && arr_var->internal_name)
                            arr_sym = lookup_symbol(arr_var->internal_name);
                    }
                    if (arr_sym && arr_sym->func_sig) {
                        literal->dataType = arr_sym->func_sig->return_type;
                        if (arr_sym->func_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(arr_sym->func_sig->multi_return);
                        }
                        if (arr_sym->func_sig->return_type == DATA_INT
                            && arr_sym->func_sig->multi_return) {
                            goto indirect_sig_found;
                        }
                    }
                }
                if (literal->multi_return_type == NULL
                    && literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                    AST_ReturnExtract* ext =
                        (AST_ReturnExtract*)literal->indirect_callee;
                    if (ext->extracted_func_sig) {
                        literal->dataType = ext->extracted_func_sig->return_type;
                        if (ext->extracted_func_sig->multi_return) {
                            literal->multi_return_type =
                                deep_copy_multi_return(
                                    ext->extracted_func_sig->multi_return);
                        }
                    }
                }
                if (literal->indirect_callee->type == AST_RETURN_EXTRACT) {
                    AST_ReturnExtract* ext = (AST_ReturnExtract*)literal->indirect_callee;
                    if (ext->extracted_func_sig) {
                        int expected = ext->extracted_func_sig->param_count;
                        if (expected != arg_count) {
                            bool ok = false;
                            if (ext->extracted_func_sig->has_varargs) {
                                int lit_temp = 0;
                                for (int i = 0; i < expected; i++) {
                                    bool is_va = false;
                                    for (int v = 0; v < ext->extracted_func_sig->vararg_param_count; v++) {
                                        if (ext->extracted_func_sig->vararg_param_indices[v] == i)
                                            is_va = true;
                                    }
                                    if (!is_va) lit_temp++;
                                }
                                if (arg_count >= lit_temp) ok = true;
                            }
                            if (!ok) {
                                ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                                      SEMLOC(literal->base.value),
                                      "function pointer expects %d argument(s), got %d",
                                      expected, arg_count);
                            }
                        }
                    }
                }
                if (literal->multi_return_type == NULL
                    && literal->returned_func_sig == NULL) {
                    literal->dataType = DATA_INT;
                }
                if (literal->returned_func_sig != NULL
                    && literal->multi_return_type == NULL
                    && literal->returned_func_sig->multi_return != NULL) {
                    literal->multi_return_type =
                        deep_copy_multi_return(
                            literal->returned_func_sig->multi_return);
                }

                if (!literal->is_closure_call && literal->indirect_callee != NULL
                    && literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* cal = (AST_Literal*)literal->indirect_callee;
                    Symbol* cal_sym = lookup_symbol(cal->base.value.value);
                    if (!cal_sym && cal->internal_name)
                        cal_sym = lookup_symbol(cal->internal_name);
                    if (cal_sym && cal_sym->is_closure_ptr) {
                        literal->is_closure_call = true;
                        if (cal_sym->func_sig) {
                            if (!literal->returned_func_sig)
                                literal->returned_func_sig = deep_copy_func_sig(cal_sym->func_sig);
                            if (cal_sym->func_sig->multi_return && !literal->multi_return_type)
                                literal->multi_return_type =
                                    deep_copy_multi_return(cal_sym->func_sig->multi_return);
                        }
                    }
                }
                if (!literal->is_closure_call
                    && literal->indirect_callee != NULL
                    && literal->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* cal = (AST_Literal*)literal->indirect_callee;
                    Symbol* cal_sym = lookup_symbol(cal->base.value.value);
                    if (!cal_sym && cal->internal_name)
                        cal_sym = lookup_symbol(cal->internal_name);
                    if (cal_sym && cal_sym->is_closure_ptr) {
                        literal->is_closure_call = true;
                        if (cal_sym->func_sig) {
                            if (!literal->returned_func_sig)
                                literal->returned_func_sig =
                                    deep_copy_func_sig(cal_sym->func_sig);
                            if (cal_sym->func_sig->multi_return
                                && !literal->multi_return_type)
                                literal->multi_return_type =
                                    deep_copy_multi_return(
                                        cal_sym->func_sig->multi_return);
                        }
                    }
                }
                break;
            }

            if (literal->method_invoke &&
                literal->call_function_Parameter &&
                literal->call_function_Parameter->type == AST_MEMBER_ACCESS) {
                AST_MemberAccess* ma = (AST_MemberAccess*)literal->call_function_Parameter;

                if (ma->member_name != NULL
                    && strcmp(ma->member_name, "call") == 0
                    && ma->object != NULL) {
                    analyze_node(ma->object);
                    const char* obj_cls_name = infer_static_class_name(ma->object);
                    if (obj_cls_name != NULL
                        && strcmp(obj_cls_name, "Function") == 0) {
                        FuncSignature* call_sig = NULL;
                        if (ma->object->type == AST_LITERAL) {
                            AST_Literal* obj_lit = (AST_Literal*)ma->object;
                            if (!obj_lit->is_call_function
                                && obj_lit->base.value.value) {
                                Symbol* fn_sym =
                                    lookup_symbol(obj_lit->base.value.value);
                                if (fn_sym && fn_sym->func_sig) {
                                    call_sig = fn_sym->func_sig;
                                }
                            } else if (obj_lit->is_call_function
                                       && obj_lit->returned_func_sig != NULL) {
                                call_sig = obj_lit->returned_func_sig;
                            }
                        } else if (ma->object->type == AST_VAR
                                   || ma->object->type == AST_VAR_ASS) {
                            AST_Var* obj_var = (AST_Var*)ma->object;
                            Symbol* fn_sym = lookup_symbol(obj_var->name.value);
                            if (fn_sym && fn_sym->func_sig) {
                                call_sig = fn_sym->func_sig;
                            }
                        } else if (ma->object->type == AST_THIS) {
                            Symbol* fn_sym = lookup_symbol("this");
                            if (fn_sym && fn_sym->func_sig) {
                                call_sig = fn_sym->func_sig;
                            }
                        }

                        if (call_sig != NULL) {
                            bool obj_is_closure = false;
                            if (ma->object->type == AST_LITERAL) {
                                AST_Literal* ol = (AST_Literal*)ma->object;
                                if (!ol->is_call_function
                                    && ol->base.value.value) {
                                    Symbol* os = lookup_symbol(
                                        ol->base.value.value);
                                    if (os && os->is_closure_ptr) {
                                        obj_is_closure = true;
                                    }
                                }
                            } else if (ma->object->type == AST_VAR
                                       || ma->object->type == AST_VAR_ASS) {
                                Symbol* os = lookup_symbol(
                                    ((AST_Var*)ma->object)->name.value);
                                if (os && os->is_closure_ptr) {
                                    obj_is_closure = true;
                                }
                            }

                            if (obj_is_closure) {
                                literal->is_indirect_call = true;
                                literal->is_closure_call = true;
                                literal->indirect_callee = ma->object;
                                ma->object = NULL;
                            } else {
                                AST_MemberAccess* raw_access =
                                    calloc(1, sizeof(AST_MemberAccess));
                                if (!raw_access) OUT_OF_MEMORY();
                                raw_access->base.type = AST_MEMBER_ACCESS;
                                raw_access->base.next = NULL;
                                raw_access->base.value = ma->base.value;
                                raw_access->object = ma->object;
                                raw_access->member_name = strdup("_raw");
                                raw_access->member_offset = 8;
                                raw_access->member_type = DATA_INT;
                                raw_access->member_return_type = DATA_INT;
                                raw_access->method_internal = NULL;
                                raw_access->const_value_str = NULL;
                                raw_access->vtable_index = -1;

                                ma->object = NULL;
                                literal->is_indirect_call = true;
                                literal->is_closure_call = false;
                                literal->indirect_callee = (ASTNode*)raw_access;
                            }

                            literal->call_function_Parameter = literal->method_arguments;
                            literal->call_function_ParameterCount = literal->method_arg_count;
                            literal->method_arguments = NULL;
                            literal->method_arg_count = 0;
                            literal->method_invoke = false;

                            if (literal->internal_name) {
                                free(literal->internal_name);
                                literal->internal_name = NULL;
                            }
                            if (literal->base.value.value) {
                                free(literal->base.value.value);
                                literal->base.value.value = NULL;
                            }

                            literal->dataType = call_sig->return_type;
                            if (call_sig->return_class_name != NULL) {
                                if (literal->call_result_class_name) {
                                    free(literal->call_result_class_name);
                                }
                                literal->call_result_class_name =
                                    strdup(call_sig->return_class_name);
                            }

                            if (call_sig->multi_return != NULL) {
                                literal->multi_return_type =
                                    deep_copy_multi_return(call_sig->multi_return);
                                literal->returns_array = true;
                                int total = 0;
                                for (int mi = 0;
                                     mi < call_sig->multi_return->count; mi++) {
                                    total += call_sig->multi_return->is_variadic[mi]
                                             ? 8 : 4;
                                }
                                if (total < 4) total = 4;
                                literal->return_total_bytes = total;
                            }

                            if (call_sig->ret_func_sig != NULL) {
                                literal->returned_func_sig =
                                    deep_copy_func_sig(call_sig->ret_func_sig);
                            }

                            if (literal->param_byte_sizes) {
                                free(literal->param_byte_sizes);
                                literal->param_byte_sizes = NULL;
                            }
                            if (call_sig->param_count > 0) {
                                literal->param_byte_sizes =
                                    malloc(sizeof(int) * call_sig->param_count);
                                if (!literal->param_byte_sizes) OUT_OF_MEMORY();
                                for (int pi = 0;
                                     pi < call_sig->param_count; pi++) {
                                    int sz = S_get_type_size(call_sig->param_types[pi]);
                                    if (sz < 4) sz = 4;
                                    literal->param_byte_sizes[pi] = sz;
                                }
                            }

                            ASTNode* arg = literal->call_function_Parameter;
                            while (arg) {
                                analyze_node(arg);
                                arg = arg->next;
                            }

                            free(ma->member_name);
                            ma->member_name = NULL;
                            free(ma);

                            goto static_method_done;
                        }
                    }
                }

                ClassEntry* target_class = NULL;
                Symbol* obj_sym = NULL;

                if (ma->object->type == AST_LITERAL) {
                    AST_Literal* obj_lit = (AST_Literal*)ma->object;
                    ClassEntry* cls = find_class(obj_lit->base.value.value);
                    if (cls == NULL) {
                        Symbol* nested_sym = lookup_symbol(obj_lit->base.value.value);
                        if (nested_sym != NULL && nested_sym->is_type_name
                            && nested_sym->class_name != NULL) {
                            cls = find_class(nested_sym->class_name);
                        }
                    }
                    ClassEntry* static_walk = cls;
                    while (static_walk) {
                        for (int i = 0; i < static_walk->member_count; i++) {
                            MemberInfo* mi = static_walk->members[i];
                            if (mi->is_method && mi->is_static &&
                                strcmp(mi->name, ma->member_name) == 0) {
                                int expected = mi->func->has_varargs
                                               ? mi->func->vararg_param_index
                                               : mi->func->parameterCount;
                                bool va_ok = mi->func->has_varargs
                                             && literal->method_arg_count >= expected;
                                if (!va_ok && expected != literal->method_arg_count)
                                    continue;
                                literal->internal_name = strdup(mi->func->internal_name);
                                literal->dataType = mi->func->Return ?
                                    mi->func->Return->returnType : DATA_VOID;

                                AST_Function* mf = mi->func;
                                if (mf->returns_array) {
                                    bool all_dynamic = true;
                                    for (int d = 0; d < mf->return_dim_count; d++) {
                                        if (mf->return_dim_sizes[d] > 0) {
                                            all_dynamic = false;
                                            break;
                                        }
                                    }
                                    if (all_dynamic) {
                                        literal->returns_array = false;
                                        literal->return_total_bytes = 0;
                                        literal->dataType = DATA_INT;
                                        literal->ptr_depth = 1;
                                    } else {
                                        literal->returns_array = true;
                                        int total = get_type_size(mf->return_element_type);
                                        for (int d = 0; d < mf->return_dim_count; d++) {
                                            if (mf->return_dim_sizes[d] > 0)
                                                total *= mf->return_dim_sizes[d];
                                        }
                                        literal->return_total_bytes = total;
                                    }
                                } else if (mf->returns_struct) {
                                    literal->returns_array = true;
                                    literal->return_total_bytes = mf->return_struct_bytes;
                                } else {
                                    literal->returns_array = false;
                                    literal->return_total_bytes = 0;
                                }
                                literal->ptr_depth = mf->return_ptr_depth;
                                if (mf->return_type == DATA_IDENTIFIER
                                    && mf->return_class_name != NULL) {
                                    if (literal->call_result_class_name)
                                        free(literal->call_result_class_name);
                                    literal->call_result_class_name = strdup(mf->return_class_name);
                                }

                                ASTNode* old_ma = literal->call_function_Parameter;
                                literal->call_function_Parameter = literal->method_arguments;
                                literal->call_function_ParameterCount = literal->method_arg_count;
                                literal->method_arguments = NULL;
                                literal->method_arg_count = 0;
                                literal->method_invoke = false;

                                ASTNode* arg = literal->call_function_Parameter;
                                while (arg) {
                                    analyze_node(arg);
                                    arg = arg->next;
                                }
                                AST_Function* target_func = mi->func;
                                ASTNode** conv_slot = &literal->call_function_Parameter;
                                int conv_idx = 0;
                                while (*conv_slot && conv_idx < target_func->parameterCount) {
                                    sem_wrap_str_if_needed(conv_slot,
                                        target_func->parameterType[conv_idx]);
                                    conv_slot = &(*conv_slot)->next;
                                    conv_idx++;
                                }
                                free(((AST_MemberAccess*)old_ma)->member_name);
                                free(old_ma);
                                goto static_method_done;
                            }
                        }
                        static_walk = (static_walk->parent_count > 0)
                                      ? find_class(static_walk->parent_names[0])
                                      : NULL;
                    }
                }

                analyze_node(ma->object);

                if (ma->object->type == AST_THIS) {
                    obj_sym = lookup_symbol("this");
                    if (!obj_sym || !obj_sym->class_name) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "'this' used outside of class context");
                        literal->dataType = DATA_ERROR;
                        break;
                    }
                    target_class = find_class(obj_sym->class_name);
                } else if (ma->object->type == AST_LITERAL) {
                    AST_Literal* obj_lit = (AST_Literal*)ma->object;
                    if (obj_lit->is_call_function
                        && obj_lit->call_result_class_name != NULL) {
                        target_class =
                            find_class(obj_lit->call_result_class_name);
                        if (target_class == NULL) {
                            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                                  SEMLOC(obj_lit->base.value),
                                  "cannot determine class of object '%s'",
                                  obj_lit->base.value.value);
                            literal->dataType = DATA_ERROR;
                            break;
                        }
                    } else {
                        obj_sym = lookup_symbol(obj_lit->base.value.value);
                        if (!obj_sym || !obj_sym->class_name) {
                            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                                  SEMLOC(obj_lit->base.value),
                                  "cannot determine class of object '%s'",
                                  obj_lit->base.value.value);
                            literal->dataType = DATA_ERROR;
                            break;
                        }
                        target_class = find_class(obj_sym->class_name);
                    }
                } else if (ma->object->type == AST_DEREF) {
                    AST_Deref* d = (AST_Deref*)ma->object;
                    if (d->operand->type == AST_LITERAL) {
                        AST_Literal* lit = (AST_Literal*)d->operand;
                        obj_sym = lookup_symbol(lit->base.value.value);
                        if (obj_sym && obj_sym->class_name)
                            target_class = find_class(obj_sym->class_name);
                    }
                    if (!target_class) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot determine class from dereferenced pointer");
                        literal->dataType = DATA_ERROR;
                        break;
                    }
                } else if (ma->object->type == AST_MEMBER_ACCESS) {
                    const char* cls_name = infer_static_class_name(ma->object);
                    if (cls_name == NULL) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot resolve class of nested member access");
                        literal->dataType = DATA_ERROR;
                        break;
                    }
                    target_class = find_class(cls_name);
                    if (!target_class) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot find class '%s'", cls_name);
                        literal->dataType = DATA_ERROR;
                        break;
                    }
                } else {
                    const char* cls_name = infer_static_class_name(ma->object);
                    if (cls_name != NULL) {
                        target_class = find_class(cls_name);
                    }
                    if (target_class == NULL) {
                        ERROR(ERR_SEM_CLASS_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot determine class of method call target");
                        literal->dataType = DATA_ERROR;
                        break;
                    }
                }

                if (!target_class) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(ma->base.value),
                          "unknown class for method '%s'",
                          ma->member_name);
                    literal->dataType = DATA_ERROR;
                    break;
                }

                ASTNode* arg_node = literal->method_arguments;
                while (arg_node) {
                    analyze_node(arg_node);
                    arg_node = arg_node->next;
                }

                arg_node = literal->method_arguments;
                ASTNode** prev_ma = &literal->method_arguments;
                while (arg_node) {
                    ASTNode* next_ma = arg_node->next;
                    if (arg_node->type == AST_LITERAL
                        && !((AST_Literal*)arg_node)->is_call_function
                        && ((AST_Literal*)arg_node)->base.value.type == TOKEN_IDENTIFIER) {
                        Symbol* arg_sym = lookup_symbol(
                            ((AST_Literal*)arg_node)->base.value.value);
                        if (arg_sym && arg_sym->is_function && arg_sym->func_sig) {
                            AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                            if (!addr) OUT_OF_MEMORY();
                            addr->base.type = AST_ADDRESS_OF;
                            addr->base.next = next_ma;
                            addr->base.value = ((AST_Literal*)arg_node)->base.value;
                            addr->operand = arg_node;
                            addr->resultType = DATA_INT;
                            addr->ptr_depth = 1;
                            addr->target_func_sig =
                                deep_copy_func_sig(arg_sym->func_sig);
                            arg_node->next = NULL;
                            *prev_ma = (ASTNode*)addr;
                            prev_ma = &addr->base.next;
                            arg_node = next_ma;
                            continue;
                        }
                    }
                    prev_ma = &arg_node->next;
                    arg_node = next_ma;
                }

                int has_outer_arg = (target_class->has_outer != 0) ? 1 : 0;
                int total_args = 1 + has_outer_arg + literal->method_arg_count;
                DataType* call_arg_types = malloc(sizeof(DataType) * total_args);
                call_arg_types[0] = DATA_IDENTIFIER;
                if (has_outer_arg) {
                    call_arg_types[1] = DATA_IDENTIFIER;
                }
                arg_node = literal->method_arguments;
                int idx = 1 + has_outer_arg;
                while (arg_node && idx < total_args) {
                    call_arg_types[idx] = get_expression_type(arg_node);
                    idx++;
                    arg_node = arg_node->next;
                }

                AST_Function* matched_method = NULL;
                ClassEntry* walk_cls = target_class;
                while (walk_cls && matched_method == NULL) {
                    for (int i = 0; i < walk_cls->member_count; i++) {
                        MemberInfo* mi = walk_cls->members[i];
                        if (!mi->is_method
                            || strcmp(mi->name, ma->member_name) != 0)
                            continue;
                        if (mi->func->parameterCount != total_args)
                            continue;
                        bool sig_match = true;
                        for (int j = 0; j < total_args; j++) {
                            if (!types_compatible(mi->func->parameterType[j],
                                                  call_arg_types[j])) {
                                sig_match = false;
                                break;
                            }
                        }
                        if (sig_match) {
                            matched_method = mi->func;
                            break;
                        }
                    }
                    walk_cls = (walk_cls->parent_count > 0)
                        ? find_class(walk_cls->parent_names[0]) : NULL;
                }

                if (matched_method) {
                    if (has_outer_arg) {
                        ASTNode* outer_expr = NULL;
                        Symbol* cur_this = lookup_symbol("this");
                        if (cur_this && cur_this->class_name
                            && strcmp(cur_this->class_name,
                                      target_class->outer_class_name) == 0) {
                            AST_This* tn = calloc(1, sizeof(AST_This));
                            tn->base.type = AST_THIS;
                            tn->base.value = literal->base.value;
                            outer_expr = (ASTNode*)tn;
                        } else {
                            Symbol* cur_outer = lookup_symbol("_outer");
                            if (cur_outer && cur_outer->class_name
                                && strcmp(cur_outer->class_name,
                                          target_class->outer_class_name) == 0) {
                                AST_Literal* ol = calloc(1, sizeof(AST_Literal));
                                ol->base.type = AST_LITERAL;
                                ol->base.value.value = strdup("_outer");
                                ol->base.value.type = TOKEN_IDENTIFIER;
                                ol->base.value.line = literal->base.value.line;
                                ol->base.value.column = literal->base.value.column;
                                ol->dataType = DATA_IDENTIFIER;
                                ol->is_call_function = false;
                                ol->internal_name = strdup("_outer");
                                outer_expr = (ASTNode*)ol;
                            } else {
                                AST_Literal* zero = calloc(1, sizeof(AST_Literal));
                                zero->base.type = AST_LITERAL;
                                zero->base.value.type = TOKEN_NUMBER;
                                zero->base.value.value = strdup("0");
                                zero->base.value.line = literal->base.value.line;
                                zero->base.value.column = literal->base.value.column;
                                zero->dataType = DATA_INT;
                                zero->is_call_function = false;
                                outer_expr = (ASTNode*)zero;
                            }
                        }
                        outer_expr->next = literal->method_arguments;
                        literal->method_arguments = outer_expr;
                        literal->method_arg_count += 1;
                    }
                    if (current_analyzing_function
                        && current_analyzing_function->is_const_method
                        && !matched_method->is_const_method
                        && ma->object->type == AST_THIS) {
                        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                              SEMLOC(literal->base.value),
                              "cannot call non-const method '%s' from const method",
                              ma->member_name);
                    }
                    if (obj_sym && (obj_sym->level_const_mask & 1u)
                        && !matched_method->is_const_method) {
                        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                              SEMLOC(literal->base.value),
                              "cannot call non-const method '%s' "
                              "through pointer to const",
                              ma->member_name);
                    }

                    ASTNode* carg = literal->method_arguments;
                    ASTNode* cparam = matched_method->Parameter;
                    if (!matched_method->is_static && cparam) {
                        cparam = cparam->next;
                    }
                    while (carg && cparam) {
                        check_call_arg_const(carg, cparam,
                                             SEMLOC(literal->base.value));
                        carg = carg->next;
                        cparam = cparam->next;
                    }

                    literal->internal_name = strdup(matched_method->internal_name);
                    if (matched_method->is_virtual || matched_method->is_override) {
                        ma->vtable_index = matched_method->vtable_index;
                    }
                    literal->dataType = matched_method->Return ?
                        matched_method->Return->returnType : DATA_VOID;

                    if (matched_method->return_class_name != NULL) {
                        if (literal->call_result_class_name)
                            free(literal->call_result_class_name);
                        literal->call_result_class_name =
                            strdup(matched_method->return_class_name);
                    }

                    if (matched_method->returns_array) {
                        bool all_dynamic = true;
                        for (int d = 0; d < matched_method->return_dim_count; d++) {
                            if (matched_method->return_dim_sizes[d] > 0) {
                                all_dynamic = false;
                                break;
                            }
                        }
                        if (all_dynamic) {
                            literal->returns_array = false;
                            literal->return_total_bytes = 0;
                            literal->dataType = DATA_INT;
                            literal->ptr_depth = 1;
                        } else {
                            literal->returns_array = true;
                            int total = get_type_size(matched_method->return_element_type);
                            for (int d = 0; d < matched_method->return_dim_count; d++) {
                                if (matched_method->return_dim_sizes[d] > 0)
                                    total *= matched_method->return_dim_sizes[d];
                            }
                            literal->return_total_bytes = total;
                        }
                    } else if (matched_method->returns_struct) {
                        literal->returns_array = true;
                        literal->return_total_bytes = matched_method->return_struct_bytes;
                    } else {
                        literal->returns_array = false;
                        literal->return_total_bytes = 0;
                    }
                    literal->ptr_depth = matched_method->return_ptr_depth;
                    if (matched_method->return_pointee_const) {
                        literal->is_return_pointee_const = true;
                    }

                    ASTNode** m_slot = &literal->method_arguments;
                    int m_idx = 1;
                    while (*m_slot && m_idx < matched_method->parameterCount) {
                        DataType m_param_type = matched_method->parameterType[m_idx];
                        ASTNode* m_arg = *m_slot;
                        DataType m_arg_type = get_expression_type(m_arg);
                        if (m_param_type == DATA_STR && m_arg_type == DATA_STRING) {
                            sem_wrap_str_if_needed(m_slot, m_param_type);
                        } else if (
                            m_arg_type != m_param_type &&
                            (is_int_type(m_arg_type) || is_uint_type(m_arg_type) ||
                             is_float_type(m_arg_type) || is_ufloat_type(m_arg_type)) &&
                            (is_int_type(m_param_type) || is_uint_type(m_param_type) ||
                             is_float_type(m_param_type) || is_ufloat_type(m_param_type))
                        ) {
                            AST_StrFromString* unused_guard = NULL;
                            (void)unused_guard;
                            ASTNode* next = m_arg->next;
                            AST_Cast* cast = calloc(1, sizeof(AST_Cast));
                            if (!cast) OUT_OF_MEMORY();
                            cast->base.type = AST_CAST;
                            cast->base.next = next;
                            cast->base.value = m_arg->value;
                            cast->base.value.value = NULL;
                            cast->operand = m_arg;
                            cast->target_type = m_param_type;
                            cast->target_class_name = NULL;
                            cast->target_class_id = -1;
                            cast->target_ptr_depth = 0;
                            cast->target_func_sig = NULL;
                            cast->target_dim_count = 0;
                            cast->target_dim_sizes = NULL;
                            cast->source_type = m_arg_type;
                            cast->source_ptr_depth = 0;
                            cast->source_func_sig = NULL;
                            cast->is_reinterpret = false;
                            cast->is_checked = false;
                            m_arg->next = NULL;
                            *m_slot = (ASTNode*)cast;
                        }
                        m_slot = &(*m_slot)->next;
                        m_idx++;
                    }
                } else {
                    if (!(current_class && current_class->is_generic)) {
                        ERROR(ERR_SEM_METHOD_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "no matching method '%s'",
                              ma->member_name);
                        literal->dataType = DATA_ERROR;
                    } else {
                        literal->dataType = DATA_INT;
                    }
                }

                int total_args_ = 1 + literal->method_arg_count;
                int* byte_sizes = malloc(sizeof(int) * total_args_);
                for (int i = 0; i < total_args_; i++) byte_sizes[i] = 4;
                ASTNode* a = literal->method_arguments;
                int idx_ = 1;
                while (a && idx_ < total_args_) {
                    Symbol* arg_sym = NULL;
                    int arg_total_bytes = 4;
                    if (a->type == AST_LITERAL) {
                        AST_Literal* lit = (AST_Literal*)a;
                        if (!lit->is_call_function)
                            arg_sym = lookup_symbol(lit->base.value.value);
                    } else if (a->type == AST_VAR || a->type == AST_VAR_ASS) {
                        AST_Var* v = (AST_Var*)a;
                        arg_sym = lookup_symbol(v->name.value);
                        if (v->total_bytes > 4) arg_total_bytes = v->total_bytes;
                    } else if (a->type == AST_ARRAY_DECL) {
                        AST_ArrayDecl* arr = (AST_ArrayDecl*)a;
                        if (arr->total_bytes > 4 && arr->total_bytes != -1)
                            arg_total_bytes = arr->total_bytes;
                    }
                    if (arg_sym && arg_sym->is_value_type && arg_sym->total_bytes > 4)
                        arg_total_bytes = arg_sym->total_bytes;
                    if (a->type == AST_LITERAL) {
                        AST_Literal* lit = (AST_Literal*)a;
                        if (!lit->is_call_function && lit->dataType == DATA_STR && arg_total_bytes < 8)
                            arg_total_bytes = 8;
                    } else if (a->type == AST_VAR || a->type == AST_VAR_ASS) {
                        AST_Var* v = (AST_Var*)a;
                        if (v->dataType == DATA_STR && arg_total_bytes < 8)
                            arg_total_bytes = 8;
                    }
                    byte_sizes[idx_] = arg_total_bytes;
                    idx_++;
                    a = a->next;
                }
                bool has_large = false;
                for (int i = 0; i < total_args_; i++)
                    if (byte_sizes[i] > 4) { has_large = true; break; }
                literal->param_byte_sizes = byte_sizes;
                if (matched_method) {
                    for (int i = 0; i < total_args_ && i < matched_method->parameterCount; i++) {
                        int sz = S_get_type_size(matched_method->parameterType[i]);
                        if (sz < 4) sz = 4;
                        byte_sizes[i] = sz;
                    }
                }
                literal->param_byte_sizes = byte_sizes;
                if (matched_method && matched_method->has_varargs && byte_sizes != NULL) {
                    for (int vi = 0; vi < matched_method->vararg_param_count; vi++) {
                        int pi = matched_method->vararg_param_indices[vi];
                        if (pi >= 0 && pi < total_args_) {
                            bool is_func_ptr = false;
                            ASTNode* pnode = matched_method->Parameter;
                            for (int s = 0; s < pi && pnode; s++) pnode = pnode->next;
                            if (pnode && (pnode->type == AST_VAR || pnode->type == AST_UNINIT_VAR)) {
                                AST_Var* pvar = (AST_Var*)pnode;
                                if (pvar->func_ptr_sig != NULL) is_func_ptr = true;
                            }
                            byte_sizes[pi] = is_func_ptr ? 4 : 8;
                        }
                    }
                }

                free(call_arg_types);

                break;
            } else {
                ASTNode* arg = literal->call_function_Parameter;
                ASTNode** prev = &literal->call_function_Parameter;
                while (arg) {
                    ASTNode* next = arg->next;
                    if (arg->type == AST_LITERAL
                        && !((AST_Literal*)arg)->is_call_function
                        && ((AST_Literal*)arg)->base.value.type == TOKEN_IDENTIFIER) {
                        Symbol* arg_sym = lookup_symbol(
                            ((AST_Literal*)arg)->base.value.value);
                        if (arg_sym && arg_sym->is_function && arg_sym->func_sig) {
                            AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                            if (!addr) OUT_OF_MEMORY();
                            addr->base.type   = AST_ADDRESS_OF;
                            addr->base.next   = next;
                            addr->base.value  = ((AST_Literal*)arg)->base.value;
                            addr->operand     = arg;
                            addr->resultType  = DATA_INT;
                            addr->ptr_depth   = 1;
                            addr->target_func_sig = deep_copy_func_sig(arg_sym->func_sig);
                            arg->next = NULL;
                            *prev = (ASTNode*)addr;
                            prev = &addr->base.next;
                            arg = next;
                            continue;
                        }
                        }
                    prev = &arg->next;
                    arg = next;
                }

                if (strcmp(literal->base.value.value, "print_err") == 0 ||
                    strcmp(literal->base.value.value, "_print_err") == 0) {
                    ASTNode** pp = &literal->call_function_Parameter;
                    ASTNode* pa = literal->call_function_Parameter;
                    while (pa) {
                        ASTNode* next = pa->next;
                        if (pa->type == AST_LITERAL) {
                            AST_Literal* pl = (AST_Literal*)pa;
                            if (pl->base.value.type == TOKEN_IDENTIFIER
                                && !pl->is_call_function) {
                                Symbol* ps = lookup_symbol(pl->base.value.value);
                                if (ps && ps->dim_count > 0) {
                                    AST_Literal* call = calloc(1, sizeof(AST_Literal));
                                    if (!call) OUT_OF_MEMORY();
                                    call->base.type = AST_LITERAL;
                                    call->base.next = next;
                                    call->base.value.type = TOKEN_IDENTIFIER;
                                    call->base.value.value = strdup("_arrayToString");
                                    call->dataType = DATA_STRING;
                                    call->internal_name = strdup("_arrayToString");
                                    call->is_call_function = true;

                                    AST_Literal* arr = calloc(1, sizeof(AST_Literal));
                                    if (!arr) OUT_OF_MEMORY();
                                    arr->base.type = AST_LITERAL;
                                    arr->base.next = NULL;
                                    arr->base.value = pl->base.value;
                                    arr->dataType = ps->type;
                                    arr->internal_name = strdup(ps->internal_name);
                                    arr->is_call_function = false;

                                    call->call_function_Parameter = (ASTNode*)arr;
                                    call->call_function_ParameterCount = 1;

                                    *pp = (ASTNode*)call;
                                    pp = &call->base.next;
                                    pa = next;
                                    continue;
                                }
                            }
                        } else if (pa->type == AST_ARRAY_ACCESS) {
                            AST_ArrayAccess* acc = (AST_ArrayAccess*)pa;
                            if (acc->dim_count > 0) {
                                AST_Literal* call = calloc(1, sizeof(AST_Literal));
                                if (!call) OUT_OF_MEMORY();
                                call->base.type = AST_LITERAL;
                                call->base.next = next;
                                call->base.value.type = TOKEN_IDENTIFIER;
                                call->base.value.value = strdup("_arrayToString");
                                call->dataType = DATA_STRING;
                                call->internal_name = strdup("_arrayToString");
                                call->is_call_function = true;

                                pa->next = NULL;
                                call->call_function_Parameter = pa;
                                call->call_function_ParameterCount = 1;

                                *pp = (ASTNode*)call;
                                pp = &call->base.next;
                                pa = next;
                                continue;
                            }
                        }
                        pp = &pa->next;
                        pa = next;
                    }
                }

                arg = literal->call_function_Parameter;
                while (arg) {
                    analyze_node(arg);
                    arg = arg->next;
                }
                int arg_count = literal->call_function_ParameterCount;
                DataType* arg_types = NULL;
                if (arg_count > 0) {
                    arg_types = malloc(sizeof(DataType) * arg_count);
                    int* byte_sizes = malloc(sizeof(int) * arg_count);
                    ASTNode* a = literal->call_function_Parameter;
                    int idx = 0;
                    while (a && idx < arg_count) {
                        analyze_node(a);
                        arg_types[idx] = get_expression_type(a);

                        Symbol* arg_sym = NULL;
                        int arg_total_bytes = 4;
                        if (a->type == AST_LITERAL) {
                            AST_Literal* lit = (AST_Literal*)a;
                            if (!lit->is_call_function) {
                                arg_sym = lookup_symbol(lit->base.value.value);
                            }
                        } else if (a->type == AST_VAR || a->type == AST_VAR_ASS) {
                            AST_Var* v = (AST_Var*)a;
                            arg_sym = lookup_symbol(v->name.value);
                            if (v->total_bytes > 4) arg_total_bytes = v->total_bytes;
                        } else if (a->type == AST_ARRAY_DECL) {
                            AST_ArrayDecl* arr = (AST_ArrayDecl*)a;
                            if (arr->total_bytes > 4 && arr->total_bytes != -1)
                                arg_total_bytes = arr->total_bytes;
                        }
                        if (arg_sym && arg_sym->is_value_type && arg_sym->total_bytes > 4) {
                            arg_total_bytes = arg_sym->total_bytes;
                        } else if (!arg_sym) {
                            DataType pt = arg_types[idx];
                            if (pt == DATA_STR) {
                                arg_total_bytes = 8;
                            } else if (a->type == AST_STR_FROM_STRING) {
                                arg_total_bytes = 8;
                            }
                        }
                        if (arg_types[idx] == DATA_STR && arg_total_bytes < 8) {
                            arg_total_bytes = 8;
                        }
                        if (arg_types[idx] == DATA_INT64 || arg_types[idx] == DATA_UINT64) {
                            byte_sizes[idx] = 8;
                        } else {
                            byte_sizes[idx] = arg_total_bytes > 4 ? arg_total_bytes : 4;
                        }
                        idx++;
                        a = a->next;
                    }
                    bool has_large = false;
                    for (int i = 0; i < arg_count; i++) {
                        if (byte_sizes[i] > 4) { has_large = true; break; }
                    }
                    literal->param_byte_sizes = byte_sizes;
                }

                if (strcmp(literal->base.value.value, "format") == 0 ||
                    strcmp(literal->base.value.value, "_format") == 0) {
                    literal->internal_name = strdup("_format_string");
                    literal->dataType = DATA_STRING;
                    free(arg_types);
                    break;
                }

                if (strcmp(literal->base.value.value, "print_err") == 0 ||
                    strcmp(literal->base.value.value, "_print_err") == 0) {
                    literal->internal_name = strdup("_print_err");
                    literal->dataType = DATA_VOID;
                    free(arg_types);
                    break;
                }

                if (strcmp(literal->base.value.value, "_arrayToString") == 0 ||
                    strcmp(literal->base.value.value, "arrayToString") == 0) {
                    if (literal->internal_name == NULL)
                        literal->internal_name = strdup("_arrayToString");
                    literal->dataType = DATA_STRING;
                    free(arg_types);
                    break;
                }

                AST_Function* func_def = NULL;
                if (literal->internal_name != NULL) {
                    for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                        if (fe->func && fe->func->internal_name
                            && strcmp(fe->func->internal_name,
                                      literal->internal_name) == 0) {
                            func_def = fe->func;
                            break;
                        }
                    }
                }
                if (func_def == NULL) {
                    func_def = find_function_by_signature(
                        literal->base.value.value, arg_count, arg_types);
                }
                if (func_def == NULL && arg_count > 0) {
                    ASTNode* a = literal->call_function_Parameter;
                    ASTNode** prev = &literal->call_function_Parameter;
                    int pi = 0;
                    while (a && pi < arg_count) {
                        ASTNode* next_a = a->next;
                        if (a->type == AST_LITERAL
                            && !((AST_Literal*)a)->is_call_function
                            && ((AST_Literal*)a)->base.value.type == TOKEN_IDENTIFIER) {
                            AST_Literal* a_lit = (AST_Literal*)a;
                            Symbol* arg_sym = lookup_symbol(a_lit->base.value.value);
                            if (arg_sym && arg_sym->is_function && arg_sym->func_sig) {
                                if (a_lit->internal_name == NULL) {
                                    a_lit->internal_name = strdup(
                                        arg_sym->internal_name
                                        ? arg_sym->internal_name
                                        : arg_sym->key);
                                }
                                AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                                if (!addr) OUT_OF_MEMORY();
                                addr->base.type = AST_ADDRESS_OF;
                                addr->base.next = next_a;
                                addr->base.value = a_lit->base.value;
                                addr->operand = a;
                                addr->resultType = DATA_INT;
                                addr->ptr_depth = 1;
                                addr->target_func_sig = deep_copy_func_sig(arg_sym->func_sig);
                                a->next = NULL;
                                *prev = (ASTNode*)addr;
                                prev = &addr->base.next;
                                arg_types[pi] = DATA_INT;
                                a = next_a;
                                pi++;
                                continue;
                            }
                        }
                        prev = &a->next;
                        a = next_a;
                        pi++;
                    }
                    func_def = find_function_by_signature(
                        literal->base.value.value, arg_count, arg_types);
                }
                if (!func_def && arg_count > 0) {
                    for (FuncEntry* e = func_table; e; e = e->next) {
                        if (strcmp(e->name, literal->base.value.value) != 0) continue;
                        if (!e->func->has_varargs) continue;
                        if (arg_count < e->func->vararg_param_index) continue;
                        func_def = e->func;
                        break;
                    }
                }
                if (func_def == NULL) {
                    for (FuncEntry* e = func_table; e; e = e->next) {
                        if (strcmp(e->name, literal->base.value.value) != 0) continue;
                        if (!e->func->is_generic) continue;

                        int min_params = e->func->has_varargs
                                         ? e->func->vararg_param_index
                                         : e->func->parameterCount;
                        if (arg_count < min_params) continue;

                        DataType* targs;
                        if (literal->is_generic_instantiation && literal->generic_type_arg_count > 0) {
                            targs = malloc(sizeof(DataType) * literal->generic_type_arg_count);
                            memcpy(targs, literal->generic_type_args,
                                   sizeof(DataType) * literal->generic_type_arg_count);
                        } else {
                            targs = infer_type_args(e->func,
                                                           literal->call_function_Parameter,
                                                           arg_count,
                                                           &literal->generic_type_arg_count);
                        }
                        if (targs != NULL && literal->generic_type_arg_count > 0) {
                            bool constraints_ok = true;
                            for (int ti = 0; ti < literal->generic_type_arg_count
                                             && ti < e->func->type_param_count; ti++) {
                                TypeParam* tp = &e->func->type_params[ti];
                                for (int cj = 0; cj < tp->constraint_count; cj++) {
                                    TypeConstraint* tc = &tp->constraints[cj];
                                    if (tc->kind == CONSTRAINT_SUBTYPE) {
                                        ClassEntry* constraint_cls = find_class(tc->class_name);
                                        if (constraint_cls && targs[ti] == DATA_IDENTIFIER) {
                                            continue;
                                        }
                                        if (targs[ti] != DATA_IDENTIFIER) {
                                            ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(literal->base.value),
                                                  "type '%s' does not satisfy constraint ': %s'",
                                                  data_type_to_string(targs[ti]), tc->class_name);
                                            constraints_ok = false;
                                        }
                                    }
                                }
                            }

                            if (constraints_ok) {
                                literal->generic_type_args = targs;
                                literal->is_generic_instantiation = true;
                                if (literal->generic_type_arg_count > 0) {
                                    literal->generic_type_arg_names = calloc(
                                        (size_t)literal->generic_type_arg_count, sizeof(char*));
                                    ASTNode* ca = literal->call_function_Parameter;
                                    int ci = 0;
                                    while (ca && ci < literal->generic_type_arg_count) {
                                        if (targs[ci] == DATA_IDENTIFIER) {
                                            Symbol* cs = NULL;
                                            if (ca->type == AST_LITERAL) {
                                                cs = lookup_symbol(((AST_Literal*)ca)->base.value.value);
                                                if (!cs) {
                                                    AST_Literal* cl = (AST_Literal*)ca;
                                                    if (cl->internal_name) cs = lookup_symbol(cl->internal_name);
                                                }
                                            } else if (ca->type == AST_VAR || ca->type == AST_VAR_ASS
                                                       || ca->type == AST_UNINIT_VAR) {
                                                cs = lookup_symbol(((AST_Var*)ca)->name.value);
                                            }
                                            if (cs && cs->class_name)
                                                literal->generic_type_arg_names[ci] = strdup(cs->class_name);
                                        }
                                        ca = ca->next;
                                        ci++;
                                    }
                                }
                                AST_Function* inst = find_instantiated(e->func->name.value, targs, literal->generic_type_arg_count,
                                                                        literal->generic_type_arg_names);
                                if (!inst) {
                                    inst = instantiate_generic_function(
                                        e->func, targs, literal->generic_type_arg_count,
                                        literal->generic_type_arg_names);
                                    if (inst) {
                                        record_instantiation(e->func->name.value,
                                                             inst->internal_name,
                                                             targs, literal->generic_type_arg_count,
                                                             literal->generic_type_arg_names,
                                                             inst);
                                    }
                                }
                                if (inst) {
                                    ASTNode* call_arg = literal->call_function_Parameter;
                                    ASTNode* param_node = inst->Parameter;
                                    int pi = 0;
                                    while (call_arg && param_node && pi < inst->parameterCount) {
                                        if (param_node->type == AST_VAR
                                            || param_node->type == AST_VAR_ASS
                                            || param_node->type == AST_UNINIT_VAR) {
                                            AST_Var* pv = (AST_Var*)param_node;
                                            if (pv->type_param_index >= 0
                                                && pv->type_param_index < literal->generic_type_arg_count
                                                && targs[pv->type_param_index] == DATA_IDENTIFIER) {
                                                const char* cls_name = NULL;
                                                Symbol* arg_sym = NULL;
                                                if (call_arg->type == AST_LITERAL) {
                                                    AST_Literal* lit = (AST_Literal*)call_arg;
                                                    arg_sym = lookup_symbol(lit->base.value.value);
                                                } else if (call_arg->type == AST_VAR
                                                           || call_arg->type == AST_VAR_ASS
                                                           || call_arg->type == AST_UNINIT_VAR) {
                                                    AST_Var* v = (AST_Var*)call_arg;
                                                    arg_sym = lookup_symbol(v->name.value);
                                                }
                                                if (arg_sym && arg_sym->class_name) {
                                                    cls_name = arg_sym->class_name;
                                                }
                                                if (cls_name) {
                                                    pv->dataType = DATA_IDENTIFIER;
                                                    if (pv->internal_name) free(pv->internal_name);
                                                    pv->internal_name = strdup(cls_name);
                                                }
                                            }
                                        }
                                        call_arg = call_arg->next;
                                        param_node = param_node->next;
                                        pi++;
                                    }
                                }
                                func_def = inst;
                                break;
                            } else {
                                free(targs);
                            }
                        }
                    }
                }
                if (func_def == NULL && arg_count > 0) {
                    for (FuncEntry* e = func_table; e; e = e->next) {
                        if (strcmp(e->name, literal->base.value.value) != 0) continue;
                        if (!e->func->is_generic) continue;
                        int min_params = e->func->has_varargs
                                         ? e->func->vararg_param_index
                                         : e->func->parameterCount;
                        if (arg_count < min_params) continue;

                        DataType* targs;
                        if (literal->is_generic_instantiation && literal->generic_type_arg_count > 0) {
                            targs = malloc(sizeof(DataType) * literal->generic_type_arg_count);
                            memcpy(targs, literal->generic_type_args,
                                   sizeof(DataType) * literal->generic_type_arg_count);
                        } else {
                            targs = infer_type_args(e->func,
                                                           literal->call_function_Parameter,
                                                           arg_count,
                                                           &literal->generic_type_arg_count);
                        }
                        if (targs != NULL && literal->generic_type_arg_count > 0) {
                            literal->generic_type_args = targs;
                            literal->is_generic_instantiation = true;

                            AST_Function* inst = find_instantiated(e->func->name.value, targs, literal->generic_type_arg_count,
                                                                    literal->generic_type_arg_names);
                            if (!inst) {
                                inst = instantiate_generic_function(
                                    e->func, targs, literal->generic_type_arg_count,
                                    literal->generic_type_arg_names);
                                if (inst) {
                                    record_instantiation(e->func->name.value,
                                                         inst->internal_name,
                                                         targs, literal->generic_type_arg_count,
                                                         literal->generic_type_arg_names,
                                                         inst);
                                }
                            }
                            if (inst) {
                                ASTNode* call_arg = literal->call_function_Parameter;
                                ASTNode* param_node = inst->Parameter;
                                int pi = 0;
                                while (call_arg && param_node
                                       && pi < inst->parameterCount) {
                                    if (param_node->type == AST_VAR
                                        || param_node->type == AST_VAR_ASS
                                        || param_node->type == AST_UNINIT_VAR) {
                                        AST_Var* pv = (AST_Var*)param_node;
                                        if (pv->type_param_index >= 0
                                            && pv->type_param_index
                                               < literal->generic_type_arg_count
                                            && targs[pv->type_param_index]
                                               == DATA_IDENTIFIER) {
                                            const char* cls_name = NULL;
                                            Symbol* arg_sym = NULL;
                                            if (call_arg->type == AST_LITERAL) {
                                                AST_Literal* lit =
                                                    (AST_Literal*)call_arg;
                                                arg_sym = lookup_symbol(
                                                    lit->base.value.value);
                                            } else if (call_arg->type == AST_VAR
                                                       || call_arg->type == AST_VAR_ASS
                                                       || call_arg->type
                                                          == AST_UNINIT_VAR) {
                                                AST_Var* v = (AST_Var*)call_arg;
                                                arg_sym = lookup_symbol(
                                                    v->name.value);
                                            }
                                            if (arg_sym && arg_sym->class_name) {
                                                cls_name = arg_sym->class_name;
                                            }
                                            if (cls_name) {
                                                pv->dataType = DATA_IDENTIFIER;
                                                if (pv->internal_name)
                                                    free(pv->internal_name);
                                                pv->internal_name =
                                                    strdup(cls_name);
                                            }
                                        }
                                    }
                                    call_arg = call_arg->next;
                                    param_node = param_node->next;
                                    pi++;
                                }
                                func_def = inst;
                            }
                            break;
                        }
                    }
                }
                if (func_def) {
                    {
                        ASTNode** pw_prev = &literal->call_function_Parameter;
                        ASTNode* pw_arg = literal->call_function_Parameter;
                        int pw_idx = 0;
                        while (pw_arg) {
                            bool param_is_function_class = false;
                            if (pw_idx < func_def->parameterCount) {
                                ASTNode* pnode = func_def->Parameter;
                                for (int s = 0; s < pw_idx && pnode; s++) pnode = pnode->next;
                                if (pnode && (pnode->type == AST_VAR || pnode->type == AST_UNINIT_VAR)) {
                                    AST_Var* pv = (AST_Var*)pnode;
                                    if (pv->static_storage_name
                                        && strcmp(pv->static_storage_name, "Function") == 0) {
                                        param_is_function_class = true;
                                    }
                                }
                            }
                            ASTNode* wrapped = param_is_function_class
                                ? sem_wrap_function_value(pw_arg)
                                : NULL;
                            if (wrapped != NULL) {
                                ASTNode* next_a = pw_arg->next;
                                wrapped->next = next_a;
                                *pw_prev = wrapped;
                                pw_prev = &wrapped->next;
                                pw_arg = next_a;
                                analyze_node(wrapped);
                                pw_idx++;
                                continue;
                            }
                            pw_prev = &pw_arg->next;
                            pw_arg = pw_arg->next;
                            pw_idx++;
                        }
                    }

                    ASTNode* carg = literal->call_function_Parameter;
                    ASTNode* cparam = func_def->Parameter;
                    while (carg && cparam) {
                        check_call_arg_const(carg, cparam,
                                             SEMLOC(literal->base.value));
                        carg = carg->next;
                        cparam = cparam->next;
                    }

                    for (int i = 0; i < arg_count && i < func_def->parameterCount; i++) {
                        ASTNode* pn = func_def->Parameter;
                        for (int k = 0; k < i && pn; k++) pn = pn->next;
                        if (pn && pn->type == AST_ARRAY_DECL) {
                            AST_ArrayDecl* pa = (AST_ArrayDecl*)pn;
                            bool all_dynamic = (pa->dim_count > 0);
                            for (int d = 0; d < pa->dim_count; d++) {
                                if (pa->dim_sizes[d] > 0) { all_dynamic = false; break; }
                            }
                            literal->param_byte_sizes[i] = all_dynamic ? 8 : 4;
                            continue;
                        }
                        int sz = S_get_type_size(func_def->parameterType[i]);
                        if (sz < 4) sz = 4;
                        if (sz > 4 && literal->param_byte_sizes[i] > sz)
                            sz = literal->param_byte_sizes[i];
                        literal->param_byte_sizes[i] = sz;
                    }
                    if (func_def->has_varargs && literal->param_byte_sizes != NULL) {
                        for (int vi = 0; vi < func_def->vararg_param_count; vi++) {
                            int pi = func_def->vararg_param_indices[vi];
                            if (pi >= 0 && pi < arg_count) {
                                literal->param_byte_sizes[pi] = 4;
                            }
                        }
                    }
                    if (func_def->has_varargs) {
                        ASTNode* a = literal->call_function_Parameter;
                        int p_idx = 0;
                        while (a && p_idx < func_def->parameterCount) {
                            bool is_vararg_param = false;
                            for (int vi = 0; vi < func_def->vararg_param_count; vi++) {
                                if (func_def->vararg_param_indices[vi] == p_idx) {
                                    is_vararg_param = true;
                                    break;
                                }
                            }
                            if (is_vararg_param) {
                            if (is_vararg_param) {
                                if (a->type == AST_VARARG_GROUP) {
                                    a = a->next;
                                    p_idx++;
                                    continue;
                                }
                                ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                                      SEMLOC(a->value),
                                      "variadic parameter of \"%s\" requires bracketed groups like [1,2,3]",
                                      func_def->name.value ? func_def->name.value : "?");
                                a = a->next;
                                p_idx++;
                                continue;
                            }
                            }
                            if (!is_vararg_param && a->type == AST_VARARG_GROUP) {
                                a = a->next;
                                p_idx++;
                                continue;
                            }
                            a = a->next;
                            p_idx++;
                        }
                    }
                    if (literal->internal_name) free(literal->internal_name);
                    bool func_was_generic = false;
                    if (func_def && func_def->is_generic) {
                        func_was_generic = true;
                        DataType* targs;
                        if (literal->is_generic_instantiation && literal->generic_type_arg_count > 0) {
                            targs = malloc(sizeof(DataType) * literal->generic_type_arg_count);
                            memcpy(targs, literal->generic_type_args,
                                   sizeof(DataType) * literal->generic_type_arg_count);
                        } else {
                            targs = infer_type_args(func_def,
                                                     literal->call_function_Parameter,
                                                     arg_count,
                                                     &literal->generic_type_arg_count);
                        }
                        if (targs && literal->generic_type_arg_count > 0) {
                            literal->generic_type_args = targs;
                            literal->is_generic_instantiation = true;

                            if (literal->generic_type_arg_count > 0
                                && literal->generic_type_arg_names == NULL) {
                                literal->generic_type_arg_names = calloc(
                                    (size_t)literal->generic_type_arg_count, sizeof(char*));
                                ASTNode* ca = literal->call_function_Parameter;
                                int ci = 0;
                                while (ca && ci < literal->generic_type_arg_count) {
                                    if (targs[ci] == DATA_IDENTIFIER) {
                                        Symbol* cs = NULL;
                                        if (ca->type == AST_LITERAL) {
                                            cs = lookup_symbol(((AST_Literal*)ca)->base.value.value);
                                            if (!cs) {
                                                AST_Literal* cl = (AST_Literal*)ca;
                                                if (cl->internal_name) cs = lookup_symbol(cl->internal_name);
                                            }
                                        } else if (ca->type == AST_VAR || ca->type == AST_VAR_ASS
                                                   || ca->type == AST_UNINIT_VAR) {
                                            cs = lookup_symbol(((AST_Var*)ca)->name.value);
                                                   }
                                        if (cs && cs->class_name)
                                            literal->generic_type_arg_names[ci] = strdup(cs->class_name);
                                    }
                                    ca = ca->next;
                                    ci++;
                                }
                            }

                            AST_Function* inst = find_instantiated(func_def->name.value, targs, literal->generic_type_arg_count,
                                                                    literal->generic_type_arg_names);
                            if (!inst) {
                                inst = instantiate_generic_function(
                                    func_def, targs, literal->generic_type_arg_count,
                                    literal->generic_type_arg_names);
                                if (inst) {
                                    record_instantiation(func_def->name.value,
                                                         inst->internal_name,
                                                         targs, literal->generic_type_arg_count,
                                                         literal->generic_type_arg_names,
                                                         inst);
                                }
                            }
                            if (inst) {
                                func_def = inst;
                            }
                        }
                    }
                    if (func_def->internal_name != NULL) {
                        literal->internal_name = strdup(func_def->internal_name);
                    } else {
                        literal->internal_name = strdup(func_def->name.value);
                    }

                    if (func_def->is_closure && func_def->upvalue_count > 0
                        && !literal->is_indirect_call
                        && literal->indirect_callee == NULL) {
                        literal->is_indirect_call = true;
                        literal->is_closure_call = true;

                        AST_Literal* self_lit = calloc(1, sizeof(AST_Literal));
                        if (!self_lit) OUT_OF_MEMORY();
                        self_lit->base.type = AST_LITERAL;
                        self_lit->base.next = NULL;
                        self_lit->base.value = literal->base.value;
                        self_lit->base.value.value = literal->base.value.value
                                                      ? strdup(literal->base.value.value)
                                                      : NULL;
                        self_lit->dataType = DATA_INT;
                        self_lit->internal_name = literal->internal_name
                                                  ? strdup(literal->internal_name)
                                                  : NULL;
                        self_lit->is_call_function = false;
                        self_lit->closure_func = func_def;
                        literal->indirect_callee = (ASTNode*)self_lit;
                    }

                    if (literal->internal_name != NULL &&
                        (strcmp(literal->internal_name, "_GetCommandLineArgc") == 0 ||
                         strcmp(literal->internal_name, "_GetCommandLineArgv") == 0 ||
                         strcmp(literal->internal_name, "_sys_argv_at") == 0)) {
                        main_has_args = true;
                    }

                    if (func_def && func_def->Parameter) {
                        ASTNode* arg = literal->call_function_Parameter;
                        ASTNode* param = func_def->Parameter;
                        while (arg && param) {
                            if (param->type == AST_VAR || param->type == AST_UNINIT_VAR
                                || param->type == AST_VAR_ASS) {
                                AST_Var* pv = (AST_Var*)param;
                                /* 传参变型检查：形参声明为泛型实例时校验实参 */
                                if (pv->decl_class_name != NULL
                                    && arg->type != AST_VARARG_GROUP) {
                                    char decl_inst_buf[192];
                                    const char* pcls = pv->decl_class_name;
                                    if (strchr(pcls, '$') == NULL
                                        && pv->generic_type_arg_count > 0
                                        && pv->generic_type_arg_names != NULL) {
                                        int nb = snprintf(decl_inst_buf,
                                                          sizeof(decl_inst_buf),
                                                          "%s", pcls);
                                        for (int qi = 0;
                                             qi < pv->generic_type_arg_count
                                                 && nb > 0
                                                 && nb < (int)sizeof(decl_inst_buf);
                                             qi++) {
                                            const char* qn =
                                                pv->generic_type_arg_names[qi];
                                            nb += snprintf(
                                                decl_inst_buf + nb,
                                                sizeof(decl_inst_buf) - (size_t)nb,
                                                "%s%s",
                                                (qi == 0) ? "$" : "_",
                                                qn ? qn : "int");
                                        }
                                        if (nb > 0) pcls = decl_inst_buf;
                                    }
                                    if (strchr(pcls, '$') != NULL) {
                                        const char* av_cls =
                                            infer_static_class_name(arg);
                                        if (av_cls != NULL
                                            && !generic_variance_assign_ok(
                                                   pcls, av_cls)) {
                                            ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                                                  SEMLOC(arg->value),
                                                  "cannot pass '%s' as '%s': generic arguments are not compatible under variance",
                                                  av_cls, pcls);
                                        }
                                    }
                                }
                                if (arg->type == AST_ADDRESS_OF) {
                                    AST_AddressOf* ao = (AST_AddressOf*)arg;
                                    if (ao->target_func_sig
                                        && ao->target_func_sig->returns_closure
                                        && pv->func_ptr_sig) {
                                        pv->func_ptr_sig->returns_closure = true;
                                    }
                                } else {
                                    bool arg_closed = false;
                                    if (arg->type == AST_LITERAL) {
                                        AST_Literal* al = (AST_Literal*)arg;
                                        if (al->is_closure_call || al->is_closure_ptr)
                                            arg_closed = true;
                                        else if (!al->is_call_function
                                                 && al->base.value.type == TOKEN_IDENTIFIER) {
                                            Symbol* s = lookup_symbol(al->base.value.value);
                                            if (s && s->is_closure_ptr) arg_closed = true;
                                        }
                                    }
                                    if (arg_closed) pv->is_closure_ptr = true;
                                }
                            }
                            arg = arg->next;
                            param = param->next;
                        }
                    }
                    if (func_def && func_def->Code && !literal->is_closure_call) {
                        ASTNode* scan = func_def->Code;
                        while (scan) {
                            if (scan->type == AST_FUNCTION
                                && ((AST_Function*)scan)->is_closure) {
                                literal->is_closure_call = true;
                                literal->closure_func = (AST_Function*)scan;
                                break;
                            }
                            scan = scan->next;
                        }
                    }
                    if (func_def->multi_return) {
                        literal->dataType = DATA_VOID;
                        literal->multi_return_type =
                            deep_copy_multi_return(func_def->multi_return);
                    } else {
                        literal->dataType = func_def->Return ?
                            func_def->Return->returnType : DATA_VOID;
                    }
                    if (func_def->return_class_name != NULL) {
                        if (literal->call_result_class_name)
                            free(literal->call_result_class_name);
                        literal->call_result_class_name =
                            strdup(func_def->return_class_name);
                    }
                    if (func_def && func_def->is_generic
                        && literal->dataType == DATA_VOID
                        && func_def->return_type == DATA_IDENTIFIER
                        && literal->call_function_Parameter != NULL) {
                        ASTNode* arg = literal->call_function_Parameter;
                        while (arg) {
                            DataType t = get_expression_type(arg);
                            if (t != DATA_ERROR && t != DATA_IDENTIFIER) {
                                literal->dataType = t;
                                break;
                            }
                            arg = arg->next;
                        }
                        if (literal->dataType == DATA_VOID) {
                            literal->dataType = DATA_INT;
                        }
                    }
                    literal->ptr_depth = func_def->return_ptr_depth;
                    if (func_def->return_pointee_const) {
                        literal->is_return_pointee_const = true;
                    }
                    if (literal->is_indirect_call
                        && func_def->ret_func_sig != NULL
                        && func_def->ret_func_sig->multi_return != NULL) {
                        literal->multi_return_type =
                            deep_copy_multi_return(func_def->ret_func_sig->multi_return);
                    }
                    if (func_def->multi_return) {
                        literal->multi_return_type =
                            deep_copy_multi_return(func_def->multi_return);
                    }
                    if (func_def->returns_array) {
                        bool all_dynamic = true;
                        for (int d = 0; d < func_def->return_dim_count; d++) {
                            if (func_def->return_dim_sizes[d] > 0) {
                                all_dynamic = false;
                                break;
                            }
                        }
                        if (all_dynamic) {
                            literal->returns_array = false;
                            literal->return_total_bytes = 0;
                            literal->dataType = DATA_INT;
                            literal->ptr_depth = 1;
                        } else {
                            literal->returns_array = true;
                            int total = get_type_size(func_def->return_element_type);
                            for (int d = 0; d < func_def->return_dim_count; d++) {
                                if (func_def->return_dim_sizes[d] > 0)
                                    total *= func_def->return_dim_sizes[d];
                            }
                            literal->return_total_bytes = total;
                        }
                    } else if (func_def->returns_struct) {
                        literal->returns_array = true;
                        literal->return_total_bytes = func_def->return_struct_bytes;
                    } else {
                        literal->returns_array = false;
                        literal->return_total_bytes = 0;
                    }
                    if (func_def->ret_func_sig != NULL) {
                        literal->returned_func_sig = deep_copy_func_sig(func_def->ret_func_sig);
                    }
                    if (!func_was_generic &&
                            func_def->parameterCount > 0 &&
                            func_def->parameterType[0] == DATA_IDENTIFIER &&
                            current_class != NULL &&
                            literal->call_function_ParameterCount + 1
                                == func_def->parameterCount) {
                        AST_This* this_node = calloc(1, sizeof(AST_This));
                        this_node->base.type = AST_THIS;
                        this_node->base.next = literal->call_function_Parameter;
                        literal->call_function_Parameter = (ASTNode*)this_node;
                        literal->call_function_ParameterCount++;

                        int new_count = literal->call_function_ParameterCount;
                        if (literal->param_byte_sizes != NULL) {
                            int* new_sizes = malloc(sizeof(int) * (size_t)new_count);
                            if (!new_sizes) OUT_OF_MEMORY();
                            new_sizes[0] = 4;
                            for (int bi = 1; bi < new_count; bi++)
                                new_sizes[bi] = literal->param_byte_sizes[bi - 1];
                            free(literal->param_byte_sizes);
                            literal->param_byte_sizes = new_sizes;
                        } else if (new_count > 0) {
                            literal->param_byte_sizes =
                                malloc(sizeof(int) * (size_t)new_count);
                            if (!literal->param_byte_sizes) OUT_OF_MEMORY();
                            for (int bi = 0; bi < new_count; bi++)
                                literal->param_byte_sizes[bi] = 4;
                        }
                    }

                    ASTNode* arg = literal->call_function_Parameter;
                    ASTNode** prev_ptr = &literal->call_function_Parameter;
                    int arg_idx = 0;
                    while (arg && arg_idx < func_def->parameterCount) {
                        ASTNode* next_arg = arg->next;
                        if (func_def->has_varargs && arg->type != AST_VARARG_GROUP) {
                            bool is_va = false;
                            for (int vi = 0; vi < func_def->vararg_param_count; vi++) {
                                if (func_def->vararg_param_indices[vi] == arg_idx) {
                                    is_va = true;
                                    break;
                                }
                            }
                            if (is_va) {
                                AST_VarargGroup* vg = calloc(1, sizeof(AST_VarargGroup));
                                if (!vg) OUT_OF_MEMORY();
                                vg->base.type = AST_VARARG_GROUP;
                                vg->base.next = next_arg;
                                vg->base.value = arg->value;
                                vg->elements = arg;
                                vg->count = 1;
                                vg->target_elem_type = DATA_ERROR;
                                arg->next = NULL;
                                *prev_ptr = (ASTNode*)vg;
                                prev_ptr = &vg->base.next;
                                arg = next_arg;
                                arg_idx++;
                                continue;
                            }
                        }
                        if (func_def->has_varargs && arg->type == AST_VARARG_GROUP) {
                            bool is_va = false;
                            for (int vi = 0; vi < func_def->vararg_param_count; vi++) {
                                if (func_def->vararg_param_indices[vi] == arg_idx) {
                                    is_va = true;
                                    break;
                                }
                            }
                            if (!is_va) {
                                AST_VarargGroup* vg = (AST_VarargGroup*)arg;
                                if (vg->count > 0) {
                                    ASTNode* first = vg->elements;
                                    vg->elements = first->next;
                                    vg->count--;
                                    if (vg->count > 0) {
                                        first->next = (ASTNode*)vg;
                                        vg->base.next = next_arg;
                                        *prev_ptr = first;
                                        prev_ptr = &vg->base.next;
                                    } else {
                                        first->next = next_arg;
                                        *prev_ptr = first;
                                        free(vg);
                                        prev_ptr = &first->next;
                                    }
                                    arg = next_arg;
                                    arg_idx++;
                                    continue;
                                }
                            }
                        }
                        DataType param_type = func_def->parameterType[arg_idx];
                        DataType arg_type = get_expression_type(arg);
                        if (param_type == DATA_STR && arg_type == DATA_STRING) {
                            sem_wrap_str_if_needed(prev_ptr, param_type);
                            prev_ptr = &(*prev_ptr)->next;
                        } else if (
                            arg_type != param_type &&
                            (is_int_type(arg_type) || is_uint_type(arg_type) ||
                             is_float_type(arg_type) || is_ufloat_type(arg_type)) &&
                            (is_int_type(param_type) || is_uint_type(param_type) ||
                             is_float_type(param_type) || is_ufloat_type(param_type))
                        ) {
                            AST_Cast* cast = calloc(1, sizeof(AST_Cast));
                            if (!cast) OUT_OF_MEMORY();
                            cast->base.type = AST_CAST;
                            cast->base.next = next_arg;
                            cast->base.value = arg->value;
                            cast->operand = arg;
                            cast->target_type = param_type;
                            cast->target_class_name = NULL;
                            cast->target_class_id = -1;
                            cast->target_ptr_depth = 0;
                            cast->target_func_sig = NULL;
                            cast->target_dim_count = 0;
                            cast->target_dim_sizes = NULL;
                            cast->source_type = arg_type;
                            cast->source_ptr_depth = 0;
                            cast->source_func_sig = NULL;
                            cast->is_reinterpret = false;
                            cast->is_checked = false;
                            arg->next = NULL;
                            *prev_ptr = (ASTNode*)cast;
                            prev_ptr = &cast->base.next;
                        } else {
                            prev_ptr = &arg->next;
                        }
                        if (arg->type == AST_VARARG_GROUP && func_def->has_varargs) {
                            int vi = -1;
                            for (int v = 0; v < func_def->vararg_param_count; v++) {
                                if (func_def->vararg_param_indices[v] == arg_idx) {
                                    vi = v;
                                    break;
                                }
                            }
                            if (vi >= 0) {
                                int func_ptr_param_idx = func_def->vararg_param_indices[vi];
                                ASTNode* param_node = func_def->Parameter;
                                for (int s = 0; s < func_ptr_param_idx && param_node; s++)
                                    param_node = param_node->next;
                                FuncSignature* expected_sig = NULL;
                                if (param_node && (param_node->type == AST_VAR || param_node->type == AST_UNINIT_VAR)) {
                                    expected_sig = ((AST_Var*)param_node)->func_ptr_sig;
                                }
                                if (expected_sig) {
                                    AST_VarargGroup* vg = (AST_VarargGroup*)arg;
                                    ASTNode* elem = vg->elements;
                                    while (elem) {
                                        FuncSignature* actual_sig = NULL;
                                        const char* func_name = NULL;
                                        if (elem->type == AST_ADDRESS_OF) {
                                            actual_sig = ((AST_AddressOf*)elem)->target_func_sig;
                                            if (((AST_AddressOf*)elem)->operand
                                                && ((AST_AddressOf*)elem)->operand->type == AST_LITERAL)
                                                func_name = ((AST_Literal*)((AST_AddressOf*)elem)->operand)->base.value.value;
                                        } else if (elem->type == AST_LITERAL
                                                   && !((AST_Literal*)elem)->is_call_function) {
                                            AST_Literal* el = (AST_Literal*)elem;
                                            func_name = el->base.value.value;
                                            Symbol* el_sym = lookup_symbol(func_name);
                                            if (el_sym && el_sym->func_sig)
                                                actual_sig = el_sym->func_sig;
                                        }
                                        if (actual_sig && !func_sig_match(expected_sig, actual_sig)) {
                                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                                  SEMLOC(elem->value),
                                                  "function '%s' does not match expected type "
                                                  "for variadic parameter %d",
                                                  func_name ? func_name : "(unknown)", vi);
                                        }
                                        elem = elem->next;
                                    }
                                }
                            }
                        }
                        arg = next_arg;
                        arg_idx++;
                    }
                } else {
                    Symbol* sym = lookup_symbol(literal->base.value.value);
                    if (sym && sym->is_function) {
                        ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                              SEMLOC(literal->base.value),
                              "no matching function '%s' for %d argument(s)",
                              literal->base.value.value, arg_count);
                        literal->dataType = DATA_ERROR;
                    } else if (sym && sym->func_sig && !sym->is_function) {
                        literal->is_indirect_call = true;
                        literal->is_closure_call =
                            (sym->is_closure_holder || sym->is_closure_ptr);
                        if (literal->internal_name) free(literal->internal_name);
                        literal->internal_name = strdup(sym->internal_name);
                        literal->dataType = sym->func_sig->return_type;
                        if (sym->is_field) {
                            literal->is_field_ref = true;
                            literal->field_offset = sym->field_offset;
                        } else {
                            literal->is_field_ref = false;
                            literal->field_offset = 0;
                        }
                        literal->dataType = sym->func_sig->return_type;
                        FuncSignature* fsig_ = sym->func_sig;
                        int pcount_ = fsig_->param_count;
                        if (pcount_ > 0 && literal->param_byte_sizes != NULL) {
                            for (int si = 0; si < arg_count && si < pcount_; si++) {
                                bool is_va = false;
                                if (fsig_->has_varargs) {
                                    for (int vi = 0; vi < fsig_->vararg_param_count; vi++) {
                                        if (fsig_->vararg_param_indices[vi] == si)
                                        { is_va = true; break; }
                                    }
                                }
                                literal->param_byte_sizes[si] = is_va ? 8 : 4;
                            }
                        }
                        if (sym->func_sig->multi_return) {
                            literal->multi_return_type = deep_copy_multi_return(sym->func_sig->multi_return);
                        }
                        if (sym->is_method_ptr) {
                            literal->is_method_call = true;
                        }
                        FuncSignature* fsig = sym->func_sig;
                        int pcount = fsig->param_count;
                        if (pcount > 0) {
                            literal->param_byte_sizes = malloc(sizeof(int) * pcount);
                            for (int si = 0; si < pcount; si++) {
                                bool is_va = false;
                                if (fsig->has_varargs) {
                                    for (int vi = 0; vi < fsig->vararg_param_count; vi++) {
                                        if (fsig->vararg_param_indices[vi] == si)
                                            { is_va = true; break; }
                                    }
                                }
                                int sz = S_get_type_size(fsig_->param_types[si]);
                                if (is_va) {
                                    literal->param_byte_sizes[si] = 8;
                                } else {
                                    literal->param_byte_sizes[si] = (sz > 4 ? sz : 4);
                                }
                            }
                        }
                        if (fsig_->ret_func_sig != NULL && !literal->returned_func_sig) {
                            literal->returned_func_sig = deep_copy_func_sig(fsig_);
                        }
                        ASTNode* farg = literal->call_function_Parameter;
                        ASTNode** fprev = &literal->call_function_Parameter;
                        int fidx = 0;
                        while (farg && fidx < pcount) {
                            ASTNode* fnext = farg->next;
                            bool is_va = false;
                            if (fsig->has_varargs) {
                                for (int vi = 0; vi < fsig->vararg_param_count; vi++) {
                                    if (fsig->vararg_param_indices[vi] == fidx)
                                        { is_va = true; break; }
                                }
                            }
                            if (farg->type == AST_VARARG_GROUP && !is_va) {
                                AST_VarargGroup* vg = (AST_VarargGroup*)farg;
                                if (vg->count > 0) {
                                    ASTNode* first = vg->elements;
                                    vg->elements = first->next;
                                    vg->count--;
                                    if (vg->count > 0) {
                                        first->next = (ASTNode*)vg;
                                        vg->base.next = fnext;
                                        *fprev = first;
                                        fprev = &vg->base.next;
                                    } else {
                                        first->next = fnext;
                                        *fprev = first;
                                        free(vg);
                                        fprev = &first->next;
                                    }
                                    farg = fnext;
                                    fidx++;
                                    continue;
                                }
                            } else if (farg->type != AST_VARARG_GROUP && is_va) {
                                AST_VarargGroup* vg = calloc(1, sizeof(AST_VarargGroup));
                                vg->base.type = AST_VARARG_GROUP;
                                vg->base.next = fnext;
                                vg->base.value = farg->value;
                                vg->elements = farg;
                                vg->count = 1;
                                farg->next = NULL;
                                *fprev = (ASTNode*)vg;
                                fprev = &vg->base.next;
                            } else {
                                fprev = &farg->next;
                            }
                            farg = fnext;
                            fidx++;
                        }
                    } else {
                        ERROR(ERR_SEM_NOT_FUNCTION,
                              SEMLOC(literal->base.value),
                              "function '%s' is not declared",
                              literal->base.value.value);
                        literal->dataType = DATA_ERROR;
                    }
                }
                free(arg_types);
            }
        } else {
            if (literal->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* sym = lookup_symbol_with_file(literal->base.value.value,
                                                      literal->base.value.file);
                if (sym == NULL) {
                    DataType alias_type = resolve_type_alias(literal->base.value.value);
                    if (alias_type != DATA_ERROR) {
                        literal->dataType = alias_type;
                    } else
                    {
                        ERROR(ERR_SEM_UNDECLARED_VAR,
                              SEMLOC(literal->base.value),
                              "variable '%s' is not declared",
                              literal->base.value.value);
                        literal->dataType = DATA_ERROR;
                    }
                } else {
                    if (sym->dim_count > 0) {
                        if (literal->dataType == DATA_IDENTIFIER
                            || literal->dataType == DATA_ERROR) {
                            literal->dataType = sym->type;
                            }
                    } else {
                        literal->dataType = sym->type;
                    }
                    if (sym->is_static_local) {
                        literal->internal_name = strdup(sym->internal_name);
                        literal->is_field_ref = false;
                        literal->field_offset = 0;
                    } else if (sym->is_field) {
                        if (sym->is_static) {
                            literal->is_field_ref = false;
                            literal->field_offset = 0;
                            literal->internal_name = strdup(sym->internal_name);
                        } else if (sym->is_outer_field_ref) {
                            literal->is_outer_field_ref = true;
                            literal->is_field_ref = false;
                            literal->field_offset = sym->field_offset;
                            free(literal->internal_name);
                            literal->internal_name = NULL;
                        } else {
                            literal->is_field_ref = true;
                            literal->field_offset = sym->field_offset;
                            literal->is_field_array = (sym->dim_count > 0);
                            free(literal->internal_name);
                            literal->internal_name = NULL;
                        }
                    } else {
                        literal->internal_name = strdup(sym->internal_name);
                    }
                    if (sym->is_function) {
                        for (FuncEntry* e = func_table; e; e = e->next) {
                            if (e->func->is_nested &&
                                e->func->is_closure &&
                                strcmp(e->func->name.value, sym->key) == 0) {
                                literal->closure_func = e->func;
                                break;
                            }
                        }

                        if (!literal->closure_func) {
                            for (FuncEntry* e = func_table; e; e = e->next) {
                                if (e->func->is_nested && e->func->is_closure &&
                                    e->func->internal_name &&
                                    strcmp(e->func->internal_name, sym->internal_name) == 0) {
                                    literal->closure_func = e->func;
                                    break;
                               }
                            }
                        }
                    }
                    literal->is_closure_ptr = sym->is_closure_ptr;
                }
            } else if (literal->dataType == DATA_ERROR) {
                literal->dataType = Get_Datatype(literal->base.value.type);
                literal->internal_name = NULL;
            }
        }

        if (!literal->is_call_function && literal->base.value.type == TOKEN_NUMBER) {
            const char* num_str = literal->base.value.value;
            if (num_str[0] == '0' && (num_str[1] == 'x' || num_str[1] == 'X')) {
                unsigned long long uv = strtoull(num_str + 2, NULL, 16);
                if (uv <= 0x7FFFFFFFULL) {
                    literal->dataType = DATA_INT;
                } else if (uv <= 0xFFFFFFFFULL) {
                    literal->dataType = DATA_UINT;
                } else if (uv <= 0x7FFFFFFFFFFFFFFFULL) {
                    literal->dataType = DATA_INT64;
                } else {
                    literal->dataType = DATA_UINT64;
                }
            } else {
                long long v64 = parse_integer64(num_str);
                if (v64 > 4294967295LL || v64 < -2147483648LL) {
                    literal->dataType = (v64 < 0) ? DATA_INT64 : DATA_UINT64;
                } else if (v64 > 2147483647LL) {
                    literal->dataType = DATA_UINT;
                }
            }
        }

        if (!literal->is_call_function) {
            if (literal->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* sym = lookup_symbol(literal->base.value.value);
                if (sym) {
                    literal->ptr_depth = sym->ptr_depth;
                    literal->is_dynamic_array = sym->is_dynamic_array;

                    if (sym->class_name != NULL && sym->key != NULL
                        && strcmp(sym->key, sym->class_name) == 0) {
                        literal->is_class_name = true;
                    }
                    if (sym->is_function) {
                        literal->is_function_ref = true;
                    }
                } else {
                    literal->ptr_depth = 0;
                }
            } else {
                literal->ptr_depth = 0;
            }
        }
        if (literal->base.value.type == TOKEN_IDENTIFIER
                && literal->internal_name == NULL
                && !literal->is_call_function
                && !literal->is_field_ref) {
            Symbol* sym = lookup_symbol(literal->base.value.value);
            if (sym && sym->internal_name) {
                literal->internal_name = strdup(sym->internal_name);
            } else {
                ERROR(ERR_SEM_UNDECLARED_VAR,
                      SEMLOC(literal->base.value),
                      "variable '%s' is not declared",
                      literal->base.value.value);
                literal->internal_name = strdup(literal->base.value.value);
            }
        }

        static_method_done:
        break;
    }

    case AST_BINARY: {
        AST_Binary* binary = (AST_Binary*)temp;
        analyze_node(binary->left);
        analyze_node(binary->right);

        if (Operation_Check(binary) && check_division_by_zero(binary)) {
            binary->resultType = Binary_get_Datatype(binary);
        } else {
            binary->resultType = DATA_ERROR;
        }
        break;
    }

    case AST_UNARY: {
        AST_Unary* unary = (AST_Unary*)temp;
        if (unary->right != NULL) {
            analyze_node(unary->right);
        }

        DataType operand_type = DATA_ERROR;
        if (unary->right != NULL) {
            operand_type = get_expression_type(unary->right);
        }

        if (unary->op.type == TOKEN_SUB) {
            bool is_int_fam = is_int_type(operand_type);
            bool is_uint_fam = is_uint_type(operand_type);
            bool is_float_fam = is_float_type(operand_type);
            bool is_ufloat_fam = is_ufloat_type(operand_type);

            if (is_int_fam || is_uint_fam || is_float_fam || is_ufloat_fam) {
                if (is_uint_fam) {
                    unary->dataType = DATA_INT;
                } else if (is_ufloat_fam) {
                    unary->dataType = DATA_FLOAT;
                } else {
                    unary->dataType = operand_type;
                }
            } else {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(unary->op),
                      "unary minus requires numeric operand, got '%s'",
                      data_type_to_string(operand_type));
                unary->dataType = DATA_ERROR;
            }
        } else if (unary->op.type == TOKEN_OPPOSE) {
            if (operand_type == DATA_BOOL || operand_type == DATA_INT || operand_type == DATA_UINT) {
                unary->dataType = DATA_BOOL;
            } else {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(unary->op),
                      "logical NOT requires boolean or integer, got '%s'",
                      data_type_to_string(operand_type));
                unary->dataType = DATA_ERROR;
            }
        } else if (unary->op.type == TOKEN_BITWISE_NOT) {
            if (operand_type == DATA_INT || operand_type == DATA_UINT || operand_type == DATA_CHAR) {
                unary->dataType = operand_type;
            } else {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(unary->op),
                      "bitwise NOT requires integer operand");
                unary->dataType = DATA_ERROR;
            }
        } else if (unary->op.type == TOKEN_INCREMENT || unary->op.type == TOKEN_DECREMENT) {
            ASTType op_type = unary->right->type;
            if (op_type == AST_ARRAY_ACCESS) {
                AST_ArrayAccess* acc = (AST_ArrayAccess*)unary->right;
                if (acc->array != NULL
                    && acc->array->type == AST_LITERAL) {
                    AST_Literal* arr_lit = (AST_Literal*)acc->array;
                    if (arr_lit->base.value.type == TOKEN_IDENTIFIER) {
                        Symbol* arr_sym = lookup_symbol(arr_lit->base.value.value);
                        if (arr_sym && arr_sym->is_const) {
                            bool in_ctor = (current_analyzing_function
                                            && current_class
                                            && current_analyzing_function->name.value
                                            && current_class->name
                                            && strcmp(current_analyzing_function->name.value,
                                                      current_class->name) == 0);
                            if (!in_ctor) {
                                ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                                      SEMLOC(unary->base.value),
                                      "cannot modify element of const array '%s'",
                                      arr_lit->base.value.value);
                            }
                        }
                    }
                }
            }
            bool is_lvalue = (op_type == AST_LITERAL) ||
                             (op_type == AST_ARRAY_ACCESS) ||
                             (op_type == AST_UNARY);
            if (!is_lvalue) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(unary->op),
                      "prefix '%s' requires an lvalue",
                      unary->op.type == TOKEN_INCREMENT ? "++" : "--");
                unary->dataType = DATA_ERROR;
            } else
            {
                int operand_pd = get_expr_ptr_depth(unary->right);
                if (operand_pd > 0) {
                    unary->ptr_step = get_pointee_size(unary->right);
                    unary->dataType = operand_type;
                } else {
                    unary->ptr_step = 1;
                    if (operand_type == DATA_INT   || operand_type == DATA_FLOAT ||
                        operand_type == DATA_UINT  || operand_type == DATA_UFLOAT ||
                        operand_type == DATA_CHAR) {
                        unary->dataType = operand_type;
                    } else {
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(unary->op),
                              "prefix '%s' requires numeric operand",
                              unary->op.type == TOKEN_INCREMENT ? "++" : "--");
                        unary->dataType = DATA_ERROR;
                    }
                }
            }
        }
        break;
    }

    case AST_CAST: {
        AST_Cast* cast = (AST_Cast*)temp;
        if (!cast->operand) break;

        if (cast->operand->type == AST_NULL
            && cast->target_class_name != NULL
            && cast->target_ptr_depth == 0) {
            cast->source_type = DATA_INT;
            cast->source_ptr_depth = 1;
            cast->is_reinterpret = false;
            cast->is_checked = false;
            break;
        }

        analyze_node(cast->operand);

        DataType src = get_expression_type(cast->operand);
        int src_pd = get_expr_ptr_depth(cast->operand);
        DataType dst = cast->target_type;
        int dst_pd = cast->target_ptr_depth;

        cast->source_type = src;
        cast->source_ptr_depth = src_pd;

        FuncSignature* src_fsig = NULL;
        bool src_is_func_sym = false;
        if (cast->operand->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)cast->operand;
            if (!lit->is_call_function) {
                Symbol* s = lookup_symbol(lit->base.value.value);
                if (s && s->func_sig) {
                    src_fsig = s->func_sig;
                    src_is_func_sym = s->is_function;
                }
            }
        } else if (cast->operand->type == AST_VAR
                   || cast->operand->type == AST_VAR_ASS
                   || cast->operand->type == AST_UNINIT_VAR) {
            AST_Var* v = (AST_Var*)cast->operand;
            Symbol* s = lookup_symbol(v->name.value);
            if (s && s->func_sig) src_fsig = s->func_sig;
        } else if (cast->operand->type == AST_ADDRESS_OF) {
            src_fsig = ((AST_AddressOf*)cast->operand)->target_func_sig;
        }

        if (src_fsig != NULL) {
            cast->source_func_sig = deep_copy_func_sig(src_fsig);
            cast->source_ptr_depth = 1;

            if (src_is_func_sym && cast->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)cast->operand;
                AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                if (!addr) OUT_OF_MEMORY();
                addr->base.type = AST_ADDRESS_OF;
                addr->base.value = lit->base.value;
                addr->operand = cast->operand;
                addr->resultType = DATA_INT;
                addr->ptr_depth = 1;
                addr->target_func_sig = deep_copy_func_sig(src_fsig);
                cast->operand = (ASTNode*)addr;
            }
        }
        if (cast->target_func_sig != NULL) {
            cast->target_ptr_depth = 1;
        }

        if (src_fsig != NULL || cast->target_func_sig != NULL) {
            if (cast->target_func_sig != NULL && src_fsig == NULL) {
                if (src != DATA_INT && src != DATA_UINT && src_pd == 0) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                          "function pointer cast requires integer source, "
                          "got '%s'", data_type_to_string(src));
                    cast->target_type = DATA_ERROR;
                    break;
                }
                cast->is_reinterpret = true;
            } else if (cast->target_func_sig == NULL && src_fsig != NULL) {
                if (dst != DATA_INT && dst != DATA_UINT && dst_pd == 0) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                          "function pointer-to-integer cast requires "
                          "integer target, got '%s'",
                          data_type_to_string(dst));
                    cast->target_type = DATA_ERROR;
                    break;
                }
                cast->is_reinterpret = true;
            } else {
                cast->is_reinterpret = true;
            }
            break;
        }

        if (cast->target_class_name != NULL) {
            ClassEntry* dst_cls = find_class(cast->target_class_name);
            if (!dst_cls) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND, SEMLOC(cast->base.value),
                      "unknown class '%s' in cast target",
                      cast->target_class_name);
                cast->target_type = DATA_ERROR;
                break;
            }
        }

        bool src_num = is_int_type(src) || is_uint_type(src) ||
                       is_float_type(src) || is_ufloat_type(src) ||
                       src == DATA_CHAR || src == DATA_BOOL;
        bool dst_num = is_int_type(dst) || is_uint_type(dst) ||
                       is_float_type(dst) || is_ufloat_type(dst) ||
                       dst == DATA_CHAR || dst == DATA_BOOL;
        bool src_ptr = (src_pd > 0);
        bool dst_ptr = (dst_pd > 0);

        if (src == DATA_ERROR || dst == DATA_ERROR) {
            break;
        }
        else if (cast->target_dim_count > 0) {
            Symbol* src_sym = NULL;
            AST_Cast* src_cast = NULL;

            if (cast->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)cast->operand;
                if (!lit->is_call_function)
                    src_sym = lookup_symbol(lit->base.value.value);
            } else if (cast->operand->type == AST_VAR
                       || cast->operand->type == AST_VAR_ASS
                       || cast->operand->type == AST_UNINIT_VAR) {
                src_sym = lookup_symbol(((AST_Var*)cast->operand)->name.value);
            } else if (cast->operand->type == AST_CAST) {
                src_cast = (AST_Cast*)cast->operand;
            }

            bool src_is_arr = (src_sym != NULL && src_sym->dim_count > 0)
                              || (src_cast != NULL
                                  && src_cast->target_dim_count > 0);
            bool src_is_ptr = (src_pd > 0);

            if (!src_is_arr && !src_is_ptr) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                      "array view cast requires pointer or array source, "
                      "got '%s%s'",
                      data_type_to_string(src), src_pd > 0 ? "*" : "");
                cast->target_type = DATA_ERROR;
                break;
            }

            if (!cast_operand_is_unsafe(cast->operand)) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                      "array view cast requires 'unsafe' qualifier "
                      "on the operand");
                cast->target_type = DATA_ERROR;
                break;
            }

            int src_elem_size = 0;
            int src_total_bytes = 0;
            bool src_size_known = false;
            int src_dim_count = 0;
            int* src_dim_sizes = NULL;

            if (src_sym && src_sym->dim_count > 0 && src_sym->dim_sizes) {
                src_elem_size = S_get_type_size(src_sym->type);
                src_dim_count = src_sym->dim_count;
                src_dim_sizes = src_sym->dim_sizes;
            } else if (src_cast && src_cast->target_dim_count > 0) {
                src_elem_size = S_get_type_size(src_cast->target_type);
                src_dim_count = src_cast->target_dim_count;
                src_dim_sizes = src_cast->target_dim_sizes;
            }

            if (src_dim_sizes != NULL && src_elem_size > 0) {
                src_total_bytes = src_elem_size;
                src_size_known = true;
                for (int i = 0; i < src_dim_count; i++) {
                    if (src_dim_sizes[i] > 0) {
                        src_total_bytes *= src_dim_sizes[i];
                    } else {
                        src_size_known = false;
                        break;
                    }
                }
            }

            bool all_target_empty = (cast->target_dim_count == 1
                                     && cast->target_dim_sizes[0] == -1);
            bool need_byte_recompute = false;
            int dst_elem_size = S_get_type_size(cast->target_type);
            if (all_target_empty && src_size_known && src_total_bytes > 0
                && dst_elem_size > 0
                && dst_elem_size != src_elem_size) {
                need_byte_recompute = true;
            }

            if (need_byte_recompute) {
                cast->target_dim_sizes[0] =
                    src_total_bytes / dst_elem_size;
            } else if (src_dim_sizes != NULL) {
                for (int i = 0; i < cast->target_dim_count; i++) {
                    if (cast->target_dim_sizes[i] == -1
                        && i < src_dim_count) {
                        cast->target_dim_sizes[i] = src_dim_sizes[i];
                    }
                }
            }

            cast->target_ptr_depth = 1;
            cast->is_reinterpret = true;
        } else if (src_ptr && dst_ptr) {
            int src_level_const = 0;
            if (cast->operand->type == AST_LITERAL) {
                AST_Literal* ol = (AST_Literal*)cast->operand;
                if (ol->base.value.type == TOKEN_IDENTIFIER) {
                    Symbol* sym = lookup_symbol(ol->base.value.value);
                    if (sym) src_level_const = sym->level_const_mask;
                }
            } else if (cast->operand->type == AST_ADDRESS_OF) {
                AST_AddressOf* ao = (AST_AddressOf*)cast->operand;
                src_level_const = ao->level_const_mask;
            }

            if (src_level_const & 1u) {
                ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                      SEMLOC(cast->base.value),
                      "cannot cast away 'const' qualifier");
            }
            cast->is_reinterpret = true;
        } else if (src_num && dst_num) {
        }
        else if (src == DATA_STRING && (dst == DATA_INT || dst == DATA_UINT)
                 && dst_pd == 0) {
            cast->is_reinterpret = true;
        }
        else if ((src == DATA_INT || src == DATA_UINT) && dst == DATA_STRING
                 && src_pd == 0) {
            cast->is_reinterpret = true;
        }
        else if (src_ptr && (dst == DATA_INT || dst == DATA_UINT)) {
            cast->is_reinterpret = true;
        }
        else if (dst_ptr && (src == DATA_INT || src == DATA_UINT)) {
            cast->is_reinterpret = true;
        }
        else if (src == DATA_IDENTIFIER && dst == DATA_IDENTIFIER
                 && src_pd == 0 && dst_pd == 0) {
            const char* src_cls_name = infer_static_class_name(cast->operand);
            ClassEntry* src_cls = src_cls_name ? find_class(src_cls_name) : NULL;
            ClassEntry* dst_cls = find_class(cast->target_class_name);
            if (dst_cls != NULL) {
                cast->target_class_id = dst_cls->class_id;
                if (src_cls != NULL) {
                    if (is_subclass_of(src_cls, dst_cls)) {
                        cast->is_reinterpret = false;
                    } else if (is_subclass_of(dst_cls, src_cls)) {
                        cast->is_checked = true;
                    } else {
                        ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                              "cannot cast between unrelated classes '%s' and '%s'",
                              src_cls->name, dst_cls->name);
                    }
                } else {
                    cast->is_checked = true;
                }
            }
        }
        else if (src == DATA_IDENTIFIER && src_pd == 0
                 && (dst == DATA_INT || dst == DATA_UINT || dst_pd > 0)) {
            if (!cast_operand_is_unsafe(cast->operand)) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                      "class-to-primitive cast requires 'unsafe' "
                      "qualifier on the operand");
                cast->target_type = DATA_ERROR;
                break;
            }
            cast->is_reinterpret = true;
        }
        else if ((src == DATA_INT || src == DATA_UINT || src_pd > 0)
                 && dst == DATA_IDENTIFIER && dst_pd == 0
                 && cast->target_class_name != NULL) {
            if (!cast_operand_is_unsafe(cast->operand)) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                      "primitive-to-class cast requires 'unsafe' "
                      "qualifier on the operand");
                cast->target_type = DATA_ERROR;
                break;
            }
            ClassEntry* dst_cls = find_class(cast->target_class_name);
            if (!dst_cls) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND, SEMLOC(cast->base.value),
                      "unknown class '%s' in cast target",
                      cast->target_class_name);
                cast->target_type = DATA_ERROR;
                break;
            }
            cast->is_reinterpret = true;
        }
        else {
            ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(cast->base.value),
                  "invalid cast from '%s%s' to '%s%s'",
                  data_type_to_string(src), src_pd > 0 ? "*" : "",
                  data_type_to_string(dst), dst_pd > 0 ? "*" : "");
            cast->target_type = DATA_ERROR;
        }
        break;
    }

    case AST_STR_SLICE: {
        AST_StrSlice* slice = (AST_StrSlice*)temp;
        if (slice->source) analyze_node(slice->source);
        if (slice->start) analyze_node(slice->start);
        if (slice->end) analyze_node(slice->end);
        DataType src_type = get_expression_type(slice->source);
        if (src_type == DATA_STRING) {
            AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
            if (!conv) OUT_OF_MEMORY();
            conv->base.type = AST_STR_FROM_STRING;
            conv->base.next = NULL;
            conv->base.value = slice->base.value;
            conv->source = slice->source;
            slice->source = (ASTNode*)conv;
            src_type = DATA_STR;
        }
        if (src_type != DATA_STR) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(slice->base.value),
                  "slice operator requires str type, got '%s'",
                  data_type_to_string(src_type));
        }
        break;
    }

    case AST_DEREF: {
        AST_Deref* deref = (AST_Deref*)temp;
        analyze_node(deref->operand);

        DataType operand_type = get_expression_type(deref->operand);

        if (deref->operand->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)deref->operand;
            Symbol* sym = lookup_symbol_with_file(lit->base.value.value,
                                                  lit->base.value.file);
            if (sym && sym->ptr_array_info) {
                deref->deref_array_info = malloc(sizeof(PtrArrayInfo));
                deref->deref_array_info->element_type = sym->ptr_array_info->element_type;
                deref->deref_array_info->dim_count    = sym->ptr_array_info->dim_count;
                deref->deref_array_info->dim_sizes    = malloc(sizeof(int) * sym->ptr_array_info->dim_count);
                memcpy(deref->deref_array_info->dim_sizes,
                       sym->ptr_array_info->dim_sizes,
                       sizeof(int) * sym->ptr_array_info->dim_count);
                deref->resultType = sym->ptr_array_info->element_type;
                deref->ptr_depth = sym->ptr_depth - 1;
                break;
            }
        }

        int operand_ptr_depth = get_expr_ptr_depth(deref->operand);

        bool is_class_operand = false;
        if (deref->operand->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)deref->operand;
            if (!lit->is_call_function
                && lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* sym = lookup_symbol(lit->base.value.value);
                if (sym && sym->class_name) is_class_operand = true;
            }
        } else if (deref->operand->type == AST_THIS) {
            is_class_operand = (current_class != NULL);
        }

        if (is_class_operand && operand_ptr_depth <= 0) {
            deref->ptr_depth = 0;
            deref->resultType = operand_type;
            break;
        }

        if (operand_ptr_depth <= 0 && operand_type != DATA_ERROR) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(deref->base.value),
                  "cannot dereference non-pointer type");
            deref->resultType = DATA_ERROR;
            deref->ptr_depth = 0;
        } else {
            deref->ptr_depth = operand_ptr_depth - 1;
            deref->resultType = operand_type;
        }
        break;
    }

    case AST_ADDRESS_OF: {
        AST_AddressOf* addr = (AST_AddressOf*)temp;

        if (addr->operand->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)addr->operand;
            if (lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* sym = lookup_symbol_with_file(lit->base.value.value,
                                                      lit->base.value.file);
                if (sym && sym->is_function && sym->func_sig) {
                    addr->target_func_sig = deep_copy_func_sig(sym->func_sig);
                    addr->ptr_depth = 1;
                    addr->resultType = DATA_INT;
                    lit->dataType = DATA_INT;
                    if (lit->internal_name) free(lit->internal_name);
                    lit->internal_name = sym->internal_name
                        ? strdup(sym->internal_name)
                        : strdup(sym->key);
                    break;
                }
                if (addr->target_func_sig != NULL) {
                    addr->ptr_depth = 1;
                    addr->resultType = DATA_INT;
                    lit->dataType = DATA_INT;
                    break;
                }
                if (sym && sym->is_const && sym->const_value_str) {
                    if (lit->internal_name) free(lit->internal_name);
                    lit->internal_name = strdup(sym->internal_name);
                    lit->dataType = sym->type;
                    addr->ptr_depth = 1;
                    addr->resultType = DATA_INT;
                    addr->level_const_mask = 1u;
                    if (addr->const_value_str) free(addr->const_value_str);
                    addr->const_value_str = strdup(sym->const_value_str);
                    break;
                }
            }
        }

        if (addr->operand->type == AST_ARRAY_ACCESS) {
            AST_ArrayAccess* arr_acc = (AST_ArrayAccess*)addr->operand;
            if (arr_acc->array != NULL
                && arr_acc->array->type == AST_LITERAL) {
                AST_Literal* arr_lit = (AST_Literal*)arr_acc->array;
                if (arr_lit->base.value.type == TOKEN_IDENTIFIER) {
                    Symbol* arr_sym = lookup_symbol(arr_lit->base.value.value);
                    if (arr_sym && arr_sym->is_const) {
                        addr->level_const_mask = 1u;
                    }
                }
            }
        }

        analyze_node(addr->operand);

        ASTType op_type = addr->operand->type;
        bool is_lvalue = (op_type == AST_LITERAL) ||
                         (op_type == AST_DEREF) ||
                         (op_type == AST_ARRAY_ACCESS) ||
                         (op_type == AST_MEMBER_ACCESS) ||
                         (op_type == AST_VAR);

        int operand_ptr_depth = get_expr_ptr_depth(addr->operand);

        if (!is_lvalue) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(addr->base.value),
                  "cannot take address of rvalue");
            addr->ptr_depth = 0;
        } else {
            addr->ptr_depth = operand_ptr_depth + 1;
        }
        addr->resultType = DATA_INT;
        break;
    }

    case AST_NULL: {
        AST_Null* n = (AST_Null*)temp;
        n->ptr_depth = 1;
        break;
    }

    case AST_CONDITIONAL: {
            AST_Conditional* tern = (AST_Conditional*)temp;
            analyze_node(tern->condition);
            analyze_node(tern->then_expr);
            analyze_node(tern->else_expr);
            DataType cond_type = get_expression_type(tern->condition);
            if (cond_type != DATA_BOOL && cond_type != DATA_ERROR) {
                ERROR(ERR_SEM_CONDITION_NOT_BOOL,
                      SEMLOC(tern->condition->value),
                      "ternary condition must be boolean, got '%s'",
                      data_type_to_string(cond_type));
            }
            DataType then_type = get_expression_type(tern->then_expr);
            DataType else_type = get_expression_type(tern->else_expr);
            tern->resultType = L_A_R_get_Datatype(then_type, else_type,
                                                   tern->base.value.line,
                                                   tern->base.value.column);
            break;
    }

    case AST_ARRAY_DECL: {
        AST_ArrayDecl* arr = (AST_ArrayDecl*)temp;

        if (arr->dim_exprs) {
            for (int i = 0; i < arr->dim_count; i++) {
                if (arr->dim_sizes[i] == -2 && arr->dim_exprs[i]) {
                    analyze_node(arr->dim_exprs[i]);
                    int64_t val = 0;
                    bool of = false;
                    if (eval_constant_expr(arr->dim_exprs[i], &val, &of)) {
                        if (of) {
                            ERROR(ERR_SEM_CONST_OVERFLOW,
                                  SEMLOC(arr->base.value),
                                  "array dimension overflows 32-bit integer range");
                        } else {
                            arr->dim_sizes[i] = (int)val;
                            AST_Free(arr->dim_exprs[i]);
                            arr->dim_exprs[i] = NULL;
                        }
                    } else {
                        ERROR(ERR_SEM_INVALID_INDEX,
                              SEMLOC(arr->base.value),
                              "array dimension must be a compile-time constant");
                    }
                }
            }
        }

        bool has_unknown_dim = false;
        int total_elements = 1;
        for (int i = 0; i < arr->dim_count; i++) {
            if (arr->dim_sizes[i] == -1) {
                has_unknown_dim = true;
                continue;
            }
            if (arr->dim_sizes[i] <= 0) {
                Token arr_token = {.line = arr->base.value.line, .column = arr->base.value.column};
                ERROR(ERR_SEM_INVALID_INDEX,
                      SEMLOC(arr_token),
                      "invalid dimension size %d for array '%s'",
                      arr->dim_sizes[i], arr->name);
                total_elements = 0;
                break;
            }
            total_elements *= arr->dim_sizes[i];
        }

        if (arr->init_list != NULL) {
            analyze_node(arr->init_list);
            if (arr->init_list->type == AST_CAST) {
                AST_Cast* c = (AST_Cast*)arr->init_list;
                if (c->target_dim_count > 0) {
                    arr->is_view = true;
                    for (int i = 0; i < arr->dim_count; i++) {
                        if (arr->dim_sizes[i] == -1
                            && i < c->target_dim_count
                            && c->target_dim_sizes[i] > 0) {
                            arr->dim_sizes[i] = c->target_dim_sizes[i];
                            }
                    }
                }
            }
            else if (arr->init_list->type == AST_NEW_ARRAY) {
                AST_NewArray* narr = (AST_NewArray*)arr->init_list;
                for (int i = 0; i < arr->dim_count && i < narr->dim_count; i++) {
                    if (arr->dim_sizes[i] == -1) {
                        arr->dim_sizes[i] = narr->dim_sizes[i];
                    }
                }
                total_elements = 1;
                for (int i = 0; i < arr->dim_count; i++) {
                    if (arr->dim_sizes[i] > 0) {
                        total_elements *= arr->dim_sizes[i];
                    }
                }
                has_unknown_dim = false;
            }
            else if (arr->init_list->type == AST_LITERAL
                     && ((AST_Literal*)arr->init_list)->base.value.type == TOKEN_IDENTIFIER
                     && !((AST_Literal*)arr->init_list)->is_call_function) {
                AST_Literal* src_lit = (AST_Literal*)arr->init_list;
                Symbol* src_sym = lookup_symbol_with_file(
                    src_lit->base.value.value, src_lit->base.value.file);
                if (src_sym && src_sym->dim_count > 0 && src_sym->dim_sizes) {
                    for (int i = 0; i < arr->dim_count; i++) {
                        if (arr->dim_sizes[i] == -1 && i < src_sym->dim_count
                            && src_sym->dim_sizes[i] > 0) {
                            arr->dim_sizes[i] = src_sym->dim_sizes[i];
                        }
                    }
                    total_elements = 1;
                    has_unknown_dim = false;
                    for (int i = 0; i < arr->dim_count; i++) {
                        if (arr->dim_sizes[i] > 0) {
                            total_elements *= arr->dim_sizes[i];
                        } else {
                            has_unknown_dim = true;
                        }
                    }
                }
            }
            else if (arr->init_list->type == AST_INIT_LIST) {
                AST_InitList* list = (AST_InitList*)arr->init_list;
                if (has_unknown_dim && list->dim_count > 0) {
                    for (int i = 0; i < arr->dim_count; i++) {
                        if (arr->dim_sizes[i] == -1 && i < list->dim_count) {
                            arr->dim_sizes[i] = list->dim_sizes[i];
                        }
                    }
                    total_elements = 1;
                    for (int i = 0; i < arr->dim_count; i++) {
                        if (arr->dim_sizes[i] > 0) {
                            total_elements *= arr->dim_sizes[i];
                        }
                    }
                    has_unknown_dim = false;
                }
                if (list->count > total_elements && total_elements > 0) {
                    Token arr_token2 = {.line = arr->base.value.line, .column = arr->base.value.column};
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(arr_token2),
                          "too many initializers for array '%s'",
                          arr->name);
                }
                if (list->elemType != DATA_ERROR && list->elemType != arr->elementType) {
                    bool compatible = false;
                    if (arr->elementType == DATA_IDENTIFIER
                        || list->elemType == DATA_IDENTIFIER) compatible = true;
                    if (arr->elementType == DATA_UINT && list->elemType == DATA_INT) compatible = true;
                    else if (arr->elementType == DATA_FLOAT && list->elemType == DATA_INT) compatible = true;
                    else if (arr->elementType == DATA_UFLOAT && list->elemType == DATA_FLOAT) compatible = true;
                    else if (arr->elementType == DATA_UFLOAT && list->elemType == DATA_INT) compatible = true;
                    else if (arr->elementType == DATA_UFLOAT && list->elemType == DATA_UINT) compatible = true;
                    else if (is_int_type(arr->elementType) && is_int_type(list->elemType)) compatible = true;
                    else if (is_uint_type(arr->elementType) && is_uint_type(list->elemType)) compatible = true;
                    else if (is_int_type(arr->elementType) && is_uint_type(list->elemType)) compatible = true;
                    else if (is_uint_type(arr->elementType) && is_int_type(list->elemType)) compatible = true;
                    else if (is_float_type(arr->elementType) && is_float_type(list->elemType)) compatible = true;
                    else if (is_ufloat_type(arr->elementType) && is_ufloat_type(list->elemType)) compatible = true;
                    else if (is_float_type(arr->elementType) && is_int_type(list->elemType)) compatible = true;
                    else if (is_float_type(arr->elementType) && is_uint_type(list->elemType)) compatible = true;
                    else if (is_ufloat_type(arr->elementType) && is_float_type(list->elemType)) compatible = true;
                    else if (is_ufloat_type(arr->elementType) && is_int_type(list->elemType)) compatible = true;
                    else if (is_ufloat_type(arr->elementType) && is_uint_type(list->elemType)) compatible = true;
                    if (!compatible) {
                        Token arr_token3 = {.line = arr->base.value.line, .column = arr->base.value.column};
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(arr_token3),
                              "array element type mismatch: expected '%s', got '%s'",
                              data_type_to_string(arr->elementType),
                              data_type_to_string(list->elemType));
                    }
                }
            }
        } else {
            if (!in_param_list && has_unknown_dim) {
                Token arr_token4 = {.line = arr->base.value.line, .column = arr->base.value.column};
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(arr_token4),
                      "array '%s' of unknown size must be initialized",
                      arr->name);
            }
        }

        int elem_size = S_get_type_size(arr->elementType);
        int total_elements_ = 1;
        bool all_known = true;
        for (int i = 0; i < arr->dim_count; i++) {
            if (arr->dim_sizes[i] > 0) {
                total_elements_ *= arr->dim_sizes[i];
            } else {
                all_known = false;
            }
        }
        bool is_heap_arr = (arr->init_list != NULL
                            && arr->init_list->type == AST_NEW_ARRAY);
        if (!is_heap_arr && arr->init_list != NULL
            && arr->init_list->type == AST_LITERAL
            && ((AST_Literal*)arr->init_list)->is_call_function
            && ((AST_Literal*)arr->init_list)->returns_array) {
            is_heap_arr = true;
        }
        if (arr->is_view) {
            arr->total_bytes = 4;
        } else if (is_heap_arr) {
            arr->total_bytes = 8;
        } else if (all_known && total_elements_ > 0) {
            int checked = compute_array_total_bytes(arr->elementType,
                                                     arr->dim_count,
                                                     arr->dim_sizes, NULL);
            if (checked < 0) {
                ERROR(ERR_SEM_INVALID_INDEX,
                      SEMLOC(arr->base.value),
                      "array '%s' size overflows 32-bit range",
                      arr->name);
                arr->total_bytes = 8;
            } else {
                arr->total_bytes = checked;
            }
        } else {
            arr->total_bytes = 8;
        }

        if (!is_heap_arr && current_analyzing_function != NULL
            && arr->modifier != 3
            && arr->total_bytes > 1048576) {
            WARNING(ERR_SEM_INVALID_INDEX,
                    SEMLOC(arr->base.value),
                    "local array '%s' needs %d bytes of stack, "
                    "exceeding the ~1 MB default stack limit; "
                    "use 'new' for heap allocation",
                    arr->name, arr->total_bytes);
        }
        char* internal = generate_internal_name();
        char mangled_key[512] = "";
        if (arr->modifier == 3
            && current_analyzing_function == NULL
            && current_class == NULL
            && arr->base.value.file != NULL
            && arr->name != NULL) {
            make_static_mangled_name(arr->base.value.file, arr->name,
                                     mangled_key, sizeof(mangled_key));
            if (mangled_key[0] != '\0') {
                free(internal);
                internal = strdup(mangled_key);
            }
        }
        arr->internal_name = strdup(internal);
        Symbol* sym = create_symbol(
            (Token){.value = arr->name, .type = TOKEN_IDENTIFIER},
            arr->elementType,
            (mangled_key[0] != '\0') ? mangled_key : arr->name,
            internal
        );
        sym->is_unsafe = arr->is_unsafe;
        sym->is_const = arr->is_const;
        sym->dim_count = arr->dim_count;
        sym->dim_sizes = malloc(sizeof(int) * arr->dim_count);
        memcpy(sym->dim_sizes, arr->dim_sizes, sizeof(int) * arr->dim_count);
        if (arr->elementType == DATA_IDENTIFIER
            && arr->element_class_name != NULL) {
            sym->class_name = strdup(arr->element_class_name);
        }

        if (arr->is_view) {
            sym->total_bytes = 4;
            sym->is_value_type = false;
            sym->is_dynamic_array = false;
            sym->alignment = 4;
        } else if (is_heap_arr) {
            sym->total_bytes = 8;
            sym->is_value_type = true;
            sym->is_dynamic_array = true;
            sym->alignment = 4;
            arr->is_dynamic_array = true;
        } else if (all_known && total_elements_ > 0) {
            if (is_heap_arr) {
                sym->total_bytes = 8;
                sym->is_value_type = true;
                sym->is_dynamic_array = true;
                sym->alignment = 4;
            } else if (all_known && total_elements_ > 0) {
                sym->total_bytes = arr->total_bytes;
                sym->is_value_type = true;
                sym->alignment = (elem_size >= 8) ? 8 : (elem_size >= 4) ? 4 : 1;
            } else {
                sym->total_bytes = 0;
                sym->is_value_type = false;
                sym->alignment = 4;
            }
        } else {
            sym->total_bytes = 0;
            sym->is_value_type = false;
            sym->alignment = 4;
        }

        insert_symbol(sym);
        free(internal);
        break;
    }

    case AST_ARRAY_ACCESS: {
        AST_ArrayAccess* acc = (AST_ArrayAccess*)temp;
        analyze_node(acc->array);
        for (int i = 0; i < acc->index_count; i++) {
            analyze_node(acc->indices[i]);
        }

        Symbol* sym = NULL;
        if (acc->array->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)acc->array;
            sym = lookup_symbol_with_file(lit->base.value.value,
                                          lit->base.value.file);
        } else if (acc->array->type == AST_DEREF) {
            AST_Deref* deref = (AST_Deref*)acc->array;
            if (deref->deref_array_info) {
                PtrArrayInfo* ai = deref->deref_array_info;
                acc->full_dim_count = ai->dim_count;
                acc->full_dim_sizes = malloc(sizeof(int) * ai->dim_count);
                memcpy(acc->full_dim_sizes, ai->dim_sizes, sizeof(int) * ai->dim_count);
                if (acc->index_count > ai->dim_count) {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(acc->indices[ai->dim_count]->value),
                          "too many indices for array pointer");
                }
                int remaining = ai->dim_count - acc->index_count;
                acc->dim_count = remaining;
                if (remaining > 0) {
                    acc->dim_sizes = malloc(sizeof(int) * remaining);
                    memcpy(acc->dim_sizes, &ai->dim_sizes[acc->index_count],
                           sizeof(int) * remaining);
                }
                acc->elementType = ai->element_type;
                acc->dim_sizes = (remaining > 0) ? acc->dim_sizes : NULL;
            }
        }

        if (sym && sym->dim_count != 0) {
            int totalDims = (sym->dim_count > 0) ? sym->dim_count : 1;
            acc->full_dim_count = totalDims;
            acc->full_dim_sizes = malloc(sizeof(int) * totalDims);
            if (sym->dim_sizes) {
                for (int i = 0; i < totalDims; i++)
                    acc->full_dim_sizes[i] = (sym->dim_sizes[i] > 0) ? sym->dim_sizes[i] : -1;
            } else {
                for (int i = 0; i < totalDims; i++)
                    acc->full_dim_sizes[i] = -1;
            }
            acc->is_heap_array = (sym != NULL) && sym->is_dynamic_array;

            if (acc->index_count > totalDims) {
                ERROR(ERR_SEM_INVALID_INDEX,
                      SEMLOC(acc->indices[totalDims]->value),
                      "too many indices for array");
                acc->elementType = DATA_ERROR;
            } else {
                int remaining = totalDims - acc->index_count;
                acc->dim_count = remaining;
                if (remaining > 0) {
                    acc->dim_sizes = malloc(sizeof(int) * remaining);
                    memcpy(acc->dim_sizes, &sym->dim_sizes[acc->index_count], sizeof(int) * remaining);
                    acc->elementType = sym->type;
                } else {
                    acc->elementType = sym->type;
                    acc->dim_sizes = NULL;
                }
            }

            check_const_indices_bounds(acc);
        } else if (sym && sym->ptr_depth > 0) {
            if (sym->func_sig && sym->func_sig->multi_return
                && sym->func_sig->multi_return->count > 0) {
                int remaining = sym->ptr_depth - 1;
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->elementType = DATA_INT;
                acc->is_heap_array = (sym->total_bytes >= 8
                                      && sym->is_value_type);
            } else {
                int remaining = sym->ptr_depth - 1;
                if (acc->index_count > sym->ptr_depth) {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(acc->indices[sym->ptr_depth]->value),
                          "too many indices for pointer");
                    acc->elementType = DATA_ERROR;
                } else {
                    acc->full_dim_count = 0;
                    acc->full_dim_sizes = NULL;
                    acc->dim_count = 0;
                    acc->dim_sizes = NULL;
                    acc->elementType = sym->type;
                    if (sym->func_sig != NULL && acc->index_count == 1) {
                        acc->func_sig = deep_copy_func_sig(sym->func_sig);
                    }
                }
            }
        } else if (sym && sym->type == DATA_STR)
        {
            if (acc->index_count > 1) {
                ERROR(ERR_SEM_INVALID_INDEX,
                      SEMLOC(acc->indices[1]->value),
                      "too many indices for str");
                acc->elementType = DATA_ERROR;
            } else {
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->elementType = DATA_INT;
            }
        } else if (acc->array->type == AST_CAST) {
            AST_Cast* c = (AST_Cast*)acc->array;
            if (c->target_dim_count > 0) {
                int totalDims = c->target_dim_count;
                acc->full_dim_count = totalDims;
                acc->full_dim_sizes = malloc(sizeof(int) * totalDims);
                memcpy(acc->full_dim_sizes, c->target_dim_sizes,
                       sizeof(int) * totalDims);
                acc->is_heap_array = false;
                if (acc->index_count > totalDims) {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(acc->indices[totalDims]->value),
                          "too many indices for array view");
                    acc->elementType = DATA_ERROR;
                } else {
                    int remaining = totalDims - acc->index_count;
                    acc->dim_count = remaining;
                    if (remaining > 0) {
                        acc->dim_sizes = malloc(sizeof(int) * remaining);
                        memcpy(acc->dim_sizes,
                               &c->target_dim_sizes[acc->index_count],
                               sizeof(int) * remaining);
                    } else {
                        acc->dim_sizes = NULL;
                    }
                    acc->elementType = c->target_type;
                }
                check_const_indices_bounds(acc);
            } else if (c->target_ptr_depth == 1 && c->target_type != DATA_IDENTIFIER) {
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->elementType = c->target_type;
            } else {
                acc->elementType = DATA_ERROR;
            }
        } else if (acc->array->type == AST_DEREF) {
            AST_Deref* deref = (AST_Deref*)acc->array;
            if (deref->deref_array_info) {
                PtrArrayInfo* ai = deref->deref_array_info;
                int totalDims = ai->dim_count;
                acc->full_dim_count = totalDims;
                acc->full_dim_sizes = malloc(sizeof(int) * totalDims);
                memcpy(acc->full_dim_sizes, ai->dim_sizes, sizeof(int) * totalDims);
                acc->is_heap_array = (sym != NULL) && sym->is_dynamic_array;
                if (acc->index_count > totalDims) {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(acc->indices[totalDims]->value),
                          "too many indices for array pointer");
                    acc->elementType = DATA_ERROR;
                } else {
                    int remaining = totalDims - acc->index_count;
                    acc->dim_count = remaining;
                    if (remaining > 0) {
                        acc->dim_sizes = malloc(sizeof(int) * remaining);
                        memcpy(acc->dim_sizes, &ai->dim_sizes[acc->index_count],
                               sizeof(int) * remaining);
                        acc->elementType = ai->element_type;
                    } else {
                        acc->elementType = ai->element_type;
                        acc->dim_sizes = NULL;
                    }
                }
                check_const_indices_bounds(acc);
            }
        } else {
            DataType expr_type = get_expression_type(acc->array);
            if (expr_type == DATA_STR) {
                acc->elementType = DATA_INT;
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
            } else if (expr_type != DATA_ERROR) {
                acc->elementType = expr_type;
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
            } else if (sym != NULL) {
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->elementType = sym->type;
                if (sym->func_sig != NULL && acc->index_count == 1) {
                    AST_Literal* acc_lit = calloc(1, sizeof(AST_Literal));
                    acc_lit->base.type = AST_LITERAL;
                    acc_lit->base.next = NULL;
                    acc_lit->base.value = acc->base.value;
                    acc_lit->dataType = sym->type;
                    acc_lit->returned_func_sig = deep_copy_func_sig(sym->func_sig);
                    acc->array = (ASTNode*)acc_lit;
                }
            } else {
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->elementType = DATA_ERROR;
            }
        }
        break;
    }

    case AST_NEW_ARRAY: {
        AST_NewArray* newArr = (AST_NewArray*)temp;
        if (newArr->elementType == DATA_IDENTIFIER) {
            newArr->element_total_bytes = 4;
        } else {
            newArr->element_total_bytes = S_get_type_size(newArr->elementType);
        }
        if (newArr->dim_exprs) {
            for (int i = 0; i < newArr->dim_count; i++) {
                if (newArr->dim_exprs[i]) {
                    analyze_node(newArr->dim_exprs[i]);
                }
            }
        }
        for (int i = 0; i < newArr->dim_count; i++) {
            if (newArr->dim_sizes[i] == -1
                && newArr->dim_exprs != NULL
                && newArr->dim_exprs[i] != NULL) {
                continue;
                }
            if (newArr->dim_sizes[i] < 0) {
                Token narr_token = {.line = newArr->base.value.line, .column = newArr->base.value.column};
                ERROR(ERR_SEM_INVALID_INDEX,
                      SEMLOC(narr_token),
                      "array dimension must be non-negative");
            }
        }
        break;
    }

    case AST_ARRAY_ASS: {
        AST_ArrayAss* ass = (AST_ArrayAss*)temp;
        if (ass->target) analyze_node(ass->target);

        if (ass->value != NULL
            && ass->target != NULL
            && ass->target->type == AST_ARRAY_ACCESS) {
            AST_ArrayAccess* pre_acc = (AST_ArrayAccess*)ass->target;
            Symbol* pre_arr_sym = NULL;
            if (pre_acc->array != NULL) {
                if (pre_acc->array->type == AST_LITERAL) {
                    AST_Literal* al = (AST_Literal*)pre_acc->array;
                    if (!al->is_call_function)
                        pre_arr_sym = lookup_symbol_with_file(al->base.value.value,
                                                              al->base.value.file);
                } else if (pre_acc->array->type == AST_VAR
                           || pre_acc->array->type == AST_VAR_ASS) {
                    pre_arr_sym = lookup_symbol(((AST_Var*)pre_acc->array)->name.value);
                }
            }
            if (pre_arr_sym != NULL
                && pre_arr_sym->class_name != NULL
                && strcmp(pre_arr_sym->class_name, "Function") == 0) {
                ASTNode* pre_wrapped = sem_wrap_function_value(ass->value);
                if (pre_wrapped != NULL) {
                    ass->value = pre_wrapped;
                }
            }
        }

        if (ass->value) analyze_node(ass->value);

        AST_ArrayAccess* acc = (AST_ArrayAccess*)ass->target;
        if (acc && acc->array != NULL
            && acc->array->type == AST_MEMBER_ACCESS) {
            AST_MemberAccess* ma = (AST_MemberAccess*)acc->array;
            if (ma->object != NULL
                && ma->object->type == AST_THIS
                && current_class
                && ma->member_name) {
                for (int ci = 0; ci < current_class->member_count; ci++) {
                    MemberInfo* fmi = current_class->members[ci];
                    if (!fmi->is_method
                        && strcmp(fmi->name, ma->member_name) == 0
                        && fmi->is_const) {
                        bool in_ctor = (current_analyzing_function
                                        && current_analyzing_function->name.value
                                        && strcmp(current_analyzing_function->name.value,
                                                  current_class->name) == 0);
                        if (!in_ctor) {
                            ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                                  SEMLOC(ass->base.value),
                                  "cannot modify element of const array '%s'",
                                  ma->member_name);
                        }
                        break;
                    }
                }
            }
        }
        if (acc && acc->array != NULL
            && acc->array->type == AST_LITERAL) {
            AST_Literal* arr_lit = (AST_Literal*)acc->array;
            if (arr_lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* arr_sym = lookup_symbol_with_file(arr_lit->base.value.value,
                                                          arr_lit->base.value.file);
                if (arr_sym && (arr_sym->level_const_mask & 1u)) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(ass->base.value),
                          "cannot assign through pointer to const '%s'",
                          arr_lit->base.value.value);
                }
                if (arr_sym && arr_sym->is_const) {
                    bool in_ctor = (current_analyzing_function
                                    && current_class
                                    && current_analyzing_function->name.value
                                    && current_class->name
                                    && strcmp(current_analyzing_function->name.value,
                                              current_class->name) == 0);
                    if (!in_ctor) {
                        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                              SEMLOC(ass->base.value),
                              "cannot modify element of const array '%s'",
                              arr_lit->base.value.value);
                    }
                }
            }
        }
        DataType elemType = (acc) ? acc->elementType : DATA_ERROR;
        if (elemType == DATA_STR && ass->value != NULL) {
            DataType valType = get_expression_type(ass->value);
            if (valType == DATA_STRING) {
                AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
                if (!conv) OUT_OF_MEMORY();
                conv->base.type  = AST_STR_FROM_STRING;
                conv->base.next  = NULL;
                conv->base.value = ass->base.value;
                conv->source     = ass->value;
                ass->value       = (ASTNode*)conv;
            }
        }
        ass->elementType = elemType;

        DataType valType = get_expression_type(ass->value);
        if (elemType != DATA_ERROR && valType != DATA_ERROR && elemType != valType) {
            bool allowed = false;

            if (elemType == DATA_IDENTIFIER || valType == DATA_IDENTIFIER) {
                allowed = true;
            }

            bool t_int = (is_int_type(elemType) || is_uint_type(elemType));
            bool s_int = (is_int_type(valType)  || is_uint_type(valType));
            if (t_int && s_int) allowed = true;
            bool t_float = (is_float_type(elemType) || is_ufloat_type(elemType));
            bool s_float = (is_float_type(valType)  || is_ufloat_type(valType));
            if (t_float && s_float) allowed = true;

            if ((elemType == DATA_FLOAT || elemType == DATA_UFLOAT) && s_int) allowed = true;
            if (t_int && s_float) allowed = true;

            if ((elemType == DATA_STRING && valType == DATA_CHAR) ||
                (elemType == DATA_CHAR  && valType == DATA_STRING)) allowed = true;

            if (elemType == DATA_UINT || elemType == DATA_UFLOAT) {
                check_unsigned_assignment(elemType, ass->value,
                                          SEMLOC(ass->base.value));
            }

            if (!allowed) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(ass->base.value),
                      "array element type mismatch: expected '%s', got '%s'",
                      data_type_to_string(elemType), data_type_to_string(valType));
            }
        }
        break;
    }

    case AST_INIT_LIST: {
        AST_InitList* list = (AST_InitList*)temp;
        DataType commonType = DATA_ERROR;
        int nested_dim = 0;
        int inner_size = -1;

        for (int i = 0; i < list->count; i++) {
            analyze_node(list->elements[i]);
            ASTNode* elem = list->elements[i];
            DataType t;
            if (elem->type == AST_INIT_LIST) {
                AST_InitList* sub = (AST_InitList*)elem;
                t = sub->elemType;
                if (nested_dim == 0) {
                    nested_dim = 1;
                    inner_size = sub->count;
                    list->dim_count = sub->dim_count + 1;
                    list->dim_sizes = malloc(sizeof(int) * list->dim_count);
                    list->dim_sizes[0] = list->count;
                    memcpy(&list->dim_sizes[1], sub->dim_sizes, sizeof(int) * sub->dim_count);
                } else {
                    if (sub->count != inner_size)
                    {
                        Token list_token2 = {.line = list->base.value.line, .column = list->base.value.column};
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(list_token2),
                              "type mismatch in initializer");
                    }
                }
            } else {
                t = get_expression_type(elem);
                if (nested_dim == 0) {
                    list->dim_count = 1;
                    list->dim_sizes = malloc(sizeof(int));
                    list->dim_sizes[0] = list->count;
                }
            }
            if (t == DATA_ERROR) continue;
            if (commonType == DATA_ERROR) commonType = t;
            else if (commonType != t) {
                if (commonType == DATA_INT && t == DATA_FLOAT) commonType = DATA_FLOAT;
                else if (commonType == DATA_FLOAT && t == DATA_INT) commonType = DATA_FLOAT;
                else if (commonType == DATA_UINT && t == DATA_INT) commonType = DATA_UINT;
                else if (commonType == DATA_INT && t == DATA_UINT) commonType = DATA_UINT;
                else if (commonType == DATA_FLOAT && t == DATA_UFLOAT) commonType = DATA_UFLOAT;
                else if (commonType == DATA_UFLOAT && t == DATA_FLOAT) commonType = DATA_UFLOAT;
                else if (commonType == DATA_UINT && t == DATA_UFLOAT) commonType = DATA_UFLOAT;
                else if (commonType == DATA_UFLOAT && t == DATA_UINT) commonType = DATA_UFLOAT;
                else if (commonType != t)
                {
                    Token type_token = {.line = list->base.value.line, .column = list->base.value.column};
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                        SEMLOC(type_token),
                        "type mismatch in initializer");
                }
            }
        }
        list->elemType = commonType;
        break;
    }

    case AST_ARRAY_LENGTH: {
        AST_ArrayLength* len = (AST_ArrayLength*)temp;
        analyze_node(len->array);

        len->is_dynamic = false;

        if (len->array->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)len->array;
            Symbol* sym = lookup_symbol_with_file(lit->base.value.value,
                                                  lit->base.value.file);
            if (sym == NULL) {
                ERROR(ERR_SEM_UNDECLARED_VAR,
                      SEMLOC(lit->base.value),
                      "variable '%s' is not declared",
                      lit->base.value.value);
            } else if (sym->dim_count == 0 && sym->type != DATA_STR
                       && !sym->is_dynamic_array) {
                ERROR(ERR_SEM_NOT_ARRAY,
                      SEMLOC(lit->base.value),
                      "'.length' requires an array or str");
            } else {
                len->is_dynamic = false;
                if (sym->type == DATA_STR) {
                    len->is_dynamic = true;
                } else if (sym->is_dynamic_array) {
                    len->is_dynamic = true;
                } else if (sym->dim_count > 0 && sym->dim_sizes != NULL) {
                    for (int i = 0; i < sym->dim_count; i++) {
                        if (sym->dim_sizes[i] <= 0) {
                            len->is_dynamic = true;
                            break;
                        }
                    }
                }
                if (!len->is_dynamic && sym->dim_count > 0 && sym->dim_sizes != NULL) {
                    int total = 1;
                    for (int i = 0; i < sym->dim_count; i++)
                        total *= sym->dim_sizes[i];
                    len->total_elements = total;
                }
            }
        } else if (len->array->type == AST_ARRAY_ACCESS) {

        } else if (len->array->type == AST_VAR || len->array->type == AST_VAR_ASS) {
            AST_Var* var = (AST_Var*)len->array;
            Symbol* sym = lookup_symbol(var->name.value);
            if (sym && sym->type != DATA_STR && sym->dim_count == 0
                && !sym->is_dynamic_array) {
                ERROR(ERR_SEM_NOT_ARRAY,
                      SEMLOC(len->base.value),
                      "'.length' requires an array or str");
            }
            len->is_dynamic = false;
            if (sym && sym->is_dynamic_array) {
                len->is_dynamic = true;
            } else if (sym && sym->dim_count > 0 && sym->dim_sizes != NULL) {
                for (int i = 0; i < sym->dim_count; i++) {
                    if (sym->dim_sizes[i] <= 0) {
                        len->is_dynamic = true;
                        break;
                    }
                }
                if (!len->is_dynamic) {
                    int total = 1;
                    for (int i = 0; i < sym->dim_count; i++)
                        total *= sym->dim_sizes[i];
                    len->total_elements = total;
                }
            }
        } else if (len->array->type == AST_CAST) {
            AST_Cast* c = (AST_Cast*)len->array;
            if (c->target_dim_count > 0) {
                len->is_dynamic = false;
                int total = 1;
                for (int i = 0; i < c->target_dim_count; i++) {
                    if (c->target_dim_sizes[i] > 0) {
                        total *= c->target_dim_sizes[i];
                    } else {
                        len->is_dynamic = true;
                        break;
                    }
                }
                if (!len->is_dynamic) {
                    len->total_elements = total;
                }
            }
        } else {
            DataType expr_type = get_expression_type(len->array);
            if (expr_type != DATA_STR && expr_type != DATA_ERROR) {
            }
        }
        break;
    }

    case AST_PRINT: {
        AST_Print* print = (AST_Print*)temp;

        ASTNode** prev_ptr = &print->Parameter;
        ASTNode* param = print->Parameter;
        while (param) {
            if (param->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)param;
                if (lit->base.value.type == TOKEN_IDENTIFIER && !lit->is_call_function) {
                    Symbol* sym = lookup_symbol(lit->base.value.value);
                    if (sym && sym->dim_count > 0) {
                        AST_Literal* call = calloc(1, sizeof(AST_Literal));
                        call->base.type = AST_LITERAL;
                        call->base.next = param->next;
                        call->base.value.type = TOKEN_IDENTIFIER;
                        call->base.value.value = strdup("_arrayToString");
                        call->dataType = DATA_STRING;
                        call->internal_name = strdup("_arrayToString");
                        call->is_call_function = true;

                        AST_Literal* arr_node = calloc(1, sizeof(AST_Literal));
                        arr_node->base.type = AST_LITERAL;
                        arr_node->base.next = NULL;
                        arr_node->base.value = lit->base.value;
                        arr_node->dataType = sym->type;
                        arr_node->internal_name = strdup(sym->internal_name);
                        arr_node->is_call_function = false;

                        call->call_function_Parameter = (ASTNode*)arr_node;
                        call->call_function_ParameterCount = 1;

                        *prev_ptr = (ASTNode*)call;
                        prev_ptr = &call->base.next;
                        param = call->base.next;
                        continue;
                    }
                }
            }
            else if (param->type == AST_ARRAY_ACCESS) {
                AST_ArrayAccess* acc = (AST_ArrayAccess*)param;
                if (acc->dim_count > 0) {
                    AST_Literal* call = calloc(1, sizeof(AST_Literal));
                    call->base.type = AST_LITERAL;
                    call->base.next = param->next;
                    call->base.value.type = TOKEN_IDENTIFIER;
                    call->base.value.value = strdup("_arrayToString");
                    call->dataType = DATA_STRING;
                    call->internal_name = strdup("_arrayToString");
                    call->is_call_function = true;

                    param->next = NULL;
                    call->call_function_Parameter = param;
                    call->call_function_ParameterCount = 1;

                    *prev_ptr = (ASTNode*)call;
                    prev_ptr = &call->base.next;
                    param = call->base.next;
                    continue;
                }
            }
            prev_ptr = &param->next;
            param = param->next;
        }

        analyze_node_list(print->Parameter);
        break;
    }

    case AST_INPUT: {
        AST_Input* inp = (AST_Input*)temp;
        analyze_node(inp->format_string);
        for (int i = 0; i < inp->arg_count; i++) {
            analyze_node(inp->args[i]);
            AST_Literal* var = (AST_Literal*)inp->args[i];
            Symbol* sym = lookup_symbol_with_file(var->base.value.value,
                                                  var->base.value.file);
            if (!sym) {
                ERROR(ERR_SEM_UNDECLARED_VAR,
                      SEMLOC(var->base.value),
                      "variable '%s' is not declared",
                      var->base.value.value);
            }
            var->internal_name = sym ? strdup(sym->internal_name) : NULL;
            var->dataType = sym ? sym->type : DATA_ERROR;
        }
        break;
    }

    case AST_VAR: {
        AST_Var* var = (AST_Var*)temp;
        if (var->Value) analyze_node(var->Value);

        if (var->Value != NULL
            && var->dataType == DATA_IDENTIFIER
            && var->internal_name != NULL
            && strcmp(var->internal_name, "Function") == 0
            && var->Value->type == AST_MEMBER_ACCESS) {
            AST_MemberAccess* rhs_ma = (AST_MemberAccess*)var->Value;
            if (rhs_ma->is_method && rhs_ma->member_name != NULL) {
                ASTNode* wrapped = sem_wrap_function_value(var->Value);
                if (wrapped != NULL) {
                    if (wrapped->type == AST_NEW_INSTANCE) {
                        AST_NewInstance* fn_ni = (AST_NewInstance*)wrapped;
                        if (fn_ni->arguments != NULL
                            && fn_ni->arguments->type == AST_ADDRESS_OF) {
                            AST_AddressOf* fn_addr = (AST_AddressOf*)fn_ni->arguments;
                            if (fn_addr->target_func_sig != NULL) {
                                var->func_ptr_sig = deep_copy_func_sig(fn_addr->target_func_sig);
                            }
                        }
                    }
                    var->Value = wrapped;
                    analyze_node(var->Value);
                }
            }
        } else if (var->Value != NULL
            && var->dataType == DATA_IDENTIFIER
            && var->internal_name != NULL
            && strcmp(var->internal_name, "Function") == 0
            && var->Value->type == AST_LITERAL) {
                AST_Literal* rhs_lit = (AST_Literal*)var->Value;
                if (!rhs_lit->is_call_function
                    && rhs_lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* rhs_sym = lookup_symbol(rhs_lit->base.value.value);
                if (rhs_sym != NULL && rhs_sym->is_function
                    && rhs_sym->func_sig != NULL) {
                    bool rhs_is_closure =
                        (rhs_lit->closure_func != NULL
                         && rhs_lit->closure_func->is_closure
                         && rhs_lit->closure_func->upvalue_count > 0);
                    if (rhs_is_closure) {
                        var->func_ptr_sig = deep_copy_func_sig(rhs_sym->func_sig);
                        var->is_closure_ptr = true;
                    } else {
                        ASTNode* wrapped = sem_wrap_function_value(var->Value);
                        if (wrapped != NULL) {
                            var->func_ptr_sig = deep_copy_func_sig(rhs_sym->func_sig);
                            var->Value = wrapped;
                            analyze_node(var->Value);
                        }
                    }
                } else if (rhs_sym != NULL
                           && rhs_sym->class_name != NULL
                           && strcmp(rhs_sym->class_name, "Function") == 0
                           && rhs_sym->func_sig != NULL) {
                    var->func_ptr_sig = deep_copy_func_sig(rhs_sym->func_sig);
                }
            } else if (rhs_lit->is_call_function) {
                if (rhs_lit->returned_func_sig != NULL) {
                    var->func_ptr_sig = deep_copy_func_sig(rhs_lit->returned_func_sig);

                    bool rhs_is_function_object =
                        (rhs_lit->call_result_class_name != NULL
                         && strcmp(rhs_lit->call_result_class_name,
                                   "Function") == 0);

                    if (!rhs_is_function_object) {
                        ClassEntry* fn_cls = find_class("Function");
                        if (fn_cls != NULL && fn_cls->total_size >= 12) {
                            AST_NewInstance* ni = calloc(1, sizeof(AST_NewInstance));
                            if (!ni) OUT_OF_MEMORY();
                            ni->base.type = AST_NEW_INSTANCE;
                            ni->base.next = NULL;
                            ni->base.value.type = var->Value->value.type;
                            ni->base.value.line = var->Value->value.line;
                            ni->base.value.column = var->Value->value.column;
                            ni->base.value.file = var->Value->value.file;
                            ni->base.value.value = var->Value->value.value
                                ? strdup(var->Value->value.value)
                                : NULL;
                            ni->class_name = strdup("Function");
                            ni->arguments = var->Value;
                            ni->instance_size = fn_cls->total_size;
                            ni->class_id = fn_cls->class_id;
                            ni->constructor_arg_count = 1;
                            ni->has_constructor = (fn_cls->constructor != NULL);
                            ni->constructor_name = fn_cls->constructor
                                ? strdup(fn_cls->constructor->internal_name)
                                : NULL;
                            if (fn_cls->vtable_size > 0) {
                                ni->vtable_label = class_vtable_label(fn_cls->name);
                            }
                            ni->args_pre_analyzed = true;
                            var->Value = (ASTNode*)ni;
                        }
                    } else if (rhs_lit->returned_func_sig->is_closure_sig) {
                        var->is_closure_ptr = true;
                    }
                }
            }
        }

        if (var->Value && var->Value->type == AST_LITERAL) {
            AST_Literal* rhs = (AST_Literal*)var->Value;
            if (rhs->is_call_function && !rhs->is_closure_call) {
                const char* target_name = rhs->internal_name;
                if (!target_name) target_name = rhs->base.value.value;
                if (target_name) {
                    for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                        if (strcmp(fe->name, target_name) != 0) continue;
                        AST_Function* called = fe->func;
                        if (called && called->Code) {
                            ASTNode* scan = called->Code;
                            while (scan) {
                                if (scan->type == AST_FUNCTION
                                    && ((AST_Function*)scan)->is_closure) {
                                    rhs->is_closure_call = true;
                                    rhs->closure_func = (AST_Function*)scan;
                                    break;
                                    }
                                scan = scan->next;
                            }
                        }
                        break;
                    }
                }
            }
        }

        Symbol* pre_i = (current_analyzing_function == NULL
                        && current_class == NULL)
                       ? lookup_symbol(var->name.value) : NULL;
        char* internal = (pre_i != NULL && pre_i->is_global_predecl
                          && pre_i->internal_name != NULL)
                         ? strdup(pre_i->internal_name)
                         : generate_internal_name();
        if (var->internal_name != NULL
            && (strncmp(var->internal_name, "__mt_", 5) == 0
                || strcmp(var->internal_name, "_outer") == 0)) {
            free(internal);
            internal = strdup(var->internal_name);
        }

        char mangled_key[512] = "";
        if (var->is_static
            && current_analyzing_function == NULL
            && current_class == NULL
            && var->base.value.file != NULL
            && var->name.value != NULL) {
            make_static_mangled_name(var->base.value.file, var->name.value,
                                     mangled_key, sizeof(mangled_key));
            if (mangled_key[0] != '\0') {
                free(internal);
                internal = strdup(mangled_key);
            }
        }

        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            DataType resolved = resolve_type_alias(var->internal_name);
            if (resolved != DATA_ERROR) {
                var->dataType = resolved;
            }
        }
        Symbol* sym = create_symbol(var->name, var->dataType,
                                    (mangled_key[0] != '\0') ? mangled_key : var->name.value,
                                    internal);
        sym->ptr_depth = var->ptr_depth;
        sym->is_unsafe = var->is_unsafe;
        if (var->is_closure_ptr) {
            sym->is_closure_ptr = true;
            sym->is_closure_holder = true;
        }
        bool in_func = (current_analyzing_function != NULL);
        bool in_cls  = (current_class != NULL);
        if (var->is_static && in_func && !in_cls) {
            sym->is_static_local = true;
            sym->has_static_init = (var->Value != NULL);
            int len = strlen(current_analyzing_function->internal_name)
                    + strlen(var->name.value) + 20;
            char* global_name = malloc(len);
            snprintf(global_name, len, "%s_%s_static",
                     current_analyzing_function->internal_name,
                     var->name.value);
            free(sym->internal_name);
            sym->internal_name = global_name;

            var->is_static_local = true;
            var->static_storage_name = strdup(global_name);
            var->static_guard_name = malloc(strlen(global_name) + 8);
            snprintf(var->static_guard_name, strlen(global_name) + 8,
                     "%s_guard", global_name);
            if (var->Value && var->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)var->Value;
                TokenType t = lit->base.value.type;
                if (t == TOKEN_NUMBER || t == TOKEN_FLOAT_LITERAL ||
                    t == TOKEN_STRING_LITERAL || t == TOKEN_CHAR_LITERAL ||
                    t == TOKEN_BOOL_LITERAL) {
                    var->has_static_init = true;
                    var->static_init_value = strdup(lit->base.value.value);
                }
            }
            free(internal);
            internal = NULL;
        }

        if (strcmp(var->name.value, "this") == 0 && current_class_name != NULL) {
            sym->class_name = strdup(current_class_name);
        }
        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            ClassEntry* cls = find_class(var->internal_name);
            if (!cls && current_class != NULL) {
                char full[256];
                snprintf(full, sizeof(full), "%s_%s",
                         current_class->name, var->internal_name);
                ClassEntry* inner_cls = find_class(full);
                if (inner_cls) {
                    free(var->internal_name);
                    var->internal_name = strdup(full);
                    cls = inner_cls;
                }
            }
            if (cls && cls->is_generic) {
                DataType* targs = NULL;
                char** targ_names = NULL;
                int targ_count = 0;

                if (var->generic_type_args != NULL && var->generic_type_arg_count > 0) {
                    targs = var->generic_type_args;
                    targ_names = var->generic_type_arg_names;
                    targ_count = var->generic_type_arg_count;
                } else if (var->Value != NULL) {
                    if (var->Value->type == AST_NEW_INSTANCE) {
                        AST_NewInstance* ni = (AST_NewInstance*)var->Value;
                        targs = ni->generic_type_args;
                        targ_names = ni->generic_type_arg_names;
                        targ_count = ni->generic_type_arg_count;
                    }
                }

                if (targ_count > 0) {
                    ClassEntry* inst_cls = instantiate_generic_class(
                        cls, targs, targ_names, targ_count);
                    if (inst_cls) {
                        free(var->internal_name);
                        var->internal_name = strdup(inst_cls->name);

                        if (sym->class_name) free(sym->class_name);
                        sym->class_name = strdup(inst_cls->name);

                        var->total_bytes = 4;
                        sym->total_bytes = 4;
                        sym->is_value_type = false;

                        if (var->Value != NULL
                            && var->Value->type == AST_NEW_INSTANCE) {
                            AST_NewInstance* ni = (AST_NewInstance*)var->Value;
                            if (inst_cls->constructor != NULL) {
                                if (ni->constructor_name != NULL)
                                    free(ni->constructor_name);
                                ni->constructor_name =
                                    strdup(inst_cls->constructor->internal_name);
                                ni->has_constructor = true;
                            }
                            ni->class_id = inst_cls->class_id;
                            ni->instance_size = inst_cls->total_size;
                            if (ni->vtable_label != NULL) {
                                free(ni->vtable_label);
                                ni->vtable_label = NULL;
                            }
                            if (inst_cls->vtable_size > 0) {
                                ni->vtable_label = class_vtable_label(inst_cls->name);
                            }
                        }
                    }
                }
            } else if (cls) {
                if (sym->class_name) free(sym->class_name);
                sym->class_name = strdup(var->internal_name);
                var->total_bytes = 4;
                sym->total_bytes = 4;
                sym->is_value_type = false;
            } else {
                bool skip_check = false;
                if (var->name.value != NULL
                    && strcmp(var->name.value, "this") == 0) {
                    skip_check = true;
                }
                if (var->internal_name != NULL
                    && var->internal_name[0] == '_') {
                    skip_check = true;
                }
                if (var->is_field_ref) {
                    skip_check = true;
                }
                if (var->func_ptr_sig != NULL || var->ptr_array_info != NULL) {
                    skip_check = true;
                }
                bool is_type_param = false;
                if (!skip_check && current_class && current_class->is_generic
                    && current_class->generic_ast) {
                    for (int tpi = 0;
                         tpi < current_class->generic_ast->type_param_count; tpi++) {
                        if (var->internal_name != NULL
                            && strcmp(var->internal_name,
                                      current_class->generic_ast->type_params[tpi].name) == 0) {
                            is_type_param = true;
                            break;
                        }
                    }
                }
                if (!skip_check && !is_type_param && current_analyzing_function
                    && current_analyzing_function->is_generic
                    && var->internal_name != NULL) {
                    for (int tpi = 0;
                         tpi < current_analyzing_function->type_param_count; tpi++) {
                        if (strcmp(var->internal_name,
                                   current_analyzing_function->type_params[tpi].name) == 0) {
                            is_type_param = true;
                            break;
                        }
                    }
                }
                if (!skip_check && !is_type_param) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(var->name),
                          "unknown type '%s' for variable '%s'",
                          var->internal_name ? var->internal_name : "(null)",
                          var->name.value ? var->name.value : "(null)");
                }
            }
        }

        if (var->dim_count > 0 && var->dim_sizes != NULL) {
            int elem_size = S_get_type_size(var->dataType);
            int total = 1;
            for (int i = 0; i < var->dim_count; i++) {
                if (var->dim_sizes[i] > 0) total *= var->dim_sizes[i];
            }
            var->total_bytes = elem_size * total;
            sym->total_bytes = var->total_bytes;
            sym->is_value_type = (var->total_bytes > 4);
            /* an array of function pointers is always a value blob on the
               stack, even when it holds a single 4-byte slot */
            if (var->func_ptr_sig != NULL && var->dim_count > 0) {
                sym->is_value_type = true;
            }
        } else if (sym->class_name != NULL) {
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
        } else if (var->ptr_depth > 0) {
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
        } else {
            var->total_bytes = S_get_type_size(var->dataType);
            sym->total_bytes = var->total_bytes;
            if (var->total_bytes > 4) {
                sym->is_value_type = true;
            }
        }
        sym->type_param_index = var->type_param_index;
        sym->is_const = var->is_const;
        sym->level_const_mask = var->level_const_mask;
        if (var->is_const && var->Value) {
            bool is_int_const =
                (var->dataType == DATA_INT  || var->dataType == DATA_UINT ||
                 var->dataType == DATA_CHAR || var->dataType == DATA_BOOL ||
                 var->dataType == DATA_INT8 || var->dataType == DATA_INT16 ||
                 var->dataType == DATA_INT32 || var->dataType == DATA_INT64 ||
                 var->dataType == DATA_UINT8 || var->dataType == DATA_UINT16 ||
                 var->dataType == DATA_UINT32 || var->dataType == DATA_UINT64);
            if (is_int_const) {
                int64_t cval = 0;
                bool of = false;
                if (eval_constant_expr(var->Value, &cval, &of)) {
                    if (of) {
                        ERROR(ERR_SEM_CONST_OVERFLOW,
                              SEMLOC(var->name),
                              "'const' variable '%s' overflows 32-bit integer range",
                              var->name.value);
                    } else {
                        char cbuf[32];
                        snprintf(cbuf, sizeof(cbuf), "%lld", (long long)cval);
                        sym->const_value_str = strdup(cbuf);

                        if (var->Value->type != AST_LITERAL) {
                            AST_Literal* lit = calloc(1, sizeof(AST_Literal));
                            if (!lit) OUT_OF_MEMORY();
                            lit->base.type = AST_LITERAL;
                            lit->base.next = NULL;
                            lit->base.value.type = TOKEN_NUMBER;
                            lit->base.value.value = strdup(cbuf);
                            lit->base.value.line = var->name.line;
                            lit->base.value.column = var->name.column;
                            lit->dataType = sym->type;
                            lit->internal_name = NULL;
                            lit->is_call_function = false;
                            AST_Free(var->Value);
                            var->Value = (ASTNode*)lit;
                        }
                    }
                } else {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(var->name),
                          "'const' variable '%s' must be initialized with "
                          "a compile-time constant expression",
                          var->name.value);
                }
            }
            bool is_float_const = (var->dataType == DATA_FLOAT  || var->dataType == DATA_UFLOAT ||
                                     var->dataType == DATA_FLOAT8 || var->dataType == DATA_FLOAT16 ||
                                     var->dataType == DATA_FLOAT32 || var->dataType == DATA_FLOAT64 ||
                                     var->dataType == DATA_FLOAT128 ||
                                     var->dataType == DATA_UFLOAT8 || var->dataType == DATA_UFLOAT16 ||
                                     var->dataType == DATA_UFLOAT32 || var->dataType == DATA_UFLOAT64 ||
                                     var->dataType == DATA_UFLOAT128);
            if (is_float_const) {
                double fval = 0.0;
                if (eval_constant_expr_as_double(var->Value, &fval)) {
                    char fbuf[64];
                    snprintf(fbuf, sizeof(fbuf), "%.9g", fval);
                    sym->const_value_str = strdup(fbuf);
                } else {
                    ERROR(ERR_SEM_INVALID_INDEX,
                          SEMLOC(var->name),
                          "'const' float variable '%s' must be initialized with "
                          "a compile-time constant expression",
                          var->name.value);
                }
            }
        }

        {
            Symbol* pre_g = (current_analyzing_function == NULL
                            && current_class == NULL)
                           ? lookup_symbol(var->name.value) : NULL;
            if (pre_g != NULL && pre_g != sym && pre_g->is_global_predecl) {
                pre_g->is_global_predecl = false;
                pre_g->type = sym->type;
                pre_g->total_bytes = sym->total_bytes;
                pre_g->is_value_type = sym->is_value_type;
                pre_g->ptr_depth = sym->ptr_depth;
                pre_g->dim_count = sym->dim_count;
                pre_g->dim_sizes = sym->dim_sizes;
                pre_g->is_dynamic_array = sym->is_dynamic_array;
                pre_g->is_field = sym->is_field;
                pre_g->is_static = sym->is_static;
                pre_g->is_const = sym->is_const;
                if (sym->class_name) {
                    if (pre_g->class_name) free(pre_g->class_name);
                    pre_g->class_name = strdup(sym->class_name);
                }
                if (sym->const_value_str) {
                    if (pre_g->const_value_str) free(pre_g->const_value_str);
                    pre_g->const_value_str = strdup(sym->const_value_str);
                }
                free_symbol(sym);
                sym = pre_g;
            } else {
                insert_symbol(sym);
            }
        }
        symbol_copy_generic_args(sym, var);

        if (var->Value != NULL && var->Value->type == AST_NEW_INSTANCE) {
            AST_NewInstance* vni = (AST_NewInstance*)var->Value;
            if (vni->mr_type != NULL) {
                sym->mr_type = vni->mr_type;
            }
        }

        if (var->Value != NULL && var->Value->type != AST_NULL) {
            int value_pd = get_expr_ptr_depth(var->Value);
            if (value_pd > 0) {
                bool is_function_holder =
                    (var->dataType == DATA_IDENTIFIER
                     && var->internal_name != NULL
                     && strcmp(var->internal_name, "Function") == 0);
                if (!is_function_holder) {
                    sym->ptr_depth = value_pd;
                    var->ptr_depth = value_pd;
                }
            }
        }

        if (var->dataType == DATA_IDENTIFIER && var->Value != NULL
            && var->Value->type == AST_LITERAL
            && ((AST_Literal*)var->Value)->is_call_function) {
            DataType vdt = get_expression_type(var->Value);
            if (vdt != DATA_ERROR && vdt != DATA_IDENTIFIER
                && vdt != DATA_VOID) {
                if (var->ptr_depth > 0) {
                    var->dataType = DATA_INT;
                    var->total_bytes = 4;
                    sym->type = vdt;
                    sym->total_bytes = 4;
                    sym->is_value_type = false;
                } else {
                    var->dataType = vdt;
                    sym->type = vdt;
                    var->total_bytes = S_get_type_size(vdt);
                    sym->total_bytes = var->total_bytes;
                    if (var->total_bytes > 4)
                        sym->is_value_type = true;
                }
            }
        }

        if (var->Value && var->Value->type == AST_LITERAL) {
            AST_Literal* rhs = (AST_Literal*)var->Value;
            if (rhs->is_call_function && rhs->is_closure_call) {
                sym->is_closure_holder = true;
                sym->is_closure_ptr = true;
            }
            if (rhs->is_call_function && !rhs->is_closure_call) {
                const char* cn = rhs->base.value.value;
                if (cn != NULL) {
                    Symbol* csym = lookup_symbol(cn);
                    if (csym && csym->func_sig
                        && csym->func_sig->returns_closure) {
                        sym->is_closure_holder = true;
                        sym->is_closure_ptr = true;
                    }
                }
            }
            if (!rhs->is_call_function
                && rhs->closure_func != NULL
                && rhs->closure_func->is_closure
                && rhs->closure_func->upvalue_count > 0) {
                sym->is_closure_holder = true;
                sym->is_closure_ptr = true;
            }
        }

        if (var->Value && var->Value->type == AST_RETURN_EXTRACT) {
            AST_ReturnExtract* ex = (AST_ReturnExtract*)var->Value;
            if (ex->extracted_func_sig && ex->extracted_func_sig->returns_closure) {
                sym->is_closure_holder = true;
                sym->is_closure_ptr = true;
                if (var->func_ptr_sig) var->func_ptr_sig->returns_closure = true;
            }
        }

        if (var->func_ptr_sig) {
            sym->func_sig = deep_copy_func_sig(var->func_ptr_sig);
        }

        if (var->Value && var->Value->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)var->Value;
            if (lit->is_call_function && lit->multi_return_type) {
                if (!sym->func_sig) {
                    sym->func_sig = calloc(1, sizeof(FuncSignature));
                    sym->func_sig->return_type = DATA_VOID;
                }
                if (!sym->func_sig->multi_return) {
                    sym->func_sig->multi_return =
                        deep_copy_multi_return(lit->multi_return_type);
                }
            }
        }

        if (sym->func_sig != NULL
            && var->Value != NULL
            && var->Value->type == AST_LITERAL
            && !((AST_Literal*)var->Value)->is_call_function) {
            AST_Literal* rhs_lit = (AST_Literal*)var->Value;
            Symbol* rhs_sym = lookup_symbol(rhs_lit->base.value.value);
            bool rhs_is_closure_lit =
                (rhs_lit->closure_func != NULL
                 && rhs_lit->closure_func->is_closure
                 && rhs_lit->closure_func->upvalue_count > 0);
            if (rhs_is_closure_lit) {
                goto skip_func_ptr_wrap_var;
            }
            if (rhs_sym && rhs_sym->is_function && rhs_sym->func_sig) {
                FuncSignature* lhs_sig = sym->func_sig;
                FuncSignature* rhs_sig = rhs_sym->func_sig;
                if (!lhs_sig->return_pointee_const
                    && rhs_sig->return_pointee_const) {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(var->name),
                          "function pointer return type "
                          "cannot drop 'const'");
                }
                if (lhs_sig->param_count == rhs_sig->param_count) {
                    for (int fi = 0; fi < lhs_sig->param_count; fi++) {
                        bool lhs_c = lhs_sig->param_pointee_const
                                     ? lhs_sig->param_pointee_const[fi] : false;
                        bool rhs_c = rhs_sig->param_pointee_const
                                     ? rhs_sig->param_pointee_const[fi] : false;
                        if (!lhs_c && rhs_c) {
                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                  SEMLOC(var->name),
                                  "function pointer parameter %d "
                                  "cannot drop 'const'", fi + 1);
                            break;
                        }
                    }
                }

                AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                if (!addr) OUT_OF_MEMORY();
                addr->base.type = AST_ADDRESS_OF;
                addr->base.next = NULL;
                addr->base.value = rhs_lit->base.value;
                addr->operand = var->Value;
                addr->resultType = DATA_INT;
                addr->ptr_depth = 1;
                addr->target_func_sig = malloc(sizeof(FuncSignature));
                memset(addr->target_func_sig, 0, sizeof(FuncSignature));
                addr->target_func_sig->return_type = rhs_sym->func_sig->return_type;
                addr->target_func_sig->return_pointee_const =
                    rhs_sym->func_sig->return_pointee_const;
                addr->target_func_sig->param_count = rhs_sym->func_sig->param_count;
                addr->target_func_sig->param_types = malloc(
                    sizeof(DataType) * rhs_sym->func_sig->param_count);
                memcpy(addr->target_func_sig->param_types,
                       rhs_sym->func_sig->param_types,
                       sizeof(DataType) * rhs_sym->func_sig->param_count);
                if (rhs_sym->func_sig->param_pointee_const) {
                    addr->target_func_sig->param_pointee_const = malloc(
                        sizeof(bool) * rhs_sym->func_sig->param_count);
                    memcpy(addr->target_func_sig->param_pointee_const,
                           rhs_sym->func_sig->param_pointee_const,
                           sizeof(bool) * rhs_sym->func_sig->param_count);
                }
                var->Value = (ASTNode*)addr;
            }
            skip_func_ptr_wrap_var:;
        }


        if (var->ptr_array_info) {
            sym->ptr_array_info = malloc(sizeof(PtrArrayInfo));
            sym->ptr_array_info->element_type = var->ptr_array_info->element_type;
            sym->ptr_array_info->dim_count = var->ptr_array_info->dim_count;
            sym->ptr_array_info->dim_sizes = malloc(sizeof(int) * var->ptr_array_info->dim_count);
            memcpy(sym->ptr_array_info->dim_sizes, var->ptr_array_info->dim_sizes,
                   sizeof(int) * var->ptr_array_info->dim_count);
        }

        if (internal != NULL) {
            if (in_param_list && var->dataType == DATA_IDENTIFIER
                && var->internal_name != NULL
                && var->internal_name[0] != '_') {
                if (var->static_storage_name) free(var->static_storage_name);
                var->static_storage_name = strdup(var->internal_name);
            }
            var->internal_name = strdup(internal);
            free(internal);
            if (sym->func_sig && var->Value != NULL
                && var->Value->type == AST_MEMBER_ACCESS
                && ((AST_MemberAccess*)var->Value)->is_method_ptr) {
                AST_MemberAccess* ma = (AST_MemberAccess*)var->Value;
                FuncSignature* sig = sym->func_sig;
                if (sig->param_count != ma->method_param_count) {
                    ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                          SEMLOC(var->name),
                          "function pointer expects %d argument(s), "
                          "but method '%s' has %d",
                          sig->param_count, ma->method_internal,
                          ma->method_param_count);
                } else {
                    for (int i = 0; i < sig->param_count; i++) {
                        if (!types_compatible(sig->param_types[i],
                                              ma->method_param_types[i])) {
                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                  SEMLOC(var->name),
                                  "parameter %d type mismatch: "
                                  "expected '%s', got '%s'",
                                  i + 1,
                                  data_type_to_string(sig->param_types[i]),
                                  data_type_to_string(ma->method_param_types[i]));
                            break;
                        }
                    }
                }
                DataType expected_ret = sig->return_type;
                DataType actual_ret = ma->member_return_type;
                if (expected_ret != DATA_VOID && actual_ret != DATA_VOID &&
                    !types_compatible(expected_ret, actual_ret)) {
                    ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                          SEMLOC(var->name),
                          "return type mismatch: expected '%s', got '%s'",
                          data_type_to_string(expected_ret),
                          data_type_to_string(actual_ret));
                }
                sym->is_method_ptr = true;
            }
        }

        if (var->Value && var->Value->type == AST_NEW_INSTANCE) {
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
            sym->is_dynamic_array = false;
        }

        if (var->Value != NULL && is_unsafe_alloc_call(var->Value)) {
            if (!var->is_unsafe) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(var->base.value),
                      "unsafe allocation requires 'unsafe' pointer, "
                      "got non-unsafe pointer");
            }
        }

        if (var->Value != NULL) {
            if (var->dataType == DATA_STR) {
                DataType expr_type = get_expression_type(var->Value);
                if (expr_type == DATA_STRING) {
                    AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
                    conv->base.type = AST_STR_FROM_STRING;
                    conv->base.next = NULL;
                    conv->base.value = var->base.value;
                    conv->source = var->Value;
                    var->Value = (ASTNode*)conv;
                }
            }

            check_unsigned_assignment(var->dataType, var->Value,
                                      SEMLOC(var->name));

            if (var->Value->type == AST_LITERAL
                && ((AST_Literal*)var->Value)->is_call_function
                && ((AST_Literal*)var->Value)->multi_return_type != NULL
                && var->dataType == DATA_INT) {
                goto skip_type_check_var;
            }

            if (sym->ptr_depth > 0) {
                int rhs_level_mask = 0;
                int rhs_ptr_depth  = 0;
                bool rhs_is_addr_of_const = false;

                ASTNode* val = var->Value;
                if (val->type == AST_ADDRESS_OF) {
                    AST_AddressOf* ao = (AST_AddressOf*)val;
                    rhs_level_mask = ao->level_const_mask;
                    rhs_ptr_depth  = ao->ptr_depth;
                    if (ao->operand->type == AST_LITERAL) {
                        AST_Literal* al = (AST_Literal*)ao->operand;
                        if (al->base.value.type == TOKEN_IDENTIFIER) {
                            Symbol* as = lookup_symbol(al->base.value.value);
                            if (as && as->is_const) {
                                rhs_level_mask |= 1u;
                                rhs_is_addr_of_const = true;
                            }
                        }
                    }
                } else if (val->type == AST_LITERAL) {
                    AST_Literal* rl = (AST_Literal*)val;
                    if (rl->base.value.type == TOKEN_IDENTIFIER) {
                        Symbol* rs = lookup_symbol(rl->base.value.value);
                        if (rs) {
                            rhs_level_mask = rs->level_const_mask;
                            rhs_ptr_depth  = rs->ptr_depth;
                            if (rs->is_const && rs->dim_count > 0)
                                rhs_level_mask |= 1u;
                        }
                    }
                }

                int max_lvl = sym->ptr_depth < rhs_ptr_depth
                              ? sym->ptr_depth : rhs_ptr_depth;
                bool violation = false;
                int  viol_level = 0;
                for (int lvl = 0; lvl < max_lvl; lvl++) {
                    if (!(sym->level_const_mask & (1u << lvl)) &&
                        (rhs_level_mask & (1u << lvl))) {
                        violation = true;
                        viol_level = lvl;
                        break;
                    }
                }
                if (violation) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(var->name),
                          "cannot assign 'const' pointer at level %d to non-const '%s'",
                          viol_level + 1, var->name.value);
                } else if (rhs_is_addr_of_const
                           && !(sym->level_const_mask & 1u)) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(var->name),
                          "cannot assign address of 'const' to non-const '%s'",
                          var->name.value);
                }
                /* 指针到泛型实例的变型检查（这条路径会跳过下面的类型检查） */
                if (var->dataType == DATA_IDENTIFIER) {
                    Symbol* pdecl = lookup_symbol(var->name.value);
                    const char* pdecl_cls = (pdecl && pdecl->class_name)
                                            ? pdecl->class_name : NULL;
                    const char* prhs_cls =
                        infer_static_class_name(var->Value);
                    if (pdecl_cls != NULL && prhs_cls != NULL
                        && !generic_variance_assign_ok(pdecl_cls, prhs_cls)) {
                        ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                              SEMLOC(var->name),
                              "cannot convert '%s' to '%s': generic arguments are not compatible under variance",
                              prhs_cls, pdecl_cls);
                    }
                }
                goto skip_type_check_var;
            }

            DataType expr_type = get_expression_type(var->Value);

            if (var->dataType == DATA_STRING && expr_type == DATA_STR) {
                AST_Literal* call = calloc(1, sizeof(AST_Literal));
                call->base.type = AST_LITERAL;
                call->base.next = NULL;
                call->base.value.type = TOKEN_IDENTIFIER;
                call->base.value.value = strdup("_str_to_string");
                call->dataType = DATA_STRING;
                call->internal_name = strdup("_str_to_string");
                call->is_call_function = true;
                call->call_function_Parameter = var->Value;
                call->call_function_ParameterCount = 1;
                var->Value = (ASTNode*)call;
                expr_type = DATA_STRING;
            }

            if (expr_type != var->dataType && expr_type != DATA_ERROR) {
                if (!types_compatible(var->dataType, expr_type)) {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(var->name),
                          "type mismatch: expected '%s', got '%s'",
                          data_type_to_string(var->dataType),
                          data_type_to_string(expr_type));
                }
            }

            /* 泛型变型检查：两边都是类类型时，按 +T/-T 判定参数兼容性 */
            if (var->dataType == DATA_IDENTIFIER
                && (expr_type == DATA_IDENTIFIER || var->ptr_depth > 0)) {
                Symbol* decl_sym = lookup_symbol(var->name.value);
                const char* decl_cls = (decl_sym && decl_sym->class_name)
                                      ? decl_sym->class_name : NULL;
                const char* rhs_cls = infer_static_class_name(var->Value);
                if (decl_cls != NULL && rhs_cls != NULL
                    && !generic_variance_assign_ok(decl_cls, rhs_cls)) {
                    ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                          SEMLOC(var->name),
                          "cannot convert '%s' to '%s': generic arguments are not compatible under variance",
                          rhs_cls, decl_cls);
                }
            }
            }

        skip_type_check_var:

        if (var->dim_count > 0 && var->Value
            && var->Value->type == AST_LITERAL
            && ((AST_Literal*)var->Value)->base.value.type == TOKEN_IDENTIFIER
            && !((AST_Literal*)var->Value)->is_call_function) {
            AST_Literal* alias_lit = (AST_Literal*)var->Value;
            Symbol* alias_src = lookup_symbol_with_file(
                alias_lit->base.value.value, alias_lit->base.value.file);
            if (alias_src && alias_src->dim_count > 0
                && alias_src->dim_sizes != NULL
                && !alias_src->is_dynamic_array) {
                bool all_static = true;
                for (int ai = 0; ai < alias_src->dim_count; ai++) {
                    if (alias_src->dim_sizes[ai] <= 0) { all_static = false; break; }
                }
                if (all_static) {
                    sym->is_dynamic_array = false;
                    sym->dim_count = alias_src->dim_count;
                    sym->dim_sizes = malloc(sizeof(int) * alias_src->dim_count);
                    if (sym->dim_sizes)
                        memcpy(sym->dim_sizes, alias_src->dim_sizes,
                               sizeof(int) * alias_src->dim_count);
                    var->dim_count = alias_src->dim_count;
                    var->dim_sizes = malloc(sizeof(int) * alias_src->dim_count);
                    if (var->dim_sizes)
                        memcpy(var->dim_sizes, alias_src->dim_sizes,
                               sizeof(int) * alias_src->dim_count);
                }
            }
        }

        if (var->Value && var->Value->type == AST_INIT_LIST) {
            AST_InitList* list = (AST_InitList*)var->Value;
            sym->dim_count = list->dim_count;
            sym->dim_sizes = malloc(sizeof(int) * list->dim_count);
            if (sym->dim_sizes && list->dim_sizes)
                memcpy(sym->dim_sizes, list->dim_sizes, sizeof(int) * list->dim_count);

            var->dim_count = list->dim_count;
            var->dim_sizes = malloc(sizeof(int) * list->dim_count);
            if (var->dim_sizes && list->dim_sizes)
                memcpy(var->dim_sizes, list->dim_sizes, sizeof(int) * list->dim_count);
        }
        else if (var->Value && var->Value->type == AST_NEW_ARRAY) {
            AST_NewArray* narr = (AST_NewArray*)var->Value;
            sym->dim_count = narr->dim_count;
            sym->dim_sizes = malloc(sizeof(int) * narr->dim_count);
            memcpy(sym->dim_sizes, narr->dim_sizes, sizeof(int) * narr->dim_count);
            var->dim_count = narr->dim_count;
            var->dim_sizes = malloc(sizeof(int) * narr->dim_count);
            memcpy(var->dim_sizes, narr->dim_sizes, sizeof(int) * narr->dim_count);
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
            sym->is_dynamic_array = true;
        }
        if (sym->is_field) {
            var->is_field_ref = true;
            var->field_offset = sym->field_offset;
        } else {
            var->is_field_ref = false;
            var->field_offset = 0;
        }
        break;
    }

    case AST_UNINIT_VAR: {
        AST_Var* var = (AST_Var*)temp;
        if (in_param_list && current_analyzing_function
            && current_analyzing_function->is_generic
            && var->dataType == DATA_IDENTIFIER
            && var->internal_name != NULL) {
            for (int i = 0; i < current_analyzing_function->type_param_count; i++) {
                if (strcmp(var->internal_name,
                           current_analyzing_function->type_params[i].name) == 0) {
                    var->type_param_index = i;
                    break;
                }
            }
        }
        Symbol* pre_i = (current_analyzing_function == NULL
                        && current_class == NULL)
                       ? lookup_symbol(var->name.value) : NULL;
        char* internal = (pre_i != NULL && pre_i->is_global_predecl
                          && pre_i->internal_name != NULL)
                         ? strdup(pre_i->internal_name)
                         : generate_internal_name();
        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            DataType resolved = resolve_type_alias(var->internal_name);
            if (resolved != DATA_ERROR) {
                var->dataType = resolved;
            }
        }

        char mangled_key[512] = "";
        if (var->is_static
            && current_analyzing_function == NULL
            && current_class == NULL
            && var->base.value.file != NULL
            && var->name.value != NULL) {
            make_static_mangled_name(var->base.value.file, var->name.value,
                                     mangled_key, sizeof(mangled_key));
            if (mangled_key[0] != '\0') {
                free(internal);
                internal = strdup(mangled_key);
            }
        }

        Symbol* sym = create_symbol(var->name, var->dataType,
                                    (mangled_key[0] != '\0') ? mangled_key : var->name.value,
                                    internal);
        sym->ptr_depth = var->ptr_depth;
        sym->is_unsafe = var->is_unsafe;
        bool in_func = (current_analyzing_function != NULL);
        bool in_cls  = (current_class != NULL);
        if (var->is_static && in_func && !in_cls) {
            sym->is_static_local = true;
            sym->has_static_init = false;
            int len = strlen(current_analyzing_function->internal_name)
                    + strlen(var->name.value) + 20;
            char* global_name = malloc(len);
            snprintf(global_name, len, "%s_%s_static",
                     current_analyzing_function->internal_name,
                     var->name.value);
            free(sym->internal_name);
            sym->internal_name = global_name;
            var->is_static_local = true;
            var->has_static_init = false;
            var->static_init_value = NULL;
            var->static_guard_name = malloc(strlen(global_name) + 8);
            snprintf(var->static_guard_name, strlen(global_name) + 8,
                     "%s_guard", global_name);
            free(var->internal_name);
            var->internal_name = strdup(global_name);
            free(internal);
            internal = NULL;
        }

        if (strcmp(var->name.value, "this") == 0 && current_class_name != NULL) {
            sym->class_name = strdup(current_class_name);
        }
        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            ClassEntry* cls = find_class(var->internal_name);
            if (!cls && current_class != NULL) {
                char full[256];
                snprintf(full, sizeof(full), "%s_%s",
                         current_class->name, var->internal_name);
                ClassEntry* inner_cls = find_class(full);
                if (inner_cls) {
                    free(var->internal_name);
                    var->internal_name = strdup(full);
                    cls = inner_cls;
                }
            }
            if (cls && !cls->is_generic) {
                if (sym->class_name) free(sym->class_name);
                sym->class_name = strdup(var->internal_name);
                var->total_bytes = 4;
                sym->total_bytes = 4;
                sym->is_value_type = false;
            }
        }
        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            ClassEntry* cls = find_class(var->internal_name);
            bool skip_check = false;
            if (var->name.value != NULL
                && strcmp(var->name.value, "this") == 0) {
                skip_check = true;
            }
            if (var->internal_name[0] == '_') {
                skip_check = true;
            }
            if (var->is_field_ref) {
                skip_check = true;
            }
            if (var->func_ptr_sig != NULL || var->ptr_array_info != NULL) {
                skip_check = true;
            }
            if (cls == NULL && !skip_check) {
                bool is_type_param = false;
                if (current_class && current_class->is_generic
                    && current_class->generic_ast) {
                    for (int tpi = 0;
                         tpi < current_class->generic_ast->type_param_count; tpi++) {
                        if (strcmp(var->internal_name,
                                   current_class->generic_ast->type_params[tpi].name) == 0) {
                            is_type_param = true;
                            break;
                        }
                    }
                }
                if (!is_type_param && in_param_list && current_analyzing_function
                    && current_analyzing_function->is_generic) {
                    for (int tpi = 0;
                         tpi < current_analyzing_function->type_param_count; tpi++) {
                        if (strcmp(var->internal_name,
                                   current_analyzing_function->type_params[tpi].name) == 0) {
                            is_type_param = true;
                            break;
                        }
                    }
                }
                if (!is_type_param) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(var->name),
                          "unknown type '%s' for variable '%s'",
                          var->internal_name, var->name.value);
                }
            }
            if (cls && cls->is_generic) {
                DataType* targs = NULL;
                char** targ_names = NULL;
                int targ_count = 0;

                if (var->generic_type_args != NULL && var->generic_type_arg_count > 0) {
                    targs = var->generic_type_args;
                    targ_names = var->generic_type_arg_names;
                    targ_count = var->generic_type_arg_count;
                } else if (var->Value != NULL) {
                    if (var->Value->type == AST_NEW_INSTANCE) {
                        AST_NewInstance* ni = (AST_NewInstance*)var->Value;
                        targs = ni->generic_type_args;
                        targ_names = ni->generic_type_arg_names;
                        targ_count = ni->generic_type_arg_count;
                    }
                }

                if (targ_count > 0) {
                    ClassEntry* inst_cls = instantiate_generic_class(
                        cls, targs, targ_names, targ_count);
                    if (inst_cls) {
                        free(var->internal_name);
                        var->internal_name = strdup(inst_cls->name);
                        if (sym->class_name) free(sym->class_name);
                        sym->class_name = strdup(inst_cls->name);
                        var->total_bytes = inst_cls->total_size;
                        sym->total_bytes = inst_cls->total_size;
                        sym->is_value_type = true;
                        sym->alignment = (inst_cls->total_size >= 8) ? 8 : 4;
                    }
                }
                if (sym->class_name) free(sym->class_name);
                sym->class_name = strdup(var->internal_name);
                var->total_bytes = cls->total_size;
                sym->total_bytes = cls->total_size;
                sym->is_value_type = (cls->total_size > 4);
            }
        }
        {
            Symbol* pre_g = (current_analyzing_function == NULL
                            && current_class == NULL)
                           ? lookup_symbol(var->name.value) : NULL;
            if (pre_g != NULL && pre_g != sym && pre_g->is_global_predecl) {
                pre_g->is_global_predecl = false;
                pre_g->type = sym->type;
                pre_g->total_bytes = sym->total_bytes;
                pre_g->is_value_type = sym->is_value_type;
                pre_g->ptr_depth = sym->ptr_depth;
                pre_g->dim_count = sym->dim_count;
                pre_g->dim_sizes = sym->dim_sizes;
                pre_g->is_dynamic_array = sym->is_dynamic_array;
                pre_g->is_field = sym->is_field;
                pre_g->is_static = sym->is_static;
                pre_g->is_const = sym->is_const;
                if (sym->class_name) {
                    if (pre_g->class_name) free(pre_g->class_name);
                    pre_g->class_name = strdup(sym->class_name);
                }
                if (sym->const_value_str) {
                    if (pre_g->const_value_str) free(pre_g->const_value_str);
                    pre_g->const_value_str = strdup(sym->const_value_str);
                }
                free_symbol(sym);
                sym = pre_g;
            } else {
                insert_symbol(sym);
            }
        }
        symbol_copy_generic_args(sym, var);

        if (sym->class_name != NULL && sym->total_bytes >= 4) {
            var->base.type = AST_VAR;
            AST_Literal* zero = calloc(1, sizeof(AST_Literal));
            zero->base.type = AST_LITERAL;
            zero->base.value.type = TOKEN_NUMBER;
            zero->base.value.value = strdup("0");
            zero->dataType = DATA_INT;
            zero->is_call_function = false;
            var->Value = (ASTNode*)zero;
        }

        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            ClassEntry* cls = find_class(var->internal_name);
            if (cls && cls->is_generic && var->generic_type_arg_count > 0) {
                ClassEntry* inst_cls = instantiate_generic_class(
                    cls, var->generic_type_args, var->generic_type_arg_names,
                    var->generic_type_arg_count);
                if (inst_cls) {
                    free(var->internal_name);
                    var->internal_name = strdup(inst_cls->name);
                    var->dataType = DATA_IDENTIFIER;
                    var->total_bytes = inst_cls->total_size;

                    free(sym->internal_name);
                    sym->internal_name = strdup(inst_cls->name);
                    sym->total_bytes = inst_cls->total_size;
                    sym->is_value_type = true;
                    if (sym->class_name) free(sym->class_name);
                    sym->class_name = strdup(inst_cls->name);
                }
            }
        }
        if (var->dataType == DATA_IDENTIFIER && var->internal_name != NULL) {
            ClassEntry* cls = find_class(var->internal_name);
            if (cls && !cls->is_generic) {
                if (sym->class_name) free(sym->class_name);
                sym->class_name = strdup(var->internal_name);
                if (strcmp(var->name.value, "this") != 0) {
                    var->total_bytes = cls->total_size;
                    sym->total_bytes = cls->total_size;
                    sym->is_value_type = (cls->total_size > 4);
                } else {
                    var->total_bytes = 4;
                    sym->total_bytes = 4;
                    sym->is_value_type = false;
                }
            }
        }

        if (var->func_ptr_sig) {
            sym->func_sig = deep_copy_func_sig(var->func_ptr_sig);
        }

        if (var->ptr_array_info) {
            sym->ptr_array_info = malloc(sizeof(PtrArrayInfo));
            sym->ptr_array_info->element_type = var->ptr_array_info->element_type;
            sym->ptr_array_info->dim_count    = var->ptr_array_info->dim_count;
            sym->ptr_array_info->dim_sizes    = malloc(sizeof(int) * var->ptr_array_info->dim_count);
            memcpy(sym->ptr_array_info->dim_sizes, var->ptr_array_info->dim_sizes,
                   sizeof(int) * var->ptr_array_info->dim_count);
        }
        sym->type_param_index = var->type_param_index;
        sym->is_const = var->is_const;
        sym->level_const_mask = var->level_const_mask;
        if (var->dim_count > 0 && var->dim_sizes != NULL) {
            int elem_size = S_get_type_size(var->dataType);
            int total = 1;
            bool has_unknown = false;
            for (int i = 0; i < var->dim_count; i++) {
                if (var->dim_sizes[i] > 0) total *= var->dim_sizes[i];
                else has_unknown = true;
            }
            if (has_unknown) {
                var->total_bytes = 8;
                sym->total_bytes = 8;
                sym->is_value_type = true;
                sym->is_dynamic_array = true;
            } else {
                var->total_bytes = elem_size * total;
                sym->total_bytes = var->total_bytes;
                sym->is_value_type = (var->total_bytes > 4);
                if (var->func_ptr_sig != NULL && var->dim_count > 0) {
                    sym->is_value_type = true;
                }
            }
        } else if (sym->class_name != NULL) {
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
        } else if (var->ptr_depth > 0) {
            var->total_bytes = 4;
            sym->total_bytes = 4;
            sym->is_value_type = false;
        } else {
            var->total_bytes = S_get_type_size(var->dataType);
            sym->total_bytes = var->total_bytes;
            if (var->total_bytes > 4) {
                sym->is_value_type = true;
            }
        }

        if (internal != NULL) {
            if (in_param_list && var->dataType == DATA_IDENTIFIER
                && var->internal_name != NULL
                && var->internal_name[0] != '_') {
                if (var->static_storage_name) free(var->static_storage_name);
                var->static_storage_name = strdup(var->internal_name);
                }
            var->internal_name = strdup(internal);
            free(internal);
        }

        if (var->dim_count > 0 && !in_param_list && var->func_ptr_sig == NULL) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(var->name),
                  "dynamic array must be initialized at declaration");
        }
        break;
    }

    case AST_VAR_ASS: {
        AST_Var* ast_var_ass = (AST_Var*)temp;

        if (ast_var_ass->ptr_depth == -1 && ast_var_ass->deref_target != NULL) {
            analyze_node(ast_var_ass->deref_target);
            analyze_node(ast_var_ass->Value);

            int deref_count = 1;
            ASTNode* innermost = ast_var_ass->deref_target;
            while (innermost && innermost->type == AST_DEREF) {
                deref_count++;
                innermost = ((AST_Deref*)innermost)->operand;
            }

            DataType target_type = DATA_ERROR;
            if (innermost && innermost->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)innermost;
                if (lit->base.value.type == TOKEN_IDENTIFIER) {
                    Symbol* ptr_sym = lookup_symbol(lit->base.value.value);
                    if (ptr_sym) {
                        if (deref_count > 0
                            && (ptr_sym->level_const_mask
                                & (1u << (deref_count - 1)))) {
                            ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                                  SEMLOC(ast_var_ass->base.value),
                                  "cannot assign through %d-level const pointer '%s'",
                                  deref_count, lit->base.value.value);
                                }
                        target_type = ptr_sym->type;
                        ast_var_ass->dataType = target_type;
                    }
                }
            } else if (ast_var_ass->deref_target->type == AST_DEREF) {
                AST_Deref* inner = (AST_Deref*)ast_var_ass->deref_target;
                target_type = inner->resultType;
                ast_var_ass->dataType = target_type;
            }
            DataType expr_type = get_expression_type(ast_var_ass->Value);
            if (target_type != DATA_ERROR && expr_type != DATA_ERROR &&
                target_type != expr_type) {
                if (!types_compatible(target_type, expr_type)) {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(ast_var_ass->base.value),
                          "type mismatch in pointer assignment: expected '%s', got '%s'",
                          data_type_to_string(target_type),
                          data_type_to_string(expr_type));
                }
            }
            break;
        }

        if (ast_var_ass->Value && ast_var_ass->Value->type == AST_NEW_INSTANCE) {
            AST_NewInstance* ni = (AST_NewInstance*)ast_var_ass->Value;
            if (ni->generic_infer) {
                Symbol* tsym = lookup_symbol(ast_var_ass->name.value);
                if (tsym && tsym->generic_type_arg_count > 0) {
                    ni->generic_type_arg_count = tsym->generic_type_arg_count;
                    ni->generic_type_args = malloc(sizeof(DataType) * (size_t)tsym->generic_type_arg_count);
                    if (!ni->generic_type_args) OUT_OF_MEMORY();
                    memcpy(ni->generic_type_args, tsym->generic_type_args,
                           sizeof(DataType) * (size_t)tsym->generic_type_arg_count);
                    ni->generic_type_arg_names = malloc(sizeof(char*) * (size_t)tsym->generic_type_arg_count);
                    if (!ni->generic_type_arg_names) OUT_OF_MEMORY();
                    for (int gi = 0; gi < tsym->generic_type_arg_count; gi++)
                        ni->generic_type_arg_names[gi] =
                            (tsym->generic_type_arg_names && tsym->generic_type_arg_names[gi])
                            ? strdup(tsym->generic_type_arg_names[gi]) : NULL;
                    ni->generic_infer = false;
                } else {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(ast_var_ass->name),
                          "cannot infer generic type arguments for '%s'",
                          ni->class_name);
                }
            }
        }

        analyze_node(ast_var_ass->Value);

        {
            Symbol* ass_sym = lookup_symbol(ast_var_ass->name.value);
            if (ass_sym != NULL
                && ass_sym->class_name != NULL
                && strcmp(ass_sym->class_name, "Function") == 0
                && ast_var_ass->Value != NULL) {
                if (ast_var_ass->Value->type == AST_LITERAL) {
                    ASTNode* wrapped = sem_wrap_function_value(ast_var_ass->Value);
                    if (wrapped != NULL) {
                        ast_var_ass->Value = wrapped;
                        FuncSignature* sig = ((AST_AddressOf*)
                            ((AST_NewInstance*)wrapped)->arguments)->target_func_sig;
                        ast_var_ass->func_ptr_sig = deep_copy_func_sig(sig);
                        if (ass_sym->func_sig) free_func_sig(ass_sym->func_sig);
                        ass_sym->func_sig = deep_copy_func_sig(sig);
                    } else {
                        AST_Literal* rhs_lit = (AST_Literal*)ast_var_ass->Value;
                        if (!rhs_lit->is_call_function
                            && rhs_lit->base.value.type == TOKEN_IDENTIFIER) {
                            Symbol* rhs_sym = lookup_symbol(rhs_lit->base.value.value);
                            if (rhs_sym != NULL
                                && rhs_sym->class_name != NULL
                                && strcmp(rhs_sym->class_name, "Function") == 0
                                && rhs_sym->func_sig != NULL) {
                                if (ass_sym->func_sig) free_func_sig(ass_sym->func_sig);
                                ass_sym->func_sig = deep_copy_func_sig(rhs_sym->func_sig);
                            }
                        }
                    }
                }
            }
        }

        Symbol* ass_pre_sym = lookup_symbol_with_file(ast_var_ass->name.value,
                                                      ast_var_ass->base.value.file);
        DataType ass_target_type = (ass_pre_sym != NULL)
                                   ? ass_pre_sym->type
                                   : ast_var_ass->dataType;
        if (ass_target_type == DATA_STR && ast_var_ass->Value != NULL) {
            DataType expr_type = get_expression_type(ast_var_ass->Value);
            if (expr_type == DATA_STRING) {
                AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
                conv->base.type = AST_STR_FROM_STRING;
                conv->base.next = NULL;
                conv->base.value = ast_var_ass->base.value;
                conv->source = ast_var_ass->Value;
                ast_var_ass->Value = (ASTNode*)conv;
            }
        }

        Symbol* sym = lookup_symbol_with_file(ast_var_ass->name.value,
                                              ast_var_ass->base.value.file);
        if (sym == NULL) {
            ERROR(ERR_SEM_UNDECLARED_VAR,
                  SEMLOC(ast_var_ass->name),
                  "variable '%s' is not declared",
                  ast_var_ass->name.value);
            ast_var_ass->dataType = DATA_ERROR;
        } else if (sym->is_const) {
            ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                  SEMLOC(ast_var_ass->name),
                  "cannot assign to const variable '%s'",
                  ast_var_ass->name.value);
            ast_var_ass->dataType = sym->type;
            break;
        } else {
            if (ast_var_ass->is_unsafe) sym->is_unsafe = true;
            if (sym->func_sig != NULL
                    && ast_var_ass->Value != NULL
                    && ast_var_ass->Value->type == AST_LITERAL
                    && !((AST_Literal*)ast_var_ass->Value)->is_call_function) {
                AST_Literal* rhs_lit = (AST_Literal*)ast_var_ass->Value;
                Symbol* rhs_sym = lookup_symbol(rhs_lit->base.value.value);
                if (rhs_sym && rhs_sym->is_function && rhs_sym->func_sig) {
                    AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                    if (!addr) OUT_OF_MEMORY();
                    addr->base.type = AST_ADDRESS_OF;
                    addr->base.next = NULL;
                    addr->base.value = rhs_lit->base.value;
                    addr->operand = ast_var_ass->Value;
                    addr->resultType = DATA_INT;
                    addr->ptr_depth = 1;
                    addr->target_func_sig = malloc(sizeof(FuncSignature));
                    memset(addr->target_func_sig, 0, sizeof(FuncSignature));
                    addr->target_func_sig->return_type = rhs_sym->func_sig->return_type;
                    addr->target_func_sig->return_pointee_const =
                        rhs_sym->func_sig->return_pointee_const;
                    addr->target_func_sig->param_count = rhs_sym->func_sig->param_count;
                    addr->target_func_sig->param_types = malloc(
                        sizeof(DataType) * rhs_sym->func_sig->param_count);
                    memcpy(addr->target_func_sig->param_types,
                           rhs_sym->func_sig->param_types,
                           sizeof(DataType) * rhs_sym->func_sig->param_count);
                    if (rhs_sym->func_sig->param_pointee_const) {
                        addr->target_func_sig->param_pointee_const = malloc(
                            sizeof(bool) * rhs_sym->func_sig->param_count);
                        memcpy(addr->target_func_sig->param_pointee_const,
                               rhs_sym->func_sig->param_pointee_const,
                               sizeof(bool) * rhs_sym->func_sig->param_count);
                    }
                    ast_var_ass->Value = (ASTNode*)addr;
                }
            }
            if (is_unsafe_alloc_call(ast_var_ass->Value)) {
                if (!sym->is_unsafe) {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(ast_var_ass->base.value),
                          "unsafe allocation requires 'unsafe' pointer");
                }
            }
            if (ast_var_ass->Value && ast_var_ass->Value->type == AST_NEW_INSTANCE) {
                ast_var_ass->copy_bytes = 0;
            } else if (ast_var_ass->Value && ast_var_ass->Value->type == AST_NEW_ARRAY) {
                ast_var_ass->copy_bytes = 8;
            }
            if (ast_var_ass->ptr_depth != -1) {
                ast_var_ass->ptr_depth = sym->ptr_depth;
            }
            ast_var_ass->dataType = sym->type;
            if (ast_var_ass->Value && ast_var_ass->Value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)ast_var_ass->Value;
                if (lit->is_call_function && lit->multi_return_type) {
                    if (!sym->func_sig) {
                        sym->func_sig = calloc(1, sizeof(FuncSignature));
                        sym->func_sig->return_type = DATA_VOID;
                    }
                    if (!sym->func_sig->multi_return) {
                        sym->func_sig->multi_return =
                            deep_copy_multi_return(lit->multi_return_type);
                    }
                }
            }
            ast_var_ass->internal_name = strdup(sym->internal_name);
            if (ast_var_ass->Value != NULL && ast_var_ass->Value->type == AST_LITERAL) {
                AST_Literal* rhs = (AST_Literal*)ast_var_ass->Value;
                if (rhs->is_call_function && rhs->is_closure_call) {
                    sym->is_closure_holder = true;
                    sym->is_closure_ptr = true;
                }
            }
            if (ast_var_ass->Value && ast_var_ass->Value->type == AST_RETURN_EXTRACT) {
                AST_ReturnExtract* ex = (AST_ReturnExtract*)ast_var_ass->Value;
                if (ex->extracted_func_sig && ex->extracted_func_sig->returns_closure) {
                    sym->is_closure_holder = true;
                    sym->is_closure_ptr = true;
                    if (sym->func_sig) sym->func_sig->returns_closure = true;
                }
            }
            if (sym->func_sig && ast_var_ass->Value != NULL
                && ast_var_ass->Value->type == AST_MEMBER_ACCESS
                && ((AST_MemberAccess*)ast_var_ass->Value)->is_method_ptr) {
                AST_MemberAccess* ma = (AST_MemberAccess*)ast_var_ass->Value;
                FuncSignature* sig = sym->func_sig;
                if (sig->param_count != ma->method_param_count) {
                    ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                          SEMLOC(ast_var_ass->name),
                          "function pointer expects %d argument(s), "
                          "but method '%s' has %d",
                          sig->param_count, ma->method_internal,
                          ma->method_param_count);
                } else {
                    for (int i = 0; i < sig->param_count; i++) {
                        if (!types_compatible(sig->param_types[i],
                                              ma->method_param_types[i])) {
                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                  SEMLOC(ast_var_ass->name),
                                  "parameter %d type mismatch: "
                                  "expected '%s', got '%s'",
                                  i + 1,
                                  data_type_to_string(sig->param_types[i]),
                                  data_type_to_string(ma->method_param_types[i]));
                            break;
                        }
                    }
                }
                DataType expected_ret = sig->return_type;
                DataType actual_ret = ma->member_return_type;
                if (expected_ret != DATA_VOID && actual_ret != DATA_VOID &&
                    !types_compatible(expected_ret, actual_ret)) {
                    ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                          SEMLOC(ast_var_ass->name),
                          "return type mismatch: expected '%s', got '%s'",
                          data_type_to_string(expected_ret),
                          data_type_to_string(actual_ret));
                }
                sym->is_method_ptr = true;
            }

            if (sym->is_dynamic_array) {
                ast_var_ass->copy_bytes = 8;
            } else if (sym->is_value_type && sym->total_bytes > 0) {
                ast_var_ass->copy_bytes = sym->total_bytes;
            } else {
                ast_var_ass->copy_bytes = 0;
            }

            if (ast_var_ass->Value != NULL) {
                check_unsigned_assignment(sym->type, ast_var_ass->Value,
                                          SEMLOC(ast_var_ass->name));
            }

            if (sym->ptr_depth > 0) {
                int rhs_level_mask = 0;
                int rhs_ptr_depth  = 0;
                bool rhs_is_addr_of_const = false;

                ASTNode* val = ast_var_ass->Value;
                if (val->type == AST_ADDRESS_OF) {
                    AST_AddressOf* addr = (AST_AddressOf*)val;
                    rhs_level_mask = addr->level_const_mask;
                    rhs_ptr_depth  = addr->ptr_depth;
                    if (addr->operand->type == AST_LITERAL) {
                        AST_Literal* al = (AST_Literal*)addr->operand;
                        if (al->base.value.type == TOKEN_IDENTIFIER) {
                            Symbol* as = lookup_symbol(al->base.value.value);
                            if (as && as->is_const) {
                                rhs_level_mask |= 1u;
                                rhs_is_addr_of_const = true;
                            }
                        }
                    }
                } else if (val->type == AST_LITERAL) {
                    AST_Literal* rl = (AST_Literal*)val;
                    if (rl->is_call_function && rl->is_return_pointee_const) {
                        rhs_level_mask |= 1u;
                    } else if (rl->base.value.type == TOKEN_IDENTIFIER) {
                        Symbol* rs = lookup_symbol(rl->base.value.value);
                        if (rs) {
                            rhs_level_mask = rs->level_const_mask;
                            rhs_ptr_depth  = rs->ptr_depth;
                            if (rs->is_const && rs->dim_count > 0)
                                rhs_level_mask |= 1u;
                        }
                    }
                }

                int max_lvl = sym->ptr_depth < rhs_ptr_depth
                              ? sym->ptr_depth : rhs_ptr_depth;
                bool violation = false;
                int  viol_level = 0;
                for (int lvl = 0; lvl < max_lvl; lvl++) {
                    if (!(sym->level_const_mask & (1u << lvl)) &&
                        (rhs_level_mask & (1u << lvl))) {
                        violation = true;
                        viol_level = lvl;
                        break;
                    }
                }
                if (violation) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(ast_var_ass->name),
                          "cannot assign 'const' pointer at level %d to non-const '%s'",
                          viol_level + 1, ast_var_ass->name.value);
                } else if (rhs_is_addr_of_const
                           && !(sym->level_const_mask & 1u)) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(ast_var_ass->name),
                          "cannot assign address of 'const' to non-const '%s'",
                          ast_var_ass->name.value);
                }
            }

            DataType expr_type = get_expression_type(ast_var_ass->Value);
            if (sym->ptr_depth > 0 && ast_var_ass->Value->type == AST_LITERAL) {
                AST_Literal* rhs_lit = (AST_Literal*)ast_var_ass->Value;
                Symbol* rhs_sym = lookup_symbol(rhs_lit->base.value.value);
                if (rhs_sym && rhs_sym->dim_count > 0) {
                    goto skip_type_check_assign;
                }
            }

            if (expr_type != sym->type && expr_type != DATA_ERROR) {
                if (!types_compatible(sym->type, expr_type)) {
                    ERROR(ERR_SEM_TYPE_MISMATCH,
                          SEMLOC(ast_var_ass->name),
                          "type mismatch: expected '%s', got '%s'",
                          data_type_to_string(sym->type),
                          data_type_to_string(expr_type));
                }
            }

            /* 泛型变型检查（赋值语句）：x = y; */
            if (sym->type == DATA_IDENTIFIER && expr_type == DATA_IDENTIFIER
                && sym->class_name != NULL) {
                const char* rhs_cls_a = infer_static_class_name(ast_var_ass->Value);
                if (rhs_cls_a != NULL
                    && !generic_variance_assign_ok(sym->class_name, rhs_cls_a)) {
                    ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                          SEMLOC(ast_var_ass->name),
                          "cannot convert '%s' to '%s': generic arguments are not compatible under variance",
                          rhs_cls_a, sym->class_name);
                }
            }
            skip_type_check_assign:
            if (sym->func_sig && ast_var_ass->Value->type == AST_ADDRESS_OF) {
                AST_AddressOf* rhs_addr = (AST_AddressOf*)ast_var_ass->Value;
                if (rhs_addr->target_func_sig) {
                    FuncSignature* lhs_sig = sym->func_sig;
                    FuncSignature* rhs_sig = rhs_addr->target_func_sig;
                    if (lhs_sig->return_type != rhs_sig->return_type
                        || lhs_sig->param_count != rhs_sig->param_count) {
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(ast_var_ass->name),
                              "function pointer signature mismatch");
                    } else {
                        if (!lhs_sig->return_pointee_const
                            && rhs_sig->return_pointee_const) {
                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                  SEMLOC(ast_var_ass->name),
                                  "function pointer return type "
                                  "cannot drop 'const'");
                        }
                        for (int fi = 0; fi < lhs_sig->param_count; fi++) {
                            if (lhs_sig->param_types[fi] != rhs_sig->param_types[fi]) {
                                ERROR(ERR_SEM_TYPE_MISMATCH,
                                      SEMLOC(ast_var_ass->name),
                                      "function pointer parameter %d type mismatch",
                                      fi + 1);
                                break;
                            }
                            bool lhs_c = lhs_sig->param_pointee_const
                                         ? lhs_sig->param_pointee_const[fi] : false;
                            bool rhs_c = rhs_sig->param_pointee_const
                                         ? rhs_sig->param_pointee_const[fi] : false;
                            if (!lhs_c && rhs_c) {
                                ERROR(ERR_SEM_TYPE_MISMATCH,
                                      SEMLOC(ast_var_ass->name),
                                      "function pointer parameter %d cannot "
                                      "drop 'const'", fi + 1);
                                break;
                            }
                        }
                    }

                    if (rhs_addr->target_func_sig->multi_return
                        && !sym->func_sig->multi_return) {
                        sym->func_sig->multi_return = deep_copy_multi_return(
                            rhs_addr->target_func_sig->multi_return);
                    }

                    if (rhs_addr->target_func_sig->has_varargs
                        && !sym->func_sig->has_varargs) {
                        sym->func_sig->has_varargs = true;
                        sym->func_sig->vararg_param_count =
                            rhs_addr->target_func_sig->vararg_param_count;
                        if (sym->func_sig->vararg_param_count > 0) {
                            sym->func_sig->vararg_param_indices = malloc(
                                sizeof(int) * sym->func_sig->vararg_param_count);
                            memcpy(sym->func_sig->vararg_param_indices,
                                   rhs_addr->target_func_sig->vararg_param_indices,
                                   sizeof(int) * sym->func_sig->vararg_param_count);
                        }
                    }
                }
            }
            int expr_ptr_depth = get_expr_ptr_depth(ast_var_ass->Value);

            if (sym->ptr_depth > 0 && expr_ptr_depth != sym->ptr_depth) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(ast_var_ass->name),
                      "pointer depth mismatch: expected %d, got %d",
                      sym->ptr_depth, expr_ptr_depth);
            }
        }

        if (sym != NULL)
        {
            if (sym->is_field) {
                if (sym->is_static) {
                    ast_var_ass->is_field_ref = false;
                    ast_var_ass->field_offset = 0;
                } else {
                    ast_var_ass->is_field_ref = true;
                    ast_var_ass->field_offset = sym->field_offset;
                    if (current_analyzing_function
                        && current_analyzing_function->is_const_method) {
                        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                              SEMLOC(ast_var_ass->name),
                              "cannot modify field '%s' in const method",
                              ast_var_ass->name.value);
                    }
                }
            } else {
                ast_var_ass->is_field_ref = false;
                ast_var_ass->field_offset = 0;
            }
        }
        break;
    }

    case AST_IF: {
        AST_If* ast_if = (AST_If*)temp;

        if (ast_if->Condition != NULL) {
            analyze_node(ast_if->Condition);

            DataType cond_type = get_expression_type(ast_if->Condition);
            if (cond_type != DATA_BOOL && cond_type != DATA_ERROR) {
                ERROR(ERR_SEM_CONDITION_NOT_BOOL,
                      SEMLOC(ast_if->Condition->value),
                      "condition must be boolean, got '%s'",
                      data_type_to_string(cond_type));
            }
        }

        enter_scope();
        if (ast_if->code != NULL) {
            analyze_node_list(ast_if->code);
        }
        exit_scope();

        if (ast_if->elif_Count > 0) {
            for (int i = 0; i < ast_if->elif_Count; i++) {
                if (ast_if->elif_condition[i] != NULL) {
                    analyze_node(ast_if->elif_condition[i]);

                    DataType cond_type = get_expression_type(ast_if->elif_condition[i]);
                    if (cond_type != DATA_BOOL && cond_type != DATA_ERROR) {
                        ERROR(ERR_SEM_CONDITION_NOT_BOOL,
                              SEMLOC(ast_if->elif_condition[i]->value),
                              "condition must be boolean, got '%s'",
                              data_type_to_string(cond_type));
                    }
                }

                enter_scope();
                if (ast_if->Elif[i] != NULL) {
                    analyze_node_list(ast_if->Elif[i]);
                }
                exit_scope();
            }
        }

        if (ast_if->Else != NULL) {
            enter_scope();
            analyze_node_list(ast_if->Else);
            exit_scope();
        }
        break;
    }

    case AST_WHILE: {
        AST_While* while_node = (AST_While*)temp;

        if (while_node->Condition != NULL) {
            analyze_node(while_node->Condition);

            DataType cond_type = get_expression_type(while_node->Condition);
            if (cond_type != DATA_BOOL && cond_type != DATA_ERROR) {
                ERROR(ERR_SEM_CONDITION_NOT_BOOL,
                      SEMLOC(while_node->Condition->value),
                      "condition must be boolean, got '%s'",
                      data_type_to_string(cond_type));
            }
        }

        enter_scope();
        if (while_node->code != NULL) {
            analyze_node_list(while_node->code);
        }
        exit_scope();
        break;
    }

    case AST_FOR: {
        AST_For* for_node = (AST_For*)temp;
        enter_scope();

        if (for_node->init) analyze_node_list(for_node->init);

        if (for_node->condition) {
            analyze_node(for_node->condition);
            DataType cond_type = get_expression_type(for_node->condition);
            if (cond_type != DATA_BOOL && cond_type != DATA_ERROR) {
                ERROR(ERR_SEM_CONDITION_NOT_BOOL,
                      SEMLOC(for_node->condition->value),
                      "condition must be boolean");
            }
        }

        if (for_node->increment) analyze_node_list(for_node->increment);

        if (for_node->code) analyze_node_list(for_node->code);

        exit_scope();
        break;
    }

    case AST_FOREACH: {
        AST_ForEach* foreach_node = (AST_ForEach*)temp;

        analyze_node(foreach_node->array);

        DataType elemType = DATA_ERROR;
        if (foreach_node->array->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)foreach_node->array;
            Symbol* sym = lookup_symbol(lit->base.value.value);
            if (sym && sym->dim_count > 0) {
                elemType = sym->type;
            } else if (sym && sym->type == DATA_STR) {
                elemType = DATA_INT;
            } else if (sym) {
                DataType cont_elem = try_container_iterable(sym, foreach_node);
                if (cont_elem != DATA_ERROR) {
                    elemType = cont_elem;
                } else {
                    ERROR(ERR_SEM_NOT_ARRAY,
                          SEMLOC(lit->base.value),
                          "cannot iterate over non-array type");
                }
            } else {
                ERROR(ERR_SEM_NOT_ARRAY,
                      SEMLOC(lit->base.value),
                      "cannot iterate over non-array type");
            }
        } else if (foreach_node->array->type == AST_ARRAY_ACCESS) {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)foreach_node->array;
            elemType = acc->elementType;
        } else if (foreach_node->array->type == AST_DEREF) {
            AST_Deref* d = (AST_Deref*)foreach_node->array;
            if (d->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)d->operand;
                Symbol* sym = lookup_symbol(lit->base.value.value);
                if (sym && sym->dim_count > 0) {
                    elemType = sym->type;
                } else {
                    ERROR(ERR_SEM_NOT_ARRAY,
                          SEMLOC(lit->base.value),
                          "cannot iterate over non-array type");
                }
            } else if (d->operand->type == AST_VAR || d->operand->type == AST_VAR_ASS) {
                AST_Var* var = (AST_Var*)d->operand;
                Symbol* sym = lookup_symbol(var->name.value);
                if (sym && sym->dim_count > 0) {
                    elemType = sym->type;
                } else {
                    ERROR(ERR_SEM_NOT_ARRAY,
                          SEMLOC(var->base.value),
                          "cannot iterate over non-array type");
                }
            } else {
                ERROR(ERR_SEM_NOT_ARRAY,
                      SEMLOC(foreach_node->iterator),
                      "cannot iterate over non-array type");
            }
        } else if (foreach_node->array->type == AST_VAR || foreach_node->array->type == AST_VAR_ASS) {
            AST_Var* var = (AST_Var*)foreach_node->array;
            Symbol* sym = lookup_symbol(var->name.value);
            if (sym && sym->dim_count > 0) {
                elemType = sym->type;
            } else if (sym) {
                DataType cont_elem = try_container_iterable(sym, foreach_node);
                if (cont_elem != DATA_ERROR) {
                    elemType = cont_elem;
                } else {
                    ERROR(ERR_SEM_NOT_ARRAY,
                          SEMLOC(var->base.value),
                          "cannot iterate over non-array type");
                }
            } else {
                ERROR(ERR_SEM_NOT_ARRAY,
                      SEMLOC(var->base.value),
                      "cannot iterate over non-array type");
            }
        } else {
            ERROR(ERR_SEM_NOT_ARRAY,
                  SEMLOC(foreach_node->iterator),
                  "cannot iterate over non-array type");
        }

        if (foreach_node->has_explicit_type) {
            DataType declared = foreach_node->elemType;
            if (elemType != DATA_ERROR &&
                !types_compatible(declared, elemType)) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(foreach_node->iterator),
                      "foreach iterator type '%s' is incompatible with "
                      "array element type '%s'",
                      data_type_to_string(declared),
                      data_type_to_string(elemType));
                }
            foreach_node->elemType = declared;
        } else {
            foreach_node->elemType = elemType;
        }

        foreach_node->is_dynamic_array = false;
        if (foreach_node->array->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)foreach_node->array;
            Symbol* sym = lookup_symbol(lit->base.value.value);
            if (sym && sym->is_dynamic_array) {
                foreach_node->is_dynamic_array = true;
            }
        } else if (foreach_node->array->type == AST_VAR || foreach_node->array->type == AST_VAR_ASS) {
            AST_Var* var = (AST_Var*)foreach_node->array;
            Symbol* sym = lookup_symbol(var->name.value);
            if (sym && sym->is_dynamic_array) {
                foreach_node->is_dynamic_array = true;
            }
        }

        foreach_node->total_elements = 0;
        if (!foreach_node->is_dynamic_array) {
            Symbol* len_sym = NULL;
            if (foreach_node->array->type == AST_LITERAL) {
                len_sym = lookup_symbol(((AST_Literal*)foreach_node->array)->base.value.value);
            } else if (foreach_node->array->type == AST_VAR || foreach_node->array->type == AST_VAR_ASS) {
                len_sym = lookup_symbol(((AST_Var*)foreach_node->array)->name.value);
            }
            if (len_sym && len_sym->dim_count > 0 && len_sym->dim_sizes != NULL) {
                int total = 1;
                for (int d = 0; d < len_sym->dim_count; d++)
                    total *= len_sym->dim_sizes[d];
                foreach_node->total_elements = total;
            }
        }

        enter_scope();
        char* internal = generate_internal_name();
        Symbol* iter_sym = create_symbol(foreach_node->iterator, elemType,
                                         foreach_node->iterator.value, internal);
        if (elemType == DATA_IDENTIFIER
            && foreach_node->array != NULL
            && (foreach_node->array->type == AST_LITERAL
                || foreach_node->array->type == AST_VAR
                || foreach_node->array->type == AST_VAR_ASS)) {
            Symbol* arr_sym = NULL;
            if (foreach_node->array->type == AST_LITERAL)
                arr_sym = lookup_symbol(((AST_Literal*)foreach_node->array)->base.value.value);
            else
                arr_sym = lookup_symbol(((AST_Var*)foreach_node->array)->name.value);
            if (arr_sym && arr_sym->generic_type_arg_names
                && arr_sym->generic_type_arg_count > 0
                && arr_sym->generic_type_arg_names[0]) {
                iter_sym->class_name = strdup(arr_sym->generic_type_arg_names[0]);
            }
        }
        insert_symbol(iter_sym);
        foreach_node->internal_iterator = strdup(internal);
        free(internal);

        if (foreach_node->code) analyze_node_list(foreach_node->code);
        exit_scope();
        break;
    }

    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)temp;
        analyze_node(sw->condition);
        DataType cond_type = get_expression_type(sw->condition);
        if (cond_type != DATA_INT && cond_type != DATA_CHAR &&
            cond_type != DATA_BOOL && cond_type != DATA_UINT && cond_type != DATA_ERROR) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(sw->condition->value),
                  "switch condition must be integer type");
        }

        for (int i = 0; i < sw->case_count; i++) {
            AST_Case* cs = sw->cases[i];
            if (!cs->is_default) {
                analyze_node(cs->value);
            }
            if (cs->body) analyze_node_list(cs->body);
        }
        if (sw->has_default && sw->default_body) {
            analyze_node_list(sw->default_body);
        }

        for (int i = 0; i < sw->case_count; i++) {
            AST_Case* cs1 = sw->cases[i];
            if (cs1->is_default) continue;
            if (cs1->value->type != AST_LITERAL) continue;
            AST_Literal* lit1 = (AST_Literal*)cs1->value;
            if (lit1->base.value.type != TOKEN_NUMBER &&
                lit1->base.value.type != TOKEN_CHAR_LITERAL &&
                lit1->base.value.type != TOKEN_BOOL_LITERAL) continue;

            for (int j = i + 1; j < sw->case_count; j++) {
                AST_Case* cs2 = sw->cases[j];
                if (cs2->is_default) continue;
                if (cs2->value->type != AST_LITERAL) continue;
                AST_Literal* lit2 = (AST_Literal*)cs2->value;
                if (lit2->base.value.type != TOKEN_NUMBER &&
                    lit2->base.value.type != TOKEN_CHAR_LITERAL &&
                    lit2->base.value.type != TOKEN_BOOL_LITERAL) continue;

                bool is_duplicate = false;
                if (lit1->base.value.type == TOKEN_NUMBER &&
                    lit2->base.value.type == TOKEN_NUMBER) {
                    int val1 = atoi(lit1->base.value.value);
                    int val2 = atoi(lit2->base.value.value);
                    if (val1 == val2) is_duplicate = true;
                }
                else if (lit1->base.value.type == TOKEN_CHAR_LITERAL &&
                         lit2->base.value.type == TOKEN_CHAR_LITERAL) {
                    if (strcmp(lit1->base.value.value, lit2->base.value.value) == 0)
                        is_duplicate = true;
                }
                else if (lit1->base.value.type == TOKEN_BOOL_LITERAL &&
                         lit2->base.value.type == TOKEN_BOOL_LITERAL) {
                    if (strcmp(lit1->base.value.value, lit2->base.value.value) == 0)
                        is_duplicate = true;
                }
                else if (lit1->base.value.type == TOKEN_NUMBER &&
                         lit2->base.value.type == TOKEN_CHAR_LITERAL) {
                    int val1 = atoi(lit1->base.value.value);
                    int val2 = (unsigned char)lit2->base.value.value[0];
                    if (val1 == val2) is_duplicate = true;
                }
                else if (lit1->base.value.type == TOKEN_CHAR_LITERAL &&
                         lit2->base.value.type == TOKEN_NUMBER) {
                    int val1 = (unsigned char)lit1->base.value.value[0];
                    int val2 = atoi(lit2->base.value.value);
                    if (val1 == val2) is_duplicate = true;
                }

                if (is_duplicate) {
                    ERROR(ERR_SEM_DUPLICATE_CASE,
                          SEMLOC(lit2->base.value),
                          "duplicate case value '%s'",
                          lit2->base.value.value);
                }
            }
        }
        break;
    }

    case AST_BREAK:
    case AST_CONTINUE:
        break;

    case AST_TRY: {
        AST_Try* tn = (AST_Try*)temp;
        enter_scope();
        if (tn->code) analyze_node_list(tn->code);
        exit_scope();

        ClassEntry* err_base = find_class("ERROR");
        for (int i = 0; i < tn->catch_count; i++) {
            CatchClause* cc = &tn->catches[i];
            ClassEntry* catch_cls = find_class(cc->type_name);
            if (!catch_cls) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND, SEMLOC(cc->var_name),
                      "catch type '%s' is not a class", cc->type_name);
            } else if (err_base && !is_subclass_of(catch_cls, err_base)) {
                ERROR(ERR_SEM_INVALID_CATCH, SEMLOC(cc->var_name),
                      "catch type '%s' must inherit from ERROR", cc->type_name);
            }

            for (int j = 0; j < i; j++) {
                if (strcmp(tn->catches[j].type_name, cc->type_name) == 0) {
                    ERROR(ERR_SEM_INVALID_CATCH, SEMLOC(cc->var_name),
                          "duplicate catch type '%s'", cc->type_name);
                    break;
                }
                ClassEntry* prev_cls = find_class(tn->catches[j].type_name);
                if (prev_cls && catch_cls
                    && is_subclass_of(catch_cls, prev_cls)) {
                    WARNING(ERR_SEM_INVALID_CATCH, SEMLOC(cc->var_name),
                            "catch '%s' is unreachable: covered by earlier catch '%s'",
                            cc->type_name, tn->catches[j].type_name);
                }
            }

            enter_scope();
            char* iname = generate_internal_name();
            Symbol* csym = create_symbol(cc->var_name, DATA_IDENTIFIER,
                                         cc->var_name.value, iname);
            free(iname);
            if (catch_cls) {
                csym->class_name = strdup(catch_cls->name);
                csym->total_bytes = catch_cls->total_size;
                csym->is_value_type = catch_cls->total_size > 4;
                cc->internal_name = strdup(csym->internal_name);
                cc->total_bytes = catch_cls->total_size;
                cc->class_id = catch_cls->class_id;
            } else {
                cc->internal_name = strdup(csym->internal_name);
                cc->total_bytes = 8;
                cc->class_id = 0;
            }
            insert_symbol(csym);
            if (cc->body) analyze_node_list(cc->body);
            exit_scope();
        }

        if (tn->has_finally && tn->finally_code) {
            enter_scope();
            analyze_node_list(tn->finally_code);
            exit_scope();
        }
        break;
    }

    case AST_THROW: {
        AST_Throw* th = (AST_Throw*)temp;
        if (th->value) analyze_node(th->value);

        ClassEntry* thrown_cls = NULL;
        if (th->value) {
            if (th->value->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)th->value;
                if (!lit->is_call_function) {
                    Symbol* s = lookup_symbol(lit->base.value.value);
                    if (s && s->class_name) thrown_cls = find_class(s->class_name);
                }
            } else if (th->value->type == AST_VAR
                       || th->value->type == AST_VAR_ASS) {
                Symbol* s = lookup_symbol(((AST_Var*)th->value)->name.value);
                if (s && s->class_name) thrown_cls = find_class(s->class_name);
            } else if (th->value->type == AST_NEW_INSTANCE) {
                thrown_cls = find_class(((AST_NewInstance*)th->value)->class_name);
            }
        }

        ClassEntry* err_base = find_class("ERROR");
        if (thrown_cls) {
            if (!is_subclass_of(thrown_cls, err_base)) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(th->base.value),
                      "cannot throw '%s': it is not an ERROR subclass",
                      thrown_cls->name);
            } else {
                th->thrown_class_name = strdup(thrown_cls->name);
            }
        } else {
            DataType t = th->value ? get_expression_type(th->value) : DATA_ERROR;
            if (t != DATA_ERROR && t != DATA_IDENTIFIER) {
                ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(th->base.value),
                      "cannot throw value of type '%s'; only ERROR subclass "
                      "instances can be thrown",
                      data_type_to_string(t));
            }
        }
        break;
    }
    case AST_FUNCTION: {
        AST_Function* function_node = (AST_Function*)temp;

        if (function_node->is_native) {
            /* 2.5.1 FFI: 只有"顶层" native 才用原始 C 符号名；
               类内 native 方法（第一个参数是 _this）保留 File__xxx 这种改编名。 */
            bool _is_method = false;
            if (function_node->Parameter) {
                ASTNode* _p0 = function_node->Parameter;
                if (_p0->type == AST_VAR || _p0->type == AST_UNINIT_VAR) {
                    const char* _pn = ((AST_Var*)_p0)->internal_name;
                    if (_pn && strcmp(_pn, "_this") == 0) _is_method = true;
                }
            }
            /* 顶层改编名的形式是 "函数名_类型_类型"；类方法是 "类名_方法名_类型"，
               前缀不是函数名，据此区分，静态方法同样适用。 */
            if (function_node->internal_name) {
                size_t _nl = strlen(function_node->name.value);
                /* 零参数函数的改编名就等于原名，也要算作顶层 */
                bool _top_mangled =
                    strcmp(function_node->internal_name, function_node->name.value) == 0 ||
                    (strncmp(function_node->internal_name, function_node->name.value, _nl) == 0
                     && function_node->internal_name[_nl] == '_');
                if (!_top_mangled) _is_method = true;
            }
            char stdcall_name[512];
            const char* raw_name = function_node->name.value;
            if (!_is_method && function_node->is_stdcall) {
                int bytes = function_node->parameterCount * 4;
                snprintf(stdcall_name, sizeof(stdcall_name), "_%s@%d", raw_name, bytes);
                raw_name = stdcall_name;
            }
            if (!_is_method) {
                if (function_node->internal_name) {
                    free(function_node->internal_name);
                    function_node->internal_name = NULL;
                }
                function_node->internal_name = strdup(raw_name);
            }
            if (function_node->internal_name) {
                semantics_register_native_symbol(function_node->internal_name);
                if (!_is_method)
                    semantics_register_user_extern(function_node->internal_name);
            }
            Symbol* func_sym = lookup_symbol(function_node->name.value);
            if (!func_sym) {
                func_sym = calloc(1, sizeof(Symbol));
                if (!func_sym) OUT_OF_MEMORY();
                func_sym->key = strdup(function_node->name.value);
                func_sym->value.type = TOKEN_IDENTIFIER;
                func_sym->value.value = strdup(function_node->name.value);
                func_sym->type = DATA_INT;
                func_sym->is_function = true;
                insert_symbol(func_sym);
            }
            if (func_sym->internal_name)
                free(func_sym->internal_name);
            func_sym->internal_name = strdup(
                function_node->internal_name
                ? function_node->internal_name
                : function_node->name.value);
            if (!func_sym->func_sig)
                func_sym->func_sig = func_sig_from_ast(function_node);
            break;
        }

        if (function_node->is_generic) {
            Symbol* func_sym = lookup_symbol(function_node->name.value);
            if (!func_sym) {
                func_sym = calloc(1, sizeof(Symbol));
                func_sym->key = strdup(function_node->name.value);
                func_sym->value.type = TOKEN_IDENTIFIER;
                func_sym->value.value = strdup(function_node->name.value);
                func_sym->type = DATA_INT;
                func_sym->internal_name = strdup(
                    function_node->internal_name ? function_node->internal_name
                                                 : function_node->name.value);
                func_sym->is_function = true;
                func_sym->func_sig = func_sig_from_ast(function_node);
                insert_symbol(func_sym);
            }
            if (!func_sym->func_sig) {
                func_sym->func_sig = func_sig_from_ast(function_node);
            }
            if (!function_node->internal_name) {
                function_node->internal_name = strdup(function_node->name.value);
            }
            break;
        }

        if (function_node->return_dim_exprs) {
            for (int i = 0; i < function_node->return_dim_count; i++) {
                if (function_node->return_dim_sizes[i] == -2
                    && function_node->return_dim_exprs[i]) {
                    analyze_node(function_node->return_dim_exprs[i]);
                    int64_t val = 0;
                    bool of = false;
                    if (eval_constant_expr(function_node->return_dim_exprs[i], &val, &of)) {
                        if (of) {
                            ERROR(ERR_SEM_CONST_OVERFLOW,
                                  SEMLOC(function_node->base.value),
                                  "return array dimension overflows 32-bit integer range");
                        } else {
                            function_node->return_dim_sizes[i] = (int)val;
                            AST_Free(function_node->return_dim_exprs[i]);
                            function_node->return_dim_exprs[i] = NULL;
                        }
                    } else {
                        ERROR(ERR_SEM_INVALID_INDEX,
                              SEMLOC(function_node->base.value),
                              "return array dimension must be compile-time constant");
                    }
                    }
            }
        }

        AST_Function* saved_func = current_analyzing_function;
        bool is_nested_func = (saved_func != NULL);
        current_analyzing_function = function_node;

        if (function_node->return_class_name != NULL) {
            function_node->returns_struct = false;
            function_node->return_struct_bytes = 0;
        }

        if (function_node->return_type == DATA_STR) {
            function_node->returns_struct = true;
            function_node->return_struct_bytes = 8;
        }

        if (function_node->is_generic
            && function_node->return_type == DATA_VOID
            && function_node->return_class_name == NULL
            && !function_node->returns_array
            && !function_node->returns_struct
            && function_node->ret_func_sig == NULL
            && function_node->multi_return == NULL) {
            function_node->return_type = DATA_IDENTIFIER;
            if (function_node->type_param_count > 0) {
                function_node->return_class_name =
                    strdup(function_node->type_params[0].name);
            }
        }

        bool had_preset_return = (function_node->Return != NULL);
        AST_Return* saved_return = function_node->Return;
        int saved_return_count = function_node->return_Count;

        function_node->Return = NULL;
        function_node->return_Count = 0;
        collect_function_return(function_node->Code, function_node);

        if (function_node->Code != NULL
            && (function_node->return_type != DATA_VOID
                || function_node->multi_return != NULL)
            && !block_always_returns(function_node->Code)
            && !function_node->is_generic) {
            ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                  SEMLOC(function_node->name),
                  "not all control paths return a value in function '%s'",
                  function_node->name.value);
        }

        if (function_node->Code == NULL && had_preset_return) {
            function_node->Return = saved_return;
            function_node->return_Count = saved_return_count;
        } else if (function_node->Code == NULL && function_node->Return == NULL && had_preset_return) {
            function_node->Return = saved_return;
            function_node->return_Count = saved_return_count;
        }
        if (function_node->internal_name == NULL) {
            int len = strlen(function_node->name.value) + 1;
            for (int i = 0; i < function_node->parameterCount; i++) {
                len += 1 + strlen(data_type_to_string(function_node->parameterType[i]));
            }
            char* sig_name = malloc(len + 1);
            if (sig_name) {
                sprintf(sig_name, "%s", function_node->name.value);
                char* p = sig_name + strlen(sig_name);
                for (int i = 0; i < function_node->parameterCount; i++) {
                    *p++ = '_';
                    const char* tn = data_type_to_string(function_node->parameterType[i]);
                    strcpy(p, tn);
                    p += strlen(tn);
                }
                *p = '\0';
                function_node->internal_name = sig_name;
            }
        }

        if (function_node->is_closure) {
            int len = strlen(function_node->internal_name) + 8;
            char* new_name = malloc(len);
            snprintf(new_name, len, "%s__impl", function_node->internal_name);
            free(function_node->internal_name);
            function_node->internal_name = new_name;
        }

        Symbol* func_sym = lookup_symbol(function_node->name.value);
        if (!func_sym) {
            func_sym = calloc(1, sizeof(Symbol));
            func_sym->key = strdup(function_node->name.value);
            func_sym->value.type = TOKEN_IDENTIFIER;
            func_sym->value.value = strdup(function_node->name.value);
            func_sym->type = DATA_INT;
            func_sym->internal_name = strdup(function_node->internal_name);
            func_sym->is_function = true;
            if (function_node->is_nested && current_scope->parent) {
                Scope* saved = current_scope;
                current_scope = current_scope->parent;
                insert_symbol(func_sym);
                current_scope = saved;
            } else {
                insert_symbol(func_sym);
            }
            if (function_node->ret_func_sig != NULL && !func_sym->func_sig) {
                func_sym->func_sig = deep_copy_func_sig(function_node->ret_func_sig);
            }
            if (func_sym->func_sig) {
                free(func_sym->func_sig->param_types);
                free(func_sym->func_sig->vararg_param_indices);
                free(func_sym->func_sig);
            }
            func_sym->func_sig = func_sig_from_ast(function_node);
        } else {
            if (func_sym->internal_name) free(func_sym->internal_name);
            func_sym->internal_name = strdup(function_node->internal_name);
        }

        if (function_node->is_static && current_class == NULL) {
            func_sym->internal_linkage = true;
        }

        enter_scope();

        function_node->is_nested = is_nested_func;

        in_param_list = true;
        if (function_node->Parameter != NULL) {
            analyze_node_list(function_node->Parameter);
        }

        in_param_list = false;

        if (function_node->has_varargs) {
            for (int vi = 0; vi < function_node->vararg_param_count; vi++) {
                char* name = function_node->vararg_internal_names[vi];
                Symbol* sym = lookup_symbol(name);
                if (sym) {
                    if (sym->func_sig == NULL) {
                        sym->dim_count = 1;
                        sym->dim_sizes = malloc(sizeof(int));
                        sym->dim_sizes[0] = -1;
                        sym->ptr_depth = 1;
                        sym->is_value_type = true;
                        sym->total_bytes = 8;
                        sym->is_dynamic_array = true;
                    }
                }
                ASTNode* p = function_node->Parameter;
                int idx = function_node->vararg_param_indices[vi];
                for (int i = 0; i < idx && p; i++) p = p->next;
                if (p && (p->type == AST_VAR || p->type == AST_UNINIT_VAR)) {
                    AST_Var* var = (AST_Var*)p;
                    if (var->func_ptr_sig == NULL) {
                        var->total_bytes = 8;
                        var->dim_count = 1;
                        if (!var->dim_sizes) {
                            var->dim_sizes = malloc(sizeof(int));
                            var->dim_sizes[0] = -1;
                        }
                    }
                }
            }
        }

        if (function_node->Code != NULL) {
            analyze_node_list(function_node->Code);
        }

        if (function_node->Code == NULL && function_node->Return != NULL) {
            analyze_node((ASTNode*)function_node->Return);
        }

        if (function_node->is_nested && current_analyzing_function != NULL) {
            int upval_cap = 4, upval_cnt = 0;
            char** upval_names = malloc(sizeof(char*) * upval_cap);
            DataType* upval_types = malloc(sizeof(DataType) * upval_cap);

            if (function_node->Code != NULL) {
                collect_free_vars_from_ast(function_node->Code,
                                           &upval_names, &upval_types,
                                           &upval_cnt, &upval_cap);
            }
            if (upval_cnt > 0) {
                function_node->is_closure = true;
                function_node->upvalue_count = upval_cnt;
                function_node->upvalue_names = upval_names;
                function_node->upvalue_types = upval_types;
                function_node->upvalue_internal_names =
                    malloc(sizeof(char*) * upval_cnt);
                for (int u = 0; u < upval_cnt; u++) {
                    Symbol* usym = lookup_symbol(upval_names[u]);
                    function_node->upvalue_internal_names[u] =
                        usym ? strdup(usym->internal_name) : strdup("?");
                }

                int new_max = function_node->parameterCount + 2;
                if (new_max > function_node->max_parameters) {
                    function_node->max_parameters = new_max;
                    function_node->parameterType = realloc(
                        function_node->parameterType,
                        sizeof(DataType) * new_max);
                }
                memmove(function_node->parameterType + 1,
                        function_node->parameterType,
                        sizeof(DataType) * function_node->parameterCount);
                function_node->parameterType[0] = DATA_INT;
                function_node->parameterCount++;
                AST_Var* env = calloc(1, sizeof(AST_Var));
                env->base.type = AST_VAR;
                env->name.value = strdup("__env");
                env->name.type = TOKEN_IDENTIFIER;
                env->type = TOKEN_IDENTIFIER;
                env->dataType = DATA_INT;
                env->internal_name = strdup("__env");
                env->base.next = function_node->Parameter;
                if (function_node->Code != NULL && upval_cnt > 0) {
                    rewrite_upvalue_refs(function_node->Code,
                                         upval_names, upval_cnt,
                                         env->internal_name);
                }
                function_node->Parameter = (ASTNode*)env;
                if (func_sym && func_sym->func_sig) {
                    func_sym->func_sig->is_closure_sig = true;
                }
            } else {
                free(upval_names);
                free(upval_types);
            }
        }

        if (function_node->is_nested) {
            add_function_with_params(function_node->name.value,
                                     function_node);
        }

        if (func_sym && func_sym->func_sig
            && function_node->Return
            && function_node->Return->value) {
            ASTNode* rv = function_node->Return->value;
            bool ret_closure = false;
            if (rv->type == AST_LITERAL) {
                AST_Literal* rlit = (AST_Literal*)rv;
                if (rlit->closure_func != NULL) {
                    ret_closure = true;
                } else if (!rlit->is_call_function
                           && rlit->base.value.type == TOKEN_IDENTIFIER) {
                    Symbol* rsym = lookup_symbol(rlit->base.value.value);
                    if (rsym && (rsym->is_closure_ptr || rsym->is_closure_holder)) {
                        ret_closure = true;
                    }
                }
            }
            if (ret_closure) {
                func_sym->func_sig->returns_closure = true;
            }
        }

        if (function_node->ret_func_sig == NULL
            && function_node->Return
            && function_node->Return->value
            && function_node->Return->value->type == AST_LITERAL) {
            AST_Literal* ret_lit = (AST_Literal*)function_node->Return->value;
            if (!ret_lit->is_call_function
                && ret_lit->base.value.type == TOKEN_IDENTIFIER
                && ret_lit->base.value.value != NULL) {
                Symbol* ret_sym = lookup_symbol(ret_lit->base.value.value);
                if (ret_sym && ret_sym->func_sig) {
                    function_node->ret_func_sig = deep_copy_func_sig(ret_sym->func_sig);
                    if (func_sym && func_sym->func_sig) {
                        if (func_sym->func_sig->ret_func_sig) {
                            free_func_sig(func_sym->func_sig->ret_func_sig);
                        }
                        func_sym->func_sig->ret_func_sig =
                            deep_copy_func_sig(ret_sym->func_sig);
                    }
                }
            }
        }

        exit_scope();

        if (strcmp(function_node->name.value, "main") == 0) {
            if (function_node->parameterCount > 0) {
                main_has_args = true;
            }
        }
        if (function_node->return_class_name != NULL) {
            function_node->returns_struct = false;
            function_node->return_struct_bytes = 0;
        }
        if (function_node->return_type == DATA_STR) {
            function_node->returns_struct = true;
            function_node->return_struct_bytes = 8;
        }
        current_analyzing_function = saved_func;
        break;
    }

    case AST_VARARG: {
        break;
    }

    case AST_VARARG_GROUP: {
        AST_VarargGroup* vg = (AST_VarargGroup*)temp;
        ASTNode** prev = &vg->elements;
        ASTNode* elem = vg->elements;
        while (elem) {
            ASTNode* next = elem->next;
            analyze_node(elem);
            if (elem->type == AST_LITERAL
                && !((AST_Literal*)elem)->is_call_function
                && ((AST_Literal*)elem)->base.value.type == TOKEN_IDENTIFIER) {
                AST_Literal* lit = (AST_Literal*)elem;
                Symbol* sym = lookup_symbol(lit->base.value.value);
                if (sym && sym->is_function && sym->func_sig) {
                    AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                    if (!addr) OUT_OF_MEMORY();
                    addr->base.type = AST_ADDRESS_OF;
                    addr->base.next = next;
                    addr->base.value = lit->base.value;
                    addr->operand = elem;
                    addr->resultType = DATA_INT;
                    addr->ptr_depth = 1;
                    addr->target_func_sig = deep_copy_func_sig(sym->func_sig);
                    elem->next = NULL;
                    *prev = (ASTNode*)addr;
                    prev = &addr->base.next;
                    elem = next;
                    continue;
                }
                }
            prev = &elem->next;
            elem = next;
        }
        break;
    }

    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* ma = (AST_MemberAccess*)temp;

        if (ma->object->type == AST_MEMBER_ACCESS) {
            AST_MemberAccess* inner = (AST_MemberAccess*)ma->object;
            if (inner->object->type == AST_LITERAL) {
                AST_Literal* outer_lit = (AST_Literal*)inner->object;
                if (outer_lit->base.value.type == TOKEN_IDENTIFIER
                    && inner->member_name != NULL) {
                    ClassEntry* outer_cls = find_class(outer_lit->base.value.value);
                    if (outer_cls != NULL) {
                        bool is_enum = false;
                        for (int ei = 0; ei < outer_cls->enum_count; ei++) {
                            if (strcmp(outer_cls->enum_names[ei], inner->member_name) == 0) {
                                is_enum = true;
                                break;
                            }
                        }
                        if (is_enum) {
                            char qname[256];
                            snprintf(qname, sizeof(qname), "%s.%s.%s",
                                     outer_lit->base.value.value,
                                     inner->member_name, ma->member_name);
                            Symbol* member_sym = lookup_symbol(qname);
                            if (member_sym != NULL) {
                                ma->member_type = member_sym->type;
                                ma->member_ptr_depth = member_sym->ptr_depth;
                                ma->member_offset = 0;
                                ma->is_method = false;
                                if (member_sym->const_value_str) {
                                    ma->const_value_str = strdup(member_sym->const_value_str);
                                }
                                break;
                            } else {
                                ERROR(ERR_SEM_FIELD_NOT_FOUND,
                                      SEMLOC(ma->base.value),
                                      "enum '%s.%s' has no member '%s'",
                                      outer_lit->base.value.value,
                                      inner->member_name, ma->member_name);
                                break;
                            }
                        } else {
                            char full[256];
                            snprintf(full, sizeof(full), "%s_%s",
                                     outer_lit->base.value.value,
                                     inner->member_name);
                            ClassEntry* inner_cls = find_class(full);
                            if (inner_cls != NULL) {
                                int found_static = 0;
                                for (int i = 0; i < inner_cls->member_count; i++) {
                                    MemberInfo* mi = inner_cls->members[i];
                                    if (mi->is_static
                                        && strcmp(mi->name, ma->member_name) == 0) {
                                        if (mi->const_value_str) {
                                            ma->const_value_str = strdup(mi->const_value_str);
                                        }
                                        free(ma->member_name);
                                        if (mi->is_method && mi->func != NULL
                                            && mi->func->internal_name != NULL) {
                                            ma->member_name = strdup(mi->func->internal_name);
                                        } else {
                                            int len = (int)strlen(inner_cls->name) + 1
                                                    + (int)strlen(mi->name) + 1;
                                            ma->member_name = malloc((size_t)len);
                                            snprintf(ma->member_name, (size_t)len,
                                                     "%s_%s", inner_cls->name, mi->name);
                                        }
                                        ma->member_type = mi->type;
                                        ma->member_ptr_depth = mi->ptr_depth;
                                        ma->member_offset = -1;
                                        ma->is_method = mi->is_method;
                                        found_static = 1;
                                        break;
                                    }
                                }
                                if (found_static) {
                                    break;
                                } else {
                                    ERROR(ERR_SEM_FIELD_NOT_FOUND,
                                          SEMLOC(ma->base.value),
                                          "static member '%s' not found in class '%s'",
                                          ma->member_name, inner_cls->name);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (ma->object->type == AST_LITERAL) {
            AST_Literal* obj_lit = (AST_Literal*)ma->object;
            if (obj_lit->base.value.type == TOKEN_IDENTIFIER) {
                Symbol* member_sym = NULL;

                if (current_class != NULL) {
                    for (int ei = 0; ei < current_class->enum_count; ei++) {
                        if (strcmp(current_class->enum_names[ei],
                                   obj_lit->base.value.value) == 0) {
                            char qname[256];
                            snprintf(qname, sizeof(qname), "%s.%s.%s",
                                     current_class->name,
                                     obj_lit->base.value.value,
                                     ma->member_name);
                            member_sym = lookup_symbol(qname);
                            break;
                        }
                    }
                }

                if (member_sym == NULL) {
                    DataType alias_type = resolve_type_alias(obj_lit->base.value.value);
                    if (alias_type != DATA_ERROR) {
                        char qname[256];
                        snprintf(qname, sizeof(qname), "%s.%s",
                                 obj_lit->base.value.value, ma->member_name);
                        member_sym = lookup_symbol(qname);
                    }
                }

                if (member_sym != NULL) {
                    ma->member_type = member_sym->type;
                    ma->member_ptr_depth = member_sym->ptr_depth;
                    ma->member_offset = 0;
                    ma->is_method = false;
                    if (member_sym->const_value_str) {
                        ma->const_value_str = strdup(member_sym->const_value_str);
                    }
                    break;
                }
            }
        }

        analyze_node(ma->object);

        if (ma->object->type == AST_LITERAL) {
            AST_Literal* obj_lit = (AST_Literal*)ma->object;
            ClassEntry* cls = find_class(obj_lit->base.value.value);
            if (cls == NULL) {
                Symbol* nested_sym = lookup_symbol(obj_lit->base.value.value);
                if (nested_sym != NULL && nested_sym->is_type_name
                    && nested_sym->class_name != NULL) {
                    cls = find_class(nested_sym->class_name);
                }
            }

            if (cls != NULL) {
                int found = 0;
                if (!ma->is_method && ma->method_internal != NULL) {
                    break;
                }
                for (int i = 0; i < cls->member_count; i++) {
                    MemberInfo* mi = cls->members[i];
                    if (mi->is_static && strcmp(mi->name, ma->member_name) == 0) {
                        if (mi->const_value_str) {
                            ma->const_value_str = strdup(mi->const_value_str);
                        }
                        if (!mi->is_method) {
                            if (ma->method_internal) free(ma->method_internal);
                            if (mi->class_name != NULL) {
                                ma->method_internal = strdup(mi->class_name);
                            } else {
                                ma->method_internal = strdup("");
                            }
                        }
                        free(ma->member_name);
                        if (mi->is_method && mi->func != NULL
                            && mi->func->internal_name != NULL) {
                            ma->member_name = strdup(mi->func->internal_name);
                        } else {
                            int len = strlen(cls->name) + 1
                                    + strlen(mi->name) + 1;
                            ma->member_name = malloc(len);
                            snprintf(ma->member_name, len,
                                     "%s_%s", cls->name, mi->name);
                        }

                        ma->member_type = mi->type;
                        ma->member_ptr_depth = mi->ptr_depth;
                        ma->member_offset = -1;
                        ma->is_method = mi->is_method;
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    ERROR(ERR_SEM_FIELD_NOT_FOUND,
                          SEMLOC(ma->base.value),
                          "static member '%s' not found in class '%s'",
                          ma->member_name, cls->name);
                }

                break;
            }
        }

        ClassEntry* target_class = NULL;
        Symbol* obj_sym = NULL;

        if (ma->object->type == AST_THIS) {
            obj_sym = lookup_symbol("this");
            if (!obj_sym || !obj_sym->class_name) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(ma->base.value),
                      "'this' used outside of class context");
                break;
            }
            target_class = find_class(obj_sym->class_name);
        } else if (ma->object->type == AST_LITERAL) {
            AST_Literal* obj_lit = (AST_Literal*)ma->object;

            if (obj_lit->is_call_function
                && obj_lit->call_result_class_name != NULL) {
                target_class = find_class(obj_lit->call_result_class_name);
                if (target_class != NULL)
                    goto member_access_resolved;
            }

            DataType alias_type = resolve_type_alias(obj_lit->base.value.value);
            (void)alias_type;

            obj_sym = lookup_symbol(obj_lit->base.value.value);
            if (obj_sym && obj_sym->ptr_depth > 0 && obj_sym->class_name != NULL) {
                ERROR(ERR_SEM_TYPE_MISMATCH,
                      SEMLOC(ma->base.value),
                      "cannot access member of pointer type; "
                      "use '->' instead of '.'");
                break;
            }
            if (!obj_sym || !obj_sym->class_name) {
                if (obj_sym && obj_sym->type == DATA_IDENTIFIER
                    && ma->object->type == AST_LITERAL) {
                    AST_Function* enclosing = current_analyzing_function;
                    if (enclosing && enclosing->is_generic) {
                        ASTNode* p = enclosing->Parameter;
                        AST_Literal* obj_lit2 = (AST_Literal*)ma->object;
                        while (p) {
                            if ((p->type == AST_VAR || p->type == AST_UNINIT_VAR
                                 || p->type == AST_VAR_ASS)) {
                                AST_Var* pv = (AST_Var*)p;
                                if (strcmp(pv->name.value,
                                           obj_lit2->base.value.value) == 0) {
                                    if (pv->type_param_index >= 0
                                        && pv->type_param_index < enclosing->type_param_count) {
                                        TypeParam* tp = &enclosing->type_params[pv->type_param_index];
                                        for (int c = 0; c < tp->constraint_count; c++) {
                                            if (tp->constraints[c].kind == CONSTRAINT_SUBTYPE) {
                                                target_class = find_class(tp->constraints[c].class_name);
                                                if (target_class && obj_sym) {
                                                    if (obj_sym->class_name) free(obj_sym->class_name);
                                                    obj_sym->class_name = strdup(target_class->name);
                                                }
                                                break;
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                            p = p->next;
                        }
                    }
                }
                if (!target_class) {
                    if (obj_sym && obj_sym->type_param_index >= 0
                        && current_analyzing_function
                        && current_analyzing_function->type_param_count > 0) {
                        int tp_idx = obj_sym->type_param_index;
                        if (tp_idx < current_analyzing_function->type_param_count) {
                            TypeParam* tp =
                                &current_analyzing_function->type_params[tp_idx];
                            for (int c = 0; c < tp->constraint_count; c++) {
                                if (tp->constraints[c].kind == CONSTRAINT_SUBTYPE) {
                                    target_class =
                                        find_class(tp->constraints[c].class_name);
                                    if (target_class) {
                                        if (obj_sym && obj_sym->class_name) {
                                            free(obj_sym->class_name);
                                            obj_sym->class_name =
                                                strdup(target_class->name);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!target_class) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(ma->base.value),
                          "cannot determine class of object '%s'",
                          ma->object->type == AST_LITERAL
                              ? ((AST_Literal*)ma->object)->base.value.value
                              : "?");
                    break;
                }
            }
            if (!target_class) {
                target_class = find_class(obj_sym->class_name);
            }
        } else if (ma->object->type == AST_DEREF) {
            AST_Deref* d = (AST_Deref*)ma->object;
            if (d->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)d->operand;
                obj_sym = lookup_symbol(lit->base.value.value);
                if (obj_sym && obj_sym->class_name) {
                    target_class = find_class(obj_sym->class_name);
                }
            }
            else if (d->operand->type == AST_THIS) {
                obj_sym = lookup_symbol("this");
                if (obj_sym && obj_sym->class_name) {
                    target_class = find_class(obj_sym->class_name);
                }
            }
            if (!target_class) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(ma->base.value),
                      "cannot determine class from dereferenced pointer");
                break;
            }
        } else if (ma->object->type == AST_MEMBER_ACCESS) {
            const char* cls_name = infer_static_class_name(ma->object);
            if (cls_name) {
                target_class = find_class(cls_name);
            }
        } else if (ma->object->type == AST_ARRAY_ACCESS) {
            const char* cls_name = infer_static_class_name(ma->object);
            if (cls_name) {
                target_class = find_class(cls_name);
            }
            if (!target_class) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(ma->base.value),
                      "cannot determine element class of array access");
                break;
            }
        } else {
            const char* cls_name = infer_static_class_name(ma->object);
            if (cls_name != NULL) {
                target_class = find_class(cls_name);
            }
        }

        if (!target_class) {
            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                  SEMLOC(ma->base.value),
                  "unknown class for member '%s'",
                  ma->member_name);
            break;
        }

        member_access_resolved:

        int found = 0;
        for (int i = 0; i < target_class->member_count; i++) {
            MemberInfo* mi = target_class->members[i];
            if (!mi->is_method && strcmp(mi->name, ma->member_name) == 0) {
                ma->member_type   = mi->type;
                ma->member_ptr_depth = mi->ptr_depth;
                ma->is_field_array = (mi->dim_count > 0);
                if (mi->is_static) {
                    int nlen = (int)strlen(target_class->name) + 1
                             + (int)strlen(mi->name) + 1;
                    char* mangled = malloc((size_t)nlen);
                    if (!mangled) OUT_OF_MEMORY();
                    snprintf(mangled, (size_t)nlen, "%s_%s",
                             target_class->name, mi->name);
                    free(ma->member_name);
                    ma->member_name = mangled;
                    ma->member_offset = -1;
                } else {
                    ma->member_offset = mi->offset;
                }
                found = 1;

                if (mi->field_modifier == 2) {
                    ClassEntry* field_owner = target_class;
                    if (mi->inherited_from != NULL) {
                        field_owner = mi->inherited_from;
                    }
                    if (current_class != field_owner) {
                        ERROR(ERR_SEM_FIELD_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot access private field '%s'",
                              ma->member_name);
                    }
                }
                else if (mi->field_modifier == 1) {
                    ClassEntry* field_owner = target_class;
                    if (mi->inherited_from != NULL) {
                        field_owner = mi->inherited_from;
                    }
                    if (!current_class
                        || !is_subclass_of(current_class, field_owner)) {
                        ERROR(ERR_SEM_FIELD_NOT_FOUND,
                              SEMLOC(ma->base.value),
                              "cannot access protected field '%s'",
                              ma->member_name);
                    }
                }
                break;
            }
        }

            if (!found) {
                int method_count = 0;
                MemberInfo* found_method_mi = NULL;
                for (int i = 0; i < target_class->member_count; i++) {
                    MemberInfo* mi = target_class->members[i];
                    if (mi->is_method && strcmp(mi->name, ma->member_name) == 0) {
                        method_count++;
                        if (method_count == 1) {
                            found_method_mi = mi;
                            found = 1;
                        }
                    }
                }

                if (found && found_method_mi) {
                    ma->is_method = true;
                    ma->is_method_ptr = true;
                    ma->method_internal = strdup(found_method_mi->func->internal_name);
                    ma->member_type = DATA_INT;
                    ma->member_ptr_depth = 0;
                    ma->member_return_type = found_method_mi->func->Return
                        ? found_method_mi->func->Return->returnType : DATA_VOID;
                    ma->method_param_count = found_method_mi->func->parameterCount - 1;
                    if (ma->method_param_count > 0) {
                        ma->method_param_types = malloc(sizeof(DataType) * ma->method_param_count);
                        memcpy(ma->method_param_types,
                               &found_method_mi->func->parameterType[1],
                               sizeof(DataType) * ma->method_param_count);
                    } else {
                        ma->method_param_types = NULL;
                    }
                }

                if (!found) {
                    WARNING(ERR_SEM_FIELD_NOT_FOUND,
                            SEMLOC(ma->base.value),
                            "member '%s' not found in class '%s'",
                            ma->member_name, target_class->name);
                    ma->member_type = DATA_INT;
                    ma->member_ptr_depth = 0;
                    ma->member_offset = -1;
                }
            }
        break;
    }

    case AST_NEW_INSTANCE: {
        AST_NewInstance* ni = (AST_NewInstance*)temp;

        if (ni->arguments && !ni->args_pre_analyzed) {
            analyze_node_list(ni->arguments);

            if (ni->class_name != NULL
                && strcmp(ni->class_name, "Thread") == 0
                && ni->arguments != NULL
                && ni->constructor_arg_count >= 1) {
                ASTNode* func_ref = ni->arguments;
                ASTNode* args_head = func_ref->next;
                int extra_arg_count = ni->constructor_arg_count - 1;

                const char* f_name = NULL;
                if (func_ref->type == AST_LITERAL) {
                    f_name = ((AST_Literal*)func_ref)->base.value.value;
                } else if (func_ref->type == AST_MEMBER_ACCESS) {
                    AST_MemberAccess* ma = (AST_MemberAccess*)func_ref;
                    if (ma->is_method && ma->member_name != NULL) {
                        f_name = ma->member_name;
                    }
                }

                bool args_ok = (f_name != NULL);
                if (args_ok) {
                    ASTNode* chk = args_head;
                    while (chk) {
                        if (chk->type == AST_VARARG_GROUP) {
                            chk = chk->next;
                            continue;
                        }
                        if (chk->type == AST_ADDRESS_OF) {
                            chk = chk->next;
                            continue;
                        }
                        if (chk->type != AST_LITERAL
                            || ((AST_Literal*)chk)->is_call_function) {
                            args_ok = false;
                            break;
                        }
                        chk = chk->next;
                    }
                }

                if (args_ok && f_name != NULL) {
                    Symbol* worker_guard = lookup_symbol(f_name);
                    if (worker_guard == NULL
                        || !worker_guard->is_function
                        || worker_guard->func_sig == NULL) {
                        bool found_in_table = false;
                        for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                            if (fe->func != NULL && fe->func->internal_name != NULL
                                && strcmp(fe->func->internal_name, f_name) == 0) {
                                found_in_table = true;
                                break;
                            }
                        }
                        if (!found_in_table) {
                            args_ok = false;
                        }
                    }
                }


                if (args_ok) {
                    Symbol* worker_sym = lookup_symbol(f_name);
                    AST_Function* worker_fn = NULL;
                    for (FuncEntry* fe = func_table; fe; fe = fe->next) {
                        if (fe->func != NULL && fe->func->internal_name != NULL
                            && strcmp(fe->func->internal_name, f_name) == 0) {
                            worker_fn = fe->func;
                            break;
                        }
                    }
                    FuncSignature* worker_sig = NULL;
                    if (worker_sym != NULL && worker_sym->func_sig != NULL) {
                        worker_sig = worker_sym->func_sig;
                    } else if (worker_fn != NULL) {
                        worker_sig = func_sig_from_ast(worker_fn);
                    }
                    if (worker_sig == NULL) {
                        ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(ni->base.value),
                              "Thread: cannot resolve worker function '%s'", f_name);
                        args_ok = false;
                    }
                    DataType worker_ret = worker_sig ? worker_sig->return_type : DATA_INT;
                    bool ret_wide = (worker_ret == DATA_INT64
                                     || worker_ret == DATA_UINT64
                                     || worker_ret == DATA_FLOAT64
                                     || worker_ret == DATA_UFLOAT64
                                     || worker_ret == DATA_STR);
                    int worker_ret_width = S_get_type_size(worker_ret);
                    if (worker_ret == DATA_IDENTIFIER) {
                        worker_ret_width = 4;
                    }
                    if (worker_sig && worker_sig->multi_return != NULL) {
                        worker_ret_width = 4;
                    }
                    if (worker_ret == DATA_INT128 || worker_ret == DATA_UINT128
                        || worker_ret == DATA_FLOAT128 || worker_ret == DATA_UFLOAT128) {
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(ni->base.value),
                              "Thread does not support 128-bit return type yet");
                        worker_ret = DATA_INT;
                        ret_wide = false;
                    }

                    ASTNode* make_args_head = NULL;
                    ASTNode* make_args_tail = NULL;
                    ASTNode* src_arg = args_head;
                    while (src_arg) {
                        ASTNode* copy;
                        if (src_arg->type == AST_VARARG_GROUP) {
                            copy = clone_vararg_group((AST_VarargGroup*)src_arg);
                        } else if (src_arg->type == AST_ADDRESS_OF) {
                            copy = clone_address_of((AST_AddressOf*)src_arg);
                        } else {
                            copy = clone_arg_literal((AST_Literal*)src_arg);
                        }
                        if (!make_args_head) {
                            make_args_head = make_args_tail = copy;
                        } else {
                            make_args_tail->next = copy;
                            make_args_tail = copy;
                        }
                        src_arg = src_arg->next;
                    }

                    static int thread_thunk_counter = 0;
                    int thunk_id = thread_thunk_counter++;

                    char thunk_name[64];
                    char make_name[64];
                    snprintf(thunk_name, sizeof(thunk_name),
                             "__thread_thunk_%d", thunk_id);
                    snprintf(make_name, sizeof(make_name),
                             "__make_thread_fn_%d", thunk_id);

                    ASTNode* call_args_head = NULL;
                    ASTNode* call_args_tail = NULL;
                    int inner_arg_count = 0;
                    int cur_byte_off = 4;
                    ASTNode* mk_arg = make_args_head;
                    int mk_pi = 0;
                    while (mk_arg && mk_pi < extra_arg_count) {
                        DataType pt = (mk_pi < worker_sig->param_count)
                                      ? worker_sig->param_types[mk_pi]
                                      : DATA_INT;
                        const char* pcls = (worker_sig->param_class_names
                                            && mk_pi < worker_sig->param_count)
                                           ? worker_sig->param_class_names[mk_pi]
                                           : NULL;
                        bool is_va = is_vararg_param(worker_sig, mk_pi)
                                     || is_array_param(worker_sig, mk_pi);

                        int N = 1;
                        if (is_va && mk_arg->type == AST_VARARG_GROUP) {
                            N = ((AST_VarargGroup*)mk_arg)->count;
                            if (N < 1) N = 1;
                        }

                        if (is_va) {
                            ASTNode* vg_elem_head = NULL;
                            ASTNode* vg_elem_tail = NULL;
                            for (int i = 0; i < N; i++) {
                                AST_Literal* env_lit = calloc(1, sizeof(AST_Literal));
                                if (!env_lit) OUT_OF_MEMORY();
                                env_lit->base.type = AST_LITERAL;
                                env_lit->base.value.type = TOKEN_IDENTIFIER;
                                env_lit->base.value.value = strdup("__env");
                                env_lit->dataType = DATA_INT;
                                env_lit->internal_name = strdup("__env");

                                AST_Cast* env_int = calloc(1, sizeof(AST_Cast));
                                if (!env_int) OUT_OF_MEMORY();
                                env_int->base.type = AST_CAST;
                                env_int->base.value = env_lit->base.value;
                                env_int->operand = (ASTNode*)env_lit;
                                env_int->target_type = DATA_INT;
                                env_int->target_class_id = -1;
                                env_int->target_ptr_depth = 0;

                                char obuf[16];
                                snprintf(obuf, sizeof(obuf), "%d", cur_byte_off);
                                AST_Literal* off_lit = calloc(1, sizeof(AST_Literal));
                                if (!off_lit) OUT_OF_MEMORY();
                                off_lit->base.type = AST_LITERAL;
                                off_lit->base.value.type = TOKEN_NUMBER;
                                off_lit->base.value.value = strdup(obuf);
                                off_lit->dataType = DATA_INT;

                                AST_Binary* env_add = calloc(1, sizeof(AST_Binary));
                                if (!env_add) OUT_OF_MEMORY();
                                env_add->base.type = AST_BINARY;
                                env_add->left = (ASTNode*)env_int;
                                env_add->op.type = TOKEN_ADD;
                                env_add->op.value = strdup("+");
                                env_add->right = (ASTNode*)off_lit;
                                env_add->resultType = DATA_INT;

                                AST_Cast* env_ptr = calloc(1, sizeof(AST_Cast));
                                if (!env_ptr) OUT_OF_MEMORY();
                                env_ptr->base.type = AST_CAST;
                                env_ptr->operand = (ASTNode*)env_add;
                                env_ptr->target_type = DATA_INT;
                                env_ptr->target_class_id = -1;
                                env_ptr->target_ptr_depth = 1;
                                env_ptr->source_type = DATA_INT;
                                env_ptr->source_ptr_depth = 1;

                                AST_Deref* a_data_ptr = calloc(1, sizeof(AST_Deref));
                                if (!a_data_ptr) OUT_OF_MEMORY();
                                a_data_ptr->base.type = AST_DEREF;
                                a_data_ptr->operand = (ASTNode*)env_ptr;
                                a_data_ptr->resultType = DATA_INT;
                                a_data_ptr->ptr_depth = 0;

                                char ibuf[16];
                                snprintf(ibuf, sizeof(ibuf), "%d", i);
                                AST_Literal* idx_lit = calloc(1, sizeof(AST_Literal));
                                if (!idx_lit) OUT_OF_MEMORY();
                                idx_lit->base.type = AST_LITERAL;
                                idx_lit->base.value.type = TOKEN_NUMBER;
                                idx_lit->base.value.value = strdup(ibuf);
                                idx_lit->dataType = DATA_INT;

                                AST_Literal* four_lit = calloc(1, sizeof(AST_Literal));
                                if (!four_lit) OUT_OF_MEMORY();
                                four_lit->base.type = AST_LITERAL;
                                four_lit->base.value.type = TOKEN_NUMBER;
                                four_lit->base.value.value = strdup("4");
                                four_lit->dataType = DATA_INT;

                                AST_Binary* mul = calloc(1, sizeof(AST_Binary));
                                if (!mul) OUT_OF_MEMORY();
                                mul->base.type = AST_BINARY;
                                mul->left = (ASTNode*)idx_lit;
                                mul->op.type = TOKEN_MUL;
                                mul->op.value = strdup("*");
                                mul->right = (ASTNode*)four_lit;
                                mul->resultType = DATA_INT;

                                AST_Binary* elem_addr = calloc(1, sizeof(AST_Binary));
                                if (!elem_addr) OUT_OF_MEMORY();
                                elem_addr->base.type = AST_BINARY;
                                elem_addr->left = (ASTNode*)a_data_ptr;
                                elem_addr->op.type = TOKEN_ADD;
                                elem_addr->op.value = strdup("+");
                                elem_addr->right = (ASTNode*)mul;
                                elem_addr->resultType = DATA_INT;

                                AST_Cast* elem_ptr = calloc(1, sizeof(AST_Cast));
                                if (!elem_ptr) OUT_OF_MEMORY();
                                elem_ptr->base.type = AST_CAST;
                                elem_ptr->operand = (ASTNode*)elem_addr;
                                elem_ptr->target_type = DATA_INT;
                                elem_ptr->target_class_id = -1;
                                elem_ptr->target_ptr_depth = 1;
                                elem_ptr->source_type = DATA_INT;
                                elem_ptr->source_ptr_depth = 1;

                                AST_Deref* elem_val = calloc(1, sizeof(AST_Deref));
                                if (!elem_val) OUT_OF_MEMORY();
                                elem_val->base.type = AST_DEREF;
                                elem_val->operand = (ASTNode*)elem_ptr;
                                elem_val->resultType = DATA_INT;
                                elem_val->ptr_depth = 0;

                                if (!vg_elem_head) {
                                    vg_elem_head = vg_elem_tail = (ASTNode*)elem_val;
                                } else {
                                    vg_elem_tail->next = (ASTNode*)elem_val;
                                    vg_elem_tail = (ASTNode*)elem_val;
                                }
                            }
                            AST_VarargGroup* vg = calloc(1, sizeof(AST_VarargGroup));
                            if (!vg) OUT_OF_MEMORY();
                            vg->base.type = AST_VARARG_GROUP;
                            vg->base.next = NULL;
                            vg->elements = vg_elem_head;
                            vg->count = N;
                            vg->target_elem_type = (pt == DATA_IDENTIFIER) ? DATA_INT : pt;
                            if (!call_args_head) {
                                call_args_head = call_args_tail = (ASTNode*)vg;
                            } else {
                                call_args_tail->next = (ASTNode*)vg;
                                call_args_tail = (ASTNode*)vg;
                            }
                            inner_arg_count++;
                        } else {
                            AST_Literal* env_lit = calloc(1, sizeof(AST_Literal));
                            if (!env_lit) OUT_OF_MEMORY();
                            env_lit->base.type = AST_LITERAL;
                            env_lit->base.value.type = TOKEN_IDENTIFIER;
                            env_lit->base.value.value = strdup("__env");
                            env_lit->dataType = DATA_INT;
                            env_lit->internal_name = strdup("__env");

                            AST_Cast* env_int = calloc(1, sizeof(AST_Cast));
                            if (!env_int) OUT_OF_MEMORY();
                            env_int->base.type = AST_CAST;
                            env_int->base.value = env_lit->base.value;
                            env_int->operand = (ASTNode*)env_lit;
                            env_int->target_type = DATA_INT;
                            env_int->target_class_id = -1;
                            env_int->target_ptr_depth = 0;

                            char obuf[16];
                            snprintf(obuf, sizeof(obuf), "%d", cur_byte_off);
                            AST_Literal* off_lit = calloc(1, sizeof(AST_Literal));
                            if (!off_lit) OUT_OF_MEMORY();
                            off_lit->base.type = AST_LITERAL;
                            off_lit->base.value.type = TOKEN_NUMBER;
                            off_lit->base.value.value = strdup(obuf);
                            off_lit->dataType = DATA_INT;

                            AST_Binary* env_add = calloc(1, sizeof(AST_Binary));
                            if (!env_add) OUT_OF_MEMORY();
                            env_add->base.type = AST_BINARY;
                            env_add->left = (ASTNode*)env_int;
                            env_add->op.type = TOKEN_ADD;
                            env_add->op.value = strdup("+");
                            env_add->right = (ASTNode*)off_lit;
                            env_add->resultType = DATA_INT;

                            AST_Cast* ptr = calloc(1, sizeof(AST_Cast));
                            if (!ptr) OUT_OF_MEMORY();
                            ptr->base.type = AST_CAST;
                            ptr->operand = (ASTNode*)env_add;
                            ptr->target_type = (pt == DATA_IDENTIFIER) ? DATA_INT : pt;
                            ptr->target_class_name = NULL;
                            ptr->target_class_id = -1;
                            ptr->target_ptr_depth = 1;
                            ptr->source_type = DATA_INT;
                            ptr->source_ptr_depth = 1;

                            AST_Deref* deref = calloc(1, sizeof(AST_Deref));
                            if (!deref) OUT_OF_MEMORY();
                            deref->base.type = AST_DEREF;
                            deref->operand = (ASTNode*)ptr;
                            deref->resultType = pt;
                            deref->ptr_depth = 0;

                            if (!call_args_head) {
                                call_args_head = call_args_tail = (ASTNode*)deref;
                            } else {
                                call_args_tail->next = (ASTNode*)deref;
                                call_args_tail = (ASTNode*)deref;
                            }
                            inner_arg_count++;
                        }
                        cur_byte_off += is_va ? 8 : thread_param_width(pt, pcls);
                        mk_arg = mk_arg->next;
                        mk_pi++;
                    }

                    AST_Literal* inner_call = calloc(1, sizeof(AST_Literal));
                    if (!inner_call) OUT_OF_MEMORY();
                    inner_call->base.type = AST_LITERAL;
                    inner_call->base.value.type = TOKEN_IDENTIFIER;
                    inner_call->base.value.value = strdup(f_name);
                    inner_call->dataType = worker_ret;
                    inner_call->is_call_function = true;
                    inner_call->call_function_Parameter = call_args_head;
                    inner_call->call_function_ParameterCount = inner_arg_count;
                    inner_call->internal_name = strdup(f_name);
                    
                    ASTNode* thunk_body_head = NULL;
                    ASTNode* thunk_body_tail = NULL;
                    ASTNode* value_node = NULL;
                    char int_rvar_buf[64] = {0};
                    char str_rvar_buf[64] = {0};

                    if (worker_sig->multi_return != NULL) {
                        inner_call->dataType = DATA_INT;
                        snprintf(int_rvar_buf, sizeof(int_rvar_buf),
                                 "__mt_sret_%d", thunk_id);

                        AST_Var* sret_var = calloc(1, sizeof(AST_Var));
                        if (!sret_var) OUT_OF_MEMORY();
                        sret_var->base.type = AST_VAR;
                        sret_var->base.value.type = TOKEN_IDENTIFIER;
                        sret_var->base.value.value = strdup(int_rvar_buf);
                        sret_var->name = sret_var->base.value;
                        sret_var->type = TOKEN_INT;
                        sret_var->dataType = DATA_INT;
                        sret_var->internal_name = strdup(int_rvar_buf);
                        sret_var->Value = (ASTNode*)inner_call;
                        sret_var->type_param_index = -1;
                        thunk_body_head = thunk_body_tail = (ASTNode*)sret_var;

                        AST_Literal* sret_ref = calloc(1, sizeof(AST_Literal));
                        if (!sret_ref) OUT_OF_MEMORY();
                        sret_ref->base.type = AST_LITERAL;
                        sret_ref->base.value.type = TOKEN_IDENTIFIER;
                        sret_ref->base.value.value = strdup(int_rvar_buf);
                        sret_ref->dataType = DATA_INT;
                        sret_ref->internal_name = strdup(int_rvar_buf);
                        value_node = (ASTNode*)sret_ref;
                    } else if (worker_ret == DATA_VOID) {
                        thunk_body_head = thunk_body_tail = (ASTNode*)inner_call;

                        AST_Literal* zero_lit = calloc(1, sizeof(AST_Literal));
                        if (!zero_lit) OUT_OF_MEMORY();
                        zero_lit->base.type = AST_LITERAL;
                        zero_lit->base.value.type = TOKEN_NUMBER;
                        zero_lit->base.value.value = strdup("0");
                        zero_lit->dataType = DATA_INT;
                        value_node = (ASTNode*)zero_lit;
                    } else if (worker_ret_width <= 4) {
                        snprintf(int_rvar_buf, sizeof(int_rvar_buf),
                                 "__mt_r_%d", thunk_id);

                        AST_Var* rvar = calloc(1, sizeof(AST_Var));
                        if (!rvar) OUT_OF_MEMORY();
                        rvar->base.type = AST_VAR;
                        rvar->base.value.type = TOKEN_IDENTIFIER;
                        rvar->base.value.value = strdup(int_rvar_buf);
                        rvar->name = rvar->base.value;
                        rvar->type = TOKEN_INT;
                        rvar->internal_name = strdup(int_rvar_buf);
                        rvar->type_param_index = -1;

                        if (is_float_type(worker_ret) || is_ufloat_type(worker_ret)) {
                            rvar->dataType = DATA_INT;
                            AST_Literal* bits_call = calloc(1, sizeof(AST_Literal));
                            if (!bits_call) OUT_OF_MEMORY();
                            bits_call->base.type = AST_LITERAL;
                            bits_call->base.value.type = TOKEN_IDENTIFIER;
                            bits_call->base.value.value = strdup("_float_to_bits");
                            bits_call->is_call_function = true;
                            bits_call->internal_name = strdup("_float_to_bits");
                            bits_call->dataType = DATA_INT;
                            bits_call->call_function_Parameter = (ASTNode*)inner_call;
                            bits_call->call_function_ParameterCount = 1;
                            rvar->Value = (ASTNode*)bits_call;
                        } else if (worker_ret == DATA_STRING) {
                            rvar->dataType = DATA_INT;
                            AST_Cast* str_to_int = calloc(1, sizeof(AST_Cast));
                            if (!str_to_int) OUT_OF_MEMORY();
                            str_to_int->base.type = AST_CAST;
                            str_to_int->base.value = inner_call->base.value;
                            str_to_int->operand = (ASTNode*)inner_call;
                            str_to_int->target_type = DATA_INT;
                            str_to_int->target_class_name = NULL;
                            str_to_int->target_class_id = -1;
                            str_to_int->target_ptr_depth = 0;
                            str_to_int->target_func_sig = NULL;
                            str_to_int->target_dim_count = 0;
                            str_to_int->target_dim_sizes = NULL;
                            str_to_int->source_type = DATA_STRING;
                            str_to_int->source_ptr_depth = 0;
                            str_to_int->source_func_sig = NULL;
                            str_to_int->is_reinterpret = true;
                            str_to_int->is_checked = false;
                            rvar->Value = (ASTNode*)str_to_int;
                        } else {
                            rvar->dataType = worker_ret;
                            rvar->Value = (ASTNode*)inner_call;
                        }

                        thunk_body_head = thunk_body_tail = (ASTNode*)rvar;

                        AST_Literal* r_ref = calloc(1, sizeof(AST_Literal));
                        if (!r_ref) OUT_OF_MEMORY();
                        r_ref->base.type = AST_LITERAL;
                        r_ref->base.value.type = TOKEN_IDENTIFIER;
                        r_ref->base.value.value = strdup(int_rvar_buf);
                        r_ref->dataType = rvar->dataType;
                        r_ref->internal_name = strdup(int_rvar_buf);
                        value_node = (ASTNode*)r_ref;
                    } else if (worker_ret == DATA_STR) {
                        snprintf(str_rvar_buf, sizeof(str_rvar_buf),
                                 "__mt_str_%d", thunk_id);

                        AST_Var* str_decl = calloc(1, sizeof(AST_Var));
                        if (!str_decl) OUT_OF_MEMORY();
                        str_decl->base.type = AST_VAR;
                        str_decl->base.value.type = TOKEN_IDENTIFIER;
                        str_decl->base.value.value = strdup(str_rvar_buf);
                        str_decl->name = str_decl->base.value;
                        str_decl->type = TOKEN_INT;
                        str_decl->dataType = DATA_STR;
                        str_decl->internal_name = strdup(str_rvar_buf);
                        str_decl->Value = (ASTNode*)inner_call;
                        str_decl->type_param_index = -1;
                        thunk_body_head = thunk_body_tail = (ASTNode*)str_decl;

                        AST_Literal* s_ref = calloc(1, sizeof(AST_Literal));
                        if (!s_ref) OUT_OF_MEMORY();
                        s_ref->base.type = AST_LITERAL;
                        s_ref->base.value.type = TOKEN_IDENTIFIER;
                        s_ref->base.value.value = strdup(str_rvar_buf);
                        s_ref->dataType = DATA_STR;
                        s_ref->internal_name = strdup(str_rvar_buf);

                        AST_AddressOf* s_addr = calloc(1, sizeof(AST_AddressOf));
                        if (!s_addr) OUT_OF_MEMORY();
                        s_addr->base.type = AST_ADDRESS_OF;
                        s_addr->operand = (ASTNode*)s_ref;
                        s_addr->resultType = DATA_INT;
                        s_addr->ptr_depth = 1;

                        AST_Cast* s_intptr = calloc(1, sizeof(AST_Cast));
                        if (!s_intptr) OUT_OF_MEMORY();
                        s_intptr->base.type = AST_CAST;
                        s_intptr->operand = (ASTNode*)s_addr;
                        s_intptr->target_type = DATA_INT;
                        s_intptr->target_ptr_depth = 1;
                        s_intptr->target_class_id = -1;

                        AST_Literal* s_idx0 = calloc(1, sizeof(AST_Literal));
                        if (!s_idx0) OUT_OF_MEMORY();
                        s_idx0->base.type = AST_LITERAL;
                        s_idx0->base.value.type = TOKEN_NUMBER;
                        s_idx0->base.value.value = strdup("0");
                        s_idx0->dataType = DATA_INT;

                        AST_ArrayAccess* s_ptr = calloc(1, sizeof(AST_ArrayAccess));
                        if (!s_ptr) OUT_OF_MEMORY();
                        s_ptr->base.type = AST_ARRAY_ACCESS;
                        s_ptr->array = (ASTNode*)s_intptr;
                        s_ptr->indices = malloc(sizeof(ASTNode*));
                        s_ptr->indices[0] = (ASTNode*)s_idx0;
                        s_ptr->index_count = 1;
                        s_ptr->elementType = DATA_INT;
                        value_node = (ASTNode*)s_ptr;
                    } else {
                        snprintf(int_rvar_buf, sizeof(int_rvar_buf),
                                 "__mt_r_%d", thunk_id);

                        AST_Var* rvar = calloc(1, sizeof(AST_Var));
                        if (!rvar) OUT_OF_MEMORY();
                        rvar->base.type = AST_VAR;
                        rvar->base.value.type = TOKEN_IDENTIFIER;
                        rvar->base.value.value = strdup(int_rvar_buf);
                        rvar->name = rvar->base.value;
                        rvar->type = TOKEN_INT64;
                        rvar->dataType = worker_ret;
                        rvar->internal_name = strdup(int_rvar_buf);
                        rvar->Value = (ASTNode*)inner_call;
                        rvar->type_param_index = -1;
                        thunk_body_head = thunk_body_tail = (ASTNode*)rvar;

                        AST_Literal* r_ref = calloc(1, sizeof(AST_Literal));
                        if (!r_ref) OUT_OF_MEMORY();
                        r_ref->base.type = AST_LITERAL;
                        r_ref->base.value.type = TOKEN_IDENTIFIER;
                        r_ref->base.value.value = strdup(int_rvar_buf);
                        r_ref->dataType = worker_ret;
                        r_ref->internal_name = strdup(int_rvar_buf);

                        AST_Literal* mask_lit = calloc(1, sizeof(AST_Literal));
                        if (!mask_lit) OUT_OF_MEMORY();
                        mask_lit->base.type = AST_LITERAL;
                        mask_lit->base.value.type = TOKEN_NUMBER;
                        mask_lit->base.value.value = strdup("4294967295");
                        mask_lit->dataType = DATA_INT64;

                        AST_Binary* and_lit = calloc(1, sizeof(AST_Binary));
                        if (!and_lit) OUT_OF_MEMORY();
                        and_lit->base.type = AST_BINARY;
                        and_lit->left = (ASTNode*)r_ref;
                        and_lit->op.type = TOKEN_BITWISE_AND;
                        and_lit->op.value = strdup("&");
                        and_lit->right = (ASTNode*)mask_lit;
                        and_lit->resultType = DATA_ERROR;

                        AST_Cast* to_int = calloc(1, sizeof(AST_Cast));
                        if (!to_int) OUT_OF_MEMORY();
                        to_int->base.type = AST_CAST;
                        to_int->operand = (ASTNode*)and_lit;
                        to_int->target_type = DATA_INT;
                        to_int->target_ptr_depth = 0;
                        to_int->target_class_id = -1;
                        value_node = (ASTNode*)to_int;
                    }

                    char ret_ptr_var[64];
                    snprintf(ret_ptr_var, sizeof(ret_ptr_var),
                             "__mt_retptr_%d", thunk_id);
                    AST_Var* ret_ptr_decl = calloc(1, sizeof(AST_Var));
                    if (!ret_ptr_decl) OUT_OF_MEMORY();
                    ret_ptr_decl->base.type = AST_VAR;
                    ret_ptr_decl->base.value.type = TOKEN_IDENTIFIER;
                    ret_ptr_decl->base.value.value = strdup(ret_ptr_var);
                    ret_ptr_decl->name = ret_ptr_decl->base.value;
                    ret_ptr_decl->type = TOKEN_INT;
                    ret_ptr_decl->dataType = DATA_INT;
                    ret_ptr_decl->internal_name = strdup(ret_ptr_var);
                    ret_ptr_decl->ptr_depth = 1;
                    ret_ptr_decl->type_param_index = -1;

                    AST_Literal* env_zero_ref = calloc(1, sizeof(AST_Literal));
                    if (!env_zero_ref) OUT_OF_MEMORY();
                    env_zero_ref->base.type = AST_LITERAL;
                    env_zero_ref->base.value.type = TOKEN_IDENTIFIER;
                    env_zero_ref->base.value.value = strdup("__env");
                    env_zero_ref->dataType = DATA_INT;
                    env_zero_ref->internal_name = strdup("__env");

                    AST_Literal* zero_idx = calloc(1, sizeof(AST_Literal));
                    if (!zero_idx) OUT_OF_MEMORY();
                    zero_idx->base.type = AST_LITERAL;
                    zero_idx->base.value.type = TOKEN_NUMBER;
                    zero_idx->base.value.value = strdup("0");
                    zero_idx->dataType = DATA_INT;

                    AST_ArrayAccess* env_zero_acc = calloc(1, sizeof(AST_ArrayAccess));
                    if (!env_zero_acc) OUT_OF_MEMORY();
                    env_zero_acc->base.type = AST_ARRAY_ACCESS;
                    env_zero_acc->array = (ASTNode*)env_zero_ref;
                    env_zero_acc->indices = malloc(sizeof(ASTNode*));
                    env_zero_acc->indices[0] = (ASTNode*)zero_idx;
                    env_zero_acc->index_count = 1;
                    env_zero_acc->elementType = DATA_ERROR;

                    AST_Cast* ret_ptr_cast = calloc(1, sizeof(AST_Cast));
                    if (!ret_ptr_cast) OUT_OF_MEMORY();
                    ret_ptr_cast->base.type = AST_CAST;
                    ret_ptr_cast->operand = (ASTNode*)env_zero_acc;
                    ret_ptr_cast->target_type = DATA_INT;
                    ret_ptr_cast->target_ptr_depth = 1;
                    ret_ptr_cast->target_class_id = -1;
                    ret_ptr_cast->source_type = DATA_ERROR;
                    ret_ptr_cast->source_ptr_depth = 0;

                    ret_ptr_decl->Value = (ASTNode*)ret_ptr_cast;

                    if (thunk_body_head) {
                        thunk_body_tail->next = (ASTNode*)ret_ptr_decl;
                        thunk_body_tail = (ASTNode*)ret_ptr_decl;
                    } else {
                        thunk_body_head = thunk_body_tail = (ASTNode*)ret_ptr_decl;
                    }

                    AST_Literal* ret_ptr_ref1 = calloc(1, sizeof(AST_Literal));
                    if (!ret_ptr_ref1) OUT_OF_MEMORY();
                    ret_ptr_ref1->base.type = AST_LITERAL;
                    ret_ptr_ref1->base.value.type = TOKEN_IDENTIFIER;
                    ret_ptr_ref1->base.value.value = strdup(ret_ptr_var);
                    ret_ptr_ref1->dataType = DATA_INT;
                    ret_ptr_ref1->internal_name = strdup(ret_ptr_var);

                    AST_Var* low_ass = calloc(1, sizeof(AST_Var));
                    if (!low_ass) OUT_OF_MEMORY();
                    low_ass->base.type = AST_VAR_ASS;
                    low_ass->base.value.type = TOKEN_IDENTIFIER;
                    low_ass->base.value.value = strdup(ret_ptr_var);
                    low_ass->name = low_ass->base.value;
                    low_ass->type = TOKEN_INT;
                    low_ass->dataType = DATA_INT;
                    low_ass->ptr_depth = -1;
                    low_ass->deref_target = (ASTNode*)ret_ptr_ref1;
                    low_ass->Value = value_node;
                    low_ass->type_param_index = -1;

                    thunk_body_tail->next = (ASTNode*)low_ass;
                    thunk_body_tail = (ASTNode*)low_ass;

                    if (worker_ret != DATA_VOID
                        || worker_sig->multi_return != NULL) {
                        ASTNode* high_value = NULL;

                        if (worker_ret_width <= 4) {
                            AST_Literal* zero_high = calloc(1, sizeof(AST_Literal));
                            if (!zero_high) OUT_OF_MEMORY();
                            zero_high->base.type = AST_LITERAL;
                            zero_high->base.value.type = TOKEN_NUMBER;
                            zero_high->base.value.value = strdup("0");
                            zero_high->dataType = DATA_INT;
                            high_value = (ASTNode*)zero_high;
                        } else if (worker_ret == DATA_STR) {
                            AST_Literal* s_ref_h = calloc(1, sizeof(AST_Literal));
                            if (!s_ref_h) OUT_OF_MEMORY();
                            s_ref_h->base.type = AST_LITERAL;
                            s_ref_h->base.value.type = TOKEN_IDENTIFIER;
                            s_ref_h->base.value.value = strdup(str_rvar_buf);
                            s_ref_h->dataType = DATA_STR;
                            s_ref_h->internal_name = strdup(str_rvar_buf);

                            AST_AddressOf* s_addr_h = calloc(1, sizeof(AST_AddressOf));
                            if (!s_addr_h) OUT_OF_MEMORY();
                            s_addr_h->base.type = AST_ADDRESS_OF;
                            s_addr_h->operand = (ASTNode*)s_ref_h;
                            s_addr_h->resultType = DATA_INT;
                            s_addr_h->ptr_depth = 1;

                            AST_Cast* s_intptr_h = calloc(1, sizeof(AST_Cast));
                            if (!s_intptr_h) OUT_OF_MEMORY();
                            s_intptr_h->base.type = AST_CAST;
                            s_intptr_h->operand = (ASTNode*)s_addr_h;
                            s_intptr_h->target_type = DATA_INT;
                            s_intptr_h->target_ptr_depth = 1;
                            s_intptr_h->target_class_id = -1;

                            AST_Literal* s_idx1 = calloc(1, sizeof(AST_Literal));
                            if (!s_idx1) OUT_OF_MEMORY();
                            s_idx1->base.type = AST_LITERAL;
                            s_idx1->base.value.type = TOKEN_NUMBER;
                            s_idx1->base.value.value = strdup("1");
                            s_idx1->dataType = DATA_INT;

                            AST_ArrayAccess* s_len = calloc(1, sizeof(AST_ArrayAccess));
                            if (!s_len) OUT_OF_MEMORY();
                            s_len->base.type = AST_ARRAY_ACCESS;
                            s_len->array = (ASTNode*)s_intptr_h;
                            s_len->indices = malloc(sizeof(ASTNode*));
                            s_len->indices[0] = (ASTNode*)s_idx1;
                            s_len->index_count = 1;
                            s_len->elementType = DATA_INT;
                            high_value = (ASTNode*)s_len;
                        } else {
                            AST_Literal* r_ref2 = calloc(1, sizeof(AST_Literal));
                            if (!r_ref2) OUT_OF_MEMORY();
                            r_ref2->base.type = AST_LITERAL;
                            r_ref2->base.value.type = TOKEN_IDENTIFIER;
                            r_ref2->base.value.value = strdup(int_rvar_buf);
                            r_ref2->dataType = worker_ret;
                            r_ref2->internal_name = strdup(int_rvar_buf);

                            AST_Literal* shift_lit = calloc(1, sizeof(AST_Literal));
                            if (!shift_lit) OUT_OF_MEMORY();
                            shift_lit->base.type = AST_LITERAL;
                            shift_lit->base.value.type = TOKEN_NUMBER;
                            shift_lit->base.value.value = strdup("32");
                            shift_lit->dataType = DATA_INT;

                            AST_Binary* shr = calloc(1, sizeof(AST_Binary));
                            if (!shr) OUT_OF_MEMORY();
                            shr->base.type = AST_BINARY;
                            shr->left = (ASTNode*)r_ref2;
                            shr->op.type = TOKEN_SHIFT_RIGHT;
                            shr->op.value = strdup(">>");
                            shr->right = (ASTNode*)shift_lit;
                            shr->resultType = DATA_ERROR;

                            AST_Cast* high_cast = calloc(1, sizeof(AST_Cast));
                            if (!high_cast) OUT_OF_MEMORY();
                            high_cast->base.type = AST_CAST;
                            high_cast->operand = (ASTNode*)shr;
                            high_cast->target_type = DATA_INT;
                            high_cast->target_ptr_depth = 0;
                            high_cast->target_class_id = -1;
                            high_cast->source_type = DATA_ERROR;
                            high_cast->source_ptr_depth = 0;

                            high_value = (ASTNode*)high_cast;
                        }

                        AST_Literal* ret_ptr_ref2 = calloc(1, sizeof(AST_Literal));
                        if (!ret_ptr_ref2) OUT_OF_MEMORY();
                        ret_ptr_ref2->base.type = AST_LITERAL;
                        ret_ptr_ref2->base.value.type = TOKEN_IDENTIFIER;
                        ret_ptr_ref2->base.value.value = strdup(ret_ptr_var);
                        ret_ptr_ref2->dataType = DATA_INT;
                        ret_ptr_ref2->internal_name = strdup(ret_ptr_var);

                        AST_Binary* ptr_plus1 = calloc(1, sizeof(AST_Binary));
                        if (!ptr_plus1) OUT_OF_MEMORY();
                        ptr_plus1->base.type = AST_BINARY;
                        ptr_plus1->left = (ASTNode*)ret_ptr_ref2;
                        ptr_plus1->op.type = TOKEN_ADD;
                        ptr_plus1->op.value = strdup("+");

                        AST_Literal* one_lit = calloc(1, sizeof(AST_Literal));
                        if (!one_lit) OUT_OF_MEMORY();
                        one_lit->base.type = AST_LITERAL;
                        one_lit->base.value.type = TOKEN_NUMBER;
                        one_lit->base.value.value = strdup("1");
                        one_lit->dataType = DATA_INT;

                        ptr_plus1->right = (ASTNode*)one_lit;
                        ptr_plus1->resultType = DATA_ERROR;

                        AST_Var* high_ass = calloc(1, sizeof(AST_Var));
                        if (!high_ass) OUT_OF_MEMORY();
                        high_ass->base.type = AST_VAR_ASS;
                        high_ass->base.value.type = TOKEN_IDENTIFIER;
                        high_ass->base.value.value = strdup(ret_ptr_var);
                        high_ass->name = high_ass->base.value;
                        high_ass->type = TOKEN_INT;
                        high_ass->dataType = DATA_INT;
                        high_ass->ptr_depth = -1;
                        high_ass->deref_target = (ASTNode*)ptr_plus1;
                        high_ass->Value = high_value;
                        high_ass->type_param_index = -1;

                        thunk_body_tail->next = (ASTNode*)high_ass;
                        thunk_body_tail = (ASTNode*)high_ass;
                    }

                    AST_Literal* zero_ret = calloc(1, sizeof(AST_Literal));
                    if (!zero_ret) OUT_OF_MEMORY();
                    zero_ret->base.type = AST_LITERAL;
                    zero_ret->base.value.type = TOKEN_NUMBER;
                    zero_ret->base.value.value = strdup("0");
                    zero_ret->dataType = DATA_INT;

                    AST_Return* thunk_ret = calloc(1, sizeof(AST_Return));
                    if (!thunk_ret) OUT_OF_MEMORY();
                    thunk_ret->base.type = AST_RETURN;
                    thunk_ret->base.next = NULL;
                    thunk_ret->returnType = DATA_INT;
                    thunk_ret->value = (ASTNode*)zero_ret;

                    thunk_body_tail->next = (ASTNode*)thunk_ret;

                    AST_Var* env_param = calloc(1, sizeof(AST_Var));
                    if (!env_param) OUT_OF_MEMORY();
                    env_param->base.type = AST_UNINIT_VAR;
                    env_param->base.next = NULL;
                    env_param->base.value.type = TOKEN_IDENTIFIER;
                    env_param->base.value.value = strdup("__env");
                    env_param->name = env_param->base.value;
                    env_param->type = TOKEN_INT;
                    env_param->dataType = DATA_INT;
                    env_param->internal_name = strdup("__env");
                    env_param->ptr_depth = 1;
                    env_param->type_param_index = -1;

                    AST_Function* thunk = calloc(1, sizeof(AST_Function));
                    if (!thunk) OUT_OF_MEMORY();
                    thunk->base.type = AST_FUNCTION;
                    thunk->base.next = NULL;
                    thunk->name.value = strdup(thunk_name);
                    thunk->name.type = TOKEN_IDENTIFIER;
                    thunk->internal_name = strdup(thunk_name);
                    thunk->return_type = DATA_INT;
                    thunk->max_parameters = 4;
                    thunk->parameterType = malloc(sizeof(DataType) * 4);
                    if (!thunk->parameterType) OUT_OF_MEMORY();
                    thunk->parameterType[0] = DATA_INT;
                    thunk->parameterCount = 1;
                    thunk->Parameter = (ASTNode*)env_param;
                    thunk->Code = thunk_body_head;
                    thunk->Return = thunk_ret;
                    thunk->return_Count = 1;
                    thunk->is_nested = true;

                    analyze_node((ASTNode*)thunk);

                    AST_Literal* thunk_ref = calloc(1, sizeof(AST_Literal));
                    if (!thunk_ref) OUT_OF_MEMORY();
                    thunk_ref->base.type = AST_LITERAL;
                    thunk_ref->base.next = NULL;
                    thunk_ref->base.value.type = TOKEN_IDENTIFIER;
                    thunk_ref->base.value.value = strdup(thunk_name);
                    thunk_ref->dataType = DATA_IDENTIFIER;
                    thunk_ref->is_call_function = false;
                    thunk_ref->internal_name = strdup(thunk->internal_name);

                    AST_AddressOf* thunk_addr = calloc(1, sizeof(AST_AddressOf));
                    if (!thunk_addr) OUT_OF_MEMORY();
                    thunk_addr->base.type = AST_ADDRESS_OF;
                    thunk_addr->base.next = NULL;
                    thunk_addr->base.value = thunk_ref->base.value;
                    thunk_addr->operand = (ASTNode*)thunk_ref;
                    thunk_addr->resultType = DATA_INT;
                    thunk_addr->ptr_depth = 1;

                    char env_var_name[64];
                    char ret_var_name[64];
                    snprintf(env_var_name, sizeof(env_var_name),
                             "__mt_env_%d", thunk_id);
                    snprintf(ret_var_name, sizeof(ret_var_name),
                             "__mt_ret_%d", thunk_id);

                    AST_Literal* env_src = calloc(1, sizeof(AST_Literal));
                    if (!env_src) OUT_OF_MEMORY();
                    env_src->base.type = AST_LITERAL;
                    env_src->base.next = NULL;
                    env_src->base.value.type = TOKEN_IDENTIFIER;
                    env_src->base.value.value = strdup(env_var_name);
                    env_src->dataType = DATA_INT;
                    env_src->internal_name = strdup(env_var_name);

                    thunk_addr->base.next = (ASTNode*)env_src;

                    AST_Literal* ret_ref_in_make = calloc(1, sizeof(AST_Literal));
                    if (!ret_ref_in_make) OUT_OF_MEMORY();
                    ret_ref_in_make->base.type = AST_LITERAL;
                    ret_ref_in_make->base.next = NULL;
                    ret_ref_in_make->base.value.type = TOKEN_IDENTIFIER;
                    ret_ref_in_make->base.value.value = strdup(ret_var_name);
                    ret_ref_in_make->dataType = DATA_INT;
                    ret_ref_in_make->internal_name = strdup(ret_var_name);

                    env_src->base.next = (ASTNode*)ret_ref_in_make;

                    AST_NewInstance* fn_ni = calloc(1, sizeof(AST_NewInstance));
                    if (!fn_ni) OUT_OF_MEMORY();
                    fn_ni->base.type = AST_NEW_INSTANCE;
                    fn_ni->class_name = strdup("Function");
                    fn_ni->arguments = (ASTNode*)thunk_addr;
                    fn_ni->constructor_arg_count = 3;

                    AST_Return* make_ret = calloc(1, sizeof(AST_Return));
                    if (!make_ret) OUT_OF_MEMORY();
                    make_ret->base.type = AST_RETURN;
                    make_ret->base.next = NULL;
                    make_ret->returnType = DATA_IDENTIFIER;
                    make_ret->value = (ASTNode*)fn_ni;

                    int ret_slots = 2;
                    if (worker_sig->multi_return != NULL) {
                        MultiReturnType* mrt = worker_sig->multi_return;
                        int mr_total = 0;
                        for (int mi = 0; mi < mrt->count; mi++) {
                            if (mrt->is_variadic[mi]) {
                                mr_total += 8;
                            } else {
                                int sz = S_get_type_size(mrt->types[mi]);
                                if (sz < 4) sz = 4;
                                mr_total += sz;
                            }
                        }
                        if (mr_total < 4) mr_total = 4;
                        ret_slots = (mr_total + 3) / 4;
                    } else if (worker_ret_width > 8) {
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(ni->base.value),
                              "Thread worker '%s' return type >8 bytes "
                              "is not supported", f_name);
                        worker_ret_width = 8;
                    }
                    int env_slots = 1;
                    for (int epi = 0; epi < extra_arg_count; epi++) {
                        DataType ept = (epi < worker_sig->param_count)
                                       ? worker_sig->param_types[epi]
                                       : DATA_INT;
                        const char* epcls = (worker_sig->param_class_names
                                             && epi < worker_sig->param_count)
                                            ? worker_sig->param_class_names[epi]
                                            : NULL;
                        int pw = (is_vararg_param(worker_sig, epi)
                                  || is_array_param(worker_sig, epi))
                                 ? 8 : thread_param_width(ept, epcls);
                        env_slots += pw / 4;
                    }
                    if (env_slots < 2) env_slots = 2;

                    AST_NewArray* ret_new = calloc(1, sizeof(AST_NewArray));
                    if (!ret_new) OUT_OF_MEMORY();
                    ret_new->base.type = AST_NEW_ARRAY;
                    ret_new->elementType = DATA_INT;
                    ret_new->dim_count = 1;
                    ret_new->dim_sizes = malloc(sizeof(int));
                    ret_new->dim_sizes[0] = ret_slots;

                    AST_ArrayDecl* ret_decl = calloc(1, sizeof(AST_ArrayDecl));
                    if (!ret_decl) OUT_OF_MEMORY();
                    ret_decl->base.type = AST_ARRAY_DECL;
                    ret_decl->elementType = DATA_INT;
                    ret_decl->name = strdup(ret_var_name);
                    ret_decl->dim_count = 1;
                    ret_decl->dim_sizes = malloc(sizeof(int));
                    ret_decl->dim_sizes[0] = ret_slots;
                    ret_decl->internal_name = strdup(ret_var_name);
                    ret_decl->init_list = (ASTNode*)ret_new;

                    ASTNode* make_body_head = (ASTNode*)ret_decl;
                    ASTNode* make_body_tail = make_body_head;

                    for (int ri = 0; ri < ret_slots; ri++) {
                        AST_Literal* ret_r = calloc(1, sizeof(AST_Literal));
                        if (!ret_r) OUT_OF_MEMORY();
                        ret_r->base.type = AST_LITERAL;
                        ret_r->base.value.type = TOKEN_IDENTIFIER;
                        ret_r->base.value.value = strdup(ret_var_name);
                        ret_r->dataType = DATA_INT;
                        ret_r->internal_name = strdup(ret_var_name);

                        char ridx_buf[16];
                        snprintf(ridx_buf, sizeof(ridx_buf), "%d", ri);
                        AST_Literal* ridx_lit = calloc(1, sizeof(AST_Literal));
                        if (!ridx_lit) OUT_OF_MEMORY();
                        ridx_lit->base.type = AST_LITERAL;
                        ridx_lit->base.value.type = TOKEN_NUMBER;
                        ridx_lit->base.value.value = strdup(ridx_buf);
                        ridx_lit->dataType = DATA_INT;

                        AST_ArrayAccess* racc = calloc(1, sizeof(AST_ArrayAccess));
                        if (!racc) OUT_OF_MEMORY();
                        racc->base.type = AST_ARRAY_ACCESS;
                        racc->array = (ASTNode*)ret_r;
                        racc->indices = malloc(sizeof(ASTNode*));
                        racc->indices[0] = (ASTNode*)ridx_lit;
                        racc->index_count = 1;
                        racc->elementType = DATA_ERROR;

                        AST_Literal* zero = calloc(1, sizeof(AST_Literal));
                        if (!zero) OUT_OF_MEMORY();
                        zero->base.type = AST_LITERAL;
                        zero->base.value.type = TOKEN_NUMBER;
                        zero->base.value.value = strdup("0");
                        zero->dataType = DATA_INT;

                        AST_ArrayAss* rass = calloc(1, sizeof(AST_ArrayAss));
                        if (!rass) OUT_OF_MEMORY();
                        rass->base.type = AST_ARRAY_ASS;
                        rass->target = (ASTNode*)racc;
                        rass->value = (ASTNode*)zero;
                        rass->elementType = DATA_ERROR;

                        make_body_tail->next = (ASTNode*)rass;
                        make_body_tail = (ASTNode*)rass;
                    }

                    AST_NewArray* env_new = calloc(1, sizeof(AST_NewArray));
                    if (!env_new) OUT_OF_MEMORY();
                    env_new->base.type = AST_NEW_ARRAY;
                    env_new->elementType = DATA_INT;
                    env_new->dim_count = 1;
                    env_new->dim_sizes = malloc(sizeof(int));
                    env_new->dim_sizes[0] = env_slots;

                    AST_ArrayDecl* env_decl = calloc(1, sizeof(AST_ArrayDecl));
                    if (!env_decl) OUT_OF_MEMORY();
                    env_decl->base.type = AST_ARRAY_DECL;
                    env_decl->elementType = DATA_INT;
                    env_decl->name = strdup(env_var_name);
                    env_decl->dim_count = 1;
                    env_decl->dim_sizes = malloc(sizeof(int));
                    env_decl->dim_sizes[0] = env_slots;
                    env_decl->internal_name = strdup(env_var_name);
                    env_decl->init_list = (ASTNode*)env_new;

                    make_body_tail->next = (ASTNode*)env_decl;
                    make_body_tail = (ASTNode*)env_decl;

                    {
                        AST_Literal* env_ref0 = calloc(1, sizeof(AST_Literal));
                        if (!env_ref0) OUT_OF_MEMORY();
                        env_ref0->base.type = AST_LITERAL;
                        env_ref0->base.value.type = TOKEN_IDENTIFIER;
                        env_ref0->base.value.value = strdup(env_var_name);
                        env_ref0->dataType = DATA_INT;
                        env_ref0->internal_name = strdup(env_var_name);

                        AST_Literal* idx0 = calloc(1, sizeof(AST_Literal));
                        if (!idx0) OUT_OF_MEMORY();
                        idx0->base.type = AST_LITERAL;
                        idx0->base.value.type = TOKEN_NUMBER;
                        idx0->base.value.value = strdup("0");
                        idx0->dataType = DATA_INT;

                        AST_ArrayAccess* acc0 = calloc(1, sizeof(AST_ArrayAccess));
                        if (!acc0) OUT_OF_MEMORY();
                        acc0->base.type = AST_ARRAY_ACCESS;
                        acc0->array = (ASTNode*)env_ref0;
                        acc0->indices = malloc(sizeof(ASTNode*));
                        acc0->indices[0] = (ASTNode*)idx0;
                        acc0->index_count = 1;
                        acc0->elementType = DATA_ERROR;

                        AST_Literal* ret_r0 = calloc(1, sizeof(AST_Literal));
                        if (!ret_r0) OUT_OF_MEMORY();
                        ret_r0->base.type = AST_LITERAL;
                        ret_r0->base.value.type = TOKEN_IDENTIFIER;
                        ret_r0->base.value.value = strdup(ret_var_name);
                        ret_r0->dataType = DATA_INT;
                        ret_r0->internal_name = strdup(ret_var_name);

                        AST_Literal* r0_idx = calloc(1, sizeof(AST_Literal));
                        if (!r0_idx) OUT_OF_MEMORY();
                        r0_idx->base.type = AST_LITERAL;
                        r0_idx->base.value.type = TOKEN_NUMBER;
                        r0_idx->base.value.value = strdup("0");
                        r0_idx->dataType = DATA_INT;

                        AST_ArrayAccess* racc0 = calloc(1, sizeof(AST_ArrayAccess));
                        if (!racc0) OUT_OF_MEMORY();
                        racc0->base.type = AST_ARRAY_ACCESS;
                        racc0->array = (ASTNode*)ret_r0;
                        racc0->indices = malloc(sizeof(ASTNode*));
                        racc0->indices[0] = (ASTNode*)r0_idx;
                        racc0->index_count = 1;
                        racc0->elementType = DATA_ERROR;

                        AST_AddressOf* ret_addr = calloc(1, sizeof(AST_AddressOf));
                        if (!ret_addr) OUT_OF_MEMORY();
                        ret_addr->base.type = AST_ADDRESS_OF;
                        ret_addr->operand = (ASTNode*)racc0;
                        ret_addr->resultType = DATA_INT;
                        ret_addr->ptr_depth = 1;

                        AST_ArrayAss* ass0 = calloc(1, sizeof(AST_ArrayAss));
                        if (!ass0) OUT_OF_MEMORY();
                        ass0->base.type = AST_ARRAY_ASS;
                        ass0->target = (ASTNode*)acc0;
                        ass0->value = (ASTNode*)ret_addr;
                        ass0->elementType = DATA_ERROR;

                        make_body_tail->next = (ASTNode*)ass0;
                        make_body_tail = (ASTNode*)ass0;
                    }

                    int cur_slot = 1;
                    for (int pi = 0; pi < extra_arg_count; pi++) {
                        DataType pt = (pi < worker_sig->param_count)
                                      ? worker_sig->param_types[pi]
                                      : DATA_INT;
                        const char* pcls = (worker_sig->param_class_names
                                            && pi < worker_sig->param_count)
                                           ? worker_sig->param_class_names[pi]
                                           : NULL;
                        int pwidth = (is_vararg_param(worker_sig, pi)
                                      || is_array_param(worker_sig, pi))
                                     ? 8 : thread_param_width(pt, pcls);
                        int words = pwidth / 4;

                        char pbuf[32];
                        snprintf(pbuf, sizeof(pbuf), "__mt_p%d_%d", thunk_id, pi);

                        AST_Literal* param_var = calloc(1, sizeof(AST_Literal));
                        if (!param_var) OUT_OF_MEMORY();
                        param_var->base.type = AST_LITERAL;
                        param_var->base.value.type = TOKEN_IDENTIFIER;
                        param_var->base.value.value = strdup(pbuf);
                        param_var->dataType = (is_vararg_param(worker_sig, pi)
                                               || is_array_param(worker_sig, pi))
                                              ? DATA_INT64 : pt;
                        param_var->internal_name = strdup(pbuf);

                        AST_AddressOf* p_addr = calloc(1, sizeof(AST_AddressOf));
                        if (!p_addr) OUT_OF_MEMORY();
                        p_addr->base.type = AST_ADDRESS_OF;
                        p_addr->operand = (ASTNode*)param_var;
                        p_addr->resultType = DATA_INT;
                        p_addr->ptr_depth = 1;

                        AST_Cast* p_as_intptr = calloc(1, sizeof(AST_Cast));
                        if (!p_as_intptr) OUT_OF_MEMORY();
                        p_as_intptr->base.type = AST_CAST;
                        p_as_intptr->operand = (ASTNode*)p_addr;
                        p_as_intptr->target_type = DATA_INT;
                        p_as_intptr->target_ptr_depth = 1;
                        p_as_intptr->target_class_id = -1;
                        p_as_intptr->source_type = DATA_INT;
                        p_as_intptr->source_ptr_depth = 1;

                        for (int wi = 0; wi < words; wi++) {
                            AST_Literal* env_ref = calloc(1, sizeof(AST_Literal));
                            if (!env_ref) OUT_OF_MEMORY();
                            env_ref->base.type = AST_LITERAL;
                            env_ref->base.value.type = TOKEN_IDENTIFIER;
                            env_ref->base.value.value = strdup(env_var_name);
                            env_ref->dataType = DATA_INT;
                            env_ref->internal_name = strdup(env_var_name);

                            char idx_buf[16];
                            snprintf(idx_buf, sizeof(idx_buf), "%d", cur_slot + wi);
                            AST_Literal* idx_lit = calloc(1, sizeof(AST_Literal));
                            if (!idx_lit) OUT_OF_MEMORY();
                            idx_lit->base.type = AST_LITERAL;
                            idx_lit->base.value.type = TOKEN_NUMBER;
                            idx_lit->base.value.value = strdup(idx_buf);
                            idx_lit->dataType = DATA_INT;

                            AST_ArrayAccess* env_acc = calloc(1, sizeof(AST_ArrayAccess));
                            if (!env_acc) OUT_OF_MEMORY();
                            env_acc->base.type = AST_ARRAY_ACCESS;
                            env_acc->array = (ASTNode*)env_ref;
                            env_acc->indices = malloc(sizeof(ASTNode*));
                            env_acc->indices[0] = (ASTNode*)idx_lit;
                            env_acc->index_count = 1;
                            env_acc->elementType = DATA_ERROR;

                            AST_Literal* w_idx = calloc(1, sizeof(AST_Literal));
                            if (!w_idx) OUT_OF_MEMORY();
                            w_idx->base.type = AST_LITERAL;
                            w_idx->base.value.type = TOKEN_NUMBER;
                            char wbuf[16];
                            snprintf(wbuf, sizeof(wbuf), "%d", wi);
                            w_idx->base.value.value = strdup(wbuf);
                            w_idx->dataType = DATA_INT;

                            AST_ArrayAccess* src_word = calloc(1, sizeof(AST_ArrayAccess));
                            if (!src_word) OUT_OF_MEMORY();
                            src_word->base.type = AST_ARRAY_ACCESS;
                            src_word->array = (ASTNode*)p_as_intptr;
                            src_word->indices = malloc(sizeof(ASTNode*));
                            src_word->indices[0] = (ASTNode*)w_idx;
                            src_word->index_count = 1;
                            src_word->elementType = DATA_INT;

                            AST_ArrayAss* w_ass = calloc(1, sizeof(AST_ArrayAss));
                            if (!w_ass) OUT_OF_MEMORY();
                            w_ass->base.type = AST_ARRAY_ASS;
                            w_ass->target = (ASTNode*)env_acc;
                            w_ass->value = (ASTNode*)src_word;
                            w_ass->elementType = DATA_ERROR;

                            make_body_tail->next = (ASTNode*)w_ass;
                            make_body_tail = (ASTNode*)w_ass;
                        }
                        cur_slot += words;
                    }

                    make_body_tail->next = (ASTNode*)make_ret;

                    ASTNode* make_param_head = NULL;
                    ASTNode* make_param_tail = NULL;
                    for (int pi = 0; pi < extra_arg_count; pi++) {
                        char pbuf[32];
                        snprintf(pbuf, sizeof(pbuf), "__mt_p%d_%d",
                                 thunk_id, pi);
                        DataType pt = (pi < worker_sig->param_count)
                                      ? worker_sig->param_types[pi]
                                      : DATA_INT;
                        if (is_vararg_param(worker_sig, pi)
                            || is_array_param(worker_sig, pi)) {
                            pt = DATA_INT64;
                        }
                        AST_Var* param = calloc(1, sizeof(AST_Var));
                        if (!param) OUT_OF_MEMORY();
                        param->base.type = AST_UNINIT_VAR;
                        param->base.next = NULL;
                        param->base.value.type = TOKEN_IDENTIFIER;
                        param->base.value.value = strdup(pbuf);
                        param->name = param->base.value;
                        param->type = TOKEN_INT;
                        param->dataType = pt;
                        param->internal_name = strdup(pbuf);
                        param->type_param_index = -1;
                        if (!make_param_head) {
                            make_param_head = make_param_tail = (ASTNode*)param;
                        } else {
                            make_param_tail->next = (ASTNode*)param;
                            make_param_tail = (ASTNode*)param;
                        }
                    }

                    AST_Function* make_fn = calloc(1, sizeof(AST_Function));
                    if (!make_fn) OUT_OF_MEMORY();
                    make_fn->base.type = AST_FUNCTION;
                    make_fn->base.next = NULL;
                    make_fn->name.value = strdup(make_name);
                    make_fn->name.type = TOKEN_IDENTIFIER;
                    make_fn->internal_name = strdup(make_name);
                    make_fn->return_type = DATA_IDENTIFIER;
                    make_fn->return_class_name = strdup("Function");
                    make_fn->max_parameters = extra_arg_count + 4;
                    make_fn->parameterType = malloc(sizeof(DataType) * (extra_arg_count + 4));
                    if (!make_fn->parameterType) OUT_OF_MEMORY();
                    for (int pi = 0; pi < extra_arg_count; pi++) {
                        DataType pt = (pi < worker_sig->param_count)
                                      ? worker_sig->param_types[pi]
                                      : DATA_INT;
                        if (is_vararg_param(worker_sig, pi)
                            || is_array_param(worker_sig, pi)) {
                            pt = DATA_INT64;
                        }
                        make_fn->parameterType[pi] = pt;
                    }
                    make_fn->parameterCount = extra_arg_count;
                    make_fn->Parameter = make_param_head;
                    make_fn->Code = make_body_head;
                    make_fn->Return = make_ret;
                    make_fn->return_Count = 1;

                    analyze_node((ASTNode*)make_fn);

                    AST_Literal* make_call = calloc(1, sizeof(AST_Literal));
                    if (!make_call) OUT_OF_MEMORY();
                    make_call->base.type = AST_LITERAL;
                    make_call->base.next = NULL;
                    make_call->base.value.type = TOKEN_IDENTIFIER;
                    make_call->base.value.value = strdup(make_name);
                    make_call->dataType = DATA_IDENTIFIER;
                    make_call->is_call_function = true;
                    make_call->internal_name = strdup(make_name);
                    make_call->call_function_Parameter = make_args_head;
                    make_call->call_function_ParameterCount = extra_arg_count;
                    if (extra_arg_count > 0) {
                        make_call->param_byte_sizes = malloc(sizeof(int) * extra_arg_count);
                        if (!make_call->param_byte_sizes) OUT_OF_MEMORY();
                        for (int pi = 0; pi < extra_arg_count; pi++) {
                            bool is_va = is_vararg_param(worker_sig, pi);
                            bool is_arr = is_array_param(worker_sig, pi);
                            if (is_va) {
                                make_call->param_byte_sizes[pi] = 4;
                            } else if (is_arr) {
                                make_call->param_byte_sizes[pi] = 8;
                            } else {
                                DataType pt = (pi < worker_sig->param_count)
                                              ? worker_sig->param_types[pi] : DATA_INT;
                                int sz = S_get_type_size(pt);
                                make_call->param_byte_sizes[pi] = (sz > 4) ? sz : 4;
                            }
                        }
                    }

                    func_ref->next = NULL;
                    AST_Free(func_ref);

                    ni->arguments = (ASTNode*)make_call;
                    ni->constructor_arg_count = 1;
                    if (worker_sig->multi_return != NULL) {
                        ni->mr_type = worker_sig->multi_return;
                    }
                }
            }

            if (ni->class_name != NULL
                && strcmp(ni->class_name, "Thread") == 0
                && ni->arguments != NULL) {
                ASTNode* wrapped = sem_wrap_function_value(ni->arguments);
                if (wrapped != NULL) {
                    wrapped->next = ni->arguments->next;
                    ni->arguments = wrapped;
                }
            }

            ASTNode** prev = &ni->arguments;
            ASTNode* arg = ni->arguments;
            while (arg) {
                ASTNode* next = arg->next;
                if (arg->type == AST_LITERAL
                    && !((AST_Literal*)arg)->is_call_function
                    && ((AST_Literal*)arg)->base.value.type == TOKEN_IDENTIFIER) {
                    Symbol* arg_sym = lookup_symbol(
                        ((AST_Literal*)arg)->base.value.value);
                    if (arg_sym && arg_sym->is_function && arg_sym->func_sig) {
                        AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
                        if (!addr) OUT_OF_MEMORY();
                        addr->base.type   = AST_ADDRESS_OF;
                        addr->base.next   = next;
                        addr->base.value  = ((AST_Literal*)arg)->base.value;
                        addr->operand     = arg;
                        addr->resultType  = DATA_INT;
                        addr->ptr_depth   = 1;
                        addr->target_func_sig = deep_copy_func_sig(arg_sym->func_sig);
                        arg->next = NULL;
                        *prev = (ASTNode*)addr;
                        prev = &addr->base.next;
                        arg = next;
                        continue;
                    }
                }
                prev = &arg->next;
                arg = next;
            }
        }

        ClassEntry* cls = find_class(ni->class_name);
        if (!cls && current_class_name) {
            int len = strlen(current_class_name) + 1
                    + strlen(ni->class_name) + 1;
            char* full_name = malloc(len);
            snprintf(full_name, len, "%s_%s",
                     current_class_name, ni->class_name);
            cls = find_class(full_name);
            if (cls) {
                free(ni->class_name);
                ni->class_name = full_name;
            } else {
                free(full_name);
            }
        }

        if (cls && cls->is_generic && ni->generic_type_arg_count > 0) {
            ClassEntry* inst_cls = instantiate_generic_class(
                cls, ni->generic_type_args, ni->generic_type_arg_names,
                ni->generic_type_arg_count);
            if (inst_cls) {
                free(ni->class_name);
                ni->class_name = strdup(inst_cls->name);
                cls = inst_cls;
            }
        }

        if (!cls) {
            ClassEntry* generic_cls = find_class(ni->class_name);
            if (generic_cls && generic_cls->is_generic && ni->generic_type_arg_count > 0) {
                cls = instantiate_generic_class(
                    generic_cls,
                    ni->generic_type_args,
                    ni->generic_type_arg_names,
                    ni->generic_type_arg_count);
                if (cls) {
                    free(ni->class_name);
                    ni->class_name = strdup(cls->name);
                }
            }
        }

        if (!cls) {
            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                  SEMLOC(ni->base.value),
                  "unknown class '%s'",
                  ni->class_name);
            ni->instance_size = 0;
            break;
        }

        if (cls->is_generic && ni->generic_type_arg_count == 0
            && !ni->generic_infer) {
            ERROR(ERR_SEM_TYPE_MISMATCH,
                  SEMLOC(ni->base.value),
                  "generic class '%s' requires type argument(s)",
                  ni->class_name);
            ni->instance_size = 0;
            break;
            }

        if (cls->has_outer && cls->constructor != NULL) {
            Symbol* cur_this = lookup_symbol("this");
            ASTNode* outer_expr = NULL;
            if (cur_this && cur_this->class_name
                && strcmp(cur_this->class_name, cls->outer_class_name) == 0) {
                AST_This* this_node = calloc(1, sizeof(AST_This));
                this_node->base.type = AST_THIS;
                this_node->base.value = ni->base.value;
                outer_expr = (ASTNode*)this_node;
            } else {
                AST_Literal* zero = calloc(1, sizeof(AST_Literal));
                zero->base.type = AST_LITERAL;
                zero->base.value.type = TOKEN_NUMBER;
                zero->base.value.value = strdup("0");
                zero->base.value.line = ni->base.value.line;
                zero->base.value.column = ni->base.value.column;
                zero->dataType = DATA_INT;
                zero->is_call_function = false;
                outer_expr = (ASTNode*)zero;
            }
            outer_expr->next = ni->arguments;
            ni->arguments = outer_expr;
            ni->constructor_arg_count += 1;
        }

        ni->instance_size = cls->total_size;
        ni->class_id = cls->class_id;
        if (cls->vtable_size > 0) {
            ni->vtable_label = class_vtable_label(ni->class_name);
        }
        if (ni->field_inits) {
            for (int k = 0; k < ni->field_init_count; k++)
                free(ni->field_inits[k].value_str);
            free(ni->field_inits);
        }
        ni->field_init_count = 0;
        ni->field_inits = NULL;

        int field_cnt = 0;
        for (int i = 0; i < cls->member_count; i++) {
            if (!cls->members[i]->is_method) field_cnt++;
        }
        if (field_cnt > 0) {
            ni->field_inits = calloc(field_cnt, sizeof(FieldInit));
            int idx = 0;
            for (int i = 0; i < cls->member_count; i++) {
                MemberInfo* mi = cls->members[i];
                if (mi->is_method) continue;
                ni->field_inits[idx].offset = mi->offset;
                ni->field_inits[idx].type = mi->type;
                ni->field_inits[idx].value_str =
                    mi->init_value_str ? strdup(mi->init_value_str) : NULL;
                idx++;
            }
            ni->field_init_count = field_cnt;
        }

        AST_Function* matched_ctor = NULL;
        const char* ctor_name = cls->constructor
                                ? cls->constructor->name.value
                                : NULL;

        if (ctor_name != NULL) {
            for (int ci = 0; ci < cls->member_count; ci++) {
                MemberInfo* ctor_mi = cls->members[ci];
                if (!ctor_mi->is_method || !ctor_mi->func) continue;
                if (strcmp(ctor_mi->name, ctor_name) != 0) continue;
                int expected_user_args = ctor_mi->func->parameterCount - 1;
                if (expected_user_args == ni->constructor_arg_count) {
                    matched_ctor = ctor_mi->func;
                    break;
                }
            }
        }

        if (matched_ctor) {
            ni->has_constructor = true;
            ni->constructor_name = strdup(matched_ctor->internal_name);
        } else if (cls->constructor) {
            int expected_user_args = cls->constructor->parameterCount - 1;
            ERROR(ERR_SEM_ARG_COUNT_MISMATCH,
                  SEMLOC(ni->base.value),
                  "constructor expects %d argument(s), got %d",
                  expected_user_args, ni->constructor_arg_count);
            ni->has_constructor = false;
        } else {
            if (ni->constructor_arg_count > 0) {
                ERROR(ERR_SEM_METHOD_NOT_FOUND,
                      SEMLOC(ni->base.value),
                      "class '%s' has no constructor",
                      ni->class_name);
            }
            ni->has_constructor = false;
        }
        break;
    }

    case AST_MEMBER_ASS: {
        AST_MemberAss* mass = (AST_MemberAss*)temp;

        analyze_node(mass->object);

        ClassEntry* target_class = NULL;
        Symbol* obj_sym = NULL;

        if (mass->object->type == AST_THIS) {
            obj_sym = lookup_symbol("this");
            if (!obj_sym || !obj_sym->class_name) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "'this' used outside of class context");
                break;
            }
            if (current_analyzing_function
                && current_analyzing_function->is_const_method) {
                ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                      SEMLOC(mass->base.value),
                      "cannot modify field '%s' in const method",
                      mass->member_name);
            }
            target_class = find_class(obj_sym->class_name);
        } else if (mass->object->type == AST_LITERAL) {
            AST_Literal* obj_lit = (AST_Literal*)mass->object;
            if (obj_lit->is_call_function
                && obj_lit->call_result_class_name != NULL) {
                target_class = find_class(obj_lit->call_result_class_name);
                if (target_class != NULL)
                    goto member_assign_resolved;
            }
            if (strcmp(obj_lit->base.value.value, "this") == 0) {
                obj_sym = lookup_symbol("this");
                if (obj_sym && obj_sym->class_name)
                    target_class = find_class(obj_sym->class_name);
            } else {
                obj_sym = lookup_symbol(obj_lit->base.value.value);
                if (!obj_sym || !obj_sym->class_name) {
                    DataType alias_type = resolve_type_alias(obj_lit->base.value.value);
                    if (alias_type != DATA_ERROR) {
                        ERROR(ERR_SEM_TYPE_MISMATCH,
                              SEMLOC(mass->base.value),
                              "cannot assign to enum member through member access");
                        break;
                    }
                }
                if (obj_sym && obj_sym->class_name)
                    target_class = find_class(obj_sym->class_name);
            }
        } else if (mass->object->type == AST_DEREF) {
            AST_Deref* d = (AST_Deref*)mass->object;
            if (d->operand->type == AST_LITERAL) {
                AST_Literal* lit = (AST_Literal*)d->operand;
                if (lit->base.value.type == TOKEN_IDENTIFIER) {
                    obj_sym = lookup_symbol(lit->base.value.value);
                    if (obj_sym && obj_sym->class_name) {
                        target_class = find_class(obj_sym->class_name);
                    }
                    if (obj_sym && (obj_sym->level_const_mask & 1u)) {
                        ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                              SEMLOC(mass->base.value),
                              "cannot modify field '%s' through "
                              "pointer to const '%s'",
                              mass->member_name, lit->base.value.value);
                    }
                }
            }
            else if (d->operand->type == AST_THIS) {
                obj_sym = lookup_symbol("this");
                if (obj_sym && obj_sym->class_name) {
                    target_class = find_class(obj_sym->class_name);
                }
            }
            if (!target_class) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot determine class from dereferenced pointer");
                break;
            }
        } else if (mass->object->type == AST_MEMBER_ACCESS) {
            const char* cls_name = infer_static_class_name(mass->object);
            if (cls_name == NULL) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot resolve class of nested member assignment");
                break;
            }
            target_class = find_class(cls_name);
            if (!target_class) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot find class '%s'", cls_name);
                break;
            }
        } else if (mass->object->type == AST_ARRAY_ACCESS) {
            const char* cls_name = infer_static_class_name(mass->object);
            if (cls_name == NULL) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot resolve element class of array assignment");
                break;
            }
            target_class = find_class(cls_name);
            if (!target_class) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot find class '%s'", cls_name);
                break;
            }
        } else {
            const char* cls_name = infer_static_class_name(mass->object);
            if (cls_name != NULL) {
                target_class = find_class(cls_name);
            }
            if (target_class == NULL) {
                ERROR(ERR_SEM_CLASS_NOT_FOUND,
                      SEMLOC(mass->base.value),
                      "cannot determine class of complex member assignment target");
                break;
            }
        }

        if (!target_class) {
            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                  SEMLOC(mass->base.value),
                  "cannot determine class for member assign");
            break;
        }

        member_assign_resolved:
        int found = 0;
        for (int i = 0; i < target_class->member_count; i++) {
            MemberInfo* mi = target_class->members[i];
            if (!mi->is_method && strcmp(mi->name, mass->member_name) == 0) {
                mass->member_type = mi->type;
                mass->value_is_array = (mi->dim_count > 0
                                        || mi->is_dynamic_array
                                        || mi->ptr_depth > 0);
                mass->member_offset = mi->offset;
                found = 1;

                if (mi->is_static) {
                    free(mass->member_name);
                    int gname_len = (int)strlen(target_class->name) + 1
                                  + (int)strlen(mi->name) + 1;
                    mass->member_name = malloc((size_t)gname_len);
                    if (!mass->member_name) OUT_OF_MEMORY();
                    snprintf(mass->member_name, (size_t)gname_len,
                             "%s_%s", target_class->name, mi->name);
                    break;
                }

                bool in_ctor = (current_analyzing_function
                                && current_class
                                && current_analyzing_function->name.value
                                && current_class->name
                                && strcmp(current_analyzing_function->name.value,
                                          current_class->name) == 0);

                if (mi->is_const && !in_ctor) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(mass->base.value),
                          "cannot assign to const field '%s'",
                          mass->member_name);
                }

                if (expr_is_const_object(mass->object)) {
                    ERROR(ERR_SEM_CANNOT_ASSIGN_CONST,
                          SEMLOC(mass->base.value),
                          "cannot modify field '%s' of const object",
                          mass->member_name);
                }

                if (mi->field_modifier == 2) {
                    ClassEntry* field_owner = target_class;
                    if (mi->inherited_from != NULL) {
                        field_owner = mi->inherited_from;
                    }
                    if (current_class != field_owner) {
                        ERROR(ERR_SEM_FIELD_NOT_FOUND,
                              SEMLOC(mass->base.value),
                              "cannot access private field '%s'",
                              mass->member_name);
                    }
                }
                else if (mi->field_modifier == 1) {
                    ClassEntry* field_owner = target_class;
                    if (mi->inherited_from != NULL) {
                        field_owner = mi->inherited_from;
                    }
                    if (!current_class || !is_subclass_of(current_class, field_owner)) {
                        ERROR(ERR_SEM_FIELD_NOT_FOUND,
                              SEMLOC(mass->base.value),
                              "cannot access protected field '%s'",
                              mass->member_name);
                    }
                }
                break;
            }
        }

        if (!found) {
            ERROR(ERR_SEM_FIELD_NOT_FOUND,
                  SEMLOC(mass->base.value),
                  "field '%s' not found in class '%s'",
                  mass->member_name, target_class->name);
            break;
        }

        analyze_node(mass->value);
        break;
    }

    case AST_ENUM_DEF: {
        AST_EnumDef* enum_def = (AST_EnumDef*)temp;
        const char* class_prefix = current_class ? current_class->name : NULL;

        if (enum_def->name != NULL) {
            char qual[256];
            if (class_prefix) {
                snprintf(qual, sizeof(qual), "%s.%s", class_prefix, enum_def->name);
            } else {
                snprintf(qual, sizeof(qual), "%s", enum_def->name);
            }
            Symbol* existing = lookup_symbol(qual);
            if (existing != NULL) {
                ERROR(ERR_SEM_DUPLICATE_VAR,
                      SEMLOC(enum_def->base.value),
                      "enum name '%s' conflicts with existing symbol",
                      qual);
            }
            add_type_alias(qual, DATA_INT);
            if (class_prefix && resolve_type_alias(enum_def->name) == DATA_ERROR) {
                add_type_alias(enum_def->name, DATA_INT);
            }
        }

        int current_value = 0;
        for (int i = 0; i < enum_def->member_count; i++) {
            AST_EnumMember* member = enum_def->members[i];

            if (member->has_explicit_value) {
                current_value = member->value;
            } else {
                member->value = current_value;
            }

            char qualified_name[256];
            if (class_prefix) {
                snprintf(qualified_name, sizeof(qualified_name), "%s.%s.%s",
                         class_prefix,
                         enum_def->name ? enum_def->name : "_anon_enum",
                         member->name);
            } else {
                snprintf(qualified_name, sizeof(qualified_name), "%s.%s",
                         enum_def->name ? enum_def->name : "_anon_enum",
                         member->name);
            }

            Symbol* existing = lookup_symbol(qualified_name);
            if (existing != NULL) {
                ERROR(ERR_SEM_DUPLICATE_VAR,
                      SEMLOC(member->base.value),
                      "duplicate enum member '%s'",
                      qualified_name);
                current_value++;
                continue;
            }

            char value_str[32];
            snprintf(value_str, sizeof(value_str), "%d", member->value);

            Symbol* sym = create_symbol(
                (Token){.value = member->name, .type = TOKEN_IDENTIFIER},
                DATA_INT,
                qualified_name,
                value_str
            );
            sym->const_value_str = strdup(value_str);
            insert_symbol(sym);

            if (class_prefix && enum_def->name != NULL) {
                char compat_name[256];
                snprintf(compat_name, sizeof(compat_name), "%s.%s",
                         enum_def->name, member->name);
                if (lookup_symbol(compat_name) == NULL) {
                    Symbol* compat = create_symbol(
                        (Token){.value = member->name, .type = TOKEN_IDENTIFIER},
                        DATA_INT,
                        compat_name,
                        value_str
                    );
                    compat->const_value_str = strdup(value_str);
                    insert_symbol(compat);
                }
            }

            current_value = member->value + 1;
        }
        break;
    }

    case AST_ENUM_MEMBER:
        break;

    case AST_THIS: {
        AST_This* tn = (AST_This*)temp;
        if (!current_class) {
            ERROR(ERR_SEM_CLASS_NOT_FOUND,
                  SEMLOC(tn->base.value),
                  "'this' used outside of class context");
            tn->instance_size = 0;
        } else {
            tn->instance_size = current_class->total_size;
        }
        break;
    }

    case AST_RETURN: {
        AST_Return* return_node = (AST_Return*)temp;

        if (return_node->value != NULL
            && current_analyzing_function != NULL
            && current_analyzing_function->return_class_name != NULL
            && strcmp(current_analyzing_function->return_class_name, "Function") == 0) {
            ASTNode* wrapped = sem_wrap_function_value(return_node->value);
            if (wrapped != NULL) {
                return_node->value = wrapped;

                FuncSignature* inferred_sig = NULL;
                if (wrapped->type == AST_NEW_INSTANCE) {
                    AST_NewInstance* ni = (AST_NewInstance*)wrapped;
                    if (ni->arguments != NULL
                        && ni->arguments->type == AST_ADDRESS_OF) {
                        AST_AddressOf* addr = (AST_AddressOf*)ni->arguments;
                        if (addr->target_func_sig != NULL) {
                            inferred_sig = addr->target_func_sig;
                        }
                    }
                }
                if (inferred_sig != NULL) {
                    if (current_analyzing_function->ret_func_sig != NULL) {
                        free_func_sig(current_analyzing_function->ret_func_sig);
                    }
                    current_analyzing_function->ret_func_sig =
                        deep_copy_func_sig(inferred_sig);

                    Symbol* fn_sym = lookup_symbol(
                        current_analyzing_function->name.value);
                    if (fn_sym != NULL && fn_sym->func_sig != NULL) {
                        if (fn_sym->func_sig->ret_func_sig != NULL) {
                            free_func_sig(fn_sym->func_sig->ret_func_sig);
                        }
                        fn_sym->func_sig->ret_func_sig =
                            deep_copy_func_sig(inferred_sig);
                    }
                }
            }
        }

        analyze_node(return_node->value);

        if (return_node->value != NULL && return_node->returnType == DATA_STRING) {
            DataType val_type = get_expression_type(return_node->value);
            if (val_type == DATA_STR) {
                AST_Literal* call = calloc(1, sizeof(AST_Literal));
                call->base.type = AST_LITERAL;
                call->base.next = NULL;
                call->base.value.type = TOKEN_IDENTIFIER;
                call->base.value.value = strdup("_str_to_string");
                call->dataType = DATA_STRING;
                call->internal_name = strdup("_str_to_string");
                call->is_call_function = true;
                call->call_function_Parameter = return_node->value;
                call->call_function_ParameterCount = 1;
                call->param_byte_sizes = malloc(sizeof(int));
                call->param_byte_sizes[0] = 8;
                return_node->value = (ASTNode*)call;
            }
        }

        if (return_node->value != NULL && return_node->returnType == DATA_STR) {
            DataType expr_type = get_expression_type(return_node->value);
            if (expr_type == DATA_STRING) {
                AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
                conv->base.type = AST_STR_FROM_STRING;
                conv->base.next = NULL;
                conv->base.value = return_node->base.value;
                conv->source = return_node->value;
                return_node->value = (ASTNode*)conv;
            }
        }

        if (current_analyzing_function &&
            current_analyzing_function->returns_array) {
            if (return_node->value == NULL) {
                ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                      SEMLOC(return_node->base.value),
                      "array-returning function must return an array value");
            }
            else {
                ASTNode* val = return_node->value;
                DataType expr_type = get_expression_type(val);
                if (val->type == AST_LITERAL) {
                    AST_Literal* lit = (AST_Literal*)val;
                    Symbol* sym = lookup_symbol(lit->base.value.value);
                    if (sym && sym->dim_count > 0) {
                        if (sym->dim_count != current_analyzing_function->return_dim_count) {
                            ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                                  SEMLOC(return_node->base.value),
                                  "return array dimension mismatch: "
                                  "expected %d, got %d",
                                  current_analyzing_function->return_dim_count,
                                  sym->dim_count);
                        }

                        if (!types_compatible(
                                current_analyzing_function->return_element_type,
                                sym->type)) {
                            ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                                  SEMLOC(return_node->base.value),
                                  "return array element type mismatch: "
                                  "expected '%s', got '%s'",
                                  data_type_to_string(
                                      current_analyzing_function->return_element_type),
                                  data_type_to_string(sym->type));
                        }
                    }
                } else if (val->type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* acc = (AST_ArrayAccess*)val;
                    if (!types_compatible(
                            current_analyzing_function->return_element_type,
                            acc->elementType)) {
                        ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                              SEMLOC(return_node->base.value),
                              "return array element type mismatch: "
                              "expected '%s', got '%s'",
                              data_type_to_string(
                                  current_analyzing_function->return_element_type),
                              data_type_to_string(acc->elementType));
                    }
                } else if (val->type == AST_NEW_ARRAY) {
                    AST_NewArray* narr = (AST_NewArray*)val;
                    if (!types_compatible(
                            current_analyzing_function->return_element_type,
                            narr->elementType))
                    {
                        ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                              SEMLOC(return_node->base.value),
                              "return array element type mismatch: "
                              "expected '%s', got '%s'",
                              data_type_to_string(
                                  current_analyzing_function->return_element_type),
                              data_type_to_string(narr->elementType));
                    }
                } else {
                    ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                          SEMLOC(return_node->base.value),
                          "array-returning function requires an array expression");
                }
            }
            break;
        }

        if (return_node->value != NULL && return_node->returnType == DATA_STR) {
            DataType expr_type = get_expression_type(return_node->value);
            if (expr_type == DATA_STRING) {
                AST_StrFromString* conv = calloc(1, sizeof(AST_StrFromString));
                conv->base.type = AST_STR_FROM_STRING;
                conv->base.next = NULL;
                conv->base.value = return_node->base.value;
                conv->source = return_node->value;
                return_node->value = (ASTNode*)conv;
            }
        }

        if (current_analyzing_function &&
            current_analyzing_function->return_ptr_depth > 0) {
            if (return_node->value == NULL) {
                ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                      SEMLOC(return_node->base.value),
                      "function returning pointer must return a value");
            }
            break;
        }

        if (return_node->returnType == DATA_VOID) {
            if (return_node->value != NULL) {
                if (!(current_analyzing_function
                    && current_analyzing_function->is_generic)) {
                    ERROR(ERR_SEM_VOID_VALUE,
                          SEMLOC(return_node->base.value),
                          "void function should not return a value");
                }
            }
        } else {
            if (return_node->value == NULL) {
                if (current_analyzing_function
                    && current_analyzing_function->is_generic
                    && return_node->returnType == DATA_IDENTIFIER) {
                } else {
                    ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                          SEMLOC(return_node->base.value),
                          "non-void function must return a value");
                }
            } else {
                DataType ret_expr_type = get_expression_type(return_node->value);

                if (return_node->returnType == DATA_STRING && ret_expr_type == DATA_STR) {
                    AST_Literal* call = calloc(1, sizeof(AST_Literal));
                    call->base.type = AST_LITERAL;
                    call->base.next = NULL;
                    call->base.value.type = TOKEN_IDENTIFIER;
                    call->base.value.value = strdup("_str_to_string");
                    call->dataType = DATA_STRING;
                    call->internal_name = strdup("_str_to_string");
                    call->is_call_function = true;
                    call->call_function_Parameter = return_node->value;
                    call->call_function_ParameterCount = 1;
                    return_node->value = (ASTNode*)call;
                    ret_expr_type = DATA_STRING;
                }

                if (return_node->returnType == DATA_IDENTIFIER
                    && ret_expr_type == DATA_IDENTIFIER
                    && current_analyzing_function != NULL
                    && current_analyzing_function->return_class_name != NULL) {
                    char decl_ret_buf[192];
                    const char* dcls = current_analyzing_function->return_class_name;
                    if (strchr(dcls, '$') == NULL
                        && current_analyzing_function->return_generic_type_arg_count > 0
                        && current_analyzing_function->return_generic_type_arg_names != NULL) {
                        int nb = snprintf(decl_ret_buf, sizeof(decl_ret_buf),
                                          "%s", dcls);
                        for (int qi = 0;
                             qi < current_analyzing_function->return_generic_type_arg_count
                                 && nb > 0
                                 && nb < (int)sizeof(decl_ret_buf);
                             qi++) {
                            const char* qn =
                                current_analyzing_function
                                    ->return_generic_type_arg_names[qi];
                            nb += snprintf(decl_ret_buf + nb,
                                           sizeof(decl_ret_buf) - (size_t)nb,
                                           "%s%s",
                                           (qi == 0) ? "$" : "_",
                                           qn ? qn : "int");
                        }
                        if (nb > 0) dcls = decl_ret_buf;
                    }
                    if (strchr(dcls, '$') != NULL) {
                        const char* rv_cls =
                            infer_static_class_name(return_node->value);
                        if (rv_cls != NULL
                            && !generic_variance_assign_ok(dcls, rv_cls)) {
                            ERROR(ERR_SEM_INCOMPATIBLE_TYPES,
                                  SEMLOC(return_node->base.value),
                                  "cannot return '%s' as '%s': generic arguments are not compatible under variance",
                                  rv_cls, dcls);
                        }
                    }
                }
                if (!types_compatible(return_node->returnType, ret_expr_type)
                    && ret_expr_type != DATA_ERROR) {
                    ERROR(ERR_SEM_RETURN_TYPE_MISMATCH,
                          SEMLOC(return_node->base.value),
                          "return type mismatch: expected '%s', got '%s'",
                          data_type_to_string(return_node->returnType),
                          data_type_to_string(ret_expr_type));
                } else if (ret_expr_type != return_node->returnType
                           && ret_expr_type != DATA_ERROR) {
                    bool rt_num = is_int_type(return_node->returnType)
                               || is_uint_type(return_node->returnType)
                               || is_float_type(return_node->returnType)
                               || is_ufloat_type(return_node->returnType);
                    bool et_num = is_int_type(ret_expr_type)
                               || is_uint_type(ret_expr_type)
                               || is_float_type(ret_expr_type)
                               || is_ufloat_type(ret_expr_type);
                    if (rt_num && et_num) {
                        ASTNode* rv = return_node->value;
                        AST_Cast* cast = calloc(1, sizeof(AST_Cast));
                        if (!cast) OUT_OF_MEMORY();
                        cast->base.type = AST_CAST;
                        cast->base.next = NULL;
                        cast->base.value = rv->value;
                        cast->operand = rv;
                        cast->target_type = return_node->returnType;
                        cast->target_class_name = NULL;
                        cast->target_class_id = -1;
                        cast->target_ptr_depth = 0;
                        cast->target_func_sig = NULL;
                        cast->target_dim_count = 0;
                        cast->target_dim_sizes = NULL;
                        cast->source_type = ret_expr_type;
                        cast->source_ptr_depth = 0;
                        cast->source_func_sig = NULL;
                        cast->is_reinterpret = false;
                        cast->is_checked = false;
                        return_node->value = (ASTNode*)cast;
                    }
                }
            }
        }
        break;
    }

    case AST_STR_FROM_STRING: {
        AST_StrFromString* conv = (AST_StrFromString*)temp;
        if (conv->source) analyze_node(conv->source);
        break;
    }

    case AST_MULTI_RETURN: {
        AST_MultiReturn* mr = (AST_MultiReturn*)temp;
        if (!current_analyzing_function || !current_analyzing_function->multi_return) {
            ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(mr->base.value),
                  "multi-return used in non-multi-return function");
            break;
        }
        MultiReturnType* mrt = current_analyzing_function->multi_return;
        if (mr->value_count != mrt->count) {
            ERROR(ERR_SEM_RETURN_TYPE_MISMATCH, SEMLOC(mr->base.value),
                  "return value count mismatch: expected %d, got %d",
                  mrt->count, mr->value_count);
        }
        for (int i = 0; i < mr->value_count && i < mrt->count; i++) {
            analyze_node(mr->values[i]);
            if (mrt->func_sigs && mrt->func_sigs[i]
                && mr->values[i]->type == AST_LITERAL
                && ((AST_Literal*)mr->values[i])->closure_func != NULL) {
                mrt->func_sigs[i]->returns_closure = true;
            }
            DataType expected = mrt->types[i];
            DataType actual = get_expression_type(mr->values[i]);
            if (mrt->is_variadic[i]) {
                if (mr->values[i]->type != AST_VARARG_GROUP) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(mr->values[i]->value),
                          "variadic return value %d must be wrapped in []", i);
                } else {
                    DataType declared_elem_type = mrt->types[i];
                    AST_VarargGroup* vg = (AST_VarargGroup*)mr->values[i];
                    bool has_mismatch = false;
                    ASTNode* elem = vg->elements;
                    while (elem) {
                        DataType elem_actual = get_expression_type(elem);
                        if (elem_actual != DATA_ERROR
                            && declared_elem_type != DATA_ERROR
                            && elem_actual != declared_elem_type
                            && types_compatible(declared_elem_type, elem_actual)) {
                            has_mismatch = true;
                            WARNING(ERR_SEM_TYPE_MISMATCH, SEMLOC(elem->value),
                                "implicit conversion from '%s' to '%s' in variadic return value %d",
                                data_type_to_string(elem_actual),
                                data_type_to_string(declared_elem_type), i);
                            }
                        elem = elem->next;
                    }
                    if (has_mismatch) {
                        vg->target_elem_type = declared_elem_type;
                    }

                   if (mrt->func_sigs && mrt->func_sigs[i]) {
                        FuncSignature* expected_fsig = mrt->func_sigs[i];
                        ASTNode* felem = vg->elements;
                        while (felem) {
                            FuncSignature* actual_fsig = NULL;
                            const char* fname = "(unknown)";
                            if (felem->type == AST_ADDRESS_OF
                                && ((AST_AddressOf*)felem)->target_func_sig) {
                                actual_fsig = ((AST_AddressOf*)felem)->target_func_sig;
                                ASTNode* op = ((AST_AddressOf*)felem)->operand;
                                if (op && op->type == AST_LITERAL)
                                    fname = ((AST_Literal*)op)->base.value.value;
                            } else if (felem->type == AST_LITERAL
                                       && !((AST_Literal*)felem)->is_call_function) {
                                AST_Literal* el = (AST_Literal*)felem;
                                fname = el->base.value.value;
                                Symbol* sym = lookup_symbol(fname);
                                if (sym) {
                                    actual_fsig = get_func_ptr_return_sig(fname);
                                    if (!actual_fsig && sym->func_sig)
                                        actual_fsig = sym->func_sig;
                                }
                            } else if (felem->type == AST_ARRAY_ACCESS) {
                                AST_ArrayAccess* acc = (AST_ArrayAccess*)felem;
                                ASTNode* arr = acc->array;
                                if (arr->type == AST_LITERAL) {
                                    AST_Literal* lit = (AST_Literal*)arr;
                                    fname = lit->base.value.value;
                                    Symbol* sym = lookup_symbol(fname);
                                    if (sym) {
                                        actual_fsig = get_func_ptr_return_sig(fname);
                                        if (!actual_fsig && sym->func_sig)
                                            actual_fsig = sym->func_sig;
                                    }
                                } else if (arr->type == AST_VAR || arr->type == AST_VAR_ASS) {
                                    AST_Var* v = (AST_Var*)arr;
                                    fname = v->name.value;
                                    Symbol* sym = lookup_symbol(fname);
                                    if (sym) {
                                        actual_fsig = get_func_ptr_return_sig(fname);
                                        if (!actual_fsig && sym->func_sig)
                                            actual_fsig = sym->func_sig;
                                    }
                                }
                            }

                            if (actual_fsig && expected_fsig->multi_return
                                && actual_fsig->multi_return) {
                                MultiReturnType* exp_mr = expected_fsig->multi_return;
                                MultiReturnType* act_mr = actual_fsig->multi_return;
                                if (act_mr->count != exp_mr->count) {
                                    ERROR(ERR_SEM_TYPE_MISMATCH,
                                          SEMLOC(felem->value),
                                          "function '%s' multi-return count mismatch: "
                                          "declared %d, got %d",
                                          fname, exp_mr->count,
                                          act_mr->count);
                                } else {
                                    for (int mi = 0; mi < exp_mr->count; mi++) {
                                        if (exp_mr->types[mi] != act_mr->types[mi]) {
                                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                                  SEMLOC(felem->value),
                                                  "function '%s' multi-return[%d] type mismatch: "
                                                  "declared '%s', actual '%s'",
                                                  fname, mi,
                                                  data_type_to_string(exp_mr->types[mi]),
                                                  data_type_to_string(act_mr->types[mi]));
                                        }
                                    }
                                }
                            }
                            if (actual_fsig && expected_fsig) {
                                if (expected_fsig->param_count != actual_fsig->param_count) {
                                    ERROR(ERR_SEM_TYPE_MISMATCH,
                                          SEMLOC(felem->value),
                                          "function '%s' parameter count mismatch: "
                                          "declared %d, actual %d",
                                          fname, expected_fsig->param_count,
                                          actual_fsig->param_count);
                                } else {
                                    for (int pi = 0; pi < expected_fsig->param_count; pi++) {
                                        if (expected_fsig->param_types[pi]
                                            != actual_fsig->param_types[pi]) {
                                            ERROR(ERR_SEM_TYPE_MISMATCH,
                                                  SEMLOC(felem->value),
                                                  "function '%s' parameter %d type mismatch: "
                                                  "declared '%s', actual '%s'",
                                                  fname, pi + 1,
                                                  data_type_to_string(
                                                      expected_fsig->param_types[pi]),
                                                  data_type_to_string(
                                                      actual_fsig->param_types[pi]));
                                        }
                                    }
                                }
                                if (expected_fsig->has_varargs
                                    != actual_fsig->has_varargs) {
                                    WARNING(ERR_SEM_TYPE_MISMATCH,
                                            SEMLOC(felem->value),
                                            "function '%s' variadic modifier mismatch",
                                            fname);
                                } else if (expected_fsig->has_varargs) {
                                    if (expected_fsig->vararg_param_count
                                        != actual_fsig->vararg_param_count) {
                                        ERROR(ERR_SEM_TYPE_MISMATCH,
                                              SEMLOC(felem->value),
                                              "function '%s' variadic parameter count "
                                              "mismatch", fname);
                                    } else {
                                        for (int vi = 0;
                                             vi < expected_fsig->vararg_param_count;
                                             vi++) {
                                            if (expected_fsig->
                                                vararg_param_indices[vi]
                                                != actual_fsig->
                                                vararg_param_indices[vi]) {
                                                WARNING(ERR_SEM_TYPE_MISMATCH,
                                                        SEMLOC(felem->value),
                                                        "function '%s' variadic "
                                                        "index mismatch at %d",
                                                        fname, vi);
                                            }
                                        }
                                    }
                                }
                            }
                            felem = felem->next;
                        }
                    }
                }
            } else {
                if (!types_compatible(expected, actual) && actual != DATA_ERROR) {
                    ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(mr->values[i]->value),
                          "return value %d type mismatch: expected '%s', got '%s'",
                          i, data_type_to_string(expected), data_type_to_string(actual));
                }
            }
        }
        if (!current_analyzing_function->multi_return_node) {
            current_analyzing_function->multi_return_node = mr;
        }
        break;
    }

    case AST_RETURN_EXTRACT: {
        AST_ReturnExtract* e = (AST_ReturnExtract*)temp;
        e->total_bytes = 0;
        e->offset = 0;
        e->is_variadic_src = false;
        e->is_thread_object = false;
        e->mr_type = NULL;

        if (e->object->type == AST_LITERAL || e->object->type == AST_VAR) {
            const char* obj_name = NULL;
            if (e->object->type == AST_LITERAL) {
                obj_name = ((AST_Literal*)e->object)->base.value.value;
            } else {
                obj_name = ((AST_Var*)e->object)->name.value;
            }
            Symbol* obj_sym = obj_name ? lookup_symbol(obj_name) : NULL;
            if (obj_sym != NULL && obj_sym->mr_type != NULL) {
                MultiReturnType* tmrt = obj_sym->mr_type;
                e->is_thread_object = true;
                e->mr_type = tmrt;
                if (e->index < 0 || e->index >= tmrt->count) {
                    ERROR(ERR_SEM_INVALID_INDEX, SEMLOC(e->base.value),
                          "getReturn index %d out of range (0..%d)",
                          e->index, tmrt->count - 1);
                    e->result_type = DATA_ERROR;
                    break;
                }
                int off = 0;
                for (int i = 0; i < tmrt->count; i++) {
                    int sz = tmrt->is_variadic[i] ? 8 : S_get_type_size(tmrt->types[i]);
                    if (sz < 4) sz = 4;
                    if (i == e->index) {
                        e->offset = off;
                    }
                    off += sz;
                }
                e->total_bytes = off;
                e->result_type = tmrt->types[e->index];
                e->is_variadic_src = tmrt->is_variadic[e->index];
                if (tmrt->func_sigs && tmrt->func_sigs[e->index]) {
                    e->extracted_func_sig = deep_copy_func_sig(tmrt->func_sigs[e->index]);
                }
                if (e->is_variadic_src && e->sub_index < 0) {
                    e->is_array = true;
                }
                analyze_node(e->object);
                break;
            }
        }

        AST_Literal* call = (AST_Literal*)e->object;

        analyze_node(e->object);

        if (call->returned_func_sig && call->returned_func_sig->multi_return
            && !call->multi_return_type) {
            call->multi_return_type = deep_copy_multi_return(
                call->returned_func_sig->multi_return);
        }

        if (call->returned_func_sig != NULL) {
            if (call->returned_func_sig->multi_return != NULL
                && call->multi_return_type == NULL) {
                call->multi_return_type = deep_copy_multi_return(
                    call->returned_func_sig->multi_return);
            }
            if (call->returned_func_sig->ret_func_sig != NULL
                  && call->returned_func_sig->ret_func_sig->multi_return != NULL
                  && call->multi_return_type == NULL) {
                call->multi_return_type = deep_copy_multi_return(
                    call->returned_func_sig->ret_func_sig->multi_return);
            }
        }

        call->returns_array = false;

        int arg_count = 0;
        ASTNode* a = call->call_function_Parameter;
        while (a) { arg_count++; a = a->next; }

        DataType* arg_types = NULL;
        if (arg_count > 0) {
            arg_types = malloc(sizeof(DataType) * arg_count);
            a = call->call_function_Parameter;
            int ai = 0;
            while (a) {
                arg_types[ai] = (a->type == AST_VARARG_GROUP)
                                ? DATA_STR : get_expression_type(a);
                ai++;
                a = a->next;
            }
        }

        AST_Function* callee = NULL;
        if (!call->is_indirect_call) {
            callee = find_function_by_signature(
                call->base.value.value, arg_count, arg_types);
        }
        free(arg_types);

            MultiReturnType* mrt = NULL;
            if (call->multi_return_type) {
                mrt = call->multi_return_type;
            } else if (call->returned_func_sig && call->returned_func_sig->multi_return) {
                mrt = call->returned_func_sig->multi_return;
            } else if (callee && callee->multi_return) {
                mrt = callee->multi_return;
            }

        if (!mrt) {
            Symbol* fp_sym = NULL;
            if (call->base.value.value) {
                fp_sym = lookup_symbol(call->base.value.value);
            }
            if (!fp_sym && call->internal_name) {
                fp_sym = lookup_symbol(call->internal_name);
            }
            if (fp_sym == NULL && call->is_indirect_call && call->indirect_callee != NULL) {
                if (call->indirect_callee->type == AST_LITERAL) {
                    AST_Literal* ic = (AST_Literal*)call->indirect_callee;
                    fp_sym = lookup_symbol(ic->base.value.value);
                    if (!fp_sym && ic->internal_name)
                        fp_sym = lookup_symbol(ic->internal_name);
                } else if (call->indirect_callee->type == AST_RETURN_EXTRACT) {
                    AST_ReturnExtract* ext = (AST_ReturnExtract*)call->indirect_callee;
                    if (ext->extracted_func_sig && ext->extracted_func_sig->multi_return) {
                        mrt = ext->extracted_func_sig->multi_return;
                    }
                } else if (call->indirect_callee->type == AST_ARRAY_ACCESS) {
                    AST_ArrayAccess* acc = (AST_ArrayAccess*)call->indirect_callee;
                    Symbol* arr_sym = NULL;
                    if (acc->array->type == AST_LITERAL) {
                        AST_Literal* arr_lit = (AST_Literal*)acc->array;
                        arr_sym = lookup_symbol(arr_lit->base.value.value);
                        if (!arr_sym && arr_lit->internal_name)
                            arr_sym = lookup_symbol(arr_lit->internal_name);
                    }
                    if (arr_sym && arr_sym->func_sig && arr_sym->func_sig->multi_return) {
                        mrt = arr_sym->func_sig->multi_return;
                    }
                }
            }
            if (!mrt && fp_sym && fp_sym->func_sig && fp_sym->func_sig->multi_return) {
                mrt = fp_sym->func_sig->multi_return;
            }
            if (!mrt && fp_sym && fp_sym->func_sig
                && fp_sym->func_sig->ret_func_sig
                && fp_sym->func_sig->ret_func_sig->multi_return) {
                mrt = fp_sym->func_sig->ret_func_sig->multi_return;
                call->multi_return_type = deep_copy_multi_return(mrt);
            }
            if (!mrt) {
                Symbol* obj_sym = NULL;
                if (call->base.value.value) {
                    obj_sym = lookup_symbol(call->base.value.value);
                }
                if (!obj_sym && call->internal_name) {
                    obj_sym = lookup_symbol(call->internal_name);
                }
                if (obj_sym && obj_sym->func_sig && obj_sym->func_sig->multi_return) {
                    mrt = obj_sym->func_sig->multi_return;
                    e->extracted_func_sig =
                        deep_copy_func_sig(obj_sym->func_sig);
                    /* Also set multi_return_type on the literal for downstream use */
                    call->multi_return_type = deep_copy_multi_return(mrt);
                }
            }
            if (!mrt && fp_sym && fp_sym->is_function) {
                AST_Function* fp_func = find_function_by_signature(
                    fp_sym->key, 0, NULL);
                if (fp_func && fp_func->multi_return) {
                    call->multi_return_type =
                        deep_copy_multi_return(fp_func->multi_return);
                    mrt = call->multi_return_type;
                }
                if (!mrt && fp_func && fp_func->ret_func_sig != NULL
                      && fp_func->ret_func_sig->multi_return != NULL) {
                    call->multi_return_type =
                        deep_copy_multi_return(fp_func->ret_func_sig->multi_return);
                    mrt = call->multi_return_type;
                }
            }
        }

        if (!mrt && call->is_indirect_call && call->indirect_callee != NULL) {
            ASTNode* walk = call->indirect_callee;
            int max_depth = 10;
            while (walk && max_depth > 0
                   && walk->type == AST_LITERAL
                   && ((AST_Literal*)walk)->is_call_function
                   && ((AST_Literal*)walk)->is_indirect_call) {
                walk = ((AST_Literal*)walk)->indirect_callee;
                max_depth--;
            }
            if (walk && walk->type == AST_RETURN_EXTRACT) {
                AST_ReturnExtract* re = (AST_ReturnExtract*)walk;
                if (re->extracted_func_sig
                    && re->extracted_func_sig->multi_return) {
                    mrt = re->extracted_func_sig->multi_return;
                }
            }
            if (!mrt && walk && walk->type == AST_LITERAL) {
                AST_Literal* wlit = (AST_Literal*)walk;
                if (wlit->multi_return_type) {
                    mrt = wlit->multi_return_type;
                } else if (wlit->returned_func_sig
                           && wlit->returned_func_sig->multi_return) {
                    mrt = wlit->returned_func_sig->multi_return;
                }
            }
        }
        if (!mrt && call->is_indirect_call && call->indirect_callee != NULL
            && call->indirect_callee->type == AST_RETURN_EXTRACT) {
            AST_ReturnExtract* inner = (AST_ReturnExtract*)call->indirect_callee;
            if (inner->extracted_func_sig
                && inner->extracted_func_sig->multi_return) {
                mrt = inner->extracted_func_sig->multi_return;
            }
        }

        if (!mrt && call->is_indirect_call
            && call->indirect_callee != NULL
            && call->indirect_callee->type == AST_LITERAL) {
            AST_Literal* ic = (AST_Literal*)call->indirect_callee;
            if (ic->returned_func_sig
                && ic->returned_func_sig->multi_return) {
                mrt = ic->returned_func_sig->multi_return;
                }
            if (!mrt && ic->multi_return_type) {
                mrt = ic->multi_return_type;
            }
        }

        if (!mrt && callee && callee->ret_func_sig != NULL
                    && callee->ret_func_sig->multi_return != NULL) {
            mrt = callee->ret_func_sig->multi_return;
            call->multi_return_type = deep_copy_multi_return(mrt);
        }

        if (!mrt && call->is_indirect_call
            && call->indirect_callee != NULL) {
            ASTNode* node = call->indirect_callee;
            int depth = 0;
            while (node && depth < 10) {
                if (node->type == AST_LITERAL) {
                    AST_Literal* nl = (AST_Literal*)node;
                    if (nl->returned_func_sig
                        && nl->returned_func_sig->multi_return) {
                        mrt = nl->returned_func_sig->multi_return;
                        break;
                        }
                    if (nl->multi_return_type) {
                        mrt = nl->multi_return_type;
                        break;
                    }
                    if (nl->is_indirect_call && nl->indirect_callee) {
                        node = nl->indirect_callee;
                        depth++;
                        continue;
                    }
                }
                if (node->type == AST_RETURN_EXTRACT) {
                    AST_ReturnExtract* re = (AST_ReturnExtract*)node;
                    if (re->extracted_func_sig
                          && re->extracted_func_sig->ret_func_sig != NULL
                          && re->extracted_func_sig->ret_func_sig->multi_return != NULL) {
                        mrt = re->extracted_func_sig->ret_func_sig->multi_return;
                        break;
                    }
                    if (re->object && re->object->type == AST_LITERAL) {
                        node = re->object;
                        depth++;
                        continue;
                    }
                }
                break;
            }
        }

        if (!mrt) {
            ERROR(ERR_SEM_TYPE_MISMATCH, SEMLOC(e->base.value),
                  "getReturn() called on non-multi-return function");
            e->result_type = DATA_ERROR;
            break;
        }

        call->multi_return_type = mrt;

        int total = 0;
        for (int i = 0; i < mrt->count; i++) {
            int sz = mrt->is_variadic[i] ? 8 : S_get_type_size(mrt->types[i]);
            if (sz < 4) sz = 4;
            total += sz;
        }

        call->returns_array = false;
        call->return_total_bytes = total;

        if (e->index < 0 || e->index >= mrt->count) {
            ERROR(ERR_SEM_INVALID_INDEX, SEMLOC(e->base.value),
                  "return index %d out of range (0..%d)",
                  e->index, mrt->count - 1);
            e->result_type = DATA_ERROR;
            break;
        }

        e->result_type = mrt->types[e->index];
        e->is_variadic_src = mrt->is_variadic[e->index];

        if (mrt->func_sigs && mrt->func_sigs[e->index]) {
            e->extracted_func_sig =
                deep_copy_func_sig(mrt->func_sigs[e->index]);
        }

        if (!e->extracted_func_sig && callee && callee->ret_func_sig != NULL
            && callee->ret_func_sig->multi_return != NULL) {
            MultiReturnType* ret_mrt = callee->ret_func_sig->multi_return;
            if (ret_mrt->func_sigs && e->index < ret_mrt->count
                && ret_mrt->func_sigs[e->index]) {
                e->extracted_func_sig =
                    deep_copy_func_sig(ret_mrt->func_sigs[e->index]);
                }
        }

        if (!e->extracted_func_sig && call->returned_func_sig) {
            e->extracted_func_sig = deep_copy_func_sig(call->returned_func_sig);
        }

        if (!e->extracted_func_sig && call->multi_return_type != NULL
            && call->multi_return_type->func_sigs
            && e->index < call->multi_return_type->count
            && call->multi_return_type->func_sigs[e->index]) {
            e->extracted_func_sig =
                deep_copy_func_sig(call->multi_return_type->func_sigs[e->index]);
        }

        if (!e->extracted_func_sig && call->is_indirect_call
            && call->indirect_callee != NULL) {
            ASTNode* walk = call->indirect_callee;
            int max_depth = 10;
            while (walk && max_depth > 0
                   && walk->type == AST_LITERAL
                   && ((AST_Literal*)walk)->is_call_function
                   && ((AST_Literal*)walk)->is_indirect_call) {
                walk = ((AST_Literal*)walk)->indirect_callee;
                max_depth--;
            }

            if (walk && walk->type == AST_RETURN_EXTRACT) {
                AST_ReturnExtract* re = (AST_ReturnExtract*)walk;
                if (re->extracted_func_sig) {
                    e->extracted_func_sig =
                        deep_copy_func_sig(re->extracted_func_sig);
                }
            }
        }
        if (!e->extracted_func_sig && call->returned_func_sig != NULL
              && call->returned_func_sig->ret_func_sig != NULL) {
            e->extracted_func_sig = deep_copy_func_sig(call->returned_func_sig);
        }

        int off = 0;
        for (int i = 0; i < mrt->count; i++) {
            int sz = mrt->is_variadic[i] ? 8 : get_type_size(mrt->types[i]);
            if (sz < 4) sz = 4;
            if (i == e->index) e->offset = off;
            off += sz;
        }
        e->total_bytes = off;
        if (call->returned_func_sig && call->returned_func_sig->ret_func_sig != NULL
              && !e->extracted_func_sig) {
            e->extracted_func_sig = deep_copy_func_sig(call->returned_func_sig);
        }

        if (e->is_variadic_src) {
            if (e->sub_index < 0) e->is_array = true;
        } else {
            if (e->sub_index >= 0) {
                if (mrt != NULL && !e->is_variadic_src) {
                    if (e->sub_index != 0) {
                        WARNING(ERR_SEM_INVALID_INDEX, SEMLOC(e->base.value),
                                "sub_index %d ignored for non-variadic return value",
                                e->sub_index);
                    }
                } else if (mrt == NULL && call != NULL
                           && call->call_function_Parameter != NULL) {
                    ASTNode* arg = call->call_function_Parameter;
                    bool has_vararg_group = false;
                    while (arg) {
                        if (arg->type == AST_VARARG_GROUP) {
                            has_vararg_group = true;
                            break;
                        }
                        arg = arg->next;
                    }
                    e->is_variadic_src = true;
                    e->is_array = false;
                    e->result_type = (call->dataType == DATA_FLOAT
                                      || call->dataType == DATA_UFLOAT)
                                     ? call->dataType : DATA_INT;
                    e->total_bytes = (e->index + 1) * 8;
                    e->offset = e->index * 8;
                } else {
                    e->is_variadic_src = true;
                    e->is_array = false;
                    e->result_type = (call->dataType == DATA_FLOAT
                                      || call->dataType == DATA_UFLOAT)
                                     ? call->dataType : DATA_INT;
                    e->total_bytes = (e->index + 1) * 8;
                    e->offset = e->index * 8;
                }
            }
        }
        break;
    }

    case AST_ERROR:
        break;

    default:
        break;
    }
}

void main_parse(ASTNode* ast_node) {
    for (ASTNode* node = ast_node; node != NULL; node = node->next) {
        if (node->type == AST_CLASS_DEF) {
            AST_ClassDef* cd = (AST_ClassDef*)node;

            ClassEntry* cls = find_class(cd->name);
            if (cls == NULL) {
                cls = calloc(1, sizeof(ClassEntry));
                cls->name = strdup(cd->name);
                cls->parent_count = cd->parent_count;
                cls->parent_names = malloc(sizeof(char*) * cd->parent_count);
                for (int i = 0; i < cd->parent_count; i++)
                    cls->parent_names[i] = strdup(cd->parent_names[i]);
                cls->modifier = cd->modifier;
                add_class(cls);
            } else if (cls->members_resolved) {
                continue;
            }

            for (int i = 0; i < cd->parent_count; i++) {
                ClassEntry* parent = find_class(cd->parent_names[i]);
                if (parent && !parent->members_resolved) {
                    ERROR(ERR_SEM_CLASS_NOT_FOUND,
                          SEMLOC(cd->base.value),
                          "parent class '%s' must be defined before '%s'",
                          cd->parent_names[i], cd->name);
                }
            }

            resolve_class_members(cd, cls);
        }
        else if (node->type == AST_ENUM_DEF) {
            analyze_node(node);
        }
    }

    ASTNode* temp = ast_node;
    while (temp != NULL) {
        switch (temp->type) {
        case AST_CLASS_DEF: {
            AST_ClassDef* cd = (AST_ClassDef*)temp;
            ClassEntry* cls = find_class(cd->name);
            if (cls && cls->members_resolved) {
                analyze_class_bodies(cd, cls);
            }
            break;
        }

        case AST_ENUM_DEF:
            break;

        default:
            analyze_node(temp);
            break;
        }
        temp = temp->next;
    }
}

void set_current_semantic_file(const char* filename) {
    current_semantic_file = filename;
    semantic_filename = filename;
}

/* 鍦ㄤ换浣曡涔?浠ｇ爜鐢熸垚涔嬪墠锛屾妸銆岃瀛愮被鍚屽悕鏂规硶閲嶅啓銆嶇殑鏂规硶缁熶竴鏍囨垚 virtual/override銆?   鍙湪闈炴硾鍨嬬被涔嬮棿鍋氾紝閬垮厤娉涘瀷瀹炰緥鍖栨椂 vtable 灏哄婕傜Щ銆?   杩欐牱鍗充娇婧愮爜涓嶅啓 virtual/override锛岀埗绫荤被鍨嬪彉閲忎篃鑳芥嬁鍒版纭疄鐜般€?*/
static void mark_implicit_virtuals(void) {
    for (ClassEntry* base = class_table; base; base = base->next) {
        if (!base->generic_ast || base->is_generic) continue;
        AST_ClassDef* bcd = base->generic_ast;
        if (!bcd->name) continue;

        for (ClassEntry* sub = class_table; sub; sub = sub->next) {
            if (sub == base || !sub->generic_ast || sub->is_generic) continue;
            AST_ClassDef* scd = sub->generic_ast;

            bool direct_child = false;
            for (int i = 0; i < sub->parent_count; i++) {
                if (sub->parent_names[i]
                    && strcmp(sub->parent_names[i], bcd->name) == 0) {
                    direct_child = true;
                    break;
                }
            }
            if (!direct_child) continue;

            for (ASTNode* pm = bcd->members; pm; pm = pm->next) {
                if (pm->type != AST_FUNCTION) continue;
                AST_Function* pf = (AST_Function*)pm;
                if (!pf->name.value || pf->is_static || pf->is_native) continue;
                if (strcmp(pf->name.value, bcd->name) == 0) continue;   /* 鏋勯€犲櫒 */

                for (ASTNode* cm = scd->members; cm; cm = cm->next) {
                    if (cm->type != AST_FUNCTION) continue;
                    AST_Function* cf = (AST_Function*)cm;
                    if (!cf->name.value || cf->is_static || cf->is_native) continue;
                    if (scd->name && strcmp(cf->name.value, scd->name) == 0) continue;
                    if (strcmp(cf->name.value, pf->name.value) != 0) continue;

                    if (!pf->is_virtual && !pf->is_override) pf->is_virtual = true;
                    if (!cf->is_virtual && !cf->is_override) cf->is_override = true;
                    break;
                }
            }
        }
    }
}

void Semantics(ASTNode* ast_node) {
    if (ast_node == NULL) {
        INTERNAL_ERROR("semantics: NULL AST node");
    }

    semantic_filename = current_semantic_file;

    bool created_global = false;
    if (current_scope == NULL) {
        enter_scope();
        created_global = true;
    }

    predeclare_all_types(ast_node);

    mark_implicit_virtuals();


    predeclare_global_vars(ast_node);

    main_parse(ast_node);

    if (!has_main_function()) {
        ERROR(ERR_SEM_NO_MAIN,
              LOC_NONE,
              "no main function found");
    }

    if (created_global) {
        exit_scope();
    }
}

static bool nameset_has(const NameSet* s, const char* n) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->names[i], n) == 0) return true;
    return false;
}

static void nameset_add(NameSet* s, const char* n) {
    if (!n || nameset_has(s, n)) return;
    if (s->count >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->names = realloc(s->names, sizeof(char*) * (size_t)s->cap);
        if (!s->names) OUT_OF_MEMORY();
    }
    s->names[s->count++] = strdup(n);
}

static void propagate_closure_stmt(ASTNode* node, NameSet* cv, NameSet* cr);

static void propagate_closure_block(ASTNode* list, NameSet* cv, NameSet* cr) {
    while (list) {
        propagate_closure_stmt(list, cv, cr);
        list = list->next;
    }
}

static void propagate_closure_stmt(ASTNode* node, NameSet* cv, NameSet* cr) {
    if (!node) return;
    switch (node->type) {
    case AST_VAR:
    case AST_VAR_ASS: {
        AST_Var* v = (AST_Var*)node;
        if (v->Value && v->Value->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)v->Value;
            if (lit->is_call_function) {
                const char* callee = lit->base.value.value;
                if (!callee) callee = lit->internal_name;
                if (lit->is_closure_call)
                    nameset_add(cv, v->name.value);
                if (callee && nameset_has(cr, callee))
                    nameset_add(cv, v->name.value);
            } else if (lit->base.value.type == TOKEN_IDENTIFIER
                       && nameset_has(cv, lit->base.value.value)) {
                lit->is_closure_ptr = true;
                nameset_add(cv, v->name.value);
                       }
        }
        if (v->Value && v->Value->type == AST_RETURN_EXTRACT) {
            AST_ReturnExtract* ex = (AST_ReturnExtract*)v->Value;
            if (ex->extracted_func_sig && ex->extracted_func_sig->returns_closure)
                nameset_add(cv, v->name.value);
        }
        break;
    }
    case AST_LITERAL: {
        AST_Literal* lit = (AST_Literal*)node;
        if (lit->is_call_function) {
            const char* callee = lit->base.value.value;
            if (!callee) callee = lit->internal_name;
            if (callee && nameset_has(cv, callee))
                lit->is_closure_call = true;
            propagate_closure_block(lit->call_function_Parameter, cv, cr);
            if (lit->method_arguments && lit->method_arguments != lit->call_function_Parameter)
                propagate_closure_block(lit->method_arguments, cv, cr);
            if (lit->indirect_callee)
                propagate_closure_stmt(lit->indirect_callee, cv, cr);
        } else if (lit->base.value.type == TOKEN_IDENTIFIER
                   && nameset_has(cv, lit->base.value.value)) {
            lit->is_closure_ptr = true;
                   }
        break;
    }
    case AST_RETURN:
        propagate_closure_stmt(((AST_Return*)node)->value, cv, cr);
        break;
    case AST_IF: {
        AST_If* n = (AST_If*)node;
        propagate_closure_stmt(n->Condition, cv, cr);
        propagate_closure_block(n->code, cv, cr);
        for (int i = 0; i < n->elif_Count; i++) {
            propagate_closure_stmt(n->elif_condition[i], cv, cr);
            propagate_closure_block(n->Elif[i], cv, cr);
        }
        propagate_closure_block(n->Else, cv, cr);
        break;
    }
    case AST_WHILE:
        propagate_closure_stmt(((AST_While*)node)->Condition, cv, cr);
        propagate_closure_block(((AST_While*)node)->code, cv, cr);
        break;
    case AST_FOR:
        propagate_closure_block(((AST_For*)node)->init, cv, cr);
        propagate_closure_stmt(((AST_For*)node)->condition, cv, cr);
        propagate_closure_block(((AST_For*)node)->increment, cv, cr);
        propagate_closure_block(((AST_For*)node)->code, cv, cr);
        break;
    case AST_SWITCH: {
        AST_Switch* sw = (AST_Switch*)node;
        propagate_closure_stmt(sw->condition, cv, cr);
        for (int i = 0; i < sw->case_count; i++)
            propagate_closure_block(sw->cases[i]->body, cv, cr);
        propagate_closure_block(sw->default_body, cv, cr);
        break;
    }
    case AST_PRINT:
        propagate_closure_stmt(((AST_Print*)node)->Parameter, cv, cr);
        break;
    case AST_BINARY:
        propagate_closure_stmt(((AST_Binary*)node)->left, cv, cr);
        propagate_closure_stmt(((AST_Binary*)node)->right, cv, cr);
        break;
    case AST_UNARY:
        propagate_closure_stmt(((AST_Unary*)node)->right, cv, cr);
        break;
    case AST_CONDITIONAL:
        propagate_closure_stmt(((AST_Conditional*)node)->condition, cv, cr);
        propagate_closure_stmt(((AST_Conditional*)node)->then_expr, cv, cr);
        propagate_closure_stmt(((AST_Conditional*)node)->else_expr, cv, cr);
        break;
    case AST_FOREACH:
        propagate_closure_stmt(((AST_ForEach*)node)->array, cv, cr);
        propagate_closure_block(((AST_ForEach*)node)->code, cv, cr);
        break;
    case AST_FUNCTION:
        break;
    case AST_TRY: {
        AST_Try* tn = (AST_Try*)node;
        propagate_closure_block(tn->code, cv, cr);
        for (int i = 0; i < tn->catch_count; i++)
            propagate_closure_block(tn->catches[i].body, cv, cr);
        propagate_closure_block(tn->finally_code, cv, cr);
        break;
    }
    case AST_THROW:
        propagate_closure_stmt(((AST_Throw*)node)->value, cv, cr);
        break;
    default:
        break;
    }
}

static void propagate_one_function(AST_Function* f) {
    if (!f || !f->Code || f->is_generic) return;
    NameSet cv = {NULL, 0, 0};
    NameSet cr = {NULL, 0, 0};
    ASTNode* p = f->Parameter;
    while (p) {
        if (p->type == AST_VAR || p->type == AST_UNINIT_VAR
            || p->type == AST_VAR_ASS) {
            AST_Var* pv = (AST_Var*)p;
            if (pv->is_closure_ptr) nameset_add(&cv, pv->name.value);
            if (pv->func_ptr_sig && pv->func_ptr_sig->returns_closure)
                nameset_add(&cr, pv->name.value);
        }
        p = p->next;
    }
    propagate_closure_block(f->Code, &cv, &cr);
    for (int i = 0; i < cv.count; i++) free(cv.names[i]);
    free(cv.names);
    for (int i = 0; i < cr.count; i++) free(cr.names[i]);
    free(cr.names);
}

void semantics_propagate_closures(ASTNode* program) {
    while (program) {
        if (program->type == AST_FUNCTION)
            propagate_one_function((AST_Function*)program);
        program = program->next;
    }
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (e->func && strchr(e->name, '$') != NULL)
            propagate_one_function(e->func);
    }
}

ASTNode* semantics_get_instantiated_functions(void) {
    ASTNode* head = NULL;
    ASTNode* tail = NULL;
    for (FuncEntry* e = func_table; e; e = e->next) {
        if (e->func && (strchr(e->name, '$') != NULL
                        || strncmp(e->name, "__thread_thunk_", 15) == 0
                        || strncmp(e->name, "__make_thread_fn_", 17) == 0)) {
            ASTNode* node = (ASTNode*)e->func;
            node->next = NULL;
            if (!head) {
                head = tail = node;
            } else {
                tail->next = node;
                tail = node;
            }
        }
    }
    return head;
}

ClassIdEntry* semantics_get_class_ids(void) {
    ClassIdEntry* head = NULL;
    for (ClassEntry* cls = class_table; cls; cls = cls->next) {
        int parent_id = 0;
        if (cls->parent_count > 0) {
            ClassEntry* p = find_class(cls->parent_names[0]);
            if (p) parent_id = p->class_id;
        }
        char* handler = NULL;
        if (cls->has_type_header) {
            for (int i = 0; i < cls->member_count; i++) {
                MemberInfo* mi = cls->members[i];
                if (mi->is_method && strcmp(mi->name, "Error") == 0
                    && mi->func && mi->func->internal_name) {
                    handler = mi->func->internal_name;
                    break;
                }
            }
        }
        ClassIdEntry* e = malloc(sizeof(ClassIdEntry));
        if (!e) OUT_OF_MEMORY();
        e->class_id = cls->class_id;
        e->parent_id = parent_id;
        e->error_handler = handler ? strdup(handler) : NULL;
        e->next = head;
        head = e;
    }
    return head;
}

int semantics_get_class_count(void) {
    int max_id = 0;
    for (ClassEntry* cls = class_table; cls; cls = cls->next) {
        if (cls->class_id > max_id) max_id = cls->class_id;
    }
    return max_id + 1;
}

void semantics_get_class_isa(int* out_count, unsigned char** out_table) {
    int n = semantics_get_class_count();
    if (out_count) *out_count = n;
    if (out_table) *out_table = NULL;
    if (n <= 0) return;

    unsigned char* isa = calloc((size_t)n * (size_t)n, sizeof(unsigned char));
    if (!isa) OUT_OF_MEMORY();

    for (ClassEntry* cls = class_table; cls; cls = cls->next) {
        int cid = cls->class_id;
        if (cid < 0 || cid >= n) continue;
        isa[(size_t)cid * n + cid] = 1;
        for (int i = 0; i < cls->parent_count; i++) {
            ClassEntry* p = find_class(cls->parent_names[i]);
            if (p && p->class_id >= 0 && p->class_id < n) {
                isa[(size_t)cid * n + p->class_id] = 1;
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (!isa[(size_t)i * n + j]) continue;
                for (int k = 0; k < n; k++) {
                    if (isa[(size_t)j * n + k] && !isa[(size_t)i * n + k]) {
                        isa[(size_t)i * n + k] = 1;
                        changed = true;
                    }
                }
            }
        }
    }

    if (out_table) *out_table = isa;
    else free(isa);
}

ClassEntry* semantics_get_all_classes(void) {
    return class_table;
}

char* class_vtable_label(const char* class_name) {
    if (!class_name) return strdup("_vt_");
    size_t len = strlen(class_name) + 8;
    char* buf = malloc(len);
    if (!buf) OUT_OF_MEMORY();
    char* out = buf;
    *out++ = '_';
    *out++ = 'v';
    *out++ = 't';
    *out++ = '_';
    for (const char* p = class_name; *p; p++) {
        if (*p == '$')
            *out++ = '_';
        else
            *out++ = *p;
    }
    *out = '\0';
    return buf;
}

const char* resolve_vtable_slot(ClassEntry* cls, int slot) {
    if (!cls || slot < 0 || slot >= cls->vtable_size) return "0";
    const char* method_name = cls->vtable_method_names[slot];
    if (!method_name) return "0";
    for (int i = 0; i < cls->member_count; i++) {
        MemberInfo* mi = cls->members[i];
        if (mi->is_method &&
            (mi->func->is_virtual || mi->func->is_override) &&
            mi->func->internal_name &&
            strcmp(mi->name, method_name) == 0) {
            return mi->func->internal_name;
            }
    }
    for (int p = 0; p < cls->parent_count; p++) {
        ClassEntry* pc = find_class(cls->parent_names[p]);
        if (pc) {
            const char* r = resolve_vtable_slot(pc, slot);
            if (r && strcmp(r, "0") != 0) return r;
        }
    }
    return "0";
}

static void free_func_sig(FuncSignature* sig) {
    if (!sig) return;
    free(sig->return_class_name);
    free(sig->param_types);
    free(sig->param_type_param_indices);
    free(sig->param_pointee_const);
    free(sig->param_is_array);
    if (sig->param_class_names) {
        for (int i = 0; i < sig->param_count; i++)
            free(sig->param_class_names[i]);
        free(sig->param_class_names);
    }
    if (sig->param_func_sigs) {
        for (int i = 0; i < sig->param_count; i++) {
            if (sig->param_func_sigs[i]) {
                free_func_sig(sig->param_func_sigs[i]);
            }
        }
        free(sig->param_func_sigs);
    }
    free(sig->vararg_param_indices);
    if (sig->multi_return) {
        MultiReturnType* mrt = sig->multi_return;
        free(mrt->types);
        free(mrt->type_param_indices);
        free(mrt->is_variadic);
        if (mrt->func_sigs) {
            for (int i = 0; i < mrt->count; i++)
                free_func_sig(mrt->func_sigs[i]);
            free(mrt->func_sigs);
        }
        free(mrt);
    }
    free_func_sig(sig->ret_func_sig);
    free(sig);
}

void free_symbol(Symbol* sym) {
    if (sym) {
        if (sym->key) free(sym->key);
        if (sym->internal_name) free(sym->internal_name);
        if (sym->dim_sizes) free(sym->dim_sizes);
        if (sym->func_sig) {
            free_func_sig(sym->func_sig);
            sym->func_sig = NULL;
        }
        if (sym->ptr_array_info) {
            free(sym->ptr_array_info->dim_sizes);
            free(sym->ptr_array_info);
        }
        if (sym->generic_type_arg_names) {
            for (int i = 0; i < sym->generic_type_arg_count; i++)
                free(sym->generic_type_arg_names[i]);
            free(sym->generic_type_arg_names);
        }
        if (sym->generic_type_args) free(sym->generic_type_args);
        if (sym->const_value_str) free(sym->const_value_str);
        free(sym);
    }
}

static void cleanup_all_scopes(void)
{
    while (current_scope != NULL) {
        if (current_scope->table) {
            for (int i = 0; i < current_scope->table->size; i++) {
                Symbol* sym = current_scope->table->buckets[i];
                while (sym) {
                    Symbol* next = sym->next;
                    free_symbol(sym);
                    sym = next;
                }
            }
            free(current_scope->table->buckets);
            free(current_scope->table);
            current_scope->table = NULL;
        }
        Scope* parent = current_scope->parent;
        free(current_scope);
        current_scope = parent;
    }
}

static void cleanup_class_table(void)
{
    ClassEntry* cls = class_table;
    while (cls) {
        ClassEntry* next = cls->next;
        if (cls->name) free(cls->name);
        if (cls->short_name) free(cls->short_name);
        if (cls->outer_class_name) free(cls->outer_class_name);
        if (cls->parent_names) {
            for (int i = 0; i < cls->parent_count; i++) {
                free(cls->parent_names[i]);
            }
            free(cls->parent_names);
        }
        if (cls->vtable_method_names) {
            for (int i = 0; i < cls->vtable_size; i++)
                free(cls->vtable_method_names[i]);
            free(cls->vtable_method_names);
        }
        if (cls->enum_names) {
            for (int i = 0; i < cls->enum_count; i++)
                free(cls->enum_names[i]);
            free(cls->enum_names);
        }
        if (cls->members) {
            for (int i = 0; i < cls->member_count; i++) {
                if (cls->members[i]) {
                    if (cls->members[i]->name) free(cls->members[i]->name);
                    if (cls->members[i]->init_value_str) free(cls->members[i]->init_value_str);
                    if (cls->members[i]->const_value_str) free(cls->members[i]->const_value_str);
                    if (cls->members[i]->field_class_name) free(cls->members[i]->field_class_name);
                    if (cls->members[i]->class_name) free(cls->members[i]->class_name);
                    if (cls->members[i]->dim_sizes) free(cls->members[i]->dim_sizes);
                    if (cls->members[i]->func_sig) free_func_sig(cls->members[i]->func_sig);
                    free(cls->members[i]);
                }
            }
            free(cls->members);
        }
        free(cls);
        cls = next;
    }
    class_table = NULL;
}

static void cleanup_type_aliases(void)
{
    TypeAlias* a = type_aliases;
    while (a) {
        TypeAlias* next = a->next;
        if (a->name) free(a->name);
        free(a);
        a = next;
    }
    type_aliases = NULL;
}

void semantics_cleanup(void)
{
    cleanup_all_scopes();
    cleanup_class_table();
    cleanup_type_aliases();
    while (g_static_decls) {
        StaticDecl* n = g_static_decls->next;
        free(g_static_decls->name);
        free(g_static_decls->file);
        free(g_static_decls);
        g_static_decls = n;
    }
    free_function_table();
    for (int i = 0; i < native_symbol_count; i++)
        free(native_symbols[i]);
    free(native_symbols);
    native_symbols = NULL;
    native_symbol_count = 0;
    native_symbol_cap = 0;
    InstantiatedFunc* ifn = instantiated_funcs;
    while (ifn) {
        InstantiatedFunc* next = ifn->next;
        free(ifn->instantiated_name);
        free(ifn->generic_name);
        free(ifn->type_args);
        free(ifn);
        ifn = next;
    }
    instantiated_funcs = NULL;
}

/* ==================== 绗﹀彿琛ㄥ鍑猴紙LSP 鐢級 ==================== */

static const char* sym_dt_name(DataType t) {
    switch (t) {
        case DATA_INT: return "int";
        case DATA_UINT: return "uint";
        case DATA_FLOAT: return "float";
        case DATA_UFLOAT: return "ufloat";
        case DATA_STRING: return "string";
        case DATA_STR: return "str";
        case DATA_CHAR: return "char";
        case DATA_BOOL: return "bool";
        case DATA_VOID: return "void";
        case DATA_INT8: return "int8";
        case DATA_INT16: return "int16";
        case DATA_INT32: return "int32";
        case DATA_INT64: return "int64";
        case DATA_INT128: return "int128";
        case DATA_UINT8: return "uint8";
        case DATA_UINT16: return "uint16";
        case DATA_UINT32: return "uint32";
        case DATA_UINT64: return "uint64";
        case DATA_UINT128: return "uint128";
        case DATA_FLOAT8: return "float8";
        case DATA_FLOAT16: return "float16";
        case DATA_FLOAT32: return "float32";
        case DATA_FLOAT64: return "float64";
        case DATA_FLOAT128: return "float128";
        case DATA_UFLOAT8: return "ufloat8";
        case DATA_UFLOAT16: return "ufloat16";
        case DATA_UFLOAT32: return "ufloat32";
        case DATA_UFLOAT64: return "ufloat64";
        case DATA_UFLOAT128: return "ufloat128";
        case DATA_IDENTIFIER: return "class";
        default: return "var";
    }
}

static void sym_json_str(FILE* out, const char* s) {
    fputc('"', out);
    if (s) {
        for (const char* p = s; *p; p++) {
            unsigned char ch = (unsigned char)*p;
            if (ch == '"' || ch == '\\') { fputc('\\', out); fputc(ch, out); }
            else if (ch < 0x20) fprintf(out, "\\u%04x", ch);
            else fputc(ch, out);
        }
    }
    fputc('"', out);
}

static void sym_emit(FILE* out, int* first, const char* file,
                     const char* name, const char* kind, const char* detail,
                     int line, int col, const char* container) {
    if (name == NULL) return;
    if (!*first) fputc(',', out);
    *first = 0;
    fputc('{', out);
    fputs("\"name\":", out);    sym_json_str(out, name);
    fputs(",\"kind\":", out);   sym_json_str(out, kind);
    fputs(",\"detail\":", out); sym_json_str(out, detail ? detail : "");
    fputs(",\"file\":", out);   sym_json_str(out, file ? file : "");
    fprintf(out, ",\"line\":%d,\"col\":%d",
            line > 0 ? line : 1, col > 0 ? col : 1);
    if (container) { fputs(",\"container\":", out); sym_json_str(out, container); }
    fputc('}', out);
}

static void sym_emit_function(FILE* out, int* first, AST_Function* f,
                              const char* container) {
    if (f == NULL || f->name.value == NULL) return;
    if (f->is_generic) return;

    char detail[256];
    int n = snprintf(detail, sizeof(detail), "%s %s(",
                     sym_dt_name(f->return_type), f->name.value);
    for (int i = 0; i < f->parameterCount && n < (int)sizeof(detail) - 48; i++) {
        const char* pname = NULL;
        ASTNode* pn = f->Parameter;
        for (int k = 0; k < i && pn; k++) pn = pn->next;
        if (pn && pn->type == AST_VAR) pname = ((AST_Var*)pn)->name.value;
        if (pname) {
            n += snprintf(detail + n, sizeof(detail) - (size_t)n, "%s%s %s",
                          i ? ", " : "", sym_dt_name(f->parameterType[i]), pname);
        } else {
            n += snprintf(detail + n, sizeof(detail) - (size_t)n, "%s%s",
                          i ? ", " : "", sym_dt_name(f->parameterType[i]));
        }
    }
    snprintf(detail + n, sizeof(detail) - (size_t)n, ")");
    sym_emit(out, first, f->name.file, f->name.value, "function", detail,
             f->name.line, f->name.column, container);
}

static void sym_emit_var(FILE* out, int* first, Token tok, const char* name,
                         DataType dt, int dim_count, int ptr_depth,
                         const char* kind, const char* container) {
    if (name == NULL) return;
    char detail[128];
    int n = snprintf(detail, sizeof(detail), "%s", sym_dt_name(dt));
    for (int i = 0; i < ptr_depth && n < 100; i++)
        n += snprintf(detail + n, sizeof(detail) - (size_t)n, "*");
    if (dim_count > 0)
        n += snprintf(detail + n, sizeof(detail) - (size_t)n, "[]");
    sym_emit(out, first, tok.file, name, kind ? kind : "var", detail,
             tok.line, tok.column, container);
}

void semantics_dump_symbols_json(ASTNode* ast_node, const char* filename, FILE* out) {
    int first = 1;
    fputs("{\"file\":", out);
    sym_json_str(out, filename ? filename : "");
    fputs(",\"symbols\":[", out);

    for (ASTNode* n = ast_node; n; n = n->next) {
        if (n->type == AST_CLASS_DEF) {
            AST_ClassDef* cd = (AST_ClassDef*)n;
            if (cd->name && (strcmp(cd->name, "ERROR") == 0
                            || strcmp(cd->name, "AssertError") == 0)) continue;
            if (!cd->name) continue;
            sym_emit(out, &first, cd->name_tok.file, cd->name, "class",
                     cd->is_generic ? "class (generic)" : "class",
                     cd->name_tok.line, cd->name_tok.column, NULL);
            for (ASTNode* m = cd->members; m; m = m->next) {
                if (m->type == AST_FUNCTION) {
                    sym_emit_function(out, &first, (AST_Function*)m, cd->name);
                } else if (m->type == AST_VAR) {
                    AST_Var* v = (AST_Var*)m;
                    sym_emit_var(out, &first, v->name, v->name.value, v->dataType,
                                 v->dim_count, v->ptr_depth, "field", cd->name);
                } else if (m->type == AST_ARRAY_DECL) {
                    AST_ArrayDecl* v = (AST_ArrayDecl*)m;
                    sym_emit_var(out, &first, v->base.value, v->name,
                                 v->elementType,
                                 v->dim_count > 0 ? v->dim_count : 1, 0,
                                 "field", cd->name);
                }
            }
        } else if (n->type == AST_ENUM_DEF) {
            AST_EnumDef* ed = (AST_EnumDef*)n;
            if (!ed->name) continue;
            sym_emit(out, &first, ed->name_tok.file, ed->name, "enum", "enum",
                     ed->name_tok.line, ed->name_tok.column, NULL);
            for (int i = 0; i < ed->member_count; i++) {
                AST_EnumMember* em = ed->members[i];
                if (!em || !em->name) continue;
                Token et = em->explicit_token ? *em->explicit_token : ed->name_tok;
                sym_emit(out, &first, et.file, em->name, "enumMember", ed->name,
                         et.line, et.column, ed->name);
            }
        } else if (n->type == AST_FUNCTION) {
            sym_emit_function(out, &first, (AST_Function*)n, NULL);
        } else if (n->type == AST_VAR) {
            AST_Var* v = (AST_Var*)n;
            sym_emit_var(out, &first, v->name, v->name.value, v->dataType,
                         v->dim_count, v->ptr_depth, "var", NULL);
        } else if (n->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* v = (AST_ArrayDecl*)n;
            sym_emit_var(out, &first, v->base.value, v->name, v->elementType,
                         v->dim_count > 0 ? v->dim_count : 1, 0, "var", NULL);
        }
    }
    fputs("]}\n", out);
}
