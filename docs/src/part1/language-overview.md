# YXG 语言简介

## 设计初衷

YXG 是一门为教学而生的编译型语言。它有四个设计目标：

1. **足够简单** —— 能在有限时间内手写出完整编译器
2. **足够丰富** —— 涵盖静态类型、面向对象、泛型、异常、模块系统
3. **可调试** —— 生成的汇编清晰可读，方便理解底层原理
4. **自包含** —— 除 C 标准库和系统 API 外不依赖第三方框架

语法受 C 影响，吸收 Java/C# 的面向对象特性。编译器完全用 C 手写，不用 Yacc/Lex/LLVM。

## 一分钟速览

下面是一个完整可运行的程序，涵盖枚举、类、函数、数组、循环：

```yxg
import yxg.Math;

pub class Animal {
    pub string name;
    pub int age;

    pub void bark() {
        print this.name, " is making sound\n";
    }
}

pub void bubbleSort(int[] arr) {
    int n = arr.length;
    for int i = 0; i < n; i++ {
        for int j = 0; j < n - i - 1; j++ {
            if arr[j] > arr[j + 1] {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

pub void main() {
    int[] nums = {5, 2, 8, 1, 9};
    bubbleSort(nums);

    print "sorted:";
    for int i = 0; i < nums.length; i++ {
        print " ", nums[i];
    }
    print "\n";

    Animal dog = new Animal();
    dog.name = "Buddy";
    dog.age = 3;
    dog.bark();

    print "sqrt(16) = ", Math.sqrt(16.0), "\n";
}
```

输出：

```
sorted: 1 2 5 8 9
Buddy is making sound
sqrt(16) = 4.0
```

> **几个关键点**：
> - `print` **不自动加空格，也不自动换行**，要自己写 `" "` 和 `"\n"`
> - 对象**必须 `new`**，`Animal dog;` 声明出来是 null
> - 标准库方法是 `static`，用 `Math.sqrt()` 而不是 `new Math().sqrt()`
> - 数组是**值语义**，传给函数时复制，函数内的修改不影响调用方（`bubbleSort` 因此改成原地排序）

## 核心特性一览

### 基本类型

| 类型 | 说明 | 示例 |
|---|---|---|
| `int` | 32 位有符号整数 | `int x = 42;` |
| `uint` | 32 位无符号整数 | `uint u = 100;` |
| `int64` | 64 位有符号整数 | `int64 n = 123456789012;` |
| `float` | 32 位浮点数 | `float pi = 3.14;` |
| `ufloat` | 无符号浮点 | `ufloat f = 2.5;` |
| `char` | 单字符（0-255） | `char ch = 'A';` |
| `bool` | 布尔值 | `bool flag = true;` |
| `string` / `str` | 字符串 | `string s = "hello";` |

类型之间支持隐式转换。`int` 可以赋给 `float`，`char` 可以参与整数运算。`uint` / `ufloat` 不允许存负数，语义分析阶段会检查。

`str` 是 `string` 的别名，但**它是保留字**，不能当变量名。

### 变量与数组

```yxg
int x = 10;
float pi = 3.14159;
string name = "YXG";

int[] scores = {90, 85, 100, 78};
int[][] matrix = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

int[] dyn = new int[100];
int first = scores[0];
int len = scores.length;
```

| 写法 | 说明 |
|---|---|
| `int[] a = {1,2,3};` | 栈数组，编译期确定大小 |
| `int[] a = new int[100];` | 堆数组，运行时分配 |
| `a.length` | 元素总数（多维时是总数，不是行数） |

**数组是值语义**：`int[] b = a;` 会复制一份，改 `b` 不影响 `a`。传参也是复制。

数组字面量只能用于**变量初始化**，不能用于赋值或 `return`。

### 运算符

- 算术：`+`  `-`  `*`  `/`  `%`
- 比较：`==`  `!=`  `>`  `<`  `>=`  `<=`
- 逻辑：`&&`  `||`  `!`（短路求值）
- 位运算：`&`  `|`  `~`  `^`  `<<`  `>>`
- 复合赋值：`+=`  `-=`  `*=`  `/=`  `%=`  `<<=`  `>>=`  `^=`
- 自增自减：`++`  `--`
- 三目：`? :`
- 类型转换：`as`

优先级遵循 C 语言惯例。

```yxg
int a = 10;
int b = a + 5;
bool c = a > 10 && b < 20;
int max = a > b ? a : b;
float f = a as float;
```

> 注意：赋值不是表达式。`int z = (x = 20);` 和 `a = b = c = 1;` 都会报错，要拆成多条语句。

### 控制流

**if / else if / else**（条件不需要括号，但花括号必需）：

```yxg
if score >= 90 {
    print "A\n";
} else if score >= 80 {
    print "B\n";
} else {
    print "F\n";
}
```

**switch / case**：

```yxg
switch day {
    case 1: print "Monday\n"; break;
    case 2: print "Tuesday\n"; break;
    default: print "Unknown\n";
}
```

漏写 `break` 会落穿到下一个 case（和 C 一样），不报错。

**while**：

```yxg
while i < n {
    i = i + 1;
}
```

**do-while**：

```yxg
do {
    sum = sum + n;
    n = n - 1;
} while n > 0;
```

**for**（初始化段必须是单变量声明，**不能加括号**）：

```yxg
for int i = 0; i < 10; i = i + 1 {
    print i, "\n";
}
```

**foreach**（只能遍历数组）：

```yxg
for int v : numbers {
    print v, "\n";
}

// 省略类型也可以
for v : numbers {
    print v, "\n";
}
```

> `for (int i = 0; ...)` 带括号会报错。`foreach` 不支持字符串，也不支持元素是数组的多维遍历。

`break` 和 `continue` 在循环和 `switch` 中均可用。

### 函数

```yxg
pub int add(int a, int b) {
    return a + b;
}

pub void greet(string name) {
    print "Hello, ", name, "\n";
}

pub void main() {
    print add(3, 5), "\n";
    greet("Alice");
}
```

| 要点 | 说明 |
|---|---|
| 参数与返回值 | 都要显式声明类型，无返回值用 `void` |
| 定义顺序 | **不受限制**，函数可以先用后定义 |
| 函数重载 | **不支持**，同作用域内函数名必须唯一（重复定义报 `E3002`） |
| 泛型函数 | 支持，语法是 `<T> T f(T x)`（类型参数在返回类型前） |
| 返回数组 | 必须返回 `new` 分配的堆数组，不能返回栈数组字面量 |

泛型函数示例：

```yxg
pub <T> T pick(T a, T b, bool useFirst) {
    if useFirst { return a; }
    return b;
}

pub void main() {
    print pick(1, 2, false), "\n";        // 2
    print pick("x", "y", true), "\n";     // x
}
```

### 类与面向对象

```yxg
pub class Point {
    pub int x;
    pub int y;
    pri int secret;

    pub Point(int x, int y) {
        this.x = x;
        this.y = y;
        this.secret = 0;
    }

    pub void move(int dx, int dy) {
        this.x = this.x + dx;
        this.y = this.y + dy;
    }

    pub int getX() { return this.x; }
}

pub class ColorPoint : Point {
    pub int color;

    pub ColorPoint(int x, int y, int color) {
        this.x = x;
        this.y = y;
        this.color = color;
    }

    pub void display() {
        print "ColorPoint(", this.x, ",", this.y, ")\n";
    }
}

pub void main() {
    Point p = new Point(10, 20);
    p.move(5, -3);
    print p.getX(), "\n";          // 15

    ColorPoint cp = new ColorPoint(0, 0, 255);
    cp.display();
}
```

访问控制三种：

| 修饰符 | 含义 |
|---|---|
| `pub` | 公开，任何地方可访问 |
| `pro` | 受保护，子类可访问 |
| `pri` | 私有，仅类内可访问，自动生成 getter/setter |

**对象必须 `new`**。`Point p;` 声明的 `p` 是 null，访问成员会报 `Error: null pointer dereference`。

**多态需要 `virtual` / `override`**：

```yxg
pub class Shape {
    pub virtual float area() {
        return 0.0;
    }
    pub void describe() {
        print "area = ", this.area(), "\n";
    }
}

pub class Circle : Shape {
    pub float r;
    pub override float area() {
        return 3.14159 * this.r * this.r;
    }
}
```

不加 `virtual` / `override` 就是静态绑定，`describe()` 里调 `area()` 会执行父类版本。

### 枚举

```yxg
enum Status {
    Pending = 0,
    Active = 1,
    Completed,
    Failed
}

pub void main() {
    Status s = Status.Pending;
    switch s {
        case Status.Pending:   print "waiting\n"; break;
        case Status.Active:    print "running\n"; break;
        case Status.Completed: print "done\n"; break;
        default:               print "unknown\n";
    }
}
```

枚举成员未显式赋值会自动递增。枚举本质是 `int`，可参与整数运算。

### 泛型

```yxg
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
    print "box = ", b.get(), "\n";          // 42

    List<JsonValue> list = new List<JsonValue>();
    list.add(Json.makeInt(1));
    print "size = ", list.size(), "\n";     // 1

    List<List<JsonValue>> matrix = new List<List<JsonValue>>();
    matrix.add(list);
    print "rows = ", matrix.size(), "\n";   // 1
}
```

支持：泛型类、多类型参数 `Pair<A,B>`、嵌套泛型、泛型继承、泛型函数。

详见[泛型](../part2/generics.md)一章。

### 异常处理

```yxg
pub class AppError : ERROR {
    pub AppError(string msg) {
        this.info = msg;
    }
}

pub int divide(int a, int b) {
    if b == 0 {
        throw new AppError("division by zero");
    }
    return a / b;
}

pub void main() {
    try {
        print divide(10, 2), "\n";
        print divide(10, 0), "\n";
    } catch (AppError e) {
        print "caught: ", e.info, "\n";
    } end {
        print "cleanup\n";
    }
}
```

| 要点 | 说明 |
|---|---|
| 抛出 | `throw new MyError(...)`，异常类必须继承 `ERROR` |
| 捕获 | `catch (类型 变量名)`，**需要括号** |
| 收尾块 | 用 **`end`** 关键字，不是 `finally` |
| 顺序 | `catch (ERROR e)` 放最后 |

详见[异常处理](../part2/exceptions.md)一章。

### 模块系统

用点路径导入：

```yxg
import yxg.Math;
import yxg.String;
import yxg.System;

pub void main() {
    print Math.sqrt(16.0), "\n";       // 4.0
    print String.len("hello"), "\n";   // 5
    print System.time(), "\n";
}
```

| 要点 | 说明 |
|---|---|
| 搜索路径 | 当前目录、编译器 exe 下的 `lib/`、`YXG_PATH` 环境变量 |
| 重复导入 | 只导入一次 |
| 别名 | 用 `as` 关键字 |

详见[模块系统](../part2/modules.md)一章。

### 标准库

`String`、`Math`、`System` 的方法都是 **`static`**，用 `类名.方法()` 调用，不要 `new`：

| 库 | 引入 | 调用方式 |
|---|---|---|
| String | `import yxg.String;` | `String.len(s)` / `String.sub(s,0,3)` |
| Math | `import yxg.Math;` | `Math.sqrt(x)` / `Math.random()` |
| System | `import yxg.System;` | `System.time()` / `System.sleep(ms)` |

容器类需要 `new`：

```yxg
import yxg.Collections.List;
import yxg.Collections.Map;

List<int> list = new List<int>();
Map<string, int> map = new Map<string, int>();
```

完整清单见[标准库](../part2/stdlib.md)与[图形与多媒体库](../part5/graphics.md)。

## 与其他语言的关系

| 特性 | 灵感来源 |
|---|---|
| 基本语法（分号、花括号） | C |
| 类型声明（`int x`） | C / Java |
| 类和继承 | Java / C# |
| foreach 循环 | C# / JavaScript |
| `this` 关键字 | Java / C++ |
| `new` 操作符 | Java / C++ |
| 字符串拼接（`"a" + ch`） | JavaScript |
| 访问控制（`pub`/`pro`/`pri`） | C++ |
| 枚举 | C / Java |
| `import` 模块系统 | Python / Go |
| `.length` 属性 | C# / JavaScript |
| 泛型 | Java / C# |
| `try`/`catch`/`end` | Python / Ruby |

## 限制与取舍

为了保持编译器可实现性，YXG 有意回避：

- **仅 Win32 x86** —— 代码生成面向 Windows + NASM + MinGW
- **没有 lambda 表达式** —— 函数作为值要用 `Function` 对象
- **没有操作符重载**
- **没有 `super`** —— 不能在子类里用父类名调用被重写的方法
- **没有自动类型提升的字符串拼接** —— `string + int` 会报类型不匹配
- **方法派发需显式声明** —— 不写 `virtual` / `override` 就是静态绑定

这些取舍让整个编译器保持在可手写的规模内，适合作为教学项目。

## 继续阅读

现在你已经对 YXG 有了整体了解。下一章搭建编译环境，运行第一个程序。
