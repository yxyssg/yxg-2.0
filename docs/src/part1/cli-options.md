# 命令行参数说明

## 基本用法

YXG 2.0 采用**子命令**风格：

```
yxg <command> [options] [files|dirs...]
```

| 子命令 | 作用 |
|---|---|
| `yxg build` | 编译项目或源文件 |
| `yxg run` | 编译并立即运行 |
| `yxg clean` | 删除构建产物 |
| `yxg new <name>` | 创建新项目骨架 |
| `yxg help` | 显示帮助 |

## 子命令详解

### build：编译

编译单个文件：

```
yxg build hello.yxg
```

默认输出 `output.exe`（当前目录）。

指定输出名：

```
yxg build hello.yxg -o myapp.exe
```

一次编译多个文件：

```
yxg build main.yxg utils.yxg math.yxg -o app.exe
```

所有文件会被依次解析，合并成一个编译单元。

编译项目（在有 `yxg.toml` 的目录里）：

```
cd demo
yxg build
```

按 `yxg.toml` 的 `[output] path` 输出，例如 `bin/demo.exe`。

### run：编译并运行

```
yxg run hello.yxg
```

输出示例：

```
Compilation complete, output file is: output.exe
Total compilation time: 0.350000 second
Hello, YXG!
```

项目模式下：

```
cd demo
yxg run
```

`-r` 是 `run` 的等价短选项：

```
yxg build hello.yxg -r
```

### clean：清理

清理项目构建产物：

```
yxg clean
```

输出：

```
cleaned 12 file(s).
```

### new：创建项目

```
yxg new demo
```

输出：

```
created project 'demo' (console template)
```

生成的骨架：

```
demo/
├── yxg.toml
├── README.md
├── .gitignore
└── src/
    └── Main.yxg
```

## 编译选项

| 选项 | 含义 |
|---|---|
| `-o <path>` | 指定输出可执行文件路径 |
| `-r` | 编译后立即运行（等同 `run`） |
| `-a` | 只生成汇编 `output.asm`，不汇编不链接 |
| `-w` | 生成窗口程序（不弹控制台） |
| `-noassert` | 关闭 `assert` 检查 |
| `--force` | 强制重新编译（忽略时间戳，不做增量跳过） |
| `-d` | 预留参数，当前版本不输出任何调试信息 |

### -o：指定输出名

```
yxg build hello.yxg -o myapp.exe
```

不加 `-o` 时默认输出 `output.exe`。

### -a：只生成汇编

```
yxg build hello.yxg -a
```

只跑到代码生成，产出 `output.asm` 后停止，不调 NASM 和 GCC。适合研究代码生成结果。

生成的汇编大致长这样：

```
_usermain:
  push ebp
  mov  ebp, esp
  push ebx
  push esi
  push edi
  sub  esp, 32
  mov  eax, _str0
  push 3
  push eax
  call _print
  add  esp, 8
  ...
```

### -w：窗口程序

```
yxg build gui.yxg -w -o gui.exe
```

生成的应用不弹控制台窗口，适合 GUI 程序。

### --force：强制重编

```
yxg build hello.yxg --force
```

默认会做增量检查（源文件时间戳没变则跳过），加 `--force` 强制完整重编。

### -g：生成调试信息（2.5 新增）

```
yxg build hello.yxg -o hello.exe -g
```

加上 `-g` 后会额外生成 `hello.exe.map`，内容是「机器码地址 → 源码行号 → 函数名」，以及局部变量在栈上的偏移：

```
# yxg dbgmap v1
# src hello.yxg
00002be0 1 _main
00002c0d 2 _main
00002c23 5 _main
# local . 144 count
# local . 148 total
```

**加了 `-g` 还有一个重要效果**：程序崩溃时会自动打印源码位置和调用栈，不用挂调试器也能定位：

```
*** CRASH ***
  fault address : 0x00D12C0A
  module offset : 0x00002C0A
  source        : crash.yxg:3   (in  deep_int)
  stack:
    0x00D12C4C       crash.yxg:6   (in  middle_int)
```

`-g` 不影响代码优化，只增加 map 文件和崩溃处理器。发布版本可以不加，排查线上问题时再加。

VSCode 里按 F5 调试时会**自动加 `-g`** 重新编译。

详见 [调试：断点、单步、崩溃定位](../part2/debugging.md)。

### -d：预留参数（当前无效果）

```
yxg build hello.yxg -d
```

`-d` 不产生任何输出，加不加编译结果完全一样。这是早期为调试功能预留的占位参数，2.5 的调试信息请用上面的 `-g`。

### 想看中间结果怎么办

`-d` 不可用，但有两个替代办法：

**1. 看生成的汇编**

```
yxg build hello.yxg -a
```

会产出 `output.asm`，可以读到最终生成的 32 位 x86 汇编。这是目前唯一能直接观察编译结果的入口。

**2. 自己改编译器加打印**

编译器源码里各阶段都有现成的数据结构，加几行 `fprintf` 就能输出：

| 想看什么 | 改哪个文件 | 加在哪 |
|---|---|---|
| Token 列表 | `Lexer.c` | 词法主循环里 |
| AST | `parser.c` | 解析完成后 |
| 符号表 | `Semantics.c` | `Semantics()` 结尾 |
| IR | `IR.c` | `parse_IR()` 返回值处 |
| 寄存器分配 | `regmap.c` | `regmap_allocate_from_cfg()` 内的分配循环 |

`main.c` 里已有一个被注释掉的 `print_IR(full_ir_head);` 调用，取消注释即可打印全部 IR。

## 其他参数

| 参数 | 含义 |
|---|---|
| `-version` | 显示版本号 |
| `-help` | 显示帮助 |
| `-time` | 显示当前时间 |

> 注意这些是**单横线**形式：`-version`、`-help`。不是 `--version` / `--help`。

```
yxg -version
```

输出：

```
version: 2.0.0
```

```
yxg -help
```

输出：

```
Usage: yxg <command> [options] [files|dirs...]

Commands:
  build [options] [files|dirs...]   Compile project
  run   [options] [files|dirs...]   Compile and run
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

Legacy single-shot usage:
  yxg <files...> -o <out> -r
```

帮助里 `-d` 写的是 `Debug mode`，但如上一节所述，**当前版本它什么都不做**。

## 旧式单次调用

早期版本没有子命令，直接写：

```
yxg hello.yxg -o myapp.exe -r
```

这种**旧式写法仍然兼容**，但新代码建议用子命令形式。

## 多文件与模块

### 方式一：命令行列出多个文件

```
yxg build main.yxg math.yxg -o app.exe
```

### 方式二：用 import

`main.yxg`：

```
import yxg.Math;
import "mylib/custom.yxg";

pub void main() {
    print Math.sqrt(16.0), "\n";
}
```

只需编译主文件：

```
yxg build main.yxg -o app.exe
```

编译器会自动查找并编译 `import` 的模块。

模块搜索路径：

1. 当前工作目录
2. 编译器 exe 所在目录下的 `lib/`
3. `YXG_PATH` 环境变量指定的目录（分号分隔）

设置自定义路径：

```
set YXG_PATH=C:\my_libs;D:\shared
```

PowerShell：

```
$env:YXG_PATH = "C:\my_libs;D:\shared"
```

## 运行时库自动链接

编译器根据源码用到的功能自动决定链接哪些运行时模块：

| 源码中用到的 | 自动链接 |
|---|---|
| `print` | `runtime_print.obj` |
| 字符串操作 | `runtime_string.obj` |
| `new` / 类实例化 | `runtime_gc.obj` + `runtime_memory.obj` |
| 浮点比较 | `runtime_float.obj` |
| 数组打印 | `runtime_array.obj` |
| `input` / 系统调用 | `runtime_system.obj` |
| 数组下标 | `runtime_bounds.obj` |
| socket | `runtime_socket.asm` |
| sqlite | `runtime_sqlite.obj` |

`.obj` 缓存放在编译器目录下的 `runtime_cache/`。**该目录必须可写**。

## 退出码

| 退出码 | 含义 |
|---|---|
| 0 | 编译成功 |
| 1 | 编译失败（有错误） |

警告不影响退出码，错误才会。

## 继续阅读

掌握命令行后，下一节是yxg.toml的详解。
