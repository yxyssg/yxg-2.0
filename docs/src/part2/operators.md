# 运算符

## 概述

YXG 的运算符按功能分为七类：

| 类别 | 运算符 |
|------|--------|
| 算术 | + - * / % |
| 比较 | == != > < >= <= |
| 逻辑 | && \|\| ! |
| 位运算 | & \| ~ ^ << >> |
| 赋值 | = += -= *= /= %= <<= >>= ^= |
| 自增自减 | ++ -- |
| 三目 | ? : |

## 算术运算符

    int main() {
        int a = 17;
        int b = 5;

        int sum = a + b;
        int diff = a - b;
        int prod = a * b;
        int quot = a / b;
        int mod = a % b;

        print sum, " ", diff, " ", prod, " ", quot, " ", mod, "\n";

        float x = 17.0;
        float y = 5.0;
        float fdiv = x / y;
        // float fmod = x % y;   // 错误：浮点不支持 % 运算符

        print fdiv, "\n";
        return 0;
    }

输出：

    22 12 85 3 2
    3.4

整数除法向零截断：7 / 3 = 2，-7 / 3 = -2。

取模结果的符号与被除数相同：-17 % 5 = -2，17 % -5 = 2。

% 运算符只适用于整数类型（int、uint、char）。浮点数取模需使用内置函数。

## 比较运算符

比较运算的结果是 bool 类型：

    int main() {
        int a = 10;
        int b = 20;

        bool eq = a == b;
        bool ne = a != b;
        bool lt = a < b;
        bool gt = a > b;
        bool le = a <= b;
        bool ge = a >= b;

        print eq, " ", ne, " ", lt, " ", gt, " ", le, " ", ge, "\n";

        string s1 = "apple";
        string s2 = "banana";
        bool strCmp = s1 == s2;
        bool strLess = s1 < s2;

        print strCmp, " ", strLess, "\n";
        return 0;
    }

输出：

    false true true false true false
    false true

字符串比较按字典序逐字符进行。== 和 != 比较字符串内容，而不是地址。

## 逻辑运算符

    int main() {
        bool a = true;
        bool b = false;

        bool and = a && b;
        bool or = a || b;
        bool not = !a;

        print and, " ", or, " ", not, "\n";

        // 短路求值
        int x = 0;
        bool result = x != 0 && 10 / x > 5;
        // 由于 x == 0，左侧为 false，右侧不会执行

        print result, "\n";
        return 0;
    }

输出：

    false true false
    false

&& 和 || 使用短路求值：
- 对于 A && B，如果 A 为 false，B 不会求值
- 对于 A || B，如果 A 为 true，B 不会求值

## 位运算符

位运算符将操作数视为二进制位序列：

    int main() {
        int a = 0x0F;
        int b = 0x3C;

        int and = a & b;
        int or = a | b;
        int xor = a ^ b;
        int not = ~a;

        int shl = a << 2;
        int shr = b >> 2;

        print and, " ", or, " ", xor, " ", not, " ", shl, " ", shr, "\n";
        return 0;
    }

输出：

    12 63 51 -16 60 15

注意事项：
- ~ 按位取反，结果与操作数类型相关
- << 左移，低位补 0
- >> 对有符号数使用算术右移（高位补符号位），对无符号数使用逻辑右移（高位补 0）
- 位运算符要求操作数为整数类型（int、uint、char、bool）

## 赋值运算符

基本赋值 = 已在前一章介绍。复合赋值将运算与赋值合并：

    int main() {
        int x = 10;

        x += 5;
        x -= 3;
        x *= 2;
        x /= 4;
        x %= 4;
        x <<= 2;
        x >>= 1;
        x ^= 0xFF;

        print x, "\n";
        return 0;
    }

输出：

    251

赋值不是表达式，不能像 C 那样链式赋值：

    int main() {
        int a;
        // int b = (a = 5) + 3;      // 错误！赋值不能作为表达式
        a = 5;
        int b = a + 3;               // 正确：a = 5, b = 8
        print b, "\n";
        return 0;
    }

## 自增自减运算符

    int main() {
        int x = 5;

        int a = ++x;
        int b = x++;
        int c = --x;
        int d = x--;

        print a, " ", b, " ", c, " ", d, " ", x, "\n";
        return 0;
    }

输出：

    6 7 6 5 5

注意后置 ++/-- 在 YXG 里和前置行为一致，都是先修改再取新值，所以 `b = x++` 得到的是自增后的 7。

自增自减只能用于变量（左值），不能用于字面量或表达式：

    int main() {
        int x = 5;
        // 5++;           // 错误
        // (x + 1)++;     // 错误
        x++;              // 正确
        return 0;
    }

## 三目运算符

三目运算符 ? : 是 YXG 中唯一的三个操作数运算符，用于根据条件选择两个值之一：

    条件 ? 为真时的值 : 为假时的值

### 基本用法

    int main() {
        int a = 10;
        int b = 20;

        int max = a > b ? a : b;
        print "max: ", max, "\n";            // max: 20

        int min = a < b ? a : b;
        print "min: ", min, "\n";            // min: 10

        return 0;
    }

输出：

    max: 20
    min: 10

### 嵌套三目

三目运算符是右结合的，可以嵌套使用：

    int main() {
        int a = 10;
        int b = 20;

        // 等价于 a > b ? 1 : (a == b ? 0 : -1)
        int result = a > b ? 1 : a == b ? 0 : -1;
        print result, "\n";                  // -1

        // 多分支选择
        int score = 85;
        string grade = score >= 90 ? "A" :
                       score >= 80 ? "B" :
                       score >= 70 ? "C" :
                       score >= 60 ? "D" : "F";
        print grade, "\n";                   // B

        return 0;
    }

输出：

    -1
    B

### 三目作为表达式的一部分

三目运算符可以嵌入到更大的表达式中：

    int main() {
        int a = 10;
        int b = 20;

        int c = (a < b ? a : b) + 5;
        print c, "\n";                       // 15

        // 在函数参数中使用
        print "The larger is ", a > b ? a : b, "\n";

        return 0;
    }

输出：

    15
    The larger is 20

### 不同分支的类型提升

当两个分支的类型不同时，结果类型会自动提升：

    int main() {
        int a = 10;
        int b = 5;

        // int 和 float → 结果为 float
        float f = a > b ? 3.14 : 2;
        print f, "\n";                       // 3.14

        // int 和 char → 结果为 int
        int c = a > b ? 65 : 'Z';
        print c, "\n";                       // 65

        // 两个分支都是字符串
        string s = a > b ? "hello" : "bye";
        print s, "\n";                       // hello

        return 0;
    }

输出：

    3.14
    65
    hello

float 用 %g 风格打印，3.14 不会变成 3.140000。

类型提升规则与二元运算一致。

### 三目与 if/else 对比

三目运算符是表达式，可以出现在任何需要值的地方；if/else 是语句，不能直接返回值：

    int main() {
        int a = 10;
        int b = 20;

        // 三目：简洁，适合简单条件
        int max = a > b ? a : b;

        // if/else：适合复杂逻辑
        int max2;
        if a > b {
            max2 = a;
        } else {
            max2 = b;
        }

        print max, " ", max2, "\n";
        return 0;
    }

输出：

    20 20

选择建议：
- 简单的二选一赋值，用三目
- 需要执行多条语句，用 if/else
- 嵌套超过两层，用 if/else if 更清晰

## 运算符优先级

从高到低排列：

| 优先级 | 运算符 | 结合性 |
|--------|--------|--------|
| 1（最高） | ++ --（后缀） | 左到右 |
| 2 | ++ --（前缀）! ~ -（一元负） | 右到左 |
| 3 | * / % | 左到右 |
| 4 | + - | 左到右 |
| 5 | << >> | 左到右 |
| 6 | < <= > >= | 左到右 |
| 7 | == != | 左到右 |
| 8 | & | 左到右 |
| 9 | ^ | 左到右 |
| 10 | \| | 左到右 |
| 11 | && | 左到右 |
| 12 | \|\| | 左到右 |
| 13 | ? : | 右到左 |
| 14（最低） | = += -= *= /= %= <<= >>= ^= | 右到左 |

如果不确定优先级，使用括号明确求值顺序：

    int main() {
        int a = 1;
        int b = 2;
        int c = 3;
        int d = 4;
        int result = (a + b) * (c - d);
        int unclear = a + b * c - d;
        print result, " ", unclear, "\n";
        return 0;
    }

输出：

    -3 3

## 运算符与类型

### 整数与浮点混合运算

    int main() {
        int i = 5;
        float f = 2.5;

        float result1 = i + f;
        float result2 = i * f;
        float result3 = i / f;

        float a = 5 / 2;
        float b = 5.0 / 2;
        float c = 5 / 2.0;

        print result1, " ", a, " ", b, " ", c, "\n";
        return 0;
    }

输出：

    7.5 2.0 2.5 2.5

注意 5 / 2 是整数除法，结果为 2。要得到浮点结果，至少一个操作数是浮点。

### 有符号与无符号混合

    int main() {
        int si = -10;
        uint ui = 20;

        uint result = si + ui;
        print result, "\n";
        return 0;
    }

输出：

    10

有符号和无符号混合运算时，有符号数被转换为无符号数。这可能导致意外结果，建议避免混合使用。

### 字符串拼接

    int main() {
        string s = "Hello";

        string s1 = s + " World";
        string s2 = s + "!";
        string s3 = "Value: ";
        string s4 = "Pi: ";

        print s1, " ", s2, " ", s3, " ", s4, "\n";
        return 0;
    }

输出：

    Hello World Hello! Value:  Pi:

+ 运算符用于字符串时表示拼接，但两边都必须是字符串。`string + int`、`string + float` 会报 `error[E3004]: incompatible types`，`string + char` 虽然能编译，运行时会崩溃。要输出数字，直接把数字作为独立的 print 参数：

    int main() {
        int n = 42;
        float pi = 3.14;
        // string bad = "Value: " + n;     // 错误！类型不匹配
        print "Value: ", n, "\n";          // 正确
        print "Pi: ", pi, "\n";
        return 0;
    }

输出：

    Value: 42
    Pi: 3.14

## 类型转换（as）

`as` 运算符做显式类型转换：

    pub void main() {
        int n = 42;
        float f = n as float;
        print "f=", f, "\n";          // 42.0

        float x = 3.9;
        int y = x as int;
        print "y=", y, "\n";          // 3（截断，不是四舍五入）

        int a = 65;
        char c = a as char;
        print "c=", c, "\n";          // A
    }

输出：

    f=42.0
    y=3
    c=A

### 常见转换

| 源类型 | 目标类型 | 结果 |
|---|---|---|
| `int` → `float` | `n as float` | `42.0` |
| `float` → `int` | `x as int` | 截断小数（3.9 → 3） |
| `int` → `char` | `a as char` | ASCII 码对应字符（65 → A） |
| `char` → `int` | `c as int` | 字符的 ASCII 码 |

### 指针相关转换

`as` 也用于指针类型转换：

    pub void main() {
        unsafe int v = 42;
        unsafe int* p = &v as int*;
        print p[0], "\n";             // 42
    }

### 注意

- `as int` 截断小数部分，不是四舍五入
- 超出目标类型范围的值会被截断，不会报错
- 字符串和数字之间不能用 `as` 转换

## sizeof：取类型大小

`sizeof(类型)` 在编译期算出一个常量（字节数），结果类型是 `int`。

```yxg
pub void main() {
    print sizeof(int), "\n";        // 4
    print sizeof(char), "\n";       // 1
    print sizeof(bool), "\n";       // 1
    print sizeof(uint), "\n";       // 4
    print sizeof(float), "\n";      // 4
    print sizeof(int64), "\n";      // 8
    print sizeof(str), "\n";        // 8   指针 + 长度
    print sizeof(string), "\n";     // 4   指针
}
```

| 写法 | 结果 |
|---|---|
| `sizeof(int)` / `sizeof(uint)` / `sizeof(float)` | 4 |
| `sizeof(char)` / `sizeof(bool)` / `sizeof(int8)` | 1 |
| `sizeof(int16)` | 2 |
| `sizeof(int64)` / `sizeof(float64)` | 8 |
| `sizeof(string)` | 4（存的是指针） |
| `sizeof(str)` | 8（两字长：指针 + 长度） |
| `sizeof(类名)` | 4（对象变量存的是指针） |

```yxg
pub class Point { pub int x; pub int y; }

print sizeof(Point), "\n";          // 4 —— 类变量是指针，不是对象体
```

**注意几点：**

- `sizeof` 只看**类型名**，不能传变量、表达式或数组：`sizeof(Point)` 可以，`sizeof(p)` 不行。
- 类名给的是**指针大小**（4），不是对象体的字节数。想知道对象体多大，得自己按字段累加。
- 数组没有 `sizeof`：数组长度用 `arr.length`（元素个数），字节数要 `arr.length * sizeof(元素类型)`。

## 常见错误

### 除零错误

    int main() {
        int x = 10;
        // int z = x / 0;    // 编译报错 error[E3005] division by zero
        int y = 0;
        int w = x / y;       // 编译通过，运行时会崩溃
        return 0;
    }

如果除数是字面量 0，编译器会直接报 `error[E3005]: division by zero`。如果是变量且运行时值为 0，程序会异常终止。

### 赋值与比较混淆

    int main() {
        int x = 5;
        // if x = 10 {            // 错误！条件里不能赋值
        //     print "x is 10";
        // }
        if x == 10 {
            print "x is 10", "\n";
        }
        return 0;
    }

= 是赋值，== 是比较。在条件中写 = 会直接编译报错（`error[E2002]: expected 'BracketLeft' before 'Assignment'`）。

### 位运算符优先级

    int main() {
        int x = 5;
        bool result = (x & 1) == 1;    // 正确写法：显式加括号
        print result, "\n";            // true
        return 0;
    }

== 的优先级高于 &，`x & 1 == 1` 实际会被解析成 `x & (1 == 1)`。建议始终加括号。

## 继续阅读

掌握了运算符后，下一章将学习 YXG 的控制流语句。
