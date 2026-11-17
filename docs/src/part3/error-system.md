# 错误处理系统

## 概述

编译器有一套贯穿各阶段的诊断系统。它能报告错误位置、错误码、严重级别，并显示源代码上下文。

## 错误级别

`Error.h` 定义五种严重级别：

| 级别 | 枚举值 | 输出标签 | 颜色 | 行为 |
|---|---|---|---|---|
| 致命 | `SEV_FATAL` | `fatal error` | 亮红 | 立即终止 |
| 错误 | `SEV_ERROR` | `error` | 红 | 记录后继续检查 |
| 警告 | `SEV_WARNING` | `warning` | 黄 | 记录后继续 |
| 提示 | `SEV_NOTE` | `note` | 青 | 仅输出 |
| 建议 | `SEV_HINT` | `hint` | 绿 | 仅输出 |

对应关系来自 `Error.c:140` 的 `sev_label()` 和 `sev_color()`。

## 错误码体系

错误码按阶段分段：

| 阶段 | 范围 | 数量 | 例子 |
|---|---|---|---|
| 词法分析 | `E1001` ~ `E1005` | 5 | `E1001` 未终止的字符串 |
| 语法分析 | `E2001` ~ `E2006` | 6 | `E2002` 缺少预期的 Token |
| 语义分析 | `E3001` ~ `E3023` | 23 | `E3001` 未声明的变量 |
| IR 生成 | `E4001` | 1 | `E4001` 空 AST 节点 |
| 代码生成 | `E5001` | 1 | `E5001` 未知变量 |
| 内部错误 | `E9001` ~ `E9004` | 4 | `E9001` 内部编译器错误 |

完整清单见[附录 A：编译错误码速查](../appendix/error-codes.md)。

## 报错格式

实际输出形状：

```
hello.yxg:5:10: error[E3001]: variable 'x' is not declared
    5 |     print x;
      |           ^
```

四段结构：

```
<文件>:<行>:<列>: <级别>[<错误码>]: <描述>
    <行号> | <源代码行>
           | <插入符标记位置>
```

## 诊断宏

各宏定义在 `Error.h`：

```c
#define ERROR(code, loc, ...)    diagnostic(SEV_ERROR,   (code), (loc), __VA_ARGS__)
#define WARNING(code, loc, ...)  diagnostic(SEV_WARNING, (code), (loc), __VA_ARGS__)
#define FATAL(code, loc, ...)    diagnostic(SEV_FATAL,   (code), (loc), __VA_ARGS__)
#define NOTE(loc, ...)           diagnostic(SEV_NOTE,    0,      (loc), __VA_ARGS__)
#define HINT(loc, ...)           diagnostic(SEV_HINT,    0,      (loc), __VA_ARGS__)
```

**注意 `NOTE` 和 `HINT` 不带错误码**（第二个参数固定是 `0`），另外三个都要传错误码。

### 使用示例

```c
// 错误
ERROR(ERR_SEM_UNDECLARED_VAR,
      LOC("hello.yxg", 5, 10),
      "variable '%s' is not declared", "x");

// 警告
WARNING(ERR_SEM_TYPE_MISMATCH,
        LOC("hello.yxg", 3, 5),
        "type mismatch: expected '%s', got '%s'", "int", "string");

// 致命错误（立即终止）
FATAL(ERR_INTERNAL, LOC_NONE,
      "cannot allocate memory for symbol table");

// 附加提示（无错误码）
NOTE(LOC("hello.yxg", 2, 1),
     "variable 'x' was declared here");

// 修复建议（无错误码）
HINT(LOC("hello.yxg", 5, 10),
     "did you mean 'y'?");
```

### 内部错误宏

```c
#define INTERNAL_ERROR(fmt, ...) \
    diagnostic(SEV_FATAL, ERR_INTERNAL, LOC_NONE, \
               "Internal compiler error: " fmt, ##__VA_ARGS__)

#define OUT_OF_MEMORY() \
    diagnostic(SEV_FATAL, ERR_OUT_OF_MEMORY, LOC_NONE, "Out of memory")
```

`INTERNAL_ERROR` 前缀固定是 `Internal compiler error: `，看到这个说明是编译器自己出了问题。

### 位置构造

```c
#define LOC_NONE     ((SourceLoc){NULL, 0, 0})
#define LOC(f,l,c)   ((SourceLoc){(f), (l), (c)})
```

## 错误管理器

定义在 `Error.h`：

```c
typedef struct {
    const char* filename;
    const char* source_text;
} SourceFile;

typedef struct {
    bool warnings_as_errors;    // 警告视为错误
    bool suppress_warnings;     // 抑制所有警告
    int max_errors;             // 最大错误数
    bool use_color;             // 颜色输出
    bool show_context;          // 显示源代码上下文
} ErrorOptions;

typedef struct {
    ErrorOptions options;
    int error_count;
    int warning_count;
    int fatal_count;
    bool has_error;
    SourceFile* source_files;   // 已注册的源文件
    int source_file_count;
} ErrorManager;
```

`Error.c:171` 的默认值：

```c
error_mgr->options.max_errors = 128;
error_mgr->options.warnings_as_errors = false;
error_mgr->options.suppress_warnings = false;
error_mgr->options.use_color = terminal_supports_color();
error_mgr->options.show_context = true;
```

**`max_errors` 默认 128**，超过后停止编译。

## 错误恢复

不同阶段的恢复策略不同。

### 词法分析

遇到无法识别的字符时跳过该字符，继续切词：

```c
ERROR(ERR_LEX_UNKNOWN_CHAR, loc, "unexpected character");
// 跳过，继续扫描
```

### 语法分析

恐慌模式：跳过 Token 直到同步点（分号、右花括号等）：

```c
while (!parser_current_is_type(parser, TOKEN_SEMICOLON) &&
       !parser_current_is_type(parser, TOKEN_EOF) &&
       !is_Modifier(parser)) {
    parser_advance(parser);
}
if (parser_current_is_type(parser, TOKEN_SEMICOLON))
    parser_advance(parser);
```

### 语义分析

把节点标成 `DATA_ERROR` 抑制级联错误：

```c
if (!types_compatible(declared, actual)) {
    ERROR(ERR_SEM_TYPE_MISMATCH, loc, "...");
    node->dataType = DATA_ERROR;    // 后续检查遇到 DATA_ERROR 就跳过
}
```

这样一处类型错误不会连带报出十个「类型不匹配」。

## 最大错误数

`Error.c:256` 附近：

```c
if (error_mgr->error_count >= error_mgr->options.max_errors) {
    fprintf(err_out(), "Too many errors, stopping compilation.\n");
    exit(EXIT_FAILURE);
}
```

## 颜色输出

`Error.c:17` 的检测逻辑：

```c
static bool terminal_supports_color(void) {
#ifdef _WIN32
    return true;                      // Windows 直接启用
#else
    static int cached = -1;
    if (cached == -1) {
        const char *term = getenv("TERM");
        if (!term) { cached = 0; return false; }
        cached = (strstr(term, "color") || strstr(term, "xterm") ||
                  strstr(term, "screen") || strstr(term, "256color"))
                 ? 1 : 0;
    }
    return cached == 1;
#endif
}
```

**Windows 下不检测，直接开颜色**。Linux/Mac 才看 `TERM` 环境变量，认 `color` / `xterm` / `screen` / `256color`。结果缓存到静态变量，只查一次。

## 实测报错对照

下面这些都是实际编译出来的，可以直接对照：

### 词法错误

```yxg
string s = "Hello, World!;
```
```
error[E1001]: unterminated string literal
```

```yxg
/* This comment never ends
```
```
error[E1003]: unterminated multi-line comment
```

### 语法错误

```yxg
pub void main() { int x = 10 }
```
```
error[E2002]: expected 'Semicolon' before 'BracketRight'
```

漏分号报的是 **E2002**（缺少预期的 Token），不是 E2001。

```yxg
pub void main() { int 123 = 456; }
```
```
error[E2002]: expected variable name
```

### 语义错误

```yxg
print x;
```
```
error[E3001]: variable 'x' is not declared
```

```yxg
int x = "hello";
```
```
error[E3003]: type mismatch: expected 'int', got 'string'
```

```yxg
int x = 1;
int x = 2;
```
```
error[E3002]: duplicate variable 'x'
```

```yxg
int z = 10 / 0;
```
```
error[E3005]: division by zero
```

```yxg
if 42 { }
```
```
error[E3020]: condition must be boolean, got 'int'
```

### 内部错误

```yxg
break;
```
```
error[E9001]: Internal compiler error: IR: break outside loop/switch
```

`break` / `continue` 放在非法位置时，枚举里定义了 `E3012` / `E3013`，但实际路径报的是 `E9001`。看到 `Internal compiler error: IR: ...` 就知道是这个问题。

### 代码生成错误

```yxg
int128 a = 1;
print a;
```
```
fatal error[E5001]: printing int128/uint128 is not supported yet (use int64)
```

## 增加新错误码的步骤

1. 在 `Error.h` 的对应枚举里加成员（注意不要重复编号）
2. 在报错处用 `ERROR(新码, LOC(...), "描述")`
3. 到 `appendix/error-codes.md` 补一行

## 继续阅读

下一部分进入实战教程。
