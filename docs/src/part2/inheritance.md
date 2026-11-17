# 类与面向对象：继承

## 基本语法

YXG 支持多继承。子类通过冒号指定父类，多个父类用逗号分隔：

    class Animal {
        string name;

        pub void speak() {
            print name, " makes a sound\n";
        }
    }

    class Dog : Animal {
        pub void bark() {
            print name, " barks: Woof!\n";
        }
    }

    int main() {
        Dog d = new Dog();
        d.name = "Buddy";
        d.speak();
        d.bark();
        return 0;
    }

输出：

    Buddy makes a sound
    Buddy barks: Woof!

子类继承父类的所有公开（pub）和受保护（pro）字段与方法。私有（pri）成员不参与继承，但通过自动生成的 getter/setter 仍可间接访问。

## 多继承

YXG 支持多继承，用逗号分隔多个父类：

    class Flyable {
        pub void fly() {
            print "Flying...\n";
        }
    }

    class Swimmable {
        pub void swim() {
            print "Swimming...\n";
        }
    }

    class Duck : Animal, Flyable, Swimmable {
        pub void quack() {
            print name, " quacks: Quack!\n";
        }
    }

    int main() {
        Duck d = new Duck();
        d.name = "Donald";
        d.speak();
        d.fly();
        d.swim();
        d.quack();
        return 0;
    }

输出：

    Donald makes a sound
    Flying...
    Swimming...
    Donald quacks: Quack!

多继承时如果多个父类有同名方法，直接调用会采用声明顺序中靠前的那个父类的版本，不会报错、也不会提示歧义；需要明确语义时，子类应自己定义同名方法来覆盖。

## 方法重写

子类可以重写父类的方法：

    class Animal {
        string name;

        pub void speak() {
            print name, " makes a sound\n";
        }
    }

    class Cat : Animal {
        pub void speak() {
            print name, " meows: Meow!\n";
        }
    }

    int main() {
        Cat c = new Cat();
        c.name = "Kitty";
        c.speak();
        return 0;
    }

输出：

    Kitty meows: Meow!

重写时方法签名（名称和参数类型）必须与父类一致。

## 虚函数与多态

**默认是静态绑定**：派生类里写同名方法，只是"藏着"了父类那份，通过父类引用调用仍然执行父类的版本。

要让调用**在运行时派发到实际类型**，父类方法加 `virtual`，子类方法加 `override`：

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

    pub void main() {
        Circle c = new Circle();
        c.r = 5.0;

        print "direct = ", c.area(), "\n";
        c.describe();                    // 走 Circle.area()
    }

输出：

    direct = 78.539749
    area = 78.539749

`describe()` 定义在 `Shape` 里，但因为它内部调的 `area()` 被声明为 `virtual`，实际执行的是 `Circle.area()`。

### 通过父类引用调用

多态的核心价值在这里——用父类类型持有子类实例，调用时派发到子类实现：

    pub class Shape {
        pub virtual float area() {
            return 0.0;
        }
    }

    pub class Circle : Shape {
        pub float r;

        pub override float area() {
            return 3.14159 * this.r * this.r;
        }
    }

    pub void main() {
        Circle c = new Circle();
        c.r = 5.0;

        Shape s = c;                     // 父类引用指向子类实例
        print "via base = ", s.area(), "\n";   // via base = 78.539749
    }

输出 `via base = 78.539749`，不是 0.0。

### 对比：不用 virtual

去掉 `virtual` 和 `override`，同样的代码行为不同：

    pub class Shape {
        pub float area() {               // 没有 virtual
            return 0.0;
        }
        pub void describe() {
            print "area = ", this.area(), "\n";
        }
    }

    pub class Circle : Shape {
        pub float r;
        pub float area() {               // 没有 override
            return 3.14159 * this.r * this.r;
        }
    }

此时 `c.describe()` 输出 `area = 0.0`，因为 `describe()` 里对 `area()` 的调用在编译期就绑定到了 `Shape.area()`。

### override 的约束

**子类写 `override`，父类必须有对应的 `virtual` 方法**，否则编译报错：

    pub class Base {
        pub void hello() { }             // 没有 virtual
    }

    pub class Sub : Base {
        pub override void hello() { }    // 错误
    }

报错：

    error[E3015]: override method 'hello' has no matching virtual method in parent class

### 小结

| 写法 | 行为 |
|---|---|
| 两边都不写 | 静态绑定，父类引用调用父类版本 |
| 父类 `virtual` + 子类 `override` | 动态派发，按实际类型执行 |
| 子类 `override` 但父类没 `virtual` | 编译报错 `E3015` |
| 重写时方法签名（名称 + 参数类型）必须一致 | 否则不构成重写 |


## 调用父类方法

YXG 没有 super 关键字，也不支持在子类里用父类名直接调用被重写的方法：父类名不能当作接收者，写 `Animal.speak()` 或 `super.speak()` 都编译不过（`Animal.speak()` 能通过编译但运行时输出 `Error: null pointer dereference`，`super` 则报 `error[E3001]: variable 'super' is not declared`）。

要在子类方法里执行父类的逻辑，可以把父类那份逻辑抽成一个独立方法，子类直接调用它：

    class Animal {
        string name;

        pub void makeSound() {
            print name, " makes a sound\n";
        }

        pub void speak() {
            makeSound();
        }
    }

    class Dog : Animal {
        pub void speak() {
            makeSound();
            print name, " barks: Woof!\n";
        }
    }

    int main() {
        Dog d = new Dog();
        d.name = "Buddy";
        d.speak();
        return 0;
    }

输出：

    Buddy makes a sound
    Buddy barks: Woof!

## 构造函数与继承

子类的构造函数需要自己负责初始化从父类继承来的字段（YXG 不会自动调用父类构造函数）：

    class Point {
        int x;
        int y;

        pub Point(int x, int y) {
            this.x = x;
            this.y = y;
        }
    }

    class Point3D : Point {
        int z;

        pub Point3D(int x, int y, int z) {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        pub void display() {
            print "Point3D(", x, ",", y, ",", z, ")\n";
        }
    }

    int main() {
        Point3D p = new Point3D(10, 20, 30);
        p.display();
        return 0;
    }

输出：

    Point3D(10,20,30)

子类构造函数里漏掉 `this.x = x;` 这类语句也不会报错，但继承来的字段会一直是默认值（int 为 0），容易被忽略。

## 访问控制与继承

三种访问级别在继承中的行为：

    class Base {
        pub int a;
        pro int b;
        pri int c;

        pub Base() {
            a = 0;
            b = 0;
            c = 0;
        }
    }

    class Derived : Base {
        pub void test() {
            a = 1;
            b = 2;
            // c = 3;          // 错误！pri 不可直接访问（报 E3014）
            setC(3);           // 正确：通过自动生成的 setter
            print a, " ", b, " ", getC(), "\n";
        }
    }

    int main() {
        Derived d = new Derived();
        d.test();
        d.a = 10;
        // d.b = 20;           // 错误（pro 对外部不可见，报 E3014 cannot access protected field）
        // d.c = 30;           // 错误（pri 对外部不可见）
        d.setC(30);            // 正确：通过 setter
        print d.a, " ", d.getC(), "\n";
        return 0;
    }

输出：

    1 2 3
    10 30

## 完整示例：图形类层次结构

    pub class Shape {
        pub string name;
        pub virtual float area() {
            return 0.0;
        }
        pub void describe() {
            print this.name, " area:", this.area(), "\n";
        }
    }

    pub class Circle : Shape {
        pub float radius;
        pub override float area() {
            return 3.14159 * this.radius * this.radius;
        }
    }

    pub class Rectangle : Shape {
        pub float width;
        pub float height;
        pub override float area() {
            return this.width * this.height;
        }
    }

    pub class Square : Rectangle {
        pub override float area() {
            return this.width * this.width;
        }
    }

    pub void main() {
        Circle c = new Circle();
        c.name = "Circle";
        c.radius = 5.0;

        Rectangle r = new Rectangle();
        r.name = "Rectangle";
        r.width = 4.0;
        r.height = 6.0;

        Square sq = new Square();
        sq.name = "Square";
        sq.width = 3.0;

        c.describe();
        r.describe();
        sq.describe();
    }

输出：

    Circle area:78.539749
    Rectangle area:24.0
    Square area:9.0

`describe()` 定义在父类 `Shape` 里，但 `area()` 声明为 `virtual`、子类用 `override` 重写，所以运行时按实际类型派发。

### 如果去掉 virtual / override

把 `Shape.area()` 前的 `virtual` 和子类的 `override` 都去掉，同样的代码输出会变成：

    Circle area:0.0
    Rectangle area:0.0
    Square area:0.0

因为 `describe()` 里对 `area()` 的调用在编译期固定绑定到了 `Shape.area()`。这就是 `virtual` 的作用。


## 继承与字段布局

子类实例在内存中包含父类的所有字段。字段按继承顺序排列：

    class A { int x; }
    class B : A { int y; }
    class C : B { int z; }

    int main() {
        C c = new C();
        c.x = 1;
        c.y = 2;
        c.z = 3;
        print c.x, c.y, c.z, "\n";
        return 0;
    }

输出：

    123

多继承时，字段按父类声明顺序依次排列。

## 常见错误

### 访问私有字段

    class Base {
        pri int secret;

        pub Base() {
            secret = 0;
        }
    }

    class Derived : Base {
        pub void test() {
            // secret = 42;    // 错误！pri 不可直接访问（报 E3014）
            setSecret(42);     // 正确：通过自动生成的 setter
        }
    }

### 方法签名不匹配

    class Base {
        pub void greet(string name) { }
    }

    class Derived : Base {
        // pub void greet(int x) { }  // 这不是重写，是新方法
    }

重写时参数类型必须完全一致。

### 循环继承

    // class A : B { }
    // class B : A { }          // 错误！循环继承

编译器会检测循环继承并报错。另外父类必须先定义后使用：子类写在父类之前会报 `error[E3016]: parent class 'A' must be defined before 'B'`。

### 忘记 new

    class Base {
        pub int a;
    }

    class Derived : Base {
        pub void test() {
            a = 1;
        }
    }

    int main() {
        Derived d;          // d 是 null
        d.test();           // 运行时输出：Error: null pointer dereference
        return 0;
    }

和普通类一样，派生类实例也必须用 new 创建：

    int main() {
        Derived d = new Derived();
        d.test();
        print d.a, "\n";
        return 0;
    }

## 继续阅读

掌握了继承后，下一章将学习枚举类型。
