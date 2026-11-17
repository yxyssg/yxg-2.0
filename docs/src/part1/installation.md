# 安装与构建

## 环境要求

YXG 编译器目前**仅支持 Windows 平台**，生成 32 位（x86）可执行文件。需要以下工具链：

| 工具 | 最低版本 | 用途 |
|------|----------|------|
| GCC (MinGW-w64，需支持 `-m32`) | 8.0+ | 编译 C 源码、链接目标文件 |
| NASM | 2.15+ | 汇编生成的 `.asm` 文件 |
| Git | 任意版本 | 获取源码 |

> 注意：GCC 必须是能生成 **32 位**代码的版本（`-m32`）。UCRT64/MINGW64 环境需要额外安装 32 位支持包。

### 安装 MinGW 与 NASM

推荐用 MSYS2，它一次装齐：

1. 从 https://www.msys2.org/ 下载并安装 MSYS2
2. 打开 MSYS2 终端，执行：

```
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-nasm
pacman -S git
```

3. 把 MSYS2 的 `bin` 目录加入系统 `PATH`：

```
C:\msys64\ucrt64\bin
```

4. 验证：

```
gcc --version
nasm --version
```

## 获取源码

```
git clone https://github.com/yxyssg/yxg_language.git
cd yxg_language
```

### 目录结构

```
yxg/
├── main.c                # 主程序入口、命令行解析
├── Lexer.c               # 词法分析
├── parser.c              # 语法分析
├── Semantics.c           # 语义分析
├── IR.c                  # 中间代码生成
├── CFG.c                 # 控制流图
├── SSA.c                 # SSA 构造与优化
├── regmap.c              # 寄存器分配
├── Codegen_win32.c       # x86 代码生成
├── Token.c               # Token 定义
├── Error.c               # 错误报告
├── YString.c             # 动态字符串库
├── *.h                   # 头文件
├── runtime/              # 运行时汇编源码
│   ├── runtime_print.asm
│   ├── runtime_string.asm
│   ├── runtime_memory.asm
│   ├── runtime_gc.asm
│   ├── runtime_float.asm
│   ├── runtime_array.asm
│   ├── runtime_system.asm
│   ├── runtime_bounds.asm
│   ├── runtime_socket.asm
│   ├── runtime_window.asm
│   └── ...
├── runtime_cache/        # 运行时汇编的 .obj 缓存
├── lib/yxg/              # 标准库（.yxg 源码）
│   ├── String.yxg
│   ├── Math.yxg
│   ├── System.yxg
│   ├── Collections/
│   └── ...
├── docs/                 # 本文档
└── repro/                # 回归测试用例
```

## 编译编译器

源码和头文件都在仓库根目录（**没有 `src/` 子目录**）。在根目录执行：

```
gcc -m32 -std=c23 -O2 -o yxg.exe ^
    main.c Token.c Lexer.c parser.c Semantics.c Error.c ^
    IR.c Codegen_win32.c regmap.c CFG.c SSA.c YString.c -I.
```

PowerShell 里把续行符 `^` 换成反引号 `` ` ``，或直接写成一行：

```
gcc -m32 -std=c23 -O2 -o yxg.exe main.c Token.c Lexer.c parser.c Semantics.c Error.c IR.c Codegen_win32.c regmap.c CFG.c SSA.c YString.c -I.
```

编译参数说明：

| 参数 | 含义 |
|------|------|
| `-m32` | 生成 32 位代码（YXG 只支持 x86） |
| `-std=c23` | 使用 C23 标准 |
| `-O2` | 优化级别 |
| `-I.` | 头文件在当前目录 |

## 运行时库

编译器**不会**在编译编译器时链接运行时汇编，而是在**编译用户程序时**按需汇编并链接。首次使用某个运行时模块时：

```
runtime/runtime_gc.asm  →  nasm -f win32  →  runtime_cache/runtime_gc.obj
```

因此需要保证：

1. `runtime/` 目录里有 `.asm` 源文件
2. `runtime_cache/` 目录**可写**（编译器要往这里写 `.obj`）

> **常见坑**：如果编译器装在 `C:\Program Files\` 这类需要管理员权限的目录，写 `runtime_cache/` 会失败并报 `Permission denied`。请把编译器放在普通可写目录，或确保 `runtime_cache/` 有写权限。

同理，编译器会相对**自身 exe 所在目录**查找 `runtime/`、`runtime_cache/`、`lib/`。所以移动 exe 时要连这三个目录一起移。

## 验证安装

```
yxg -version
```

输出：

```
version: 1.0.0-alpha.1
```

查看帮助：

```
yxg -help
```

输出：

```
Usage: yxg <command> [options] [files|dirs...]

Commands:
  build [options] [files|dirs...]   Compile project
  run   [options] [options] [files|dirs...]   Compile and run
  clean                             Remove build artifacts
  new   <name>                      Create a new project
  help                              Show this help

Options:
  -o <path>       Output executable path
  -r              Compile and run (same as 'run')
  -a              Emit assembly only
  -w              Windowed-only program (no console)
  -noassert       Disable assert checks
  --force         Force rebuild (ignore timestamps)
  -d              Debug mode

Others:
  -time           Show current time
  -version        Show current version
```

## 编译并运行第一个程序

创建 `hello.yxg`：

```
pub void main() {
    print "Hello, YXG!\n";
}
```

编译并运行：

```
yxg run hello.yxg
```

预期输出：

```
Compilation complete, output file is: output.exe
Total compilation time: 0.350000 second
Hello, YXG!
```

## 创建项目

除了编译单个文件，也可以用 `new` 生成标准项目骨架：

```
yxg new demo
```

输出：

```
created project 'demo' (console template)
```

生成的目录结构：

```
demo/
├── yxg.toml
├── README.md
├── .gitignore
└── src/
    └── Main.yxg
```

`yxg.toml` 内容：

```toml
[project]
name = "demo"
version = "0.1.0"
entry = "src/Main.yxg"

[output]
path = "bin/demo.exe"

[build]
# 编译时附加参数，等同命令行效果，命令行优先级更高
# 支持: -a -w -r -noassert --force -o <path>
flags = ""
```

在项目根目录（有 `yxg.toml` 的那一层）执行：

```
cd demo
yxg build        # 产物: bin/demo.exe
yxg run          # 编译并运行
yxg clean        # 清理构建产物
```

## 编译流程

执行 `yxg build` 时内部依次经过：

```
hello.yxg
   │
   ▼
[词法分析]   → Token 序列
   │
   ▼
[语法分析]   → AST
   │
   ▼
[语义分析]   → 带类型的 AST + 符号表
   │
   ▼
[IR 生成]    → 三地址中间表示
   │
   ▼
[CFG 构建]   → 基本块 + 控制流图
   │
   ▼
[SSA 构造]   → SSA 形式 + 优化
   │
   ▼
[寄存器分配] → Chaitin-Briggs 图着色
   │
   ▼
[代码生成]   → output.asm
   │
   ▼
[NASM 汇编]  → output.obj
   │
   ▼
[GCC 链接]   → output.exe
```

用 `-a` 可以停在汇编阶段，查看生成的 `output.asm`。

## 常见问题

### 提示 "nasm 不是内部或外部命令"

NASM 未安装或不在 `PATH` 中。重新安装时勾选"添加到 PATH"，或手动把 NASM 目录加进系统环境变量。

### 提示 "gcc 不是内部或外部命令"

MinGW 未安装或不在 `PATH` 中。检查 `C:\msys64\ucrt64\bin` 是否已加入 `PATH`。

### 编译报 "Permission denied"（写 runtime_cache 失败）

编译器所在目录不可写。把整个 yxg 目录移到可写位置，例如 `D:\yxg\`。

### 链接时提示 undefined reference

通常是 `runtime_cache/` 里的 `.obj` 缺失或损坏。删除 `runtime_cache/` 下的 `.obj` 后重新编译，让编译器重新生成。

### 提示 "无法解析的外部符号 _ExitProcess@4"

链接时缺少 Windows 系统库。确保用 MinGW 的 `gcc` 链接（它会自动带 kernel32 等）。

### 生成的 exe 提示缺少 DLL

YXG 生成 32 位程序。如果提示缺 `libgcc_s_dw2-1.dll`，把 MinGW 的 `bin` 目录加入 `PATH`。

### 文件名大小写

源码文件名必须和 `import` 中写的一致（Windows 上不敏感，但跨平台/发布时要注意）。`Semantics.c`、`Lexer.c`、`Codegen_win32.c` 这些源码文件名首字母大写。

## 继续阅读

环境搭好后，下一章写第一个 YXG 程序，逐行拆解基本语法。
