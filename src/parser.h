#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "Lexer.h"

extern bool assertions_enabled;

extern char* read_source(char* file_name);

typedef struct Parser
{
    Token* tokens;
    int index;
    Token current;
    char* filename;
    Token previous;
}Parser;

typedef struct ModulePath {
    char* path;
    struct ModulePath* next;
} ModulePath;

Parser* parser_init(Token* tokens,const char* filename);
ASTNode* parser_Sentence(Parser* parser,bool is_block);
ASTNode* parser_parse(Parser* parser);
static ASTNode* parser_member_assign(Parser* parser, ASTNode* left);
ASTNode* parser_class_def(Parser* parser,int modifier);
ASTNode* parser_constructor_def(Parser* parser, int modifier);
ASTNode* parser_enum_def(Parser* parser);
ASTNode* parser_return(Parser* parser);
ASTNode* parser_class_def_with_modifier(Parser* parser, int modifier);
ASTNode* parser_def_function_with_modifier(Parser* parser, int modifier);
void parser_register_nested_generic(const char* mangled, const char* base,
                                    DataType* args, char** names, int argc);
bool parser_lookup_nested_generic(const char* mangled, char** base,
                                  DataType** args, char*** names, int* argc);

ASTNode* parser_var_with_typename(Parser* parser);
ASTNode* parser_def_function(Parser* parser);
ASTNode* parser_vararg_group(Parser* parser);
ASTNode* parser_while(Parser* parser);
ASTNode* parser_do_while(Parser* parser);
ASTNode* parser_for(Parser* parser);
ASTNode* parser_switch(Parser* parser);
ASTNode* parser_break(Parser* parser);
ASTNode* parser_continue(Parser* parser);
ASTNode* parser_try(Parser* parser);
ASTNode* parser_throw_stmt(Parser* parser);
bool parser_current_is_type(Parser* parser, TokenType type);
void parser_eat(Parser* parser, TokenType expected);
void parser_advance(Parser* parser);
ASTNode* parser_if(Parser* parser);
static ASTNode* parser_multi_return_func_ptr_var(Parser* parser);
static MultiReturnType* parse_multi_return_type(Parser* parser);
static FuncSignature *parser_parse_bare_func_ptr(Parser *parser,
                                                  char **out_name,
                                                  MultiReturnType *outer_mrt);
ASTNode* parser_def_var(Parser* parser);
ASTNode* parser_var(Parser* parser, bool allow_multi_decl);
ASTNode* parser_input(Parser* parser);
ASTNode* parser_Binary(Parser* parser);
ASTNode* parser_Unary(Parser* parser);
ASTNode* parser_init_list(Parser* parser);
ASTNode* parser_import(Parser* parser);
static ASTNode* parser_compound_assign_or_incdec(Parser* parser);
static ASTNode* parser_ArrayAccessSuffix(Parser* parser, ASTNode* left);
int* parse_dim_sizes(Parser* parser, int* count, DataType elemType);
ASTNode* parser_Print(Parser* parser);
void Print_AST(ASTNode* node, int indent,bool after_sem);
void AST_Free(ASTNode* node);
bool is_Modifier(Parser* parser);
char* AST_Type_to_String(ASTType type);
void module_path_add(const char* path);
void module_path_add_defaults(const char* exe_dir);
bool is_already_imported(const char* abs_path);
char* realpath_wrapper(const char* path);
void mark_imported(const char* abs_path);
int  parser_parse_type_params(Parser* parser, TypeParam** out);
void free_type_params(TypeParam* params, int count);
ASTNode* parser_def_function_with_generics(Parser* parser,
                                            int modifier,
                                            TypeParam* tparams,
                                            int tp_count);
ASTNode* parser_generic_function_body(Parser* parser,
                                       AST_Function* func,
                                       TypeParam* tparams, int tp_count);
void module_system_cleanup(void);
static void free_func_sig_recursive(FuncSignature* sig);
static void free_func_sig(FuncSignature* sig);
static FuncSignature* parse_func_ptr_type_signature(Parser* parser);
static bool parser_try_nested_func_ptr_param(Parser* parser,
                                             DataType* out_base,
                                             FuncSignature** out_sig);
void AST_Free(ASTNode* node);
static void free_func_sig_recursive(FuncSignature* sig);

#endif
