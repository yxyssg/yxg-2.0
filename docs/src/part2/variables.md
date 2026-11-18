# 变量与赋值

## 变量声明

YXG 中变量必须先声明后使用。声明变量的语法为：

    类型 变量名 [= 初始值];

### 带初始化的声明

    int main() {
        int age = 25;
        float price = 19.99;
        string name = "Alice";
        bool active = true;
        char grade = 'A';

        print age, " ", price, " ", name, " ", active, " ", grade, "\n";
        return 0;
    }

输出：

    25 19.99 Alice true A

print 不会自动插入空格，也不会自动换行，所有分隔和换行都要自己写进参数里。

### 不初始化的声明

    int main() {
        int count;
        float score;
        string title;
        bool ready;

        count = 10;
        score = 95.5;
        title = "Manager";
        ready = true;

        print count, " ", score, " ", title, " ", ready, "\n";
        return 0;
    }

输出：

    10 95.5 Manager true

虽然 YXG 允许不初始化，但建议始终在声明时赋值，避免使用默认值带来的意外行为。

### 一次声明多个变量

每条声明语句还可以声明多个变量：

    int main() {
        int a = 1, b = 2;

        print a, " ", b, "\n";
        return 0;
    }

## 赋值

使用 = 进行赋值。赋值是一条语句，不能作为表达式参与运算：

    int main() {
        int x;
        int y;

        x = 10;
        y = x;

        print x, " ", y, "\n";
        return 0;
    }

输出：

    10 10

不能在表达式里使用赋值，也不支持连续赋值：

    int x;
    // int z = (x = 20);   // 错误！赋值不能作为表达式
    // int a = b = c = 100;  // 错误！不支持连续赋值

    int z;
    x = 20;
    z = x;                  // 正确：拆成两条语句
    int a;
    int b;
    a = 100;
    b = 100;                // 正确：逐个赋值

## 复合赋值运算符

YXG 支持所有常见的复合赋值：

    int main() {
        int x = 10;

        x += 5;
        x -= 3;
        x *= 2;
        x /= 4;
        x %= 4;

        print x, "\n";
        return 0;
    }

输出：

    2

位运算的复合赋值：

    int main() {
        int x = 0x0F;

        x <<= 4;
        x >>= 2;
        x ^= 0xFF;

        print x, "\n";
        return 0;
    }

输出：

    195

## 自增与自减

    int main() {
        int a = 5;
        int b;

        b = ++a;
        print "前置++: a =", a, " b =", b, "\n";

        b = a++;
        print "后置++: a =", a, " b =", b, "\n";

        b = --a;
        print "前置--: a =", a, " b =", b, "\n";

        b = a--;
        print "后置--: a =", a, " b =", b, "\n";

        return 0;
    }

输出：

    前置++: a =6 b =6
    后置++: a =7 b =7
    前置--: a =6 b =6
    后置--: a =5 b =5

注意：YXG 的后置 ++/-- 与前置行为一致，都是先自增自减再把新值赋给左侧，所以 b 拿到的是修改后的值。

自增自减只能用于变量，不能用于表达式：

    int main() {
        int x = 5;
        // (x + 1)++;     // 错误！不能对表达式自增

        x++;              // 正确
        return 0;
    }

## 变量命名规则

变量名必须遵循以下规则：

3. 区分大小写（name 和 Name 是不同的变量）
4. 不能与关键字同名

合法示例：

    int main() {
        int age;
        int player1_score;
        int _internal;
        int MAX_VALUE;
        int 1st_place;
        int 测试;

        return 0;
    }

不合法示例：

    // int my-var;        // 连字符不允许
    // int class;         // class 是关键字
    // int first name;    // 空格不允许
    // string str;        // str 是保留字，换用 s 或 text

## 作用域

变量的作用域是声明它的函数体或控制流语句块（if、for、while 的花括号）。YXG 不支持独立的 `{}` 语句块，下面的写法会报 `error[E2004]: illegal operand '{'`：

    int main() {
        int outer = 100;

        // {                      // 错误！独立代码块不支持
        //     int inner = 200;
        // }

        return 0;
    }

要限制变量生命周期，把代码放进 if 或 for 的语句块里：

    int main() {
        int outer = 100;

        if outer > 0 {
            int inner = 200;
            print outer, " ", inner, "\n";   // 100 200
        }

        // print inner;          // 错误！inner 已超出作用域

        return 0;
    }

输出：

    100 200

if 语句块内可以声明与外层同名的变量，这会遮蔽外层变量：

    int main() {
        int x = 10;

        if x > 0 {
            int x = 20;
            print x, "\n";           // 20
        }

        print x, "\n";               // 10（外层 x 未被修改）

        return 0;
    }

输出：

    20
    10

函数参数的作用域是整个函数体：

    void greet(string name) {
        print "Hello, ", name, "\n";
    }

## 常量

YXG 目前没有 const 关键字。约定用全大写命名表示不应修改的变量：

    int main() {
        int MAX_SIZE = 100;
        float PI = 3.14159;
        string APP_NAME = "MyApp";

        print MAX_SIZE, " ", PI, " ", APP_NAME, "\n";
        return 0;
    }

输出：

    100 3.14159 MyApp

枚举成员也可以作为具名常量使用：

    enum Limits {
        MAX_PLAYERS = 64,
        MAX_LEVEL = 99,
        TIMEOUT = 30
    }

    int main() {
        int players = Limits.MAX_PLAYERS;
        print "Max players: ", players, "\n";
        return 0;
    }

输出：

    Max players: 64

## 类型别名（通过类名）

YXG 没有 typedef，但可以用类名作为类型：

    class Point {
        int x;
        int y;
    }

    int main() {
        Point p = new Point();
        p.x = 10;
        p.y = 20;
        print p.x, " ", p.y, "\n";
        return 0;
    }

输出：

    10 20

注意 `Point p;` 只声明变量，p 此时是 null，直接访问字段会触发 `Error: null pointer dereference`。必须用 `new Point()` 创建实例。

## 全局变量

定义在函数外面的变量是全局变量，整个程序共享同一份：

    int counter = 0;
    string appName = "demo";

    pub void bump() {
        counter = counter + 1;
    }

    pub void main() {
        print "name=", appName, "\n";
        bump();
        bump();
        print "counter=", counter, "\n";
    }

输出：

    name=demo
    counter=2

### 全局变量的位置

**推荐把全局变量定义在 `main` 之前**，这是最稳妥的写法：

    int counter = 0;          // ← 在 main 之前

    pub void main() {
        print "counter=", counter, "\n";
    }

> **注意**：把全局变量定义在 `main` **之后**、又在 `main` 里引用，当前版本可能读到错误的值。请始终把全局变量写在 `main` 之前。

### 全局数组

    int[] table = {1, 2, 3};

    pub void main() {
        print "len=", table.length, "\n";
        table[0] = 99;
        print "table[0]=", table[0], "\n";
    }

输出：

    len=3
    table[0]=99

### 全局常量

用 `const` 声明不可修改的全局量：

    const int MAX = 100;
    float ratio = 0.5;

    pub void main() {
        print "MAX=", MAX, " ratio=", ratio, "\n";
    }

输出：

    MAX=100 ratio=0.5

对 `const` 赋值会编译报错：

    const int MAX = 100;

    pub void main() {
        MAX = 200;            // 错误
    }

### 全局变量的默认值

未显式初始化的全局变量会被清零：

    int gCount;
    float gRatio;
    bool gFlag;

    pub void main() {
        print gCount, " ", gRatio, " ", gFlag, "\n";
    }

输出：

    0 0.0 false

### 什么时候用全局变量

适合放：

- 程序配置（名称、路径、开关）
- 全局计数器、状态标志
- 常量表

不适合放：

- 只在单个函数里用的临时数据（用局部变量）
- 需要并发访问的共享状态（没有同步机制）

### 局部变量优先

能用局部变量就别用全局。全局变量会让函数之间产生隐式依赖，难以测试和排查：

    // 不好：函数依赖外部状态
    int total = 0;
    pub void add(int x) {
        total = total + x;
    }

    // 更好：通过参数和返回值传递
    pub int addTo(int base, int x) {
        return base + x;
    }

## 常见错误

### 使用未声明的变量

    int main() {
        x = 10;              // 错误：x 未声明
        return 0;
    }

编译器报错：

    error[E3001]: variable 'x' is not declared

### 重复声明

    int main() {
        int x = 10;
        int x = 20;          // 错误：x 已声明
        return 0;
    }

编译器报错：

    error[E3002]: duplicate variable 'x'

### 类型不匹配

    int main() {
        int x = "hello";     // 错误：类型不匹配
        return 0;
    }

编译器报错：

    error[E3003]: type mismatch: expected 'int', got 'string'

### 无符号类型赋负值

    int main() {
        uint x = -5;         // 错误
        return 0;
    }

编译器报错：

    error[E3017]: cannot assign negative value to unsigned type 'uint'

## 继续阅读

掌握了变量与赋值后，下一章将学习 YXG 的运算符体系。
