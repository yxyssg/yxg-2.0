# 内置函数参考

## 概述

YXG 编译器预注册了一系列运行时函数，**无需 import 即可直接调用**。它们提供基本的 I/O、字符串操作、系统调用等功能。

## 输入输出

### print

向控制台输出内容，自动识别类型并格式化。`print` 是语句，不需要括号。**逗号分隔的参数之间不会自动加空格，也不会自动换行**：

    int main() {
        print "Hello, World!";
        print 42;
        print 3.14159;
        print true;
        print 'A';

        // 多个参数：不加空格，要空格自己写
        print "The answer is ", 42;

        // 打印数组
        int[] nums = {1, 2, 3};
        print nums;

        return 0;
    }

输出：

    Hello, World!
    42
    3.141590
    true
    A
    The answer is 42
    [1, 2, 3]

### input

从标准输入读取一行，按空格/制表符切分，依次填入变量。

**语法**：

    input(变量1, 变量2, ...);

括号内是**变量名**，不是表达式，也不需要 `&` 取地址。

**基本用法**：

    pub void main() {
        int age;
        string name;
        input(age, name);
        print "age=", age, " name=", name, "\n";
    }

输入：

    42 alice

输出：

    age=42 name=alice

**类型转换规则**

`input` 按每个变量**声明时的类型**解析，不是根据输入内容推断。解析失败时取默认值：

| 变量类型 | 期望输入 | 解析失败时的值 |
|---|---|---|
| `int` / `uint` / `int64` / `uint64` | 十进制整数 | `0` |
| `float` / `ufloat` | 浮点数（`3.14`、`-2.5`） | `0.0` |
| `char` | 单个字符（取首字符） | `\0` |
| `string` | 整个 token | 空串 |
| `bool` | `true` / `false` / `0` / `1` | `false` |

token 分隔符是空格和制表符，**换行结束本次输入**。

**多行输入**

每次 `input(...)` 读一行。要读多行就调用多次：

    pub void main() {
        int n;
        input(n);
        int i = 0;
        while i < n {
            int x;
            input(x);
            print "x=", x, "\n";
            i = i + 1;
        }
    }

输入：

    3
    10
    20
    30

输出：

    x=10
    x=20
    x=30

> **注意**：`input` 依赖 Windows 控制台 API（`ReadConsoleW`），必须在真实控制台窗口运行。用管道或重定向喂入数据不会被读取，变量会保持原值。这在自动化测试里容易踩坑。

## 字符串格式化

### format

格式化字符串并返回 `string`。模板里的 `{}` 按顺序被后面的参数替换。

**语法**：

    format("模板 {} {} ...", 值1, 值2, ...)

`input` 是「从输入解析到变量」，`format` 正好相反——「从变量拼字符串」。

**基本用法**：

    pub void main() {
        int a = 42;
        string b = "hello";
        float c = 3.14;
        string s = format("a={}, b={}, c={}", a, b, c);
        print s, "\n";
    }

输出：

    a=42, b=hello, c=3.140000

**类型映射**

`format` 内部按运行时类型码处理：

| YXG 类型 | 运行时类型码 | 输出形式 |
|---|---|---|
| `int` / `int8` / `int16` / `int32` / `int64` / `int128` | 0 | 十进制 |
| `uint` / `uint64` 等 | 5 | 十进制无符号 |
| `float` / `float64` 等 | 1 | `%f` 风格（如 `3.140000`） |
| `ufloat` 系列 | 6 | 无符号浮点 |
| `char` | 2 | 单字符 |
| `string` / `str` | 3 | 字符串 |
| `bool` | 4 | `true` / `false` |

实测各类型的输出：

    pub void main() {
        print "[", format("{}", 3.14), "]\n";      // [3.140000]
        print "[", format("{}", true), "]\n";      // [true]
        print "[", format("{}", false), "]\n";     // [false]
        print "[", format("{}", 'A'), "]\n";       // [A]
        print "[", format("{}", 42), "]\n";        // [42]
        print "[", format("{}", "str"), "]\n";     // [str]
        print "[", format("{} {}", 1, 2), "]\n";   // [1 2]
    }

**占位符数量与参数数量**

占位符按顺序消耗参数。模板里的 `{}` 多于实际参数时会读越界，少于实际参数时多余参数被忽略。**请保证两者数量一致。**

模板里没有 `{}` 时，参数不参与拼接：

    print format("no placeholder", 5);    // no placeholder

**花括号转义**

连续两个花括号表示一个字面花括号：

    print format("{{}}", 0);     // {}

**关于格式说明符**

早期文档里写过 `{:x}`、`{:.2f}`、`{:04d}` 这类「`{}` 内加格式说明符」的写法。**这不是 `format` 的受支持语法**，`{}` 之间不应放任何字符。需要定宽或进制转换时，请用 `String` 库或自己写转换函数。

> 当前实现里 `{:x}` / `{:X}` / `{:o}` / `{:b}` / `{:d}` / `{:.2f}` / `{:04d}` 恰好能被识别，但这属于实现细节，不保证跨版本稳定，不要依赖。

## 字符串操作函数

这些是底层函数，**推荐使用 `String` 标准库的封装**（`String.len(s)` 等），语义更清楚。

### _str_length

返回字符串长度：

    int main() {
        string s = "Hello";
        print _str_length(s);        // 5
        return 0;
    }

### _str_char_at

返回指定位置的字符的 ASCII 码：

    int main() {
        string s = "Hello";
        print _str_char_at(s, 0);    // 72（'H'）
        return 0;
    }

### _str_substring

提取子串，参数为「原字符串、起始索引、长度」：

    int main() {
        string s = "Hello, World!";
        print _str_substring(s, 0, 5);   // Hello
        return 0;
    }

### 字符串拼接：用 `+` 运算符

**`_str_concat` 不是「返回新字符串」的函数**，它是 C 风格的原地 `strcat(dest, src)`：把 `src` 写到 `dest` 末尾并返回 `dest`。

所以下面这样用会崩溃（`dest` 是只读字符串字面量，写入触发访问违例）：

    // 错误：会崩溃
    string x = _str_concat("Hello, ", "World!");

拼接字符串请直接用 `+` 运算符：

    int main() {
        string p = "Hello, ";
        string q = "World!";
        print p + q, "\n";        // Hello, World!
        print "x" + "y", "\n";    // xy
        return 0;
    }

### _char_to_str

字符转字符串：

    int main() {
        print _char_to_str('A');      // A
        return 0;
    }

### _str_plus_char

字符串 + 字符：

    int main() {
        print _str_plus_char("Hello", '!');   // Hello!
        return 0;
    }

### _char_plus_str

字符 + 字符串：

    int main() {
        print _char_plus_str('A', "BC");      // ABC
        return 0;
    }

### 字符串比较

`_str_equal` / `_str_not_equal` **在当前版本中不存在**。字符串比较直接用 `==` / `!=` 运算符：

    int main() {
        string a = "abc";
        string b = "abc";
        if a == b {
            print "equal\n";
        }
        return 0;
    }

## 数组操作

**没有 `_array_to_string` 这个内置函数。** `print` 自己就会把数组格式化成 `[1, 2, 3]`：

    int main() {
        int[] nums = {1, 2, 3, 4, 5};
        print nums;              // [1, 2, 3, 4, 5]
        return 0;
    }

需要字符串形式时，可以先 print 或者用 `String` 库手动拼接。

## 系统函数

### _system_exit

退出程序并返回退出码：

    int main() {
        print "即将退出";
        _system_exit(0);
        print "这行不会执行";
        return 0;
    }

### _system_sleep

暂停指定毫秒数：

    int main() {
        print "等待中...";
        _system_sleep(1000);     // 暂停 1 秒
        print "完成";
        return 0;
    }

### _system_time

返回当前 Unix 时间戳（秒）：

    int main() {
        print "当前时间戳:", _system_time();
        return 0;
    }

### _system_getenv

读取环境变量：

    int main() {
        print "PATH:", _system_getenv("PATH");
        return 0;
    }

### _system_call

执行系统命令并返回退出码：

    int main() {
        print "退出码:", _system_call("dir");
        return 0;
    }

## 命令行参数

### 推荐写法：main 的参数

最直接的方式是让 `main` 接收 `string[] args`：

    pub void main(string[] args) {
        print "参数个数: ", args.length, "\n";
        int i = 0;
        while i < args.length {
            print i, ": ", args[i], "\n";
            i = i + 1;
        }
    }

`args[0]` 是程序自身路径，`args.length` 包含程序名。

### _GetCommandLineArgc

返回命令行参数个数（含程序名）：

    int main() {
        print "参数个数:", _GetCommandLineArgc();
        return 0;
    }

### _GetCommandLineArgv

**无参数**，返回整个 argv 指针数组：

    int main() {
        int argv = _GetCommandLineArgv();   // 返回指针
        return 0;
    }

> 注意：它不接受索引参数。要按索引取单个参数请用 `_sys_argv_at(i)` 或者直接用 `main(string[] args)`。

### _sys_argv_at

按索引取命令行参数：

    int main() {
        int i = 0;
        while i < _GetCommandLineArgc() {
            print "argv[", i, "] = ", _sys_argv_at(i), "\n";
            i = i + 1;
        }
        return 0;
    }

## 内存管理

### gc

手动触发一次垃圾回收：

    int main() {
        print "执行垃圾回收";
        gc();
        print "完成";
        return 0;
    }

### _gc_status

输出 GC 状态信息（调试用）：

    int main() {
        _gc_status();
        return 0;
    }

## 浮点运算辅助函数

以下函数用于浮点比较和取模，通常由编译器在需要时自动生成调用，用户一般不需要直接写：

| 函数 | 说明 |
|---|---|
| `_fgt(a, b)` | 浮点大于比较 |
| `_fge(a, b)` | 浮点大于等于比较 |
| `_feq(a, b)` | 浮点等于比较 |
| `_fne(a, b)` | 浮点不等于比较 |
| `_fle(a, b)` | 浮点小于等于比较 |
| `_flt(a, b)` | 浮点小于比较 |
| `_fmod_fpu(a, b)` | 浮点取模 |

## 内置函数与标准库对照

标准库对常用功能做了静态方法封装，推荐优先使用：

| 内置函数 | 标准库写法 |
|---|---|
| `_str_length(s)` | `String.len(s)` |
| `_str_char_at(s, i)` | `String.charAt(s, i)` |
| `_str_substring(s, start, len)` | `String.sub(s, start, len)` |
| `_str_concat(a, b)` | `a + b`（运算符） |
| `_system_exit(code)` | `System.exit(code)` |
| `_system_time()` | `System.time()` |
| `_system_sleep(ms)` | `System.sleep(ms)` |
| `_GetCommandLineArgc()` | `System.argc()` |
| `_sys_argv_at(i)` | `System.argv(i)` |

标准库需要先 `import`：

    import yxg.String;
    import yxg.System;

    int main() {
        print String.len("hello"), "\n";
        print System.time(), "\n";
        return 0;
    }

注意这些都是 **`static` 方法**，用 `类名.方法()` 调用，不要通过实例调用。

## 继续阅读

至此，YXG 语言参考部分结束。下一部分将深入编译器内部，从整体架构开始讲解。
