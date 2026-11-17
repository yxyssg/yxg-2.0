# 实战六：用泛型容器做数据聚合

## 目标

用泛型容器组织数据，做一个简单的学生分组统计：

- `List<int>` 存一组成绩
- `Map<string, int>` 做分组聚合
- 泛型容器的遍历与统计

练习点：

- `List<T>` 的增删查改
- `Map<K,V>` 的键值操作
- 容器的遍历方式
- 泛型类作为函数参数和返回值

## List 基本用法

```yxg
import yxg.Collections.List;

pub void main() {
    List<int> nums = new List<int>();
    nums.add(30);
    nums.add(10);
    nums.add(20);

    print "size = ", nums.size(), "\n";
    print "get(0) = ", nums.get(0), "\n";
    print "indexOf(20) = ", nums.indexOf(20), "\n";
}
```

输出：

```
size = 3
get(0) = 30
indexOf(20) = 2
```

### 常用方法

| 方法 | 说明 |
|---|---|
| `add(T)` | 尾部追加 |
| `insert(i, T)` | 插入到位置 i |
| `get(i)` | 读第 i 个 |
| `set(i, T)` | 写第 i 个 |
| `removeAt(i)` | 删除第 i 个并返回 |
| `removeLast()` | 删除最后一个并返回 |
| `contains(T)` | 是否包含 |
| `indexOf(T)` | 查找下标，找不到返回 -1 |
| `size()` | 元素个数 |
| `isEmpty()` | 是否为空 |
| `clear()` | 清空 |

## 遍历 List

`List` 不支持 `foreach`，用 `while` + `size()`：

```yxg
import yxg.Collections.List;

pub void main() {
    List<int> nums = new List<int>();
    nums.add(30);
    nums.add(10);
    nums.add(20);

    int sum = 0;
    int i = 0;
    while i < nums.size() {
        sum = sum + nums.get(i);
        i = i + 1;
    }
    print "总和 = ", sum, "\n";
    print "平均 = ", sum / nums.size(), "\n";
}
```

输出：

```
总和 = 60
平均 = 20
```

### 最大值和最小值

```yxg
import yxg.Collections.List;

pub int maxOf(List<int> nums) {
    int m = nums.get(0);
    int i = 1;
    while i < nums.size() {
        if nums.get(i) > m {
            m = nums.get(i);
        }
        i = i + 1;
    }
    return m;
}

pub int minOf(List<int> nums) {
    int m = nums.get(0);
    int i = 1;
    while i < nums.size() {
        if nums.get(i) < m {
            m = nums.get(i);
        }
        i = i + 1;
    }
    return m;
}
```

## Map 做分组

`Map<string, int>` 适合做「类别 → 计数」的聚合：

```yxg
import yxg.Collections.Map;

pub void main() {
    Map<string, int> count = new Map<string, int>();

    // 模拟统计各科人数
    count.put("math", 3);
    count.put("english", 2);
    count.put("physics", 4);

    print "科目数 = ", count.size(), "\n";
    print "math = ", count.get("math"), "\n";
    print "has history = ", count.has("history"), "\n";
}
```

输出：

```
科目数 = 3
math = 3
has history = false
```

### 累计计数

Map 最常见的用法是「累加」，但要先判断键是否存在：

```yxg
import yxg.Collections.Map;

pub void bump(Map<string, int> m, string key) {
    if m.has(key) {
        m.put(key, m.get(key) + 1);
    } else {
        m.put(key, 1);
    }
}
```

再配合 `getOr` 风格可以更短，但 `Map` 没有 `getOr`，所以需要显式判断。

## 组合示例：按首字母分组

```yxg
import yxg.Collections.List;
import yxg.Collections.Map;
import yxg.String;

pub void main() {
    List<string> words = new List<string>();
    words.add("apple");
    words.add("avocado");
    words.add("banana");
    words.add("blueberry");
    words.add("cherry");

    Map<string, int> group = new Map<string, int>();

    int i = 0;
    while i < words.size() {
        string w = words.get(i);
        int ch = String.charAt(w, 0);
        string key = Char.toLower(ch) as char + "";
        if group.has(key) {
            group.put(key, group.get(key) + 1);
        } else {
            group.put(key, 1);
        }
        i = i + 1;
    }

    print "a 组: ", group.get("a"), " 个\n";
    print "b 组: ", group.get("b"), " 个\n";
    print "c 组: ", group.get("c"), " 个\n";
}
```

### 把字符转成字符串当键

```yxg
int ch = String.charAt(w, 0);
string key = Char.toLower(ch) as char + "";
```

- `String.charAt(w, 0)` 返回首字母的 ASCII 码（如 `'a'` 是 97）
- `Char.toLower(ch)` 转小写，仍是 ASCII 码
- `as char` 转成字符
- `+ ""` 拼成字符串当 Map 的键

## 完整程序：成绩分组统计

```yxg
import yxg.Collections.List;
import yxg.Collections.Map;

pub class Student {
    pub string name;
    pub int score;

    pub Student(string n, int s) {
        this.name = n;
        this.score = s;
    }
}

pub string gradeOf(int score) {
    if score >= 90 {
        return "A";
    } else if score >= 80 {
        return "B";
    } else if score >= 70 {
        return "C";
    } else if score >= 60 {
        return "D";
    }
    return "F";
}

pub int countOf(Map<string, int> m, string key) {
    if !m.has(key) {
        return 0;
    }
    return m.get(key);
}

pub void main() {
    List<Student> students = new List<Student>();
    students.add(new Student("Alice", 92));
    students.add(new Student("Bob", 78));
    students.add(new Student("Carol", 85));
    students.add(new Student("Dave", 58));
    students.add(new Student("Eve", 95));

    print "=== 学生列表 ===\n";
    int total = 0;
    int i = 0;
    while i < students.size() {
        Student s = students.get(i);
        print s.name, ": ", s.score, " (", gradeOf(s.score), ")\n";
        total = total + s.score;
        i = i + 1;
    }

    print "\n=== 等级分布 ===\n";
    Map<string, int> dist = new Map<string, int>();
    i = 0;
    while i < students.size() {
        string g = gradeOf(students.get(i).score);
        if dist.has(g) {
            dist.put(g, dist.get(g) + 1);
        } else {
            dist.put(g, 1);
        }
        i = i + 1;
    }

    print "A: ", countOf(dist, "A"), " 人\n";
    print "B: ", countOf(dist, "B"), " 人\n";
    print "C: ", countOf(dist, "C"), " 人\n";
    print "D: ", countOf(dist, "D"), " 人\n";
    print "F: ", countOf(dist, "F"), " 人\n";

    print "\n=== 汇总 ===\n";
    print "人数: ", students.size(), "\n";
    print "总分: ", total, "\n";
    print "平均: ", total / students.size(), "\n";
}
```

**注意**：程序里用了 `countOf` 辅助函数而不是直接 `dist.get("D")`。为什么？看下一节。

### Map.get 遇到不存在的键会抛异常

这是 `Map` 的一个关键行为：

```yxg
import yxg.Collections.Map;

pub void main() {
    Map<string, int> m = new Map<string, int>();
    m.put("a", 1);
    print "has b = ", m.has("b"), "\n";      // false

    try {
        print m.get("b"), "\n";
    } catch (ERROR e) {
        print "get 不存在键 -> 异常: ", e.info, "\n";
    }
}
```

输出：

```
has b = false
get 不存在键 -> 异常: map key not found
```

所以**读 Map 前必须先 `has` 判断**，或者包一层辅助函数。上面例子里「D 级 0 人」就是因为没有 D 级的键，直接 `dist.get("D")` 会崩。

输出：

```
=== 学生列表 ===
Alice: 92 (A)
Bob: 78 (C)
Carol: 85 (B)
Dave: 58 (F)
Eve: 95 (A)

=== 等级分布 ===
A: 2 人
B: 1 人
C: 1 人
D: 0 人
F: 1 人

=== 汇总 ===
人数: 5
总分: 408
平均: 81
```

### 验算

| 学生 | 分数 | 等级 |
|---|---|---|
| Alice | 92 | A |
| Bob | 78 | C |
| Carol | 85 | B |
| Dave | 58 | F |
| Eve | 95 | A |

A 级 2 人（Alice、Eve），B 级 1 人（Carol），C 级 1 人（Bob），D 级 0 人，F 级 1 人（Dave）。

总分 92 + 78 + 85 + 58 + 95 = 408，平均 408 / 5 = 81。

## 知识点总结

| 知识点 | 用法 |
|---|---|
| List 声明 | `List<int> nums = new List<int>();` |
| 元素添加 | `nums.add(x)` |
| 元素读取 | `nums.get(i)` |
| 列表长度 | `nums.size()` |
| 列表遍历 | `while i < nums.size() { ... nums.get(i) ... }` |
| Map 声明 | `Map<string, int> m = new Map<string, int>();` |
| 键值写入 | `m.put(key, value)` |
| 键值读取 | `m.get(key)` |
| 键是否存在 | `m.has(key)` |
| 泛型元素类型 | `List<Student>` / `Map<string, int>` |
| 累计计数 | `has` 判断 + `get` 加 1 + `put` 写回 |

## 常见错误

### foreach 遍历 List

```yxg
for int n : nums { ... }       // ❌ List 不能 foreach
```

`foreach` 只支持数组。`List` 用 `while` + `get(i)`。

### Map 直接累加

```yxg
m.put(key, m.get(key) + 1);    // ❌ 键不存在时 get 返回不可预期值
```

必须先 `has` 判断。

### 忘记 import

```yxg
List<int> nums = ...;          // ❌ 没 import yxg.Collections.List
```

编译报错 `variable 'List' is not declared`。

### 用基本类型做 Map 的键以外的类型

`Map<string, int>` 里键必须是 `string`。如果想用 `int` 做键，声明 `Map<int, int>`——但注意 Map 的实现在类类型键上是按引用比较的，基本类型才安全。

## 继续阅读

下一篇综合运用多种特性，做一个完整的通讯录。
