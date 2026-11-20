# 类与面向对象

## 概述

类是 YXG 面向对象编程的核心。类将数据（字段）和行为（方法）封装在一起，支持构造函数、访问控制、继承和方法重写（当前版本没有虚函数派发）。

## 定义一个简单的类

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

对象变量声明后默认是 null，必须先 `new` 出实例才能访问成员。写成 `Point p; p.x = 10;` 会在运行时输出 `Error: null pointer dereference`。

## 类的基本组成

一个完整的类可以包含以下成员：

    class MyClass {
        // 字段
        pub int publicField;
        pro int protectedField;
        pri int privateField;

        // 构造函数
        pub MyClass(int value) {
            this.publicField = value;
        }

        // 方法
        pub void doSomething() {
            print "doing something\n";
        }

        pro int calculate() {
            return publicField * 2;
        }

        pri void internalWork() {
            print "internal\n";
        }
    }

## 创建和使用对象

无论类有没有构造函数，创建对象都必须使用 new：

### 无构造函数时也必须 new

    class Simple {
        int value;
        string name;
    }

    int main() {
        Simple s = new Simple();
        s.value = 42;
        s.name = "test";
        print s.value, " ", s.name, "\n";
        return 0;
    }

输出：

    42 test

类里没写构造函数时，`new Simple()` 依然可用（相当于调入一个什么都不做的构造函数），实例的字段按类型取默认值。

### 有构造函数时用 new 传入实参

    class Point {
        int x;
        int y;

        pub Point(int x, int y) {
            this.x = x;
            this.y = y;
        }
    }

    int main() {
        Point p = new Point(10, 20);
        print p.x, " ", p.y, "\n";
        return 0;
    }

输出：

    10 20

## 类中的 this

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

当参数名和字段名相同时，必须用 this 区分。名字不冲突时 this 可以省略。

## 访问控制

YXG 提供三种访问级别：

| 修饰符 | 类内部 | 子类 | 外部 |
|--------|--------|------|------|
| pub | ✓ | ✓ | ✓ |
| pro | ✓ | ✓ | ✗ |
| pri | ✓ | ✗ | ✗ |

    class Example {
        pub int a;        // 任何地方可访问
        pro int b;        // 本类和子类可访问
        pri int c;        // 仅本类可访问，自动生成 getter/setter
    }

pri 字段会自动生成 getter 和 setter，例如 pri int value 会生成 getValue() 和 setValue(int v)。

## 继承

YXG 支持多继承，子类用冒号指定父类，多个父类用逗号分隔：

    class Animal {
        string name;

        pub void speak() {
            print name, " makes a sound\n";
        }
    }

    class Flyable {
        pub void fly() {
            print "Flying...\n";
        }
    }

    class Bird : Animal, Flyable {
        pub void chirp() {
            print name, " chirps!\n";
        }
    }

    int main() {
        Bird b = new Bird();
        b.name = "Tweety";
        b.speak();
        b.fly();
        b.chirp();
        return 0;
    }

输出：

    Tweety makes a sound
    Flying...
    Tweety chirps!

子类继承父类所有 `pub` 和 `pro` 成员。

**虚函数派发**：基类方法标 `virtual`、子类标 `override` 后，通过基类引用调用会派发到子类的版本：

```yxg
pub class Animal {
    pub string name;
    pub virtual void speak() {
        print this.name, " makes a sound\n";
    }
    pub void intro() {
        print "I am ";
        this.speak();          // ← 也会派发到子类版本
    }
}

pub class Dog : Animal {
    pub override void speak() {
        print this.name, " barks!\n";
    }
}

pub void main() {
    Animal a = new Dog();
    a.name = "Spot";
    a.speak();                 // Spot barks!
    a.intro();                 // I am Spot barks!
}
```

**非泛型类之间不用写关键字也会自动派发**：只要子类声明了和父类同名的方法，编译器就把父类方法标成 `virtual`、子类方法标成 `override`。

```yxg
pub class Shape {
    pub int sides() { return -1; }        // 自动变 virtual
}
pub class Circle : Shape {
    pub int sides() { return 0; }         // 自动变 override
}

Shape a = new Circle();
print a.sides();      // 0
```

**泛型类仍然需要显式写 `virtual` / `override`**，否则静态绑定，编译器会给警告：

```
warning[E3003]: method 'x' hides parent method 'x' but neither is marked
'virtual'/'override'; calls through a parent-typed variable will use the parent version
```

详见[继承](inheritance.md)。

## 类与结构体

YXG 没有单独的 struct 关键字。没有方法的类本质上就是结构体：

    // 这就像一个 C 结构体
    class Point3D {
        float x;
        float y;
        float z;
    }

    int main() {
        Point3D p = new Point3D();
        p.x = 1.0;
        p.y = 2.0;
        p.z = 3.0;
        print p.x, " ", p.y, " ", p.z, "\n";
        return 0;
    }

输出：

    1.0 2.0 3.0

## 嵌套类

类里可以定义类，用 `Outer.Inner` 访问：

```yxg
pub class Outer {
    pub int v;

    pub class Inner {
        pub int w;
    }
}

pub void main() {
    Outer o = new Outer();
    o.v = 1;

    Outer.Inner i = new Outer.Inner();
    i.w = 7;
    print o.v, " ", i.w, "\n";     // 1 7
}
```

### 嵌套类可以有方法

```yxg
pub class Outer {
    pub class Inner {
        pub int w;
        pub int get() { return this.w * 2; }
    }

    pub Inner make(int x) {
        Inner i = new Inner();
        i.w = x;
        return i;
    }
}

pub void main() {
    Outer o = new Outer();
    Outer.Inner i = o.make(5);
    print i.get(), "\n";     // 10
}
```

外层类的方法可以直接用 `Inner` 短名，外面要用 `Outer.Inner`。

### 多层嵌套

```yxg
pub class A {
    pub class B {
        pub class C {
            pub int n;
        }
    }
}

pub void main() {
    A.B.C c = new A.B.C();
    c.n = 9;
    print c.n, "\n";     // 9
}
```

### 注意

嵌套类**和外部类没有自动关联**——`Inner` 不能直接访问 `Outer` 的实例字段，需要显式传对象：

```yxg
pub class Outer {
    pub int v;

    pub class Inner {
        pub int copyOf(Outer o) {
            return o.v;          // 通过参数访问
        }
    }
}
```

## 完整示例：学生管理系统

    class Student {
        pri int id;
        pub string name;
        pri float score;

        pub Student(int id, string name, float score) {
            this.id = id;
            this.name = name;
            this.score = score;
        }

        pub int getId() {
            return id;
        }

        pub float getScore() {
            return score;
        }

        pub void setScore(float s) {
            if s >= 0 && s <= 100 {
                score = s;
            }
        }

        pub string getGrade() {
            if score >= 90 { return "A"; }
            if score >= 80 { return "B"; }
            if score >= 70 { return "C"; }
            if score >= 60 { return "D"; }
            return "F";
        }

        pub void printInfo() {
            print "ID: ", id, " 姓名: ", name, " 分数: ", score, " 等级: ", getGrade(), "\n";
        }
    }

    int main() {
        Student s1 = new Student(1001, "Alice", 92.5);
        Student s2 = new Student(1002, "Bob", 78.0);
        Student s3 = new Student(1003, "Charlie", 55.5);

        s1.printInfo();
        s2.printInfo();
        s3.printInfo();

        s3.setScore(68.0);
        print "Charlie 补考后:\n";
        s3.printInfo();

        return 0;
    }

输出：

    ID: 1001 姓名: Alice 分数: 92.5 等级: A
    ID: 1002 姓名: Bob 分数: 78.0 等级: C
    ID: 1003 姓名: Charlie 分数: 55.5 等级: F
    Charlie 补考后:
    ID: 1003 姓名: Charlie 分数: 68.0 等级: D

float 按 `%g` 格式输出，所以 92.5 打出来就是 `92.5`，不会补成 92.500000。

## 章节导航

| 章节 | 内容 |
|------|------|
| 定义类 | 类的声明、字段、方法、构造函数、this |
| 成员访问控制 | pub/pro/pri 详解、getter/setter |
| 继承 | 单继承、多继承、方法重写 |

## 继续阅读

下一章将详细介绍类的定义语法。
