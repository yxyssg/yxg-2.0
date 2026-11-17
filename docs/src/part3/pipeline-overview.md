# 整体流水线

## 概述

YXG 编译器把源代码变成可执行文件，分九个阶段：

```
hello.yxg
   │
   ▼
┌──────────┐
│  词法分析  │  → Token 序列
│  Lexer    │
└──────────┘
   │
   ▼
┌──────────┐
│  语法分析  │  → AST 抽象语法树
│  Parser   │
└──────────┘
   │
   ▼
┌──────────┐
│  语义分析  │  → 标注类型的 AST + 符号表
│ Semantics │
└──────────┘
   │
   ▼
┌──────────┐
│  IR 生成  │  → 三地址码 IR 链表
│  IR Gen   │
└──────────┘
   │
   ▼
┌──────────┐
│  CFG 构建 │  → 基本块 + 控制流图
│  CFG      │
└──────────┘
   │
   ▼
┌──────────┐
│  SSA 构造 │  → SSA 形式 + 优化
│  SSA      │
└──────────┘
   │
   ▼
┌──────────┐
│ SSA 解构  │  → 普通 IR（插复制、删 φ）
│ destruct  │
└──────────┘
   │
   ▼
┌──────────┐
│ 重建 CFG  │  → 解构后的控制流图
│  CFG      │
└──────────┘
   │
   ▼
┌──────────┐
│ 寄存器分配│  → 寄存器映射表
│ RegAlloc  │
└──────────┘
   │
   ▼
┌──────────┐
│  代码生成  │  → output.asm
│ Codegen   │
└──────────┘
   │
   ▼
┌──────────┐
│ NASM 汇编 │  → output.obj
└──────────┘
   │
   ▼
┌──────────┐
│ GCC 链接  │  → output.exe
└──────────┘
```

> **注意 SSA 之后的顺序**：`ssa_destruct` 会把 SSA 形式转回普通 IR（插入复制指令、删掉 φ 节点），控制流已经变了，所以**必须重新建一次 CFG** 才能做寄存器分配。用解构前的 CFG 会得到错误的活跃区间。

## 各阶段职责

### 词法分析（Lexer）

把源代码字符串切成 Token 序列。Token 是最小语义单元：关键字 `int`、标识符 `main`、运算符 `+`、字面量 `42`。

输入：

```
int x = 10;
```

输出：

```
TOKEN_INT  TOKEN_IDENTIFIER("x")  TOKEN_ASSIGN  TOKEN_NUMBER("10")  TOKEN_SEMICOLON
```

### 语法分析（Parser）

按语法规则把 Token 序列组织成 AST。

输入：

```
int x = 10;
```

输出：

```
AST_VAR
├── type: int
├── name: x
└── value: AST_LITERAL(10)
```

### 语义分析（Semantics）

遍历 AST，做类型检查、符号表管理、作用域分析。

`Semantics()` 内部有四步（`Semantics.c:18694`）：

```c
enter_scope();                      // 建立全局作用域
predeclare_all_types(ast_node);     // 先扫一遍，登记所有类/枚举名
predeclare_global_vars(ast_node);   // 再扫一遍，登记所有全局变量
main_parse(ast_node);               // 正式分析
```

**「两遍预扫描」是全局变量支持前向引用的原因**——先登记所有名字，再分析函数体，所以函数可以用位于其后定义的全局变量。

`main.c` 在调用 `Semantics()` 前后还各做了一件事：

```c
ast_node = semantics_prepare_program(ast_node);   // 注入 ERROR / AssertError 内置类
Semantics(ast_node);
semantics_propagate_closures(ast_node);           // 传播闭包信息
```

`semantics_prepare_program` 把 `ERROR` 和 `AssertError` 两个内置类的 AST 链到程序头部，这样 `throw` / `catch` / `assert` 才能找到基类。

### IR 生成（IR Gen）

把 AST 转成三地址码。每条指令最多三个操作数。

输入：AST

输出：

```
.t1 = 10
_var0 = .t1
```

### CFG 构建

把线性 IR 链表切成基本块，建立前驱后继关系。基本块是一段顺序执行、只在末尾跳转的指令序列。

输入：IR 链表

输出：基本块数组 + 边

### SSA 构造

转换成静态单赋值形式（每个变量只赋值一次），并在 φ 节点处合并不同路径的值。同时跑优化 Pass。

输入：普通 IR + CFG

输出：SSA 形式 IR（优化后）

### SSA 解构

把 SSA 转回普通 IR：

- 在每条边上前插入复制指令，把 φ 节点的语义展开
- 删除 φ 节点
- 变量名去版本号

输入：SSA 形式 IR

输出：普通 IR（可执行）

### 重建 CFG

`ssa_destruct` 改变了指令数量和跳转结构，必须重新构建控制流图。活跃区间是寄存器分配的依据，用旧 CFG 会算错。

输入：解构后的 IR

输出：新的 CFG + 活跃区间

### 寄存器分配

把虚拟变量映射到物理寄存器。使用 Chaitin-Briggs 图着色。

```c
regmap_allocate_from_cfg(cfg2, 5);
```

第二个参数 `5` 是可用寄存器数（从 `eax` / `ecx` / `edx` / `ebx` / `esi` / `edi` 中选 5 个给变量，留出的那个给临时计算）。溢出变量分配到栈上。

输入：CFG + 活跃区间

输出：寄存器映射表

### 代码生成

把 IR 翻译成 x86 NASM 汇编。处理函数序言结语、调用约定、数据段布局。

输入：IR + 寄存器映射

输出：`output.asm`

### 汇编与链接

```
nasm -g output.asm -O1 -f win32 -o output.obj
gcc -m32 -g -fuse-ld=lld output.obj <运行时 .obj> -o output.exe
```

运行时 `.obj` 按需链接：用到 `print` 才链 `runtime_print.obj`，用到数组下标才链 `runtime_bounds.obj`。

## 核心源文件速查

| 阶段 | 主要文件 | 核心函数 |
|---|---|---|
| 词法分析 | `Lexer.c` | `lexer_parse()` |
| 语法分析 | `parser.c` | `parser_parse()` → `parser_Sentence()` |
| 语义分析 | `Semantics.c` | `semantics_prepare_program()` → `Semantics()` → `semantics_propagate_closures()` |
| IR 生成 | `IR.c` | `parse_IR()` → `generate_IR()` |
| CFG | `CFG.c` | `cfg_build()` |
| SSA | `SSA.c` | `ssa_construct()` → `ssa_destruct()` |
| 寄存器分配 | `regmap.c` | `regmap_allocate_from_cfg(cfg, num_regs)` |
| 代码生成 | `Codegen_win32.c` | `codegen_win32()` → `codegen_win32_end()` |
| 主程序 | `main.c` | `run_compiler()` 编排整个流水线 |

## 数据结构流转

```
Token ──组成──▶ ASTNode ──转换──▶ IR ──切分──▶ CFG(BasicBlock[])
                    │                          │
                    ▼                          ▼
              Symbol Table                  SSA 形式 IR
              (符号表)                          │
                                                ▼
                                          解构后的 IR
                                                │
                                                ▼
                                          重建 CFG
                                                │
                                                ▼
                                          RegMap
```

## 完整追踪示例

以 `int x = 10;` 为例：

**词法分析**

```
Token[0]: {value: "int", type: TOKEN_INT}
Token[1]: {value: "x",   type: TOKEN_IDENTIFIER}
Token[2]: {value: "=",   type: TOKEN_ASSIGN}
Token[3]: {value: "10",  type: TOKEN_NUMBER}
Token[4]: {value: ";",   type: TOKEN_SEMICOLON}
```

**语法分析**

```
AST_VAR
├── type: TOKEN_INT
├── name: "x"
└── value: AST_LITERAL
    └── value: "10", type: TOKEN_NUMBER
```

**语义分析**

```
AST_VAR
├── type: TOKEN_INT
├── dataType: DATA_INT        ← 标注类型
├── internal_name: "_var0"    ← 生成内部名
└── value: AST_LITERAL
    └── dataType: DATA_INT
```

同时符号表新增一条 `x → Symbol{type: DATA_INT, internal_name: "_var0"}`。

**IR 生成**

```
IR[0]: Literal(type:0): .t1 = 10
IR[1]: Var(type:0): _var0 = .t1
```

**代码生成**

```asm
mov eax, 10
mov dword [ebp-4], eax
```

## 增量编译

`yxg build` 默认做时间戳检查：源文件和产物比，没变就跳过。`--force` 强制重编。

该逻辑在 `main.c` 的 `needs_rebuild()`。

## 继续阅读

下一章介绍各阶段用到的核心数据结构。
