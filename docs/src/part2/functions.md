# 函数

## 定义函数

函数是组织代码的基本单元。定义函数的语法：

    返回类型 函数名(参数列表) {
        函数体
        return 返回值;
    }

一个简单的例子：

    int add(int a, int b) {
        return a + b;
    }

    int main() {
        int result = add(3, 5);
        print "3 + 5 = ", result, "\n";
        return 0;
    }

输出：

    3 + 5 = 8

print 语句本身不插入空格也不换行，输出中的空格和换行都来自字符串字面量里的 `" "` 和 `"\n"`。

## 参数

函数可以接收零个或多个参数，每个参数需要声明类型和名称：

    // 无参数
    void sayHello() {
        print "Hello!\n";
    }

    // 一个参数
    void greet(string name) {
        print "Hello, ", name, "\n";
    }

    // 多个参数
    int max(int a, int b, int c) {
        if a >= b && a >= c { return a; }
        if b >= a && b >= c { return b; }
        return c;
    }

参数之间用逗号分隔。调用时实参按位置顺序传递：

    int main() {
        sayHello();
        greet("Alice");
        int m = max(10, 25, 15);
        print "最大值: ", m, "\n";
        return 0;
    }

输出：

    Hello!
    Hello, Alice
    最大值: 25

## 返回值

函数通过 return 语句返回值。返回类型必须与声明一致：

    int getNumber() {
        return 42;
    }

    float getPi() {
        return 3.14159;
    }

    string getMessage() {
        return "Hello, World!";
    }

    bool isPositive(int n) {
        return n > 0;
    }

void 函数没有返回值，return 语句可选：

    void printTwice(string s) {
        print s;
        print s;
        print "\n";
    }

    void maybeReturn(int x) {
        if x < 0 {
            return;
        }
        print "x is non-negative: ", x, "\n";
    }

### 返回多个值

返回类型可以写成括号里的类型列表，用 `getReturn(i)` 取值：

```yxg
pub (int, string) pair() {
    return (123, "aaa");
}

pub void main() {
    print pair().getReturn(0), "\n";    // 123
    print pair().getReturn(1), "\n";    // aaa
}
```

可变数量的返回值用 `(int...)`，返回时用 `[...]` 包：

```yxg
pub (int...) many() {
    return ([5, 6, 7]);
}

pub void main() {
    print many().getReturn(0).length, "\n";     // 3
}
```

完整语法、混合写法、函数指针组合见[多返回值与可变参数](variadic.md)。

> 注意：YXG 没有元组类型，也不能解构赋值。多返回值只能即时用 `getReturn` 取，不能存进变量。

## 参数进阶

### 可变参数

参数名后面加 `...`，调用时用 `[...]` 传一组实参：

```yxg
pub int sum(int a, ...) {
    int s = 0;
    int i = 0;
    while i < a.length {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}

pub void main() {
    print sum([1, 2, 3, 4]), "\n";     // 10
}
```

变参位置收到的是一个**数组**，用 `a.length` 和 `a[i]` 访问。

支持多个可变参数：

```yxg
pub int mul(int a, ..., int b, ..., char op) { ... }
mul([1, 2, 3], [4, 5, 6], '+');
```

详见[多返回值与可变参数](variadic.md)。

### 指针参数

要让函数修改调用方的变量，传指针：

```yxg
pub unsafe void increment(int* p) {
    *p = *p + 1;
}

pub void main() {
    int a = 10;
    increment(&a);
    print a, "\n";     // 11
}
```

详见[指针](pointers.md)。

### 函数指针

函数可以作为参数传递：

```yxg
pub int add(int a, int b) { return a + b; }

pub unsafe int apply(int (*f)(int, int), int x, int y) {
    return f(x, y);
}

pub void main() {
    print apply(&add, 4, 5), "\n";     // 9
}
```

函数指针支持返回、数组、嵌套、多返回值，完整说明见[指针](pointers.md)的「函数指针」一节。

## 数组参数与返回值

函数可以接收和返回数组：

    int sum(int[] arr) {
        int total = 0;
        for int v : arr {
            total += v;
        }
        return total;
    }

    int[] makeArray(int size) {
        int[] arr = new int[size];
        for int i = 0; i < size; i++ {
            arr[i] = i * 2;
        }
        return arr;
    }

    int main() {
        int[] nums = {1, 2, 3, 4, 5};
        int s = sum(nums);
        print "总和: ", s, "\n";

        int[] evens = makeArray(5);
        for int v : evens {
            print v, "\n";
        }

        return 0;
    }

输出：

    总和: 15
    0
    2
    4
    6
    8

多维数组也可以作为参数。注意多维数组的 foreach 不好用，直接按下标遍历即可：

    void printMatrix(int[][] matrix) {
        for int row = 0; row < matrix.length; row++ {
            for int col = 0; col < matrix[row].length; col++ {
                print matrix[row][col], "\n";
            }
        }
    }

## 函数重载

**YXG 支持函数重载**：同名函数只要参数类型或参数个数不同，就是不同的函数。

按参数类型重载：

    pub int add(int a, int b) {
        return a + b;
    }

    pub float add(float a, float b) {
        return a + b;
    }

    pub void main() {
        print add(1, 2), "\n";         // 3
        print add(1.5, 2.5), "\n";     // 4.0
    }

按参数个数重载：

    pub int add(int a) {
        return a + 1;
    }

    pub int add(int a, int b) {
        return a + b;
    }

    pub void main() {
        print add(5), "\n";        // 6
        print add(5, 6), "\n";     // 11
    }

多个类型都可以重载：

    pub int val(int a) { return 1; }
    pub int val(float a) { return 2; }
    pub int val(string a) { return 3; }

    pub void main() {
        print val(1), " ", val(1.5), " ", val("x"), "\n";   // 1 2 3
    }

### 匹配规则

调用时按**参数个数 + 参数类型**精确匹配：

    pub int show(int a) { return 100; }
    pub int show(float a) { return 200; }

    pub void main() {
        int i = 5;
        float f = 5.0;
        print show(i), "\n";       // 100（int 版本）
        print show(f), "\n";       // 200（float 版本）
        print show(5), "\n";       // 100（整数字面量匹配 int）
        print show(5.5), "\n";     // 200（浮点字面量匹配 float）
    }

`char` 和 `int` 也能区分：

    pub int pick(int a) { return 1; }
    pub int pick(char a) { return 2; }

    pub void main() {
        char c = 'A';
        print pick(c), "\n";       // 2（char 版本）
        print pick(65), "\n";      // 1（int 版本）
    }

### 什么情况会报错

**参数类型和个数完全相同**才算重复定义：

    pub int add(int a, int b) { return a + b; }
    pub int add(int a, int b) { return a * b; }     // 错误

报错：

    error[E3002]: duplicate function 'add'

**只有返回类型不同**也不行：

    pub int f(int a) { return a; }
    pub float f(int a) { return a as float; }       // 错误

报错：

    error[E3002]: function 'f' redeclared with different return type

因为调用点无法从返回值推断该用哪个版本。

### 建议

虽然支持重载，但参数类型相近时（比如 `int` 和 `uint`）容易写错调用。类型差异明显时用重载可读性好；差异细微时，用不同函数名更清楚：

    pub int addInt(int a, int b) { return a + b; }
    pub float addFloat(float a, float b) { return a + b; }

## 递归

函数可以调用自身：

    int factorial(int n) {
        if n <= 1 {
            return 1;
        }
        return n * factorial(n - 1);
    }

    int fibonacci(int n) {
        if n <= 1 {
            return n;
        }
        return fibonacci(n - 1) + fibonacci(n - 2);
    }

    int main() {
        print "5! = ", factorial(5), "\n";
        print "fib(7) = ", fibonacci(7), "\n";
        return 0;
    }

输出：

    5! = 120
    fib(7) = 13

递归深度受限于栈空间。YXG 没有尾递归优化，深度递归可能导致栈溢出。

## main 函数

main 函数是程序的入口点，每个程序必须有且仅有一个。返回类型必须是 int，return 0 表示正常退出。

### 无参数版本

    int main() {
        print "Hello, YXG!\n";
        return 0;
    }

### 接收命令行参数

main 函数可以声明为接收 string[] 参数来获取命令行参数：

    int main(string[] args) {
        print "参数个数: ", args.length, "\n";
        for int i = 0; i < args.length; i++ {
            print "args[", i, "] = ", args[i], "\n";
        }
        return 0;
    }

args[0] 始终是程序自身的路径，后续元素是用户传递的参数。

### 通过 System 库获取命令行参数

除了在 main 函数签名中声明参数外，也可以使用 System 标准库在程序的任何位置获取命令行参数。System 的所有成员都是 static，直接通过类名 `System.xxx()` 调用，不要 new：

    import yxg.System;

    int main() {
        int count = System.argc();
        print "参数个数: ", count, "\n";

        for int i = 0; i < count; i++ {
            print "argv[", i, "] = ", System.argv(i), "\n";
        }

        System.printArgs();

        return 0;
    }

System 库还提供了其他系统功能：

    import yxg.System;

    int main() {
        int result = System.system("dir");
        int now = System.time();
        print "当前时间戳: ", now, "\n";
        System.sleep(1000);
        string path = System.env("PATH");
        print "PATH: ", path, "\n";

        return 0;
    }

## 函数定义顺序

函数的定义与调用顺序没有强制要求。在 main 中调用的函数，只要在同一个文件里出现，无论写在 main 之前还是之后都能编译：

    int add(int a, int b) {
        return a + b;
    }

    int main() {
        int result = add(3, 5);
        print result, "\n";
        return 0;
    }

把 add 写在 main 后面，输出同样是 `8`：

    int main() {
        int result = add(3, 5);
        print result, "\n";
        return 0;
    }

    int add(int a, int b) {
        return a + b;
    }

真正会报错的是「调用了根本不存在的函数」：

    int main() {
        int result = add(3, 5);    // 错误！没有定义 add
        print result;
        return 0;
    }

编译器报错：

    error[E3008]: function 'add' is not declared

当函数分散在多个文件时，需要 import 对应的模块（见模块一章）。

## 局部变量与作用域

函数内部声明的变量是局部变量，只在函数内可见：

    void myFunc() {
        int x = 10;
        print x, "\n";
    }

    int main() {
        myFunc();
        // print x;             // 错误！x 不在作用域内
        return 0;
    }

函数参数也是局部变量，传递方式是值传递：

    void test(int n) {
        n = n + 1;
        print "函数内: ", n, "\n";
    }

    int main() {
        int a = 5;
        test(a);
        print "函数外: ", a, "\n";
        return 0;
    }

输出：

    函数内: 6
    函数外: 5

对于基本类型，传递的是值的副本。对于数组和类实例，传递的是引用的副本（但引用指向同一对象）。

## 数组参数的传递

数组作为参数时传递的是引用，函数内修改数组会影响原数组：

    void doubleElements(int[] arr) {
        for int i = 0; i < arr.length; i++ {
            arr[i] = arr[i] * 2;
        }
    }

    int main() {
        int[] nums = {1, 2, 3, 4, 5};
        doubleElements(nums);

        for int v : nums {
            print v, "\n";
        }

        return 0;
    }

输出：

    2
    4
    6
    8
    10

## 常见错误

### 返回类型不匹配

    int getValue() {
        return "hello";              // 错误！
    }

编译器报错：

    error[E3019]: return type mismatch: expected 'int', got 'string'

### 缺少 return 语句

    int getValue() {
        int x = 10;
        // 忘记 return
    }

编译器报错：

    error[E3019]: not all control paths return a value in function 'getValue'

### 参数数量不匹配

    int add(int a, int b) { return a + b; }

    int main() {
        int x = add(5);              // 错误！
        return 0;
    }

编译器报错：

    error[E3009]: no matching function 'add' for 1 argument(s)

### 参数类型不匹配

    void printNumber(int n) { print n, "\n"; }

    int main() {
        printNumber("hello");        // 错误！
        return 0;
    }

编译器报错：

    error[E3009]: no matching function 'printNumber' for 1 argument(s)

参数类型不对时同样报 E3009（参数匹配失败），不会给出单独的「类型不匹配」编号。

## 继续阅读

- [指针](pointers.md)：用指针修改调用方的变量、函数指针回调
- [多返回值与可变参数](variadic.md)：返回多个值、`...` 变参
- [数组](arrays.md)：数组参数与返回值
