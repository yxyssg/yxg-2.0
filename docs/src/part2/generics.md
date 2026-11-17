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

```yxg
import yxg.Collections.List;
import yxg.Json;

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

### 泛型类继承泛型父类时，类型实参不能是基本类型

```yxg
pub class Mine<T> : List<T> {
    pub int count2() { return this.size(); }
}
```

- `Mine<JsonValue>` ✅ 正常
- `Mine<int>` ❌ 报 `error[E3015]: no matching method 'size'`

**绕法**：直接继承已实例化的父类（`class IntList : List<int> {}`），不要用「泛型类继承泛型父类 + 基本类型实参」的组合。

```yxg
import yxg.Collections.List;

pub class IntList : List<int> {
}

pub void main() {
    IntList l = new IntList();
    l.add(1);
    l.add(2);
    print "size = ", l.size(), "\n";     // 2
}
```

### 泛型不支持基本类型装箱以外的自动转换

`List<int>` 不能直接赋给 `List<long>`，也没有协变/逆变。需要转换时逐元素复制。

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
