#ifndef AST_H
#define AST_H

#include "Token.h"

typedef enum ASTTYPE
{
    AST_LITERAL,
    AST_BINARY,
    AST_UNARY,
    AST_CAST,
    AST_CONDITIONAL,
    AST_VAR,
    AST_UNINIT_VAR,
    AST_VAR_ASS,
    AST_PRINT,
    AST_INPUT,
    AST_IF,
    AST_SWITCH,
    AST_CASE,
    AST_BREAK,
    AST_CONTINUE,
    AST_WHILE,
    AST_FOR,
    AST_FOREACH,
    AST_FUNCTION,
    AST_RETURN,
    AST_PARAM,
    AST_ARRAY_DECL,
    AST_ARRAY_ACCESS,
    AST_ARRAY_ASS,
    AST_NEW_ARRAY,
    AST_INIT_LIST,
    AST_ARRAY_LENGTH,
    AST_SUBARRAY,
    AST_CLASS_DEF,
    AST_NEW_INSTANCE,
    AST_MEMBER_ACCESS,
    AST_THIS,
    AST_CLASS_MEMBER,
    AST_MEMBER_ASS,
    AST_ENUM_DEF,
    AST_ENUM_MEMBER,
    AST_DEREF,
    AST_ADDRESS_OF,
    AST_STR_FROM_STRING,
    AST_STR_SLICE,
    AST_VARARG,
    AST_VARARG_ACCESS,
    AST_VARARG_GROUP,
    AST_MULTI_RETURN,
    AST_RETURN_EXTRACT,
    AST_TRY,
    AST_THROW,
    AST_NULL,

    AST_ERROR,
}ASTType;

typedef enum DATATYPE
{
    DATA_INT = 0,
    DATA_FLOAT,
    DATA_CHAR,
    DATA_STRING,
    DATA_STR,
    DATA_BOOL,
    DATA_VOID,
    DATA_UINT,
    DATA_UFLOAT,
    DATA_INT8,
    DATA_INT16,
    DATA_INT32,
    DATA_INT64,
    DATA_INT128,
    DATA_UINT8,
    DATA_UINT16,
    DATA_UINT32,
    DATA_UINT64,
    DATA_UINT128,
    DATA_FLOAT8,
    DATA_FLOAT16,
    DATA_FLOAT32,
    DATA_FLOAT64,
    DATA_FLOAT128,
    DATA_UFLOAT8,
    DATA_UFLOAT16,
    DATA_UFLOAT32,
    DATA_UFLOAT64,
    DATA_UFLOAT128,
    DATA_IDENTIFIER,

    DATA_ERROR,
}DataType;

typedef enum CONSTRAINT_KIND {
    CONSTRAINT_NONE = 0,
    CONSTRAINT_SUBTYPE,
    CONSTRAINT_SUPERTYPE,
} ConstraintKind;

typedef struct MULTIRETURNTYPE MultiReturnType;
typedef struct FUNCSIGNATURE FuncSignature;

typedef struct ASTNODE
{
    ASTType type;
    Token value;
    struct ASTNODE* next;
}ASTNode;

typedef struct AST_LITERAL
{
    ASTNode base;
    DataType dataType;
    char* internal_name;
    char* call_result_class_name;
    bool is_class_name;
    bool is_function_ref;
    bool is_dynamic_array;

    bool is_call_function;
    ASTNode* call_function_Parameter;
    int call_function_ParameterCount;

    ASTNode* method_arguments;
    int method_arg_count;

    bool is_field_ref;
    int field_offset;
    bool is_field_array;
    bool is_outer_field_ref;
    bool is_return_pointee_const;

    bool returns_array;
    int return_total_bytes;

    int ptr_depth;
    bool is_indirect_call;

    int* param_byte_sizes;

    bool is_method_call;
    bool method_invoke;

    MultiReturnType* multi_return_type;
    char* multi_ret_buf;

    ASTNode* indirect_callee;
    FuncSignature* returned_func_sig;

    bool is_generic_instantiation;
    DataType* generic_type_args;
    int generic_type_arg_count;
    char** generic_type_arg_names;
    struct AST_FUNCTION* closure_func;
    bool is_closure_call;
    bool is_closure_ptr;
}AST_Literal;

typedef struct AST_BINARY
{
    ASTNode base;
    ASTNode* left;
    Token op;
    ASTNode* right;
    DataType resultType;
    bool is_unsigned;
    int result_ptr_depth;
    int elem_size;
}AST_Binary;

typedef struct AST_STR_FROM_STRING {
    ASTNode base;
    ASTNode* source;
} AST_StrFromString;

typedef struct AST_ARRAY_DECL {
    ASTNode base;
    DataType elementType;
    char* element_class_name;
    char* name;
    int* dim_sizes;
    int dim_count;
    ASTNode** dim_exprs;
    char* internal_name;
    ASTNode* init_list;
    int modifier;
    int total_bytes;
    bool is_unsafe;
    bool is_const;
    bool is_view;
    bool is_dynamic_array;
} AST_ArrayDecl;

typedef struct AST_ARRAY_ACCESS {
    ASTNode base;
    ASTNode* array;
    ASTNode** indices;
    int index_count;
    DataType elementType;
    int dim_count;
    int* dim_sizes;
    int* full_dim_sizes;
    int full_dim_count;
    bool is_heap_array;
    FuncSignature* func_sig;
} AST_ArrayAccess;

typedef struct AST_ARRAY_ASS {
    ASTNode base;
    ASTNode* target;
    ASTNode* value;
    DataType elementType;
    int copy_bytes;
} AST_ArrayAss;

typedef struct AST_NEW_ARRAY {
    ASTNode base;
    DataType elementType;
    char* element_class_name;
    int element_total_bytes;
    int dim_count;
    int* dim_sizes;
    ASTNode** dim_exprs;
} AST_NewArray;

typedef struct AST_INIT_LIST {
    ASTNode base;
    ASTNode** elements;
    int count;
    DataType elemType;
    int dim_count;
    int* dim_sizes;
} AST_InitList;

typedef struct AST_ARRAY_LENGTH {
    ASTNode base;
    ASTNode* array;
    bool is_dynamic;
    int total_elements;
} AST_ArrayLength;

typedef struct AST_SUBARRAY {
    ASTNode base;
    ASTNode* array;
    int index_count;
    DataType elementType;
    int dim_count;
    int* dim_sizes;
    char* base_temp;
} AST_SubArray;

typedef struct AST_STR_SLICE {
    ASTNode base;
    ASTNode* source;
    ASTNode* start;
    ASTNode* end;
} AST_StrSlice;

typedef struct AST_CONDITIONAL {
    ASTNode base;
    ASTNode* condition;
    ASTNode* then_expr;
    ASTNode* else_expr;
    DataType resultType;
} AST_Conditional;

typedef struct AST_UNARY
{
    ASTNode base;
    Token op;
    ASTNode* right;
    DataType dataType;
    int ptr_step;
}AST_Unary;

typedef struct AST_CAST
{
    ASTNode base;
    ASTNode* operand;
    DataType target_type;
    char* target_class_name;
    int target_class_id;
    int target_ptr_depth;
    FuncSignature* target_func_sig;
    int target_dim_count;
    int* target_dim_sizes;
    DataType source_type;
    int source_ptr_depth;
    FuncSignature* source_func_sig;
    bool is_reinterpret;
    bool is_checked;
}AST_Cast;

typedef struct FUNCSIGNATURE {
    DataType return_type;
    int return_type_param_index;
    bool return_pointee_const;
    char* return_class_name;
    bool is_closure_sig;

    bool returns_closure;
    bool is_closure_ptr;

    int param_count;
    DataType* param_types;
    int* param_type_param_indices;
    bool* param_pointee_const;
    char** param_class_names;
    bool* param_is_array;

    struct FUNCSIGNATURE** param_func_sigs;

    MultiReturnType* multi_return;

    bool has_varargs;
    int vararg_param_count;
    int* vararg_param_indices;

    struct FUNCSIGNATURE* ret_func_sig;
} FuncSignature;

typedef struct PTRARRAYINFO {
    DataType element_type;
    int dim_count;
    int* dim_sizes;
} PtrArrayInfo;

typedef struct AST_VAR
{
    ASTNode base;
    Token name;
    ASTNode* Value;

    TokenType type;
    DataType dataType;
    char* internal_name;
    char* static_storage_name;

    int dim_count;
    int* dim_sizes;
    ASTNode** dim_exprs;

    int modifier;
    bool is_field_ref;
    int field_offset;

    bool is_const;
    unsigned int level_const_mask;

    int ptr_depth;
    FuncSignature* func_ptr_sig;
    PtrArrayInfo*  ptr_array_info;
    ASTNode* deref_target;
    bool is_unsafe;
    bool is_static;

    bool is_static_local;
    char* static_guard_name;
    bool has_static_init;
    char* static_init_value;

    int copy_bytes;
    int total_bytes;
    bool is_dynamic_array;

    DataType* generic_type_args;
    int generic_type_arg_count;
    char** generic_type_arg_names;

    bool is_closure_ptr;
    int type_param_index;
    char* decl_class_name;   /* 声明的类名（语义分析前保存，用于变型检查） */
}AST_Var;

typedef struct AST_PRINT
{
    ASTNode base;
    ASTNode* Parameter;
}AST_Print;

typedef struct AST_INPUT {
    ASTNode base;
    ASTNode* format_string;
    ASTNode** args;
    int arg_count;
} AST_Input;

typedef struct AST_IF
{
    ASTNode base;
    ASTNode* Condition;
    ASTNode* code;
    int elif_Count;
    ASTNode** Elif;
    ASTNode** elif_condition;
    int has_else;
    ASTNode* Else;
}AST_If;

typedef struct AST_SWITCH_NODE {
    ASTNode base;
    ASTNode* condition;
    int case_count;
    int case_capacity;
    struct AST_CASE_NODE** cases;
    bool has_default;
    ASTNode* default_body;
} AST_Switch;

typedef struct AST_CASE_NODE {
    ASTNode base;
    ASTNode* value;
    ASTNode* body;
    bool is_default;
} AST_Case;

typedef struct AST_BREAK_NODE {
    ASTNode base;
} AST_Break;

typedef struct AST_CONTINUE_NODE {
    ASTNode base;
} AST_Continue;

typedef struct AST_WHILE
{
    ASTNode base;
    ASTNode* Condition;
    ASTNode* code;
    bool has_do;
}AST_While;

typedef struct AST_FOR {
    ASTNode base;
    ASTNode* init;
    ASTNode* condition;
    ASTNode* increment;
    ASTNode* code;
} AST_For;

typedef struct AST_FOREACH {
    ASTNode base;
    Token iterator;
    ASTNode* array;
    ASTNode* code;
    char* internal_iterator;
    DataType elemType;
    bool has_explicit_type;
    bool is_dynamic_array;
    int total_elements;
    bool is_container;
    char* container_class_name;
    char* container_size_method;
    char* container_get_method;
} AST_ForEach;

typedef struct AST_RETURN
{
    ASTNode base;
    DataType returnType;
    ASTNode* value;
    int return_bytes;
}AST_Return;

typedef struct MULTIRETURNTYPE {
    int count;
    DataType* types;
    int* type_param_indices;
    bool* is_variadic;
    FuncSignature** func_sigs;
} MultiReturnType;

typedef struct TYPECONSTRAINT {
    ConstraintKind kind;
    char* class_name;
} TypeConstraint;

typedef struct TYPEPARAM {
    char* name;
    int  variance;      /* 0 = 不变, +1 = 协变(+T), -1 = 逆变(-T) */
    int  index;
    int  constraint_count;
    TypeConstraint* constraints;
    bool has_default_type;
    DataType default_type;
} TypeParam;

typedef struct AST_MULTI_RETURN AST_MultiReturn;

typedef struct AST_FUNCTION {
    ASTNode base;
    Token name;
    char* internal_name;
    ASTNode* Parameter;
    DataType* parameterType;
    int parameterCount;
    int max_parameters;

    ASTNode* Code;

    AST_Return* Return;
    int return_Count;
    DataType return_type;
    bool returns_array;
    bool return_pointee_const;
    DataType return_element_type;
    int return_dim_count;
    int* return_dim_sizes;
    ASTNode** return_dim_exprs;
    int Modifier;
    int return_ptr_depth;
    bool is_static;

    FuncSignature* ret_func_sig;
    /* 返回类型是泛型实例时记录其类型实参，供变型检查拼实例名 */
    char** return_generic_type_arg_names;
    int return_generic_type_arg_count;

    char* return_class_name;
    bool returns_struct;
    int return_struct_bytes;

    bool has_varargs;
    int vararg_param_index;
    char* vararg_internal_name;

    int vararg_param_count;
    int* vararg_param_indices;
    char** vararg_internal_names;

    MultiReturnType* multi_return;
    AST_MultiReturn* multi_return_node;

    TypeParam* type_params;
    int type_param_count;
    bool is_generic;

    bool is_nested;
    bool is_closure;
    bool is_native;
    bool is_stdcall;
    bool is_virtual;
    bool is_override;
    bool is_const_method;
    int vtable_index;
    int upvalue_count;
    char** upvalue_names;
    DataType* upvalue_types;
    char** upvalue_internal_names;
}AST_Function;

typedef struct AST_VARARG {
    ASTNode base;
    Token name;
    char* internal_name;
} AST_Vararg;

typedef struct AST_CLASS_DEF {
    ASTNode base;
    char* name;
    Token name_tok;
    char* parent_name;
    char** parent_names;
    int parent_count;
    int modifier;
    ASTNode* members;
    TypeParam* type_params;
    int type_param_count;
    bool is_generic;
    bool is_function_class;
} AST_ClassDef;


typedef struct FieldInit {
    int offset;
    DataType type;
    char* value_str;
} FieldInit;

typedef struct AST_NEW_INSTANCE {
    ASTNode base;
    char* class_name;
    ASTNode* arguments;
    int instance_size;
    bool has_constructor;
    char* constructor_name;
    int  constructor_arg_count;
    int field_init_count;
    FieldInit* field_inits;
    DataType* generic_type_args;
    int generic_type_arg_count;
    char** generic_type_arg_names;
    int class_id;
    bool generic_infer;
    char* vtable_label;
    bool args_pre_analyzed;
    MultiReturnType* mr_type;
} AST_NewInstance;

typedef struct AST_MEMBER_ACCESS {
    ASTNode base;
    ASTNode* object;
    char* member_name;
    int member_offset;
    DataType member_type;
    int member_ptr_depth;
    bool is_method;
    bool is_method_ptr;
    bool is_field_array;
    /* 字段是数组时的完整维度信息（供 a[i][j] 这类下标计算用） */
    int member_dim_count;
    int* member_dim_sizes;
    char* method_internal;
    int method_param_count;
    DataType* method_param_types;
    DataType member_return_type;
    int vtable_index;
    char* const_value_str;
} AST_MemberAccess;

typedef struct AST_THIS {
    ASTNode base;
    int instance_size;
} AST_This;

typedef struct AST_MEMBER_ASS {
    ASTNode base;
    ASTNode* object;
    char* member_name;
    Token op;
    ASTNode* value;
    DataType member_type;
    bool value_is_array;   /* 语义阶段标记：字段是数组，RHS 存的是指针 */
    int member_offset;
} AST_MemberAss;

typedef struct AST_ENUM_MEMBER {
    ASTNode base;
    char* name;
    int value;
    bool has_explicit_value;
    Token* explicit_token;
} AST_EnumMember;

typedef struct AST_ENUM_DEF {
    ASTNode base;
    char* name;
    Token name_tok;
    int member_count;
    int member_capacity;
    AST_EnumMember** members;
    int modifier;
} AST_EnumDef;

typedef struct AST_DEREF {
    ASTNode base;
    ASTNode* operand;
    DataType resultType;
    int ptr_depth;
    PtrArrayInfo* deref_array_info;
} AST_Deref;

typedef struct AST_ADDRESS_OF {
    ASTNode base;
    ASTNode* operand;
    DataType resultType;
    int ptr_depth;
    unsigned int level_const_mask;
    char* const_value_str;
    FuncSignature* target_func_sig;
} AST_AddressOf;

typedef struct AST_VARARG_GROUP {
    ASTNode base;
    ASTNode* elements;
    int count;
    DataType target_elem_type;
} AST_VarargGroup;

typedef struct AST_MULTI_RETURN {
    ASTNode base;
    int value_count;
    ASTNode** values;
} AST_MultiReturn;

typedef struct AST_RETURN_EXTRACT {
    ASTNode base;
    ASTNode* object;
    int index;
    int sub_index;
    DataType result_type;
    bool is_array;
    int total_bytes;
    int offset;
    bool is_variadic_src;
    FuncSignature* extracted_func_sig;
    bool is_thread_object;
    MultiReturnType* mr_type;
} AST_ReturnExtract;

typedef struct AST_CATCH_CLAUSE {
    char* type_name;
    Token var_name;
    ASTNode* body;
    char* internal_name;
    int total_bytes;
    int class_id;
} CatchClause;

typedef struct AST_TRY {
    ASTNode base;
    ASTNode* code;
    CatchClause* catches;
    int catch_count;
    int catch_capacity;
    ASTNode* finally_code;
    bool has_finally;
} AST_Try;

typedef struct AST_THROW {
    ASTNode base;
    ASTNode* value;
    char* thrown_class_name;
} AST_Throw;

typedef struct AST_NULL_LITERAL {
    ASTNode base;
    int ptr_depth;
} AST_Null;

#endif
