# 附录 A：编译错误码速查

错误码按阶段分段：`E1xxx` 词法、`E2xxx` 语法、`E3xxx` 语义、`E4xxx` IR、`E5xxx` 代码生成、`E9xxx` 内部错误。

每条都标注了**实测的典型报错文本**，方便对照。

## 词法分析错误（E1xxx）

来源：`Lexer.c`

| 错误码 | 枚举名 | 说明 | 实测报错示例 |
|---|---|---|---|
| E1001 | `ERR_LEX_UNTERMINATED_STRING` | 字符串缺少闭合引号 | `unterminated string literal` |
| E1002 | `ERR_LEX_UNTERMINATED_CHAR` | 字符缺少闭合单引号 | `unterminated character literal` |
| E1003 | `ERR_LEX_UNTERMINATED_COMMENT` | 多行注释缺少 `*/` | `unterminated comment` |
| E1004 | `ERR_LEX_UNKNOWN_CHAR` | 无法识别的字符 | — |
| E1005 | `ERR_LEX_MULTICHAR_LITERAL` | 字符字面量含多个字符 | `character literal too long` |

> 注意：像 `@` 这类符号在多数位置会被后续语法分析先拦下，报的可能是 E2002 而不是 E1004。

## 语法分析错误（E2xxx）

来源：`parser.c`

| 错误码 | 枚举名 | 说明 | 实测报错示例 |
|---|---|---|---|
| E2001 | `ERR_PARSE_UNEXPECTED_TOKEN` | 意外的 Token | — |
| E2002 | `ERR_PARSE_EXPECTED_TOKEN` | 缺少预期的 Token | `expected 'Semicolon' before 'BracketRight'` |
| E2003 | `ERR_PARSE_UNEXPECTED_EOF` | 文件意外结束 | `unexpected end of file` |
| E2004 | `ERR_PARSE_ILLEGAL_OPERAND` | 非法操作数 | `illegal operand '{'` |
| E2005 | `ERR_PARSE_INVALID_TYPE` | 无效的类型关键字 | — |
| E2006 | `ERR_PARSE_MODULE_NOT_FOUND` | 模块未找到 | `module 'xxx' not found` |

**E2002 是最常见的**，典型场景：

| 写法 | 报错 |
|---|---|
| 语句末尾漏分号 | `expected 'Semicolon' before 'Print'` |
| `if cond print x;`（漏花括号） | `expected 'BracketLeft' before 'Print'` |
| `string str = ...;`（`str` 是保留字） | `expected 'Semicolon' before 'Str'` |
| `catch XxxError e {`（漏括号） | `expected 'ParenLeft' before 'Identifier'` |

## 语义分析错误（E3xxx）

来源：`Semantics.c`

| 错误码 | 枚举名 | 说明 | 实测报错示例 |
|---|---|---|---|
| E3001 | `ERR_SEM_UNDECLARED_VAR` | 变量未声明 | `variable 'nope' is not declared` |
| E3002 | `ERR_SEM_DUPLICATE_VAR` | 变量或函数重复声明 | `duplicate variable 'x'` / `duplicate function 'add'` |
| E3003 | `ERR_SEM_TYPE_MISMATCH` | 类型不匹配 | `type mismatch: expected 'int', got 'string'` |
| E3004 | `ERR_SEM_INCOMPATIBLE_TYPES` | 二元运算两侧类型不兼容 | `incompatible types: 'int' and 'string'` |
| E3005 | `ERR_SEM_DIV_BY_ZERO` | 编译期检测到除零 | `division by zero` |
| E3006 | `ERR_SEM_NOT_ARRAY` | 对非数组使用数组操作 | `'.length' requires an array or str` |
| E3007 | `ERR_SEM_INVALID_INDEX` | 数组索引越界或维度非法 | `array index 5 out of bounds (size 3)` / `array dimension must be non-negative` |
| E3008 | `ERR_SEM_NOT_FUNCTION` | 调用了不存在的函数 | `function 'nosuch' is not declared` |
| E3009 | `ERR_SEM_ARG_COUNT_MISMATCH` | 参数个数或类型不匹配 | `no matching function 'f' for 2 argument(s)` |
| E3010 | `ERR_SEM_VOID_VALUE` | void 函数返回值 | `void function should not return a value` |
| E3011 | `ERR_SEM_NO_MAIN` | 缺少 main | `no main function found` |
| E3012 | `ERR_SEM_INVALID_BREAK` | break 不在循环或 switch 中 | ⚠️ 实际报 E9001（见下方说明） |
| E3013 | `ERR_SEM_INVALID_CONTINUE` | continue 不在循环中 | ⚠️ 实际报 E9001（见下方说明） |
| E3014 | `ERR_SEM_FIELD_NOT_FOUND` | 字段不存在或访问权限不足 | `cannot access private field 'x'` |
| E3015 | `ERR_SEM_METHOD_NOT_FOUND` | 方法不存在 | `no matching method 'g'` |
| E3016 | `ERR_SEM_CLASS_NOT_FOUND` | 类未定义 | `unknown class 'Nope'` / `catch type 'Nope' is not a class` |
| E3017 | `ERR_SEM_CANNOT_ASSIGN_NEG` | 负数赋给无符号类型 | `cannot assign negative value to unsigned type 'uint'` |
| E3018 | `ERR_SEM_DUPLICATE_CASE` | switch 有重复 case | `duplicate case value '1'` |
| E3019 | `ERR_SEM_RETURN_TYPE_MISMATCH` | 返回类型不符或控制流未覆盖 | `return type mismatch: expected 'string', got 'int'` / `not all control paths return a value in function 'f'` |
| E3020 | `ERR_SEM_CONDITION_NOT_BOOL` | 条件不是布尔类型 | `condition must be boolean, got 'int'` |
| E3021 | `ERR_SEM_INVALID_CATCH` | catch 子句异常类型无效 | ⚠️ 部分场景报 E3016 |
| E3022 | `ERR_SEM_CANNOT_ASSIGN_CONST` | 给 const 赋值 | `cannot assign to const variable 'M'` |
| E3023 | `ERR_SEM_CONST_OVERFLOW` | 常量表达式溢出 | `array dimension overflows 32-bit integer range` |

### 关于 E3012 / E3013

`break` / `continue` 用在非法位置时，当前版本在 IR 生成阶段才检测到，报的是内部错误：

```
error[E9001]: Internal compiler error: IR: break outside loop/switch
error[E9001]: Internal compiler error: IR: continue outside loop
```

枚举里定义了 E3012 / E3013，但实际路径没走到。看到 E9001 且消息是这两条，就知道是 break/continue 位置错误。

### 关于 E3019 的两种含义

同一个码覆盖两种情况：

```yxg
// 情况一：返回类型不对
pub string f() { return 1; }
// error[E3019]: return type mismatch: expected 'string', got 'int'

// 情况二：非 void 函数有路径没返回
pub int f(int a) { if a > 0 { return 1; } }
// error[E3019]: not all control paths return a value in function 'f'
```

## IR 生成错误（E4xxx）

来源：`IR.c`

| 错误码 | 枚举名 | 说明 |
|---|---|---|
| E4001 | `ERR_IR_NULL_AST` | AST 节点为空（通常意味着前面某阶段出了错） |

## 代码生成错误（E5xxx）

来源：`Codegen_win32.c`

| 错误码 | 枚举名 | 说明 |
|---|---|---|
| E5001 | `ERR_CG_UNKNOWN_VAR` | 代码生成时变量不在符号表，如 `array 'x' not found` |

## 内部错误（E9xxx）

来源：各阶段的 `INTERNAL_ERROR`

| 错误码 | 枚举名 | 说明 |
|---|---|---|
| E9001 | `ERR_INTERNAL` | 编译器内部错误 |
| E9002 | `ERR_OUT_OF_MEMORY` | 内存不足 |
| E9003 | `ERR_UNREACHABLE` | 执行到不应到达的位置 |
| E9004 | `ERR_UNHANDLED_AST_TYPE` | 未处理的 AST 节点类型 |

看到 E9xxx 说明是编译器的问题，不是代码写错。把源码附上反馈。

## 运行时错误

运行时错误**不是编译错误**，程序能编出来但运行时会打印信息并以退出码 1 终止：

| 信息 | 触发条件 |
|---|---|
| `Error: Array index out of bounds` | 数组下标越界 |
| `Error: null pointer dereference` | 访问 null 对象的成员 |
| `map key not found` | `Map.get()` 读不存在的键 |
| `list index out of range` | `List.get()` 下标越界 |

这些可以在 `try` / `catch` 里捕获（`map key not found` 和 `list index out of range` 会抛 `ContainerError`），但数组越界和空指针是直接终止进程。

## 快速排查流程

看到报错时的顺序：

1. **看错误码前缀**
   - `E1xxx` → 检查引号、注释闭合
   - `E2xxx` → 检查分号、花括号、括号、保留字
   - `E3xxx` → 检查类型、声明、调用签名
   - `E9xxx` → 编译器问题

2. **看报错文本里的名字**
   - `'str'` / `'print'` 这类 → 用了保留字当变量名
   - `'Xxx'` 类名 → 忘了 `import` 或类名拼错
   - `method 'xxx'` → 方法名不对，或该方法是 `static` 需要用 `类名.方法()`

3. **常见的高频原因**

| 症状 | 原因 |
|---|---|
| `expected 'Semicolon'` | 漏分号，或用了保留字当标识符 |
| `variable 'X' is not declared` | 忘了 `import`，或变量拼错 |
| `no matching method 'X'` | 静态方法当实例方法调了（`new Math().sqrt()`） |
| `no matching function 'X'` | 参数个数/类型不对，或函数名拼错 |
| `cannot access private field` | 访问了 `pri` 字段，用 getter |
| `override method ... has no matching virtual method` | 父类方法没加 `virtual` |
