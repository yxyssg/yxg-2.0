# 数据结构总览

## 概述

YXG 编译器用一组结构体承载各阶段的信息。这一章逐个说明它们的定义、用途和生命周期。

## 数据流转

```
Token ──组成──▶ ASTNode ──转换──▶ IR ──切分──▶ BasicBlock / CFG
                    │                          │
                    ▼                          ▼
              Symbol Table               SSA 形式 IR
              (符号表)                          │
                                                ▼
                                          RegMap
                                          (寄存器映射)
```

## Token（词法单元）

定义在 `Token.h`：

```c
typedef struct Token {
    char* value;        // 词法值，如 "int"、"42"、"x"
    TokenType type;     // 词法类型，如 TOKEN_INT、TOKEN_NUMBER
    int line;           // 行号
    int column;         // 列号
    const char* file;   // 源文件名
} Token;
```

`file` 字段用于多文件编译——报错时要知道是哪个文件。

`TokenType` 枚举有 **119 个**成员，按类别分：

| 类别 | 数量 | 例子 |
|---|---|---|
| 关键字 | 约 40 | `TOKEN_INT` `TOKEN_CLASS` `TOKEN_PUB` `TOKEN_VIRTUAL` |
| 标识符/字面量 | 4 | `TOKEN_IDENTIFIER` `TOKEN_NUMBER` `TOKEN_STRING` `TOKEN_CHAR` |
| 运算符 | 约 40 | `TOKEN_ADD` `TOKEN_ASSIGN` `TOKEN_SHL` `TOKEN_AND` |
| 分隔符 | 6 | `TOKEN_SEMICOLON` `TOKEN_COMMA` `TOKEN_PAREN_LEFT` |
| 特殊 | 若干 | `TOKEN_EOF` `TOKEN_VARARG` `TOKEN_UNSAFE` |

## ASTNode（抽象语法树节点）

定义在 `AST.h`：

```c
typedef struct ASTNODE {
    ASTType type;        // 节点类型
    Token value;         // 关联的词法单元
    struct ASTNODE* next;  // 兄弟节点（语句链表）
} ASTNode;
```

`ASTType` 枚举有 **49 个**成员：

| 分类 | 节点类型 |
|---|---|
| 表达式 | `AST_LITERAL` `AST_BINARY` `AST_UNARY` `AST_CAST` `AST_CONDITIONAL` |
| 变量 | `AST_VAR` `AST_UNINIT_VAR` `AST_VAR_ASS` |
| 输入输出 | `AST_PRINT` `AST_INPUT` |
| 控制流 | `AST_IF` `AST_SWITCH` `AST_CASE` `AST_BREAK` `AST_CONTINUE` `AST_WHILE` `AST_FOR` `AST_FOREACH` |
| 函数 | `AST_FUNCTION` `AST_RETURN` `AST_PARAM` |
| 数组 | `AST_ARRAY_DECL` `AST_ARRAY_ACCESS` `AST_ARRAY_ASS` `AST_NEW_ARRAY` `AST_INIT_LIST` `AST_ARRAY_LENGTH` `AST_SUBARRAY` |
| 类与对象 | `AST_CLASS_DEF` `AST_NEW_INSTANCE` `AST_MEMBER_ACCESS` `AST_MEMBER_ASS` `AST_CLASS_MEMBER` `AST_THIS` |
| 枚举 | `AST_ENUM_DEF` `AST_ENUM_MEMBER` |
| 指针 | `AST_DEREF` `AST_ADDRESS_OF` |
| 字符串 | `AST_STR_FROM_STRING` `AST_STR_SLICE` |
| 可变参数/多返回值 | `AST_VARARG` `AST_VARARG_ACCESS` `AST_VARARG_GROUP` `AST_MULTI_RETURN` `AST_RETURN_EXTRACT` |
| 异常 | `AST_TRY` `AST_THROW` |
| 其它 | `AST_NULL` `AST_ERROR` |

每个具体类型对应一个结构体，都以 `ASTNode` 作为首字段（`base`），形成简单的继承：

```c
typedef struct AST_BINARY {
    ASTNode base;           // 基类
    ASTNode* left;          // 左操作数
    Token op;               // 运算符
    ASTNode* right;         // 右操作数
    DataType resultType;    // 结果类型（语义分析后填充）
    bool is_unsigned;       // 是否无符号运算
} AST_Binary;
```

**为什么用「首字段继承」**：可以把 `AST_Binary*` 直接当 `ASTNode*` 传，靠 `base.type` 判断具体类型。C 里没有虚表，这是最省的做法。

## Symbol 与符号表

定义在 `Semantics.h`：

```c
typedef struct SYMBOL {
    struct SYMBOL* next;    // 哈希桶链表
    Token value;            // 词法信息
    DataType type;          // 数据类型
    char* key;              // 符号名
    char* internal_name;    // 编译器内部名，如 _var0
    char* class_name;       // 所属类名
    int dim_count;          // 数组维度
    int* dim_sizes;         // 各维度大小
    bool is_field;          // 是否是类字段
    int field_offset;       // 字段在类中的偏移
    bool is_function;       // 是否是函数
    int field_modifier;     // 访问修饰符（pub/pro/pri）
} Symbol;
```

作用域用链式结构：

```c
typedef struct SCOPE {
    Symbol_Table* table;    // 当前作用域哈希表
    struct SCOPE* parent;   // 父作用域
    int depth;              // 作用域深度
} Scope;

typedef struct SYMBOL_TABLE {
    Symbol** buckets;       // 哈希桶数组
    int count;              // 符号数量
    int size;               // 桶数量
} Symbol_Table;
```

查找时沿 `parent` 往上走，实现变量遮蔽。

### 预扫描

`Semantics()` 在正式分析前跑两遍：

```c
predeclare_all_types(ast_node);     // 登记所有类名、枚举名
predeclare_global_vars(ast_node);   // 登记所有全局变量
main_parse(ast_node);               // 正式分析
```

这是全局变量能前向引用的原因。

## IR（中间表示）

定义在 `IR.h`，三地址码形式：

```c
typedef struct IR {
    IRType type;            // 指令类型
    char* result;           // 结果变量
    int def_id;             // 定义 ID（SSA 用）

    IR_OP_Type op;          // 二元运算的操作符
    char* args1;            // 第一操作数
    int args1_def;
    DataType args1_type;
    char* args2;            // 第二操作数
    int args2_def;
    DataType args2_type;

    DataType data_type;     // 结果类型
    struct IR* next;        // 下一条指令

    bool has_return;        // 函数入口标签标记
    int array_size;
    int elem_size;
    int stack_bytes;        // 栈分配字节数

    int phi_arg_count;      // φ 节点参数数量
    char** phi_arg_names;

    char** label;           // 标签数组
    int label_count;

    DataType* call_func_args_type;  // 调用参数类型
    int call_func_args_num;         // 参数数量
    int total_push_bytes;           // 压栈总字节
} IR;
```

`IRType` 枚举有 **72 个**成员：

| 分类 | 指令 |
|---|---|
| 数据移动 | `IR_LITERAL` `IR_VAR` `IR_VAR_ASS` `IR_VAR_UNINIT` `IR_COPY_BLOCK` `IR_TRUNCATE` |
| 运算 | `IR_BINARY` `IR_UNARY` `IR_CAST` |
| 算术操作符 | `IR_ADD` `IR_SUB` `IR_MUL` `IR_DIV` `IR_MOD` `IR_UDIV` `IR_UMOD` |
| 比较 | `IR_JE` `IR_JNE` `IR_JG` `IR_JL` `IR_JGE` `IR_JLE` `IR_ULT` `IR_ULE` `IR_UGT` `IR_UGE` |
| 逻辑/位运算 | `IR_LOGICAL_AND` `IR_LOGICAL_OR` `IR_BITWISE_AND` `IR_BITWISE_OR` `IR_BITWISE_XOR` `IR_BITWISE_NOT` `IR_SHL` `IR_SHR` `IR_SAR` |
| 跳转 | `IR_LABEL` `IR_JMP` `IR_JMPZ` `IR_JMPNZ` |
| 函数 | `IR_PARAM_DEF` `IR_PUSH` `IR_CALL` `IR_CALL_INDIRECT` `IR_RET` |
| 数组 | `IR_ARRAY_DECL` `IR_ARRAY_LOAD` `IR_ARRAY_STORE` `IR_ALLOC_ARRAY` `IR_ARRAY_LENGTH` `IR_ARRAY_TOSTRING` `IR_BOUNDS_CHECK` |
| 类与对象 | `IR_ALLOC_CLASS` `IR_LOAD_MEMBER` `IR_STORE_MEMBER` `IR_LOAD_THIS` |
| 指针 | `IR_DEREF` `IR_ADDRESS_OF` `IR_PTR_STORE` |
| 字符串 | `IR_STR_FROM_STRING` `IR_STR_FROM_LITERAL` `IR_STR_LENGTH` `IR_STR_INDEX` `IR_STR_SUBSTR` `IR_STR_TO_STRING` |
| 可变参数 | `IR_VARARG_INIT` |
| SSA | `IR_PHI` |
| 其它 | `IR_PRINT` `IR_INPUT` `IR_FORMAT` `IR_IF` `IR_ERROR` `IR_OP_ERROR` |

## BasicBlock 与 CFG

定义在 `CFG.h`：

```c
typedef struct BasicBlock {
    int id;                     // 块编号
    IR* first_ir;               // 第一条 IR
    IR* last_ir;                // 最后一条 IR
    int ir_count;

    struct BasicBlock** succs;  // 后继块
    int succ_count;
    struct BasicBlock** preds;  // 前驱块
    int pred_count;

    struct BasicBlock* idom;    // 直接支配者
    struct BasicBlock** dom_children;  // 支配树子节点
    struct BasicBlock** df;     // 支配边界

    bool* live_gen;             // 活跃变量 gen 集
    bool* live_kill;            // kill 集
    bool* live_in;              // in 集
    bool* live_out;             // out 集

    IR* phi_nodes;              // φ 节点链表
    char** def_names;           // 本块定义的变量
    char** use_names;           // 本块使用的变量
    int linear_start;           // 线性编号起点
    int linear_end;             // 线性编号终点
} BasicBlock;
```

```c
typedef struct CFG {
    BasicBlock** blocks;        // 基本块数组
    int block_count;
    BasicBlock* entry;          // 入口块
    BasicBlock* exit_block;     // 出口块

    char** var_names;           // 所有变量名
    int var_count;
    LiveInterval* intervals;    // 活跃区间
    int interval_count;
} CFG;
```

`live_*` 四组位图是活跃分析的载体，`dom_children` / `df` 用于 SSA 构造时插入 φ 节点。

## RegMap（寄存器映射）

定义在 `regmap.h`：

```c
typedef enum Reg {
    R_NONE = -1,
    R_EAX  = 0,
    R_ECX  = 1,
    R_EDX  = 2,
    R_EBX  = 3,
    R_ESI  = 4,
    R_EDI  = 5,
} Reg;

typedef struct RegMapEntry {
    Reg reg;            // 分配的寄存器
    bool is_float;      // 是否浮点变量
    int spill_slot;     // 溢出槽偏移（ebp 相对）
} RegMapEntry;

typedef struct RegMap {
    char** names;               // 变量名
    RegMapEntry* entries;       // 映射条目
    int count;
    int capacity;
    unsigned callee_saved_mask; // 被调用者保存寄存器掩码
} RegMap;
```

调用方式：

```c
regmap_allocate_from_cfg(cfg, 5);
```

`5` 是可用寄存器数。`callee_saved_mask` 记录函数用到了哪些需要保存的寄存器（`ebx` / `esi` / `edi`），函数序言里要 push、结语里要 pop。

## 生命周期

| 结构 | 创建 | 释放 |
|---|---|---|
| `Token` | 词法分析 | 语法分析后 |
| `ASTNode` | 语法分析 | 代码生成后 |
| `Symbol` | 语义分析 | `semantics_cleanup()` |
| `IR` | IR 生成 | `codegen_cleanup()` |
| `CFG` | `cfg_build()` | 寄存器分配后 |
| `RegMap` | `regmap_allocate_from_cfg()` | 代码生成后 |

`main.c` 在正常结束和错误退出时都会调用 `semantics_cleanup()` 和 `codegen_cleanup()`。

## 继续阅读

下一章介绍错误处理系统。
