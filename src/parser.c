#include "parser.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "Semantics.h"
#include "Error.h"

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <stdlib.h>
    #define PATH_SEPARATOR '\\'
    #define MAX_PATH 260
#else
    #include <unistd.h>
    #include <limits.h>
    #define PATH_SEPARATOR '/'
    #define MAX_PATH PATH_MAX
#endif

typedef struct ParsedTypeInfo {
    DataType base_type;
    char* type_name;
    int ptr_depth;
    int dim_count;
    int *dim_sizes;
    ASTNode **dim_exprs;
    FuncSignature *func_sig;
    PtrArrayInfo *ptr_array_info;
    MultiReturnType *multi_return;
    bool is_multi_return;
    DataType* generic_type_args;
    char** generic_type_arg_names;
    int generic_type_arg_count;
} ParsedTypeInfo;

typedef struct ImportedLexer {
    Lexer* lex;
    struct ImportedLexer* next;
} ImportedLexer;
static ImportedLexer* imported_lexers = NULL;

ModulePath* module_paths = NULL;
#define IMPORTED_CAPACITY 128
char** imported_modules = NULL;
int imported_count = 0;
int imported_capacity = 0;
static const char* parser_filename = NULL;
static TypeParam* g_parse_type_params = NULL;
static int g_parse_type_param_count = 0;
bool assertions_enabled = true;
static bool g_native_requested = false;

static int resolve_type_param_index(const char* name) {
    if (!g_parse_type_params || !name) return -1;
    for (int i = 0; i < g_parse_type_param_count; i++) {
        if (strcmp(g_parse_type_params[i].name, name) == 0)
            return i;
    }
    return -1;
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

static void parsed_type_info_free(ParsedTypeInfo *pti) {
    if (!pti) return;
    free(pti->type_name);
    if (pti->generic_type_args) {
        free(pti->generic_type_args);
        pti->generic_type_args = NULL;
    }
    if (pti->generic_type_arg_names) {
        for (int i = 0; i < pti->generic_type_arg_count; i++)
            free(pti->generic_type_arg_names[i]);
        free(pti->generic_type_arg_names);
        pti->generic_type_arg_names = NULL;
    }
    pti->generic_type_arg_count = 0;
    pti->type_name = NULL;
    free(pti->dim_sizes);
    if (pti->dim_exprs) {
        for (int i = 0; i < pti->dim_count; i++)
            if (pti->dim_exprs[i]) AST_Free(pti->dim_exprs[i]);
        free(pti->dim_exprs);
    }
    if (pti->func_sig) {
        free(pti->func_sig->param_types);
        free(pti->func_sig->vararg_param_indices);
        if (pti->func_sig->multi_return) {
            MultiReturnType *mrt = pti->func_sig->multi_return;
            free(mrt->types);
            free(mrt->is_variadic);
            if (mrt->func_sigs) {
                for (int i = 0; i < mrt->count; i++)
                    if (mrt->func_sigs[i]) {
                        free(mrt->func_sigs[i]->param_types);
                        free(mrt->func_sigs[i]->vararg_param_indices);
                        free(mrt->func_sigs[i]);
                    }
                free(mrt->func_sigs);
            }
            free(mrt);
        }
        free(pti->func_sig);
    }
    if (pti->ptr_array_info) {
        free(pti->ptr_array_info->dim_sizes);
        free(pti->ptr_array_info);
    }
    if (pti->multi_return) {
        MultiReturnType *mrt = pti->multi_return;
        free(mrt->types);
        free(mrt->is_variadic);
        if (mrt->func_sigs) {
            for (int i = 0; i < mrt->count; i++)
                if (mrt->func_sigs[i]) {
                    free(mrt->func_sigs[i]->param_types);
                    free(mrt->func_sigs[i]->vararg_param_indices);
                    free(mrt->func_sigs[i]);
                }
            free(mrt->func_sigs);
        }
        free(mrt);
    }
    memset(pti, 0, sizeof(*pti));
}

static int skip_to_name(Parser* parser) {
    int idx = parser->index + 1;

    if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) {
        int look = idx + 1;
        int star_count = 0;
        while (parser->tokens[look].type == TOKEN_MUL) {
            star_count++;
            look++;
        }

        if (star_count > 0) {
            if (parser->tokens[look].type == TOKEN_IDENTIFIER) {
                look++;
            }
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                look++;
            }
            while (parser->tokens[look].type == TOKEN_SQUARE_BRACKET_LEFT) {
                look++;
                if (parser->tokens[look].type == TOKEN_NUMBER)
                    look++;
                if (parser->tokens[look].type == TOKEN_SQUARE_BRACKET_RIGHT)
                    look++;
                else
                    break;
            }
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) {
                int depth = 1;
                look++;
                while (depth > 0 && parser->tokens[look].type != TOKEN_EOF) {
                    if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                    if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) depth--;
                    if (depth > 0) look++;
                }
                if (parser->tokens[look].type == TOKEN_PAREN_RIGHT)
                    look++;
            }
            return look;
        }
    }

    while (parser->tokens[idx].type == TOKEN_DOT
           && parser->tokens[idx + 1].type == TOKEN_IDENTIFIER) {
        idx += 2;
    }

    while (parser->tokens[idx].type == TOKEN_MUL)
        idx++;

    if (parser->tokens[idx].type == TOKEN_LT) {
        int depth = 1;
        idx++;
        while (depth > 0 && parser->tokens[idx].type != TOKEN_EOF) {
            if (parser->tokens[idx].type == TOKEN_LT) depth++;
            else if (parser->tokens[idx].type == TOKEN_GT) depth--;
            idx++;
        }
    }

    if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) {
        int look2 = idx + 1;
        while (parser->tokens[look2].type == TOKEN_MUL) {
            look2++;
        }
        if (parser->tokens[look2].type == TOKEN_IDENTIFIER) {
            look2++;
        }
        if (parser->tokens[look2].type == TOKEN_PAREN_RIGHT) {
            idx = look2 + 1;
            if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) {
                int depth = 1;
                idx++;
                while (depth > 0 && parser->tokens[idx].type != TOKEN_EOF) {
                    if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) depth++;
                    if (parser->tokens[idx].type == TOKEN_PAREN_RIGHT) depth--;
                    if (depth > 0) idx++;
                }
                if (parser->tokens[idx].type == TOKEN_PAREN_RIGHT)
                    idx++;
            }
        }
    }

    while (parser->tokens[idx].type == TOKEN_SQUARE_BRACKET_LEFT) {
        idx++;
        if (parser->tokens[idx].type == TOKEN_NUMBER)
            idx++;
        if (parser->tokens[idx].type == TOKEN_SQUARE_BRACKET_RIGHT)
            idx++;
        else
            break;
    }

    if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) {
        int depth = 1;
        idx++;
        while (depth > 0 && parser->tokens[idx].type != TOKEN_EOF) {
            if (parser->tokens[idx].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[idx].type == TOKEN_PAREN_RIGHT) depth--;
            if (depth > 0) idx++;
        }
        if (parser->tokens[idx].type == TOKEN_PAREN_RIGHT)
            idx++;
    }

    return idx;
}

static ASTNode* clone_ast(ASTNode* node) {
    if (!node) return NULL;
    switch (node->type) {
    case AST_LITERAL: {
        AST_Literal* src = (AST_Literal*)node;
        AST_Literal* dst = calloc(1, sizeof(AST_Literal));
        if (!dst) OUT_OF_MEMORY();
        dst->base.type = AST_LITERAL;
        dst->base.next = NULL;
        if (src->base.value.value) {
            dst->base.value.value = strdup(src->base.value.value);
            dst->base.value.type  = src->base.value.type;
            dst->base.value.line  = src->base.value.line;
            dst->base.value.column = src->base.value.column;
        }
        dst->dataType = src->dataType;
        dst->internal_name = src->internal_name ? strdup(src->internal_name) : NULL;
        dst->call_result_class_name = src->call_result_class_name
                              ? strdup(src->call_result_class_name) : NULL;
        dst->is_call_function = src->is_call_function;
        dst->call_function_Parameter = NULL;
        dst->call_function_ParameterCount = 0;
        dst->method_arguments = NULL;
        dst->method_arg_count = 0;
        dst->is_field_ref = src->is_field_ref;
        dst->field_offset = src->field_offset;
        dst->is_field_array = src->is_field_array;
        return (ASTNode*)dst;
    }

    case AST_THIS: {
        AST_This* src = (AST_This*)node;
        AST_This* dst = calloc(1, sizeof(AST_This));
        if (!dst) OUT_OF_MEMORY();
        dst->base.type  = AST_THIS;
        dst->base.next  = NULL;
        dst->instance_size = src->instance_size;
        return (ASTNode*)dst;
    }

    case AST_MEMBER_ACCESS: {
        AST_MemberAccess* src = (AST_MemberAccess*)node;
        AST_MemberAccess* dst = calloc(1, sizeof(AST_MemberAccess));
        if (!dst) OUT_OF_MEMORY();
        dst->base.type = AST_MEMBER_ACCESS;
        dst->base.next = NULL;
        dst->object = clone_ast(src->object);
        dst->member_name = src->member_name ? strdup(src->member_name) : NULL;
        dst->member_offset = src->member_offset;
        dst->member_type = src->member_type;
        dst->is_method = src->is_method;
        dst->is_field_array = src->is_field_array;
        dst->vtable_index = src->vtable_index;
        dst->const_value_str = src->const_value_str
                               ? strdup(src->const_value_str) : NULL;
        return (ASTNode*)dst;
    }

    case AST_ARRAY_ACCESS: {
        AST_ArrayAccess* src = (AST_ArrayAccess*)node;
        AST_ArrayAccess* dst = calloc(1, sizeof(AST_ArrayAccess));
        if (!dst) OUT_OF_MEMORY();
        dst->base.type = AST_ARRAY_ACCESS;
        dst->base.next = NULL;
        dst->base.value = src->base.value;
        dst->array = clone_ast(src->array);
        dst->index_count = src->index_count;
        if (src->index_count > 0) {
            dst->indices = malloc(sizeof(ASTNode*) * src->index_count);
            if (!dst->indices) OUT_OF_MEMORY();
            for (int i = 0; i < src->index_count; i++)
                dst->indices[i] = clone_ast(src->indices[i]);
        } else {
            dst->indices = NULL;
        }
        dst->elementType = src->elementType;
        dst->dim_count = src->dim_count;
        dst->dim_sizes = NULL;
        dst->full_dim_sizes = NULL;
        dst->full_dim_count = 0;
        dst->is_heap_array = src->is_heap_array;
        dst->func_sig = NULL;
        return (ASTNode*)dst;
    }

    case AST_DEREF: {
        AST_Deref* src = (AST_Deref*)node;
        AST_Deref* dst = calloc(1, sizeof(AST_Deref));
        if (!dst) OUT_OF_MEMORY();
        dst->base.type = AST_DEREF;
        dst->base.next = NULL;
        dst->base.value = src->base.value;
        dst->operand = clone_ast(src->operand);
        dst->resultType = src->resultType;
        dst->ptr_depth = src->ptr_depth;
        dst->deref_array_info = NULL;
        return (ASTNode*)dst;
    }

    default:
        return NULL;
    }
}

void module_path_add(const char* path) {
    ModulePath* mp = malloc(sizeof(ModulePath));
    mp->path = strdup(path);
    mp->next = module_paths;
    module_paths = mp;
}

void module_path_add_defaults(const char* exe_dir) {
    module_path_add(".");
    if (exe_dir) {
        int len = strlen(exe_dir) + 5 + 1;
        char* lib_path = malloc(len);
        snprintf(lib_path, len, "%s%clib", exe_dir, PATH_SEPARATOR);
        module_path_add(lib_path);
        free(lib_path);
    }
    const char* env = getenv("YXG_PATH");
    if (env) {
        char* env_copy = strdup(env);
        char* token = strtok(env_copy,
            #ifdef _WIN32
                ";"
            #else
                ":"
            #endif
        );
        while (token) {
            module_path_add(token);
            token = strtok(NULL,
                #ifdef _WIN32
                    ";"
                #else
                    ":"
                #endif
            );
        }
        free(env_copy);
    }
}

static bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char* resolve_module(const char* module_name, const char* from_file) {
    #define MAX_SEARCH 32
    char* search_dirs[MAX_SEARCH];
    bool owned[MAX_SEARCH];
    int search_count = 0;

    if (from_file) {
        const char* sep1 = strrchr(from_file, '/');
        const char* sep2 = strrchr(from_file, '\\');
        const char* sep = (sep1 > sep2) ? sep1 : sep2;
        if (sep) {
            int dlen = (int)(sep - from_file);
            if (dlen > 0 && search_count < MAX_SEARCH) {
                char* dir = malloc(dlen + 1);
                memcpy(dir, from_file, dlen);
                dir[dlen] = '\0';
                search_dirs[search_count] = dir;
                owned[search_count] = true;
                search_count++;
            }
        }
    }

    for (ModulePath* mp = module_paths; mp && search_count < MAX_SEARCH; mp = mp->next) {
        search_dirs[search_count] = mp->path;
        owned[search_count] = false;
        search_count++;
    }

    bool is_path_form = (strchr(module_name, '/') != NULL
                      || strchr(module_name, '\\') != NULL
                      || strstr(module_name, ".yxg") != NULL);

    char* target;
    if (is_path_form) {
        target = strdup(module_name);
    } else {
        target = strdup(module_name);
        for (char* p = target; *p; p++) {
            if (*p == '.') *p = PATH_SEPARATOR;
        }
        int len = strlen(target) + 5;
        char* with_ext = malloc(len);
        snprintf(with_ext, len, "%s.yxg", target);
        free(target);
        target = with_ext;
    }

    char* result = NULL;
    for (int i = 0; i < search_count; i++) {
        int len = strlen(search_dirs[i]) + 1 + strlen(target) + 1;
        char* full = malloc(len);
        snprintf(full, len, "%s%c%s", search_dirs[i], PATH_SEPARATOR, target);
        if (file_exists(full)) {
            result = realpath_wrapper(full);
            free(full);
            break;
        }
        free(full);
    }

    if (!result && file_exists(target)) {
        result = realpath_wrapper(target);
    }

    free(target);
    for (int i = 0; i < search_count; i++) {
        if (owned[i]) free(search_dirs[i]);
    }

    #undef MAX_SEARCH
    return result;
}

char* realpath_wrapper(const char* path) {
    char* resolved = malloc(MAX_PATH);
    if (!resolved) return NULL;
#ifdef _WIN32
    if (_fullpath(resolved, path, MAX_PATH) == NULL) {
        free(resolved);
        return NULL;
    }
#else
    if (realpath(path, resolved) == NULL) {
        free(resolved);
        return NULL;
    }
#endif
    return resolved;
}

bool is_already_imported(const char* abs_path) {
    for (int i = 0; i < imported_count; i++) {
        if (strcmp(imported_modules[i], abs_path) == 0)
            return true;
    }
    return false;
}

void mark_imported(const char* abs_path) {
    if (imported_count >= imported_capacity) {
        imported_capacity = (imported_capacity == 0) ? IMPORTED_CAPACITY : imported_capacity * 2;
        imported_modules = realloc(imported_modules, sizeof(char*) * imported_capacity);
    }
    imported_modules[imported_count++] = strdup(abs_path);
}

void module_system_cleanup(void) {
    ModulePath* mp = module_paths;
    while (mp) {
        ModulePath* next = mp->next;
        free(mp->path);
        free(mp);
        mp = next;
    }
    module_paths = NULL;
    for (int i = 0; i < imported_count; i++)
        free(imported_modules[i]);
    free(imported_modules);
    imported_modules = NULL;
    imported_count = 0;
    imported_capacity = 0;

    while (imported_lexers) {
        ImportedLexer* next = imported_lexers->next;
        lexer_free(imported_lexers->lex);
        free(imported_lexers);
        imported_lexers = next;
    }
}

bool is_var_type(TokenType type) {
    return type == TOKEN_INT || type == TOKEN_FLOAT || type == TOKEN_STRING ||
        type == TOKEN_CHAR || type == TOKEN_BOOL || type == TOKEN_VOID ||
        type == TOKEN_UINT || type == TOKEN_UFLOAT || type == TOKEN_STR ||
        type == TOKEN_INT8 || type == TOKEN_INT16 || type == TOKEN_INT32 ||
        type == TOKEN_INT64 || type == TOKEN_INT128 ||
        type == TOKEN_UINT8 || type == TOKEN_UINT16 || type == TOKEN_UINT32 ||
        type == TOKEN_UINT64 || type == TOKEN_UINT128 ||
        type == TOKEN_FLOAT8 || type == TOKEN_FLOAT16 || type == TOKEN_FLOAT32 ||
        type == TOKEN_FLOAT64 || type == TOKEN_FLOAT128 ||
        type == TOKEN_UFLOAT8 || type == TOKEN_UFLOAT16 || type == TOKEN_UFLOAT32 ||
        type == TOKEN_UFLOAT64 || type == TOKEN_UFLOAT128;
}

static ASTNode* parse_dim_expression(Parser* parser, int* out_size) {
    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
        *out_size = -1;
        return NULL;
    }

    if (parser_current_is_type(parser, TOKEN_NUMBER)) {
        TokenType next = parser->tokens[parser->index + 1].type;
        if (next == TOKEN_SQUARE_BRACKET_RIGHT) {
            *out_size = atoi(parser->current.value);
            parser_eat(parser, TOKEN_NUMBER);
            return NULL;
        }
    }
    ASTNode* expr = parser_Binary(parser);
    *out_size = -2;
    return expr;
}

/* Detect and consume a nested function-pointer parameter declarator such as
   "int (*f)(int)", "int (*)(int)" or "int (*g)(int (*)(int), int)".
   The whole declarator (return type included) is consumed and the recursive
   signature is returned through out_sig.  Returns false without consuming
   anything when the tokens at the cursor are not such a declarator. */
static bool parser_try_nested_func_ptr_param(Parser* parser,
                                             DataType* out_base,
                                             FuncSignature** out_sig) {
    *out_base = DATA_ERROR;
    *out_sig = NULL;

    TokenType t = parser->current.type;
    if (!is_var_type(t) && t != TOKEN_IDENTIFIER && t != TOKEN_VOID)
        return false;

    int scan = parser->index + 1;
    while (parser->tokens[scan].type == TOKEN_DOT
           && parser->tokens[scan + 1].type == TOKEN_IDENTIFIER)
        scan += 2;
    if (parser->tokens[scan].type == TOKEN_LT) {
        int depth = 1;
        scan++;
        while (parser->tokens[scan].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[scan].type == TOKEN_LT) depth++;
            else if (parser->tokens[scan].type == TOKEN_GT) depth--;
            else if (parser->tokens[scan].type == TOKEN_SHIFT_RIGHT) {
                depth -= 2;
                if (depth < 0) depth = 0;
            }
            scan++;
        }
    }
    if (parser->tokens[scan].type != TOKEN_PAREN_LEFT
        || parser->tokens[scan + 1].type != TOKEN_MUL)
        return false;

    int saved = parser->index;
    FuncSignature* sig = parse_func_ptr_type_signature(parser);
    if (!sig) {
        parser->index = saved;
        parser->current = parser->tokens[parser->index];
        return false;
    }
    *out_base = DATA_INT;
    *out_sig = sig;
    return true;
}

static bool parse_return_type_func_ptr(Parser* parser, AST_Function* func,
                                        DataType base_return_type,
                                        int base_ptr_depth) {
    if (!parser_current_is_type(parser, TOKEN_PAREN_LEFT))
        return false;
    int saved = parser->index;
    int chk = saved + 1;
    int stars = 0;
    while (parser->tokens[chk].type == TOKEN_MUL) {
        stars++;
        chk++;
    }
    if (stars > 0 && parser->tokens[chk].type == TOKEN_IDENTIFIER
        && parser->tokens[chk + 1].type == TOKEN_PAREN_RIGHT) {
        return false;
    }

    bool is_multi = false;
    int look = saved + 1;
    if (is_var_type(parser->tokens[look].type)
        || parser->tokens[look].type == TOKEN_IDENTIFIER) {
        int p = look;
        int depth = 1;
        bool looks_like_multi = false;
        while (p < 200 && depth > 0) {
            TokenType t = parser->tokens[p].type;
            if (t == TOKEN_PAREN_LEFT) depth++;
            if (t == TOKEN_PAREN_RIGHT) { depth--; if (depth == 0) break; }
            if (t == TOKEN_VARARG) looks_like_multi = true;
            if (t == TOKEN_COMMA) looks_like_multi = true;
            p++;
        }
        if (depth == 0 && parser->tokens[p + 1].type == TOKEN_PAREN_LEFT
            && parser->tokens[p + 2].type == TOKEN_MUL) {
            is_multi = looks_like_multi;
        }
    }

    FuncSignature* sig = calloc(1, sizeof(FuncSignature));
    if (!sig) OUT_OF_MEMORY();

    if (is_multi) {
        parser_eat(parser, TOKEN_PAREN_LEFT);
        sig->multi_return = parse_multi_return_type(parser);
        sig->return_type = DATA_VOID;
    } else {
        sig->return_type = base_return_type;
    }

    parser_eat(parser, TOKEN_PAREN_LEFT);
    while (parser_current_is_type(parser, TOKEN_MUL)) {
        func->return_ptr_depth++;
        parser_advance(parser);
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    parser_eat(parser, TOKEN_PAREN_LEFT);
    int param_cap = 4;
    int param_count = 0;
    DataType* param_types = malloc(sizeof(DataType) * param_cap);
    int* param_type_param_indices = malloc(sizeof(int) * param_cap);
    bool func_ptr_has_varargs = false;
    int* func_ptr_vararg_indices = NULL;
    int func_ptr_vararg_count = 0;
    int func_ptr_vararg_cap = 4;
    func_ptr_vararg_indices = malloc(sizeof(int) * func_ptr_vararg_cap);

    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF)) {
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            if (param_count > 0) {
                func_ptr_has_varargs = true;
                if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                    func_ptr_vararg_cap *= 2;
                    func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                        sizeof(int) * func_ptr_vararg_cap);
                }
                func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
            }
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        if (!is_var_type(parser->current.type)
            && parser->current.type != TOKEN_IDENTIFIER) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected type in function pointer parameters");
            break;
        }
        DataType pt = Get_Datatype(parser->current.type);
        int pt_idx = (pt == DATA_IDENTIFIER)
                     ? resolve_type_param_index(parser->current.value) : -1;
        parser_advance(parser);
        if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
            parser_advance(parser);
        bool is_vararg = false;
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            is_vararg = true;
            parser_advance(parser);
        }
        if (param_count >= param_cap) {
            param_cap *= 2;
            param_types = realloc(param_types, sizeof(DataType) * param_cap);
            param_type_param_indices = realloc(param_type_param_indices, sizeof(int) * param_cap);
        }
        param_types[param_count] = pt;
        param_type_param_indices[param_count] = pt_idx;
        if (is_vararg) {
            func_ptr_has_varargs = true;
            if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                func_ptr_vararg_cap *= 2;
                func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                    sizeof(int) * func_ptr_vararg_cap);
            }
            func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
        }
        param_count++;
        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                func_ptr_has_varargs = true;
                if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                    func_ptr_vararg_cap *= 2;
                    func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                        sizeof(int) * func_ptr_vararg_cap);
                }
                func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    sig->param_count = param_count;
    sig->param_types = param_types;
    sig->param_type_param_indices = param_type_param_indices;
    sig->has_varargs = func_ptr_has_varargs;
    sig->vararg_param_count = func_ptr_vararg_count;
    sig->vararg_param_indices = (func_ptr_vararg_count > 0) ? func_ptr_vararg_indices : NULL;
    if (func_ptr_vararg_count == 0) free(func_ptr_vararg_indices);

    if (func->ret_func_sig) free_func_sig_recursive(func->ret_func_sig);
    func->ret_func_sig = sig;

    return true;
}

Parser* parser_init(Token* tokens, const char* filename)
{
    if (tokens == NULL)
    {
        INTERNAL_ERROR("parser_init: tokens is NULL");
    }
    Parser* parser = malloc(sizeof(Parser));
    if (parser == NULL)
    {
        INTERNAL_ERROR("parser_init: parser allocation failed");
    }
    parser->tokens = tokens;
    parser->index = 0;
    parser->current = parser->tokens[parser->index];
    parser->previous = parser->tokens[0];
    parser->filename = filename;
    parser_filename = filename;
    return parser;
}

void parser_advance(Parser* parser)
{
    if (parser->current.type == TOKEN_EOF)
    {
        Token prev = parser->previous;
        int err_line   = prev.line;
        int err_column = prev.column + strlen(prev.value);
        ERROR(ERR_PARSE_UNEXPECTED_EOF,
              LOC(parser->filename, err_line, err_column),
              "unexpected end of file");
        return;
    }
    parser->previous = parser->current;
    parser->current = parser->tokens[++parser->index];
}

bool parser_current_is_type(Parser* parser, TokenType type)
{
    return parser->current.type == type;
}

void parser_eat(Parser* parser, TokenType expected)
{
    if (!parser_current_is_type(parser, expected))
    {
        int err_line   = parser->previous.line;
        int err_column = parser->previous.column + strlen(parser->previous.value);
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, err_line, err_column),
              "expected '%s' before '%s'",
              type_to_string(expected),
              type_to_string(parser->current.type));
    }
    if (!parser_current_is_type(parser, TOKEN_EOF))
    {
        parser_advance(parser);
    }
}

ASTNode* parser_parse(Parser* parser)
{
    if (parser == NULL)
    {
        INTERNAL_ERROR("parser_parse: parser is NULL");
    }
    ASTNode* head = NULL;
    ASTNode* tail = NULL;

    while (!parser_current_is_type(parser, TOKEN_EOF))
    {
        ASTNode* stmt = parser_Sentence(parser, false);
        if (stmt == NULL)
        {
            while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                   !parser_current_is_type(parser, TOKEN_EOF) &&
                   !is_Modifier(parser))
            {
                parser_advance(parser);
            }
            if (parser_current_is_type(parser, TOKEN_SEMICOLON))
                parser_advance(parser);
            continue;
        }

        ASTNode* stmt_tail = stmt;
        while (stmt_tail->next != NULL) {
            stmt_tail = stmt_tail->next;
        }
        if (head == NULL) {
            head = stmt;
            tail = stmt_tail;
        } else {
            tail->next = stmt;
            tail = stmt_tail;
        }
    }
    return head;
}

ASTNode* parser_parse_code_block(Parser* parser)
{
    if (parser == NULL)
    {
        INTERNAL_ERROR("parser_parse_code_block: parser is NULL");
    }
    ASTNode* head = NULL;
    ASTNode* tail = NULL;

    while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
           !parser_current_is_type(parser, TOKEN_EOF))
    {
        ASTNode* stmt = parser_Sentence(parser, true);
        if (stmt == NULL)
        {
            while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                   !parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
            {
                parser_advance(parser);
            }
            if (parser_current_is_type(parser, TOKEN_SEMICOLON))
                parser_advance(parser);
            continue;
        }

        ASTNode* stmt_tail = stmt;
        while (stmt_tail->next != NULL) {
            stmt_tail = stmt_tail->next;
        }
        if (head == NULL) {
            head = stmt;
            tail = stmt_tail;
        } else {
            tail->next = stmt;
            tail = stmt_tail;
        }
    }
    return head;
}

bool is_Modifier(Parser* parser)
{
    return parser->current.type == TOKEN_PUB || parser->current.type == TOKEN_PRO ||
        parser->current.type == TOKEN_PRI || parser->current.type == TOKEN_STATIC ||
            parser->current.type == TOKEN_NATIVE ||
            parser->current.type == TOKEN_VIRTUAL ||
            parser->current.type == TOKEN_OVERRIDE ||
            parser->current.type == TOKEN_CONST;
}

ASTNode* parser_create_literal(Token tok) {
    AST_Literal* lit = calloc(1,sizeof(AST_Literal));
    lit->base.type = AST_LITERAL;
    lit->base.next = NULL;
    lit->base.value = tok;
    lit->dataType = DATA_IDENTIFIER;
    lit->internal_name = NULL;
    lit->is_call_function = false;
    lit->call_function_ParameterCount = 0;
    lit->method_arguments = NULL;
    lit->method_arg_count = 0;
    lit->is_field_ref = false;
    lit->field_offset = 0;
    lit->multi_return_type = NULL;
    lit->multi_ret_buf = NULL;
    return (ASTNode*)lit;
}

static void parser_apply_var_modifiers(Parser* parser, ASTNode* node,
                                        int modifier, bool is_static,
                                        bool is_const) {
    while (node) {
        if (node->type == AST_VAR || node->type == AST_UNINIT_VAR) {
            AST_Var* v = (AST_Var*)node;
            v->is_static = is_static;
            v->modifier = modifier;
            if (v->level_const_mask != 0) {
                v->is_const = false;
            } else if (v->ptr_depth > 0 && is_const) {
                v->level_const_mask = 1u << (v->ptr_depth - 1);
                v->is_const = false;
            } else if (is_const || v->is_const) {
                if (node->type == AST_UNINIT_VAR) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, v->name.line, v->name.column),
                          "'const' variable '%s' must be initialized",
                          v->name.value);
                } else {
                    v->is_const = true;
                }
            }
        } else if (node->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* a = (AST_ArrayDecl*)node;
            if (is_const) a->is_const = true;
            a->modifier = (is_static ? 3 : modifier);
        }
        node = node->next;
    }
}

static ASTNode* parser_finish_lvalue_stmt(Parser* parser, ASTNode* temp) {
    if (!temp) return NULL;
    ASTType t = temp->type;
    if (t != AST_ARRAY_ACCESS && t != AST_DEREF && t != AST_MEMBER_ACCESS) {
        return temp;
    }
    if (t == AST_MEMBER_ACCESS) {
        TokenType ct = parser->current.type;
        if (ct == TOKEN_ASSIGN || ct == TOKEN_ADD_ASSIGN ||
            ct == TOKEN_SUB_ASSIGN || ct == TOKEN_MUL_ASSIGN ||
            ct == TOKEN_DIV_ASSIGN || ct == TOKEN_MOD_ASSIGN) {
            return parser_member_assign(parser, temp);
        }
        return temp;
    }
    TokenType ct = parser->current.type;
    if (ct != TOKEN_ASSIGN && ct != TOKEN_ADD_ASSIGN &&
        ct != TOKEN_SUB_ASSIGN && ct != TOKEN_MUL_ASSIGN &&
        ct != TOKEN_DIV_ASSIGN && ct != TOKEN_MOD_ASSIGN) {
        return temp;
    }
    Token op = parser->current;
    parser_advance(parser);
    ASTNode* value = parser_Binary(parser);
    if (op.type != TOKEN_ASSIGN) {
        TokenType bin_op_type;
        switch (op.type) {
        case TOKEN_ADD_ASSIGN: bin_op_type = TOKEN_ADD; break;
        case TOKEN_SUB_ASSIGN: bin_op_type = TOKEN_SUB; break;
        case TOKEN_MUL_ASSIGN: bin_op_type = TOKEN_MUL; break;
        case TOKEN_DIV_ASSIGN: bin_op_type = TOKEN_DIV; break;
        case TOKEN_MOD_ASSIGN: bin_op_type = TOKEN_MOD; break;
        default: bin_op_type = TOKEN_ADD; break;
        }
        AST_Binary* bin = calloc(1, sizeof(AST_Binary));
        bin->base.type = AST_BINARY;
        bin->base.next = NULL;
        bin->left = clone_ast(temp);
        bin->op = (Token){.value = strdup(type_to_string(bin_op_type)),
                             .type = bin_op_type,
                             .line = op.line, .column = op.column};
        bin->right = value;
        bin->resultType = DATA_ERROR;
        value = (ASTNode*)bin;
    }
    if (t == AST_ARRAY_ACCESS) {
        AST_ArrayAss* ass = calloc(1, sizeof(AST_ArrayAss));
        ass->base.type = AST_ARRAY_ASS;
        ass->base.next = NULL;
        ass->base.value = temp->value;
        ass->target = temp;
        ass->value = value;
        ass->elementType = DATA_ERROR;
        return (ASTNode*)ass;
    } else if (t == AST_DEREF) {
        AST_Deref* d = (AST_Deref*)temp;
        AST_Var* assign = calloc(1, sizeof(AST_Var));
        assign->base.type = AST_VAR_ASS;
        assign->base.next = NULL;
        assign->base.value = d->base.value;
        assign->name = d->base.value;
        assign->Value = value;
        assign->type = TOKEN_MUL;
        assign->dataType = DATA_ERROR;
        assign->internal_name = NULL;
        assign->ptr_depth = -1;
        assign->deref_target = d->operand;
        assign->type_param_index = -1;
        d->operand = NULL;
        free(d);
        return (ASTNode*)assign;
    }
    return temp;
}

ASTNode* parser_Sentence(Parser* parser, bool is_block)
{
    ASTNode* temp = NULL;
    int modifier = 0;
    bool is_static = false;
    bool is_native = false;
    bool is_virtual = false;
    bool is_override = false;
    bool is_const = false;
    bool return_pointee_const = false;
    bool has_visibility = false;
    g_native_requested = false;
    if (is_Modifier(parser)) {
        while (is_Modifier(parser))
        {
            TokenType t = parser->current.type;
            if (t == TOKEN_PUB || t == TOKEN_PRO || t == TOKEN_PRI) {
                if (has_visibility) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate visibility modifier '%s'",
                          type_to_string(t));
                }
                has_visibility = true;
                if (t == TOKEN_PUB)      modifier = 0;
                else if (t == TOKEN_PRO) modifier = 1;
                else                     modifier = 2;
            }
            else if (t == TOKEN_STATIC) {
                if (is_static) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate 'static' modifier");
                }
                is_static = true;
            }
            else if (t == TOKEN_NATIVE) {
                if (is_native) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate 'native' modifier");
                }
                is_native = true;
            }
            else if (t == TOKEN_VIRTUAL) {
                if (is_virtual) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate 'virtual' modifier");
                }
                is_virtual = true;
            }
            else if (t == TOKEN_OVERRIDE) {
                if (is_override) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate 'override' modifier");
                }
                is_override = true;
            }
            else if (t == TOKEN_CONST) {
                int look = parser->index + 1;
                bool is_ret_const = false;
                if (is_var_type(parser->tokens[look].type)
                    || parser->tokens[look].type == TOKEN_IDENTIFIER) {
                    look++;
                    if (parser->tokens[look].type == TOKEN_MUL) {
                        look++;
                        while (parser->tokens[look].type == TOKEN_MUL) look++;
                        if (parser->tokens[look].type == TOKEN_IDENTIFIER) {
                            look++;
                            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) {
                                is_ret_const = true;
                            }
                        }
                    }
                }
                if (is_ret_const) {
                    return_pointee_const = true;
                    parser_advance(parser);
                    continue;
                }
                if (is_const) {
                    ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "duplicate 'const' modifier");
                }
                is_const = true;
            }
            parser_advance(parser);
        }

        if (is_native) {
            int name_idx = skip_to_name(parser);
            bool looks_func =
                parser->current.type == TOKEN_PAREN_LEFT ||
                (parser->tokens[parser->index + 1].type == TOKEN_PAREN_LEFT) ||
                (parser->tokens[name_idx].type == TOKEN_IDENTIFIER &&
                 parser->tokens[name_idx + 1].type == TOKEN_PAREN_LEFT);
            if (looks_func) {
                g_native_requested = true;
            } else {
                ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "'native' can only be used on function declarations");
            }
        }

        if (parser->current.type == TOKEN_LT) {
            TypeParam* tparams = NULL;
            int tp_count = parser_parse_type_params(parser, &tparams);
            if (tp_count < 0) {
                while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                       !parser_current_is_type(parser, TOKEN_EOF))
                    parser_advance(parser);
                return NULL;
            }
            if (parser->current.type == TOKEN_CLASS) {
                ASTNode* cls = parser_class_def(parser, modifier);
                if (cls && cls->type == AST_CLASS_DEF) {
                    AST_ClassDef* cd = (AST_ClassDef*)cls;
                    cd->type_params      = tparams;
                    cd->type_param_count = tp_count;
                    cd->is_generic       = true;
                } else {
                    free_type_params(tparams, tp_count);
                }
                return cls;
            }
            ASTNode* func = parser_def_function_with_generics(
                parser, modifier, tparams, tp_count);
            if (func && func->type == AST_FUNCTION) {
                ((AST_Function*)func)->is_static = is_static;
                ((AST_Function*)func)->is_virtual = is_virtual;
                ((AST_Function*)func)->is_override = is_override;
                ((AST_Function*)func)->return_pointee_const = return_pointee_const;
            }
            return func;
        }

        if (parser->current.type == TOKEN_CLASS) {
            return parser_class_def(parser, modifier);
        }

        if (parser->current.type == TOKEN_ENUM) {
            return parser_enum_def(parser);
        }

        if (parser->current.type == TOKEN_PAREN_LEFT) {
            ASTNode* func = parser_def_function_with_modifier(parser, modifier);
            if (func && func->type == AST_FUNCTION) {
                ((AST_Function*)func)->is_static = is_static;
                ((AST_Function*)func)->is_virtual = is_virtual;
                ((AST_Function*)func)->is_override = is_override;
                ((AST_Function*)func)->return_pointee_const = return_pointee_const;
            }
            return func;
        }

        if (parser->current.type == TOKEN_IDENTIFIER &&
            parser->tokens[parser->index + 1].type == TOKEN_PAREN_LEFT) {
            ASTNode* ctor = parser_constructor_def(parser, modifier);
            if (ctor && ctor->type == AST_FUNCTION) {
                ((AST_Function*)ctor)->is_static = is_static;
                ((AST_Function*)ctor)->is_native = is_native;
                ((AST_Function*)ctor)->is_virtual = is_virtual;
                ((AST_Function*)ctor)->is_override = is_override;
                ((AST_Function*)ctor)->return_pointee_const = return_pointee_const;
            }
            return ctor;
        }


        if (is_var_type(parser->current.type)) {
            int name_idx = skip_to_name(parser);

            if (parser->tokens[name_idx].type == TOKEN_IDENTIFIER &&
                parser->tokens[name_idx + 1].type == TOKEN_PAREN_LEFT) {
                ASTNode* func = parser_def_function_with_modifier(parser, modifier);
                if (func && func->type == AST_FUNCTION) {
                    ((AST_Function*)func)->is_static = is_static;
                    ((AST_Function*)func)->is_virtual = is_virtual;
                    ((AST_Function*)func)->is_override = is_override;
                    ((AST_Function*)func)->return_pointee_const = return_pointee_const;
                }
                return func;
            } else {
                temp = parser_var(parser, true);
                if (temp) {
                    parser_apply_var_modifiers(parser, temp, modifier, is_static, is_const);
                }
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            }
        } else if (parser->current.type == TOKEN_IDENTIFIER) {
            int name_idx = skip_to_name(parser);
            if (parser->tokens[name_idx].type == TOKEN_IDENTIFIER &&
                parser->tokens[name_idx + 1].type == TOKEN_PAREN_LEFT) {
                ASTNode* func = parser_def_function_with_modifier(parser, modifier);
                if (func && func->type == AST_FUNCTION) {
                    ((AST_Function*)func)->is_static = is_static;
                    ((AST_Function*)func)->is_virtual = is_virtual;
                    ((AST_Function*)func)->is_override = is_override;
                    ((AST_Function*)func)->return_pointee_const = return_pointee_const;
                }
                return func;
            } else if (parser->tokens[parser->index + 1].type == TOKEN_LT) {
                temp = parser_var_with_typename(parser);
                if (temp) {
                    parser_apply_var_modifiers(parser, temp, modifier, is_static, is_const);
                }
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            } else {
                temp = parser_var(parser, true);
                if (temp) {
                    parser_apply_var_modifiers(parser, temp, modifier, is_static, is_const);
                }
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            }
        }

        if (parser->current.type == TOKEN_UNSAFE) {
            parser_advance(parser);

            if (is_var_type(parser->current.type) ||
                parser->current.type == TOKEN_IDENTIFIER) {
                int name_idx = skip_to_name(parser);
                if (parser->tokens[name_idx].type == TOKEN_IDENTIFIER &&
                    parser->tokens[name_idx + 1].type == TOKEN_PAREN_LEFT) {
                    ASTNode* func = parser_def_function_with_modifier(parser, modifier);
                    if (func && func->type == AST_FUNCTION) {
                        ((AST_Function*)func)->is_static = is_static;
                        ((AST_Function*)func)->is_virtual = is_virtual;
                        ((AST_Function*)func)->is_override = is_override;
                    }
                    return func;
                }
            }
            
            temp = parser_var(parser, true);
            if (temp) {
                ASTNode* n = temp;
                while (n) {
                    if (n->type == AST_VAR || n->type == AST_UNINIT_VAR) {
                        ((AST_Var*)n)->is_unsafe = true;
                        ((AST_Var*)n)->is_static = is_static;
                        ((AST_Var*)n)->modifier = modifier;
                    } else if (n->type == AST_ARRAY_DECL) {
                        ((AST_ArrayDecl*)n)->is_unsafe = true;
                        ((AST_ArrayDecl*)n)->modifier = (is_static ? 3 : modifier);
                    }
                    n = n->next;
                }
            }
            parser_eat(parser, TOKEN_SEMICOLON);
            return temp;
        }

        if (parser->current.type == TOKEN_IDENTIFIER) {
            TokenType next = parser->tokens[parser->index + 1].type;
            if (next == TOKEN_PAREN_LEFT) {
                return parser_constructor_def(parser, modifier);
            }

            if (next == TOKEN_IDENTIFIER) {
                temp = parser_var_with_typename(parser);
                if (temp) {
                    if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                        ((AST_Var*)temp)->modifier = modifier;
                    }
                }
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            }

            if (next == TOKEN_LT) {
                int look2 = parser->index + 2;
                int depth2 = 1;
                while (parser->tokens[look2].type != TOKEN_EOF && depth2 > 0) {
                    if (parser->tokens[look2].type == TOKEN_LT) depth2++;
                    if (parser->tokens[look2].type == TOKEN_GT) depth2--;
                    if (depth2 > 0) look2++;
                }
                if (parser->tokens[look2].type == TOKEN_GT
                    && parser->tokens[look2 + 1].type == TOKEN_PAREN_LEFT) {
                    ASTNode* node = parser_Binary(parser);
                    parser_eat(parser, TOKEN_SEMICOLON);
                    return node;
                } else {
                    temp = parser_var_with_typename(parser);
                }
                if (temp) {
                    if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                        ((AST_Var*)temp)->modifier = modifier;
                    }
                }
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            }
        }

        ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "unexpected token '%s' after modifier",
              type_to_string(parser->current.type));
        return NULL;
    }

    if (parser->current.type == TOKEN_PRINT) {
        temp = parser_Print(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_INPUT) {
        temp = parser_input(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_INPUT_SET) {
        parser_eat(parser, TOKEN_INPUT_SET);
        parser_eat(parser, TOKEN_PAREN_LEFT);

        if (parser_current_is_type(parser, TOKEN_STRING_LITERAL)) {
            parser_advance(parser);
        } else {
            WARNING(ERR_PARSE_EXPECTED_TOKEN,
                    LOC(parser->filename, parser->current.line, parser->current.column),
                    "InputSet expects a string literal argument");
        }

        parser_eat(parser, TOKEN_PAREN_RIGHT);
        parser_eat(parser, TOKEN_SEMICOLON);

        AST_Null* nop = calloc(1, sizeof(AST_Null));
        if (!nop) OUT_OF_MEMORY();
        nop->base.type  = AST_NULL;
        nop->base.next  = NULL;
        nop->base.value = (Token){ .value = strdup("InputSet"),
                                    .type = TOKEN_NONE,
                                    .line = parser->current.line,
                                    .column = parser->current.column,
                                    .file = parser->filename };
        nop->ptr_depth = 0;
        temp = (ASTNode*)nop;
    } else if (is_var_type(parser->current.type)) {
        int name_idx = skip_to_name(parser);

        if (parser->tokens[name_idx].type == TOKEN_IDENTIFIER &&
            parser->tokens[name_idx + 1].type == TOKEN_PAREN_LEFT) {
            temp = parser_def_function(parser);
        } else {
            temp = parser_var(parser, true);
            parser_eat(parser, TOKEN_SEMICOLON);
        }
    } else if (parser->current.type == TOKEN_MUL) {
        Token star_token = parser->current;
        parser_advance(parser);
        ASTNode* operand = parser_Unary(parser);
        if (parser_current_is_type(parser, TOKEN_ASSIGN) ||
            parser_current_is_type(parser, TOKEN_ADD_ASSIGN) ||
            parser_current_is_type(parser, TOKEN_SUB_ASSIGN) ||
            parser_current_is_type(parser, TOKEN_MUL_ASSIGN) ||
            parser_current_is_type(parser, TOKEN_DIV_ASSIGN) ||
            parser_current_is_type(parser, TOKEN_MOD_ASSIGN)) {
            Token op = parser->current;
            parser_advance(parser);
            ASTNode* right = parser_Binary(parser);
            if (op.type != TOKEN_ASSIGN) {
                AST_Deref* deref_copy = calloc(1, sizeof(AST_Deref));
                deref_copy->base.type = AST_DEREF;
                deref_copy->base.next = NULL;
                deref_copy->base.value = star_token;
                deref_copy->operand = clone_ast(operand);
                deref_copy->resultType = DATA_ERROR;
                deref_copy->ptr_depth = 0;
                TokenType bin_op;
                switch (op.type) {
                    case TOKEN_ADD_ASSIGN: bin_op = TOKEN_ADD; break;
                    case TOKEN_SUB_ASSIGN: bin_op = TOKEN_SUB; break;
                    case TOKEN_MUL_ASSIGN: bin_op = TOKEN_MUL; break;
                    case TOKEN_DIV_ASSIGN: bin_op = TOKEN_DIV; break;
                    case TOKEN_MOD_ASSIGN: bin_op = TOKEN_MOD; break;
                    default: bin_op = TOKEN_ADD; break;
                }
                AST_Binary* bin = calloc(1, sizeof(AST_Binary));
                bin->base.type = AST_BINARY;
                bin->left = (ASTNode*)deref_copy;
                bin->op = (Token){ .value = strdup(type_to_string(bin_op)),
                                   .type = bin_op,
                                   .line = op.line, .column = op.column };
                bin->right = right;
                bin->resultType = DATA_ERROR;
                right = (ASTNode*)bin;
            }
            AST_Var* assign = calloc(1, sizeof(AST_Var));
            assign->base.type = AST_VAR_ASS;
            assign->base.next = NULL;
            assign->base.value = star_token;
            assign->name = star_token;
            assign->Value = right;
            assign->type = star_token.type;
            assign->dataType = DATA_ERROR;
            assign->internal_name = NULL;
            assign->ptr_depth = -1;
            assign->deref_target  = operand;
            assign->type_param_index = -1;
            temp = (ASTNode*)assign;
        } else {
            AST_Deref* deref = calloc(1, sizeof(AST_Deref));
            deref->base.type  = AST_DEREF;
            deref->base.next  = NULL;
            deref->base.value = star_token;
            deref->operand    = operand;
            deref->resultType = DATA_ERROR;
            deref->ptr_depth  = 0;
            temp = (ASTNode*)deref;
        }
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_UNSAFE) {
        temp = parser_var(parser, true);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_LT) {
        TypeParam* tparams = NULL;
        int tp_count = parser_parse_type_params(parser, &tparams);
        if (tp_count < 0) {
            while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            return NULL;
        }
        ASTNode* func = parser_def_function_with_generics(
            parser, 0, tparams, tp_count);
        return func;
    } else if (parser->current.type == TOKEN_PAREN_LEFT) {
        int saved = parser->index;
        int look = saved + 1;
        int depth = 1;
        while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) depth--;
            if (depth > 0) look++;
        }
        look++;
        if (parser->tokens[look].type == TOKEN_PAREN_LEFT &&
            parser->tokens[look + 1].type == TOKEN_MUL &&
            parser->tokens[look + 2].type == TOKEN_IDENTIFIER &&
            parser->tokens[look + 3].type == TOKEN_PAREN_RIGHT) {
            int after_params = look + 4;
            int pdepth = 0;
            while (parser->tokens[after_params].type != TOKEN_EOF) {
                if (parser->tokens[after_params].type == TOKEN_PAREN_LEFT) pdepth++;
                if (parser->tokens[after_params].type == TOKEN_PAREN_RIGHT) {
                    pdepth--;
                    if (pdepth == 0) break;
                }
                after_params++;
            }
            if (parser->tokens[look + 4].type == TOKEN_PAREN_LEFT
                && parser->tokens[after_params + 1].type == TOKEN_BRACKET_LEFT) {
                parser->index = saved;
                parser->current = parser->tokens[parser->index];
                temp = parser_def_function(parser);
            } else {
                parser->index = saved;
                parser->current = parser->tokens[parser->index];
                temp = parser_multi_return_func_ptr_var(parser);
                parser_eat(parser, TOKEN_SEMICOLON);
            }
        } else if (parser->tokens[look].type != TOKEN_PAREN_LEFT) {
            /* leading (*expr) not followed by ( : expression statement */
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
            ASTNode* lv_stmt = parser_Unary(parser);
            temp = parser_finish_lvalue_stmt(parser, lv_stmt);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
            temp = parser_def_function(parser);
        }
    } else if (parser->current.type == TOKEN_IDENTIFIER) {
        TokenType next = parser->tokens[parser->index + 1].type;
        TokenType next2 = parser->tokens[parser->index + 2].type;

        if (next == TOKEN_SQUARE_BRACKET_LEFT) {
            int idx = parser->index + 1;
            while (parser->tokens[idx].type == TOKEN_SQUARE_BRACKET_LEFT) {
                int depth = 0;
                while (parser->tokens[idx].type != TOKEN_EOF) {
                    if (parser->tokens[idx].type == TOKEN_SQUARE_BRACKET_LEFT) {
                        depth++;
                    } else if (parser->tokens[idx].type == TOKEN_SQUARE_BRACKET_RIGHT) {
                        depth--;
                        idx++;
                        if (depth == 0) break;
                        continue;
                    }
                    idx++;
                }
            }
            if (parser->tokens[idx].type == TOKEN_IDENTIFIER) {
                temp = parser_var(parser, true);
                parser_eat(parser, TOKEN_SEMICOLON);
                return temp;
            }
        }

        if (next == TOKEN_MUL) {
            temp = parser_var(parser, true);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else if (next == TOKEN_IDENTIFIER && next2 == TOKEN_PAREN_LEFT) {
            temp = parser_def_function(parser);
        } else if (next == TOKEN_IDENTIFIER &&
                 (next2 == TOKEN_ASSIGN || next2 == TOKEN_SEMICOLON
                  || next2 == TOKEN_SQUARE_BRACKET_LEFT)) {
            temp = parser_var_with_typename(parser);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else if (next == TOKEN_ASSIGN) {
            temp = parser_def_var(parser);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else if (next == TOKEN_LT) {
            int look = parser->index + 2;
            int depth = 1;
            while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                if (parser->tokens[look].type == TOKEN_LT) depth++;
                if (parser->tokens[look].type == TOKEN_GT) depth--;
                if (depth > 0) look++;
            }
            if (parser->tokens[look].type == TOKEN_GT
                && parser->tokens[look + 1].type == TOKEN_PAREN_LEFT) {
                temp = parser_Binary(parser);
                parser_eat(parser, TOKEN_SEMICOLON);
            } else {
                temp = parser_var_with_typename(parser);
                parser_eat(parser, TOKEN_SEMICOLON);
            }
        } else if (next == TOKEN_DOT || next == TOKEN_ARROW) {
            bool is_type_decl = false;
            bool is_func_decl = false;
            if (next == TOKEN_DOT) {
                int look = parser->index + 1;
                int last_ident = -1;
                while (parser->tokens[look].type == TOKEN_DOT
                       && parser->tokens[look + 1].type == TOKEN_IDENTIFIER) {
                    last_ident = look + 1;
                    look += 2;
                }
                if (last_ident >= 0) {
                    int after_chain = look;
                    while (parser->tokens[after_chain].type == TOKEN_SQUARE_BRACKET_LEFT) {
                        int d = 1;
                        after_chain++;
                        while (d > 0 && parser->tokens[after_chain].type != TOKEN_EOF) {
                            if (parser->tokens[after_chain].type == TOKEN_SQUARE_BRACKET_LEFT) d++;
                            else if (parser->tokens[after_chain].type == TOKEN_SQUARE_BRACKET_RIGHT) d--;
                            after_chain++;
                        }
                    }
                    if (parser->tokens[after_chain].type == TOKEN_IDENTIFIER) {
                        TokenType after = parser->tokens[after_chain + 1].type;
                        if (after == TOKEN_ASSIGN || after == TOKEN_SEMICOLON
                            || after == TOKEN_SQUARE_BRACKET_LEFT) {
                            is_type_decl = true;
                        } else if (after == TOKEN_PAREN_LEFT) {
                            is_func_decl = true;
                        }
                    }
                }
            }
            if (is_type_decl) {
                temp = parser_var(parser, true);
                parser_eat(parser, TOKEN_SEMICOLON);
            } else if (is_func_decl) {
                temp = parser_def_function(parser);
            } else {
                ASTNode* left = parser_Unary(parser);
                temp = parser_finish_lvalue_stmt(parser, left);
                parser_eat(parser, TOKEN_SEMICOLON);
            }
        } else if (next == TOKEN_INCREMENT || next == TOKEN_DECREMENT ||
                 next == TOKEN_ADD_ASSIGN || next == TOKEN_SUB_ASSIGN ||
                 next == TOKEN_MUL_ASSIGN || next == TOKEN_DIV_ASSIGN ||
                 next == TOKEN_MOD_ASSIGN || next == TOKEN_SHIFT_LEFT_ASSIGN ||
                 next == TOKEN_SHIFT_RIGHT_ASSIGN || next == TOKEN_BITWISE_XOR_ASSIGN) {
            temp = parser_compound_assign_or_incdec(parser);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else if (next == TOKEN_PAREN_LEFT) {
            int look = parser->index + 2;
            int depth = 1;
            while (depth > 0 && parser->tokens[look].type != TOKEN_EOF) {
                if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) depth--;
                if (depth > 0) look++;
            }
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT
                && parser->tokens[look + 1].type == TOKEN_BRACKET_LEFT) {
                temp = parser_constructor_def(parser, 0);
            } else {
                temp = parser_Binary(parser);
                parser_eat(parser, TOKEN_SEMICOLON);
            }
        } else {
            temp = parser_Binary(parser);
            if (temp && (temp->type == AST_ARRAY_ACCESS ||
                         temp->type == AST_DEREF ||
                         temp->type == AST_MEMBER_ACCESS)) {
                TokenType ct = parser->current.type;
                if (ct == TOKEN_ASSIGN || ct == TOKEN_ADD_ASSIGN ||
                    ct == TOKEN_SUB_ASSIGN || ct == TOKEN_MUL_ASSIGN ||
                    ct == TOKEN_DIV_ASSIGN || ct == TOKEN_MOD_ASSIGN) {
                    Token op = parser->current;
                    parser_advance(parser);
                    ASTNode* value = parser_Binary(parser);
                    if (op.type != TOKEN_ASSIGN) {
                        TokenType bin_op_type;
                        switch (op.type) {
                            case TOKEN_ADD_ASSIGN: bin_op_type = TOKEN_ADD; break;
                            case TOKEN_SUB_ASSIGN: bin_op_type = TOKEN_SUB; break;
                            case TOKEN_MUL_ASSIGN: bin_op_type = TOKEN_MUL; break;
                            case TOKEN_DIV_ASSIGN: bin_op_type = TOKEN_DIV; break;
                            case TOKEN_MOD_ASSIGN: bin_op_type = TOKEN_MOD; break;
                            default: bin_op_type = TOKEN_ADD; break;
                        }
                        AST_Binary* bin = calloc(1, sizeof(AST_Binary));
                        bin->base.type = AST_BINARY;
                        bin->base.next = NULL;
                        bin->left = clone_ast(temp);
                        bin->op = (Token){.value = strdup(type_to_string(bin_op_type)),
                                             .type = bin_op_type,
                                             .line = op.line, .column = op.column};
                        bin->right = value;
                        bin->resultType = DATA_ERROR;
                        value = (ASTNode*)bin;
                    }
                    if (temp->type == AST_ARRAY_ACCESS) {
                        AST_ArrayAss* ass = calloc(1, sizeof(AST_ArrayAss));
                        ass->base.type = AST_ARRAY_ASS;
                        ass->base.next = NULL;
                        ass->base.value = temp->value;
                        ass->target = temp;
                        ass->value = value;
                        ass->elementType = DATA_ERROR;
                        temp = (ASTNode*)ass;
                    } else if (temp->type == AST_DEREF) {
                        AST_Deref* d = (AST_Deref*)temp;
                        AST_Var* assign = calloc(1, sizeof(AST_Var));
                        assign->base.type = AST_VAR_ASS;
                        assign->base.next = NULL;
                        assign->base.value = d->base.value;
                        assign->name = d->base.value;
                        assign->Value = value;
                        assign->type = TOKEN_MUL;
                        assign->dataType = DATA_ERROR;
                        assign->internal_name = NULL;
                        assign->ptr_depth = -1;
                        assign->deref_target = d->operand;
                        assign->type_param_index = -1;
                        d->operand = NULL;
                        free(d);
                        temp = (ASTNode*)assign;
                    } else if (temp->type == AST_MEMBER_ACCESS) {
                        AST_MemberAccess* ma = (AST_MemberAccess*)temp;
                        AST_MemberAss* mass = calloc(1, sizeof(AST_MemberAss));
                        mass->base.type = AST_MEMBER_ASS;
                        mass->base.next = NULL;
                        mass->base.value = ma->base.value;
                        mass->object = ma->object;
                        mass->member_name = ma->member_name;
                        mass->op = op;
                        mass->value = value;
                        mass->member_type = DATA_ERROR;
                        mass->member_offset = -1;
                        ma->object = NULL;
                        ma->member_name = NULL;
                        free(ma);
                        temp = (ASTNode*)mass;
                    }
                }
            }
            parser_eat(parser, TOKEN_SEMICOLON);
        }
    } else if (parser->current.type == TOKEN_THIS) {
        TokenType next = parser->tokens[parser->index + 1].type;
        if (next == TOKEN_DOT || next == TOKEN_ARROW) {
            ASTNode* left = parser_Unary(parser);
            temp = parser_finish_lvalue_stmt(parser, left);
            parser_eat(parser, TOKEN_SEMICOLON);
        } else {
            temp = parser_Binary(parser);
            parser_eat(parser, TOKEN_SEMICOLON);
        }
    } else if (parser->current.type == TOKEN_IF)
    {
        temp = parser_if(parser);
    } else if (parser->current.type == TOKEN_SWITCH) {
        temp = parser_switch(parser);
    } else if (parser->current.type == TOKEN_BREAK) {
        temp = parser_break(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_CONTINUE) {
        temp = parser_continue(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_TRY) {
        temp = parser_try(parser);
    } else if (parser->current.type == TOKEN_THROW) {
        temp = parser_throw_stmt(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_WHILE) {
        temp = parser_while(parser);
    } else if (parser->current.type == TOKEN_DO) {
        temp = parser_do_while(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_FOR) {
        temp = parser_for(parser);
    } else if (is_Modifier(parser) && !is_block) {
        temp = parser_def_function(parser);
    } else if (parser->current.type == TOKEN_RETURN) {
        temp = parser_return(parser);
        parser_eat(parser, TOKEN_SEMICOLON);
    } else if (parser->current.type == TOKEN_CLASS) {
        temp = parser_class_def(parser, 0);
    } else if (parser->current.type == TOKEN_ENUM) {
        temp = parser_enum_def(parser);
    } else if (parser->current.type == TOKEN_IMPORT) {
        ASTNode* imported = parser_import(parser);
        if (parser_current_is_type(parser, TOKEN_SEMICOLON))
            parser_eat(parser, TOKEN_SEMICOLON);
        if (imported == NULL) {
            AST_Null* nop = calloc(1, sizeof(AST_Null));
            if (!nop) OUT_OF_MEMORY();
            nop->base.type  = AST_NULL;
            nop->base.next  = NULL;
            nop->base.value = (Token){ .value = strdup("import"),
                                       .type = TOKEN_NONE,
                                       .line = parser->current.line,
                                       .column = parser->current.column,
                                       .file = parser->filename };
            nop->ptr_depth = 0;
            return (ASTNode*)nop;
        }
        return imported;
    } else {
        temp = parser_Binary(parser);
        temp = parser_finish_lvalue_stmt(parser, temp);
        parser_eat(parser, TOKEN_SEMICOLON);
    }
    return temp;
}

static ASTNode* parser_member_assign(Parser* parser, ASTNode* left)
{
    Token op = parser->current;
    parser_advance(parser);
    ASTNode* right = parser_Binary(parser);

    if (left->type != AST_MEMBER_ACCESS) {
        ERROR(ERR_PARSE_ILLEGAL_OPERAND,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "left side of member assignment must be a member access");
        free(right);
        return NULL;
    }

    AST_MemberAccess* access = (AST_MemberAccess*)left;
    char* member_name = access->member_name;
    access->member_name = NULL;

    if (op.type != TOKEN_ASSIGN) {
        ASTNode* obj_copy = clone_ast(access->object);
        AST_MemberAccess* ma_copy = calloc(1, sizeof(AST_MemberAccess));
        ma_copy->base.type = AST_MEMBER_ACCESS;
        ma_copy->object = obj_copy;
        ma_copy->member_name = strdup(member_name);
        ma_copy->vtable_index = -1;

        TokenType bin_op_type;
        switch (op.type) {
            case TOKEN_ADD_ASSIGN: bin_op_type = TOKEN_ADD; break;
            case TOKEN_SUB_ASSIGN: bin_op_type = TOKEN_SUB; break;
            case TOKEN_MUL_ASSIGN: bin_op_type = TOKEN_MUL; break;
            case TOKEN_DIV_ASSIGN: bin_op_type = TOKEN_DIV; break;
            case TOKEN_MOD_ASSIGN: bin_op_type = TOKEN_MOD; break;
            default:
                INTERNAL_ERROR("parser_member_assign: unknown compound assignment token");
                free(member_name);
                free(ma_copy);
                return NULL;
        }

        AST_Binary* bin = calloc(1, sizeof(AST_Binary));
        bin->base.type = AST_BINARY;
        bin->left = (ASTNode*)ma_copy;
        bin->op.type = bin_op_type;
        bin->op.value = strdup(type_to_string(bin_op_type));
        bin->right = right;
        right = (ASTNode*)bin;
    }

    AST_MemberAss* ass = calloc(1, sizeof(AST_MemberAss));
    ass->base.type = AST_MEMBER_ASS;
    ass->object = access->object;
    ass->member_name = member_name;
    ass->value = right;
    ass->member_type = DATA_ERROR;
    ass->member_offset = -1;

    free(access);
    return (ASTNode*)ass;
}

ASTNode* parser_return(Parser* parser)
{
    Token ret_tok = parser->current;
    parser_eat(parser, TOKEN_RETURN);

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        bool is_multi = false;
        {
            int saved = parser->index;
            int look = saved + 1;
            int depth = 1;
            while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                TokenType t = parser->tokens[look].type;
                if (t == TOKEN_PAREN_LEFT) {
                    depth++;
                } else if (t == TOKEN_PAREN_RIGHT) {
                    depth--;
                    if (depth == 0) break;
                } else if (depth == 1 && t == TOKEN_COMMA) {
                    is_multi = true;
                }
                look++;
            }
            if (!is_multi) {
                parser->index = saved;
                parser->current = parser->tokens[parser->index];
            }
        }

        if (is_multi) {
            parser_eat(parser, TOKEN_PAREN_LEFT);

            AST_MultiReturn* mr = calloc(1, sizeof(AST_MultiReturn));
            mr->base.type = AST_MULTI_RETURN;
            mr->base.value = ret_tok;
            mr->base.next = NULL;
            int cap = 4;
            mr->values = malloc(sizeof(ASTNode*) * cap);

            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) && !parser_current_is_type(parser, TOKEN_EOF)) {
                ASTNode* val;
                if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                    val = parser_vararg_group(parser);
                } else {
                    val = parser_Binary(parser);
                }
                if (mr->value_count >= cap) {
                    cap *= 2;
                    mr->values = realloc(mr->values, sizeof(ASTNode*) * cap);
                }
                mr->values[mr->value_count++] = val;
                if (!parser_current_is_type(parser, TOKEN_COMMA)) break;
                parser_advance(parser);
            }

            parser_eat(parser, TOKEN_PAREN_RIGHT);
            return (ASTNode*)mr;
        }
    }

    AST_Return* ast_return = calloc(1, sizeof(AST_Return));
    ast_return->base.value = ret_tok;
    if (parser_current_is_type(parser, TOKEN_SEMICOLON)) {
        ast_return->value = NULL;
    } else {
        ast_return->value = parser_Binary(parser);
    }
    ast_return->base.next = NULL;
    ast_return->base.type = AST_RETURN;
    return (ASTNode*)ast_return;
}

void function_parameter_resize(AST_Function* ast_function)
{
    ast_function->max_parameters *= 2;
    DataType* parameterType = realloc(ast_function->parameterType, ast_function->max_parameters * sizeof(DataType));
    if (parameterType == NULL)
    {
        OUT_OF_MEMORY();
    }
    ast_function->parameterType = parameterType;
}

void function_add_parameter(AST_Function* ast_function, ASTNode* ast_parameter, DataType data_type)
{
    if (ast_function->Parameter == NULL)
    {
        ast_function->Parameter = ast_parameter;
        ast_function->parameterType[0] = data_type;
        ast_function->parameterCount++;
        if (ast_function->parameterCount == ast_function->max_parameters)
        {
            function_parameter_resize(ast_function);
        }
    } else
    {
        ASTNode* temp = ast_function->Parameter;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = ast_parameter;
        ast_function->parameterType[ast_function->parameterCount++] = data_type;
        if (ast_function->parameterCount == ast_function->max_parameters)
        {
            function_parameter_resize(ast_function);
        }
    }
}


static void set_return_type_recursive(ASTNode* node, DataType return_type)
{
    if (node == NULL) return;
    switch (node->type) {
        case AST_RETURN: {
            AST_Return* ret = (AST_Return*)node;
            ret->returnType = return_type;
            break;
        }
        case AST_IF: {
            AST_If* if_node = (AST_If*)node;
            set_return_type_recursive(if_node->Condition, return_type);
            set_return_type_recursive(if_node->code, return_type);
            for (int i = 0; i < if_node->elif_Count; i++) {
                set_return_type_recursive(if_node->elif_condition[i], return_type);
                set_return_type_recursive(if_node->Elif[i], return_type);
            }
            set_return_type_recursive(if_node->Else, return_type);
            break;
        }
        case AST_WHILE: {
            AST_While* while_node = (AST_While*)node;
            set_return_type_recursive(while_node->Condition, return_type);
            set_return_type_recursive(while_node->code, return_type);
            break;
        }
        case AST_FOR: {
            AST_For* f = (AST_For*)node;
            set_return_type_recursive(f->init, return_type);
            set_return_type_recursive(f->condition, return_type);
            set_return_type_recursive(f->increment, return_type);
            set_return_type_recursive(f->code, return_type);
            break;
        }
        case AST_FOREACH: {
            AST_ForEach* fe = (AST_ForEach*)node;
            set_return_type_recursive(fe->array, return_type);
            set_return_type_recursive(fe->code, return_type);
            break;
        }
        case AST_FUNCTION: {
            break;
        }
        case AST_PRINT: {
            AST_Print* print_node = (AST_Print*)node;
            set_return_type_recursive(print_node->Parameter, return_type);
            break;
        }
        case AST_VAR:
        case AST_VAR_ASS:
        case AST_UNINIT_VAR: {
            AST_Var* var_node = (AST_Var*)node;
            set_return_type_recursive(var_node->Value, return_type);
            break;
        }
        case AST_BINARY: {
            AST_Binary* binary_node = (AST_Binary*)node;
            set_return_type_recursive(binary_node->left, return_type);
            set_return_type_recursive(binary_node->right, return_type);
            break;
        }
        case AST_UNARY: {
            AST_Unary* unary_node = (AST_Unary*)node;
            set_return_type_recursive(unary_node->right, return_type);
            break;
        }
        case AST_LITERAL: {
            AST_Literal* literal_node = (AST_Literal*)node;
            if (literal_node->is_call_function) {
                set_return_type_recursive(literal_node->call_function_Parameter, return_type);
            }
            break;
        }
        case AST_CONDITIONAL: {
            AST_Conditional* tern = (AST_Conditional*)node;
            set_return_type_recursive(tern->condition, return_type);
            set_return_type_recursive(tern->then_expr, return_type);
            set_return_type_recursive(tern->else_expr, return_type);
            break;
        }
        case AST_SWITCH: {
            AST_Switch* sw = (AST_Switch*)node;
            set_return_type_recursive(sw->condition, return_type);
            for (int i = 0; i < sw->case_count; i++) {
                if (sw->cases[i]) {
                    set_return_type_recursive(sw->cases[i]->value, return_type);
                    set_return_type_recursive(sw->cases[i]->body, return_type);
                }
            }
            if (sw->has_default && sw->default_body) {
                set_return_type_recursive(sw->default_body, return_type);
            }
            break;
        }
        case AST_CASE: {
            AST_Case* cs = (AST_Case*)node;
            if (cs->value) set_return_type_recursive(cs->value, return_type);
            if (cs->body)  set_return_type_recursive(cs->body, return_type);
            break;
        }
        case AST_MULTI_RETURN: {
            AST_MultiReturn* mr = (AST_MultiReturn*)node;
            for (int i = 0; i < mr->value_count; i++)
                set_return_type_recursive(mr->values[i], return_type);
            break;
        }
        case AST_RETURN_EXTRACT:
            set_return_type_recursive(((AST_ReturnExtract*)node)->object, return_type);
            break;
        case AST_TRY: {
            AST_Try* tn = (AST_Try*)node;
            set_return_type_recursive(tn->code, return_type);
            for (int i = 0; i < tn->catch_count; i++)
                set_return_type_recursive(tn->catches[i].body, return_type);
            set_return_type_recursive(tn->finally_code, return_type);
            break;
        }
        case AST_THROW:
            set_return_type_recursive(((AST_Throw*)node)->value, return_type);
            break;
        default:
            break;
    }
    set_return_type_recursive(node->next, return_type);
}

static FuncSignature* parse_func_ptr_type_signature(Parser* parser) {
    FuncSignature* sig = calloc(1, sizeof(FuncSignature));
    if (!sig) OUT_OF_MEMORY();
    sig->return_type = DATA_VOID;

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved = parser->index;
        int depth = 1;
        int look = saved + 1;
        bool has_comma_or_va = false;
        while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                depth--;
                if (depth == 0) break;
            }
            if (depth == 1 && (parser->tokens[look].type == TOKEN_COMMA
                               || parser->tokens[look].type == TOKEN_VARARG))
                has_comma_or_va = true;
            look++;
        }
        look++;
        bool followed_by_fptr = (parser->tokens[look].type == TOKEN_PAREN_LEFT
                                 && parser->tokens[look + 1].type == TOKEN_MUL);

        if (has_comma_or_va) {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
            sig->multi_return = parse_multi_return_type(parser);
            sig->return_type = DATA_VOID;
        } else {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
        }
    }

    if (!sig->multi_return && sig->return_type == DATA_VOID) {
        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)
            && parser->tokens[parser->index + 1].type != TOKEN_MUL) {
            sig->multi_return = parse_multi_return_type(parser);
            sig->return_type = DATA_VOID;
        } else if (is_var_type(parser->current.type)
                   || parser->current.type == TOKEN_IDENTIFIER) {
            sig->return_type = Get_Datatype(parser->current.type);
            sig->return_type_param_index =
                (sig->return_type == DATA_IDENTIFIER)
                ? resolve_type_param_index(parser->current.value)
                : -1;
            parser_advance(parser);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        parser_eat(parser, TOKEN_PAREN_LEFT);
        while (parser_current_is_type(parser, TOKEN_MUL))
            parser_advance(parser);
        if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
            parser_advance(parser);
        parser_eat(parser, TOKEN_PAREN_RIGHT);
    } else {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected '(' before '*' in function pointer type");
        free_func_sig_recursive(sig);
        return NULL;
    }

    parser_eat(parser, TOKEN_PAREN_LEFT);
    int param_cap = 4, param_cnt = 0;
    DataType* param_types = malloc(sizeof(DataType) * param_cap);
    int* param_type_param_indices = malloc(sizeof(int) * param_cap);
    bool* param_pointee_const = malloc(sizeof(bool) * param_cap);
    FuncSignature** param_func_sigs = calloc((size_t)param_cap, sizeof(FuncSignature*));
    bool has_varargs = false;
    int vararg_cap = 4, vararg_cnt = 0;
    int* vararg_indices = malloc(sizeof(int) * vararg_cap);

    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF)) {

        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            if (param_cnt > 0) {
                has_varargs = true;
                if (vararg_cnt >= vararg_cap) {
                    vararg_cap *= 2;
                    vararg_indices = realloc(vararg_indices, sizeof(int) * vararg_cap);
                }
                vararg_indices[vararg_cnt++] = param_cnt;
            }
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }

        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            FuncSignature* inner_sig = parse_func_ptr_type_signature(parser);
            if (inner_sig) {
               if (param_cnt >= param_cap) {
                   int old_param_cap = param_cap;
                   param_cap *= 2;
                   param_types = realloc(param_types, sizeof(DataType) * param_cap);
                   param_type_param_indices = realloc(param_type_param_indices, sizeof(int) * param_cap);
                   param_pointee_const = realloc(param_pointee_const, sizeof(bool) * param_cap);
                   param_func_sigs = realloc(param_func_sigs, sizeof(FuncSignature*) * param_cap);
                   memset(param_func_sigs + old_param_cap, 0,
                          sizeof(FuncSignature*) * (size_t)(param_cap - old_param_cap));
               }
               param_types[param_cnt] = DATA_INT;
                param_type_param_indices[param_cnt] = -1;
                param_pointee_const[param_cnt] = false;
                param_func_sigs[param_cnt] = inner_sig;
                param_cnt++;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
                continue;
            }
        }

        {
            DataType nested_base = DATA_ERROR;
            FuncSignature* nested_sig = NULL;
            if (parser_try_nested_func_ptr_param(parser, &nested_base, &nested_sig)) {
               if (param_cnt >= param_cap) {
                   int old_param_cap = param_cap;
                   param_cap *= 2;
                   param_types = realloc(param_types, sizeof(DataType) * param_cap);
                   param_type_param_indices = realloc(param_type_param_indices, sizeof(int) * param_cap);
                   param_pointee_const = realloc(param_pointee_const, sizeof(bool) * param_cap);
                   param_func_sigs = realloc(param_func_sigs, sizeof(FuncSignature*) * param_cap);
                   memset(param_func_sigs + old_param_cap, 0,
                          sizeof(FuncSignature*) * (size_t)(param_cap - old_param_cap));
               }
               param_types[param_cnt] = nested_base;
                param_type_param_indices[param_cnt] = -1;
                param_pointee_const[param_cnt] = false;
                param_func_sigs[param_cnt] = nested_sig;
                param_cnt++;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
                continue;
            }
        }

        bool param_is_pointee_const = false;
        if (parser_current_is_type(parser, TOKEN_CONST)) {
            param_is_pointee_const = true;
            parser_advance(parser);
        }

        if (!is_var_type(parser->current.type)
            && parser->current.type != TOKEN_IDENTIFIER
            && parser->current.type != TOKEN_VOID) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected parameter type, got '%s'",
                  type_to_string(parser->current.type));
            break;
        }

        DataType pt = Get_Datatype(parser->current.type);
        int pt_idx = -1;
        if (pt == DATA_IDENTIFIER) {
            pt_idx = resolve_type_param_index(parser->current.value);
            parser_advance(parser);
            if (pt_idx < 0) {
                while (parser_current_is_type(parser, TOKEN_DOT)
                       && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                    parser_advance(parser);
                    parser_advance(parser);
                }
            }
        } else {
            parser_advance(parser);
        }

        int star_count = 0;
        while (parser_current_is_type(parser, TOKEN_MUL)) {
            star_count++;
            parser_advance(parser);
        }
        if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
            parser_advance(parser);

        if (star_count == 0) param_is_pointee_const = false;

        bool is_vararg = false;
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            is_vararg = true;
            parser_advance(parser);
        }

        if (param_cnt >= param_cap) {
            param_cap *= 2;
            param_types = realloc(param_types, sizeof(DataType) * param_cap);
            param_type_param_indices = realloc(param_type_param_indices, sizeof(int) * param_cap);
            param_pointee_const = realloc(param_pointee_const, sizeof(bool) * param_cap);
        }
        param_types[param_cnt] = pt;
        param_type_param_indices[param_cnt] = pt_idx;
        param_pointee_const[param_cnt] = param_is_pointee_const;
        param_cnt++;

        if (is_vararg) {
            has_varargs = true;
            if (vararg_cnt >= vararg_cap) {
                vararg_cap *= 2;
                vararg_indices = realloc(vararg_indices, sizeof(int) * vararg_cap);
            }
            vararg_indices[vararg_cnt++] = param_cnt;
        }

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                has_varargs = true;
                if (vararg_cnt >= vararg_cap) {
                    vararg_cap *= 2;
                    vararg_indices = realloc(vararg_indices, sizeof(int) * vararg_cap);
                }
                vararg_indices[vararg_cnt++] = param_cnt;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    sig->param_count = param_cnt;
    sig->param_types = param_types;
    sig->param_type_param_indices = param_type_param_indices;
    sig->param_pointee_const = param_pointee_const;
    sig->param_func_sigs = param_func_sigs;
    sig->has_varargs = has_varargs;
    sig->vararg_param_count = vararg_cnt;
    sig->vararg_param_indices = (vararg_cnt > 0) ? vararg_indices : NULL;
    if (vararg_cnt == 0) free(vararg_indices);

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved2 = parser->index;
        parser_eat(parser, TOKEN_PAREN_LEFT);
        if (parser_current_is_type(parser, TOKEN_MUL)) {
            while (parser_current_is_type(parser, TOKEN_MUL))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);

            FuncSignature* outer = parse_func_ptr_type_signature(parser);
            if (outer) {
                FuncSignature** tail = &outer->ret_func_sig;
                while (*tail) tail = &(*tail)->ret_func_sig;
                *tail = sig;
                return outer;
            }
        } else {
            parser->index = saved2;
            parser->current = parser->tokens[parser->index];
        }
    }

    return sig;
}

static MultiReturnType* parse_multi_return_type(Parser* parser) {
    MultiReturnType* mrt = calloc(1, sizeof(MultiReturnType));
    if (!mrt) OUT_OF_MEMORY();

    int cap = 4;
    mrt->types = malloc(sizeof(DataType) * cap);
    mrt->type_param_indices = malloc(sizeof(int) * cap);
    mrt->is_variadic = malloc(sizeof(bool) * cap);
    mrt->func_sigs = calloc(cap, sizeof(FuncSignature*));
    if (!mrt->types || !mrt->type_param_indices || !mrt->is_variadic || !mrt->func_sigs) OUT_OF_MEMORY();

    parser_eat(parser, TOKEN_PAREN_LEFT);

    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF)) {

        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            int saved = parser->index;

            int look = saved + 1;
            int depth = 1;
            bool has_comma = false;
            bool has_vararg = false;
            while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                    depth--;
                    if (depth == 0) break;
                }
                if (depth == 1 && parser->tokens[look].type == TOKEN_COMMA) {
                    has_comma = true;
                }
                if (depth == 1 && parser->tokens[look].type == TOKEN_VARARG) {
                    has_vararg = true;
                }
                look++;
            }
            look++;

            bool is_func_ptr = false;
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT
                && parser->tokens[look + 1].type == TOKEN_MUL) {
                is_func_ptr = true;
            }

            if (is_func_ptr) {
                parser->index = saved;
                parser->current = parser->tokens[parser->index];

                FuncSignature* fsig = parse_func_ptr_type_signature(parser);
                if (!fsig) {
                    while (!parser_current_is_type(parser, TOKEN_COMMA)
                           && !parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                           && !parser_current_is_type(parser, TOKEN_EOF))
                        parser_advance(parser);
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }

                bool variadic = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    variadic = true;
                    parser_advance(parser);
                }

                if (mrt->count >= cap) {
                    int old_cap = cap;
                    cap *= 2;
                    mrt->types = realloc(mrt->types, sizeof(DataType) * cap);
                    mrt->type_param_indices = realloc(mrt->type_param_indices, sizeof(int) * cap);
                    mrt->is_variadic = realloc(mrt->is_variadic, sizeof(bool) * cap);
                    mrt->func_sigs = realloc(mrt->func_sigs, sizeof(FuncSignature*) * cap);
                    if (!mrt->types || !mrt->is_variadic || !mrt->func_sigs) OUT_OF_MEMORY();
                    memset(&mrt->is_variadic[old_cap], 0, sizeof(bool) * (cap - old_cap));
                    memset(&mrt->func_sigs[old_cap], 0, sizeof(FuncSignature*) * (cap - old_cap));
                }

                mrt->types[mrt->count] = DATA_INT;
                mrt->is_variadic[mrt->count] = variadic;
                mrt->func_sigs[mrt->count] = fsig;
                mrt->count++;

                if (!parser_current_is_type(parser, TOKEN_COMMA)) break;
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    if (mrt->count > 0)
                        mrt->is_variadic[mrt->count - 1] = true;
                    break;
                }
                continue;
            }

            parser->index = saved;
            parser->current = parser->tokens[parser->index];
        }

        if (is_var_type(parser->current.type)
            || parser->current.type == TOKEN_IDENTIFIER) {
            DataType dt = Get_Datatype(parser->current.type);
            int tp_idx = -1;
            if (dt == DATA_IDENTIFIER) {
                int saved = parser->index - 1;
                if (saved >= 0 && parser->tokens[saved].type == TOKEN_IDENTIFIER) {
                    tp_idx = resolve_type_param_index(parser->tokens[saved].value);
                }
            }
            parser_advance(parser);
            if (dt == DATA_IDENTIFIER && tp_idx < 0) {
                while (parser_current_is_type(parser, TOKEN_DOT)
                       && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                    parser_advance(parser);
                    parser_advance(parser);
                }
            }
            if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)
                && parser->tokens[parser->index + 1].type == TOKEN_MUL) {
                parser_eat(parser, TOKEN_PAREN_LEFT);
                while (parser_current_is_type(parser, TOKEN_MUL))
                    parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                    parser_advance(parser);
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                parser_eat(parser, TOKEN_PAREN_LEFT);
                int fpc = 0, fpcap = 4;
                DataType* fpts = malloc(sizeof(DataType) * fpcap);
                while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                       && !parser_current_is_type(parser, TOKEN_EOF)) {
                    if (!is_var_type(parser->current.type)
                        && parser->current.type != TOKEN_IDENTIFIER) break;
                    DataType fpt = Get_Datatype(parser->current.type);
                    parser_advance(parser);
                    if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                        parser_advance(parser);
                    if (fpc >= fpcap) {
                        fpcap *= 2;
                        fpts = realloc(fpts, sizeof(DataType) * fpcap);
                    }
                    fpts[fpc++] = fpt;
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    else break;
                }
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                FuncSignature* fsig = calloc(1, sizeof(FuncSignature));
                fsig->return_type = dt;
                fsig->return_type_param_index = tp_idx;
                fsig->param_count = fpc;
                fsig->param_types = fpts;
                if (fpc > 0) {
                    fsig->param_type_param_indices = malloc(sizeof(int) * fpc);
                    for (int pi = 0; pi < fpc; pi++) {
                        fsig->param_type_param_indices[pi] =
                            (fpts[pi] == DATA_IDENTIFIER) ? tp_idx : -1;
                    }
                }
                bool variadic = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    variadic = true;
                    parser_advance(parser);
                }
                if (mrt->count >= cap) {
                    int old_cap = cap;
                    cap *= 2;
                    mrt->types = realloc(mrt->types, sizeof(DataType) * cap);
                    mrt->type_param_indices = realloc(mrt->type_param_indices, sizeof(int) * cap);
                    mrt->is_variadic = realloc(mrt->is_variadic, sizeof(bool) * cap);
                    mrt->func_sigs = realloc(mrt->func_sigs, sizeof(FuncSignature*) * cap);
                    if (!mrt->types || !mrt->is_variadic || !mrt->func_sigs) OUT_OF_MEMORY();
                    memset(&mrt->is_variadic[old_cap], 0, sizeof(bool) * (cap - old_cap));
                    memset(&mrt->func_sigs[old_cap], 0, sizeof(FuncSignature*) * (cap - old_cap));
                }
                mrt->types[mrt->count] = DATA_INT;
                mrt->is_variadic[mrt->count] = variadic;
                mrt->func_sigs[mrt->count] = fsig;
                mrt->count++;
            } else {
                bool variadic = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    variadic = true;
                    parser_advance(parser);
                }
                if (mrt->count >= cap) {
                    int old_cap = cap;
                    cap *= 2;
                    mrt->types = realloc(mrt->types, sizeof(DataType) * cap);
                    mrt->type_param_indices = realloc(mrt->type_param_indices, sizeof(int) * cap);
                    mrt->is_variadic = realloc(mrt->is_variadic, sizeof(bool) * cap);
                    mrt->func_sigs = realloc(mrt->func_sigs, sizeof(FuncSignature*) * cap);
                    if (!mrt->types || !mrt->is_variadic || !mrt->func_sigs) OUT_OF_MEMORY();
                    memset(&mrt->is_variadic[old_cap], 0, sizeof(bool) * (cap - old_cap));
                    memset(&mrt->func_sigs[old_cap], 0, sizeof(FuncSignature*) * (cap - old_cap));
                }
                mrt->types[mrt->count] = dt;
                mrt->type_param_indices[mrt->count] = tp_idx;
                mrt->is_variadic[mrt->count] = variadic;
                mrt->func_sigs[mrt->count] = NULL;
                mrt->count++;
            }
        } else {
            ERROR(ERR_PARSE_INVALID_TYPE,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected type in multi-return declaration, got '%s'",
                  type_to_string(parser->current.type));
            while (!parser_current_is_type(parser, TOKEN_COMMA)
                   && !parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
        }


        if (!parser_current_is_type(parser, TOKEN_COMMA)) break;
        parser_advance(parser);
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            if (mrt->count > 0)
                mrt->is_variadic[mrt->count - 1] = true;
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    return mrt;
}

ASTNode* parser_constructor_def(Parser* parser, int modifier)
{
    AST_Function* ast_function = calloc(1, sizeof(AST_Function));
    if (!ast_function) {
        OUT_OF_MEMORY();
    }
    ast_function->max_parameters = 4;
    ast_function->parameterType = malloc(sizeof(DataType) * ast_function->max_parameters);
    ast_function->return_Count = 0;
    ast_function->parameterCount = 0;
    ast_function->Parameter = NULL;
    ast_function->Return = NULL;
    ast_function->Modifier = modifier;
    ast_function->internal_name = NULL;
    DataType return_type = DATA_VOID;
    ast_function->return_type = DATA_VOID;
    ast_function->base.value = parser->current;
    ast_function->name = parser->current;
    parser_advance(parser);
    parser_eat(parser, TOKEN_PAREN_LEFT);
    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF))
    {
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            ast_function->has_varargs = true;

            int idx = ast_function->vararg_param_count++;
            ast_function->vararg_param_indices = realloc(
                ast_function->vararg_param_indices,
                sizeof(int) * ast_function->vararg_param_count);
            ast_function->vararg_internal_names = realloc(
                ast_function->vararg_internal_names,
                sizeof(char*) * ast_function->vararg_param_count);

            if (ast_function->parameterCount > 0) {
                ast_function->vararg_param_indices[idx] = ast_function->parameterCount - 1;
            } else {
                ERROR(ERR_PARSE_INVALID_TYPE,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "'...' must follow a parameter name");
            }

            ASTNode* last = ast_function->Parameter;
            if (last) {
                while (last->next) last = last->next;
                if (last->type == AST_VAR || last->type == AST_UNINIT_VAR)
                    ast_function->vararg_internal_names[idx] = strdup(((AST_Var*)last)->name.value);
                else if (last->type == AST_ARRAY_DECL)
                    ast_function->vararg_internal_names[idx] = strdup(((AST_ArrayDecl*)last)->name);
            }
            if (ast_function->vararg_param_count == 1) {
                ast_function->vararg_param_index = ast_function->vararg_param_indices[0];
                ast_function->vararg_internal_name = ast_function->vararg_internal_names[0];
            }
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        ASTNode* param = parser_var(parser,false);
        if (param == NULL) {
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_COMMA)
                   && !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        DataType param_dtype;
        if (param->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)param;
            param_dtype = arr->elementType;
        } else if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)param;
            if (var->func_ptr_sig || var->ptr_array_info) {
                var->dataType = DATA_INT;
            }
            param_dtype = var->dataType;
        } else {
            param_dtype = DATA_ERROR;
        }
        function_add_parameter(ast_function, param, param_dtype);
        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_eat(parser, TOKEN_COMMA);
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    if (g_native_requested) {
        g_native_requested = false;
        parser_eat(parser, TOKEN_SEMICOLON);
        ast_function->is_native = true;
        ast_function->return_Count = 1;
        ast_function->Return = calloc(1, sizeof(AST_Return));
        if (!ast_function->Return) OUT_OF_MEMORY();
        ast_function->Return->base.type = AST_RETURN;
        ast_function->Return->returnType = DATA_VOID;
        ast_function->base.type = AST_FUNCTION;
        ast_function->base.next = NULL;
        return (ASTNode*)ast_function;
    }

    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ast_function->Code = parser_parse_code_block(parser);
    set_return_type_recursive(ast_function->Code, return_type);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);
    ASTNode* scan = ast_function->Code;
    while (scan != NULL) {
        if (scan->type == AST_RETURN) {
            AST_Return* ret = (AST_Return*)scan;
            ast_function->Return = ret;
            ast_function->return_Count = 1;
            ret->returnType = return_type;
        }
        scan = scan->next;
    }
    ast_function->base.type = AST_FUNCTION;
    ast_function->base.next = NULL;
    return (ASTNode*)ast_function;
}

static char* parser_make_instance_name(const char* base, DataType* args,
                                       char** names, int argc);
static int parser_parse_generic_arg_list(Parser* parser, DataType** out_types,
                                         char*** out_names);

ASTNode* parser_class_def(Parser* parser, int modifier)
{
    Token class_token = parser->current;
    parser_eat(parser, TOKEN_CLASS);
    TypeParam* type_params = NULL;
    int type_param_count = 0;
    if (parser_current_is_type(parser, TOKEN_LT)) {
        type_param_count = parser_parse_type_params(parser, &type_params);
        if (type_param_count < 0) {
            while (!parser_current_is_type(parser, TOKEN_BRACKET_LEFT) &&
                   !parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            return NULL;
        }
    }
    char* name = strdup(parser->current.value);
    parser_eat(parser, TOKEN_IDENTIFIER);
    if (parser_current_is_type(parser, TOKEN_LT)) {
        type_param_count = parser_parse_type_params(parser, &type_params);
        if (type_param_count < 0) {
            free(name);
            while (!parser_current_is_type(parser, TOKEN_BRACKET_LEFT) &&
                   !parser_current_is_type(parser, TOKEN_SEMICOLON) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            return NULL;
        }
    }
    char** parents = NULL;
    int parent_count = 0;
    if (parser_current_is_type(parser, TOKEN_COLON)) {
        parser_advance(parser);
        int cap = 2;
        parents = malloc(sizeof(char*) * cap);
        do {
            if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "expected parent class name after ':'");
                break;
            }
            if (parent_count >= cap) {
                cap *= 2;
                parents = realloc(parents, sizeof(char*) * cap);
            }
            char* parent_name = strdup(parser->current.value);
            parser_eat(parser, TOKEN_IDENTIFIER);
            if (parser_current_is_type(parser, TOKEN_LT)) {
                parser_advance(parser);
                DataType* ptypes = NULL;
                char** pnames = NULL;
                int pcount = parser_parse_generic_arg_list(parser, &ptypes,
                                                           &pnames);
                char* p_mangled = parser_make_instance_name(parent_name, ptypes,
                                                            pnames, pcount);
                bool p_has_type_param = false;
                for (int pk = 0; pk < pcount; pk++) {
                    if (!pnames[pk]) continue;
                    for (int ti = 0; ti < type_param_count; ti++) {
                        if (type_params[ti].name
                            && strcmp(pnames[pk], type_params[ti].name) == 0) {
                            p_has_type_param = true;
                            break;
                        }
                    }
                    if (p_has_type_param) break;
                }
                if (!p_has_type_param) {
                    parser_register_nested_generic(p_mangled, parent_name,
                                                   ptypes, pnames, pcount);
                }
                free(ptypes);
                if (pnames) {
                    for (int pk = 0; pk < pcount; pk++) free(pnames[pk]);
                    free(pnames);
                }
                free(parent_name);
                parent_name = p_mangled;
            }
            parents[parent_count] = parent_name;
            parent_count++;
        } while (parser_current_is_type(parser, TOKEN_COMMA)
                 && (parser_advance(parser), 1));
    }
    TypeParam* saved_params = g_parse_type_params;
    int saved_count = g_parse_type_param_count;
    if (type_param_count > 0 && type_params) {
        g_parse_type_params = type_params;
        g_parse_type_param_count = type_param_count;
    }
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ASTNode* members_head = NULL;
    ASTNode* members_tail = NULL;
    while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
           !parser_current_is_type(parser, TOKEN_EOF)) {
        ASTNode* stmt = parser_Sentence(parser, true);
        if (stmt) {
            ASTNode* stmt_tail = stmt;
            while (stmt_tail->next != NULL)
                stmt_tail = stmt_tail->next;
            if (!members_head) {
                members_head = stmt;
                members_tail = stmt_tail;
            } else {
                members_tail->next = stmt;
                members_tail = stmt_tail;
            }
        }
    }
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    g_parse_type_params = saved_params;
    g_parse_type_param_count = saved_count;

    AST_ClassDef* class_def = calloc(1, sizeof(AST_ClassDef));
    class_def->base.type = AST_CLASS_DEF;
    class_def->base.value = class_token;
    class_def->name = name;
    class_def->parent_names = parents;
    class_def->parent_count = parent_count;
    class_def->modifier = modifier;
    class_def->members = members_head;
    class_def->type_params = type_params;
    class_def->type_param_count = type_param_count;
    class_def->is_generic = (type_param_count > 0);
    class_def->is_function_class = (name != NULL && strcmp(name, "Function") == 0);
    return (ASTNode*)class_def;
}

ASTNode* parser_enum_def(Parser* parser)
{
    parser_eat(parser, TOKEN_ENUM);

    char* name = NULL;
    if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
        name = strdup(parser->current.value);
        parser_advance(parser);
    }

    parser_eat(parser, TOKEN_BRACKET_LEFT);

    AST_EnumDef* enum_def = calloc(1, sizeof(AST_EnumDef));
    enum_def->base.type = AST_ENUM_DEF;
    enum_def->base.next = NULL;
    enum_def->name = name;
    enum_def->member_count = 0;
    enum_def->member_capacity = 4;
    enum_def->members = malloc(sizeof(AST_EnumMember*) * enum_def->member_capacity);
    enum_def->modifier = 0;

    while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
           !parser_current_is_type(parser, TOKEN_EOF))
    {
        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected enum member name");
            while (!parser_current_is_type(parser, TOKEN_COMMA) &&
                   !parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }

        AST_EnumMember* member = calloc(1, sizeof(AST_EnumMember));
        member->base.type = AST_ENUM_MEMBER;
        member->base.next = NULL;
        member->name = strdup(parser->current.value);
        member->value = -1;
        member->has_explicit_value = false;
        member->explicit_token = NULL;

        parser_advance(parser);

        if (parser_current_is_type(parser, TOKEN_ASSIGN)) {
            parser_advance(parser);

            if (parser_current_is_type(parser, TOKEN_NUMBER)) {
                member->has_explicit_value = true;
                member->explicit_token = malloc(sizeof(Token));
                *member->explicit_token = parser->current;
                member->explicit_token->value = strdup(parser->current.value);
                member->value = parse_integer(parser->current.value);
                parser_advance(parser);
            } else if (parser_current_is_type(parser, TOKEN_SUB) &&
                       parser->tokens[parser->index + 1].type == TOKEN_NUMBER) {
                member->has_explicit_value = true;
                member->explicit_token = malloc(sizeof(Token));
                member->explicit_token->type = TOKEN_NUMBER;
                member->explicit_token->line = parser->current.line;
                member->explicit_token->column = parser->current.column;
                int len = strlen(parser->current.value) +
                          strlen(parser->tokens[parser->index + 1].value) + 1;
                member->explicit_token->value = malloc(len);
                snprintf(member->explicit_token->value, len, "-%s",
                         parser->tokens[parser->index + 1].value);
                member->value = -parse_integer(parser->tokens[parser->index + 1].value);
                parser_advance(parser);
                parser_advance(parser);
            } else {
                ERROR(ERR_PARSE_ILLEGAL_OPERAND,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "enum member '%s' requires an integer constant",
                      member->name);
                while (!parser_current_is_type(parser, TOKEN_COMMA) &&
                       !parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
                       !parser_current_is_type(parser, TOKEN_EOF))
                    parser_advance(parser);
            }
        }

        if (enum_def->member_count >= enum_def->member_capacity) {
            enum_def->member_capacity *= 2;
            enum_def->members = realloc(enum_def->members,
                                        sizeof(AST_EnumMember*) * enum_def->member_capacity);
        }
        enum_def->members[enum_def->member_count++] = member;

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_BRACKET_RIGHT))
                break;
        } else {
            break;
        }
    }

    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    return (ASTNode*)enum_def;
}

ASTNode* parser_class_def_with_modifier(Parser* parser, int modifier) {
    parser_eat(parser, TOKEN_CLASS);
    char* name = strdup(parser->current.value);
    parser_eat(parser, TOKEN_IDENTIFIER);
    char** parents = NULL;
    int parent_count = 0;
    if (parser_current_is_type(parser, TOKEN_COLON)) {
        parser_advance(parser);
        int cap = 2;
        parents = malloc(sizeof(char*) * cap);
        do {
            if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "expected parent class name after ':'");
                break;
            }
            if (parent_count >= cap) {
                cap *= 2;
                parents = realloc(parents, sizeof(char*) * cap);
            }
            char* parent_name = strdup(parser->current.value);
            parser_eat(parser, TOKEN_IDENTIFIER);
            if (parser_current_is_type(parser, TOKEN_LT)) {
                parser_advance(parser);
                DataType* ptypes = NULL;
                char** pnames = NULL;
                int pcount = parser_parse_generic_arg_list(parser, &ptypes,
                                                           &pnames);
                char* p_mangled = parser_make_instance_name(parent_name, ptypes,
                                                            pnames, pcount);
                bool p_has_type_param = false;
                (void)p_has_type_param;
                if (!p_has_type_param) {
                    parser_register_nested_generic(p_mangled, parent_name,
                                                   ptypes, pnames, pcount);
                }
                free(ptypes);
                if (pnames) {
                    for (int pk = 0; pk < pcount; pk++) free(pnames[pk]);
                    free(pnames);
                }
                free(parent_name);
                parent_name = p_mangled;
            }
            parents[parent_count] = parent_name;
            parent_count++;
        } while (parser_current_is_type(parser, TOKEN_COMMA)
                 && (parser_advance(parser), 1));
    }
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ASTNode* members_head = NULL;
    ASTNode* members_tail = NULL;
    while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
           !parser_current_is_type(parser, TOKEN_EOF))
    {
        ASTNode* stmt = parser_Sentence(parser, true);
        if (stmt) {
            if (stmt->type == AST_FUNCTION) {
                AST_Function* func = (AST_Function*)stmt;
                int len = strlen(name) + 1 + strlen(func->name.value) + 1;
                char* mangled = malloc(len);
                if (mangled) {
                    snprintf(mangled, len, "%s_%s", name, func->name.value);
                }
                if (func->internal_name) free(func->internal_name);
                func->internal_name = mangled;
            }
            if (!members_head) {
                members_head = stmt;
                members_tail = stmt;
            } else {
                members_tail->next = stmt;
                while (members_tail->next != NULL) {
                    members_tail = members_tail->next;
                }
            }
        }
    }
    parser_eat(parser, TOKEN_BRACKET_RIGHT);
    AST_ClassDef* class_def = calloc(1, sizeof(AST_ClassDef));
    class_def->base.type = AST_CLASS_DEF;
    class_def->name = name;
    class_def->parent_names = parents;
    class_def->parent_count = parent_count;
    class_def->modifier = modifier;
    class_def->members = members_head;
    return (ASTNode*)class_def;
}

ASTNode* parser_def_function_with_modifier(Parser* parser, int modifier) {
    AST_Function* ast_function = calloc(1,sizeof(AST_Function));
    ast_function->max_parameters = 4;
    ast_function->parameterType = malloc(sizeof(DataType) * ast_function->max_parameters);
    ast_function->return_Count = 0;
    ast_function->parameterCount = 0;
    ast_function->Parameter = NULL;
    ast_function->Return = NULL;
    ast_function->Modifier = modifier;
    ast_function->internal_name = NULL;
    ast_function->returns_array = false;
    ast_function->return_element_type = DATA_VOID;
    ast_function->return_dim_count = 0;
    ast_function->return_dim_sizes = NULL;
    ast_function->ret_func_sig = NULL;
    if (parser->current.type == TOKEN_IDENTIFIER) {
        ast_function->return_class_name = strdup(parser->current.value);
    }

    DataType return_type = DATA_VOID;
    MultiReturnType* multi_return = NULL;
    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)
        && parser->tokens[parser->index + 1].type != TOKEN_MUL) {
        int saved = parser->index;
        int look = saved + 1;
        int depth = 1;
        while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) { depth--; if (depth == 0) break; }
            look++;
        }
        bool is_func_ptr = false;
        if (depth == 0) {
            int after = look + 1;
            if (parser->tokens[after].type == TOKEN_PAREN_LEFT
                && parser->tokens[after + 1].type == TOKEN_MUL
                && parser->tokens[after + 2].type == TOKEN_PAREN_RIGHT) {
                is_func_ptr = true;
            }
        }
        if (is_func_ptr) {
            FuncSignature* fpsig = parse_func_ptr_type_signature(parser);
            if (fpsig) {
                if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
                ast_function->ret_func_sig = fpsig;
                return_type = DATA_INT;
            }
        } else {
            multi_return = parse_multi_return_type(parser);
            return_type = DATA_VOID;
        }
    } else if (is_var_type(parser->current.type) || parser->current.type == TOKEN_IDENTIFIER) {
        return_type = Get_Datatype(parser->current.type);
        if (return_type == DATA_IDENTIFIER) {
            parser_advance(parser);
            char* tn = ast_function->return_class_name;
            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int nl = (int)strlen(tn) + 1 + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)nl);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)nl, "%s_%s", tn, parser->current.value);
                free(tn);
                tn = merged;
                parser_advance(parser);
                   }
            ast_function->return_class_name = tn;
        } else {
            parser_advance(parser);
        }
    } else {
        ERROR(ERR_PARSE_INVALID_TYPE,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected return type, got '%s'",
              type_to_string(parser->current.type));
    }
    int return_ptr_depth = 0;
    while (parser_current_is_type(parser, TOKEN_MUL)) {
        return_ptr_depth++;
        parser_advance(parser);
    }

    ast_function->return_ptr_depth = return_ptr_depth;

    if (parser_current_is_type(parser, TOKEN_LT)) {
        parser_advance(parser);
        int depth = 1;
        while (depth > 0 && !parser_current_is_type(parser, TOKEN_EOF)) {
            if (parser_current_is_type(parser, TOKEN_LT)) depth++;
            else if (parser_current_is_type(parser, TOKEN_GT)) depth--;
            else if (parser_current_is_type(parser, TOKEN_SHIFT_RIGHT)) {
                depth -= 2;
                if (depth < 0) depth = 0;
            }
            parser_advance(parser);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_idx = parser->index;
        int look = saved_idx + 1;
        int star_count = 0;
        while (parser->tokens[look].type == TOKEN_MUL && look < saved_idx + 10) {
            star_count++;
            look++;
        }

        if (star_count > 0 && parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            while (parser_current_is_type(parser, TOKEN_MUL)) {
                return_ptr_depth++;
                parser_advance(parser);
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            parser_eat(parser, TOKEN_PAREN_LEFT);
            int param_cap = 4;
            int param_count = 0;
            DataType* param_types = malloc(sizeof(DataType) * param_cap);
            bool func_ptr_has_varargs = false;
            int* func_ptr_vararg_indices = NULL;
            int func_ptr_vararg_count = 0;
            int func_ptr_vararg_cap = 4;
            func_ptr_vararg_indices = malloc(sizeof(int) * func_ptr_vararg_cap);
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_EOF)) {
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    if (param_count > 0) {
                        func_ptr_has_varargs = true;
                        if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                            func_ptr_vararg_cap *= 2;
                            func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                                sizeof(int) * func_ptr_vararg_cap);
                        }
                        func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count - 1;
                    }
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
                {
                    DataType nested_base = DATA_ERROR;
                    FuncSignature* nested_sig = NULL;
                    if (parser_try_nested_func_ptr_param(parser, &nested_base, &nested_sig)) {
                        if (param_count >= param_cap) {
                            param_cap *= 2;
                            param_types = realloc(param_types, sizeof(DataType) * param_cap);
                        }
                        param_types[param_count] = nested_base;
                        param_count++;
                        if (parser_current_is_type(parser, TOKEN_COMMA)) {
                            parser_eat(parser, TOKEN_COMMA);
                        } else {
                            break;
                        }
                        continue;
                    }
                }
                if (!is_var_type(parser->current.type) && parser->current.type != TOKEN_IDENTIFIER) {
                    ERROR(ERR_PARSE_EXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "expected type in function pointer return type");
                    break;
                }
                DataType pt = Get_Datatype(parser->current.type);
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                    parser_advance(parser);
                bool is_vararg = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    is_vararg = true;
                    parser_advance(parser);
                }
                if (param_count >= param_cap) {
                    param_cap *= 2;
                    param_types = realloc(param_types, sizeof(DataType) * param_cap);
                }
                param_types[param_count] = pt;
                if (is_vararg) {
                    func_ptr_has_varargs = true;
                    if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                        func_ptr_vararg_cap *= 2;
                        func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                            sizeof(int) * func_ptr_vararg_cap);
                    }
                    func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
                }
                param_count++;
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            FuncSignature* sig = calloc(1, sizeof(FuncSignature));
            sig->return_type = return_type;
            sig->param_count = param_count;
            sig->param_types = param_types;
            sig->has_varargs = func_ptr_has_varargs;
            sig->vararg_param_count = func_ptr_vararg_count;
            sig->vararg_param_indices = (func_ptr_vararg_count > 0) ? func_ptr_vararg_indices : NULL;
            sig->multi_return = multi_return;
            multi_return = NULL;
            if (func_ptr_vararg_count == 0) free(func_ptr_vararg_indices);
            if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
            ast_function->ret_func_sig = sig;
            ast_function->return_ptr_depth += star_count;
            return_type = DATA_INT;
        }
        else if (parse_return_type_func_ptr(parser, ast_function,
                                             return_type, return_ptr_depth)) {
            return_type = DATA_INT;
        }
    }

    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT))
    {
        int dim_cap = 4;
        int dim_cnt = 0;
        int* dim_sizes = malloc(sizeof(int) * dim_cap);
        ASTNode** dim_exprs = calloc(dim_cap, sizeof(ASTNode*));
        if (!dim_sizes) OUT_OF_MEMORY();
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT))
        {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode* expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (dim_cnt >= dim_cap)
            {
                dim_cap *= 2;
                dim_sizes = realloc(dim_sizes, sizeof(int) * (size_t)dim_cap);
                dim_exprs = realloc(dim_exprs, sizeof(ASTNode*) * dim_cap);
                if (!dim_sizes) OUT_OF_MEMORY();
            }
            dim_sizes[dim_cnt] = size;
            dim_exprs[dim_cnt++] = expr;
        }
        ast_function->return_dim_exprs = dim_exprs;
        if (dim_cnt > 0)
        {
            ast_function->returns_array = true;
            ast_function->return_element_type = return_type;
            ast_function->return_dim_count = dim_cnt;
            ast_function->return_dim_sizes = dim_sizes;
            return_type = DATA_VOID;
        }
        else
        {
            free(dim_sizes);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_name = parser->index;
        if (parser->tokens[saved_name + 1].type == TOKEN_MUL
            && parser->tokens[saved_name + 2].type == TOKEN_IDENTIFIER
            && parser->tokens[saved_name + 3].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            parser_eat(parser, TOKEN_MUL);
            ast_function->base.value = parser->current;
            ast_function->name = parser->current;
            parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            } else {
                ast_function->base.value = parser->current;
                ast_function->name = parser->current;
                parser_advance(parser);
            }
    } else {
        ast_function->base.value = parser->current;
        ast_function->name = parser->current;
        parser_advance(parser);
    }

    parser_eat(parser, TOKEN_PAREN_LEFT);
    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) && !parser_current_is_type(parser, TOKEN_EOF)) {
        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            int saved = parser->index;
            Token saved_cur = parser->current;
            ASTNode* param = parser_multi_return_func_ptr_var(parser);
            if (param != NULL) {
                DataType pdt = DATA_INT;
                function_add_parameter(ast_function, param, pdt);
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx] = ast_function->parameterCount - 1;
                    if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
                        AST_Var* var = (AST_Var*)param;
                        ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                        if (var->func_ptr_sig == NULL) {
                            if (var->total_bytes < 8) var->total_bytes = 8;
                            var->dim_count = 1;
                            if (!var->dim_sizes) {
                                var->dim_sizes = malloc(sizeof(int));
                                var->dim_sizes[0] = -1;
                            }
                        }
                    } else {
                        char placeholder[32];
                        snprintf(placeholder, sizeof(placeholder),
                                 "_va_mfp_%d", idx);
                        ast_function->vararg_internal_names[idx] = strdup(placeholder);
                    }
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name = strdup(
                            ast_function->vararg_internal_names[0]);
                    }
                }
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
                continue;
            }
            parser->index = saved;
            parser->current = saved_cur;
        }
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            ast_function->has_varargs = true;
            char placeholder_name[32];
            snprintf(placeholder_name, sizeof(placeholder_name),
                     "_va_placeholder_%d", ast_function->vararg_param_count);
            AST_Var* placeholder = calloc(1, sizeof(AST_Var));
            placeholder->base.type = AST_VARARG;
            placeholder->base.next = NULL;
            placeholder->name.value = strdup(placeholder_name);
            placeholder->name.type = TOKEN_IDENTIFIER;
            placeholder->type = TOKEN_INT;
            placeholder->dataType = DATA_INT;
            placeholder->internal_name = strdup(placeholder_name);
            placeholder->dim_count = 0;
            placeholder->type_param_index = -1;
            ast_function->vararg_param_indices = realloc(
                ast_function->vararg_param_indices,
                sizeof(int) * (ast_function->vararg_param_count + 1));
            ast_function->vararg_internal_names = realloc(
                ast_function->vararg_internal_names,
                sizeof(char*) * (ast_function->vararg_param_count + 1));
            ast_function->vararg_param_indices[ast_function->vararg_param_count]
                = ast_function->parameterCount;
            ast_function->vararg_internal_names[ast_function->vararg_param_count]
                = strdup(placeholder_name);
            if (ast_function->vararg_param_count == 0) {
                ast_function->vararg_param_index = ast_function->parameterCount;
                ast_function->vararg_internal_name = strdup(placeholder_name);
            }
            ast_function->vararg_param_count++;
            function_add_parameter(ast_function, (ASTNode*)placeholder, DATA_INT);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        ASTNode* temp = parser_var(parser,false);
        if (temp == NULL) {
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_COMMA) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        DataType param_dtype;
        if (temp->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)temp;
            param_dtype = arr->elementType;
        } else if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)temp;
            if (var->func_ptr_sig || var->ptr_array_info) {
                var->dataType = DATA_INT;
            }
            param_dtype = var->dataType;
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                ast_function->has_varargs = true;
                int idx = ast_function->vararg_param_count++;
                ast_function->vararg_param_indices = realloc(
                    ast_function->vararg_param_indices,
                    sizeof(int) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_internal_names = realloc(
                    ast_function->vararg_internal_names,
                    sizeof(char*) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_param_indices[idx] = ast_function->parameterCount;
                ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                if (ast_function->vararg_param_count == 1) {
                    ast_function->vararg_param_index = ast_function->parameterCount;
                    if (ast_function->vararg_internal_name)
                        free(ast_function->vararg_internal_name);
                    ast_function->vararg_internal_name = strdup(var->name.value);
                }
                if (var->total_bytes < 8) var->total_bytes = 8;
            }
        } else {
            param_dtype = DATA_ERROR;
        }
        function_add_parameter(ast_function, temp, param_dtype);

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                        AST_Var* pv = (AST_Var*)temp;
                        ast_function->has_varargs = true;
                        int idx = ast_function->vararg_param_count++;
                        ast_function->vararg_param_indices = realloc(
                            ast_function->vararg_param_indices,
                            sizeof(int) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_internal_names = realloc(
                            ast_function->vararg_internal_names,
                            sizeof(char*) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_param_indices[idx]
                            = ast_function->parameterCount - 1;
                        ast_function->vararg_internal_names[idx]
                            = strdup(pv->name.value);
                        if (ast_function->vararg_param_count == 1) {
                            ast_function->vararg_param_index
                                = ast_function->parameterCount - 1;
                            if (ast_function->vararg_internal_name)
                                free(ast_function->vararg_internal_name);
                            ast_function->vararg_internal_name
                                = strdup(pv->name.value);
                        }
                        if (pv->total_bytes < 8) pv->total_bytes = 8;
                        if (pv->dim_count == 0) {
                            pv->dim_count = 1;
                            pv->dim_sizes = malloc(sizeof(int));
                            pv->dim_sizes[0] = -1;
                        }
                    }
                    continue;
                }
                if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                    AST_Var* pv = (AST_Var*)temp;
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx]
                        = ast_function->parameterCount - 1;
                    ast_function->vararg_internal_names[idx]
                        = strdup(pv->name.value);
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index
                            = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name
                            = strdup(pv->name.value);
                    }
                }
                break;
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    if (parser_current_is_type(parser, TOKEN_CONST)) {
        ast_function->is_const_method = true;
        parser_advance(parser);
    }

    ast_function->return_type = return_type;

    if (g_native_requested) {
        g_native_requested = false;
        parser_eat(parser, TOKEN_SEMICOLON);
        ast_function->is_native = true;
        ast_function->return_Count = 1;
        ast_function->Return = calloc(1, sizeof(AST_Return));
        if (!ast_function->Return) OUT_OF_MEMORY();
        ast_function->Return->base.type = AST_RETURN;
        ast_function->Return->returnType = return_type;
        if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
            ast_function->ret_func_sig->multi_return = multi_return;
            multi_return = NULL;
        }
        ast_function->multi_return = multi_return;
        ast_function->base.type = AST_FUNCTION;
        ast_function->base.next = NULL;
        return (ASTNode*)ast_function;
    }

parse_function_body:
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ast_function->Code = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    set_return_type_recursive(ast_function->Code, return_type);
    if (ast_function->is_generic && return_type == DATA_VOID
    && ast_function->ret_func_sig == NULL && !ast_function->returns_array
    && ast_function->multi_return == NULL) {
        ast_function->return_type = DATA_IDENTIFIER;
        set_return_type_recursive(ast_function->Code, DATA_IDENTIFIER);
    }

    ASTNode* temp = ast_function->Code;
    while (temp) {
        if (temp->type == AST_RETURN) {
            AST_Return* ret = (AST_Return*)temp;
            ast_function->Return = ret;
            ast_function->return_Count = 1;
            ret->returnType = return_type;
        }
        temp = temp->next;
    }

    if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
        ast_function->ret_func_sig->multi_return = multi_return;
        multi_return = NULL;
    }
    ast_function->multi_return = multi_return;
    ast_function->base.type = AST_FUNCTION;
    ast_function->base.next = NULL;
    return (ASTNode*)ast_function;
}

typedef struct ParserNestedGeneric {
    char* mangled;
    char* base;
    int argc;
    DataType* args;
    char** arg_names;
} ParserNestedGeneric;

static ParserNestedGeneric* g_nested_generics = NULL;
static int g_nested_generic_count = 0;
static int g_nested_generic_cap = 0;
static bool g_gt_second_pending = false;

void parser_register_nested_generic(const char* mangled, const char* base,
                                    DataType* args, char** names, int argc) {
    if (mangled == NULL || base == NULL) return;
    for (int i = 0; i < g_nested_generic_count; i++) {
        if (strcmp(g_nested_generics[i].mangled, mangled) == 0) return;
    }
    if (g_nested_generic_count >= g_nested_generic_cap) {
        g_nested_generic_cap = g_nested_generic_cap ? g_nested_generic_cap * 2 : 8;
        g_nested_generics = realloc(g_nested_generics,
            sizeof(ParserNestedGeneric) * (size_t)g_nested_generic_cap);
        if (!g_nested_generics) OUT_OF_MEMORY();
    }
    ParserNestedGeneric* e = &g_nested_generics[g_nested_generic_count++];
    e->mangled = strdup(mangled);
    e->base = strdup(base);
    e->argc = argc;
    e->args = malloc(sizeof(DataType) * (size_t)(argc > 0 ? argc : 1));
    e->arg_names = calloc((size_t)(argc > 0 ? argc : 1), sizeof(char*));
    if (!e->args || !e->arg_names) OUT_OF_MEMORY();
    for (int i = 0; i < argc; i++) {
        e->args[i] = args[i];
        e->arg_names[i] = (names && names[i]) ? strdup(names[i]) : NULL;
    }
}

bool parser_lookup_nested_generic(const char* mangled, char** base,
                                  DataType** args, char*** names, int* argc) {
    if (mangled == NULL) return false;
    for (int i = 0; i < g_nested_generic_count; i++) {
        if (strcmp(g_nested_generics[i].mangled, mangled) == 0) {
            if (base) *base = g_nested_generics[i].base;
            if (args) *args = g_nested_generics[i].args;
            if (names) *names = g_nested_generics[i].arg_names;
            if (argc) *argc = g_nested_generics[i].argc;
            return true;
        }
    }
    return false;
}

static const char* parser_generic_arg_piece(DataType* args, char** names,
                                            int i) {
    if (args[i] == DATA_IDENTIFIER && names && names[i]) return names[i];
    return data_type_to_string(args[i]);
}

static char* parser_make_instance_name(const char* base, DataType* args,
                                       char** names, int argc) {
    int len = (int)strlen(base) + 2;
    for (int i = 0; i < argc; i++)
        len += (int)strlen(parser_generic_arg_piece(args, names, i)) + 1;
    char* name = malloc((size_t)len + 1);
    if (!name) OUT_OF_MEMORY();
    strcpy(name, base);
    strcat(name, "$");
    for (int i = 0; i < argc; i++) {
        strcat(name, parser_generic_arg_piece(args, names, i));
        if (i < argc - 1) strcat(name, "_");
    }
    return name;
}

static bool parser_at_generic_gt(Parser* parser) {
    return parser_current_is_type(parser, TOKEN_GT)
        || parser_current_is_type(parser, TOKEN_SHIFT_RIGHT);
}

static void parser_take_generic_gt(Parser* parser) {
    if (parser_current_is_type(parser, TOKEN_GT)) {
        g_gt_second_pending = false;
        parser_advance(parser);
        return;
    }
    if (parser_current_is_type(parser, TOKEN_SHIFT_RIGHT)) {
        if (!g_gt_second_pending) {
            g_gt_second_pending = true;
        } else {
            g_gt_second_pending = false;
            parser_advance(parser);
        }
        return;
    }
    parser_eat(parser, TOKEN_GT);
}

static int parser_parse_generic_arg_list(Parser* parser, DataType** out_types,
                                         char*** out_names) {
    int cap = 4;
    int count = 0;
    DataType* types = malloc(sizeof(DataType) * (size_t)cap);
    char** names = calloc((size_t)cap, sizeof(char*));
    if (!types || !names) OUT_OF_MEMORY();

    do {
        if (!is_var_type(parser->current.type)
            && parser->current.type != TOKEN_IDENTIFIER) {
            ERROR(ERR_PARSE_INVALID_TYPE,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected type argument, got '%s'",
                  type_to_string(parser->current.type));
            break;
        }
        DataType dt = Get_Datatype(parser->current.type);
        char* arg_name = NULL;
        if (dt == DATA_IDENTIFIER) {
            arg_name = strdup(parser->current.value);
            parser_advance(parser);
            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int nl = (int)strlen(arg_name) + 1
                       + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)nl);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)nl, "%s_%s",
                         arg_name, parser->current.value);
                free(arg_name);
                arg_name = merged;
                parser_advance(parser);
            }
            if (parser_current_is_type(parser, TOKEN_LT)) {
                parser_advance(parser);
                DataType* inner_types = NULL;
                char** inner_names = NULL;
                int inner_count = parser_parse_generic_arg_list(
                    parser, &inner_types, &inner_names);
                char* inner_mangled = parser_make_instance_name(
                    arg_name, inner_types, inner_names, inner_count);
                parser_register_nested_generic(inner_mangled, arg_name,
                                               inner_types, inner_names,
                                               inner_count);
                free(inner_types);
                if (inner_names) {
                    for (int k = 0; k < inner_count; k++) free(inner_names[k]);
                    free(inner_names);
                }
                free(arg_name);
                arg_name = inner_mangled;
            }
        } else {
            parser_advance(parser);
        }

        if (count >= cap) {
            int old_cap = cap;
            cap *= 2;
            types = realloc(types, sizeof(DataType) * (size_t)cap);
            names = realloc(names, sizeof(char*) * (size_t)cap);
            if (!types || !names) OUT_OF_MEMORY();
            for (int k = old_cap; k < cap; k++) names[k] = NULL;
        }
        types[count] = dt;
        names[count] = arg_name;
        count++;

        if (!parser_current_is_type(parser, TOKEN_COMMA)) break;
        parser_advance(parser);
    } while (true);

    parser_take_generic_gt(parser);
    *out_types = types;
    *out_names = names;
    return count;
}
ASTNode* parser_var_with_typename(Parser* parser) {
    char* className = strdup(parser->current.value);
    parser_advance(parser);

    DataType* generic_type_args = NULL;
    char** generic_type_arg_names = NULL;
    int generic_type_arg_count = 0;
    int generic_type_cap = 0;

    if (parser_current_is_type(parser, TOKEN_LT)) {
        parser_advance(parser);
        generic_type_arg_count = parser_parse_generic_arg_list(
            parser, &generic_type_args, &generic_type_arg_names);
        generic_type_cap = generic_type_arg_count;
    }

    ASTNode* head = NULL;
    ASTNode* tail = NULL;

    do {
        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected variable name after type");
            free(className);
            AST_Free(head);
            return NULL;
        }

        Token varName = parser->current;
        parser_advance(parser);

        ASTNode* init = NULL;
        if (parser_current_is_type(parser, TOKEN_ASSIGN)) {
            parser_advance(parser);
            init = parser_Binary(parser);
        }

        AST_Var* var = calloc(1, sizeof(AST_Var));
        var->base.type = (init != NULL) ? AST_VAR : AST_UNINIT_VAR;
        var->base.next = NULL;
        var->base.value = varName;
        var->type = TOKEN_IDENTIFIER;
        var->name = varName;
        var->Value = init;
        var->dataType = DATA_IDENTIFIER;
        var->internal_name = className;
        var->dim_count = 0;
        var->dim_sizes = NULL;
        var->modifier = 0;
        var->type_param_index = -1;

        if (generic_type_arg_count > 0) {
            var->generic_type_args = malloc(
                sizeof(DataType) * generic_type_arg_count);
            memcpy(var->generic_type_args, generic_type_args,
                   sizeof(DataType) * generic_type_arg_count);
            var->generic_type_arg_count = generic_type_arg_count;
            var->generic_type_arg_names = generic_type_arg_names;
            generic_type_arg_names = NULL;
        }

        ASTNode* node = (ASTNode*)var;

        if (!head) {
            head = tail = node;
        } else {
            var->internal_name = strdup(className);
            tail->next = node;
            tail = node;
        }

        if (!parser_current_is_type(parser, TOKEN_COMMA))
            break;
        parser_advance(parser);

    } while (true);

    return head;
}

ASTNode* parser_def_function(Parser* parser) {
    AST_Function* ast_function = calloc(1,sizeof(AST_Function));
    ast_function->max_parameters = 4;
    ast_function->parameterType = malloc(sizeof(DataType) * ast_function->max_parameters);
    ast_function->return_Count = 0;
    ast_function->parameterCount = 0;
    ast_function->Parameter = NULL;
    ast_function->Return = NULL;
    ast_function->internal_name = NULL;
    ast_function->returns_array = false;
    ast_function->return_element_type = DATA_VOID;
    ast_function->return_dim_count = 0;
    ast_function->return_dim_sizes = NULL;
    ast_function->ret_func_sig = NULL;

    if (parser_current_is_type(parser, TOKEN_PUB))
    {
        ast_function->Modifier = 0;
        parser_eat(parser, TOKEN_PUB);
    } else if (parser_current_is_type(parser, TOKEN_PRO))
    {
        ast_function->Modifier = 1;
        parser_eat(parser, TOKEN_PRO);
    } else if (parser_current_is_type(parser, TOKEN_PRI))
    {
        ast_function->Modifier = 2;
        parser_eat(parser, TOKEN_PRI);
    }

    if (parser->current.type == TOKEN_IDENTIFIER) {
        ast_function->return_class_name = strdup(parser->current.value);
    }

    DataType return_type = DATA_VOID;
    MultiReturnType* multi_return = NULL;
    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)
        && parser->tokens[parser->index + 1].type != TOKEN_MUL) {
        int saved = parser->index;
        int look = saved + 1;
        int depth = 1;
        while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) { depth--; if (depth == 0) break; }
            look++;
        }
        bool is_func_ptr = false;
        if (depth == 0) {
            int after = look + 1;
            if (parser->tokens[after].type == TOKEN_PAREN_LEFT
                && parser->tokens[after + 1].type == TOKEN_MUL
                && parser->tokens[after + 2].type == TOKEN_PAREN_RIGHT) {
                is_func_ptr = true;
            }
        }
        if (is_func_ptr) {
            FuncSignature* fpsig = parse_func_ptr_type_signature(parser);
            if (fpsig) {
                if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
                ast_function->ret_func_sig = fpsig;
                return_type = DATA_INT;;
            }
        } else {
            multi_return = parse_multi_return_type(parser);
            return_type = DATA_VOID;
        }
    } else if (is_var_type(parser->current.type) || parser->current.type == TOKEN_IDENTIFIER) {
        return_type = Get_Datatype(parser->current.type);
        if (return_type == DATA_IDENTIFIER) {
            parser_advance(parser);
            char* tn = ast_function->return_class_name;
            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int nl = (int)strlen(tn) + 1 + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)nl);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)nl, "%s_%s", tn, parser->current.value);
                free(tn);
                tn = merged;
                parser_advance(parser);
                   }
            ast_function->return_class_name = tn;
        } else {
            parser_advance(parser);
        }
    } else {
        ERROR(ERR_PARSE_INVALID_TYPE,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected return type, got '%s'",
              type_to_string(parser->current.type));
    }
    int return_ptr_depth = 0;
    while (parser_current_is_type(parser, TOKEN_MUL)) {
        return_ptr_depth++;
        parser_advance(parser);
    }

    ast_function->return_ptr_depth = return_ptr_depth;

    if (parser_current_is_type(parser, TOKEN_LT)) {
        parser_advance(parser);
        int depth = 1;
        while (depth > 0 && !parser_current_is_type(parser, TOKEN_EOF)) {
            if (parser_current_is_type(parser, TOKEN_LT)) depth++;
            else if (parser_current_is_type(parser, TOKEN_GT)) depth--;
            else if (parser_current_is_type(parser, TOKEN_SHIFT_RIGHT)) {
                depth -= 2;
                if (depth < 0) depth = 0;
            }
            parser_advance(parser);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_idx = parser->index;
        int look = saved_idx + 1;
        int star_count = 0;
        while (parser->tokens[look].type == TOKEN_MUL && look < saved_idx + 10) {
            star_count++;
            look++;
        }

        if (star_count > 0 && parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            while (parser_current_is_type(parser, TOKEN_MUL)) {
                return_ptr_depth++;
                parser_advance(parser);
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            parser_eat(parser, TOKEN_PAREN_LEFT);
            int param_cap = 4;
            int param_count = 0;
            DataType* param_types = malloc(sizeof(DataType) * param_cap);
            bool func_ptr_has_varargs = false;
            int* func_ptr_vararg_indices = NULL;
            int func_ptr_vararg_count = 0;
            int func_ptr_vararg_cap = 4;
            func_ptr_vararg_indices = malloc(sizeof(int) * func_ptr_vararg_cap);
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_EOF)) {
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    if (param_count > 0) {
                        func_ptr_has_varargs = true;
                        if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                            func_ptr_vararg_cap *= 2;
                            func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                                sizeof(int) * func_ptr_vararg_cap);
                        }
                        func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count - 1;
                    }
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
                {
                    DataType nested_base = DATA_ERROR;
                    FuncSignature* nested_sig = NULL;
                    if (parser_try_nested_func_ptr_param(parser, &nested_base, &nested_sig)) {
                        if (param_count >= param_cap) {
                            param_cap *= 2;
                            param_types = realloc(param_types, sizeof(DataType) * param_cap);
                        }
                        param_types[param_count] = nested_base;
                        param_count++;
                        if (parser_current_is_type(parser, TOKEN_COMMA)) {
                            parser_eat(parser, TOKEN_COMMA);
                        } else {
                            break;
                        }
                        continue;
                    }
                }
                if (!is_var_type(parser->current.type) && parser->current.type != TOKEN_IDENTIFIER) {
                    ERROR(ERR_PARSE_EXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "expected type in function pointer return type");
                    break;
                }
                DataType pt = Get_Datatype(parser->current.type);
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                    parser_advance(parser);
                bool is_vararg = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    is_vararg = true;
                    parser_advance(parser);
                }
                if (param_count >= param_cap) {
                    param_cap *= 2;
                    param_types = realloc(param_types, sizeof(DataType) * param_cap);
                }
                param_types[param_count] = pt;
                if (is_vararg) {
                    func_ptr_has_varargs = true;
                    if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                        func_ptr_vararg_cap *= 2;
                        func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                            sizeof(int) * func_ptr_vararg_cap);
                    }
                    func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
                }
                param_count++;
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            FuncSignature* sig = calloc(1, sizeof(FuncSignature));
            sig->return_type = return_type;
            sig->param_count = param_count;
            sig->param_types = param_types;
            sig->has_varargs = func_ptr_has_varargs;
            sig->vararg_param_count = func_ptr_vararg_count;
            sig->vararg_param_indices = (func_ptr_vararg_count > 0) ? func_ptr_vararg_indices : NULL;
            sig->multi_return = multi_return;
            multi_return = NULL;
            if (func_ptr_vararg_count == 0) free(func_ptr_vararg_indices);
            if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
            ast_function->ret_func_sig = sig;
            ast_function->return_ptr_depth += star_count;
            return_type = DATA_INT;
        }
        else if (parse_return_type_func_ptr(parser, ast_function,
                                             return_type, return_ptr_depth)) {
            return_type = DATA_INT;
        }
    }

    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT))
    {
        int dim_cap = 4;
        int dim_cnt = 0;
        int* dim_sizes = malloc(sizeof(int) * dim_cap);
        ASTNode** dim_exprs = calloc(dim_cap, sizeof(ASTNode*));
        if (!dim_sizes) OUT_OF_MEMORY();
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT))
        {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode* expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (dim_cnt >= dim_cap)
            {
                dim_cap *= 2;
                dim_sizes = realloc(dim_sizes, sizeof(int) * (size_t)dim_cap);
                dim_exprs = realloc(dim_exprs, sizeof(ASTNode*) * dim_cap);
                if (!dim_sizes) OUT_OF_MEMORY();
            }
            dim_sizes[dim_cnt] = size;
            dim_exprs[dim_cnt] = expr;
            dim_cnt++;
        }
        ast_function->return_dim_exprs = dim_exprs;
        if (dim_cnt > 0)
        {
            ast_function->returns_array = true;
            ast_function->return_element_type = return_type;
            ast_function->return_dim_count = dim_cnt;
            ast_function->return_dim_sizes = dim_sizes;
            return_type = DATA_VOID;
        }
        else
        {
            free(dim_sizes);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_name = parser->index;
        if (parser->tokens[saved_name + 1].type == TOKEN_MUL
            && parser->tokens[saved_name + 2].type == TOKEN_IDENTIFIER
            && parser->tokens[saved_name + 3].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            parser_eat(parser, TOKEN_MUL);
            ast_function->base.value = parser->current;
            ast_function->name = parser->current;
            parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            } else {
                ast_function->base.value = parser->current;
                ast_function->name = parser->current;
                parser_advance(parser);
            }
    } else {
        ast_function->base.value = parser->current;
        ast_function->name = parser->current;
        parser_advance(parser);
    }

    parser_eat(parser, TOKEN_PAREN_LEFT);
    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) && !parser_current_is_type(parser, TOKEN_EOF)) {
        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            int saved = parser->index;
            Token saved_cur = parser->current;
            ASTNode* param = parser_multi_return_func_ptr_var(parser);
            if (param != NULL) {
                DataType pdt = DATA_INT;
                function_add_parameter(ast_function, param, pdt);
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx] = ast_function->parameterCount - 1;
                    if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
                        AST_Var* var = (AST_Var*)param;
                        ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                        if (var->func_ptr_sig == NULL) {
                            if (var->total_bytes < 8) var->total_bytes = 8;
                            var->dim_count = 1;
                            if (!var->dim_sizes) {
                                var->dim_sizes = malloc(sizeof(int));
                                var->dim_sizes[0] = -1;
                            }
                        }
                    } else {
                        char placeholder[32];
                        snprintf(placeholder, sizeof(placeholder),
                                 "_va_mfp_%d", idx);
                        ast_function->vararg_internal_names[idx] = strdup(placeholder);
                    }
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name = strdup(
                            ast_function->vararg_internal_names[0]);
                    }
                }
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
                continue;
            }
            parser->index = saved;
            parser->current = saved_cur;
        }
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            ast_function->has_varargs = true;

            char placeholder_name[32];
            snprintf(placeholder_name, sizeof(placeholder_name),
                     "_va_placeholder_%d", ast_function->vararg_param_count);

            AST_Var* placeholder = calloc(1, sizeof(AST_Var));
            placeholder->base.type = AST_VARARG;
            placeholder->base.next = NULL;
            placeholder->name.value = strdup(placeholder_name);
            placeholder->name.type = TOKEN_IDENTIFIER;
            placeholder->type = TOKEN_INT;
            placeholder->dataType = DATA_INT;
            placeholder->internal_name = strdup(placeholder_name);
            placeholder->dim_count = 0;
            placeholder->type_param_index = -1;

            ast_function->vararg_param_indices = realloc(
                ast_function->vararg_param_indices,
                sizeof(int) * (ast_function->vararg_param_count + 1));
            ast_function->vararg_internal_names = realloc(
                ast_function->vararg_internal_names,
                sizeof(char*) * (ast_function->vararg_param_count + 1));

            ast_function->vararg_param_indices[ast_function->vararg_param_count]
                = ast_function->parameterCount;
            ast_function->vararg_internal_names[ast_function->vararg_param_count]
                = strdup(placeholder_name);

            if (ast_function->vararg_param_count == 0) {
                ast_function->vararg_param_index = ast_function->parameterCount;
                ast_function->vararg_internal_name = strdup(placeholder_name);
            }
            ast_function->vararg_param_count++;

            function_add_parameter(ast_function, (ASTNode*)placeholder, DATA_INT);

            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        ASTNode* temp = parser_var(parser,false);
        if (temp == NULL) {
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_COMMA) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }

        DataType param_dtype;
        if (temp->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)temp;
            param_dtype = arr->elementType;
        } else if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)temp;
            if (var->func_ptr_sig || var->ptr_array_info) {
                var->dataType = DATA_INT;
            }
            param_dtype = var->dataType;
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                ast_function->has_varargs = true;
                int idx = ast_function->vararg_param_count++;
                ast_function->vararg_param_indices = realloc(
                    ast_function->vararg_param_indices,
                    sizeof(int) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_internal_names = realloc(
                    ast_function->vararg_internal_names,
                    sizeof(char*) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_param_indices[idx] = ast_function->parameterCount;
                ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                if (ast_function->vararg_param_count == 1) {
                    ast_function->vararg_param_index = ast_function->parameterCount;
                    if (ast_function->vararg_internal_name)
                        free(ast_function->vararg_internal_name);
                    ast_function->vararg_internal_name = strdup(var->name.value);
                }
                if (var->total_bytes < 8) var->total_bytes = 8;
            }
        } else {
            param_dtype = DATA_ERROR;
        }

        function_add_parameter(ast_function, temp, param_dtype);

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                        AST_Var* pv = (AST_Var*)temp;
                        ast_function->has_varargs = true;
                        int idx = ast_function->vararg_param_count++;
                        ast_function->vararg_param_indices = realloc(
                            ast_function->vararg_param_indices,
                            sizeof(int) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_internal_names = realloc(
                            ast_function->vararg_internal_names,
                            sizeof(char*) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_param_indices[idx]
                            = ast_function->parameterCount - 1;
                        ast_function->vararg_internal_names[idx]
                            = strdup(pv->name.value);
                        if (ast_function->vararg_param_count == 1) {
                            ast_function->vararg_param_index
                                = ast_function->parameterCount - 1;
                            if (ast_function->vararg_internal_name)
                                free(ast_function->vararg_internal_name);
                            ast_function->vararg_internal_name
                                = strdup(pv->name.value);
                        }
                        if (pv->total_bytes < 8) pv->total_bytes = 8;
                        if (pv->dim_count == 0) {
                            pv->dim_count = 1;
                            pv->dim_sizes = malloc(sizeof(int));
                            pv->dim_sizes[0] = -1;
                        }
                    }
                    continue;
                }
                if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                    AST_Var* pv = (AST_Var*)temp;
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx]
                        = ast_function->parameterCount - 1;
                    ast_function->vararg_internal_names[idx]
                        = strdup(pv->name.value);
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index
                            = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name
                            = strdup(pv->name.value);
                    }
                }
                break;
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    if (parser_current_is_type(parser, TOKEN_CONST)) {
        ast_function->is_const_method = true;
        parser_advance(parser);
    }

    ast_function->return_type = return_type;

    if (g_native_requested) {
        g_native_requested = false;
        parser_eat(parser, TOKEN_SEMICOLON);
        ast_function->is_native = true;
        ast_function->return_Count = 1;
        ast_function->Return = calloc(1, sizeof(AST_Return));
        if (!ast_function->Return) OUT_OF_MEMORY();
        ast_function->Return->base.type = AST_RETURN;
        ast_function->Return->returnType = return_type;
        if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
            ast_function->ret_func_sig->multi_return = multi_return;
            multi_return = NULL;
        }
        ast_function->multi_return = multi_return;
        ast_function->base.type = AST_FUNCTION;
        ast_function->base.next = NULL;
        return (ASTNode*)ast_function;
    }

parse_function_body:
    parser_eat(parser, TOKEN_BRACKET_LEFT);

    ast_function->Code = parser_parse_code_block(parser);
    set_return_type_recursive(ast_function->Code, return_type);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);
    ASTNode* temp = ast_function->Code;
    while (temp != NULL)
    {
        if (temp->type == AST_RETURN)
        {
            AST_Return* ast_return = (AST_Return*)temp;
            ast_function->Return = ast_return;
            ast_function->return_Count = 1;
        }
        temp = temp->next;
    }

    if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
        ast_function->ret_func_sig->multi_return = multi_return;
        multi_return = NULL;
    }
    ast_function->multi_return = multi_return;
    ast_function->base.type = AST_FUNCTION;
    ast_function->base.next = NULL;
    return (ASTNode*)ast_function;
}

ASTNode* parser_while(Parser* parser)
{
    AST_While* ast_while = calloc(1,sizeof(AST_While));
    ast_while->base.value = parser->current;
    parser_eat(parser, TOKEN_WHILE);
    ast_while->Condition = parser_Binary(parser);
    ast_while->has_do = false;
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ast_while->code = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);
    ast_while->base.next = NULL;
    ast_while->base.type = AST_WHILE;
    return (ASTNode*)ast_while;
}

ASTNode* parser_do_while(Parser* parser)
{
    AST_While* ast_while = calloc(1,sizeof(AST_While));
    ast_while->base.value = parser->current;
    parser_eat(parser, TOKEN_DO);

    ast_while->has_do = true;

    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ast_while->code = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    parser_eat(parser, TOKEN_WHILE);

    ast_while->Condition = parser_Binary(parser);

    ast_while->base.next = NULL;
    ast_while->base.type = AST_WHILE;
    return (ASTNode*)ast_while;
}

static ASTNode* parser_for_init_part(Parser* parser) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;

    while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
           !parser_current_is_type(parser, TOKEN_EOF)) {
        ASTNode* item = NULL;

        if (is_var_type(parser->current.type)) {
            TokenType saved_type = parser->current.type;
            parser_advance(parser);

            if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "expected variable name");
                return head;
            }

            Token var_name = parser->current;
            parser_advance(parser);

            ASTNode* init_value = NULL;
            if (parser_current_is_type(parser, TOKEN_ASSIGN)) {
                parser_advance(parser);
                init_value = parser_Binary(parser);
            }

            AST_Var* var = calloc(1, sizeof(AST_Var));
            var->base.type = (init_value != NULL) ? AST_VAR : AST_UNINIT_VAR;
            var->base.next = NULL;
            var->base.value = var_name;
            var->type = saved_type;
            var->name = var_name;
            var->Value = init_value;
            var->dataType = Get_Datatype(saved_type);
            var->internal_name = NULL;
            var->dim_count = 0;
            var->dim_sizes = NULL;
            var->type_param_index = -1;

            item = (ASTNode*)var;
        } else {
            item = parser_Binary(parser);
        }

        if (!item) break;

        if (!head) {
            head = item;
            tail = item;
        } else {
            tail->next = item;
            tail = item;
        }

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
        } else {
            break;
        }
           }
    parser_eat(parser, TOKEN_SEMICOLON);
    return head;
}

static ASTNode* parser_statement_expr(Parser* parser) {
    if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
    {
        TokenType next = parser->tokens[parser->index + 1].type;
        if (next == TOKEN_INCREMENT || next == TOKEN_DECREMENT ||
            next == TOKEN_ADD_ASSIGN || next == TOKEN_SUB_ASSIGN ||
            next == TOKEN_MUL_ASSIGN || next == TOKEN_DIV_ASSIGN ||
            next == TOKEN_MOD_ASSIGN) {
            return parser_compound_assign_or_incdec(parser);
            }

        if (next == TOKEN_ASSIGN) {
            Token var_token = parser->current;
            parser_advance(parser);
            parser_advance(parser);
            ASTNode* value = parser_Binary(parser);
            AST_Var* var = calloc(1, sizeof(AST_Var));
            if (!var) {
                OUT_OF_MEMORY();
            }
            var->base.type = AST_VAR_ASS;
            var->base.next = NULL;
            var->base.value = var_token;
            var->name = var_token;
            var->Value = value;
            var->type = var_token.type;
            var->dataType = DATA_ERROR;
            var->internal_name = NULL;
            var->is_field_ref = false;
            var->field_offset = 0;
            var->type_param_index = -1;
            return (ASTNode*)var;
        }

        if (next == TOKEN_SQUARE_BRACKET_LEFT) {
            return parser_def_var(parser);
        }
    }

    if (parser_current_is_type(parser, TOKEN_THIS)) {
        TokenType next = parser->tokens[parser->index + 1].type;
        if (next == TOKEN_DOT) {

            ASTNode* left = parser_Unary(parser);

            if (parser_current_is_type(parser, TOKEN_ASSIGN) ||
                parser_current_is_type(parser, TOKEN_ADD_ASSIGN) ||
                parser_current_is_type(parser, TOKEN_SUB_ASSIGN) ||
                parser_current_is_type(parser, TOKEN_MUL_ASSIGN) ||
                parser_current_is_type(parser, TOKEN_DIV_ASSIGN) ||
                parser_current_is_type(parser, TOKEN_MOD_ASSIGN)) {
                return parser_member_assign(parser, left);
            }
            return left;
        }
    }

    if (parser_current_is_type(parser, TOKEN_INCREMENT) ||
        parser_current_is_type(parser, TOKEN_DECREMENT)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Unary(parser);
        AST_Unary* unary = malloc(sizeof(AST_Unary));
        unary->base.type = AST_UNARY;
        unary->base.next = NULL;
        unary->base.value = op;
        unary->op = op;
        unary->right = right;
        unary->dataType = DATA_ERROR;
        return (ASTNode*)unary;
    }

    return parser_Binary(parser);
}

ASTNode* parser_for(Parser* parser) {
    parser_eat(parser, TOKEN_FOR);

    if (is_var_type(parser->current.type)) {
        int saved_idx = parser->index;
        TokenType t1 = parser->tokens[saved_idx + 1].type;
        TokenType t2 = parser->tokens[saved_idx + 2].type;
        if (t1 == TOKEN_IDENTIFIER && t2 == TOKEN_COLON) {
            DataType declared_type = Get_Datatype(parser->current.type);
            parser_advance(parser);
            Token iter_token = parser->current;
            parser_advance(parser);
            parser_eat(parser, TOKEN_COLON);
            ASTNode* array_expr = parser_Binary(parser);
            parser_eat(parser, TOKEN_BRACKET_LEFT);
            ASTNode* body = parser_parse_code_block(parser);
            parser_eat(parser, TOKEN_BRACKET_RIGHT);
            AST_ForEach* foreach_node = calloc(1, sizeof(AST_ForEach));
            foreach_node->base.type = AST_FOREACH;
            foreach_node->iterator = iter_token;
            foreach_node->array = array_expr;
            foreach_node->code = body;
            foreach_node->internal_iterator = NULL;
            foreach_node->elemType = declared_type;
            foreach_node->has_explicit_type = true;
            return (ASTNode*)foreach_node;
        }
    }

    if (parser_current_is_type(parser, TOKEN_IDENTIFIER) &&
        parser->tokens[parser->index + 1].type == TOKEN_COLON) {

        Token iter_token = parser->current;
        parser_advance(parser);
        parser_eat(parser, TOKEN_COLON);

        ASTNode* array_expr = parser_Binary(parser);

        parser_eat(parser, TOKEN_BRACKET_LEFT);
        ASTNode* body = parser_parse_code_block(parser);
        parser_eat(parser, TOKEN_BRACKET_RIGHT);

        AST_ForEach* foreach_node = calloc(1, sizeof(AST_ForEach));
        foreach_node->base.type = AST_FOREACH;
        foreach_node->iterator = iter_token;
        foreach_node->array = array_expr;
        foreach_node->code = body;
        foreach_node->internal_iterator = NULL;
        foreach_node->has_explicit_type = false;
        foreach_node->elemType = DATA_ERROR;
        return (ASTNode*)foreach_node;
    }

    ASTNode* init = parser_for_init_part(parser);

    ASTNode* condition = NULL;
    if (!parser_current_is_type(parser, TOKEN_SEMICOLON)) {
        condition = parser_Binary(parser);
    }
    parser_eat(parser, TOKEN_SEMICOLON);

    ASTNode* increment = NULL;
    ASTNode* incr_tail = NULL;

    while (!parser_current_is_type(parser, TOKEN_BRACKET_LEFT) &&
           !parser_current_is_type(parser, TOKEN_EOF)) {
               ASTNode* stmt = parser_statement_expr(parser);
               if (!stmt) break;
               if (!increment) {
                   increment = stmt;
                   incr_tail = stmt;
               } else {
                   incr_tail->next = stmt;
                   incr_tail = stmt;
               }
               if (parser_current_is_type(parser, TOKEN_COMMA)) {
                   parser_advance(parser);
               } else {
                   break;
               }
           }

    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ASTNode* body = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    AST_For* for_node = calloc(1, sizeof(AST_For));
    for_node->base.type = AST_FOR;
    for_node->init = init;
    for_node->condition = condition;
    for_node->increment = increment;
    for_node->code = body;
    return (ASTNode*)for_node;
}

void parser_elif_resize(AST_If* ast_if)
{

    ast_if->elif_condition = realloc(ast_if->elif_condition, sizeof(ASTNode*) * (ast_if->elif_Count + 2));
    if (ast_if->elif_condition == NULL)
    {
        INTERNAL_ERROR("parser_elif_resize: realloc failed");
    }
    ast_if->Elif = realloc(ast_if->Elif, sizeof(ASTNode*) * (ast_if->elif_Count + 2));
    if (ast_if->Elif == NULL)
    {
        INTERNAL_ERROR("parser_elif_resize: realloc failed");
    }
}

void parser_elif_add(AST_If* ast_if, ASTNode* condition, ASTNode* code)
{
    ast_if->elif_condition[ast_if->elif_Count - 1] = condition;

    ast_if->Elif[ast_if->elif_Count - 1] = code;

    parser_elif_resize(ast_if);
}

ASTNode* parser_if(Parser* parser)
{
    AST_If* ast_if = calloc(1,sizeof(AST_If));
    ast_if->base.value = parser->current;
    ast_if->elif_Count = 0;
    parser_eat(parser, TOKEN_IF);

    ASTNode* Condition = parser_Binary(parser);
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ASTNode* code = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    ast_if->elif_condition = calloc(1, sizeof(ASTNode*));
    ast_if->Elif = calloc(1, sizeof(ASTNode*));

    ASTNode* el_code = NULL;
    while (parser_current_is_type(parser, TOKEN_ELSE))
    {
        parser_eat(parser, TOKEN_ELSE);
        if (parser_current_is_type(parser, TOKEN_IF))
        {
            parser_eat(parser, TOKEN_IF);
            ast_if->elif_Count++;
            ASTNode* elif_condition_temp = parser_Binary(parser);

            parser_eat(parser, TOKEN_BRACKET_LEFT);
            ASTNode* elif_code_temp = parser_parse_code_block(parser);
            parser_eat(parser, TOKEN_BRACKET_RIGHT);

            parser_elif_add(ast_if, elif_condition_temp, elif_code_temp);
        } else
        {
            ast_if->has_else = true;
            parser_eat(parser, TOKEN_BRACKET_LEFT);
            el_code = parser_parse_code_block(parser);
            parser_eat(parser, TOKEN_BRACKET_RIGHT);
            break;
        }
    }

    ast_if->Condition = Condition;
    ast_if->code = code;
    ast_if->Else = el_code;
    ast_if->base.next = NULL;
    ast_if->base.type = AST_IF;
    return (ASTNode*)ast_if;
}

ASTNode* parser_switch(Parser* parser) {
    parser_eat(parser, TOKEN_SWITCH);
    ASTNode* cond = parser_Binary(parser);
    parser_eat(parser, TOKEN_BRACKET_LEFT);

    AST_Switch* sw = calloc(1, sizeof(AST_Switch));
    sw->base.type = AST_SWITCH;
    sw->condition = cond;
    sw->cases = NULL;
    sw->case_count = 0;
    sw->case_capacity = 0;
    sw->has_default = false;
    sw->default_body = NULL;

    while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
           !parser_current_is_type(parser, TOKEN_EOF)) {
        if (parser_current_is_type(parser, TOKEN_CASE)) {
            parser_eat(parser, TOKEN_CASE);
            ASTNode* case_val = parser_Binary(parser);
            parser_eat(parser, TOKEN_COLON);

            ASTNode* body_head = NULL;
            ASTNode* body_tail = NULL;
            while (!parser_current_is_type(parser, TOKEN_CASE) &&
                   !parser_current_is_type(parser, TOKEN_DEFAULT) &&
                   !parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF)) {
                ASTNode* stmt = parser_Sentence(parser, true);
                if (stmt) {
                    // �ҵ� stmt ����������β�ڵ�
                    ASTNode* stmt_tail = stmt;
                    while (stmt_tail->next != NULL)
                        stmt_tail = stmt_tail->next;

                    if (!body_head) {
                        body_head = stmt;
                        body_tail = stmt_tail;
                    } else {
                        body_tail->next = stmt;
                        body_tail = stmt_tail;
                    }
                }
            }

            AST_Case* cs = calloc(1, sizeof(AST_Case));
            cs->base.type = AST_CASE;
            cs->value = case_val;
            cs->body = body_head;
            cs->is_default = false;

            if (sw->case_count >= sw->case_capacity) {
                sw->case_capacity = (sw->case_capacity == 0) ? 4 : sw->case_capacity * 2;
                sw->cases = realloc(sw->cases, sizeof(AST_Case*) * sw->case_capacity);
            }
            sw->cases[sw->case_count++] = cs;
        }
        else if (parser_current_is_type(parser, TOKEN_DEFAULT)) {
            parser_eat(parser, TOKEN_DEFAULT);
            parser_eat(parser, TOKEN_COLON);

            ASTNode* body_head = NULL;
            ASTNode* body_tail = NULL;
            while (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF)) {
                ASTNode* stmt = parser_Sentence(parser, true);
                if (stmt) {
                    ASTNode* stmt_tail = stmt;
                    while (stmt_tail->next != NULL)
                        stmt_tail = stmt_tail->next;
                    if (!body_head) {
                        body_head = stmt;
                        body_tail = stmt_tail;
                    } else {
                        body_tail->next = stmt;
                        body_tail = stmt_tail;
                    }
                }
            }

            sw->has_default = true;
            sw->default_body = body_head;
        } else {
            parser_advance(parser);
        }
    }

    parser_eat(parser, TOKEN_BRACKET_RIGHT);
    return (ASTNode*)sw;
}

ASTNode* parser_break(Parser* parser) {
    AST_Break* b = calloc(1, sizeof(AST_Break));
    b->base.type = AST_BREAK;
    b->base.value = parser->current;
    b->base.next = NULL;
    parser_advance(parser);
    return (ASTNode*)b;
}

ASTNode* parser_continue(Parser* parser) {
    AST_Continue* c = calloc(1, sizeof(AST_Continue));
    c->base.type = AST_CONTINUE;
    c->base.value = parser->current;
    parser_advance(parser);
    return (ASTNode*)c;
}

ASTNode* parser_try(Parser* parser)
{
    Token try_tok = parser->current;
    parser_eat(parser, TOKEN_TRY);
    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ASTNode* body = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    AST_Try* tn = calloc(1, sizeof(AST_Try));
    if (!tn) OUT_OF_MEMORY();
    tn->base.type = AST_TRY;
    tn->base.value = try_tok;
    tn->code = body;
    tn->catch_count = 0;
    tn->catch_capacity = 4;
    tn->catches = malloc(sizeof(CatchClause) * (size_t)tn->catch_capacity);
    if (!tn->catches) OUT_OF_MEMORY();

    while (parser_current_is_type(parser, TOKEN_CATCH)) {
        parser_advance(parser);
        parser_eat(parser, TOKEN_PAREN_LEFT);

        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected exception type in catch clause");
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            continue;
        }
        char* type_name = strdup(parser->current.value);
        parser_advance(parser);

        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected exception variable name in catch clause");
            free(type_name);
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            continue;
        }
        Token var_name = parser->current;
        parser_advance(parser);
        parser_eat(parser, TOKEN_PAREN_RIGHT);

        parser_eat(parser, TOKEN_BRACKET_LEFT);
        ASTNode* cbody = parser_parse_code_block(parser);
        parser_eat(parser, TOKEN_BRACKET_RIGHT);

        if (tn->catch_count >= tn->catch_capacity) {
            tn->catch_capacity *= 2;
            tn->catches = realloc(tn->catches,
                sizeof(CatchClause) * (size_t)tn->catch_capacity);
            if (!tn->catches) OUT_OF_MEMORY();
        }
        tn->catches[tn->catch_count].type_name = type_name;
        tn->catches[tn->catch_count].var_name = var_name;
        tn->catches[tn->catch_count].body = cbody;
        tn->catch_count++;
    }

    if (parser_current_is_type(parser, TOKEN_END)) {
        parser_advance(parser);
        parser_eat(parser, TOKEN_BRACKET_LEFT);
        tn->finally_code = parser_parse_code_block(parser);
        parser_eat(parser, TOKEN_BRACKET_RIGHT);
        tn->has_finally = true;
    }

    if (tn->catch_count == 0 && !tn->has_finally) {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, try_tok.line, try_tok.column),
              "try statement requires at least one catch clause or an end block");
        free(tn->catches);
        free(tn);
        return NULL;
    }
    return (ASTNode*)tn;
}

ASTNode* parser_throw_stmt(Parser* parser)
{
    Token throw_tok = parser->current;
    parser_advance(parser);
    ASTNode* value = parser_Binary(parser);
    AST_Throw* th = calloc(1, sizeof(AST_Throw));
    if (!th) OUT_OF_MEMORY();
    th->base.type = AST_THROW;
    th->base.value = throw_tok;
    th->value = value;
    return (ASTNode*)th;
}

static bool parser_parse_complex_type(Parser *parser, ParsedTypeInfo *pti) {
    memset(pti, 0, sizeof(*pti));
    pti->base_type = DATA_ERROR;

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved = parser->index;

        int look = saved + 1;
        bool looks_like_multi = false;
        if (is_var_type(parser->tokens[look].type)
            || parser->tokens[look].type == TOKEN_IDENTIFIER) {
            int depth = 1;
            while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) { depth--; if (depth==0) break; }
                if (parser->tokens[look].type == TOKEN_COMMA) looks_like_multi = true;
                if (parser->tokens[look].type == TOKEN_VARARG) looks_like_multi = true;
                look++;
            }
            look++;
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT
                && parser->tokens[look+1].type == TOKEN_MUL) {
                looks_like_multi = true;
            } else {
                looks_like_multi = false;
            }
        }

        if (looks_like_multi) {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];

            MultiReturnType *mrt = parse_multi_return_type(parser);
            if (mrt) {
                if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
                    char *inner_name = NULL;
                    FuncSignature *inner_sig = parser_parse_bare_func_ptr(
                        parser, &inner_name, mrt);
                    if (inner_sig) {
                        pti->func_sig = inner_sig;
                        pti->multi_return = mrt;
                        pti->is_multi_return = true;
                        pti->base_type = DATA_INT;
                        free(inner_name);
                        return true;
                    }
                }
                free(mrt->types); free(mrt->is_variadic);
                if (mrt->func_sigs) free(mrt->func_sigs);
                free(mrt);
            }
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved = parser->index;
        int look = saved + 1;
        if (parser->tokens[look].type == TOKEN_MUL) {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];

            char *inner_name = NULL;
            FuncSignature *sig = parser_parse_bare_func_ptr(
                parser, &inner_name, NULL);
            if (sig) {
                pti->func_sig = sig;
                pti->base_type = DATA_INT;
                free(inner_name);
                return true;
            }
        }
        parser->index = saved;
        parser->current = parser->tokens[parser->index];
        return false;
    }

    if (is_var_type(parser->current.type)
        || parser->current.type == TOKEN_IDENTIFIER) {
        pti->base_type = Get_Datatype(parser->current.type);
        if (pti->base_type == DATA_IDENTIFIER) {
            char* tn = strdup(parser->current.value);
            parser_advance(parser);
            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int nl = (int)strlen(tn) + 1
                       + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)nl);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)nl, "%s_%s",
                         tn, parser->current.value);
                free(tn);
                tn = merged;
                parser_advance(parser);
            }
            pti->type_name = tn;
        } else {
            pti->type_name = NULL;
            parser_advance(parser);
        }

        if (pti->base_type == DATA_IDENTIFIER
            && parser_current_is_type(parser, TOKEN_LT)) {
            parser_advance(parser);

            int cap = 4;
            pti->generic_type_args = malloc(sizeof(DataType) * cap);
            pti->generic_type_arg_names = calloc(cap, sizeof(char*));
            pti->generic_type_arg_count = 0;

            while (!parser_current_is_type(parser, TOKEN_GT)
                   && !parser_current_is_type(parser, TOKEN_EOF)) {

                if (!is_var_type(parser->current.type)
                    && parser->current.type != TOKEN_IDENTIFIER) {
                    break;
                }

                if (pti->generic_type_arg_count >= cap) {
                    cap *= 2;
                    pti->generic_type_args = realloc(pti->generic_type_args,
                        sizeof(DataType) * cap);
                    pti->generic_type_arg_names = realloc(pti->generic_type_arg_names,
                        sizeof(char*) * cap);
                }

                DataType dt = Get_Datatype(parser->current.type);
                pti->generic_type_args[pti->generic_type_arg_count] = dt;
                pti->generic_type_arg_names[pti->generic_type_arg_count] =
                    (dt == DATA_IDENTIFIER) ? strdup(parser->current.value) : NULL;
                pti->generic_type_arg_count++;
                parser_advance(parser);

                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                } else {
                    break;
                }
            }

            if (parser_current_is_type(parser, TOKEN_GT)) {
                parser_advance(parser);
            }
        }

        while (parser_current_is_type(parser, TOKEN_MUL)) {
            pti->ptr_depth++;
            parser_advance(parser);
        }

        int dim_cap = 4;
        pti->dim_sizes = malloc(sizeof(int) * (size_t)dim_cap);
        pti->dim_exprs  = NULL;
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode *expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (pti->dim_count >= dim_cap) {
                dim_cap *= 2;
                pti->dim_sizes = realloc(pti->dim_sizes,
                    sizeof(int) * (size_t)dim_cap);
            }
            pti->dim_sizes[pti->dim_count] = size;
            if (expr) {
                if (!pti->dim_exprs) {
                    pti->dim_exprs = calloc((size_t)dim_cap, sizeof(ASTNode*));
                }
                pti->dim_exprs[pti->dim_count] = expr;
            }
            pti->dim_count++;
        }
        return true;
    }

    return false;
}

static FuncSignature *parser_parse_bare_func_ptr(Parser *parser,
                                                  char **out_name,
                                                  MultiReturnType *outer_mrt) {
    if (out_name) *out_name = NULL;

    parser_eat(parser, TOKEN_PAREN_LEFT);

    int star_count = 0;
    while (parser_current_is_type(parser, TOKEN_MUL)) {
        star_count++;
        parser_advance(parser);
    }
    if (star_count == 0) {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected '*' in function pointer type");
        return NULL;
    }
    (void)star_count;

    char *name = NULL;
    if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
        name = strdup(parser->current.value);
        parser_advance(parser);
        if (out_name) *out_name = name;
    }

    parser_eat(parser, TOKEN_PAREN_RIGHT);

    parser_eat(parser, TOKEN_PAREN_LEFT);

    int param_cap = 4;
    int param_cnt = 0;
    DataType *param_types = malloc(sizeof(DataType) * (size_t)param_cap);
    bool *param_pointee_const_arr = malloc(sizeof(bool) * (size_t)param_cap);
    if (!param_types) OUT_OF_MEMORY();
    if (!param_pointee_const_arr) OUT_OF_MEMORY();

    bool has_varargs = false;
    int vararg_cap = 4;
    int vararg_cnt = 0;
    int *vararg_indices = malloc(sizeof(int) * (size_t)vararg_cap);
    if (!vararg_indices) OUT_OF_MEMORY();

    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF)) {

        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            if (param_cnt > 0) {
                has_varargs = true;
                if (vararg_cnt >= vararg_cap) {
                    vararg_cap *= 2;
                    vararg_indices = realloc(vararg_indices,
                        sizeof(int) * (size_t)vararg_cap);
                    if (!vararg_indices) OUT_OF_MEMORY();
                }
                vararg_indices[vararg_cnt++] = param_cnt;
            }
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }

        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            int saved2 = parser->index;
            int look2 = saved2 + 1;
            bool looks_like_multi = false;
            if (is_var_type(parser->tokens[look2].type)
                || parser->tokens[look2].type == TOKEN_IDENTIFIER) {
                int depth2 = 1;
                while (parser->tokens[look2].type != TOKEN_EOF && depth2 > 0) {
                    if (parser->tokens[look2].type == TOKEN_PAREN_LEFT) depth2++;
                    if (parser->tokens[look2].type == TOKEN_PAREN_RIGHT) { depth2--; if (depth2==0) break; }
                    if (parser->tokens[look2].type == TOKEN_COMMA) looks_like_multi = true;
                    if (parser->tokens[look2].type == TOKEN_VARARG) looks_like_multi = true;
                    look2++;
                }
                look2++;
                if (parser->tokens[look2].type == TOKEN_PAREN_LEFT
                    && parser->tokens[look2+1].type == TOKEN_MUL) {
                    looks_like_multi = true;
                }
            }
            if (looks_like_multi) {
                parser->index = saved2;
                parser->current = parser->tokens[parser->index];
                MultiReturnType *inner_mrt = parse_multi_return_type(parser);
                if (inner_mrt) {
                    char *inner_name = NULL;
                    FuncSignature *inner_sig = parser_parse_bare_func_ptr(
                        parser, &inner_name, inner_mrt);
                    if (inner_sig) {
                        inner_sig->multi_return = inner_mrt;
                        if (param_cnt >= param_cap) {
                            param_cap *= 2;
                            param_types = realloc(param_types,
                                sizeof(DataType) * (size_t)param_cap);
                            param_pointee_const_arr = realloc(param_pointee_const_arr,
                                sizeof(bool) * (size_t)param_cap);
                            if (!param_types) OUT_OF_MEMORY();
                            if (!param_pointee_const_arr) OUT_OF_MEMORY();
                        }
                        param_types[param_cnt] = DATA_INT;
                        param_pointee_const_arr[param_cnt] = false;
                        param_cnt++;
                        free(inner_name);
                        free(inner_sig->param_types);
                        free(inner_sig->param_pointee_const);
                        free(inner_sig->vararg_param_indices);
                        free(inner_sig);
                    }
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
                parser->index = saved2;
                parser->current = parser->tokens[parser->index];
            }
        }

        bool param_is_pointee_const = false;
        if (parser_current_is_type(parser, TOKEN_CONST)) {
            param_is_pointee_const = true;
            parser_advance(parser);
        }

        if (!is_var_type(parser->current.type)
            && parser->current.type != TOKEN_IDENTIFIER
            && parser->current.type != TOKEN_VOID) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected parameter type, got '%s'",
                  type_to_string(parser->current.type));
            break;
        }

        DataType pt = Get_Datatype(parser->current.type);
        parser_advance(parser);

        int star_count2 = 0;
        while (parser_current_is_type(parser, TOKEN_MUL)) {
            star_count2++;
            parser_advance(parser);
        }

        if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
            parser_advance(parser);

        if (star_count2 == 0) param_is_pointee_const = false;

        bool is_vararg = false;
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            is_vararg = true;
            parser_advance(parser);
        }

        if (param_cnt >= param_cap) {
            param_cap *= 2;
            param_types = realloc(param_types,
                sizeof(DataType) * (size_t)param_cap);
            param_pointee_const_arr = realloc(param_pointee_const_arr,
                sizeof(bool) * (size_t)param_cap);
            if (!param_types) OUT_OF_MEMORY();
            if (!param_pointee_const_arr) OUT_OF_MEMORY();
        }
        param_types[param_cnt] = pt;
        param_pointee_const_arr[param_cnt] = param_is_pointee_const;
        param_cnt++;

        if (is_vararg) {
            has_varargs = true;
            if (vararg_cnt >= vararg_cap) {
                vararg_cap *= 2;
                vararg_indices = realloc(vararg_indices,
                    sizeof(int) * (size_t)vararg_cap);
                if (!vararg_indices) OUT_OF_MEMORY();
            }
            vararg_indices[vararg_cnt++] = param_cnt;
        }

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                has_varargs = true;
                if (vararg_cnt >= vararg_cap) {
                    vararg_cap *= 2;
                    vararg_indices = realloc(vararg_indices,
                        sizeof(int) * (size_t)vararg_cap);
                    if (!vararg_indices) OUT_OF_MEMORY();
                }
                vararg_indices[vararg_cnt++] = param_cnt;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    FuncSignature *sig = calloc(1, sizeof(FuncSignature));
    if (!sig) OUT_OF_MEMORY();
    sig->return_type = DATA_VOID;
    sig->param_count = param_cnt;
    sig->param_types = param_types;
    sig->param_pointee_const = param_pointee_const_arr;
    sig->has_varargs = has_varargs;
    sig->vararg_param_count = vararg_cnt;
    sig->vararg_param_indices = (vararg_cnt > 0) ? vararg_indices : NULL;
    if (vararg_cnt == 0) free(vararg_indices);
    sig->multi_return = outer_mrt;

    return sig;
}

static ASTNode* parser_multi_return_func_ptr_var(Parser* parser) {
    MultiReturnType* mrt = NULL;

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved = parser->index;
        bool has_multi = false;
        {
            int look = saved + 1;
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) {
                int depth = 1;
                look++;
                while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                    if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                    if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                        depth--;
                        if (depth == 0) break;
                    }
                    if (depth == 1 && (parser->tokens[look].type == TOKEN_COMMA
                                       || parser->tokens[look].type == TOKEN_VARARG))
                        has_multi = true;
                    look++;
                }
            } else if (is_var_type(parser->tokens[look].type)
                       || parser->tokens[look].type == TOKEN_IDENTIFIER) {
                int depth = 1;
                while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                    if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                    if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                        depth--;
                        if (depth == 0) break;
                    }
                    if (parser->tokens[look].type == TOKEN_COMMA
                        || parser->tokens[look].type == TOKEN_VARARG)
                        has_multi = true;
                    look++;
                }
            }
        }

        if (has_multi) {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
            mrt = parse_multi_return_type(parser);
            if (!mrt) return NULL;
        } else {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];
        }
    }

    int total_ptr_depth = 0;
    int final_dim_count = 0;
    int* final_dim_sizes = NULL;
    char* final_name = NULL;
    Token final_name_token = {0};
    FuncSignature* outermost_sig = NULL;
    MultiReturnType* current_mrt = mrt;
    bool has_any_layer = false;

    while (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        if (parser->tokens[parser->index + 1].type != TOKEN_MUL) {
            int saved = parser->index;
            Token saved_cur = parser->current;

            parser_eat(parser, TOKEN_PAREN_LEFT);
            int saved2 = parser->index;
            int look = saved2;
            int depth = 1;
            bool has_multi = false;
            while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
                if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
                if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
                    depth--;
                    if (depth == 0) break;
                }
                if (depth == 1 && parser->tokens[look].type == TOKEN_COMMA)
                    has_multi = true;
                if (depth == 1 && parser->tokens[look].type == TOKEN_VARARG)
                    has_multi = true;
                look++;
            }
            look++;
            bool is_fptr = (parser->tokens[look].type == TOKEN_PAREN_LEFT
                            && parser->tokens[look + 1].type == TOKEN_MUL);

            if (is_fptr) {
                parser->index = saved;
                parser->current = saved_cur;
                FuncSignature* fsig = parse_func_ptr_type_signature(parser);
                if (fsig) {
                    if (current_mrt) {
                        free(current_mrt->types);
                        free(current_mrt->is_variadic);
                        if (current_mrt->func_sigs) {
                            for (int i = 0; i < current_mrt->count; i++)
                                if (current_mrt->func_sigs[i]) {
                                    free(current_mrt->func_sigs[i]->param_types);
                                    free(current_mrt->func_sigs[i]);
                                }
                            free(current_mrt->func_sigs);
                        }
                        free(current_mrt);
                    }
                    current_mrt = calloc(1, sizeof(MultiReturnType));
                    if (!current_mrt) OUT_OF_MEMORY();
                    current_mrt->count = 1;
                    current_mrt->types = malloc(sizeof(DataType));
                    current_mrt->types[0] = DATA_INT;
                    current_mrt->is_variadic = malloc(sizeof(bool));
                    current_mrt->is_variadic[0] = false;
                    current_mrt->func_sigs = malloc(sizeof(FuncSignature*));
                    current_mrt->func_sigs[0] = fsig;
                    has_any_layer = true;
                    continue;
                }
            } else if (has_multi) {
                MultiReturnType* mrt = parse_multi_return_type(parser);
                if (mrt != NULL) {
                    if (current_mrt) {
                        free(current_mrt->types);
                        free(current_mrt->is_variadic);
                        if (current_mrt->func_sigs) {
                            for (int i = 0; i < current_mrt->count; i++)
                                if (current_mrt->func_sigs[i]) {
                                    free(current_mrt->func_sigs[i]->param_types);
                                    free(current_mrt->func_sigs[i]);
                                }
                            free(current_mrt->func_sigs);
                        }
                        free(current_mrt);
                    }
                    current_mrt = mrt;
                    has_any_layer = true;
                    continue;
                }
            }

            parser->index = saved;
            parser->current = saved_cur;
            break;
        }

        parser_eat(parser, TOKEN_PAREN_LEFT);
        has_any_layer = true;

        int stars = 0;
        while (parser_current_is_type(parser, TOKEN_MUL)) {
            stars++;
            parser_advance(parser);
        }

        char* layer_name = NULL;
        Token layer_name_token = {0};
        if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            layer_name = strdup(parser->current.value);
            layer_name_token = parser->current;
            parser_advance(parser);
        }

        /* array of function pointers: "(*name[expr])(params)" */
        int layer_dim_count = 0;
        int layer_dim_cap = 4;
        int* layer_dim_sizes = malloc(sizeof(int) * (size_t)layer_dim_cap);
        if (!layer_dim_sizes) OUT_OF_MEMORY();
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int layer_dim_size = -1;
            if (parser_current_is_type(parser, TOKEN_NUMBER)) {
                layer_dim_size = atoi(parser->current.value);
                parser_advance(parser);
            } else if (!parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
                ASTNode* layer_dim_expr = parser_Binary(parser);
                if (layer_dim_expr) AST_Free(layer_dim_expr);
                layer_dim_size = -2;
            }
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (layer_dim_count >= layer_dim_cap) {
                layer_dim_cap *= 2;
                layer_dim_sizes = realloc(layer_dim_sizes,
                    sizeof(int) * (size_t)layer_dim_cap);
                if (!layer_dim_sizes) OUT_OF_MEMORY();
            }
            layer_dim_sizes[layer_dim_count++] = layer_dim_size;
        }

        parser_eat(parser, TOKEN_PAREN_RIGHT);
        parser_eat(parser, TOKEN_PAREN_LEFT);

        int pcnt = 0, pcap = 4;
        DataType* ptypes = malloc(sizeof(DataType) * (size_t)pcap);
        bool* ppointer_const = malloc(sizeof(bool) * (size_t)pcap);
        FuncSignature** psigs = calloc((size_t)pcap, sizeof(FuncSignature*));
        if (!ptypes) OUT_OF_MEMORY();
        if (!ppointer_const) OUT_OF_MEMORY();
        if (!psigs) OUT_OF_MEMORY();
        bool has_va = false;
        int vacnt = 0, vacap = 4;
        int* vai = malloc(sizeof(int) * (size_t)vacap);

        while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
               && !parser_current_is_type(parser, TOKEN_EOF)) {
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                if (pcnt > 0) {
                    has_va = true;
                    if (vacnt >= vacap) {
                        vacap *= 2;
                        vai = realloc(vai, sizeof(int) * (size_t)vacap);
                    }
                    vai[vacnt++] = pcnt - 1;
                }
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
                continue;
            }

            if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
                FuncSignature *inner_sig = parse_func_ptr_type_signature(parser);
                if (inner_sig) {
                   if (pcnt >= pcap) {
                       int old_pcap = pcap;
                       pcap *= 2;
                       ptypes = realloc(ptypes, sizeof(DataType) * (size_t)pcap);
                       ppointer_const = realloc(ppointer_const, sizeof(bool) * (size_t)pcap);
                       psigs = realloc(psigs, sizeof(FuncSignature*) * (size_t)pcap);
                       if (!ptypes) OUT_OF_MEMORY();
                       if (!ppointer_const) OUT_OF_MEMORY();
                       if (!psigs) OUT_OF_MEMORY();
                       memset(psigs + old_pcap, 0,
                              sizeof(FuncSignature*) * (size_t)(pcap - old_pcap));
                   }
                   ptypes[pcnt] = DATA_INT;
                    ppointer_const[pcnt] = false;
                    psigs[pcnt] = NULL;
                    pcnt++;
                    free(inner_sig->param_types);
                    free(inner_sig->param_pointee_const);
                    free(inner_sig->vararg_param_indices);
                    if (inner_sig->multi_return) {
                        MultiReturnType *mrt = inner_sig->multi_return;
                        free(mrt->types);
                        free(mrt->is_variadic);
                        if (mrt->func_sigs) {
                            for (int i = 0; i < mrt->count; i++) {
                                if (mrt->func_sigs[i]) {
                                    free(mrt->func_sigs[i]->param_types);
                                    free(mrt->func_sigs[i]->param_pointee_const);
                                    free(mrt->func_sigs[i]->vararg_param_indices);
                                    free(mrt->func_sigs[i]);
                                }
                            }
                            free(mrt->func_sigs);
                        }
                        free(mrt);
                    }
                    free(inner_sig);
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
            }

            {
                DataType nested_base = DATA_ERROR;
                FuncSignature* nested_sig = NULL;
                if (parser_try_nested_func_ptr_param(parser, &nested_base, &nested_sig)) {
                   if (pcnt >= pcap) {
                       int old_pcap = pcap;
                       pcap *= 2;
                       ptypes = realloc(ptypes, sizeof(DataType) * (size_t)pcap);
                       ppointer_const = realloc(ppointer_const, sizeof(bool) * (size_t)pcap);
                       psigs = realloc(psigs, sizeof(FuncSignature*) * (size_t)pcap);
                       if (!ptypes) OUT_OF_MEMORY();
                       if (!ppointer_const) OUT_OF_MEMORY();
                       if (!psigs) OUT_OF_MEMORY();
                       memset(psigs + old_pcap, 0,
                              sizeof(FuncSignature*) * (size_t)(pcap - old_pcap));
                   }
                   ptypes[pcnt] = nested_base;
                    ppointer_const[pcnt] = false;
                    psigs[pcnt] = nested_sig;
                    pcnt++;
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
            }

            bool param_pointee_const_val = false;
            if (parser_current_is_type(parser, TOKEN_CONST)) {
                param_pointee_const_val = true;
                parser_advance(parser);
            }

            if (!is_var_type(parser->current.type)
                && parser->current.type != TOKEN_IDENTIFIER
                && parser->current.type != TOKEN_VOID) {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "expected parameter type");
                free(ptypes); free(ppointer_const); free(vai); free(layer_name);
                goto error;
            }

            DataType pt = Get_Datatype(parser->current.type);
            parser_advance(parser);

            int star_count = 0;
            while (parser_current_is_type(parser, TOKEN_MUL)) {
                star_count++;
                parser_advance(parser);
            }

            if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                parser_advance(parser);

            if (star_count == 0) param_pointee_const_val = false;

           if (pcnt >= pcap) {
               int old_pcap = pcap;
               pcap *= 2;
               ptypes = realloc(ptypes, sizeof(DataType) * (size_t)pcap);
               ppointer_const = realloc(ppointer_const, sizeof(bool) * (size_t)pcap);
               psigs = realloc(psigs, sizeof(FuncSignature*) * (size_t)pcap);
               if (!ptypes) OUT_OF_MEMORY();
               if (!ppointer_const) OUT_OF_MEMORY();
               if (!psigs) OUT_OF_MEMORY();
               memset(psigs + old_pcap, 0,
                      sizeof(FuncSignature*) * (size_t)(pcap - old_pcap));
           }
           ptypes[pcnt] = pt;
            ppointer_const[pcnt] = param_pointee_const_val;
            psigs[pcnt] = NULL;
            pcnt++;

            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            else
                break;
        }
        parser_eat(parser, TOKEN_PAREN_RIGHT);

        FuncSignature* layer_sig = calloc(1, sizeof(FuncSignature));
        layer_sig->return_type = DATA_VOID;
        layer_sig->param_count = pcnt;
        layer_sig->param_types = ptypes;
        layer_sig->param_pointee_const = ppointer_const;
        layer_sig->param_func_sigs = psigs;
        layer_sig->has_varargs = has_va;
        layer_sig->vararg_param_count = vacnt;
        layer_sig->vararg_param_indices = (vacnt > 0) ? vai : NULL;
        if (vacnt == 0) free(vai);
        layer_sig->multi_return = current_mrt;
        current_mrt = NULL;

        total_ptr_depth += stars;

        if (outermost_sig) {
            free(outermost_sig->param_types);
            free(outermost_sig->vararg_param_indices);
            free(outermost_sig);
        }
        outermost_sig = layer_sig;

        if (layer_name) {
            if (final_name) free(final_name);
            final_name = layer_name;
            final_name_token = layer_name_token;
            if (layer_dim_count > 0) {
                free(final_dim_sizes);
                final_dim_sizes = layer_dim_sizes;
                final_dim_count = layer_dim_count;
                layer_dim_sizes = NULL;
                layer_dim_count = 0;
            }
        }
        free(layer_dim_sizes);
    }

    if (!has_any_layer) {
        if (current_mrt) {
            free(current_mrt->types);
            free(current_mrt->is_variadic);
            free(current_mrt->func_sigs);
            free(current_mrt);
        }
        return NULL;
    }

    if (!final_name && has_any_layer
        && parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
        final_name = strdup(parser->current.value);
        final_name_token = parser->current;
        parser_advance(parser);
        }
    if (!final_name) {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected variable name in function pointer declaration");
        goto error;
    }

    ASTNode* init = NULL;
    if (parser_current_is_type(parser, TOKEN_ASSIGN)) {
        parser_eat(parser, TOKEN_ASSIGN);
        if (parser_current_is_type(parser, TOKEN_BRACKET_LEFT)) {
            init = parser_init_list(parser);
        } else {
            init = parser_Binary(parser);
        }
    }

    AST_Var* var = calloc(1, sizeof(AST_Var));
    var->base.type = (init != NULL) ? AST_VAR : AST_UNINIT_VAR;
    var->base.next = NULL;
    var->base.value = final_name_token;
    var->type = TOKEN_IDENTIFIER;
    var->name = final_name_token;
    var->Value = init;
    var->dataType = DATA_INT;
    var->internal_name = NULL;
    var->dim_count = final_dim_count;
    var->dim_sizes = final_dim_sizes;
    var->modifier = 0;
    var->ptr_depth = total_ptr_depth;
    var->func_ptr_sig = outermost_sig;
    var->type_param_index = -1;
    if (final_dim_count > 0) {
        int fp_total_bytes = 4;
        for (int fdi = 0; fdi < final_dim_count; fdi++) {
            if (final_dim_sizes[fdi] > 0) fp_total_bytes *= final_dim_sizes[fdi];
        }
        var->total_bytes = fp_total_bytes;
    }

    free(final_name);
    return (ASTNode*)var;

error:
    if (final_name) free(final_name);
    if (outermost_sig) {
        free(outermost_sig->param_types);
        free(outermost_sig->vararg_param_indices);
        free(outermost_sig);
    }
    if (current_mrt) {
        free(current_mrt->types);
        free(current_mrt->is_variadic);
        free(current_mrt->func_sigs);
        free(current_mrt);
    }
    return NULL;
}

ASTNode* parser_def_var(Parser* parser)
{
    Token name_token = parser->current;
    parser_advance(parser);

    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
        int idx_cap = 4, idx_cnt = 0;
        ASTNode** indices = malloc(sizeof(ASTNode*) * idx_cap);
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            ASTNode* idx = parser_Binary(parser);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (idx_cnt >= idx_cap) {
                idx_cap *= 2;
                indices = realloc(indices, sizeof(ASTNode*) * idx_cap);
            }
            indices[idx_cnt++] = idx;
        }
        parser_eat(parser, TOKEN_ASSIGN);
        ASTNode* value = parser_Binary(parser);
        AST_ArrayAccess* acc = calloc(1,sizeof(AST_ArrayAccess));
        acc->base.type = AST_ARRAY_ACCESS;
        acc->base.value = name_token;
        acc->array = parser_create_literal(name_token);
        acc->indices = indices;
        acc->index_count = idx_cnt;
        acc->elementType = DATA_ERROR;
        acc->dim_count = 0;
        acc->dim_sizes = NULL;
        acc->full_dim_sizes = NULL;
        acc->full_dim_count = 0;
        AST_ArrayAss* ass = calloc(1, sizeof(AST_ArrayAss));
        ass->base.type = AST_ARRAY_ASS;
        ass->base.value = name_token;
        ass->target = (ASTNode*)acc;
        ass->value = value;
        ass->elementType = DATA_ERROR;
        ass->base.next = NULL;
        acc->base.next = NULL;
        return (ASTNode*)ass;
    }

    AST_Var* ast_var = calloc(1, sizeof(AST_Var));
    if (ast_var == NULL)
    {
        OUT_OF_MEMORY();
    }

    ast_var->base.next = NULL;
    ast_var->base.type = AST_VAR_ASS;
    ast_var->base.value = name_token;

    ast_var->name = name_token;
    parser_eat(parser, TOKEN_ASSIGN);
    ast_var->Value = parser_Binary(parser);
    ast_var->type = ast_var->Value->value.type;
    ast_var->dataType = DATA_ERROR;
    ast_var->internal_name = NULL;
    ast_var->type_param_index = -1;

    return (ASTNode*)ast_var;
}

ASTNode* parser_var(Parser* parser, bool allow_multi_decl)
{
    bool is_unsafe = false;
    bool is_const = false;
    bool is_pointer_const = false;
    while (parser_current_is_type(parser, TOKEN_CONST) ||
           parser_current_is_type(parser, TOKEN_UNSAFE)) {
        if (parser_current_is_type(parser, TOKEN_CONST)) {
            if (is_const) {
                ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "duplicate 'const' modifier");
            }
            is_const = true;
        } else {
            if (is_unsafe) {
                ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "duplicate 'unsafe' modifier");
            }
            is_unsafe = true;
        }
        parser_advance(parser);
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int mr_saved = parser->index;
        ASTNode* mr_node = parser_multi_return_func_ptr_var(parser);
        if (mr_node) {
            ASTNode* mrn = mr_node;
            while (mrn) {
                if (mrn->type == AST_VAR || mrn->type == AST_UNINIT_VAR) {
                    ((AST_Var*)mrn)->is_unsafe = is_unsafe;
                    ((AST_Var*)mrn)->is_const = is_const;
                }
                mrn = mrn->next;
            }
            return mr_node;
        }
        parser->index = mr_saved;
        parser->current = parser->tokens[parser->index];
    }

    ParsedTypeInfo pti;

    bool has_complex = parser_parse_complex_type(parser, &pti);

    while (has_complex && pti.func_sig != NULL
           && parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved = parser->index;
        int look = saved + 1;
        if (parser->tokens[look].type == TOKEN_MUL) {
            parser->index = saved;
            parser->current = parser->tokens[parser->index];

            char *outer_name = NULL;
            MultiReturnType *wrap_mrt = NULL;
            if (pti.multi_return) {
                wrap_mrt = pti.multi_return;
                pti.multi_return = NULL;
            } else if (pti.func_sig->multi_return) {
                wrap_mrt = pti.func_sig->multi_return;
                pti.func_sig->multi_return = NULL;
            }

            FuncSignature *outer_sig = parser_parse_bare_func_ptr(
                parser, &outer_name, wrap_mrt);
            if (outer_sig) {
                if (pti.func_sig) {
                    free(pti.func_sig->param_types);
                    free(pti.func_sig->vararg_param_indices);
                    free(pti.func_sig);
                }
                pti.func_sig = outer_sig;
                pti.base_type = DATA_INT;
                pti.is_multi_return = (wrap_mrt != NULL);
                pti.multi_return = wrap_mrt;
                free(outer_name);
                continue;
            }
        }
        parser->index = saved;
        parser->current = parser->tokens[parser->index];
        break;
    }

    if (!has_complex) {
        Token type_token = parser->current;
        if (!is_var_type(type_token.type) && type_token.type != TOKEN_IDENTIFIER) {
            ERROR(ERR_PARSE_INVALID_TYPE,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected type keyword, got '%s'",
                  type_to_string(type_token.type));
            return NULL;
        }
        parser_advance(parser);
        pti.base_type = Get_Datatype(type_token.type);
        if (pti.base_type == DATA_IDENTIFIER) {
            pti.type_name = strdup(type_token.value);
        } else {
            pti.type_name = NULL;
        }
        while (parser_current_is_type(parser, TOKEN_MUL)) {
            pti.ptr_depth++;
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_CONST)) {
                ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "mid-level pointer 'const' is not yet supported");
            }
        }
        if (parser_current_is_type(parser, TOKEN_CONST) && pti.ptr_depth > 0) {
            is_pointer_const = true;
            parser_advance(parser);
        }
        int dim_cap = 4;
        pti.dim_sizes = malloc(sizeof(int) * (size_t)dim_cap);
        pti.dim_exprs  = NULL;
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode *expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (pti.dim_count >= dim_cap) {
                dim_cap *= 2;
                pti.dim_sizes = realloc(pti.dim_sizes,
                    sizeof(int) * (size_t)dim_cap);
            }
            pti.dim_sizes[pti.dim_count] = size;
            if (expr) {
                if (!pti.dim_exprs) {
                    pti.dim_exprs = calloc((size_t)dim_cap, sizeof(ASTNode*));
                }
                pti.dim_exprs[pti.dim_count] = expr;
            }
            pti.dim_count++;
        }
        pti.base_type = Get_Datatype(type_token.type);
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        DataType saved_return_type = pti.base_type;
        ASTNode* node = parser_multi_return_func_ptr_var(parser);
        if (node) {
            if (node->type == AST_VAR || node->type == AST_UNINIT_VAR) {
                AST_Var* var = (AST_Var*)node;
                if (var->func_ptr_sig) {
                    var->func_ptr_sig->return_type = saved_return_type;
                }
            }
            return node;
        }
    }

    ASTNode *head = NULL;
    ASTNode *tail = NULL;

    do {
        char *var_name_str = NULL;
        Token var_name_token;

        if (pti.func_sig && !parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected variable name after type");
            parsed_type_info_free(&pti);
            return NULL;
        }

        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected variable name");
            parsed_type_info_free(&pti);
            AST_Free(head);
            return NULL;
        }

        var_name_token = parser->current;
        var_name_str = strdup(var_name_token.value);
        parser_advance(parser);

        int var_dim_count = 0;
        int var_dim_cap = 4;
        int *var_dim_sizes = malloc(sizeof(int) * (size_t)var_dim_cap);
        ASTNode **var_dim_exprs = NULL;
        int var_dim_expr_cap = 0;

        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode *expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (var_dim_count >= var_dim_cap) {
                var_dim_cap *= 2;
                var_dim_sizes = realloc(var_dim_sizes, sizeof(int) * (size_t)var_dim_cap);
            }
            if (var_dim_count >= var_dim_expr_cap) {
                var_dim_expr_cap = (var_dim_expr_cap == 0) ? 4 : var_dim_expr_cap * 2;
                var_dim_exprs = realloc(var_dim_exprs,
                    sizeof(ASTNode*) * (size_t)var_dim_expr_cap);
            }
            var_dim_sizes[var_dim_count] = size;
            var_dim_exprs[var_dim_count] = expr;
            var_dim_count++;
        }

        int total_dim_count = pti.dim_count + var_dim_count;
        int *total_dim_sizes = NULL;
        ASTNode **total_dim_exprs = NULL;
        if (total_dim_count > 0) {
            total_dim_sizes = malloc(sizeof(int) * (size_t)total_dim_count);
            if (pti.dim_sizes)
                memcpy(total_dim_sizes, pti.dim_sizes, sizeof(int) * (size_t)pti.dim_count);
            memcpy(total_dim_sizes + pti.dim_count, var_dim_sizes,
                   sizeof(int) * (size_t)var_dim_count);

            if (pti.dim_exprs || var_dim_exprs) {
                total_dim_exprs = malloc(sizeof(ASTNode*) * (size_t)total_dim_count);
                for (int i = 0; i < pti.dim_count; i++)
                    total_dim_exprs[i] = pti.dim_exprs ? pti.dim_exprs[i] : NULL;
                for (int i = 0; i < var_dim_count; i++)
                    total_dim_exprs[pti.dim_count + i] = var_dim_exprs ? var_dim_exprs[i] : NULL;
            }
            free(var_dim_exprs);
            if (pti.dim_exprs) { free(pti.dim_exprs); pti.dim_exprs = NULL; }
        }
        free(var_dim_sizes);

        ASTNode *init = NULL;
        if (parser_current_is_type(parser, TOKEN_ASSIGN)) {
            parser_eat(parser, TOKEN_ASSIGN);
            if (parser_current_is_type(parser, TOKEN_BRACKET_LEFT))
                init = parser_init_list(parser);
            else
                init = parser_Binary(parser);
        }

        ASTNode *node = NULL;

        if (total_dim_count > 0) {
            AST_ArrayDecl *arr = calloc(1, sizeof(AST_ArrayDecl));
            if (!arr) OUT_OF_MEMORY();
            arr->base.type = AST_ARRAY_DECL;
            arr->base.next = NULL;
            arr->base.value = var_name_token;
            arr->elementType = pti.base_type;
            if (pti.base_type == DATA_IDENTIFIER && pti.type_name != NULL) {
                arr->element_class_name = strdup(pti.type_name);
            } else {
                arr->element_class_name = NULL;
            }
            arr->name = var_name_str;
            arr->dim_sizes = total_dim_sizes;
            arr->dim_count = total_dim_count;
            arr->internal_name = NULL;
            arr->init_list = init;
            arr->modifier = 0;
            arr->dim_exprs = total_dim_exprs;
            arr->is_unsafe = is_unsafe;
            arr->is_const = is_const;
            node = (ASTNode*)arr;
        } else {
            free(total_dim_sizes);

            AST_Var *var = calloc(1, sizeof(AST_Var));
            if (!var) OUT_OF_MEMORY();
            var->base.type = (init != NULL) ? AST_VAR : AST_UNINIT_VAR;
            var->base.next = NULL;
            var->base.value = var_name_token;
            var->type = TOKEN_IDENTIFIER;
            var->name = var_name_token;
            var->Value = init;
            var->dataType = pti.base_type;
            var->internal_name = NULL;
            var->dim_count = 0;
            var->dim_sizes = NULL;
            var->modifier = 0;
            var->ptr_depth = pti.ptr_depth;
            var->is_unsafe = is_unsafe;
            var->level_const_mask = 0;
            if (is_const && pti.ptr_depth > 0) {
                var->level_const_mask = 1u << (pti.ptr_depth - 1);
                var->is_const = false;
            } else {
                var->is_const = is_const;
            }
            if (is_pointer_const) {
                var->is_const = true;
            }
            if (pti.generic_type_arg_count > 0) {
                var->generic_type_args = pti.generic_type_args;
                var->generic_type_arg_names = pti.generic_type_arg_names;
                var->generic_type_arg_count = pti.generic_type_arg_count;
                pti.generic_type_args = NULL;
                pti.generic_type_arg_names = NULL;
                pti.generic_type_arg_count = 0;
            }
            var->func_ptr_sig = pti.func_sig;
            pti.func_sig = NULL;
            var->ptr_array_info = pti.ptr_array_info;
            pti.ptr_array_info = NULL;
            var->type_param_index = -1;

            if (pti.base_type == DATA_IDENTIFIER && pti.type_name != NULL) {
                var->internal_name = strdup(pti.type_name);
            }
            node = (ASTNode*)var;
        }

        if (!head) {
            head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }

        if (!allow_multi_decl)
            break;
        if (!parser_current_is_type(parser, TOKEN_COMMA))
            break;
        parser_advance(parser);

    } while (true);

    parsed_type_info_free(&pti);
    return head;
}

ASTNode* parser_Print(Parser* parser)
{
    AST_Print* ast_print = calloc(1,sizeof(AST_Print));
    ast_print->base.next = NULL;
    ast_print->base.type = AST_PRINT;
    ast_print->base.value = parser->current;
    parser_eat(parser, TOKEN_PRINT);

    ASTNode* head_Parament = parser_Binary(parser);
    ASTNode* tail_Param = head_Parament;
    while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
           !parser_current_is_type(parser, TOKEN_EOF)) {
        if (!parser_current_is_type(parser, TOKEN_COMMA)) {
            break;
        }

        parser_eat(parser, TOKEN_COMMA);
        ASTNode* nextParam = parser_Binary(parser);

        if (tail_Param != NULL) {
            tail_Param->next = nextParam;
            tail_Param = nextParam;
        } else {
            head_Parament = tail_Param = nextParam;
        }
    }
    ast_print->Parameter = head_Parament;
    return (ASTNode*)ast_print;
}

ASTNode* parser_input(Parser* parser) {
    parser_eat(parser, TOKEN_INPUT);
    parser_eat(parser, TOKEN_PAREN_LEFT);

    ASTNode** args = NULL;
    int arg_count = 0, arg_cap = 0;

    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
           && !parser_current_is_type(parser, TOKEN_EOF)) {

        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected variable name");
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_COMMA)
                   && !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }

        AST_Literal* var = calloc(1, sizeof(AST_Literal));
        if (!var) OUT_OF_MEMORY();
        var->base.type  = AST_LITERAL;
        var->base.next  = NULL;
        var->base.value = parser->current;
        var->base.value.value = strdup(parser->current.value);
        var->dataType         = DATA_IDENTIFIER;
        var->is_call_function = false;
        parser_advance(parser);

        if (arg_count >= arg_cap) {
            arg_cap = arg_cap ? arg_cap * 2 : 4;
            args = realloc(args, sizeof(ASTNode*) * (size_t)arg_cap);
            if (!args) OUT_OF_MEMORY();
        }
        args[arg_count++] = (ASTNode*)var;

        if (!parser_current_is_type(parser, TOKEN_COMMA)) break;
        parser_advance(parser);
    }

    parser_eat(parser, TOKEN_PAREN_RIGHT);

    size_t fmt_len = arg_count == 0 ? 0 : (size_t)arg_count * 3 - 1;
    char* fmt_str = malloc(fmt_len + 1);
    if (!fmt_str) OUT_OF_MEMORY();
    size_t pos = 0;
    for (int i = 0; i < arg_count; i++) {
        fmt_str[pos++] = '{';
        fmt_str[pos++] = '}';
        if (i < arg_count - 1) {
            fmt_str[pos++] = ' ';
        }
    }
    fmt_str[pos] = '\0';

    AST_Literal* fmt_node = calloc(1, sizeof(AST_Literal));
    if (!fmt_node) OUT_OF_MEMORY();
    fmt_node->base.type        = AST_LITERAL;
    fmt_node->base.next        = NULL;
    fmt_node->base.value.type  = TOKEN_STRING_LITERAL;
    fmt_node->base.value.value = fmt_str;
    fmt_node->base.value.line  = parser->current.line;
    fmt_node->base.value.column = parser->current.column;
    fmt_node->dataType         = DATA_STRING;
    fmt_node->is_call_function = false;

    AST_Input* input = calloc(1, sizeof(AST_Input));
    if (!input) OUT_OF_MEMORY();
    input->base.type     = AST_INPUT;
    input->base.next     = NULL;
    input->format_string = (ASTNode*)fmt_node;
    input->args          = args;
    input->arg_count     = arg_count;

    return (ASTNode*)input;
}

int* parse_dim_sizes(Parser* parser, int* count, DataType elemType) {
    int capacity = 4;
    int* sizes = malloc(sizeof(int) * capacity);
    *count = 0;
    while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
        parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
        int size = -1;
        if (parser_current_is_type(parser, TOKEN_NUMBER)) {
            size = atoi(parser->current.value);
            parser_eat(parser, TOKEN_NUMBER);
        } else if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
            size = -1;
        } else {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected array size or ']'");
            free(sizes);
            return NULL;
        }
        parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
        if (*count >= capacity) {
            capacity *= 2;
            sizes = realloc(sizes, sizeof(int) * capacity);
        }
        sizes[(*count)++] = size;
    }
    return sizes;
}

ASTNode* parser_init_list(Parser* parser) {
    parser_eat(parser, TOKEN_BRACKET_LEFT);

    ASTNode** elems = NULL;
    int count = 0;
    int capacity = 0;

    if (!parser_current_is_type(parser, TOKEN_BRACKET_RIGHT)) {
        do {
            ASTNode* elem;
            if (parser_current_is_type(parser, TOKEN_BRACKET_LEFT))
                elem = parser_init_list(parser);
            else
                elem = parser_Binary(parser);

            if (count >= capacity) {
                capacity = (capacity == 0) ? 4 : capacity * 2;
                elems = realloc(elems, sizeof(ASTNode*) * capacity);
            }
            elems[count++] = elem;

        } while (parser_current_is_type(parser, TOKEN_COMMA) &&
                 (parser_advance(parser), 1));
    }

    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    AST_InitList* list = calloc(1, sizeof(AST_InitList));
    list->base.type = AST_INIT_LIST;
    list->elements = elems;
    list->count = count;
    return (ASTNode*)list;
}

ASTNode* parser_vararg_group(Parser* parser) {
    Token left_bracket = parser->current;
    parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);

    ASTNode* head = NULL;
    ASTNode* tail = NULL;
    int count = 0;

    if (!parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
        do {
            ASTNode* elem = parser_Binary(parser);
            if (!elem) break;
            count++;
            if (!head) { head = tail = elem; }
            else { tail->next = elem; tail = elem; tail->next = NULL; }
        } while (parser_current_is_type(parser, TOKEN_COMMA) &&
                 (parser_advance(parser), 1));
    }

    parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);

    AST_VarargGroup* group = calloc(1, sizeof(AST_VarargGroup));
    if (!group) OUT_OF_MEMORY();
    group->base.type = AST_VARARG_GROUP;
    group->base.next = NULL;
    group->base.value = left_bracket;
    group->elements = head;
    group->count = count;
    group->target_elem_type = DATA_ERROR;
    return (ASTNode*)group;
}

static bool looks_like_generic_call(Parser* parser) {
    if (!parser_current_is_type(parser, TOKEN_LT))
        return false;

    int depth = 1;
    int look = parser->index + 1;
    int max_look = parser->index + 200;

    while (depth > 0 && look < max_look) {
        TokenType t = parser->tokens[look].type;

        if (t == TOKEN_SEMICOLON || t == TOKEN_BRACKET_RIGHT ||
            t == TOKEN_BRACKET_LEFT || t == TOKEN_EOF)
            return false;

        if (depth == 1 && (t == TOKEN_ADD || t == TOKEN_SUB ||
                           t == TOKEN_MUL || t == TOKEN_DIV ||
                           t == TOKEN_EQ || t == TOKEN_NE ||
                           t == TOKEN_LE || t == TOKEN_GE ||
                           t == TOKEN_LOGICAL_AND || t == TOKEN_LOGICAL_OR ||
                           t == TOKEN_DOT || t == TOKEN_SQUARE_BRACKET_LEFT))
            return false;

        if (t == TOKEN_LT) depth++;
        if (t == TOKEN_GT) depth--;
        look++;
    }

    if (depth != 0 || look >= max_look)
        return false;

    int after = look;
    while (parser->tokens[after].type == TOKEN_MUL)
        after++;
    if (parser->tokens[after].type == TOKEN_IDENTIFIER)
        after++;
    if (parser->tokens[after].type == TOKEN_PAREN_RIGHT)
        after++;

    return (parser->tokens[after].type == TOKEN_PAREN_LEFT);
}

ASTNode* parser_Postfix(Parser* parser, ASTNode* left) {
    while (1) {
        if (parser_current_is_type(parser, TOKEN_LT)
            && left->type == AST_LITERAL) {
            AST_Literal* lit = (AST_Literal*)left;

            if (lit->base.value.type != TOKEN_IDENTIFIER) {
                break;
            }

            if (!looks_like_generic_call(parser)) {
                break;
            }

            int saved = parser->index;
            Token saved_cur = parser->current;
            parser_advance(parser);

            DataType* targs = NULL;
            char** targ_names = NULL;
            int tcnt = 0, tcap = 0;
            bool ok = true;

            while (!parser_current_is_type(parser, TOKEN_GT)
                   && !parser_current_is_type(parser, TOKEN_EOF)) {
                if (!is_var_type(parser->current.type)
                    && parser->current.type != TOKEN_IDENTIFIER) {
                    ok = false; break;
                }
                DataType dt = Get_Datatype(parser->current.type);
                char* nm = (parser->current.type == TOKEN_IDENTIFIER)
                            ? strdup(parser->current.value) : NULL;
                parser_advance(parser);
                if (tcnt >= tcap) {
                    tcap = tcap ? tcap * 2 : 4;
                    targs = realloc(targs, sizeof(DataType) * (size_t)tcap);
                    targ_names = realloc(targ_names, sizeof(char*) * (size_t)tcap);
                }
                targs[tcnt] = dt;
                targ_names[tcnt] = nm;
                tcnt++;
                if (parser_current_is_type(parser, TOKEN_COMMA))
                    parser_advance(parser);
                else break;
            }

            if (ok && parser_current_is_type(parser, TOKEN_GT)) {
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
                    lit->generic_type_args = targs;
                    lit->generic_type_arg_count = tcnt;
                    lit->generic_type_arg_names = targ_names;
                    lit->is_generic_instantiation = true;
                    continue;
                }
            }

            parser->index = saved;
            parser->current = saved_cur;
            if (targs) free(targs);
            if (targ_names) {
                for (int i = 0; i < tcnt; i++) free(targ_names[i]);
                free(targ_names);
            }
            break;
        } else if (parser_current_is_type(parser, TOKEN_ARROW)) {
            parser_advance(parser);

            AST_Deref* deref = calloc(1, sizeof(AST_Deref));
            if (!deref) OUT_OF_MEMORY();
            deref->base.type  = AST_DEREF;
            deref->base.next  = NULL;
            deref->base.value = (Token){.value = strdup("->"), .type = TOKEN_ARROW,
                                         .line = parser->current.line,
                                         .column = parser->current.column,
                                         .file = parser->filename};
            deref->operand    = left;
            deref->resultType = DATA_ERROR;
            deref->ptr_depth  = 0;

            if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                Token member_tok = parser->current;
                char* member = strdup(parser->current.value);
                parser_advance(parser);

                AST_MemberAccess* access = calloc(1, sizeof(AST_MemberAccess));
                access->base.type = AST_MEMBER_ACCESS;
                access->object    = (ASTNode*)deref;
                access->member_name = member;
                access->base.next = NULL;
                access->base.value = member_tok;
                access->member_offset = -1;
                access->member_type = DATA_ERROR;
                access->member_return_type = DATA_ERROR;
                access->vtable_index = -1;

                left = (ASTNode*)access;

                if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
                    parser_eat(parser, TOKEN_PAREN_LEFT);

                    ASTNode* args_head = NULL;
                    ASTNode* args_tail = NULL;
                    int arg_count = 0;

                    if (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)) {
                        do {
                            ASTNode* arg = parser_Binary(parser);
                            arg_count++;
                            if (!args_head) { args_head = arg; args_tail = arg; }
                            else { args_tail->next = arg; args_tail = arg; }
                        } while (parser_current_is_type(parser, TOKEN_COMMA)
                                 && (parser_advance(parser), 1));
                    }
                    parser_eat(parser, TOKEN_PAREN_RIGHT);

                    AST_Literal* call = calloc(1, sizeof(AST_Literal));
                    call->base.type = AST_LITERAL;
                    call->base.next = NULL;
                    call->base.value = (Token){access->member_name ? strdup(access->member_name) : NULL,
                                               TOKEN_IDENTIFIER,
                                               parser->current.line, parser->current.column,
                                               parser->filename};
                    call->dataType = DATA_IDENTIFIER;
                    call->internal_name = NULL;
                    call->is_call_function = true;
                    call->method_invoke = true;
                    call->method_arguments = args_head;
                    call->method_arg_count = arg_count;
                    call->call_function_Parameter = (ASTNode*)access;
                    call->call_function_ParameterCount = arg_count + 1;

                    left = (ASTNode*)call;
                }
            } else if (parser_current_is_type(parser, TOKEN_IDENTIFIER) &&
                       strcmp(parser->current.value, "length") == 0) {
                parser_advance(parser);
                AST_ArrayLength* len = calloc(1, sizeof(AST_ArrayLength));
                len->base.type = AST_ARRAY_LENGTH;
                len->array = (ASTNode*)deref;
                len->base.next = NULL;
                left = (ASTNode*)len;
            } else {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line, parser->current.column),
                      "expected member name after '->'");
                free(deref);
                return NULL;
            }
        } else if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            ASTNode* first = parser_Binary(parser);
            if (parser_current_is_type(parser, TOKEN_DOT_DOT)) {
                parser_advance(parser);
                ASTNode* second = parser_Binary(parser);
                parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
                AST_StrSlice* slice = calloc(1, sizeof(AST_StrSlice));
                slice->base.type = AST_STR_SLICE;
                slice->base.next = NULL;
                slice->base.value = (Token){NULL, TOKEN_NONE, 0, 0};
                slice->source = left;
                slice->start = first;
                slice->end = second;
                left = (ASTNode*)slice;
            } else
            {
                int idx_cap = 4, idx_cnt = 1;
                ASTNode** indices = malloc(sizeof(ASTNode*) * idx_cap);
                indices[0] = first;
                parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
                while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                    parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
                    ASTNode* idx = parser_Binary(parser);
                    if (idx == NULL) {
                        free(indices);
                        parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
                        return NULL;
                    }
                    parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
                    if (idx_cnt >= idx_cap) {
                        idx_cap *= 2;
                        indices = realloc(indices, sizeof(ASTNode*) * idx_cap);
                    }
                    indices[idx_cnt++] = idx;
                }
                AST_ArrayAccess* acc = calloc(1,sizeof(AST_ArrayAccess));
                acc->base.type = AST_ARRAY_ACCESS;
                acc->base.next = NULL;
                acc->base.value = (Token){NULL, TOKEN_NONE, 0, 0, parser->filename};
                acc->array = left;
                acc->indices = indices;
                acc->index_count = idx_cnt;
                acc->elementType = DATA_ERROR;
                acc->dim_count = 0;
                acc->dim_sizes = NULL;
                acc->full_dim_count = 0;
                acc->full_dim_sizes = NULL;
                left = (ASTNode*)acc;
                if (indices[0] && indices[0]->value.line > 0) {
                    acc->base.value = indices[0]->value;
                }
            }
        } else if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            if (left->type == AST_LITERAL && !((AST_Literal*)left)->is_call_function) {
                AST_Literal* func_lit = (AST_Literal*)left;
                parser_eat(parser, TOKEN_PAREN_LEFT);
                func_lit->is_call_function = true;
                ASTNode* head = NULL;
                ASTNode* tail = NULL;
                int count = 0;
                while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                       !parser_current_is_type(parser, TOKEN_EOF)) {
                    ASTNode* arg;
                    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                        arg = parser_vararg_group(parser);
                    } else {
                        arg = parser_Binary(parser);
                    }
                    count++;
                    if (head == NULL) {
                        head = arg;
                        head->next = NULL;
                        tail = arg;
                    } else {
                        tail->next = arg;
                        tail = arg;
                        tail->next = NULL;
                    }
                    if (!parser_current_is_type(parser, TOKEN_COMMA)) {
                        break;
                    }
                    parser_eat(parser, TOKEN_COMMA);
                }
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                func_lit->call_function_Parameter = head;
                func_lit->call_function_ParameterCount = count;

                if (func_lit->base.value.value
                    && strcmp(func_lit->base.value.value, "assert") == 0) {
                    if (count != 1) {
                        ERROR(ERR_PARSE_ILLEGAL_OPERAND,
                              LOC(parser->filename, func_lit->base.value.line,
                                  func_lit->base.value.column),
                              "assert expects exactly 1 argument, got %d", count);
                        left = (ASTNode*)func_lit;
                    } else {
                        Token atok = func_lit->base.value;

                        if (!assertions_enabled) {
                            AST_Free(head);
                            func_lit->call_function_Parameter = NULL;
                            free(func_lit);

                            AST_Null* nop = calloc(1, sizeof(AST_Null));
                            if (!nop) OUT_OF_MEMORY();
                            nop->base.type  = AST_NULL;
                            nop->base.next  = NULL;
                            nop->base.value = (Token){ .value = strdup("assert(disabled)"),
                                                       .type  = TOKEN_NONE,
                                                       .line  = atok.line,
                                                       .column = atok.column };
                            nop->ptr_depth = 0;
                            left = (ASTNode*)nop;
                        } else {
                            AST_Unary* not = calloc(1, sizeof(AST_Unary));
                            if (!not) OUT_OF_MEMORY();
                            not->base.type = AST_UNARY;
                            not->base.value = atok;
                            not->op = (Token){.value = strdup("!"),
                                              .type = TOKEN_OPPOSE,
                                              .line = atok.line,
                                              .column = atok.column};
                            not->right = head;
                            not->dataType = DATA_ERROR;

                            char msg_buf[96];
                            snprintf(msg_buf, sizeof(msg_buf),
                                     "assertion failed at line %d, column %d",
                                     atok.line, atok.column);
                            AST_Literal* msg = calloc(1, sizeof(AST_Literal));
                            if (!msg) OUT_OF_MEMORY();
                            msg->base.type = AST_LITERAL;
                            msg->base.next = NULL;
                            msg->base.value.type = TOKEN_STRING_LITERAL;
                            msg->base.value.value = strdup(msg_buf);
                            msg->base.value.line = atok.line;
                            msg->base.value.column = atok.column;
                            msg->dataType = DATA_STRING;
                            msg->is_call_function = false;

                            AST_NewInstance* ni = calloc(1, sizeof(AST_NewInstance));
                            if (!ni) OUT_OF_MEMORY();
                            ni->base.type = AST_NEW_INSTANCE;
                            ni->class_name = strdup("AssertError");
                            ni->arguments = (ASTNode*)msg;
                            ni->has_constructor = false;
                            ni->constructor_name = NULL;
                            ni->constructor_arg_count = 1;

                            AST_Throw* th = calloc(1, sizeof(AST_Throw));
                            if (!th) OUT_OF_MEMORY();
                            th->base.type = AST_THROW;
                            th->base.value = atok;
                            th->value = (ASTNode*)ni;

                            AST_If* ifn = calloc(1, sizeof(AST_If));
                            if (!ifn) OUT_OF_MEMORY();
                            ifn->base.type = AST_IF;
                            ifn->base.value = atok;
                            ifn->Condition = (ASTNode*)not;
                            ifn->code = (ASTNode*)th;
                            ifn->elif_Count = 0;
                            ifn->Elif = NULL;
                            ifn->elif_condition = NULL;
                            ifn->has_else = 0;
                            ifn->Else = NULL;

                            func_lit->call_function_Parameter = NULL;
                            free(func_lit);
                            left = (ASTNode*)ifn;
                        }
                    }
                }
            } else {
                parser_eat(parser, TOKEN_PAREN_LEFT);
                ASTNode* args_head = NULL;
                ASTNode* args_tail = NULL;
                int count = 0;
                if (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)) {
                    do {
                        ASTNode* arg;
                        if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                            arg = parser_vararg_group(parser);
                        } else {
                            arg = parser_Binary(parser);
                        }
                        count++;
                        if (!args_head) { args_head = arg; args_tail = arg; }
                        else { args_tail->next = arg; args_tail = arg; args_tail->next = NULL; }
                    } while (parser_current_is_type(parser, TOKEN_COMMA)
                             && (parser_advance(parser), 1));
                }
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                AST_Literal* call = calloc(1, sizeof(AST_Literal));
                call->base.type = AST_LITERAL;
                call->base.next = NULL;
                call->base.value = (Token){NULL, TOKEN_NONE,
                                          parser->current.line, parser->current.column,
                                          parser->filename};
                call->dataType = DATA_IDENTIFIER;
                call->internal_name = NULL;
                call->is_call_function = true;
                call->is_indirect_call = true;
                call->indirect_callee = left;
                call->call_function_Parameter = args_head;
                call->call_function_ParameterCount = count;
                left = (ASTNode*)call;
            }
        } else if (parser_current_is_type(parser, TOKEN_DOT)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_IDENTIFIER) &&
                strcmp(parser->current.value, "getReturn") == 0) {
                parser_advance(parser);
                parser_eat(parser, TOKEN_PAREN_LEFT);
                AST_ReturnExtract* extract = calloc(1, sizeof(AST_ReturnExtract));
                extract->base.type = AST_RETURN_EXTRACT;
                extract->object = left;
                extract->sub_index = -1;
                if (!parser_current_is_type(parser, TOKEN_NUMBER)) {
                    ERROR(ERR_PARSE_EXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line, parser->current.column),
                          "expected return value index in getReturn()");
                    free(extract);
                    break;
                }
                extract->index = atoi(parser->current.value);
                parser_eat(parser, TOKEN_NUMBER);
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    if (parser_current_is_type(parser, TOKEN_NUMBER)) {
                        extract->sub_index = atoi(parser->current.value);
                        parser_eat(parser, TOKEN_NUMBER);
                    }
                }
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                left = (ASTNode*)extract;
                continue;
            }
            if (parser_current_is_type(parser, TOKEN_IDENTIFIER) &&
                strcmp(parser->current.value, "length") == 0) {
                parser_advance(parser);
                AST_ArrayLength* len = calloc(1, sizeof(AST_ArrayLength));
                len->base.type = AST_ARRAY_LENGTH;
                len->array = left;
                len->base.next = NULL;
                len->base.value = (Token){NULL, TOKEN_NONE, 0, 0, parser->filename};
                left = (ASTNode*)len;
            } else if (parser_current_is_type(parser, TOKEN_IDENTIFIER) &&
                       strcmp(parser->current.value, "toString") == 0) {
                parser_advance(parser);
                parser_eat(parser, TOKEN_PAREN_LEFT);
                parser_eat(parser, TOKEN_PAREN_RIGHT);
                AST_Literal* call = calloc(1, sizeof(AST_Literal));
                call->base.type = AST_LITERAL;
                call->base.next = NULL;
                call->base.value = (Token){NULL, TOKEN_NONE, 0, 0,parser->filename};
                call->base.value.type = TOKEN_IDENTIFIER;
                call->base.value.value = strdup("_arrayToString");
                call->dataType = DATA_STRING;
                call->internal_name = strdup("_arrayToString");
                call->is_call_function = true;
                call->call_function_Parameter = left;
                call->call_function_ParameterCount = 1;
                left = (ASTNode*)call;
            } else {
                Token member_tok = parser->current;
                char* member = strdup(parser->current.value);
                parser_advance(parser);

                AST_MemberAccess* access = calloc(1, sizeof(AST_MemberAccess));
                access->base.type = AST_MEMBER_ACCESS;
                access->object = left;
                access->member_name = member;
                access->base.next = NULL;
                access->base.value = member_tok;
                access->member_type = DATA_ERROR;
                access->member_return_type = DATA_ERROR;
                access->vtable_index = -1;

                left = (ASTNode*)access;
                if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
                    parser_eat(parser, TOKEN_PAREN_LEFT);

                    ASTNode* args_head = NULL;
                    ASTNode* args_tail = NULL;
                    int arg_count = 0;

                    if (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)) {
                        do {
                            ASTNode* arg = parser_Binary(parser);
                            arg_count++;
                            if (!args_head) { args_head = arg; args_tail = arg; }
                            else { args_tail->next = arg; args_tail = arg; }
                        } while (parser_current_is_type(parser, TOKEN_COMMA) && (parser_advance(parser), 1));
                    }
                    parser_eat(parser, TOKEN_PAREN_RIGHT);

                    AST_Literal* call = calloc(1, sizeof(AST_Literal));
                    call->base.type = AST_LITERAL;
                    call->base.next = NULL;
                    call->base.value = (Token){access->member_name ? strdup(access->member_name) : NULL,
                                               TOKEN_IDENTIFIER,
                                               parser->current.line, parser->current.column,
                                               parser->filename};
                    call->dataType = DATA_IDENTIFIER;
                    call->internal_name = NULL;
                    call->is_call_function = true;
                    call->method_invoke = true;
                    call->method_arguments = args_head;
                    call->method_arg_count = arg_count;
                    call->call_function_Parameter = (ASTNode*)access;
                    call->call_function_ParameterCount = arg_count + 1;
                    left = (ASTNode*)call;
                }
            }
        } else if (parser_current_is_type(parser, TOKEN_INCREMENT)) {
            Token op = parser->current;
            parser_advance(parser);
            AST_Unary* unary = malloc(sizeof(AST_Unary));
            unary->base.type = AST_UNARY;
            unary->base.next = NULL;
            unary->base.value = op;
            unary->op = op;
            unary->right = left;
            unary->dataType = DATA_ERROR;
            left = (ASTNode*)unary;
        } else if (parser_current_is_type(parser, TOKEN_DECREMENT))
        {
            Token op = parser->current;
            parser_advance(parser);
            AST_Unary* unary = malloc(sizeof(AST_Unary));
            unary->base.type = AST_UNARY;
            unary->base.next = NULL;
            unary->base.value = op;
            unary->op = op;
            unary->right = left;
            unary->dataType = DATA_ERROR;
            left = (ASTNode*)unary;
        } else {
            break;
        }
    }
    return left;
}

static ASTNode* parser_primary(Parser* parser)
{
    if (parser->current.type == TOKEN_SIZEOF) {
        parser_advance(parser);
        parser_eat(parser, TOKEN_PAREN_LEFT);

        while (parser_current_is_type(parser, TOKEN_CONST)) {
            parser_advance(parser);
        }
        DataType target_type = DATA_ERROR;
        char* class_name = NULL;
        if (is_var_type(parser->current.type)) {
            target_type = Get_Datatype(parser->current.type);
            parser_advance(parser);
        } else if (parser->current.type == TOKEN_IDENTIFIER) {
            class_name = strdup(parser->current.value);
            parser_advance(parser);
        } else {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected type name after 'sizeof('");
        }
        parser_eat(parser, TOKEN_PAREN_RIGHT);
        AST_Literal* lit = calloc(1, sizeof(AST_Literal));
        lit->base.type = AST_LITERAL;
        lit->base.next = NULL;
        lit->base.value = (Token){ .value = NULL, .type = TOKEN_NUMBER,
                                   .line = parser->current.line,
                                   .column = parser->current.column };
        lit->dataType = DATA_INT;
        if (class_name) {
            lit->internal_name = class_name;
            lit->base.value.value = strdup("0");
        } else {
            int size = get_type_size(target_type);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", size);
            lit->base.value.value = strdup(buf);
        }
        lit->is_call_function = false;
        return (ASTNode*)lit;
    }

    if (parser->current.type == TOKEN_NULL) {
        parser_advance(parser);
        AST_Null* n = calloc(1, sizeof(AST_Null));
        n->base.type = AST_NULL;
        n->base.next = NULL;
        n->base.value = (Token){ .value = strdup("0"),
                                 .type = TOKEN_NUMBER,
                                 .line = parser->current.line,
                                 .column = parser->current.column,
                                 .file = parser->filename };
        n->ptr_depth = 0;
        return (ASTNode*)n;
    }

    if (parser->current.type == TOKEN_PAREN_LEFT) {
        parser_eat(parser, TOKEN_PAREN_LEFT);
        ASTNode* node = parser_Binary(parser);
        parser_eat(parser, TOKEN_PAREN_RIGHT);
        return node;
    }

    if (parser->current.type == TOKEN_THIS) {
        parser_advance(parser);
        AST_This* this_node = calloc(1, sizeof(AST_This));
        this_node->base.type = AST_THIS;
        this_node->base.next = NULL;
        this_node->base.value = (Token){NULL, TOKEN_NONE,
                                        parser->current.line, parser->current.column,
                                        parser->filename};
        return (ASTNode*)this_node;
    }

    if (parser->current.type == TOKEN_NEW)
    {
        Token new_token = parser->current;
        parser_advance(parser);

        bool is_array_new = is_var_type(parser->current.type);
        if (!is_array_new && parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            int look = parser->index + 1;
            while (parser->tokens[look].type == TOKEN_DOT
                   && parser->tokens[look + 1].type == TOKEN_IDENTIFIER) {
                look += 2;
            }
            if (parser->tokens[look].type == TOKEN_SQUARE_BRACKET_LEFT) {
                is_array_new = true;
            }
        }

        if (is_array_new) {
            DataType elemType;
            char* elemClassName = NULL;
            if (is_var_type(parser->current.type)) {
                elemType = Get_Datatype(parser->current.type);
                parser_advance(parser);
            } else {
                elemType = DATA_IDENTIFIER;
                char* tn = strdup(parser->current.value);
                parser_advance(parser);
                while (parser_current_is_type(parser, TOKEN_DOT)
                       && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                    parser_advance(parser);
                    int nl = (int)strlen(tn) + 1 + (int)strlen(parser->current.value) + 1;
                    char* merged = malloc((size_t)nl);
                    if (!merged) OUT_OF_MEMORY();
                    snprintf(merged, (size_t)nl, "%s_%s", tn, parser->current.value);
                    free(tn);
                    tn = merged;
                    parser_advance(parser);
                }
                elemClassName = tn;
            }

            int dim_cap = 4, dim_cnt = 0;
            int* dim_sizes = malloc(sizeof(int) * dim_cap);
            ASTNode** dim_exprs = calloc(dim_cap, sizeof(ASTNode*));
            if (!dim_sizes || !dim_exprs) OUT_OF_MEMORY();

            while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
                int size = -1;
                ASTNode* expr = NULL;
                if (parser_current_is_type(parser, TOKEN_NUMBER)) {
                    size = atoi(parser->current.value);
                    parser_eat(parser, TOKEN_NUMBER);
                } else if (!parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
                    expr = parser_Binary(parser);
                    if (expr == NULL) {
                        free(dim_sizes);
                        free(dim_exprs);
                        return NULL;
                    }
                    if (expr->type == AST_LITERAL &&
                        ((AST_Literal*)expr)->base.value.type == TOKEN_NUMBER) {
                        size = atoi(((AST_Literal*)expr)->base.value.value);
                    }
                }
                parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);

                if (dim_cnt >= dim_cap) {
                    dim_cap *= 2;
                    dim_sizes = realloc(dim_sizes, sizeof(int) * dim_cap);
                    dim_exprs = realloc(dim_exprs, sizeof(ASTNode*) * dim_cap);
                    if (!dim_sizes || !dim_exprs) OUT_OF_MEMORY();
                }
                dim_sizes[dim_cnt] = size;
                dim_exprs[dim_cnt] = expr;
                dim_cnt++;
            }

            AST_NewArray* newArr = calloc(1, sizeof(AST_NewArray));
            newArr->base.type = AST_NEW_ARRAY;
            newArr->base.value = new_token;
            newArr->elementType = elemType;
            newArr->element_class_name = elemClassName;
            newArr->dim_count = dim_cnt;
            newArr->dim_sizes = dim_sizes;
            newArr->dim_exprs = dim_exprs;
            return (ASTNode*)newArr;
        } else if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            char* class_name = strdup(parser->current.value);
            parser_advance(parser);

            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int new_len = (int)strlen(class_name) + 1
                            + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)new_len);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)new_len, "%s_%s",
                         class_name, parser->current.value);
                free(class_name);
                class_name = merged;
                parser_advance(parser);
            }

            DataType* generic_type_args = NULL;
            char** generic_type_arg_names = NULL;
            int generic_type_arg_count = 0;
            int generic_type_cap = 0;

            bool is_diamond = false;
            if (parser_current_is_type(parser, TOKEN_LT)) {
                parser_advance(parser);
                if (parser_at_generic_gt(parser)) {
                    parser_take_generic_gt(parser);
                    is_diamond = true;
                } else {
                    generic_type_arg_count = parser_parse_generic_arg_list(
                        parser, &generic_type_args, &generic_type_arg_names);
                    generic_type_cap = generic_type_arg_count;
                }
            }
            parser_eat(parser, TOKEN_PAREN_LEFT);

            ASTNode* args_head = NULL;
            ASTNode* args_tail = NULL;
            int arg_count = 0;
            if (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)) {
                do {
                    ASTNode* arg;
                    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                        arg = parser_vararg_group(parser);
                    } else {
                        arg = parser_Binary(parser);
                    }
                    if (!arg) {
                        while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                               && !parser_current_is_type(parser, TOKEN_EOF))
                            parser_advance(parser);
                        break;
                    }
                    arg_count++;
                    if (!args_head) {
                        args_head = arg;
                        args_tail = arg;
                    } else {
                        args_tail->next = arg;
                        args_tail = arg;
                    }
                } while (parser_current_is_type(parser, TOKEN_COMMA)
                         && (parser_advance(parser), 1));
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            AST_NewInstance* new_inst = calloc(1, sizeof(AST_NewInstance));
            new_inst->base.type = AST_NEW_INSTANCE;
            new_inst->base.value = new_token;
            new_inst->class_name = class_name;
            new_inst->arguments = args_head;
            new_inst->has_constructor = false;
            new_inst->constructor_name = NULL;
            new_inst->constructor_arg_count = arg_count;
            new_inst->generic_type_args = generic_type_args;
            new_inst->generic_type_arg_count = generic_type_arg_count;
            new_inst->generic_type_arg_names = generic_type_arg_names;
            new_inst->generic_infer = is_diamond;
            return (ASTNode*)new_inst;
        } else {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected type or class name after 'new'");
            return NULL;
        }
    }

    AST_Literal* lit = calloc(1,sizeof(AST_Literal));
    lit->base.type = AST_LITERAL;
    lit->base.next = NULL;

    if (parser->current.type == TOKEN_NUMBER ||
        parser->current.type == TOKEN_STRING_LITERAL ||
        parser->current.type == TOKEN_CHAR_LITERAL ||
        parser->current.type == TOKEN_BOOL_LITERAL ||
        parser->current.type == TOKEN_FLOAT_LITERAL ||
        parser->current.type == TOKEN_IDENTIFIER) {

        if (parser->current.type == TOKEN_BOOL_LITERAL) {
            if (strcmp(parser->current.value, "true") == 0) {
                lit->base.value = (Token){strdup("1"), TOKEN_NUMBER,
                                           parser->current.line, parser->current.column};
            } else {
                lit->base.value = (Token){strdup("0"), TOKEN_NUMBER,
                                           parser->current.line, parser->current.column};
            }
        } else
        {
            lit->base.value = parser->current;
        }

        lit->dataType = Get_Datatype(parser->current.type);
        lit->internal_name = NULL;
        lit->is_call_function = false;
        lit->call_function_Parameter = NULL;
        lit->call_function_ParameterCount = 0;
        lit->method_arguments = NULL;
        lit->method_arg_count = 0;
        lit->is_field_ref = false;
        lit->field_offset = 0;
        parser_advance(parser);
        return (ASTNode*)lit;
        }

    ERROR(ERR_PARSE_ILLEGAL_OPERAND,
          LOC(parser->filename, parser->current.line, parser->current.column),
          "illegal operand '%s'",
          parser->current.value);
    AST_Literal* error_ast_node = calloc(1,sizeof(AST_Literal));
    error_ast_node->base.value = parser->current;
    error_ast_node->base.type = AST_ERROR;
    error_ast_node->base.next = NULL;
    error_ast_node->base.value = parser->current;
    error_ast_node->is_field_ref = false;
    error_ast_node->field_offset = 0;
    parser_advance(parser);
    return (ASTNode*)error_ast_node;
}

ASTNode* parser_Unary(Parser* parser) {
    if (parser_current_is_type(parser, TOKEN_MUL)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* operand = parser_Unary(parser);
        AST_Deref* deref = calloc(1, sizeof(AST_Deref));
        deref->base.type  = AST_DEREF;
        deref->base.next  = NULL;
        deref->base.value = op;
        deref->operand    = operand;
        deref->resultType = DATA_ERROR;
        deref->ptr_depth  = 0;
        return (ASTNode*)deref;
    }

    if (parser_current_is_type(parser, TOKEN_BITWISE_AND)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* operand = parser_Unary(parser);  // �ҽ��?
        AST_AddressOf* addr = calloc(1, sizeof(AST_AddressOf));
        addr->base.type  = AST_ADDRESS_OF;
        addr->base.next  = NULL;
        addr->base.value = op;
        addr->operand    = operand;
        addr->resultType = DATA_INT;
        addr->ptr_depth  = 0;
        return (ASTNode*)addr;
    }

    if (parser_current_is_type(parser, TOKEN_SUB) || parser_current_is_type(parser, TOKEN_OPPOSE) ||
    parser_current_is_type(parser, TOKEN_BITWISE_NOT) || parser_current_is_type(parser, TOKEN_INCREMENT) ||
    parser_current_is_type(parser, TOKEN_DECREMENT)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Unary(parser);

        AST_Unary* unary = malloc(sizeof(AST_Unary));
        unary->base.type = AST_UNARY;
        unary->base.next = NULL;
        unary->base.value = op;
        unary->op = op;
        unary->right = right;
        unary->dataType = DATA_ERROR;
        return (ASTNode*)unary;
    }

    ASTNode* atom = parser_primary(parser);
    if (!atom) return NULL;
    return parser_Postfix(parser, atom);
}

static ASTNode* parser_compound_assign_or_incdec(Parser* parser)
{
    Token var_token = parser->current;
    parser_advance(parser);
    Token op_token = parser->current;
    parser_advance(parser);

    ASTNode* right_expr = NULL;

    if (op_token.type == TOKEN_INCREMENT || op_token.type == TOKEN_DECREMENT) {
        AST_Literal* one = calloc(1, sizeof(AST_Literal));
        one->base.type = AST_LITERAL;
        one->base.value.value = strdup("1");
        one->base.value.type = TOKEN_NUMBER;
        one->dataType = DATA_INT;
        one->is_call_function = false;

        AST_Literal* var_expr = calloc(1, sizeof(AST_Literal));
        var_expr->base.type = AST_LITERAL;
        var_expr->base.value = var_token;
        var_expr->base.value.value = strdup(var_token.value);
        var_expr->dataType = DATA_IDENTIFIER;
        var_expr->is_call_function = false;

        AST_Binary* bin = calloc(1, sizeof(AST_Binary));
        bin->base.type = AST_BINARY;
        bin->left = (ASTNode*)var_expr;
        bin->op.type = (op_token.type == TOKEN_INCREMENT) ? TOKEN_ADD : TOKEN_SUB;
        bin->op.value = strdup((bin->op.type == TOKEN_ADD) ? "+" : "-");
        bin->right = (ASTNode*)one;
        bin->resultType = DATA_ERROR;
        right_expr = (ASTNode*)bin;
    } else {
        ASTNode* expr = parser_Binary(parser);

        AST_Literal* var_expr = calloc(1, sizeof(AST_Literal));
        var_expr->base.type = AST_LITERAL;
        var_expr->base.value = var_token;
        var_expr->base.value.value = strdup(var_token.value);
        var_expr->dataType = DATA_IDENTIFIER;
        var_expr->is_call_function = false;

        TokenType bin_type;
        switch (op_token.type) {
            case TOKEN_ADD_ASSIGN: bin_type = TOKEN_ADD; break;
            case TOKEN_SUB_ASSIGN: bin_type = TOKEN_SUB; break;
            case TOKEN_MUL_ASSIGN: bin_type = TOKEN_MUL; break;
            case TOKEN_DIV_ASSIGN: bin_type = TOKEN_DIV; break;
            case TOKEN_MOD_ASSIGN: bin_type = TOKEN_MOD; break;
            case TOKEN_SHIFT_LEFT_ASSIGN:  bin_type = TOKEN_SHIFT_LEFT;  break;
            case TOKEN_SHIFT_RIGHT_ASSIGN: bin_type = TOKEN_SHIFT_RIGHT; break;
            case TOKEN_BITWISE_XOR_ASSIGN: bin_type = TOKEN_BITWISE_XOR; break;
            default: bin_type = TOKEN_ADD;
        }

        AST_Binary* bin = calloc(1, sizeof(AST_Binary));
        bin->base.type = AST_BINARY;
        bin->left = (ASTNode*)var_expr;
        bin->op.type = bin_type;
        bin->op.value = strdup(type_to_string(bin_type));
        bin->right = expr;
        bin->resultType = DATA_ERROR;
        right_expr = (ASTNode*)bin;
    }

    AST_Var* assign = calloc(1, sizeof(AST_Var));
    assign->base.type = AST_VAR_ASS;
    assign->base.value = var_token;
    assign->name = var_token;
    assign->Value = right_expr;
    assign->type = var_token.type;
    assign->dataType = DATA_ERROR;
    assign->internal_name = NULL;
    assign->type_param_index = -1;

    return (ASTNode*)assign;
}

static ASTNode* parser_As(Parser* parser)
{
    ASTNode* left = parser_Unary(parser);

    while (parser_current_is_type(parser, TOKEN_AS)) {
        Token as_tok = parser->current;
        parser_advance(parser);

        DataType target_type = DATA_ERROR;
        char* target_class_name = NULL;
        int ptr_depth = 0;
        FuncSignature* target_func_sig = NULL;

        bool is_func_ptr = false;
        if ((is_var_type(parser->current.type)
             || parser_current_is_type(parser, TOKEN_IDENTIFIER))
            && parser->tokens[parser->index + 1].type == TOKEN_PAREN_LEFT
            && parser->tokens[parser->index + 2].type == TOKEN_MUL) {
            is_func_ptr = true;
        }

        if (is_func_ptr) {
            target_func_sig = parse_func_ptr_type_signature(parser);
            if (target_func_sig == NULL) {
                break;
            }
            target_type = DATA_INT;
            ptr_depth = 1;
        } else if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            char* tn = strdup(parser->current.value);
            parser_advance(parser);
            while (parser_current_is_type(parser, TOKEN_DOT)
                   && parser->tokens[parser->index + 1].type == TOKEN_IDENTIFIER) {
                parser_advance(parser);
                int nl = (int)strlen(tn) + 1 + (int)strlen(parser->current.value) + 1;
                char* merged = malloc((size_t)nl);
                if (!merged) OUT_OF_MEMORY();
                snprintf(merged, (size_t)nl, "%s_%s", tn, parser->current.value);
                free(tn);
                tn = merged;
                parser_advance(parser);
            }
            target_class_name = tn;
            target_type = DATA_IDENTIFIER;
        } else if (is_var_type(parser->current.type)) {
            target_type = Get_Datatype(parser->current.type);
            parser_advance(parser);
        } else if (parser_current_is_type(parser, TOKEN_VOID)) {
            target_type = DATA_VOID;
            parser_advance(parser);
        } else {
            ERROR(ERR_PARSE_INVALID_TYPE,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected type after 'as', got '%s'",
                  type_to_string(parser->current.type));
            break;
        }

        while (parser_current_is_type(parser, TOKEN_MUL)) {
            ptr_depth++;
            parser_advance(parser);
        }

        int dim_count = 0;
        int dim_cap = 0;
        int* dim_sizes = NULL;
        if (!is_func_ptr) {
            while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
                parser_advance(parser);
                int size = -1;
                if (parser_current_is_type(parser, TOKEN_NUMBER)) {
                    size = atoi(parser->current.value);
                    parser_advance(parser);
                } else if (!parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_RIGHT)) {
                    ERROR(ERR_PARSE_EXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line,
                              parser->current.column),
                          "array view cast requires integer size");
                    break;
                }
                parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
                if (dim_count >= dim_cap) {
                    dim_cap = dim_cap ? dim_cap * 2 : 4;
                    dim_sizes = realloc(dim_sizes, sizeof(int) * dim_cap);
                    if (!dim_sizes) OUT_OF_MEMORY();
                }
                dim_sizes[dim_count++] = size;
            }
        }

        AST_Cast* cast = calloc(1, sizeof(AST_Cast));
        if (!cast) OUT_OF_MEMORY();
        cast->base.type = AST_CAST;
        cast->base.next = NULL;
        cast->base.value = as_tok;
        cast->operand = left;
        cast->target_type = target_type;
        cast->target_class_name = target_class_name;
        cast->target_class_id = -1;
        cast->target_ptr_depth = ptr_depth;
        cast->target_func_sig = target_func_sig;
        cast->target_dim_count = dim_count;
        cast->target_dim_sizes = dim_sizes;
        cast->source_type = DATA_ERROR;
        cast->source_ptr_depth = 0;
        cast->source_func_sig = NULL;
        cast->is_reinterpret = false;
        cast->is_checked = false;

        left = (ASTNode*)cast;
    }
    return left;
}

ASTNode* parser_Mul_Div(Parser* parser)
{
    ASTNode* left = parser_As(parser);
    while (parser_current_is_type(parser, TOKEN_MUL) || parser_current_is_type(parser, TOKEN_DIV) || parser_current_is_type(parser, TOKEN_MOD))
    {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_As(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

ASTNode* parser_Add_Sub(Parser* parser)
{
    ASTNode* left = parser_Mul_Div(parser);
    while (parser_current_is_type(parser, TOKEN_ADD) || parser_current_is_type(parser, TOKEN_SUB))
    {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Mul_Div(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_Shift(Parser* parser)
{
    ASTNode* left = parser_Add_Sub(parser);
    while (parser_current_is_type(parser, TOKEN_SHIFT_LEFT) ||
           parser_current_is_type(parser, TOKEN_SHIFT_RIGHT))
    {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Add_Sub(parser);
        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left  = left;
        binary->op    = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_BitwiseAnd(Parser* parser) {
    ASTNode* left = parser_Shift(parser);
    while (parser_current_is_type(parser, TOKEN_BITWISE_AND)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Shift(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_BitwiseXor(Parser* parser) {
    ASTNode* left = parser_BitwiseAnd(parser);
    while (parser_current_is_type(parser, TOKEN_BITWISE_XOR)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_BitwiseAnd(parser);
        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left  = left;
        binary->op    = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_BitwiseOr(Parser* parser) {
    ASTNode* left = parser_BitwiseXor(parser);
    while (parser_current_is_type(parser, TOKEN_BITWISE_OR)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_BitwiseXor(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

ASTNode* parser_Logic(Parser* parser)
{
    ASTNode* left = parser_BitwiseOr(parser);
    while (parser_current_is_type(parser, TOKEN_EQ) ||
           parser_current_is_type(parser, TOKEN_NE) ||
           parser_current_is_type(parser, TOKEN_LT) ||
           parser_current_is_type(parser, TOKEN_GT) ||
           parser_current_is_type(parser, TOKEN_LE) ||
           parser_current_is_type(parser, TOKEN_GE))
    {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_BitwiseOr(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_LogicalAnd(Parser* parser) {
    ASTNode* left = parser_Logic(parser);
    while (parser_current_is_type(parser, TOKEN_LOGICAL_AND)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_Logic(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_LogicalOr(Parser* parser) {
    ASTNode* left = parser_LogicalAnd(parser);
    while (parser_current_is_type(parser, TOKEN_LOGICAL_OR)) {
        Token op = parser->current;
        parser_advance(parser);
        ASTNode* right = parser_LogicalAnd(parser);

        AST_Binary* binary = calloc(sizeof(AST_Binary),1);
        binary->left = left;
        binary->op = op;
        binary->right = right;
        binary->base.next = NULL;
        binary->base.type = AST_BINARY;
        binary->base.value = binary->op;
        left = (ASTNode*)binary;
    }
    return left;
}

static ASTNode* parser_Conditional(Parser* parser) {
    ASTNode* cond = parser_LogicalOr(parser);
    if (parser_current_is_type(parser, TOKEN_QUESTION)) {
        parser_eat(parser, TOKEN_QUESTION);
        ASTNode* then_expr = parser_Binary(parser);
        parser_eat(parser, TOKEN_COLON);
        ASTNode* else_expr = parser_Conditional(parser);
        AST_Conditional* tern = calloc(1, sizeof(AST_Conditional));
        if (!tern) OUT_OF_MEMORY();
        tern->base.type  = AST_CONDITIONAL;
        tern->base.next  = NULL;
        tern->base.value = (Token){ .value = strdup("?:"),
                                    .type  = TOKEN_QUESTION,
                                    .line  = cond->value.line,
                                    .column = cond->value.column };
        tern->condition  = cond;
        tern->then_expr  = then_expr;
        tern->else_expr  = else_expr;
        tern->resultType = DATA_ERROR;
        return (ASTNode*)tern;
    }
    return cond;
}

ASTNode* parser_Binary(Parser* parser)
{
    return parser_Conditional(parser);
}

ASTNode* parser_import(Parser* parser) {
    parser_eat(parser, TOKEN_IMPORT);
    char* module_name = NULL;
    if (parser_current_is_type(parser, TOKEN_STRING_LITERAL)) {
        module_name = strdup(parser->current.value);
        parser_advance(parser);
    }
    else if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
        int cap = 64;
        module_name = malloc(cap);
        module_name[0] = '\0';
        while (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            if (strlen(module_name) > 0) strcat(module_name, ".");
            strcat(module_name, parser->current.value);
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_DOT)) {
                parser_advance(parser);
                if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                    ERROR(ERR_PARSE_EXPECTED_TOKEN,
                          LOC(parser->filename, parser->current.line, parser->current.column),
                          "expected module name after '.'");
                    free(module_name);
                    return NULL;
                }
            } else {
                break;
            }
        }
    } else {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line, parser->current.column),
              "expected module name after 'import'");
        return NULL;
    }
    char* alias = NULL;
    if (parser_current_is_type(parser, TOKEN_AS)) {
        parser_advance(parser);
        if (parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            alias = strdup(parser->current.value);
            parser_advance(parser);
        } else {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line, parser->current.column),
                  "expected alias name after 'as'");
            free(module_name);
            return NULL;
        }
    }
    char* abs_path = resolve_module(module_name, parser->filename);
    if (!abs_path) {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND,
              LOC(parser->filename, 0, 0),
              "module '%s' not found",
              module_name);
        free(module_name);
        free(alias);
        return NULL;
    }
    if (is_already_imported(abs_path)) {
        free(module_name);
        free(alias);
        free(abs_path);
        return NULL;
    }
    mark_imported(abs_path);
    char* source = read_source(abs_path);
    if (!source) {
        ERROR(ERR_PARSE_MODULE_NOT_FOUND,
              LOC(parser->filename, 0, 0),
              "failed to read module '%s'",
              module_name);
        free(module_name);
        free(alias);
        free(abs_path);
        return NULL;
    }
    Lexer* lex = lexer(source, abs_path);
    ImportedLexer* il = malloc(sizeof(ImportedLexer));
    if (!il) OUT_OF_MEMORY();
    il->lex = lex;
    il->next = imported_lexers;
    imported_lexers = il;

    Parser* sub_parser = parser_init(lex->tokens, abs_path);
    ASTNode* imported_ast = parser_parse(sub_parser);

    free(module_name);
    free(alias);

    return imported_ast;
}

int parser_parse_type_params(Parser* parser, TypeParam** out) {
    parser_eat(parser, TOKEN_LT);

    int cap = 4;
    *out = malloc(sizeof(TypeParam) * (size_t)cap);
    if (!*out) OUT_OF_MEMORY();
    int count = 0;

    do {
        if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
            ERROR(ERR_PARSE_EXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected type parameter name, got '%s'",
                  type_to_string(parser->current.type));
            goto error;
        }

        if (count >= cap) {
            cap *= 2;
            *out = realloc(*out, sizeof(TypeParam) * (size_t)cap);
            if (!*out) OUT_OF_MEMORY();
        }

        TypeParam* tp = &(*out)[count];
        memset(tp, 0, sizeof(TypeParam));
        tp->name   = strdup(parser->current.value);
        tp->index  = count;
        parser_advance(parser);

        int constr_cap = 2;
        tp->constraints = malloc(sizeof(TypeConstraint) * (size_t)constr_cap);
        if (!tp->constraints) OUT_OF_MEMORY();

        while (parser_current_is_type(parser, TOKEN_COLON) ||
               (parser_current_is_type(parser, TOKEN_GT) &&
                parser->tokens[parser->index + 1].type == TOKEN_COLON &&
                parser->tokens[parser->index + 1].column
                    == parser->current.column + 1)) {

            ConstraintKind kind = CONSTRAINT_SUBTYPE;

            if (parser_current_is_type(parser, TOKEN_COLON)) {
                kind = CONSTRAINT_SUBTYPE;
                parser_advance(parser);
            } else if (parser_current_is_type(parser, TOKEN_GT)) {
                parser_advance(parser);
                parser_eat(parser, TOKEN_COLON);
                kind = CONSTRAINT_SUPERTYPE;
            } else {
                break;
            }

            if (!parser_current_is_type(parser, TOKEN_IDENTIFIER)) {
                ERROR(ERR_PARSE_EXPECTED_TOKEN,
                      LOC(parser->filename, parser->current.line,
                          parser->current.column),
                      "expected class name after constraint '%s'",
                      kind == CONSTRAINT_SUBTYPE ? ":" : ">:");
                goto error;
            }

            if (tp->constraint_count >= constr_cap) {
                constr_cap *= 2;
                tp->constraints = realloc(tp->constraints,
                    sizeof(TypeConstraint) * (size_t)constr_cap);
                if (!tp->constraints) OUT_OF_MEMORY();
            }

            tp->constraints[tp->constraint_count].kind       = kind;
            tp->constraints[tp->constraint_count].class_name =
                strdup(parser->current.value);
            tp->constraint_count++;
            parser_advance(parser);
        }

        count++;

    } while (parser_current_is_type(parser, TOKEN_COMMA) &&
             (parser_advance(parser), 1));

    if (!parser_current_is_type(parser, TOKEN_GT)) {
        ERROR(ERR_PARSE_EXPECTED_TOKEN,
              LOC(parser->filename, parser->current.line,
                  parser->current.column),
              "expected '>' to close type parameter list, got '%s'",
              type_to_string(parser->current.type));
        goto error;
    }
    parser_eat(parser, TOKEN_GT);
    return count;

error:
    free_type_params(*out, count);
    *out = NULL;
    return -1;
}

ASTNode* parser_def_function_with_generics(Parser* parser,
                                            int modifier,
                                            TypeParam* tparams,
                                            int tp_count) {
    TypeParam* saved_params = g_parse_type_params;
    int saved_count = g_parse_type_param_count;
    g_parse_type_params = tparams;
    g_parse_type_param_count = tp_count;

    AST_Function* ast_function = calloc(1, sizeof(AST_Function));
    ast_function->max_parameters = 4;
    ast_function->parameterType = malloc(sizeof(DataType) * 4);
    ast_function->return_Count = 0;
    ast_function->parameterCount = 0;
    ast_function->Parameter = NULL;
    ast_function->Return = NULL;
    ast_function->Modifier = modifier;
    ast_function->internal_name = NULL;
    ast_function->returns_array = false;
    ast_function->return_element_type = DATA_VOID;
    ast_function->return_dim_count = 0;
    ast_function->return_dim_sizes = NULL;
    ast_function->ret_func_sig = NULL;

    ast_function->type_params = tparams;
    ast_function->type_param_count = tp_count;
    ast_function->is_generic = true;

    DataType return_type = DATA_VOID;
    MultiReturnType* multi_return = NULL;

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)
        && parser->tokens[parser->index + 1].type != TOKEN_MUL) {
        int saved = parser->index;
        int look = saved + 1;
        int depth = 1;
        while (parser->tokens[look].type != TOKEN_EOF && depth > 0) {
            if (parser->tokens[look].type == TOKEN_PAREN_LEFT) depth++;
            if (parser->tokens[look].type == TOKEN_PAREN_RIGHT) { depth--; if (depth == 0) break; }
            look++;
        }
        bool is_func_ptr = false;
        if (depth == 0) {
            int after = look + 1;
            if (parser->tokens[after].type == TOKEN_PAREN_LEFT
                && parser->tokens[after + 1].type == TOKEN_MUL
                && parser->tokens[after + 2].type == TOKEN_PAREN_RIGHT) {
                is_func_ptr = true;
            }
        }
        if (is_func_ptr) {
            FuncSignature* fpsig = parse_func_ptr_type_signature(parser);
            if (fpsig) {
                if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
                ast_function->ret_func_sig = fpsig;
                return_type = DATA_INT;
            }
        } else {
            multi_return = parse_multi_return_type(parser);
            return_type = DATA_VOID;
        }
    } else if (parser->current.type == TOKEN_IDENTIFIER) {
        TokenType next = parser->tokens[parser->index + 1].type;
        if (next == TOKEN_PAREN_LEFT || next == TOKEN_LT) {
            ERROR(ERR_PARSE_INVALID_TYPE,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "expected return type after type parameters, "
                  "got function name '%s'",
                  parser->current.value);
        } else {
            return_type = Get_Datatype(parser->current.type);
            parser_advance(parser);
        }
    } else if (is_var_type(parser->current.type)) {
        return_type = Get_Datatype(parser->current.type);
        parser_advance(parser);
    }

    int return_ptr_depth = 0;
    while (parser_current_is_type(parser, TOKEN_MUL)) {
        return_ptr_depth++;
        parser_advance(parser);
    }
    ast_function->return_ptr_depth = return_ptr_depth;

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_idx = parser->index;
        int look = saved_idx + 1;
        int star_count = 0;
        while (parser->tokens[look].type == TOKEN_MUL && look < saved_idx + 10) {
            star_count++;
            look++;
        }
        if (star_count > 0 && parser->tokens[look].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            while (parser_current_is_type(parser, TOKEN_MUL)) {
                return_ptr_depth++;
                parser_advance(parser);
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            parser_eat(parser, TOKEN_PAREN_LEFT);
            int param_cap = 4;
            int param_count = 0;
            DataType* param_types = malloc(sizeof(DataType) * param_cap);
            bool func_ptr_has_varargs = false;
            int* func_ptr_vararg_indices = NULL;
            int func_ptr_vararg_count = 0;
            int func_ptr_vararg_cap = 4;
            func_ptr_vararg_indices = malloc(sizeof(int) * func_ptr_vararg_cap);
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT)
                   && !parser_current_is_type(parser, TOKEN_EOF)) {
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    if (param_count > 0) {
                        func_ptr_has_varargs = true;
                        if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                            func_ptr_vararg_cap *= 2;
                            func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                                sizeof(int) * func_ptr_vararg_cap);
                        }
                        func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count - 1;
                    }
                    if (parser_current_is_type(parser, TOKEN_COMMA))
                        parser_advance(parser);
                    continue;
                }
                {
                    DataType nested_base = DATA_ERROR;
                    FuncSignature* nested_sig = NULL;
                    if (parser_try_nested_func_ptr_param(parser, &nested_base, &nested_sig)) {
                        if (param_count >= param_cap) {
                            param_cap *= 2;
                            param_types = realloc(param_types, sizeof(DataType) * param_cap);
                        }
                        param_types[param_count] = nested_base;
                        param_count++;
                        if (parser_current_is_type(parser, TOKEN_COMMA)) {
                            parser_eat(parser, TOKEN_COMMA);
                        } else {
                            break;
                        }
                        continue;
                    }
                }
                if (!is_var_type(parser->current.type) && parser->current.type != TOKEN_IDENTIFIER) {
                    break;
                }
                DataType pt = Get_Datatype(parser->current.type);
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_IDENTIFIER))
                    parser_advance(parser);
                bool is_vararg = false;
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    is_vararg = true;
                    parser_advance(parser);
                }
                if (param_count >= param_cap) {
                    param_cap *= 2;
                    param_types = realloc(param_types, sizeof(DataType) * param_cap);
                }
                param_types[param_count] = pt;
                if (is_vararg) {
                    func_ptr_has_varargs = true;
                    if (func_ptr_vararg_count >= func_ptr_vararg_cap) {
                        func_ptr_vararg_cap *= 2;
                        func_ptr_vararg_indices = realloc(func_ptr_vararg_indices,
                            sizeof(int) * func_ptr_vararg_cap);
                    }
                    func_ptr_vararg_indices[func_ptr_vararg_count++] = param_count;
                }
                param_count++;
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
            }
            parser_eat(parser, TOKEN_PAREN_RIGHT);
            FuncSignature* sig = calloc(1, sizeof(FuncSignature));
            sig->return_type = return_type;
            sig->param_count = param_count;
            sig->param_types = param_types;
            sig->has_varargs = func_ptr_has_varargs;
            sig->vararg_param_count = func_ptr_vararg_count;
            sig->vararg_param_indices = (func_ptr_vararg_count > 0) ? func_ptr_vararg_indices : NULL;
            sig->multi_return = multi_return;
            multi_return = NULL;
            if (func_ptr_vararg_count == 0) free(func_ptr_vararg_indices);
            if (ast_function->ret_func_sig) free_func_sig_recursive(ast_function->ret_func_sig);
            ast_function->ret_func_sig = sig;
            ast_function->return_ptr_depth += star_count;
            return_type = DATA_INT;
        }
        else if (parse_return_type_func_ptr(parser, ast_function,
                                             return_type, return_ptr_depth)) {
            return_type = DATA_INT;
        }
    }

    if (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
        int dim_cap = 4;
        int dim_cnt = 0;
        int* dim_sizes = malloc(sizeof(int) * dim_cap);
        ASTNode** dim_exprs = calloc(dim_cap, sizeof(ASTNode*));
        if (!dim_sizes) OUT_OF_MEMORY();
        while (parser_current_is_type(parser, TOKEN_SQUARE_BRACKET_LEFT)) {
            parser_eat(parser, TOKEN_SQUARE_BRACKET_LEFT);
            int size = -1;
            ASTNode* expr = parse_dim_expression(parser, &size);
            parser_eat(parser, TOKEN_SQUARE_BRACKET_RIGHT);
            if (dim_cnt >= dim_cap) {
                dim_cap *= 2;
                dim_sizes = realloc(dim_sizes, sizeof(int) * (size_t)dim_cap);
                dim_exprs = realloc(dim_exprs, sizeof(ASTNode*) * dim_cap);
                if (!dim_sizes) OUT_OF_MEMORY();
            }
            dim_sizes[dim_cnt] = size;
            dim_exprs[dim_cnt] = expr;
            dim_cnt++;
        }
        ast_function->return_dim_exprs = dim_exprs;
        if (dim_cnt > 0) {
            ast_function->returns_array = true;
            ast_function->return_element_type = return_type;
            ast_function->return_dim_count = dim_cnt;
            ast_function->return_dim_sizes = dim_sizes;
            return_type = DATA_VOID;
        } else {
            free(dim_sizes);
        }
    }

    if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
        int saved_name = parser->index;
        if (parser->tokens[saved_name + 1].type == TOKEN_MUL
            && parser->tokens[saved_name + 2].type == TOKEN_IDENTIFIER
            && parser->tokens[saved_name + 3].type == TOKEN_PAREN_RIGHT) {
            parser_eat(parser, TOKEN_PAREN_LEFT);
            parser_eat(parser, TOKEN_MUL);
            ast_function->base.value = parser->current;
            ast_function->name = parser->current;
            parser_advance(parser);
            parser_eat(parser, TOKEN_PAREN_RIGHT);
        } else {
            ast_function->base.value = parser->current;
            ast_function->name = parser->current;
            parser_advance(parser);
        }
    } else {
        ast_function->base.value = parser->current;
        ast_function->name = parser->current;
        parser_advance(parser);
    }

    parser_eat(parser, TOKEN_PAREN_LEFT);
    while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) && !parser_current_is_type(parser, TOKEN_EOF)) {
        if (parser_current_is_type(parser, TOKEN_PAREN_LEFT)) {
            int saved = parser->index;
            Token saved_cur = parser->current;
            ASTNode* param = parser_multi_return_func_ptr_var(parser);
            if (param != NULL) {
                DataType pdt = DATA_INT;
                function_add_parameter(ast_function, param, pdt);
                if (parser_current_is_type(parser, TOKEN_VARARG)) {
                    parser_advance(parser);
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx] = ast_function->parameterCount - 1;
                    if (param->type == AST_VAR || param->type == AST_UNINIT_VAR) {
                        AST_Var* var = (AST_Var*)param;
                        ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                        if (var->total_bytes < 8) var->total_bytes = 8;
                        var->dim_count = 1;
                        if (!var->dim_sizes) {
                            var->dim_sizes = malloc(sizeof(int));
                            var->dim_sizes[0] = -1;
                        }
                    } else {
                        char placeholder[32];
                        snprintf(placeholder, sizeof(placeholder), "_va_mfp_%d", idx);
                        ast_function->vararg_internal_names[idx] = strdup(placeholder);
                    }
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name = strdup(
                            ast_function->vararg_internal_names[0]);
                    }
                }
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_eat(parser, TOKEN_COMMA);
                } else {
                    break;
                }
                continue;
            }
            parser->index = saved;
            parser->current = saved_cur;
        }
        if (parser_current_is_type(parser, TOKEN_VARARG)) {
            parser_advance(parser);
            ast_function->has_varargs = true;
            char placeholder_name[32];
            snprintf(placeholder_name, sizeof(placeholder_name),
                     "_va_placeholder_%d", ast_function->vararg_param_count);
            AST_Var* placeholder = calloc(1, sizeof(AST_Var));
            placeholder->base.type = AST_VARARG;
            placeholder->base.next = NULL;
            placeholder->name.value = strdup(placeholder_name);
            placeholder->name.type = TOKEN_IDENTIFIER;
            placeholder->type = TOKEN_INT;
            placeholder->dataType = DATA_INT;
            placeholder->internal_name = strdup(placeholder_name);
            placeholder->dim_count = 0;
            placeholder->type_param_index = -1;
            ast_function->vararg_param_indices = realloc(
                ast_function->vararg_param_indices,
                sizeof(int) * (ast_function->vararg_param_count + 1));
            ast_function->vararg_internal_names = realloc(
                ast_function->vararg_internal_names,
                sizeof(char*) * (ast_function->vararg_param_count + 1));
            ast_function->vararg_param_indices[ast_function->vararg_param_count]
                = ast_function->parameterCount;
            ast_function->vararg_internal_names[ast_function->vararg_param_count]
                = strdup(placeholder_name);
            if (ast_function->vararg_param_count == 0) {
                ast_function->vararg_param_index = ast_function->parameterCount;
                ast_function->vararg_internal_name = strdup(placeholder_name);
            }
            ast_function->vararg_param_count++;
            function_add_parameter(ast_function, (ASTNode*)placeholder, DATA_INT);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        ASTNode* temp = parser_var(parser, false);
        if (temp == NULL) {
            while (!parser_current_is_type(parser, TOKEN_PAREN_RIGHT) &&
                   !parser_current_is_type(parser, TOKEN_COMMA) &&
                   !parser_current_is_type(parser, TOKEN_EOF))
                parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_COMMA))
                parser_advance(parser);
            continue;
        }
        DataType param_dtype;
        if (temp->type == AST_ARRAY_DECL) {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)temp;
            param_dtype = arr->elementType;
        } else if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
            AST_Var* var = (AST_Var*)temp;
            if (var->func_ptr_sig || var->ptr_array_info) {
                var->dataType = DATA_INT;
            }
            param_dtype = var->dataType;
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                ast_function->has_varargs = true;
                int idx = ast_function->vararg_param_count++;
                ast_function->vararg_param_indices = realloc(
                    ast_function->vararg_param_indices,
                    sizeof(int) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_internal_names = realloc(
                    ast_function->vararg_internal_names,
                    sizeof(char*) * (size_t)ast_function->vararg_param_count);
                ast_function->vararg_param_indices[idx] = ast_function->parameterCount;
                ast_function->vararg_internal_names[idx] = strdup(var->name.value);
                if (ast_function->vararg_param_count == 1) {
                    ast_function->vararg_param_index = ast_function->parameterCount;
                    if (ast_function->vararg_internal_name)
                        free(ast_function->vararg_internal_name);
                    ast_function->vararg_internal_name = strdup(var->name.value);
                }
                if (var->total_bytes < 8) var->total_bytes = 8;
            }
        } else {
            param_dtype = DATA_ERROR;
        }
        function_add_parameter(ast_function, temp, param_dtype);

        if (parser_current_is_type(parser, TOKEN_COMMA)) {
            parser_advance(parser);
            if (parser_current_is_type(parser, TOKEN_VARARG)) {
                parser_advance(parser);
                if (parser_current_is_type(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                        AST_Var* pv = (AST_Var*)temp;
                        ast_function->has_varargs = true;
                        int idx = ast_function->vararg_param_count++;
                        ast_function->vararg_param_indices = realloc(
                            ast_function->vararg_param_indices,
                            sizeof(int) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_internal_names = realloc(
                            ast_function->vararg_internal_names,
                            sizeof(char*) * (size_t)ast_function->vararg_param_count);
                        ast_function->vararg_param_indices[idx]
                            = ast_function->parameterCount - 1;
                        ast_function->vararg_internal_names[idx]
                            = strdup(pv->name.value);
                        if (ast_function->vararg_param_count == 1) {
                            ast_function->vararg_param_index
                                = ast_function->parameterCount - 1;
                            if (ast_function->vararg_internal_name)
                                free(ast_function->vararg_internal_name);
                            ast_function->vararg_internal_name
                                = strdup(pv->name.value);
                        }
                        if (pv->total_bytes < 8) pv->total_bytes = 8;
                        if (pv->dim_count == 0) {
                            pv->dim_count = 1;
                            pv->dim_sizes = malloc(sizeof(int));
                            pv->dim_sizes[0] = -1;
                        }
                    }
                    continue;
                }
                if (temp->type == AST_VAR || temp->type == AST_UNINIT_VAR) {
                    AST_Var* pv = (AST_Var*)temp;
                    ast_function->has_varargs = true;
                    int idx = ast_function->vararg_param_count++;
                    ast_function->vararg_param_indices = realloc(
                        ast_function->vararg_param_indices,
                        sizeof(int) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_internal_names = realloc(
                        ast_function->vararg_internal_names,
                        sizeof(char*) * (size_t)ast_function->vararg_param_count);
                    ast_function->vararg_param_indices[idx]
                        = ast_function->parameterCount - 1;
                    ast_function->vararg_internal_names[idx]
                        = strdup(pv->name.value);
                    if (ast_function->vararg_param_count == 1) {
                        ast_function->vararg_param_index
                            = ast_function->parameterCount - 1;
                        if (ast_function->vararg_internal_name)
                            free(ast_function->vararg_internal_name);
                        ast_function->vararg_internal_name
                            = strdup(pv->name.value);
                    }
                }
                break;
            }
        } else {
            break;
        }
    }
    parser_eat(parser, TOKEN_PAREN_RIGHT);

    if (parser_current_is_type(parser, TOKEN_CONST)) {
        ast_function->is_const_method = true;
        parser_advance(parser);
    }

    ast_function->return_type = return_type;

    if (g_native_requested) {
        g_native_requested = false;
        if (ast_function->is_generic) {
            ERROR(ERR_PARSE_UNEXPECTED_TOKEN,
                  LOC(parser->filename, parser->current.line,
                      parser->current.column),
                  "native generic functions are not supported");
        }
        parser_eat(parser, TOKEN_SEMICOLON);
        ast_function->is_native = true;
        ast_function->return_Count = 1;
        ast_function->Return = calloc(1, sizeof(AST_Return));
        if (!ast_function->Return) OUT_OF_MEMORY();
        ast_function->Return->base.type = AST_RETURN;
        ast_function->Return->returnType = return_type;
        if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
            ast_function->ret_func_sig->multi_return = multi_return;
            multi_return = NULL;
        }
        ast_function->multi_return = multi_return;
        ast_function->base.type = AST_FUNCTION;
        ast_function->base.next = NULL;
        return (ASTNode*)ast_function;
    }

    if (ast_function->is_generic && return_type == DATA_VOID
    && ast_function->ret_func_sig == NULL
        && !ast_function->returns_array
        && ast_function->multi_return == NULL) {
        ast_function->return_type = DATA_IDENTIFIER;
        return_type = DATA_IDENTIFIER;
    }

    parser_eat(parser, TOKEN_BRACKET_LEFT);
    ast_function->Code = parser_parse_code_block(parser);
    parser_eat(parser, TOKEN_BRACKET_RIGHT);

    set_return_type_recursive(ast_function->Code, return_type);
    if (ast_function->is_generic && return_type == DATA_VOID
        && ast_function->ret_func_sig == NULL && !ast_function->returns_array
        && ast_function->multi_return == NULL) {
        ast_function->return_type = DATA_IDENTIFIER;
        return_type = DATA_IDENTIFIER;
        if (ast_function->type_param_count == 1) {
            ast_function->return_class_name = strdup(ast_function->type_params[0].name);
        }
    }

    ASTNode* scan = ast_function->Code;
    while (scan) {
        if (scan->type == AST_RETURN) {
            AST_Return* ret = (AST_Return*)scan;
            ast_function->Return = ret;
            ast_function->return_Count = 1;
            ret->returnType = return_type;
        }
        scan = scan->next;
    }

    if (ast_function->ret_func_sig != NULL && multi_return != NULL) {
        ast_function->ret_func_sig->multi_return = multi_return;
        multi_return = NULL;
    }
    ast_function->multi_return = multi_return;
    ast_function->base.type = AST_FUNCTION;
    ast_function->base.next = NULL;

    g_parse_type_params = saved_params;
    g_parse_type_param_count = saved_count;
    return (ASTNode*)ast_function;
}

void free_type_params(TypeParam* params, int count) {
    if (!params) return;
    for (int i = 0; i < count; i++) {
        free(params[i].name);
        if (params[i].constraints) {
            for (int j = 0; j < params[i].constraint_count; j++)
                free(params[i].constraints[j].class_name);
            free(params[i].constraints);
        }
    }
    free(params);
}

char* AST_Type_to_String(ASTType type)
{
    switch (type)
    {
    case AST_LITERAL:
        return "literal";
    case AST_BINARY:
        return "binary";
    case AST_PRINT:
        return "print";
    case AST_UNARY:
        return "unary";
    case AST_VAR:
        return "var";
    case AST_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

void Print_AST(ASTNode* node, int indent, bool after_sem) {
    while (node != NULL) {
        for (int i = 0; i < indent; i++) {
            printf("  ");
        }

        switch (node->type) {
        case AST_LITERAL: {
           AST_Literal* literal = (AST_Literal*)node;
           printf("LITERAL(type: %s,%s): %s\n",
                  after_sem ? data_type_to_string(literal->dataType) : type_to_string(literal->base.value.type),
                  literal->is_call_function ? "is call function" : "not a call function",
                  literal->base.value.value ? literal->base.value.value : "(null)");

           if (literal->is_call_function && literal->call_function_Parameter) {
               for (int i = 0; i < indent + 1; i++) printf("  ");
               printf("Call target:\n");
               Print_AST(literal->call_function_Parameter, indent + 2, after_sem);
           }

           if (literal->is_call_function && literal->method_arguments) {
               for (int i = 0; i < indent + 1; i++) printf("  ");
               printf("Method arguments (%d):\n", literal->method_arg_count);
               Print_AST(literal->method_arguments, indent + 2, after_sem);
           }
           break;
        }

        case AST_BINARY: {
            AST_Binary* binary = (AST_Binary*)node;
            printf("BINARY_EXPR(type: %s):\n",
                   after_sem ? data_type_to_string(binary->resultType) : "unknown");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Operator: %s (line %d, col %d)\n",
                   type_to_string(binary->op.type), binary->op.line, binary->op.column);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Left:\n");
            if (binary->left) Print_AST(binary->left, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Right:\n");
            if (binary->right) Print_AST(binary->right, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            break;
        }

        case AST_UNARY: {
            AST_Unary* unary = (AST_Unary*)node;
            if (after_sem) printf("UNARY_EXPR(type:%s):\n",data_type_to_string(unary->dataType));
            else printf("UNARY_EXPR(type:%s):\n",type_to_string(unary->base.type));
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Operator: %s\n", type_to_string(unary->op.type));
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Operand:\n");
            if (unary->right) Print_AST(unary->right, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            break;
        }

        case AST_CAST: {
            AST_Cast* cast = (AST_Cast*)node;
            printf("CAST(to %s", data_type_to_string(cast->target_type));
            for (int i = 0; i < cast->target_ptr_depth; i++) printf("*");
            if (cast->target_class_name) printf(" [%s]", cast->target_class_name);
            if (cast->target_func_sig) printf(" [fn ptr]");
            for (int i = 0; i < cast->target_dim_count; i++) {
                printf("[%d]", cast->target_dim_sizes[i]);
            }
            if (after_sem) {
                printf(", source: %s", data_type_to_string(cast->source_type));
                if (cast->source_func_sig) printf(" [fn ptr]");
                if (cast->is_reinterpret) printf(", reinterpret");
                if (cast->is_checked) printf(", checked");
            }
            printf("):\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Operand:\n");
            if (cast->operand) Print_AST(cast->operand, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            break;
        }

        case AST_STR_FROM_STRING: {
            AST_StrFromString* conv = (AST_StrFromString*)node;
            printf("STR_FROM_STRING (implicit conversion):\n");
            if (conv->source) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Source:\n");
                Print_AST(conv->source, indent + 2, after_sem);
            }
            break;
        }

        case AST_STR_SLICE: {
            AST_StrSlice* slice = (AST_StrSlice*)node;
            printf("STR_SLICE:\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Source:\n");
            if (slice->source) Print_AST(slice->source, indent + 2, after_sem);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Start:\n");
            if (slice->start) Print_AST(slice->start, indent + 2, after_sem);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("End:\n");
            if (slice->end) Print_AST(slice->end, indent + 2, after_sem);
            break;
        }

        case AST_CONDITIONAL: {
            AST_Conditional* tern = (AST_Conditional*)node;
            printf("CONDITIONAL(result: %s):\n",
                   after_sem ? data_type_to_string(tern->resultType) : "unknown");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Condition:\n");
            if (tern->condition) Print_AST(tern->condition, indent + 2, after_sem);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Then:\n");
            if (tern->then_expr) Print_AST(tern->then_expr, indent + 2, after_sem);
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Else:\n");
            if (tern->else_expr) Print_AST(tern->else_expr, indent + 2, after_sem);
            break;
        }

        case AST_UNINIT_VAR: {
            AST_Var* var = (AST_Var*)node;
            printf("UNINIT_VAR(type: %s):\n",
                   after_sem ? data_type_to_string(var->dataType) : type_to_string(var->type));
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("name: %s\n", var->name.value ? var->name.value : "(null)");
            if (var->dim_count > 0) {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("dims: [");
                for (int i = 0; i < var->dim_count; i++) {
                    if (i > 0) printf(", ");
                    printf("%d", var->dim_sizes ? var->dim_sizes[i] : -1);
                }
                printf("]\n");
            }
            break;
        }

        case AST_VAR:
        case AST_VAR_ASS: {
            AST_Var* var = (AST_Var*)node;
            printf("%s(type: %s):\n",
                   node->type == AST_VAR ? "VARIABLE" : "VAR_ASS",
                   after_sem ? data_type_to_string(var->dataType) : type_to_string(var->type));
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("name: %s\n", var->name.value ? var->name.value : "(null)");
            if (var->Value != NULL) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Value:\n");
                Print_AST(var->Value, indent + 2, after_sem);
            } else {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Value: (uninitialized)\n");
            }
            if (var->dim_count > 0) {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("dims: [");
                for (int i = 0; i < var->dim_count; i++) {
                    if (i > 0) printf(", ");
                    printf("%d", var->dim_sizes ? var->dim_sizes[i] : -1);
                }
                printf("]\n");
            }
            break;
        }

        case AST_PRINT: {
            AST_Print* print = (AST_Print*)node;
            printf("PRINT:\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Parameter:\n");
            if (print->Parameter) Print_AST(print->Parameter, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            break;
        }
        case AST_IF: {
            AST_If* ast_if = (AST_If*)node;
            printf("IF:\n");
            if (ast_if->Condition) Print_AST(ast_if->Condition, indent + 2, after_sem);
            if (ast_if->code) Print_AST(ast_if->code, indent + 2, after_sem);
            printf("else if(count: %d):\n", ast_if->elif_Count);
            for (int i = 0; i < ast_if->elif_Count; i++) {
                printf("the %d branch\n", i + 1);
                if (ast_if->elif_condition[i]) Print_AST(ast_if->elif_condition[i], indent + 2, after_sem);
                if (ast_if->Elif[i]) Print_AST(ast_if->Elif[i], indent + 2, after_sem);
            }
            printf("else:\n");
            if (ast_if->Else) Print_AST(ast_if->Else, indent + 2, after_sem);
            break;
        }

        case AST_WHILE: {
            AST_While* ast_while = (AST_While*)node;
            printf("WHILE:\n");
            if (ast_while->Condition) Print_AST(ast_while->Condition, indent + 2, after_sem);
            if (ast_while->code) Print_AST(ast_while->code, indent + 2, after_sem);
            break;
        }

        case AST_FOR: {
            AST_For* f = (AST_For*)node;
            printf("FOR:\n");
            if (f->init)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Init:\n");
                Print_AST(f->init, indent+2, after_sem);
            }
            if (f->condition)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Condition:\n");
                Print_AST(f->condition, indent+2, after_sem);
            }
            if (f->increment)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Increment:\n");
                Print_AST(f->increment, indent+2, after_sem);
            }
            if (f->code)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Body:\n");
                Print_AST(f->code, indent+2, after_sem);
            }
            break;
        }

        case AST_FOREACH: {
            AST_ForEach* fe = (AST_ForEach*)node;
            printf("FOREACH (iterator: %s):\n", fe->iterator.value);
            if (after_sem) {
                printf(", element type: %s", data_type_to_string(fe->elemType));
                if (fe->has_explicit_type)
                    printf(" [explicit]");
            }
            if (fe->array)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Array:\n");
                Print_AST(fe->array, indent+2, after_sem);
            }
            if (fe->code)
            {
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Body:\n");
                Print_AST(fe->code, indent+2, after_sem);
            }
            break;
        }

        case AST_SWITCH: {
            AST_Switch* sw = (AST_Switch*)node;
            printf("SWITCH:\n");
            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Condition:\n");
            if (sw->condition) Print_AST(sw->condition, indent + 2, after_sem);

            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Cases (%d):\n", sw->case_count);
            for (int i = 0; i < sw->case_count; i++) {
                AST_Case* cs = sw->cases[i];
                for (int j = 0; j < indent + 2; j++) printf("  ");
                printf("Case %d:\n", i + 1);

                if (!cs->is_default && cs->value) {
                    for (int j = 0; j < indent + 3; j++) printf("  ");
                    printf("Value:\n");
                    Print_AST(cs->value, indent + 4, after_sem);
                } else if (cs->is_default) {
                    for (int j = 0; j < indent + 3; j++) printf("  ");
                    printf("(default)\n");
                }

                if (cs->body) {
                    for (int j = 0; j < indent + 3; j++) printf("  ");
                    printf("Body:\n");
                    Print_AST(cs->body, indent + 4, after_sem);
                }
            }

            if (sw->has_default && sw->default_body) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Default:\n");
                Print_AST(sw->default_body, indent + 2, after_sem);
            }
            break;
        }

        case AST_CASE: {
            AST_Case* cs = (AST_Case*)node;
            printf("CASE%s:\n", cs->is_default ? " (default)" : "");
            if (!cs->is_default && cs->value) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Value:\n");
                Print_AST(cs->value, indent + 2, after_sem);
            }
            if (cs->body) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Body:\n");
                Print_AST(cs->body, indent + 2, after_sem);
            }
            break;
        }

        case AST_BREAK: {
            AST_Break* brk = (AST_Break*)node;
            printf("BREAK (line %d, col %d)\n",
                   brk->base.value.line, brk->base.value.column);
            break;
        }

        case AST_CONTINUE: {
            AST_Continue* cont = (AST_Continue*)node;
            printf("CONTINUE (line %d, col %d)\n",
                   cont->base.value.line, cont->base.value.column);
            break;
        }

        case AST_FUNCTION:{
            AST_Function* ast_function = (AST_Function*)node;
            printf("FUNCTION(%s%s%s):\n",
                   ast_function->name.value,
                   ast_function->is_virtual ? " [virtual]" : "",
                   ast_function->is_override ? " [override]" : "");
            if (ast_function->is_generic && ast_function->type_param_count > 0) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("TypeParams: <");
                for (int i = 0; i < ast_function->type_param_count; i++) {
                    if (i > 0) printf(", ");
                    TypeParam* tp = &ast_function->type_params[i];
                    printf("%s", tp->name);
                    for (int j = 0; j < tp->constraint_count; j++) {
                        printf("%s %s",
                               tp->constraints[j].kind == CONSTRAINT_SUPERTYPE
                                   ? " >:" : " :",
                               tp->constraints[j].class_name);
                    }
                }
                printf(">\n");
            }
            if (ast_function->Parameter != NULL) {
                printf("parameters:\n");
                Print_AST(ast_function->Parameter, indent + 2, after_sem);
            } else {
                printf("no have parameter\n");
            }
            printf("code:\n");
            if (ast_function->Code) Print_AST(ast_function->Code, indent + 2, after_sem);
            else { for (int i = 0; i < indent+2;i++) printf("  "); printf("(NULL)\n"); }
            if (ast_function->multi_return_node != NULL) {
                printf("multi_return:\n");
                Print_AST((ASTNode*)ast_function->multi_return_node, indent + 2, after_sem);
            } else if (ast_function->Return != NULL) {
                printf("return:\n");
                Print_AST((ASTNode*)ast_function->Return, indent + 2, after_sem);
            } else {
                printf("no have return\n");
            }
            break;
        }

        case AST_VARARG: {
            AST_Vararg* va = (AST_Vararg*)node;
            printf("VARARG(name: %s)\n", va->name.value ? va->name.value : "(unnamed)");
            break;
        }

        case AST_CLASS_DEF: {
            AST_ClassDef* cls = (AST_ClassDef*)node;
            printf("CLASS_DEF (name: %s, modifier: %s",
                   cls->name ? cls->name : "(null)",
                   cls->modifier == 0 ? "pub" : cls->modifier == 1 ? "pro" : "pri");
            if (cls->parent_count > 0) {
                printf(", parents: [");
                for (int i = 0; i < cls->parent_count; i++) {
                    if (i > 0) printf(", ");
                    printf("%s", cls->parent_names[i]);
                }
                printf("]");
            }
            printf(")");
            if (cls->is_generic && cls->type_param_count > 0) {
                printf(" <");
                for (int i = 0; i < cls->type_param_count; i++) {
                    if (i > 0) printf(", ");
                    TypeParam* tp = &cls->type_params[i];
                    printf("%s", tp->name);
                    for (int j = 0; j < tp->constraint_count; j++) {
                        printf("%s %s",
                               tp->constraints[j].kind == CONSTRAINT_SUPERTYPE
                                   ? " >:" : " :",
                               tp->constraints[j].class_name);
                    }
                }
                printf(">");
            }
            printf(":\n");
            if (cls->members) {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("Members:\n");
                Print_AST(cls->members, indent + 2, after_sem);
            } else {
                for (int i = 0; i < indent + 1; i++) printf("  ");
                printf("(no members)\n");
            }
            break;
        }

        case AST_NEW_INSTANCE: {
            AST_NewInstance* ni = (AST_NewInstance*)node;
            printf("NEW_INSTANCE (class: %s, size: %d)\n",
                   ni->class_name ? ni->class_name : "(null)",
                   after_sem ? ni->instance_size : -1);
            break;
        }

        case AST_CLASS_MEMBER: {
            printf("class_member");
            break;
        }

        case AST_THIS: {
            printf("this\n");
            break;
        }

        case AST_MEMBER_ACCESS: {
            AST_MemberAccess* ma = (AST_MemberAccess*)node;
            printf("MEMBER_ACCESS (member: %s", ma->member_name == NULL ? "(null)" : ma->member_name);
            if (after_sem) {
                printf(", type: %s", data_type_to_string(ma->member_type));
                if (ma->is_method) {
                    printf(", ret: %s", data_type_to_string(ma->member_return_type));
                    printf(", method: true");
                } else {
                    printf(", offset: %d", ma->member_offset);
                }
            }
            printf("):\n");

            for (int i = 0; i < indent + 1; i++) printf("  ");
            printf("Object:\n");
            if (ma->object) {
                Print_AST(ma->object, indent + 2, after_sem);
            } else {
                for (int i = 0; i < indent + 2; i++) printf("  ");
                printf("(NULL)\n");
            }
            break;
        }

        case AST_MEMBER_ASS: {
            AST_MemberAss* ass = (AST_MemberAss*)node;
            printf("MEMBER_ASS (member: %s", ass->member_name);
            if (after_sem) {
                printf(", type: %s, offset: %d", data_type_to_string(ass->member_type), ass->member_offset);
            }
            printf("):\n");
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Object:\n");
            if (ass->object) Print_AST(ass->object, indent+2, after_sem);
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Value:\n");
            if (ass->value) Print_AST(ass->value, indent+2, after_sem);
            break;
        }

        case AST_RETURN:{
            AST_Return* ast_return = (AST_Return*)node;
            printf("RETURN(type:%s):\n", data_type_to_string(ast_return->returnType));
            if (ast_return->value) Print_AST(ast_return->value, indent + 2, after_sem);
            break;
        }

        case AST_ARRAY_DECL: {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)node;
            printf("ARRAY_DECL(type: %s, dims: [",
                   after_sem ? data_type_to_string(arr->elementType) : "unknown");
            for (int i = 0; i < arr->dim_count; i++) {
                if (i > 0) printf(", ");
                printf("%d", arr->dim_sizes[i]);
            }
            printf("]):\n");
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("name: %s\n", arr->name ? arr->name : "(null)");
            break;
        }

        case AST_ARRAY_ACCESS: {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
            printf("ARRAY_ACCESS(type: %s, indices:%d):\n",
                   after_sem ? data_type_to_string(acc->elementType) : "unknown",
                   acc->index_count);
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Array:\n");
            if (acc->array) Print_AST(acc->array, indent+2, after_sem);
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Indices:\n");
            for (int i = 0; i < acc->index_count; i++) {
                for (int j = 0; j < indent+2; j++) printf("  ");
                printf("Index[%d]:\n", i);
                if (acc->indices[i]) {
                    Print_AST(acc->indices[i], indent+3, after_sem);
                } else {
                    for (int j = 0; j < indent+3; j++) printf("  ");
                    printf("(NULL)\n");
                }
            }
            break;
        }

        case AST_ARRAY_ASS: {
            AST_ArrayAss* ass = (AST_ArrayAss*)node;
            printf("ARRAY_ASS(type: %s):\n",
                   after_sem ? data_type_to_string(ass->elementType) : "unknown");
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Target:\n");
            if (ass->target) Print_AST(ass->target, indent+2, after_sem);
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Value:\n");
            if (ass->value) Print_AST(ass->value, indent+2, after_sem);
            break;
        }

        case AST_NEW_ARRAY: {
                AST_NewArray* narr = (AST_NewArray*)node;
                printf("NEW_ARRAY(type: %s, dims: [",
                       data_type_to_string(narr->elementType));
                for (int i = 0; i < narr->dim_count; i++) {
                    if (i > 0) printf(", ");
                    printf("%d", narr->dim_sizes[i]);
                }
                printf("])\n");
                break;
        }

        case AST_INIT_LIST: {
                AST_InitList* list = (AST_InitList*)node;
                printf("INIT_LIST(count:%d, dims:", list->count);
                if (list->dim_sizes) {
                    for (int i=0; i<list->dim_count; i++) printf("%d,", list->dim_sizes[i]);
                } else printf("null");
                printf("):\n");
                for (int i=0; i<list->count; i++) {
                    for (int j=0;j<indent+1;j++) printf("  ");
                    printf("Element[%d]:\n", i);
                    if (list->elements[i]) Print_AST(list->elements[i], indent+2, after_sem);
                }
                break;
        }

        case AST_ARRAY_LENGTH: {
                AST_ArrayLength* len = (AST_ArrayLength*)node;
                printf("ARRAY_LENGTH:\n");
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Array:\n");
                if (len->array) Print_AST(len->array, indent+2, after_sem);
                break;
        }

        case AST_ENUM_DEF: {
                AST_EnumDef* ed = (AST_EnumDef*)node;
                printf("ENUM_DEF(name: %s, members: %d):\n",
                       ed->name ? ed->name : "(anonymous)", ed->member_count);
                for (int i = 0; i < ed->member_count; i++) {
                    for (int j = 0; j < indent + 1; j++) printf("  ");
                    printf("%s", ed->members[i]->name);
                    if (ed->members[i]->has_explicit_value) {
                        printf(" = %d", ed->members[i]->value);
                    }
                    if (after_sem) {
                        printf("  [value: %d]", ed->members[i]->value);
                    }
                    printf("\n");
                }
                break;
        }

        case AST_ENUM_MEMBER: {
                AST_EnumMember* em = (AST_EnumMember*)node;
                printf("ENUM_MEMBER(%s = %d)\n", em->name, em->value);
                break;
        }

        case AST_DEREF: {
                AST_Deref* d = (AST_Deref*)node;
                printf("DEREF(ptr_depth:%d, type:%s):\n",
                       d->ptr_depth, data_type_to_string(d->resultType));
                for (int i = 0; i < indent+1; i++) printf("  ");
                printf("Operand:\n");
                if (d->operand) Print_AST(d->operand, indent+2, after_sem);
                break;
        }

        case AST_ADDRESS_OF: {
            AST_AddressOf* addr = (AST_AddressOf*)node;
            printf("ADDRESS_OF(ptr_depth:%d):\n", addr->ptr_depth);
            for (int i = 0; i < indent+1; i++) printf("  ");
            printf("Operand:\n");
            if (addr->operand) Print_AST(addr->operand, indent+2, after_sem);
            break;
        }

        case AST_NULL: {
            printf("NULL_LITERAL\n");
            break;
        }

        case AST_VARARG_GROUP: {
            AST_VarargGroup* vg = (AST_VarargGroup*)node;
            printf("VARARG_GROUP(count:%d):\n", vg->count);
            ASTNode* elem = vg->elements;
            int i = 0;
            while (elem) {
                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("Elem[%d]:\n", i++);
                Print_AST(elem, indent + 2, after_sem);
                elem = elem->next;
            }
            break;
        }

        case AST_MULTI_RETURN: {
            AST_MultiReturn* mr = (AST_MultiReturn*)node;
            printf("MULTI_RETURN(count=%d):\n", mr->value_count);
            for (int i = 0; i < mr->value_count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("[%d]:\n", i);
                if (mr->values[i]) Print_AST(mr->values[i], indent + 2, after_sem);
            }
            break;
        }
        case AST_RETURN_EXTRACT: {
            AST_ReturnExtract* e = (AST_ReturnExtract*)node;
            printf("RETURN_EXTRACT(idx=%d, sub=%d):\n", e->index, e->sub_index);
            if (e->object) Print_AST(e->object, indent + 1, after_sem);
            break;
        }

        case AST_TRY: {
            AST_Try* tn = (AST_Try*)node;
            printf("TRY:\n");
            if (tn->code) Print_AST(tn->code, indent + 2, after_sem);
            for (int i = 0; i < tn->catch_count; i++) {
                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("CATCH (%s %s):\n",
                       tn->catches[i].type_name ? tn->catches[i].type_name : "(null)",
                       tn->catches[i].var_name.value ? tn->catches[i].var_name.value : "(null)");
                if (tn->catches[i].body) Print_AST(tn->catches[i].body, indent + 2, after_sem);
            }
            if (tn->has_finally) {
                for (int j = 0; j < indent + 1; j++) printf("  ");
                printf("FINALLY:\n");
                if (tn->finally_code) Print_AST(tn->finally_code, indent + 2, after_sem);
            }
            break;
        }

        case AST_THROW: {
            AST_Throw* th = (AST_Throw*)node;
            printf("THROW:\n");
            if (th->value) Print_AST(th->value, indent + 2, after_sem);
            break;
        }

        case AST_ERROR:
            printf("ERROR NODE\n");
            break;

        default:
            printf("Unknown AST type: %d\n", node->type);
            break;
        }

        node = node->next;
    }
}

static void free_func_sig(FuncSignature* sig) {
    if (!sig) return;
    free(sig->return_class_name);
    free(sig->param_types);
    free(sig->param_type_param_indices);
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

static void free_func_sig_recursive(FuncSignature* sig) {
    if (!sig) return;
    free(sig->return_class_name);
    free(sig->param_types);
    free(sig->param_type_param_indices);
    free(sig->param_pointee_const);
    if (sig->param_func_sigs) {
        for (int i = 0; i < sig->param_count; i++) {
            if (sig->param_func_sigs[i]) {
                free_func_sig_recursive(sig->param_func_sigs[i]);
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
                free_func_sig_recursive(mrt->func_sigs[i]);
            free(mrt->func_sigs);
        }
        free(mrt);
    }

    free_func_sig_recursive(sig->ret_func_sig);

    free(sig);
}

void AST_Free(ASTNode* node)
{
    while (node != NULL) {
        ASTNode* next = node->next;
        switch (node->type) {

        case AST_LITERAL: {
            AST_Literal* lit = (AST_Literal*)node;
            free(lit->internal_name);
            lit->internal_name = NULL;
            free(lit->call_result_class_name);
            lit->call_result_class_name = NULL;
            if (lit->call_function_Parameter && lit->method_arguments &&
                lit->call_function_Parameter == lit->method_arguments) {
                AST_Free(lit->call_function_Parameter);
                lit->call_function_Parameter = NULL;
                lit->method_arguments = NULL;
            } else {
                if (lit->call_function_Parameter) {
                    AST_Free(lit->call_function_Parameter);
                    lit->call_function_Parameter = NULL;
                }
                if (lit->method_arguments) {
                    AST_Free(lit->method_arguments);
                    lit->method_arguments = NULL;
                }
            }
            if (lit->multi_ret_buf) {
                free(lit->multi_ret_buf);
                lit->multi_ret_buf = NULL;
            }
            if (lit->indirect_callee) {
                AST_Free(lit->indirect_callee);
                lit->indirect_callee = NULL;
            }
            if (lit->returned_func_sig) {
                free_func_sig(lit->returned_func_sig);
                lit->returned_func_sig = NULL;
            }
            if (lit->generic_type_args) {
                free(lit->generic_type_args);
                lit->generic_type_args = NULL;
            }
            if (lit->generic_type_arg_names) {
                for (int i = 0; i < lit->generic_type_arg_count; i++)
                    free(lit->generic_type_arg_names[i]);
                free(lit->generic_type_arg_names);
                lit->generic_type_arg_names = NULL;
            }
            if (lit->param_byte_sizes) {
                free(lit->param_byte_sizes);
                lit->param_byte_sizes = NULL;
            }
            lit->multi_return_type = NULL;
            break;
        }

        case AST_BINARY: {
            AST_Binary* bin = (AST_Binary*)node;
            if (bin->left) { AST_Free(bin->left); bin->left  = NULL; }
            if (bin->right) { AST_Free(bin->right); bin->right = NULL; }
            break;
        }

        case AST_UNARY: {
            AST_Unary* un = (AST_Unary*)node;
            if (un->right) { AST_Free(un->right); un->right = NULL; }
            break;
        }

        case AST_CAST: {
            AST_Cast* cast = (AST_Cast*)node;
            if (cast->operand) { AST_Free(cast->operand); cast->operand = NULL; }
            if (cast->target_class_name) { free(cast->target_class_name); cast->target_class_name = NULL; }
            if (cast->target_func_sig) { free_func_sig_recursive(cast->target_func_sig); cast->target_func_sig = NULL; }
            if (cast->target_dim_sizes) { free(cast->target_dim_sizes); cast->target_dim_sizes = NULL; }
            if (cast->source_func_sig) { free_func_sig_recursive(cast->source_func_sig); cast->source_func_sig = NULL; }
            break;
        }

        case AST_STR_FROM_STRING: {
            AST_StrFromString* conv = (AST_StrFromString*)node;
            if (conv->source) { AST_Free(conv->source); conv->source = NULL; }
            break;
        }

        case AST_STR_SLICE: {
            AST_StrSlice* slice = (AST_StrSlice*)node;
            if (slice->source) { AST_Free(slice->source); slice->source = NULL; }
            if (slice->start)  { AST_Free(slice->start);  slice->start = NULL; }
            if (slice->end)    { AST_Free(slice->end);    slice->end = NULL; }
            break;
        }


        case AST_CONDITIONAL: {
            AST_Conditional* tern = (AST_Conditional*)node;
            if (tern->condition) { AST_Free(tern->condition); tern->condition = NULL; }
            if (tern->then_expr) { AST_Free(tern->then_expr); tern->then_expr = NULL; }
            if (tern->else_expr) { AST_Free(tern->else_expr); tern->else_expr = NULL; }
            break;
        }

        case AST_VAR:
        case AST_VAR_ASS:
        case AST_UNINIT_VAR: {
            AST_Var* var = (AST_Var*)node;
            if (var->internal_name) { free(var->internal_name); var->internal_name = NULL; }
            if (var->static_storage_name) { free(var->static_storage_name); var->static_storage_name = NULL; }
            if (var->dim_sizes) { free(var->dim_sizes); var->dim_sizes = NULL; }
            if (var->Value) { AST_Free(var->Value); var->Value = NULL; }
            if (var->deref_target) { AST_Free(var->deref_target); var->deref_target = NULL; }
            if (var->dim_exprs) {
                for (int i = 0; i < var->dim_count; i++)
                    if (var->dim_exprs[i]) AST_Free(var->dim_exprs[i]);
                free(var->dim_exprs);
                var->dim_exprs = NULL;
            }
            if (var->func_ptr_sig) {
                free(var->func_ptr_sig->param_types);
                free(var->func_ptr_sig->param_type_param_indices);
                free(var->func_ptr_sig->vararg_param_indices);
                free(var->func_ptr_sig);
                var->func_ptr_sig = NULL;
            }
            if (var->ptr_array_info) {
                free(var->ptr_array_info->dim_sizes);
                free(var->ptr_array_info);
                var->ptr_array_info = NULL;
            }
            if (var->generic_type_args) {
                free(var->generic_type_args);
                var->generic_type_args = NULL;
            }
            if (var->generic_type_arg_names) {
                for (int gni = 0; gni < var->generic_type_arg_count; gni++)
                    free(var->generic_type_arg_names[gni]);
                free(var->generic_type_arg_names);
                var->generic_type_arg_names = NULL;
            }
            break;
        }

        case AST_PRINT: {
            AST_Print* prt = (AST_Print*)node;
            if (prt->Parameter) { AST_Free(prt->Parameter); prt->Parameter = NULL; }
            break;
        }

        case AST_INPUT: {
            AST_Input* inp = (AST_Input*)node;
            if (inp->format_string) { AST_Free(inp->format_string); inp->format_string = NULL; }
            if (inp->args) {
                for (int i = 0; i < inp->arg_count; i++) {
                    if (inp->args[i]) AST_Free(inp->args[i]);
                }
                free(inp->args);
                inp->args = NULL;
            }
            break;
        }

        case AST_IF: {
            AST_If* ifn = (AST_If*)node;
            if (ifn->Condition) { AST_Free(ifn->Condition); ifn->Condition = NULL; }
            if (ifn->code) { AST_Free(ifn->code); ifn->code = NULL; }
            if (ifn->Else) { AST_Free(ifn->Else); ifn->Else = NULL; }
            if (ifn->elif_condition) {
                for (int i = 0; i < ifn->elif_Count; i++) {
                    if (ifn->elif_condition[i]) AST_Free(ifn->elif_condition[i]);
                }
                free(ifn->elif_condition);
                ifn->elif_condition = NULL;
            }
            if (ifn->Elif) {
                for (int i = 0; i < ifn->elif_Count; i++) {
                    if (ifn->Elif[i]) AST_Free(ifn->Elif[i]);
                }
                free(ifn->Elif);
                ifn->Elif = NULL;
            }
            break;
        }

        case AST_SWITCH: {
            AST_Switch* sw = (AST_Switch*)node;
            if (sw->condition) { AST_Free(sw->condition); sw->condition = NULL; }
            if (sw->cases) {
                for (int i = 0; i < sw->case_count; i++) {
                    if (sw->cases[i]) {
                        if (sw->cases[i]->value) AST_Free(sw->cases[i]->value);
                        if (sw->cases[i]->body)  AST_Free(sw->cases[i]->body);
                        free(sw->cases[i]);
                    }
                }
                free(sw->cases);
                sw->cases = NULL;
            }
            if (sw->default_body) { AST_Free(sw->default_body); sw->default_body = NULL; }
            break;
        }

        case AST_CASE: {
            AST_Case* cs = (AST_Case*)node;
            if (cs->value) { AST_Free(cs->value); cs->value = NULL; }
            if (cs->body) { AST_Free(cs->body); cs->body = NULL; }
            break;
        }

        case AST_BREAK:
        case AST_CONTINUE:
            break;

        case AST_WHILE: {
            AST_While* w = (AST_While*)node;
            if (w->Condition) { AST_Free(w->Condition); w->Condition = NULL; }
            if (w->code) { AST_Free(w->code); w->code = NULL; }
            break;
        }

        case AST_FOR: {
            AST_For* f = (AST_For*)node;
            if (f->init) { AST_Free(f->init); f->init = NULL; }
            if (f->condition) { AST_Free(f->condition); f->condition = NULL; }
            if (f->increment) { AST_Free(f->increment); f->increment = NULL; }
            if (f->code) { AST_Free(f->code); f->code = NULL; }
            break;
        }

        case AST_FOREACH: {
            AST_ForEach* fe = (AST_ForEach*)node;
            if (fe->array) { AST_Free(fe->array); fe->array = NULL; }
            if (fe->code) { AST_Free(fe->code);  fe->code = NULL; }
            if (fe->internal_iterator) { free(fe->internal_iterator); fe->internal_iterator = NULL; }
            if (fe->container_class_name) { free(fe->container_class_name); fe->container_class_name = NULL; }
            if (fe->container_size_method) { free(fe->container_size_method); fe->container_size_method = NULL; }
            if (fe->container_get_method) { free(fe->container_get_method); fe->container_get_method = NULL; }
            break;
        }

        case AST_FUNCTION: {
            AST_Function* func = (AST_Function*)node;
            if (func->internal_name) { free(func->internal_name); func->internal_name = NULL; }
            if (func->Parameter) { AST_Free(func->Parameter); func->Parameter     = NULL; }
            if (func->Code) { AST_Free(func->Code); func->Code = NULL; }
            if (func->parameterType) { free(func->parameterType); func->parameterType = NULL; }
            if (func->return_dim_exprs) {
                for (int i = 0; i < func->return_dim_count; i++)
                    if (func->return_dim_exprs[i]) AST_Free(func->return_dim_exprs[i]);
                free(func->return_dim_exprs);
                func->return_dim_exprs = NULL;
            }
            if (func->ret_func_sig) {
                free_func_sig_recursive(func->ret_func_sig);
                func->ret_func_sig = NULL;
            }
            if (func->vararg_internal_name) { free(func->vararg_internal_name); func->vararg_internal_name = NULL; }
            if (func->vararg_param_indices) { free(func->vararg_param_indices); }
            if (func->vararg_internal_names) {
                for (int i = 0; i < func->vararg_param_count; i++)
                    free(func->vararg_internal_names[i]);
                free(func->vararg_internal_names);
            }
            if (func->type_params) {
                free_type_params(func->type_params, func->type_param_count);
                func->type_params = NULL;
            }
            break;
        }

        case AST_VARARG: {
            AST_Vararg* va = (AST_Vararg*)node;
            if (va->name.value) { free(va->name.value); va->name.value = NULL; }
            if (va->internal_name) { free(va->internal_name); va->internal_name = NULL; }
            break;
        }

        case AST_RETURN: {
            AST_Return* ret = (AST_Return*)node;
            if (ret->value) { AST_Free(ret->value); ret->value = NULL; }
            break;
        }

        case AST_CLASS_DEF: {
            AST_ClassDef* cls = (AST_ClassDef*)node;
            if (cls->name) { free(cls->name); cls->name = NULL; }
            if (cls->parent_name) { free(cls->parent_name); cls->parent_name = NULL; }
            if (cls->parent_names) {
                for (int i = 0; i < cls->parent_count; i++) {
                    free(cls->parent_names[i]);
                }
                free(cls->parent_names);
                cls->parent_names = NULL;
            }
            if (cls->type_params) {
                free_type_params(cls->type_params, cls->type_param_count);
                cls->type_params = NULL;
            }
            if (cls->members) { AST_Free(cls->members); cls->members = NULL; }
            break;
        }

        case AST_NEW_INSTANCE: {
            AST_NewInstance* ni = (AST_NewInstance*)node;
            if (ni->class_name) { free(ni->class_name); ni->class_name = NULL; }
            if (ni->constructor_name) { free(ni->constructor_name); ni->constructor_name = NULL; }
            if (ni->arguments) { AST_Free(ni->arguments); ni->arguments = NULL; }
            if (ni->field_inits) {
                for (int i = 0; i < ni->field_init_count; i++) {
                    if (ni->field_inits[i].value_str)
                        free(ni->field_inits[i].value_str);
                }
                free(ni->field_inits);
                ni->field_inits = NULL;
            }
            if (ni->generic_type_args) {
                free(ni->generic_type_args);
                ni->generic_type_args = NULL;
            }
            if (ni->generic_type_arg_names) {
                for (int gni = 0; gni < ni->generic_type_arg_count; gni++)
                    free(ni->generic_type_arg_names[gni]);
                free(ni->generic_type_arg_names);
                ni->generic_type_arg_names = NULL;
            }
            if (ni->vtable_label) {
                free(ni->vtable_label);
                ni->vtable_label = NULL;
            }
            break;
        }

        case AST_MEMBER_ACCESS: {
            AST_MemberAccess* ma = (AST_MemberAccess*)node;
            if (ma->member_name) { free(ma->member_name); ma->member_name = NULL; }
            if (ma->object) { AST_Free(ma->object); ma->object = NULL; }
            if (ma->method_internal) { free(ma->method_internal); ma->method_internal = NULL; }
            if (ma->method_param_types) { free(ma->method_param_types); ma->method_param_types = NULL; }
            if (ma->const_value_str) { free(ma->const_value_str); ma->const_value_str = NULL; }
            break;
        }

        case AST_MEMBER_ASS: {
            AST_MemberAss* mass = (AST_MemberAss*)node;
            if (mass->member_name) { free(mass->member_name); mass->member_name = NULL; }
            if (mass->object) { AST_Free(mass->object); mass->object = NULL; }
            if (mass->value) { AST_Free(mass->value); mass->value = NULL; }
            break;
        }

        case AST_THIS:
            break;

        case AST_ARRAY_DECL: {
            AST_ArrayDecl* arr = (AST_ArrayDecl*)node;
            if (arr->name) { free(arr->name); arr->name = NULL; }
            if (arr->element_class_name) { free(arr->element_class_name); arr->element_class_name = NULL; }
            if (arr->internal_name) { free(arr->internal_name); arr->internal_name = NULL; }
            if (arr->dim_sizes) { free(arr->dim_sizes); arr->dim_sizes = NULL; }
            if (arr->init_list) { AST_Free(arr->init_list); arr->init_list = NULL; }
            if (arr->dim_exprs) {
                for (int i = 0; i < arr->dim_count; i++)
                    if (arr->dim_exprs[i]) AST_Free(arr->dim_exprs[i]);
                free(arr->dim_exprs);
                arr->dim_exprs = NULL;
            }
            break;
        }

        case AST_ARRAY_ACCESS: {
            AST_ArrayAccess* acc = (AST_ArrayAccess*)node;
            if (acc->array) { AST_Free(acc->array); acc->array = NULL; }
            if (acc->indices) {
                for (int i = 0; i < acc->index_count; i++) {
                    if (acc->indices[i]) AST_Free(acc->indices[i]);
                }
                free(acc->indices);
                acc->indices = NULL;
            }
            if (acc->dim_sizes) { free(acc->dim_sizes); acc->dim_sizes = NULL; }
            if (acc->full_dim_sizes) { free(acc->full_dim_sizes); acc->full_dim_sizes = NULL; }
            if (acc->func_sig) {
                free(acc->func_sig->param_types);
                free(acc->func_sig->vararg_param_indices);
                free(acc->func_sig);
                acc->func_sig = NULL;
            }
            break;
        }

        case AST_ARRAY_ASS: {
            AST_ArrayAss* ass = (AST_ArrayAss*)node;
            if (ass->target) { AST_Free(ass->target); ass->target = NULL; }
            if (ass->value) { AST_Free(ass->value); ass->value = NULL; }
            break;
        }

        case AST_NEW_ARRAY: {
            AST_NewArray* narr = (AST_NewArray*)node;
            if (narr->element_class_name) { free(narr->element_class_name); narr->element_class_name = NULL; }
            if (narr->dim_sizes) { free(narr->dim_sizes); narr->dim_sizes = NULL; }
            if (narr->dim_exprs) {
                for (int i = 0; i < narr->dim_count; i++) {
                    if (narr->dim_exprs[i]) AST_Free(narr->dim_exprs[i]);
                }
                free(narr->dim_exprs);
                narr->dim_exprs = NULL;
            }
            break;
        }

        case AST_INIT_LIST: {
            AST_InitList* list = (AST_InitList*)node;
            if (list->elements) {
                for (int i = 0; i < list->count; i++) {
                    if (list->elements[i]) AST_Free(list->elements[i]);
                }
                free(list->elements);
                list->elements = NULL;
            }
            if (list->dim_sizes) { free(list->dim_sizes); list->dim_sizes = NULL; }
            break;
        }

        case AST_ARRAY_LENGTH: {
            AST_ArrayLength* alen = (AST_ArrayLength*)node;
            if (alen->array) { AST_Free(alen->array); alen->array = NULL; }
            break;
        }

        case AST_SUBARRAY: {
            AST_SubArray* sub = (AST_SubArray*)node;
            if (sub->array) { AST_Free(sub->array); sub->array = NULL; }
            if (sub->dim_sizes) { free(sub->dim_sizes); sub->dim_sizes = NULL; }
            if (sub->base_temp) { free(sub->base_temp); sub->base_temp = NULL; }
            break;
        }

        case AST_ENUM_DEF: {
            AST_EnumDef* ed = (AST_EnumDef*)node;
            if (ed->name) { free(ed->name); ed->name = NULL; }
            if (ed->members) {
                for (int i = 0; i < ed->member_count; i++) {
                    if (ed->members[i]) {
                        if (ed->members[i]->name) free(ed->members[i]->name);
                        if (ed->members[i]->explicit_token) {
                            if (ed->members[i]->explicit_token->value)
                                free(ed->members[i]->explicit_token->value);
                            free(ed->members[i]->explicit_token);
                        }
                        free(ed->members[i]);
                    }
                }
                free(ed->members);
                ed->members = NULL;
            }
            break;
        }

        case AST_ENUM_MEMBER: {
            AST_EnumMember* em = (AST_EnumMember*)node;
            if (em->name) { free(em->name); em->name = NULL; }
            if (em->explicit_token) {
                if (em->explicit_token->value) free(em->explicit_token->value);
                free(em->explicit_token);
                em->explicit_token = NULL;
            }
            break;
        }

        case AST_DEREF: {
            AST_Deref* d = (AST_Deref*)node;
            if (d->operand) { AST_Free(d->operand); d->operand = NULL; }
            if (d->deref_array_info) {
                free(d->deref_array_info->dim_sizes);
                free(d->deref_array_info);
                d->deref_array_info = NULL;
            }
            break;
        }

        case AST_ADDRESS_OF: {
            AST_AddressOf* addr = (AST_AddressOf*)node;
            if (addr->operand) { AST_Free(addr->operand); addr->operand = NULL; }
            if (addr->const_value_str) { free(addr->const_value_str); addr->const_value_str = NULL; }
            if (addr->target_func_sig) {
                free(addr->target_func_sig->param_types);
                free(addr->target_func_sig);
                addr->target_func_sig = NULL;
            }
            break;
        }

        case AST_VARARG_GROUP:
            AST_VarargGroup* vg = (AST_VarargGroup*)node;
            if (vg->elements) { AST_Free(vg->elements); vg->elements = NULL; }
            break;

        case AST_MULTI_RETURN: {
            AST_MultiReturn* mr = (AST_MultiReturn*)node;
            if (mr->values) {
                for (int i = 0; i < mr->value_count; i++)
                    if (mr->values[i]) AST_Free(mr->values[i]);
                free(mr->values);
            }
            break;
        }

        case AST_RETURN_EXTRACT: {
            AST_ReturnExtract* e = (AST_ReturnExtract*)node;
            if (e->object) AST_Free(e->object);
            if (e->extracted_func_sig) {
                free(e->extracted_func_sig->param_types);
                free(e->extracted_func_sig);
                e->extracted_func_sig = NULL;
            }
            break;
        }

        case AST_TRY: {
            AST_Try* tn = (AST_Try*)node;
            if (tn->code) { AST_Free(tn->code); tn->code = NULL; }
            for (int i = 0; i < tn->catch_count; i++) {
                free(tn->catches[i].type_name);
                tn->catches[i].type_name = NULL;
                free(tn->catches[i].internal_name);
                tn->catches[i].internal_name = NULL;
                if (tn->catches[i].body) AST_Free(tn->catches[i].body);
                tn->catches[i].body = NULL;
            }
            free(tn->catches);
            tn->catches = NULL;
            if (tn->finally_code) { AST_Free(tn->finally_code); tn->finally_code = NULL; }
            break;
        }

        case AST_THROW: {
            AST_Throw* th = (AST_Throw*)node;
            if (th->value) { AST_Free(th->value); th->value = NULL; }
            if (th->thrown_class_name) { free(th->thrown_class_name); th->thrown_class_name = NULL; }
            break;
        }

        case AST_NULL:
            break;

        case AST_ERROR:
            break;
        default:
            break;
        }

        free(node);
        node = next;
    }
}
