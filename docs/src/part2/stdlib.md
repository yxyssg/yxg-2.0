# 标准库

## 本章内容

这一章介绍日常编码最常用的一组库：字符串、数学、字符、容器、JSON、类型转换、并发。

图形、多媒体、网络、文件、数据库等模块请查阅[第五部分：图形与多媒体库](../part5/graphics.md)和[网络与文件库](../part5/network-file.md)。

## 总览

| 分组 | 模块 | 用途 |
|---|---|---|
| 基础 | `String` `Math` `Char` | 字符串、数学、字符判断 |
| 容器 | `Collections.List` `Collections.Map` `Collections.Set` `Collections.Queue` `Collections.Stack` | 泛型容器 |
| 数据 | `Json` `JsonExt` `Utils` | JSON 处理、编码/哈希、类型转换 |
| 并发 | `Thread` `Sync` `Function` | 线程、互斥量/事件、函数对象 |

> `String`、`Math`、`Char`、`JsonExt`、`Utils` 里的方法都是 `static`，用 `类名.方法()` 调用，**不要** `new`。
> 容器（`List`、`Map` 等）有实例状态，**必须** `new`。

## 字符串 String

导入：`import yxg.String;`

全部是 `static` 方法，第一个参数传字符串：

```yxg
import yxg.String;

pub void main() {
    str s = "Hello, YXG";
    print "len = ", String.len(s), "\n";              // 11
    print "sub = ", String.sub(s, 0, 5), "\n";         // Hello
    print "upper = ", String.toUpperStr(s), "\n";      // HELLO, YXG
    print "index = ", String.indexOfStr(s, "YXG"), "\n"; // 7
}
```

| 方法 | 说明 |
|---|---|
| `len(str s)` | 长度 |
| `isEmpty(str s)` | 是否为空 |
| `charAt(str s, int i)` | 第 i 个字符的 ASCII 码 |
| `first(s)` / `last(s)` | 首/尾字符的 ASCII 码 |
| `sub(str s, int start, int n)` | 子串（**第三参数是长度，不是结束下标**） |
| `left(s, n)` / `right(s, n)` | 左/右 n 个字符 |
| `startsWith(s, p)` / `endsWith(s, q)` | 前缀/后缀判断 |
| `indexOf(s, ch)` / `lastIndexOf(s, ch)` | 按字符查找 |
| `indexOfStr(s, sub)` | 查找子串，找不到返回 -1 |
| `contains(s, ch)` / `containsStr(s, sub)` | 是否包含 |
| `toUpperStr(s)` / `toLowerStr(s)` | 转大写/小写（返回 `string`） |
| `trim(s)` / `trimL(s)` / `trimR(s)` | 去空白 |
| `isSpace(ch)` | 是否空白字符 |
| `repeat(s, count)` | 重复 |
| `padL(s, total, ch)` / `padR(s, total, ch)` | 左右填充 |
| `replace(s, old, newStr)` | 替换 |
| `count(s, ch)` / `countStr(s, sub)` | 统计出现次数 |
| `split(s, sep)` | 按分隔符切分成 `string[]` |

**注意**：`str` 是保留字，变量名不能用 `str`。

### 字符串拼接用 +

拼接字符串直接用 `+`，不要用 `_str_concat`（那是原地 strcat）：

```yxg
string s = "Hello";
string r = s + ", YXG!";
print r, "\n";       // Hello, YXG!
```

`string + char`、`char + string`、`string + string` 都支持。

### 字符串转整数

语言没有内置的字符串转整数，用 `Convert.parseInt`（见 [Utils](#类型转换-utils)）或手写：

```yxg
import yxg.String;

pub int stringToInt(string s) {
    int result = 0;
    int sign = 1;
    int i = 0;
    int n = String.len(s);
    if n > 0 {
        if String.charAt(s, 0) == 45 {   // '-'
            sign = -1;
            i = 1;
        }
    }
    while i < n {
        int c = String.charAt(s, i);
        if c >= 48 && c <= 57 {
            result = result * 10 + (c - 48);
        }
        i = i + 1;
    }
    return result * sign;
}

pub void main() {
    print stringToInt("-123"), "\n";     // -123
}
```

## 数学 Math

导入：`import yxg.Math;`

全部是 `static`：

```yxg
import yxg.Math;

pub void main() {
    print "abs = ", Math.abs(-5), "\n";            // 5
    print "sqrt = ", Math.sqrt(16.0), "\n";        // 4.0
    print "pow = ", Math.pow(2, 10), "\n";         // 1024
    print "max = ", Math.max(3, 7), "\n";          // 7
    print "PI = ", Math.PI, "\n";                  // 3.141593
    print "rand = ", Math.randint(1, 100), "\n";   // 随机整数
}
```

| 方法 | 说明 |
|---|---|
| `abs(int)` / `fabs(float)` | 绝对值 |
| `min(a,b)` / `max(a,b)` | 整数最值 |
| `fmin(a,b)` / `fmax(a,b)` | 浮点最值 |
| `clamp(v, lo, hi)` | 夹到区间 |
| `pow(int, int)` | 幂 |
| `sqrt(float)` | 平方根 |
| `sin` / `cos` / `tan` | 三角函数 |
| `factorial(n)` | 阶乘 |
| `gcd(a,b)` / `lcm(a,b)` | 最大公约 / 最小公倍 |
| `isPrime(n)` | 是否质数 |
| `srand(seed)` | 设置随机种子 |
| `random()` | 随机整数 |
| `randint(lo, hi)` | 区间随机整数 |
| `randfloat()` | 随机浮点 |
| `sign(x)` | 符号（-1/0/1） |

常量：`Math.PI`、`Math.E`、`Math.TAU`。

## 字符 Char

导入：`import yxg.Char;`

字符判断与转换。参数是 ASCII 码，**返回值也是 ASCII 码**：

```yxg
import yxg.Char;

pub void main() {
    print Char.isDigit('5'), "\n";      // true
    print Char.isAlpha('A'), "\n";      // true
    print Char.isSpace(' '), "\n";      // true
    print Char.toUpper('a'), "\n";      // 65（ASCII 码，不是 'A'）
    print Char.toUpper('a') as char, "\n";  // A（转成字符才显示字母）
}
```

| 方法 | 说明 |
|---|---|
| `isDigit(ch)` | 是否数字 |
| `isAlpha(ch)` | 是否字母 |
| `isAlphaNum(ch)` | 是否字母或数字 |
| `isUpper(ch)` / `isLower(ch)` | 大写 / 小写 |
| `isSpace(ch)` | 空白 |
| `isHexDigit(ch)` | 十六进制数字 |
| `toUpper(ch)` / `toLower(ch)` | 转大小写，**返回 int（ASCII 码）** |

## 容器

所有容器都是泛型类，**需要 `new`**。继承自 `Container`，共享 `size()` / `isEmpty()` / `clear()` / `destroy()`。

> 用类类型作为元素时，`contains`/`indexOf` 按**引用**比较。要按值比较，用基本类型（`int`、`string` 等）。

### List

```yxg
import yxg.Collections.List;

pub void main() {
    List<int> l = new List<int>();
    l.add(10);
    l.add(20);
    l.insert(1, 15);
    print "size = ", l.size(), "\n";             // 3
    print "get(1) = ", l.get(1), "\n";           // 15
    print "indexOf(20) = ", l.indexOf(20), "\n"; // 2
    l.set(0, 99);
    print "removeAt(0) = ", l.removeAt(0), "\n"; // 99
}
```

| 方法 | 说明 |
|---|---|
| `add(T)` / `insert(i, T)` | 追加 / 插入 |
| `get(i)` / `set(i, T)` | 读 / 写 |
| `removeAt(i)` / `removeLast()` | 删除 |
| `contains(T)` / `indexOf(T)` | 查找 |
| `reserve(n)` | 预留容量 |

### Map

```yxg
import yxg.Collections.Map;

pub void main() {
    Map<string, int> m = new Map<string, int>();
    m.put("a", 1);
    m.put("b", 2);
    print "has(a) = ", m.has("a"), "\n";     // true
    print "get(b) = ", m.get("b"), "\n";     // 2
    m.remove("a");
    print "size = ", m.size(), "\n";         // 1
}
```

| 方法 | 说明 |
|---|---|
| `put(K, V)` / `get(K)` | 写入 / 读取 |
| `has(K)` / `remove(K)` | 存在 / 删除 |
| `destroy()` | 释放 |

### Set

```yxg
import yxg.Collections.Set;

pub void main() {
    Set<int> s = new Set<int>();
    s.add(5);
    s.add(7);
    s.add(5);                    // 重复，不会加入
    print "size = ", s.size(), "\n";        // 2
    print "has5 = ", s.contains(5), "\n";   // true
    s.remove(5);
    print "size = ", s.size(), "\n";        // 1
}
```

### Queue（先进先出）

```yxg
import yxg.Collections.Queue;

pub void main() {
    Queue<int> q = new Queue<int>();
    q.enqueue(1);
    q.enqueue(2);
    print "front = ", q.peekFront(), "\n";   // 1
    print "deq = ", q.dequeue(), "\n";       // 1
    print "size = ", q.size(), "\n";         // 1
}
```

### Stack（后进先出）

```yxg
import yxg.Collections.Stack;

pub void main() {
    Stack<int> st = new Stack<int>();
    st.push(10);
    st.push(20);
    print "peek = ", st.peek(), "\n";        // 20
    print "pop = ", st.pop(), "\n";          // 20
    print "size = ", st.size(), "\n";        // 1
}
```

## JSON

导入：`import yxg.Json;`

JSON 解析与构造，值是 `JsonValue` 对象：

```yxg
import yxg.Json;

pub void main() {
    JsonValue v = Json.makeInt(42);
    print "asInt = ", v.asInt(), "\n";         // 42
    print "text = ", Json.stringify(v), "\n";  // 42

    JsonValue parsed = Json.parse("{\"a\": 1}");
    print "a = ", parsed.get("a").asInt(), "\n";  // 1
}
```

| 类别 | 方法 |
|---|---|
| 构造 | `makeInt`、`makeFloat`、`makeString`、`makeBool`、`makeNull`、`makeArray`、`makeObject` |
| 读取 | `asInt()`、`asFloat()`、`asString()`、`asBool()`、`isNull()` |
| 对象 | `get(key)`、`set(key, v)`、`has(key)`、`remove(key)` |
| 数组 | `at(i)`、`push(v)`、`size()` |
| 转换 | `parse(str)`、`stringify(v)` |

`JsonValue` 常和 `List`、`Map` 配合做泛型容器元素类型：

```yxg
import yxg.Collections.List;
import yxg.Json;

pub void main() {
    List<JsonValue> list = new List<JsonValue>();
    list.add(Json.makeInt(1));
    list.add(Json.makeString("hi"));
    print "size = ", list.size(), "\n";     // 2
}
```

## 编码与哈希 JsonExt

导入：`import yxg.JsonExt;`

全部 `static`：

```yxg
import yxg.JsonExt;

pub void main() {
    print "base64 = ", Base64URL.encode("hello"), "\n";
    print "sha256 = ", SHA256.hash("hello"), "\n";
}
```

| 类 | 方法 | 说明 |
|---|---|---|
| `Base64URL` | `encode(s)` / `decode(s)` | Base64 URL-safe 编解码 |
| `SHA256` | `hash(s)` / `hashBytes(s)` | SHA-256 摘要 |
| `HMAC` | — | HMAC 签名 |

## 类型转换 Utils

导入：`import yxg.Utils;`

`Convert` 类，全部 `static`：

```yxg
import yxg.Utils;

pub void main() {
    print Convert.formatInt(42), "\n";            // 42
    print Convert.formatFloat(3.14159, 2), "\n";  // 3.14
    print "parseInt = ", Convert.parseInt("123"), "\n";     // 123
    print "parseFloat = ", Convert.parseFloat("3.5"), "\n"; // 3.5
    print Convert.intToStr(99), "\n";             // 99
}
```

| 方法 | 说明 |
|---|---|
| `formatInt(n)` | 整数转字符串 |
| `formatFloat(f)` / `formatFloat(f, precision)` | 浮点转字符串 |
| `parseInt(s)` / `parseFloat(s)` | 字符串转数字 |
| `intToStr(n)` / `floatToStr(f)` | 同上 |
| `strToInt(s)` | 字符串转整数 |

## 线程 Thread

导入：`import yxg.Thread;`

```yxg
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
```

| 方法 | 说明 |
|---|---|
| `Thread(Function f)` | 构造，传函数对象 |
| `start()` | 启动，返回线程句柄 |
| `join()` | 等待结束 |
| `isRunning()` | 是否运行中 |
| `resultInt()` / `resultFloat()` / `resultI64()` / `result()` | 取返回值 |
| `Thread.self()` | 当前线程 ID |
| `Thread.sleep(ms)` | 线程休眠 |

详见[函数对象](function-objects.md)。

## 同步 Sync

导入：`import yxg.Sync;`

```yxg
import yxg.Sync;

pub void main() {
    Mutex m = new Mutex();
    print "valid = ", m.isValid(), "\n";
    m.lock();
    // 临界区
    m.unlock();
    m.destroy();
}
```

| 类 | 方法 | 说明 |
|---|---|---|
| `Mutex` | `lock()` / `unlock()` / `isValid()` / `destroy()` | 互斥锁 |
| `Event` | `set()` / `reset()` / `wait()` / `waitTimeout(ms)` | 事件通知 |

## 继续阅读

- 图形、多媒体：[第五部分：图形与多媒体库](../part5/graphics.md)
- 网络、文件：[网络与文件库](../part5/network-file.md)
