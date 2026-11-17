#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "AST.h"
#include <stdbool.h>

typedef struct SYMBOL
{
    struct SYMBOL* next;
    Token value;
    DataType type;
    char* key;
    char* internal_name;
    char* class_name;
    int dim_count;
    int* dim_sizes;
    bool is_field;
    int field_offset;
    bool is_outer_field_ref;
    bool is_function;
    FuncSignature* func_sig;
    PtrArrayInfo* ptr_array_info;
    MultiReturnType* mr_type;
    int field_modifier;
    int ptr_depth;
    bool is_unsafe;
    bool is_static;
    bool is_const;
    unsigned int level_const_mask;
    bool is_global_predecl;
    bool is_static_local;
    char* const_value_str;
    bool internal_linkage;
    char* static_guard_name;
    bool has_static_init;
    char* static_init_value;
    int total_bytes;
    bool is_value_type;
    bool is_dynamic_array;
    int alignment;
    bool is_method_ptr;
    int type_param_index;
    bool is_closure_holder;
    bool is_closure_ptr;
    bool is_type_name;
    DataType* generic_type_args;
    char** generic_type_arg_names;
    int generic_type_arg_count;
} Symbol;

typedef struct SYMBOL_TABLE
{
    Symbol** buckets;
    int count;
    int size;
} Symbol_Table;

typedef struct SCOPE
{
    Symbol_Table* table;
    struct SCOPE* parent;
    int depth;
} Scope;

typedef struct FuncEntry {
    char* name;
    AST_Function* func;
    int param_count;
    DataType* param_types;
    struct FuncEntry* next;
} FuncEntry;

typedef struct MemberInfo {
    char* name;
    DataType type;
    bool is_method;
    AST_Function* func;
    bool is_const_method;
    int offset;
    struct ClassEntry* inherited_from;
    int field_modifier;
    ASTNode* init_expr;
    char* init_value_str;
    char* field_class_name;
    char* class_name;
    bool is_static;
    bool is_const;
    char* const_value_str;
    int type_param_index;
    bool is_unsafe;
    int ptr_depth;
    int dim_count;
    int* dim_sizes;
    bool is_dynamic_array;
    DataType* generic_type_args;
    char** generic_type_arg_names;
    int generic_type_arg_count;
    FuncSignature* func_sig;
} MemberInfo;

typedef struct ClassEntry {
    char* name;
    char* short_name;
    char* outer_class_name;
    char** parent_names;
    int parent_count;
    int modifier;
    MemberInfo** members;
    int member_count;
    int member_capacity;
    int total_size;
    struct ClassEntry* next;
    AST_Function* constructor;
    bool members_resolved;
    MemberInfo** static_members;
    int static_member_count;
    TypeParam* type_params;
    int type_param_count;
    bool is_generic;
    AST_ClassDef* generic_ast;
    int class_id;
    bool has_type_header;
    bool has_outer;
    int vtable_size;
    char** vtable_method_names;
    int next_vtable_index;
    bool is_function_class;
    char** enum_names;
    int enum_count;
    int enum_capacity;
} ClassEntry;

typedef struct TypeAlias {
    char* name;
    DataType actual_type;
    struct TypeAlias* next;
} TypeAlias;

typedef struct ClassIdEntry {
    int class_id;
    int parent_id;
    char* error_handler;
    struct ClassIdEntry* next;
} ClassIdEntry;

extern Scope* current_scope;
extern bool main_has_args;

void semantics_register_native_symbol(const char* name);
bool semantics_is_native_symbol(const char* name);

void Semantics(ASTNode* ast_node);
void set_current_semantic_file(const char* filename);
void main_parse(ASTNode* ast_node);
ClassIdEntry* semantics_get_class_ids(void);
int semantics_get_class_count(void);
void semantics_get_class_isa(int* out_count, unsigned char** out_table);
ClassEntry* find_class(const char* name);
void add_class(ClassEntry* entry);
char* data_type_to_string(DataType type);
DataType Get_Datatype(TokenType type);
static int S_get_type_size(DataType type);
void enter_scope(void);
void exit_scope(void);
Symbol* lookup_symbol(const char* name);
Symbol* lookup_symbol_with_file(const char* name, const char* file);
void insert_symbol(Symbol* sym);
static DataType get_expression_type(ASTNode* node);
static bool is_subclass_of(ClassEntry* class_entry, ClassEntry* parent_entry);
char* generate_internal_name(void);
ASTNode* parser_create_literal(Token tok);
AST_Function* find_function_by_signature(const char* name, int arg_count, DataType* arg_types);
bool function_signature_exists(const char* name, int param_count, DataType* param_types);
static void predeclare_all_types(ASTNode* node);
static AST_Function* find_instantiated(const char* generic_name,
                                         DataType* type_args, int count,
                                         char** type_arg_names);
static AST_Function* instantiate_generic_function(AST_Function* generic_func,
                                                   DataType* type_args,
                                                   int type_arg_count,
                                                   char** type_arg_names);
static void record_instantiation(const char* generic_name,
                                  const char* inst_name,
                                  DataType* type_args, int count,
                                  char** type_arg_names,
                                  AST_Function* func);
static DataType* infer_type_args(AST_Function* generic_func,
                                  ASTNode* call_args,
                                  int arg_count,
                                  int* out_type_arg_count);
static void collect_function_return(ASTNode* node, AST_Function* func);
ASTNode* semantics_get_instantiated_functions(void);
static ClassEntry* instantiate_generic_class(ClassEntry* generic_cls,
                                               DataType* type_args,
                                               char** type_arg_names,
                                               int type_arg_count);
ClassEntry* semantics_get_all_classes(void);
char* class_vtable_label(const char* class_name);
const char* resolve_vtable_slot(ClassEntry* cls, int slot);
ASTNode* semantics_prepare_program(ASTNode* ast_node);
void semantics_propagate_closures(ASTNode* program);
static void free_func_sig(FuncSignature* sig);
void semantics_cleanup(void);
void free_symbol(Symbol* sym);

#endif