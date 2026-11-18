# 泛型

## 为什么需要泛型

没有泛型时，要写一个「能装 int 的列表」和「能装 string 的列表」，只能写两份几乎一样的代码，或者用 `void*` 丢掉类型检查。

泛型让容器和算法**只写一次，适用多种类型**，而且类型检查仍然在编译期完成。

## 使用泛型容器

标准库的 `List`、`Map`、`Set`、`Queue`、`Stack` 都是泛型类：

```yxg
import yxg.Collections.List;
import yxg.Collections.Map;

pub void main() {
    List<int> nums = new List<int>();
    nums.add(10);
    nums.add(20);
    nums.add(30);

    print "size = ", nums.size(), "\n";          // 3
    print "get(1) = ", nums.get(1), "\n";        // 20
    print "indexOf(20) = ", nums.indexOf(20), "\n";  // 1

    Map<string, int> m = new Map<string, int>();
    m.put("a", 1);
    m.put("b", 2);

    print "has(a) = ", m.has("a"), "\n";         // true
    print "get(b) = ", m.get("b"), "\n";         // 2
}
```

尖括号里写具体的类型参数。`List<int>` 和 `List<string>` 是**两个不同的类型**，`nums.add("x")` 会直接编译报错。

### List 常用方法

| 方法 | 说明 |
|---|---|
| `add(T item)` | 尾部追加 |
| `get(int i)` | 按下标取 |
| `set(int i, T v)` | 按下标写 |
| `insert(int i, T v)` | 插入到指定位置 |
| `removeAt(int i)` | 删除指定位置并返回 |
| `removeLast()` | 删除最后一个并返回 |
| `contains(T v)` | 是否包含 |
| `indexOf(T v)` | 查找下标，找不到返回 -1 |
| `reserve(int n)` | 预留容量 |
| `size()` | 元素个数（继承自 `Container`） |
| `isEmpty()` | 是否为空（继承自 `Container`） |
| `clear()` | 清空（继承自 `Container`） |

### Map 常用方法

| 方法 | 说明 |
|---|---|
| `put(K key, V value)` | 写入键值 |
| `get(K key)` | 按键取值 |
| `has(K key)` | 键是否存在 |
| `remove(K key)` | 删除键 |
| `size()` | 键值对数量 |

## 嵌套泛型

类型参数本身也可以是泛型类型：

```yxg
import yxg.Collections.List;
import yxg.Json;

pub void main() {
    List<List<JsonValue>> matrix = new List<List<JsonValue>>();

    List<JsonValue> row1 = new List<JsonValue>();
    row1.add(Json.makeInt(1));

    List<JsonValue> row2 = new List<JsonValue>();
    row2.add(Json.makeInt(3));

    matrix.add(row1);
    matrix.add(row2);

    print "行数 = ", matrix.size(), "\n";                        // 2
    print "第1行第0个 = ", matrix.get(1).get(0).asInt(), "\n";   // 3
}
```

多层嵌套也支持，例如三层 `List`、`Map<string, List<X>>`：

```yxg
import yxg.Collections.List;
import yxg.Collections.Map;

pub void main() {
    Map<string, List<int>> groups = new Map<string, List<int>>();

    List<int> evens = new List<int>();
    evens.add(2);
    evens.add(4);
    groups.put("even", evens);

    print "even 组大小 = ", groups.get("even").size(), "\n";   // 2
}
```

> 写闭合尖括号时 `>>` 会被词法器当作右移运算符，这种情况编译器已经处理，`List<List<JsonValue>>` 可以直接写。
>


## 自定义泛型类

用 `class 类名<T>` 声明类型参数：

```yxg
pub class Box<T> {
    pro T value;

    pub Box(T v) {
        this.value = v;
    }

    pub T get() {
        return this.value;
    }

    pub void set(T v) {
        this.value = v;
    }
}

pub void main() {
    Box<int> b = new Box<int>(42);
    print "b.get() = ", b.get(), "\n";      // 42

    b.set(99);
    print "after set = ", b.get(), "\n";    // 99

    Box<string> s = new Box<string>("hi");
    print "s.get() = ", s.get(), "\n";      // hi
}
```

`Box<int>` 和 `Box<string>` 会各生成一份实例化代码，字段类型分别被替换成 `int` 和 `string`。

### 多个类型参数

```yxg
pub class Pair<A, B> {
    pro A first;
    pro B second;

    pub Pair(A a, B b) {
        this.first = a;
        this.second = b;
    }

    pub A getFirst() { return this.first; }
    pub B getSecond() { return this.second; }
}

pub void main() {
    Pair<int, string> p = new Pair<int, string>(1, "one");
    print p.getFirst(), " ", p.getSecond(), "\n";   // 1 one
}
```

## 泛型继承

可以让一个类继承**已实例化**的泛型父类：

```yxg
import yxg.Collections.List;
import yxg.Json;

pub class JsonList : List<JsonValue> {
    pub int total() {
        int s = 0;
        int i = 0;
        while i < this.size() {
            s = s + this.get(i).asInt();
            i = i + 1;
        }
        return s;
    }
}

pub void main() {
    JsonList l = new JsonList();
    l.add(Json.makeInt(3));
    l.add(Json.makeInt(4));
    print "size = ", l.size(), " total = ", l.total(), "\n";   // size = 2 total = 7
}
```

`JsonList` 继承了 `List<JsonValue>` 的全部方法（`add` / `get` / `size` …），还能加自己的方法。

多层嵌套的父类也可以：

```yxg
import yxg.Collections.List;
import yxg.Json;

pub class DeepList : List<List<JsonValue>> {
}

pub void main() {
    DeepList d = new DeepList();
    List<JsonValue> inner = new List<JsonValue>();
    inner.add(Json.makeInt(9));
    d.add(inner);
    print "deep = ", d.size(), " inner = ", d.get(0).size(), "\n";  // deep = 1 inner = 1
}
```

### 泛型类继承泛型父类

`class Mine<T> : List<T>` 这种写法，父类的类型实参可以是子类的类型参数，也可以是具体类型：

```yxg
import yxg.Collections.List;

pub class Mine<T> : List<T> {
    pub int count2() {
        return this.size();
    }
}

pub void main() {
    Mine<JsonValue> m = new Mine<JsonValue>();
    m.add(Json.makeInt(1));
    m.add(Json.makeInt(2));
    print "mine = ", m.count2(), "\n";      // 2
}
```

**基本类型实参也支持**：

```yxg
import yxg.Collections.List;

pub class Mine<T> : List<T> {
}

pub void main() {
    Mine<int> m = new Mine<int>();
    m.add(1);
    m.add(2);
    m.add(3);
    print m.size(), " ", m.get(1), "\n";     // 3 2
}
```

多个类型参数转发：

```yxg
import yxg.Collections.Map;

pub class MyMap<K, V> : Map<K, V> {
}

pub void main() {
    MyMap<string, int> m = new MyMap<string, int>();
    m.put("a", 1);
    print m.get("a"), "\n";                   // 1
}
```

### 构造器继承

子类不写构造器时，父类的构造器会被继承：

```yxg
pub class Box<T> {
    pro T v;
    pub Box(T x) { this.v = x; }
    pub T get() { return this.v; }
}

pub class Mine<T> : Box<T> {
}

pub void main() {
    Mine<int> m = new Mine<int>(5);      // 用 Box 的构造器
    print m.get(), "\n";                 // 5
}
```

子类写了自己的构造器就用子类的：

```yxg
pub class Mine<T> : Box<T> {
    pub Mine(T x) { this.v = x; }        // ← 但这行会报错，见下
}
```

## 当前的限制

### 泛型函数的语法

函数也支持泛型，但**类型参数写在返回类型前面**（不是函数名后面）：

```yxg
<T> T pick(T a, T b, bool useFirst) {
    if useFirst { return a; }
    return b;
}

pub void main() {
    print "int = ", pick(1, 2, false), "\n";        // int = 2
    print "str = ", pick("x", "y", true), "\n";     // str = x
}
```

调用时类型参数可以自动推断，也可以显式指定：

```yxg
print pick<int>(3, 4, true);      // 3
```

多个类型参数：

```yxg
<A, B> void show(A x, B y) {
    print x, " ", y, "\n";
}

pub void main() {
    show("hello", 99);      // hello 99
}
```

写成 `T pick<T>(...)`（类型参数跟在函数名后）是**错误语法**，会报 `expected 'Semicolon' before 'Less'`。

### 泛型类体访问父类字段

泛型子类里可以直接用父类字段：

```yxg
pub class Box<T> {
    pro T v;
    pub Box(T x) { this.v = x; }
    pub T get() { return this.v; }
}

pub class Layer<T> : Box<T> {
    pub Layer(T x) { this.v = x; }
}

pub void main() {
    Layer<int> l = new Layer<int>(5);
    print l.get(), "\n";      // 5
}
```

多层转发也可以：

```yxg
pub class Layer1<T> : Box<T> {
    pub Layer1(T x) { this.v = x; }
}

pub class Layer2<T> : Layer1<T> {
    pub Layer2(T x) { this.v = x; }
}

pub void main() {
    Layer2<float> l = new Layer2<float>(2.5);
    print l.get(), "\n";      // 2.5
}
```

注意：子类**不能重复声明同名字段**，会报 `error[E3002]: duplicate variable 'v'`。


### 泛型约束

类型参数后面可以加约束，限定实参必须是什么类的子类或父类。

**子类约束 `<T : Base>`** —— 实参必须是 `Base` 或 `Base` 的子类：

```yxg
pub class Animal { pub int id; }
pub class Dog : Animal { }
pub class Rock { pub int w; }

pub <T : Animal> int feed(T x) {
    return 1;
}

pub void main() {
    Dog d = new Dog();
    Animal a = new Animal();
    print feed(d), "\n";        // 1
    print feed(a), "\n";        // 1
}
```

传一个不相干的类型会报错：

```yxg
Rock r = new Rock();
print feed(r);
```

```
error[E3003]: type argument 'Rock' for 'T' violates constraint ':Animal'
```

**父类约束 `<T >: Base>`** —— `Base` 必须是实参的子类，也就是实参得是「父类型」：

```yxg
pub class Animal { pub int id; }
pub class Dog : Animal { }
pub class Puppy : Dog { }

pub <T >: Dog> int walk(T x) {
    return 2;
}

pub void main() {
    Animal a = new Animal();
    Dog d = new Dog();
    print walk(a), "\n";        // 2  Animal 是 Dog 的父类
    print walk(d), "\n";        // 2  Dog 本身可以
}
```

`Puppy` 是 `Dog` 的子类，不满足：

```yxg
Puppy p = new Puppy();
print walk(p);
```

```
error[E3003]: type argument 'Puppy' for 'T' violates constraint '>:Dog'
```

**泛型类也支持约束**：

```yxg
pub class Box<T : Animal> {
    pro T v;
    pub Box(T x) { this.v = x; }
}

pub void main() {
    Dog d = new Dog();
    Box<Dog> b = new Box<Dog>(d);       // ✅
    Rock r = new Rock();
    // Box<Rock> bad = new Box<Rock>(r); // ❌ violates constraint ':Animal'
}
```

约束可以写多条：`<T : Base1 : Base2>`。

### 协变与逆变

`Dog` 是 `Animal` 的子类时，`Box<Dog>` 和 `Box<Animal>` 之间能不能互相赋值？这由类型参数的**变型标注**决定。

| 标注 | 名称 | 允许的转换 |
|---|---|---|
| `<T>` | 不变（默认） | 只有实参完全相同才能赋值 |
| `<+T>` | 协变 | `Box<Dog>` → `Box<Animal>` |
| `<-T>` | 逆变 | `Box<Animal>` → `Box<Dog>` |

**不变（默认）**

```yxg
pub class Box<T> {
    pro T v;
    pub Box(T x) { this.v = x; }
}

Box<Dog> bd = new Box<Dog>(d);
Box<Animal> ba = bd;      // ❌ 不允许
```

```
error[E3004]: cannot convert 'Box$Dog' to 'Box$Animal': generic arguments are not compatible under variance
```

**协变 `<+T>` —— 只能产出，不能消费**

```yxg
pub class CoBox<+T> {
    pro T v;
    pub CoBox(T x) { this.v = x; }      // 构造函数豁免
    pub T get() { return this.v; }
}

Box<Dog> bd = ...;
CoBox<Animal> ca = bd;      // ✅ 子类容器可以当父类容器用
```

协变参数**不能出现在方法参数位置**：

```yxg
pub class CoBox<+T> {
    ...
    pub void set(T x) { this.v = x; }   // ❌
}
```

```
error[E3004]: covariant type parameter 'T' cannot be used as a parameter type of 'set'
                (a covariant parameter only produces values)
```

**逆变 `<-T>` —— 只能消费，不能产出**

```yxg
pub class Sink<-T> {
    pub int n;
    pub Sink() { this.n = 0; }
    pub void put(T x) { this.n = 1; }
}

Sink<Animal> sa = new Sink<Animal>();
Sink<Dog> sd = sa;          // ✅ 能处理 Animal 的，当然能处理 Dog
```

逆变参数**不能作为返回类型**：

```yxg
pub class Sink<-T> {
    ...
    pub T fetch() { ... }   // ❌
}
```

```
error[E3004]: contravariant type parameter 'T' cannot be the return type of 'fetch'
                (a contravariant parameter only consumes values)
```

**为什么方向是反的**

- 协变（`+`）：`Dog → Animal` 时，`Box<Dog> → Box<Animal>`，同向
- 逆变（`-`）：`Dog → Animal` 时，`Sink<Animal> → Sink<Dog>`，反向

逆变的意义：`Sink<Animal>` 能处理任何 `Animal`，自然也能处理 `Dog`，所以它是 `Sink<Dog>` 的子类型。

**构造函数豁免**

协变类的构造函数必须能接收 `T`，否则无法初始化。编译器对 `T 类名(T x)` 这种构造器不做变型检查。

### 标准容器是不变的

`List<T>`、`Map<K,V>` 等都是**不变**的，所以不能这么写：

```yxg
List<Dog> dogs = new List<Dog>();
List<Animal> zoo = dogs;      // ❌
```

这是好事——允许的话会出问题：

```yxg
List<Animal> zoo = dogs;
zoo.add(new Cat());           // Cat 混进 Dog 列表
Dog d = dogs.get(0);          // 按 Dog 读出来是错的
```

需要跨类型用时逐元素复制：

```yxg
List<Dog> dogs = ...;
List<Animal> zoo = new List<Animal>();
int i = 0;
while i < dogs.size() {
    zoo.add(dogs.get(i));
    i = i + 1;
}
```

### 变型检查覆盖的场景

变型判定在下面几处都会生效：

| 场景 | 示例 |
|---|---|
| 变量声明 | `Box<Animal> a = bd;` |
| 变量赋值 | `ba = bd;` |
| 函数传参 | `take(bd)` |
| 函数返回 | `return bd;` |
| 指针声明 | `unsafe Box<Animal>* p = &bd;` |

类名或类型实参里含下划线也判断正确（内部用类型实参结构比较，不靠名字拆分）：

```yxg
pub class My_Base { pub int id; }
pub class My_Derived : My_Base { }

pub class CoBox<+T> {
    pro T v;
    pub CoBox(T x) { this.v = x; }
    pub T get() { return this.v; }
}

My_Derived d = new My_Derived();
CoBox<My_Derived> cd = new CoBox<My_Derived>(d);
CoBox<My_Base> cb = cd;      // ✅ 协变，正确放行
```

## 小结

| 能力 | 支持 |
|---|---|
| `List<T>` / `Map<K,V>` 等标准容器 | ✅ |
| 嵌套泛型 `List<List<X>>` | ✅ |
| 自定义泛型类 `class Box<T>` | ✅ |
| 多类型参数 `class Pair<A,B>` | ✅ |
| 泛型函数 `<T> T f(T x)` | ✅ |
| 泛型函数显式类型参数 `f<int>(x)` | ✅ |
| 继承已实例化的泛型父类 `class X : List<Y>` | ✅ |
| 泛型类继承泛型父类（类类型实参） | ✅ |
| 泛型类继承泛型父类（基本类型实参） | ❌ |

## 继续阅读

下一章介绍异常处理。
