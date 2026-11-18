# 函数对象

## 把函数当值传递

YXG 里函数可以作为值传递——用 `Function` 类型承载：

    import yxg.Function;

    pub int double_it(int x) {
        return x * 2;
    }

    pub void main() {
        Function f = double_it;      // 直接把函数名赋给 Function
        print "f(21) = ", f(21), "\n";   // f(21) = 42
    }

输出：

    f(21) = 42

`Function f = double_it;` 编译器会自动展开成 `new Function(&double_it)`，不需要手写取地址。

## 调用方式

两种写法等价：

    Function f = double_it;
    print f(21), "\n";          // 简写，自动展开为 f.call(21)
    print f.call(21), "\n";     // 显式调用

`call` 有多个重载，按参数个数选择：

    pub native int  call();           // 无参
    pub native int  call(int a);      // 一个 int 参数

## 作为参数传递

这是函数对象最常用的场景——把行为参数化：

    import yxg.Function;

    pub int addOne(int x) { return x + 1; }
    pub int square(int x) { return x * x; }

    pub int apply(Function f, int v) {
        return f.call(v);
    }

    pub void main() {
        Function a = addOne;
        Function s = square;
        print "apply(a, 10) = ", apply(a, 10), "\n";
        print "apply(s, 10) = ", apply(s, 10), "\n";
    }

输出：

    apply(a, 10) = 11
    apply(s, 10) = 100

## 检查是否为空

`isNull()` 判断函数对象是否为空：

    import yxg.Function;

    pub int twice(int x) { return x * 2; }

    pub void main() {
        Function f = twice;
        print "isNull = ", f.isNull(), "\n";     // isNull = false
        print "call = ", f.call(7), "\n";        // call = 14
    }

## 显式构造

需要从裸地址构造时用构造函数：

    import yxg.Function;

    pub int twice(int x) { return x * 2; }

    pub void main() {
        Function f = new Function(&twice);
        print f.call(3), "\n";                   // 6
    }

三个构造函数：

| 构造函数 | 用途 |
|---|---|
| `new Function(int f)` | 只包函数指针 |
| `new Function(int f, int env)` | 带捕获环境 |
| `new Function(int f, int env, int ret)` | 带环境 + 返回值存储 |

`env` 和 `ret` 是给闭包用的运行时环境指针，一般不需要手动传。

## Function 的字段

`Function` 类公开了三个字段，可以读取：

    pub class Function {
        pub int _raw;           // 原始函数指针
        pub unsafe int* _env;   // 捕获环境
        pub unsafe int* _ret;   // 返回值存储
    }

`raw()` 返回 `_raw` 的值，用于和裸函数指针对比。

## 与 Thread 配合

`Thread` 要求传 `Function` 对象，这是函数对象的典型用法：

    import yxg.Thread;

    pub void worker() {
        print "worker running\n";
    }

    pub void main() {
        Thread t = new Thread(worker);
        t.start();
        t.join();
        print "done\n";
    }

### 给线程函数传参

`Thread` 构造函数支持在函数名后跟参数——编译器会自动生成一个 thunk 把这些实参打包传给 worker：

    import yxg.Thread;

    pub int add2(int a, int b) {
        return a + b;
    }

    pub void main() {
        Thread t = new Thread(add2, 1, 2);
        t.start();
        t.join();
        print "resultInt = ", t.resultInt(), "\n";     // 3
    }

变参用中括号分组：

    pub int w(int a, ...) {
        return a.length;
    }

    Thread t = new Thread(w, [1, 2, 3]);    // a.length == 3

支持的类型、`char` 的位置限制、返回值取值方法，详见[标准库](stdlib.md)的「线程 Thread」一节。

### 无参函数也照常可用

    pub int worker() {
        return 42;
    }

    Thread t = new Thread(worker);
    t.start();
    t.join();
    print t.resultInt(), "\n";     // 42

## 关于 lambda / 匿名函数

**YXG 不支持 lambda 表达式语法**。没有 `() => {}`、`x => x * 2` 这类写法。

需要「现场定义的小函数」时，就在外面定义一个具名函数再赋给 `Function`：

    import yxg.Function;

    pub int addTen(int x) {
        return x + 10;
    }

    pub void main() {
        Function f = addTen;
        print f(5), "\n";        // 15
    }

如果这个函数只在一处用，可以定义在调用点附近，命名清楚即可。

## 注意事项

- **函数签名要匹配**：赋给 `Function` 的函数必须能通过 `call` 调用。`call(int a)` 只接受一个 `int` 参数，多参数函数要用对应的重载或包一层
- **不是真正的闭包**：`Function` 只是函数指针的包装，不支持自动捕获外层变量。需要携带状态时，把状态作为参数传入，或者放在全局变量 / 对象字段里
- **不要漏 import**：用 `Function` 前需要 `import yxg.Function;`

## 继续阅读

下一章介绍模块系统。
