# YXG Compiler / YXG 编译器

[![License](https://img.shields.io/badge/license-Apache-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.0.0-green.svg)](https://github.com/yxyssg/yxg-2.0/releases)

Want to read the document? Click here / 想阅读文档？点击这里：

https://yxyssg.github.io/yxg-2.0/
 
**YXG** is a statically-typed C-like language that compiles to native Windows x86 executables. The compiler is entirely hand-written in C without Flex/Bison/LLVM, making it ideal for learning the full compilation pipeline.
**YXG** 是一门静态类型的类 C 语言，编译到原生 Windows x86 可执行文件。

## Features / 特性

### Language Features / 语言特性

- **Basic types / 基本类型**: `int`, `uint`, `int64`, `float`, `ufloat`, `char`, `string`/`str`, `bool`, `void`
- **Control flow / 控制流**: `if`/`else if`/`else`, `switch`/`case`/`default`, `while`, `do-while`, `for`, `for-each` (arrays and containers)
- **Functions / 函数**: overloading, methods, constructors, closures, generic functions `<T> T f(T x)` / 函数重载、方法、构造函数、闭包、泛型函数
- **Generics / 泛型**: generic classes `class Box<T>`, multiple type params `class Pair<A,B>`, nested generics `List<List<X>>`, generic inheritance `class X : List<Y>`, constraints `<T : Base>` / `<T >: Base>`, variance `<+T>` / `<-T>` / 泛型类、多类型参数、嵌套泛型、泛型继承、类型约束、协变/逆变
- **Arrays / 数组**: multi-dimensional, dynamic (`new T[n]`), stack arrays, `.length`, value semantics on assignment / 多维数组、动态数组、栈数组、`.length`、赋值时值语义复制
- **Classes & Objects / 类与对象**: single/multiple inheritance, `pub`/`pro`/`pri` access control, methods, constructors, `this`, `new` / 单/多继承、访问控制、方法、构造函数、`this`、`new`
- **Exceptions / 异常**: `try`/`catch`/`finally`, `throw`, `ERROR` base class / 异常处理
- **Enums / 枚举**: explicit/implicit assignment / 支持显式/隐式赋值
- **Module system / 模块系统**: `import yxg.Math;` dot-path imports, string paths / 点路径导入、字符串路径导入
- **Operators / 运算符**: arithmetic, comparison, logical (`&&`/`||` short-circuit), bitwise (`&`/`|`/`~`), shift (`<<`/`>>`), compound assignment, `++`/`--`, ternary, `as` casts / 算术、比较、逻辑短路、位运算、移位、复合赋值、自增自减、三目、`as` 转换
- **Global variables / 全局变量**: forward references allowed (definition may appear after use) / 支持前向引用（定义可出现在使用之后）
- **Output / 输出**: `print` takes any number of arguments, does **not** auto-insert spaces or newlines / `print` 接受任意数量参数，**不自动**插入空格或换行
- **Input / 输入**: `input(变量1, 变量2, ...)` parses one line by variable type / 按变量类型解析一行输入
- **String formatting / 字符串格式化**: `format("a={}, b={}", x, y)` / 占位符格式化

### Standard Library / 标准库

| Module / 模块 | Import / 导入 | Highlights / 主要内容 |
|---|---|---|
| String | `yxg.String` | `len`, `sub`, `indexOf`, `toUpperStr`, `trim`, `split`, `replace` (all `static`) |
| Math | `yxg.Math` | `abs`, `sqrt`, `pow`, `random`, `sin`/`cos`, `PI`/`E` (all `static`) |
| System | `yxg.System` | `time`, `sleep`, `exit`, `env`, `argc`/`argv` (all `static`) |
| File | `yxg.File` | `readAllText`, `writeAllText`, path helpers |
| Json | `yxg.Json` | `parse`, `stringify`, `JsonValue` tree |
| Collections | `yxg.Collections.*` | `List<T>`, `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>`, `Database` (SQLite) |
| Socket | `yxg.Socket` | TCP/UDP sockets, `SocketClient`, `SocketServer`, `SocketUDP` |
| ThreadPool | `yxg.ThreadPool` | `TPRequest`/`TPResponse`/`TPHandler`, HTTP base for web apps |

> **Note / 注意**: `String`, `Math`, `System` methods are all `static`. Call them as `Math.sqrt(x)`, **not** `new Math().sqrt(x)`.
> `String`/`Math`/`System` 的方法全是 `static`，用 `Math.sqrt(x)` 调用，**不是** `new Math().sqrt(x)`。

### Compiler Features / 编译器特性

- 🔍 Hand-written lexer (state machine) / 手写词法分析器（状态机）
- 🌳 Recursive descent parser with error recovery / 递归下降语法分析器（带错误恢复）
- 📝 Full semantic analysis (type checking, scoping, symbol table, generics instantiation) / 完整语义分析（类型检查、作用域、符号表、泛型实例化）
- 🔧 Three-address code IR (60+ instructions) / 三地址码 IR（60+ 种指令）
- ⚡ SSA optimizer (constant folding, constant propagation, copy propagation, dead code elimination) / SSA 优化器
- 🎯 x86-32 Windows code generation (NASM assembly, Chaitin-Briggs graph-coloring register allocation) / x86-32 Windows 代码生成
- 🗑️ Built-in garbage collector (mark-sweep) / 内置垃圾回收器（标记-清除）
- 🛡️ Runtime bounds checking and null pointer detection / 运行时数组越界检查与空指针检测
- 🎨 GCC/Clang-style colored error output / GCC/Clang 风格彩色错误输出

## Quick Start / 快速开始

### Requirements / 环境要求

| Tool / 工具 | Purpose / 用途 | Download / 下载 |
|-------------|----------------|-----------------|
| GCC (MinGW-w64, needs `-m32` / 需支持 `-m32`) | C compiler + linker / C 编译器 + 链接器 | [MinGW-w64](https://www.mingw-w64.org/) |
| NASM        | Assembler / 汇编器 | [NASM](https://www.nasm.us/) |
| Windows     | Target platform / 目标平台 | x86 or x64 (compiles to 32-bit) / x86 或 x64（编译为 32 位） |

Make sure both `gcc` and `nasm` are in your `PATH`:
确保 `gcc` 和 `nasm` 都在 `PATH` 中：

    gcc --version
    nasm --version

### Building the Compiler / 编译编译器

    git clone https://github.com/yxyssg/yxg-2.0.git
    cd yxg

    gcc -m32 -std=c23 -O2 -o yxg.exe main.c Token.c Lexer.c parser.c Semantics.c Error.c IR.c Codegen_win32.c regmap.c CFG.c SSA.c YString.c -I.

> Source files are in the repository **root** (there is no `src/` subdirectory).
> 源码在仓库**根目录**（没有 `src/` 子目录）。

### Runtime Objects / 运行时对象文件

The compiler assembles runtime `.asm` files on demand into `runtime_cache/*.obj`.
编译器会按需把 `runtime/` 下的 `.asm` 汇编到 `runtime_cache/*.obj`。

**Important / 重要**: `runtime_cache/` must be **writable**. If you install yxg under `C:\Program Files\`, assembling will fail with `Permission denied`. Put the whole `yxg` folder somewhere writable (e.g. `D:\yxg\`).
`runtime_cache/` 必须**可写**。装在 `C:\Program Files\` 下会报 `Permission denied`，请把整个 yxg 目录放到可写位置（如 `D:\yxg\`）。

The compiler looks for `runtime/`, `runtime_cache/` and `lib/` **relative to the exe location**, so move all four together.
编译器按**自身 exe 所在目录**查找 `runtime/`、`runtime_cache/`、`lib/`，移动时要连同这几个目录一起移。

### First Program / 第一个程序

Create `hello.yxg`:
创建 `hello.yxg`：

    pub void main() {
        print "Hello, YXG!\n";
    }

Compile and run / 编译并运行：

    yxg run hello.yxg

Output / 输出：

    Hello, YXG!

### Creating a Project / 创建项目

    yxg new demo
    cd demo
    yxg build
    yxg run

This generates:
生成的结构：

    demo/
    ├── yxg.toml
    ├── README.md
    ├── .gitignore
    └── src/
        └── Main.yxg

`yxg.toml`:

    [project]
    name = "demo"
    version = "0.1.0"
    entry = "src/Main.yxg"

    [output]
    path = "bin/demo.exe"

### More Examples / 更多示例

**Variables & Types / 变量与类型**：

    pub void main() {
        int age = 25;
        float pi = 3.14;
        string name = "YXG";
        bool is_cool = true;
        print name, " is ", age, " years old\n";
        print "Pi = ", pi, "\n";
    }

Output / 输出：

    YXG is 25 years old
    Pi = 3.14

**Arrays / 数组**：

    pub void main() {
        int[] numbers = {1, 2, 3, 4, 5};
        print "length = ", numbers.length, "\n";

        for int i = 0; i < numbers.length; i++ {
            print numbers[i], "\n";
        }

        for int n : numbers {
            print n, "\n";
        }
    }

**Classes & Inheritance / 类与继承**：

    pub class Animal {
        pub string name;

        pub virtual void speak() {
            print this.name, " makes a sound\n";
        }
    }

    pub class Dog : Animal {
        pub override void speak() {
            print this.name, " barks!\n";
        }
    }

    pub void main() {
        Dog dog = new Dog();
        dog.name = "Rex";
        dog.speak();               // Rex barks!

        Animal a = new Dog();
        a.name = "Spot";
        a.speak();                 // Spot barks!
    }

> Polymorphism requires `virtual` on the base method and `override` on the derived one. Without them, calls bind statically to the base version.
> 多态需要基类方法加 `virtual`、子类方法加 `override`。不加就是静态绑定，会走基类版本。

> Objects must be created with `new`. A plain declaration `Dog d;` initializes `d` to **null**; accessing its members reports `Error: null pointer dereference`.
> 对象必须用 `new` 创建。声明 `Dog d;` 后 `d` 是 **null**，访问成员会报 `Error: null pointer dereference`。

**Generics / 泛型**：

    import yxg.Collections.List;
    import yxg.Json;

    pub class Box<T> {
        pro T value;

        pub Box(T v) {
            this.value = v;
        }

        pub T get() {
            return this.value;
        }
    }

    pub void main() {
        Box<int> b = new Box<int>(42);
        print "box = ", b.get(), "\n";

        List<List<JsonValue>> matrix = new List<List<JsonValue>>();
        List<JsonValue> row = new List<JsonValue>();
        row.add(Json.makeInt(1));
        matrix.add(row);
        print "rows = ", matrix.size(), "\n";
    }

**SQLite / 数据库**：

    import yxg.Collections.Database;

    pub void main() {
        Database db = new Database("app.db");
        if !db.isOpen() {
            print "open failed\n";
            return;
        }
        db.exec("CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT)");
        db.exec("INSERT INTO users(name) VALUES('alice')");
        print "lastId = ", db.lastInsertId(), "\n";

        Statement st = db.prepare("SELECT id, name FROM users");
        while st.step() == 100 {
            print "row ", st.columnInt(0), " = ", st.columnText(1), "\n";
        }
        st.finalize();
        db.close();
    }

**Web Server / Web 服务**：

    import yxg.Socket;
    import yxg.ThreadPool;

    pub class Handler : TPHandler {
        pub override void handle(TPRequest req, TPResponse res) {
            res.html("hello from YXG");
        }
    }

    pub void main() {
        TPServer server = new TPServer(8080);
        server.run(new Handler());       // blocks, accepting requests / 阻塞运行
    }

## Command-line Arguments / 命令行参数

    yxg <command> [options] [files|dirs...]

### Commands / 子命令

| Command / 子命令 | Description / 说明 |
|---|---|
| `build` | Compile project or source files / 编译项目或源文件 |
| `run` | Compile and run / 编译并运行 |
| `clean` | Remove build artifacts / 清理构建产物 |
| `new <name>` | Create a new project / 创建新项目 |
| `help` | Show help / 显示帮助 |

### Options / 选项

| Option / 选项 | Description / 说明 |
|---|---|
| `-o <path>` | Output executable path / 指定输出路径（default: `output.exe` in cwd / 默认当前目录的 `output.exe`） |
| `-r` | Compile and run (same as `run`) / 编译后立即运行 |
| `-a` | Emit assembly only / 只生成汇编 |
| `-w` | Windowed program (no console) / 窗口程序（无控制台） |
| `-noassert` | Disable assert checks / 关闭 assert 检查 |
| `--force` | Force rebuild / 强制重新编译 |
| `-d` | Reserved, no effect in current version / 预留参数，当前无效果 |
| `-version` | Show version / 显示版本 |
| `-help` | Show help / 显示帮助 |

> Note / 注意: these are **single-dash** options (`-version`, `-help`), not `--version`/`--help`.
> 注意是**单横线**（`-version`、`-help`），不是 `--version`。

Legacy single-shot usage is still supported:
旧式单次调用仍兼容：

    yxg hello.yxg -o myapp.exe -r

## Project Structure / 项目结构

    yxg/
    ├── main.c              # Entry, compilation flow, CLI parsing / 入口、编译流程、命令行解析
    ├── Token.c/h           # Token types, keyword matching / Token 类型、关键字匹配
    ├── Lexer.c/h           # Lexical analyzer / 词法分析器
    ├── parser.c/h          # Recursive descent parser / 递归下降语法分析器
    ├── AST.h               # AST node definitions / 抽象语法树节点定义
    ├── Semantics.c/h       # Semantic analysis / 语义分析
    ├── IR.c/h              # Intermediate representation / 中间表示
    ├── CFG.c/h             # Control flow graph / 控制流图
    ├── SSA.c/h             # SSA construction & optimization / SSA 构造与优化
    ├── regmap.c/h          # Register allocation / 寄存器分配
    ├── Codegen_win32.c     # x86-32 code generation / x86-32 代码生成
    ├── Codegen.h           # Codegen interface / 代码生成接口
    ├── Error.c/h           # Error reporting / 错误报告
    ├── YString.c/h         # Dynamic string library / 动态字符串库
    ├── runtime/            # Runtime assembly sources / 运行时汇编源码
    │   ├── runtime_print.asm
    │   ├── runtime_string.asm
    │   ├── runtime_memory.asm
    │   ├── runtime_gc.asm
    │   ├── runtime_float.asm
    │   ├── runtime_array.asm
    │   ├── runtime_system.asm
    │   ├── runtime_bounds.asm
    │   ├── runtime_socket.asm
    │   └── ...
    ├── runtime_cache/      # Assembled .obj cache (must be writable) / .obj 缓存（需可写）
    ├── lib/yxg/            # Standard library (.yxg modules) / 标准库
    ├── docs/               # This documentation (mdBook) / 本文档
    └── repro/              # Regression test cases (.yxg) / 回归测试用例

## Compilation Pipeline / 编译流程

    Source (.yxg) / 源文件 (.yxg)
        │
        ▼
    [Lexer / 词法分析] ──→ Token stream / Token 流
        │
        ▼
    [Parser / 语法分析] ──→ AST / AST 树
        │
        ▼
    [Semantic Analysis / 语义分析] ──→ Annotated AST + symbol table / 带类型标注的 AST + 符号表
        │
        ▼
    [IR Generation / IR 生成]  ──→ Three-address code IR / 三地址码 IR
        │
        ▼
    [CFG Construction / CFG 构建] ──→ Control flow graph / 控制流图
        │
        ▼
    [SSA Construction / SSA 构造] ──→ SSA form + φ nodes / SSA 形式 + φ 节点
        │
        ▼
    [SSA Optimization / SSA 优化] ──→ Const/Copy prop, folding, DCE / 常量传播、折叠、DCE
        │
        ▼
    [SSA Destruction / SSA 降级] ──→ Out-of-SSA / 退出 SSA
        │
        ▼
    [Register Allocation / 寄存器分配] ──→ Chaitin-Briggs graph coloring / 图着色
        │
        ▼
    [Code Generation / 代码生成] ──→ NASM assembly (.asm) / NASM 汇编
        │
        ▼
    [NASM]     ──→ Object file (.obj) + Runtime (.obj) / 目标文件 + 运行时
        │
        ▼
    [GCC Link / GCC 链接] ──→ Executable (.exe) / 可执行文件

## Known Limitations / 已知限制

These are current, verified limitations — plan around them / 以下是实测确认的限制，写代码时请绕开：

- **String escaping / 字符串转义** only supports backslash (`\n`, `\t`, `\\`, `\"`). Do **not** use backtick escaping (`` `n ``) — it generates invalid assembly.
  只支持反斜杠转义，**不要**用反引号转义。
- **`str` is a reserved type keyword** — cannot be used as a variable name (`string str = ...` fails).
  `str` 是类型关键字，不能作变量名。
- **`print` does not insert spaces or newlines** between comma-separated arguments.
  `print` 逗号分隔的参数之间不自动加空格，也不自动换行。
- **Array literals** can only initialize a declaration (`int[] a = {1,2,3};`), not appear in assignment or `return`.
  数组字面量只能用于变量初始化，不能用于赋值或 `return`。
- **`for` does not accept parentheses**: write `for int i = 0; i < 5; i++`, not `for (int i = 0; ...)`. `if`/`switch` do accept parentheses.
  `for` 不支持括号；`if`/`switch` 支持。
- **`for` init must be a declaration** and accepts one variable: `for i = 0` (already declared) and `for int i = 0, j = 10` are rejected.
  `for` 初始化段必须是单变量声明。
- **`foreach` only works on arrays**, not on strings, and the element type cannot be an array (`for int[] row : matrix`).
  `foreach` 只支持数组，不支持字符串，元素类型不能是数组。
- **`print(...)` with parentheses** accepts only one argument.
  括号形式的 `print(...)` 只接受 1 个参数。
- **`-d` flag is reserved** and produces no debug output in the current version.
  `-d` 是预留参数，当前版本不输出任何调试信息。想看中间结果用 `-a` 生成 `output.asm`。
- **`int128` / `uint128` / `float128` cannot be printed** — declaring and assigning works, but `print` reports `fatal error[E5001]`. Use `int64` / `float64` instead.
  `int128` 等 128 位类型能声明赋值，但 `print` 会报 `E5001`。改用 `int64` / `float64`。
- **No tuples or destructuring** — multi-return values can only be read via `getReturn(i)`; there is no way to store the whole result in a variable.
  没有元组和解构赋值，多返回值只能用 `getReturn(i)` 即时取值。
- **Variadic arguments require brackets** — call `f([1,2,3])`, not `f(arr)`. Passing an array variable reports `E3009`.
  变参调用必须写中括号，传数组变量会报 `E3009`。

## Contributing / 贡献

Issues and pull requests are welcome!
欢迎提交 Issue 和 Pull Request！

- Report bugs: attach a reproducible `.yxg` source file / 报告 Bug：附上能复现的 .yxg 源文件
- Feature suggestions: describe syntax and expected behavior / 功能建议：描述语法和期望行为
- Code contributions: follow the existing code style (C23, snake_case for C, Chinese comments in C sources) / 代码贡献：遵循现有代码风格
- Before submitting a PR, run the regression cases under `repro/` / 提交 PR 前请跑一遍 `repro/` 下的回归用例

## License / 许可证

Apache-2.0 License — see the LICENSE file for details.
Apache-2.0 许可证 — 详见 LICENSE 文件。

## Acknowledgments / 致谢

- NASM assembler / NASM 汇编器
- MinGW-w64 toolchain / MinGW-w64 工具链
- Everyone who has provided feedback and suggestions / 所有提供反馈和建议的用户
