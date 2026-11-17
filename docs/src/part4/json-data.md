# 实战四：JSON 数据处理

## 目标

用 `yxg.Json` 处理结构化数据：

- 构造 JSON 值和数组
- 序列化成字符串
- 解析 JSON 字符串
- 嵌套对象与数组的遍历

练习点：

- `JsonValue` 的构造与读取方法
- 类型判断（`isInt` / `isString` / `isArray` 等）
- `Json.parse` / `Json.stringify`
- 递归/嵌套数据结构的处理

## JsonValue 是什么

`JsonValue` 是一个可以装任意 JSON 值的对象：null、bool、int、float、string、array、object。

它不是静态类，需要构造和操作实例：

```yxg
import yxg.Json;

pub void main() {
    JsonValue v = Json.makeInt(42);
    print "值: ", v.asInt(), "\n";
    print "JSON: ", Json.stringify(v), "\n";
}
```

输出：

```
值: 42
JSON: 42
```

## 第一步：构造各种类型

```yxg
import yxg.Json;

pub void main() {
    JsonValue i = Json.makeInt(100);
    JsonValue f = Json.makeFloat(3.14);
    JsonValue s = Json.makeString("hello");
    JsonValue b = Json.makeBool(true);
    JsonValue n = Json.null_value();

    print Json.stringify(i), "\n";
    print Json.stringify(f), "\n";
    print Json.stringify(s), "\n";
    print Json.stringify(b), "\n";
    print Json.stringify(n), "\n";
}
```

输出：

```
100
3.14
"hello"
true
null
```

注意字符串序列化后带引号。

## 第二步：构造数组

用 `Json.makeArray()` 建空数组，`add()` 追加元素：

```yxg
import yxg.Json;

pub void main() {
    JsonValue arr = Json.makeArray();
    arr.add(Json.makeInt(10));
    arr.add(Json.makeInt(20));
    arr.add(Json.makeString("hello"));

    print "size = ", arr.size(), "\n";
    print "at(0) = ", arr.at(0).asInt(), "\n";
    print "at(1) = ", arr.at(1).asInt(), "\n";
    print "at(2) = ", arr.at(2).asString(), "\n";
    print "json = ", Json.stringify(arr), "\n";
}
```

输出：

```
size = 3
at(0) = 10
at(1) = 20
at(2) = hello
json = [10,20,"hello"]
```

### 方法速查

| 方法 | 说明 |
|---|---|
| `Json.makeArray()` | 建空数组 |
| `Json.makeObject()` | 建空对象 |
| `arr.add(v)` | 数组追加 |
| `arr.at(i)` | 数组取值 |
| `arr.size()` | 数组长度 |
| `arr.insertAt(i, v)` | 插入 |
| `arr.removeAt(i)` | 删除 |
| `obj.put(key, v)` | 对象写字段 |
| `obj.get(key)` | 对象读字段 |
| `obj.has(key)` | 字段是否存在 |
| `obj.remove(key)` | 删字段 |
| `obj.getOr(key, def)` | 读字段带默认值 |

## 第三步：构造嵌套结构

```yxg
import yxg.Json;

pub void main() {
    JsonValue user = Json.makeObject();
    user.put("name", Json.makeString("Alice"));
    user.put("age", Json.makeInt(25));
    user.put("active", Json.makeBool(true));

    JsonValue tags = Json.makeArray();
    tags.add(Json.makeString("admin"));
    tags.add(Json.makeString("dev"));
    user.put("tags", tags);

    print Json.stringify(user), "\n";

    print "name = ", user.get("name").asString(), "\n";
    print "age = ", user.get("age").asInt(), "\n";
    print "tags[0] = ", user.get("tags").at(0).asString(), "\n";
    print "tags count = ", user.get("tags").size(), "\n";
}
```

输出：

```
{"name":"Alice","age":25,"active":true,"tags":["admin","dev"]}
name = Alice
age = 25
tags[0] = admin
tags count = 2
```

`user.get("tags")` 返回的是 `JsonValue`，可以直接继续 `.at(0)` 链式访问。

## 第四步：解析 JSON 字符串

```yxg
import yxg.Json;

pub void main() {
    JsonValue v = Json.parse("{\"name\": \"Bob\", \"age\": 30}");
    print "name = ", v.get("name").asString(), "\n";
    print "age = ", v.get("age").asInt(), "\n";
}
```

输出：

```
name = Bob
age = 30
```

注意源码里字符串内的引号要转义成 `\"`。

解析数组：

```yxg
JsonValue arr = Json.parse("[1, 2, 3]");
print "size = ", arr.size(), "\n";
int i = 0;
while i < arr.size() {
    print arr.at(i).asInt(), "\n";
    i = i + 1;
}
```

## 第五步：类型判断

解析未知数据时，先判断类型再读：

```yxg
import yxg.Json;

pub void describe(JsonValue v) {
    if v.isNull() {
        print "null\n";
    } else if v.isBool() {
        print "bool: ", v.asBool(), "\n";
    } else if v.isInt() {
        print "int: ", v.asInt(), "\n";
    } else if v.isFloat() {
        print "float: ", v.asFloat(), "\n";
    } else if v.isString() {
        print "string: ", v.asString(), "\n";
    } else if v.isArray() {
        print "array with ", v.size(), " items\n";
    } else if v.isObject() {
        print "object with ", v.size(), " keys\n";
    }
}

pub void main() {
    describe(Json.null_value());
    describe(Json.makeBool(false));
    describe(Json.makeInt(7));
    describe(Json.makeFloat(2.5));
    describe(Json.makeString("hi"));

    JsonValue a = Json.makeArray();
    a.add(Json.makeInt(1));
    describe(a);

    JsonValue o = Json.makeObject();
    o.put("k", Json.makeInt(1));
    describe(o);
}
```

输出：

```
null
bool: false
int: 7
float: 2.5
string: hi
array with 1 items
object with 1 keys
```

类型判断方法：`isNull()`、`isBool()`、`isInt()`、`isFloat()`、`isString()`、`isArray()`、`isObject()`、`isNumber()`。

## 完整程序

一个简易的「学生列表」处理器：

```yxg
import yxg.Json;

pub JsonValue makeStudent(string name, int score) {
    JsonValue s = Json.makeObject();
    s.put("name", Json.makeString(name));
    s.put("score", Json.makeInt(score));
    return s;
}

pub void main() {
    JsonValue students = Json.makeArray();
    students.add(makeStudent("Alice", 92));
    students.add(makeStudent("Bob", 78));
    students.add(makeStudent("Carol", 85));

    print "=== 原始 JSON ===\n";
    print Json.stringify(students), "\n\n";

    print "=== 遍历 ===\n";
    int total = 0;
    int i = 0;
    while i < students.size() {
        JsonValue s = students.at(i);
        print s.get("name").asString(), ": ", s.get("score").asInt(), "\n";
        total = total + s.get("score").asInt();
        i = i + 1;
    }

    print "\n=== 统计 ===\n";
    print "人数: ", students.size(), "\n";
    print "总分: ", total, "\n";
    print "平均: ", total / students.size(), "\n";
}
```

输出：

```
=== 原始 JSON ===
[{"name":"Alice","score":92},{"name":"Bob","score":78},{"name":"Carol","score":85}]

=== 遍历 ===
Alice: 92
Bob: 78
Carol: 85

=== 统计 ===
人数: 3
总分: 255
平均: 85
```

### 为什么用函数生成对象

```yxg
pub JsonValue makeStudent(string name, int score) {
    JsonValue s = Json.makeObject();
    s.put("name", Json.makeString(name));
    s.put("score", Json.makeInt(score));
    return s;
}
```

不用写三遍重复的 `put`。这是把「构造一段固定结构」封装成函数的典型用法。

## 知识点总结

| 知识点 | 用法 |
|---|---|
| 构造标量 | `Json.makeInt(42)` / `makeString("x")` / `makeBool(true)` |
| 构造数组 | `Json.makeArray()` + `add()` |
| 构造对象 | `Json.makeObject()` + `put(key, v)` |
| 读取 | `asInt()` / `asString()` / `asFloat()` / `asBool()` |
| 数组访问 | `at(i)` / `size()` / `add(v)` |
| 对象访问 | `get(key)` / `has(key)` / `put(key, v)` |
| 序列化 | `Json.stringify(v)` |
| 解析 | `Json.parse(text)` |
| 类型判断 | `isInt()` / `isString()` / `isArray()` / `isObject()` |
| 链式访问 | `v.get("tags").at(0).asString()` |

## 常见错误

### 把 JsonValue 当静态类

```yxg
Json.makeInt(42);              // 这是对的，返回 JsonValue
JsonValue v = Json.makeInt(42);
print v.asInt();               // 实例方法
```

`Json.makeXxx()` 是静态构造，`v.asXxx()` 是实例读取，注意区分。

### 用 `push` 而不是 `add`

```yxg
arr.push(Json.makeInt(1));     // ❌ E3015: no matching method 'push'
arr.add(Json.makeInt(1));      // ✅
```

### 字符串里的引号没转义

```yxg
Json.parse("{"name": "Bob"}");        // ❌ 语法错误
Json.parse("{\"name\": \"Bob\"}");    // ✅
```

### 读错类型

```yxg
JsonValue v = Json.parse("\"hello\"");
print v.asInt();      // 不是整数，结果不可预期
```

先用 `isString()` / `isInt()` 判断。

## 继续阅读

下一篇做一个带异常处理的命令行工具。
