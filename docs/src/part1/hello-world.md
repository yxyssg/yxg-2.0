# 第一个程序：Hello World

## 最小的 YXG 程序

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

输出：

```
Hello, YXG!
```

## 逐行解析

### 第 1 行：pub void main()

```
pub void main() {
```

- `pub` 是访问修饰符，表示这个函数对外可见
- `void` 是返回值类型，表示不返回值
- `main` 是函数名，程序的入口点。每个可执行程序必须有且仅有一个 `main`
- `()` 是参数列表，这里为空
- `{` 表示函数体开始

### 第 2 行：print

```
    print "Hello, YXG!\n";
```

- `print` 是内置语句，向控制台输出
- `"Hello, YXG!\n"` 是字符串字面量，`\n` 是换行
- `;` 表示语句结束

### 第 3 行：}

函数体结束。

## 关于 main 的两种写法

YXG 支持两种 `main` 签名，都能作为程序入口：

**写法一：void，不接收参数**

```
pub void main() {
    print "hello\n";
}
```

**写法二：接收命令行参数**

```
pub void main(string[] args) {
    print "参数个数: ", args.length, "\n";
    int i = 0;
    while i < args.length {
        print i, ": ", args[i], "\n";
        i = i + 1;
    }
}
```

编译运行：

```
yxg build hello_args.yxg -o hello_args.exe
hello_args.exe foo bar
```

输出：

```
参数个数: 3
0: D:\path\to\hello_args.exe
1: foo
2: bar
```

- `args` 类型是 `string[]`
- `args[0]` 始终是程序自身路径
- `args.length` 返回参数个数（含程序名）

**写法三：兼容 C 风格的 int main()**

```
int main() {
    print "hello\n";
    return 0;
}
```

`return 0` 表示正常退出。这种写法同样有效，适合从 C/C++ 转过来的读者。

## print 的多种用法

`print` 接受任意数量、任意类型的参数，用逗号分隔。**逗号之间不会自动加空格**，要空格请自己写进字符串里：

```
pub void main() {
    print 42, "\n";
    print 3.14159, "\n";
    print true, "\n";
    print false, "\n";
    print 'A', "\n";
    print "The answer is ", 42, "\n";

    int[] nums = {1, 2, 3, 4, 5};
    print nums, "\n";
}
```

输出：

```
42
3.14159
true
false
A
The answer is 42
[1, 2, 3, 4, 5]
```

> 注意 `print` **不会自动换行**，需要自己写 `\n`。

## 字符串转义

**只有反斜杠转义有效**：

| 写法 | 含义 |
|---|---|
| `\n` | 换行 |
| `\t` | 制表符 |
| `\\` | 反斜杠 |
| `\"` | 双引号 |

> **常见坑**：不要用反引号 `` ` `` 做转义（那是 Windows 命令行习惯）。`print "a`nb"` 会生成非法汇编，报 `expression syntax error`。**必须用 `\n`。**

## 注释

```
// 单行注释，从 // 到行尾

/*
   多行注释
   可以跨行
*/
```

## 变量与基本运算

```
pub void main() {
    int a = 10;
    int b = 20;
    int sum = a + b;
    print "a =", a, "\n";
    print "b =", b, "\n";
    print "sum =", sum, "\n";

    float pi = 3.14159;
    float radius = 5.0;
    float area = pi * radius * radius;
    print "area =", area, "\n";

    string greeting = "Hello";
    string name = "YXG";
    string message = greeting + ", " + name + "!";
    print message, "\n";

    bool isGreater = a > b;
    print "a > b ?", isGreater, "\n";
}
```

输出：

```
a = 10
b = 20
sum = 30
area = 78.539749
Hello, YXG!
a > b ? false
```

## 程序结构规范

一个典型的 YXG 程序：

```
// 1. import 语句（可选，必须放在最前面）
import yxg.Math;

// 2. 枚举定义（可选）
enum Status { Active, Inactive }

// 3. 类定义（可选）
pub class MyClass {
    int value;
    pub void show() { print value, "\n"; }
}

// 4. 函数定义（可选）
int helper(int x) {
    return x * 2;
}

// 5. main 函数（必须）
pub void main() {
    print helper(21), "\n";
}
```

只有 `main` 是必须的。

## 常见错误

### 忘记分号

```
pub void main() {
    print "Hello"       // 错误：缺分号
    print "World";
}
```

报错：

```
error[E2002]: expected 'Semicolon' before 'Print'
```

### 忘记写 main

```
pub void foo() {
    print "hi";
}
```

报错：

```
error[E3011]: no main function found
```

### main 名字大小写错

```
int Main() {             // 错误：大小写敏感
    print "Hello";
    return 0;
}
```

报错：

```
error[E3011]: no main function found
```

### int main 缺少 return

```
int main() {
    print "Hello";       // 错误：int 返回值必须 return
}
```

报错：

```
error[E3019]: not all control paths return a value in function 'main'
```

### 用 str 当变量名

```
pub void main() {
    string str = "x";     // 错误：str 是类型关键字
}
```

报错：

```
error[E2002]: expected 'Semicolon' before 'Str'
```

`str` 和 `string` 都是类型关键字，不能用作变量名。换个名字即可。

### 数组字面量作返回值

```
int[] make() {
    return {1, 2, 3};    // 错误
}
```

报错：

```
error[E2004]: illegal operand '{'
```

数组字面量只能用于**变量初始化**（`int[] a = {1,2,3};`），不能直接作为 `return` 的值。

要返回数组，必须返回**堆上分配的动态数组**（`new T[n]`）。不能返回栈上的静态数组字面量——函数返回后栈帧销毁，返回的指针会悬空，读出来是垃圾值：

```
int[] make() {
    int[] tmp = new int[3];    // 堆分配，正确
    tmp[0] = 1;
    tmp[1] = 2;
    tmp[2] = 3;
    return tmp;
}

pub void main() {
    int[] a = make();
    print "len=", a.length, "\n";        // 3
    print a[0], " ", a[1], " ", a[2], "\n";   // 1 2 3
}
```

### 用反引号做转义

```
pub void main() {
    print "a`nb";         // 错误
}
```

汇编阶段报错：

```
output.asm: error: expression syntax error
```

改用 `\n`。

## 继续阅读

掌握了基本结构后，下一章详细介绍命令行参数和编译选项。
