# 基本类型

## 概述

YXG 是一门静态类型语言，每个变量在编译时就必须确定类型。共有 7 种基本类型：

| 类型 | 关键字 | 占用字节 | 取值范围 | 默认值 |
|------|--------|----------|----------|--------|
| 有符号整数 | int | 4 | -2147483648 ~ 2147483647 | 0 |
| 无符号整数 | uint | 4 | 0 ~ 4294967295 | 0 |
| 浮点数 | float | 4 | ±1.18e-38 ~ ±3.4e+38 | 0.0 |
| 无符号浮点 | ufloat | 4 | 0 ~ 3.4e+38 | 0.0 |
| 字符 | char | 1 | 0 ~ 255 | '\0' |
| 布尔 | bool | 1 | true / false | false |
| 字符串 | string | 4（指针） | 任意长度 | null |

## int：有符号整数

最常用的整数类型，32 位有符号：

    int main() {
        int a = 42;
        int b = -100;
        int c = 0;

        int hex = 0xFF;
        int oct = 077;
        int bin = 0b1010;

        int sum = a + b;
        int diff = a - b;
        int prod = a * b;
        int quot = a / b;
        int mod = a % b;

        print sum, " ", diff, " ", prod, " ", quot, " ", mod, "\n";
        print hex, " ", oct, " ", bin, "\n";
        return 0;
    }

输出：

    -58 142 -4200 0 42
    255 77 10

整数除法向零截断。例如 7 / 3 = 2，-7 / 3 = -2。取模结果的符号与被除数相同。YXG 不检查整数溢出，行为与底层 CPU 一致。

## uint：无符号整数

32 位无符号，不能存储负数：

    int main() {
        uint a = 100;
        uint b = 200;
        uint max = 4294967295;

        uint sum = a + b;

        // uint x = -1;          // 编译错误！

        if a < b {
            print "a is less than b", "\n";
        }

        print sum, "\n";

        return 0;
    }

输出：

    a is less than b
    300

uint 和 int 混合运算时，结果提升为 uint。比较时使用无符号比较指令。

## float：浮点数

32 位 IEEE 754 单精度浮点：

    int main() {
        float pi = 3.14159;
        float e = 2.71828;
        float small = 0.0015;
        float big = 602000000000000000000000.0;

        float sum = pi + e;
        float area = pi * 5.0 * 5.0;

        float half = 1 / 2.0;
        float mixed = 3 + 0.14;

        print pi, " ", area, " ", half, " ", mixed, "\n";
        return 0;
    }

输出：

    3.14159 78.539749 0.5 3.14

浮点数字面量必须包含小数点。YXG 不支持科学计数法写法（如 `1.5e-3`），这样写会报 `error[E3001]`。整数与浮点混合运算时，整数自动提升为浮点。单精度约 7 位有效数字，存在舍入误差。

float 使用 `%g` 风格打印：`print 92.5` 输出 `92.5`，`print 3.14159` 输出 `3.14159`，不会补成 6 位小数。

更复杂的数学运算可使用 Math 标准库。Math 的所有方法都是 static，直接用 `类名.方法()` 调用，不需要 `new`：

    import yxg.Math;

    int main() {
        float x = Math.sqrt(16.0);
        float y = Math.sin(1.5708);
        float z = Math.fabs(-3.14);

        print x, " ", y, " ", z, "\n";
        return 0;
    }

输出：

    4.0 1.0 3.14

写成 `Math m = new Math(); m.sqrt(16.0);` 会报 `error[E3015]: no matching method 'sqrt'`。

Math 库还提供了 cos、tan、pow、min、max、clamp 以及随机数等功能。

## ufloat：无符号浮点

与 float 相同的内存布局，但语义上不允许负值：

    int main() {
        ufloat price = 19.99;
        ufloat distance = 100.5;

        // ufloat x = -1.5;      // 编译错误！

        float discount = 0.8;
        ufloat final_price = price * discount;

        print final_price, "\n";
        return 0;
    }

输出：

    15.992

ufloat 与 float 混合运算时，结果提升为 ufloat。编译器在语义分析阶段检查负值赋值。

## char：字符

单字节，存储 0 到 255 之间的值：

    int main() {
        char ch = 'A';
        char newline = '\n';
        char tab = '\t';
        char quote = '\"';
        char backslash = '\\';

        char next = ch + 1;
        int code = ch;

        if ch >= 'A' && ch <= 'Z' {
            print ch, " is uppercase", "\n";
        }

        print ch, " ", next, " ", code, "\n";
        return 0;
    }

输出：

    A is uppercase
    A B 65

注意：不要直接打印 `char + char` 这种表达式的结果，例如 `print a + b`（a、b 均为 char）会让程序异常退出。需要整数值时先把结果存进 int，或用 `(a - 0) + (b - 0)` 这类写法拆开。

转义序列：

| 序列 | 含义 | ASCII |
|------|------|-------|
| \n | 换行 | 10 |
| \t | 制表符 | 9 |
| \\ | 反斜杠 | 92 |
| \' | 单引号 | 39 |
| \" | 双引号 | 34 |
| \0 | 空字符 | 0 |

char 和 int 可以互相赋值和运算。char 赋值给 int 时零扩展。

## bool：布尔值

只有两个值：true 和 false：

    int main() {
        bool flag = true;
        bool done = false;

        bool result = 10 > 5;
        bool equal = 3 == 4;

        bool and = true && false;
        bool or = true || false;
        bool not = !true;

        if flag {
            print "flag is true", "\n";
        }

        int asInt = true;
        int asInt2 = false;

        print result, " ", and, " ", or, " ", not, " ", asInt, "\n";
        return 0;
    }

输出：

    flag is true
    true false true false 1

bool 打印出来是 true/false 文本，转成 int 后是 1/0。

bool 在内存中占 1 字节。true 存储为 1，false 存储为 0。bool 可以隐式转换为 int，但 int 不能隐式转换为 bool（条件表达式除外）。

## string：字符串

可变长度的字符序列。详细用法见字符串章节，这里展示基本操作：

    import yxg.String;

    int main() {
        string s1 = "Hello";
        string s2 = "World";

        string s3 = s1 + " " + s2;

        int len = String.len(s1);

        int c = String.charAt(s1, 0);

        if s1 == s2 {
            print "equal", "\n";
        } else {
            print "not equal", "\n";
        }

        print s3, " ", len, " ", c, "\n";
        return 0;
    }

输出：

    not equal
    Hello World 5 72

String 的成员方法全是 static，必须用 `String.方法名(参数)` 调用。另外 `str` 是保留字，不能拿来当变量名——`string str = ...;` 会报 `error[E2002]: expected 'Semicolon' before 'Str'`，换个名字（`s`、`text`）即可。

字符串在底层以 null 结尾，存储了长度信息。字符串是不可变的，每次拼接创建新对象，旧对象由 GC 回收。

## 窄整型与扩展类型

除了前面 7 种常用类型，YXG 还支持一组不同宽度的数值类型。

### 完整类型表

| 类别 | 类型名 | 状态 |
|---|---|---|
| 有符号整数 | `int8` `int16` `int32` `int64` | ✅ 可用 |
| 无符号整数 | `uint8` `uint16` `uint32` `uint64` | ✅ 可用 |
| 浮点 | `float8` `float16` `float32` `float64` | ✅ 可用 |
| 无符号浮点 | `ufloat8` `ufloat16` `ufloat32` `ufloat64` | ✅ 可用 |
| 128 位 | `int128` `uint128` `float128` `ufloat128` | ⚠️ 能声明，**不能打印** |

等价关系：

| 别名 | 实际类型 |
|---|---|
| `int32` | `int` |
| `uint32` | `uint` |
| `float32` | `float` |
| `ufloat32` | `ufloat` |

### 用法示例

```yxg
pub void main() {
    int8   a = 12;
    int16  b = 1234;
    uint8  c = 200;
    uint16 d = 60000;
    float32 e = 3.14;
    float64 f = 2.718281828;

    print "int8=", a, " int16=", b, "\n";
    print "uint8=", c, " uint16=", d, "\n";
    print "float32=", e, " float64=", f, "\n";
}
```

输出：

```
int8=12 int16=1234
uint8=200 uint16=60000
float32=3.14 float64=2.718282
```

### 转成 int64

窄类型转 `int64` 会正确做符号扩展：

```yxg
pub void main() {
    int16 a = 1000;
    int64 b = a as int64;
    print "扩展: ", b, "\n";       // 1000

    int16 n = -5;
    int64 c = n as int64;
    print "负数扩展: ", c, "\n";   // -5
}
```

### 运算时的溢出

窄类型参与运算时按声明宽度回绕：

```yxg
pub void main() {
    int16 a = 32767;
    a = a + 1;
    print a, "\n";        // -32768，回绕

    uint8 b = 255;
    b = b + 1;
    print b, "\n";        // 0，回绕
}
```

### 赋值时的行为

赋值**不做截断**，超出范围的值原样保存：

```yxg
pub void main() {
    int16 b = 40000;
    print b, "\n";        // 40000，超出 16 位范围也照存
}
```

这是当前实现的特性：写入用 16 位指令，但读回时不做高位清理。**运算会回绕、赋值不回绕**，两者不一致。

需要严格的宽度语义时，赋值后手动转换：

```yxg
int16 b = 40000 as int16;    // 明确要 16 位语义
```

### 128 位类型不能打印

```yxg
pub void main() {
    int128 a = 1;
    print a, "\n";
}
```

报错：

```
fatal error[E5001]: printing int128/uint128 is not supported yet (use int64)
```

`float128` / `ufloat128` 同理。能声明、能赋值、能参与运算，但**不能打印**。需要时用 `int64` / `float64` 代替。

### 选择建议

| 场景 | 建议 |
|---|---|
| 普通整数 | `int` |
| 需要更大范围 | `int64` |
| 普通浮点 | `float`（等价 `float32`） |
| 高精度浮点 | `float64` |
| 省内存的窄整型 | 可用，但注意赋值的宽度不一致 |
| 128 位 | 不要用于需要打印的场景 |

**日常编码记住这几个就够**：`int`、`int64`、`float`、`float64`、`uint`、`char`、`bool`、`string`。

## 类型转换

### 隐式转换

    int main() {
        float f = 42;
        int i = 'A';
        int b = true;
        uint u = 100;

        print f, " ", i, " ", b, " ", u, "\n";
        return 0;
    }

输出：

    42.0 65 1 100

注意 `string + int` 和 `string + float` 都会报 `error[E3004]: incompatible types`。需要拼接数字时，把数字作为独立的 print 参数传进去：

    int main() {
        int v = 42;
        // string s = "value:" + v;      // 错误！string 和 int 类型不匹配
        print "value: ", v, "\n";        // 正确

        return 0;
    }

输出：

    value: 42

### 显式转换（as）

用 `as` 把一个值转成指定类型：

```yxg
pub void main() {
    int a = 10;
    float f = a as float;
    print "int->float: ", f, "\n";        // 10.0

    float g = 3.99;
    int b = g as int;
    print "float->int: ", b, "\n";        // 3，截断不是四舍五入

    int c = 65;
    char ch = c as char;
    print "int->char: ", ch, "\n";        // A

    char d = 'Z';
    int e = d as int;
    print "char->int: ", e, "\n";         // 90
}
```

小数转整数是**截断**，不是四舍五入：

```yxg
print 3.99 as int, "\n";     // 3
print (-3.99) as int, "\n";  // -3
print 3.5 as int, "\n";      // 3
```

### 窄类型转换

转窄类型时按目标宽度截断：

```yxg
pub void main() {
    int big = 300;
    int8 small = big as int8;
    print "300 as int8 = ", small, "\n";     // 44（300 的低 8 位）

    int16 mid = 100000 as int16;
    print "100000 as int16 = ", mid, "\n";   // 34464
}
```

窄转宽会正确做符号扩展：

```yxg
pub void main() {
    int16 a = 1000;
    print a as int64, "\n";      // 1000

    int16 n = -5;
    print n as int64, "\n";      // -5，负数正确
}
```

### 指针与整数互转

```yxg
pub void main() {
    int a = 11;
    unsafe int* p = &a;

    int addr = p as int;
    print "地址 = ", addr, "\n";

    unsafe int* q = addr as int*;
    print "解回 = ", *q, "\n";     // 11
}
```

详见[指针](pointers.md)。

### 字符串转数字

语言没有内置的字符串转数字转换。用 `Utils` 库：

```yxg
import yxg.Utils;

pub void main() {
    print Convert.parseInt("123"), "\n";       // 123
    print Convert.parseFloat("3.5"), "\n";     // 3.5
}
```

或者手写扫描（见[标准库](stdlib.md)）。

### 类型提升规则

二元运算时的类型提升：

| 左操作数 | 右操作数 | 结果类型 |
|----------|----------|----------|
| int | int | int |
| int | float | float |
| int | uint | uint |
| float | int | float |
| float | float | float |
| uint | uint | uint |
| uint | float | ufloat |
| char | char | int |
| char | int | int |
| string | string | string |

注意 `string + int`、`string + float` 会报 `error[E3004]: incompatible types`。`string + char` 虽然能编译通过，但运行时会让程序崩溃，实际使用时请先用 print 分隔参数代替。

比较运算的结果始终是 bool。

## 类型默认值

未初始化的变量具有以下默认值：

    int main() {
        int i;
        uint u;
        float f;
        ufloat uf;
        char c;
        bool b;
        string s;

        print i, " ", f, " ", b, "\n";
        return 0;
    }

输出：

    0 0.0 false

建议始终显式初始化变量。特别注意未初始化的 string 是 null，访问其属性会出错。

## 类型相关错误

### 类型不匹配

    // int x = "hello";     // 错误！error[E3003]

### 无符号类型赋负值

    // uint x = -5;         // 错误！error[E3017]

### 条件表达式类型错误

    int main() {
        int x = 10;
        // if x { ... }         // 错误！error[E3020] condition must be boolean, got 'int'
        if x != 0 {
            print "x is not zero", "\n";    // 正确
        }
        return 0;
    }

## 继续阅读

掌握了基本类型后，下一章将学习变量的声明、赋值和作用域。
