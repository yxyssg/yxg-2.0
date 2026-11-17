# 类与面向对象：定义类

## 基本语法

类是 YXG 面向对象编程的核心。定义类使用 class 关键字：

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

类将数据（字段）和行为（方法）封装在一起。对象变量声明时不分配实例，默认值是 null，必须用 `new` 创建实例后才能访问字段和方法。

## 字段

字段是类中声明的变量，每个实例拥有自己的一份副本：

    class Person {
        string name;
        int age;
        float height;
    }

    int main() {
        Person p1 = new Person();
        p1.name = "Alice";
        p1.age = 25;
        p1.height = 165.5;

        Person p2 = new Person();
        p2.name = "Bob";
        p2.age = 30;
        p2.height = 178.0;

        print p1.name, " ", p1.age, " ", p1.height, "\n";
        print p2.name, " ", p2.age, " ", p2.height, "\n";
        return 0;
    }

输出：

    Alice 25 165.5
    Bob 30 178.0

每个 new 出来的实例都有一份独立的字段副本。字段的默认值与其类型对应：int 为 0，float 为 0.0，string 为 null，bool 为 false。

## 方法

方法是定义在类内部的函数，可以访问类的字段：

    class Counter {
        int value;

        pub void increment() {
            value = value + 1;
        }

        pub void decrement() {
            value = value - 1;
        }

        pub int getValue() {
            return value;
        }

        pub void reset() {
            value = 0;
        }
    }

    int main() {
        Counter c = new Counter();
        c.increment();
        c.increment();
        c.increment();
        print c.getValue(), "\n";

        c.decrement();
        print c.getValue(), "\n";

        c.reset();
        print c.getValue(), "\n";

        return 0;
    }

输出：

    3
    2
    0

方法使用 pub 标记为公开，外部代码才能调用。没有 pub 的方法默认为私有，仅类内部可用。

## this 关键字

在方法内部，this 指代当前实例：

    class Rectangle {
        int width;
        int height;

        pub void setSize(int width, int height) {
            this.width = width;
            this.height = height;
        }

        pub int area() {
            return this.width * this.height;
        }
    }

当参数名和字段名相同时，必须用 this 区分。如果名字不冲突，this 可以省略。

## 构造函数

构造函数是与类同名的方法，在使用 new 创建实例时自动调用：

    class Point {
        int x;
        int y;

        pub Point(int x, int y) {
            this.x = x;
            this.y = y;
        }

        pub void show() {
            print "Point(", x, ", ", y, ")\n";
        }
    }

    int main() {
        Point p = new Point(10, 20);
        p.show();

        Point q = new Point(5, 15);
        q.show();

        return 0;
    }

输出：

    Point(10, 20)
    Point(5, 15)

构造函数没有返回类型（连 void 也不写）。使用 new 调用构造函数创建实例。

注意方法名不能叫 `print`：print 是语言内建的关键字，写成 `pub void print()` 会编译报错 `error[E2002]: expected variable name`。改用 `show()`、`printInfo()` 这类名字。

即使类里没有定义构造函数，也不能省略 new：

    class Simple {
        int value;
    }

    int main() {
        Simple s = new Simple();
        s.value = 42;
        print s.value, "\n";
        return 0;
    }

输出：

    42

写成 `Simple s;` 只是声明了一个值为 null 的引用，紧接着访问 `s.value` 会在运行时输出 `Error: null pointer dereference`。

## 访问控制

YXG 提供三种访问级别：

    class Example {
        pub int publicField;
        pro int protectedField;
        pri int privateField;

        pub void publicMethod() {
            print "public\n";
        }

        pro void protectedMethod() {
            print "protected\n";
        }

        pri void privateMethod() {
            print "private\n";
        }
    }

pri 字段的特殊之处：编译器会自动生成 getter 和 setter 方法。例如 pri int value 会生成：

    int getValue() { return this.value; }
    void setValue(int value) { this.value = value; }

外部代码通过 getter/setter 访问私有字段：

    class Example {
        pri int value;
    }

    int main() {
        Example e = new Example();
        e.setValue(100);
        int v = e.getValue();
        print v, "\n";
        return 0;
    }

输出：

    100

getter/setter 是按字段自动生成的，签名固定为 `get字段名()` / `set字段名(值)`，所以一个 pri 字段就同时拥有了读写入口。注意：类里必须真的有对应的 pri 字段才会生成；只声明而没有对应 pri 字段的 getC()/setC() 会编译报错 `error[E3015]: no matching method`。

## 完整示例：银行账户

    class BankAccount {
        pri string accountNumber;
        pri float balance;
        pub string ownerName;

        pub BankAccount(string owner, string number) {
            this.ownerName = owner;
            this.accountNumber = number;
            this.balance = 0.0;
        }

        pub void deposit(float amount) {
            if amount > 0 {
                balance = balance + amount;
                print "存入: ", amount, " 余额: ", balance, "\n";
            }
        }

        pub bool withdraw(float amount) {
            if amount > 0 && amount <= balance {
                balance = balance - amount;
                print "取出: ", amount, " 余额: ", balance, "\n";
                return true;
            }
            print "余额不足\n";
            return false;
        }

        pub float getBalance() {
            return balance;
        }

        pub string getAccountNumber() {
            return accountNumber;
        }
    }

    int main() {
        BankAccount acc = new BankAccount("Alice", "6222021234567890");

        print "账户: ", acc.getAccountNumber(), "\n";
        print "户主: ", acc.ownerName, "\n";

        acc.deposit(1000.0);
        acc.deposit(500.0);
        acc.withdraw(300.0);
        acc.withdraw(2000.0);

        print "最终余额: ", acc.getBalance(), "\n";
        return 0;
    }

输出：

    账户: 6222021234567890
    户主: Alice
    存入: 1000.0 余额: 1000.0
    存入: 500.0 余额: 1500.0
    取出: 300.0 余额: 1200.0
    余额不足
    最终余额: 1200.0

float 用 %g 格式输出，1000.0 打印出来是 `1000.0`，不会带 6 位小数。

## 常见错误

### 忘记 new

    Point p = new Point(1, 2);     // 正确
    // Point p = Point(1, 2);      // 错误！需要 new，报 E3008 找不到函数 Point

即使类没有构造函数，也不能写成 `Point p;` 然后直接访问成员：

    class Point { int x; int y; }

    int main() {
        Point p;                   // p 是 null
        p.x = 10;                  // 运行时输出：Error: null pointer dereference
        return 0;
    }

正确写法：

    int main() {
        Point p = new Point();
        p.x = 10;
        print p.x, "\n";
        return 0;
    }

### 访问私有成员

    class Foo {
        pri int secret;
    }

    int main() {
        Foo f = new Foo();
        // f.secret = 42;           // 错误！secret 是私有的，报 E3014
        f.setSecret(42);            // 正确：通过自动生成的 setter
        print f.getSecret(), "\n";
        return 0;
    }

输出：

    42

### 构造函数没有返回类型

    class Foo {
        // pub void Foo() { ... }   // 错误！构造函数不能有 void
        pub Foo() { ... }           // 正确
    }

## 继续阅读

掌握了类的基本定义后，下一章将学习类的继承。