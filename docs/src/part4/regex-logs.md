# 实战八：用 Regex 从日志里提取数据

正则表达式最容易的学法是**带着任务学**。这一篇用三个真实场景把常用语法过一遍。

## 场景一：从访问日志里统计状态码

假设日志长这样：

```
2026-11-20 10:00:01 GET /api/user 200 1234ms
2026-11-20 10:00:02 POST /api/order 500 89ms
2026-11-20 10:00:03 GET /index.html 404 12ms
```

想取出每行的状态码。

```yxg
import yxg.Regex;

pub void main() {
    string line = "2026-11-20 10:00:01 GET /api/user 200 1234ms";

    // 状态码是「三个数字，后面跟空格」
    string code = Regex.firstMatch("[0-9]{3}(?= )", line);
    print "状态码 = ", code, "\n";      // 200

    // 或者更精确：空格 + 三位数字 + 空格
    print "再看  = ", Regex.firstMatch(" [0-9]{3} ", line), "\n";
}
```

`(?= )` 是**前向断言**：要求后面是空格，但空格本身不被消耗。

## 场景二：提取路径参数

URL 是 `/api/user/12345/profile`，要取出 `12345`。

```yxg
import yxg.Regex;

pub void main() {
    string url = "/api/user/12345/profile";

    // 用分组：只关心第一个括号里的内容
    print "id = ", Regex.group("/api/user/([0-9]+)/", url, 1), "\n";   // 12345

    // 如果路径层级可能变化，用 [^/]+ 匹配「非斜杠」
    string u2 = "/v2/users/67890";
    print "id2 = ", Regex.group("/([0-9]+)$", u2, 1), "\n";            // 67890
}
```

`[^/]` 是**否定字符类**：任何不是 `/` 的字符。

## 场景三：找出所有数字

```yxg
import yxg.Regex;
import yxg.String;

pub void main() {
    string line = "GET /api/user 200 1234ms";

    // 没有「找全部」的直接接口，用循环 + findFrom
    int pos = 0;
    int n = String.len(line);
    while pos < n {
        int s = Regex.findFrom("[0-9]+", line, pos);
        if s < 0 { break; }
        string num = Regex.firstMatch("^[0-9]+", String.sub(line, s, n - s));
        print "数字: ", num, "\n";
        pos = s + String.len(num);
    }
}
```

输出（日期和时间里的数字也会被取出来）：

```
数字: 2026
数字: 11
数字: 20
数字: 10
数字: 00
数字: 01
数字: 200
数字: 1234
```

想只要状态码和耗时，把模式收紧：

```yxg
int s = Regex.findFrom("(?<= )[0-9]{3}(?= )|(?<= )[0-9]+(?=ms)", line, pos);
```

## 场景四：校验用户输入

```yxg
import yxg.Regex;

pub void main() {
    // 手机号：1 开头，第二位 3-9，共 11 位
    print Regex.matches("^1[3-9][0-9]{9}$", "13812345678"), "\n";   // true
    print Regex.matches("^1[3-9][0-9]{9}$", "1381234"), "\n";       // false

    // 邮箱（简化版）
    string re = "^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$";
    print Regex.matches(re, "user@example.com"), "\n";              // true
    print Regex.matches(re, "user@@example"), "\n";                 // false

    // 日期 YYYY-MM-DD
    print Regex.matches("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", "2026-11-20"), "\n";  // true
}
```

> `^` 和 `$` 是锚点，分别锁定开头和结尾。校验一定要加，否则
> `"abc13812345678xyz"` 也会被判为匹配。

## 场景五：替换敏感信息

```yxg
import yxg.Regex;

pub void main() {
    // 把每一段数字整体替换成固定文本
    string msg = "user=alice phone=13812345678 id=12345";
    print Regex.replaceAll("[0-9]+", msg, "#"), "\n";
    // → user=alice phone=# id=#

    // 只替换首次
    print Regex.replaceFirst("[0-9]+", msg, "#"), "\n";
    // → user=alice phone=# id=12345
}
```

> ⚠️ `replaceAll` / `replaceFirst` 的替换串是**字面量**，
> 不支持 `$1` `$2` 这类分组引用——写了会原样输出。
>
> 需要按分组重构内容时，手工拼：

```yxg
import yxg.Regex;
import yxg.String;

pub void main() {
    string msg = "phone=13812345678";
    // 取出中间四位，手工拼接
    string mid = Regex.group("[0-9]{11}", msg, 0);
    // 想按组处理就用 group 逐个取再拼
    print "原始 = ", mid, "\n";
}
```

## 场景六：非贪婪匹配（最常用的技巧）

```yxg
import yxg.Regex;

pub void main() {
    string html = "<title>A</title><title>B</title>";

    // 贪婪：吃掉最后一个 </title>
    print Regex.firstMatch("<title>.*</title>", html), "\n";
    // → <title>A</title><title>B</title>

    // 非贪婪：停在第一个 </title>
    print Regex.firstMatch("<title>.*?</title>", html), "\n";
    // → <title>A</title>

    // 配 group 时用贪婪版本（非贪婪配 group 有已知问题，见下）
    print Regex.group("<title>(.*)</title>", html, 1), "\n";
    // → A</title><title>B
}
```

> ⚠️ **已知限制**：`group()` 配非贪婪内层（`(.*?)`）目前返回 `""`。
> `firstMatch()` 的非贪婪是正常的。需要抓「第一段」时用：
>
> ```yxg
> string first = Regex.firstMatch("<title>(.*?)</title>", html);
> string inner = String.sub(first, 7, String.len(first) - 7 - 8);
> ```

`*?` `+?` `??` `{n,m}?` 都是非贪婪。**抓标签、抓引号内容、抓括号内容时几乎都要用。**

## 场景七：后向断言

```yxg
import yxg.Regex;

pub void main() {
    // 只取「元」后面的数字
    print Regex.firstMatch("(?<=元)[0-9]+", "100元200"), "\n";     // 200

    // 取 $ 后面的价格
    print Regex.firstMatch("(?<=\\$)[0-9]+", "price $99"), "\n";   // 99

    // 前面不能是「元」的数字
    print Regex.firstMatch("(?<!元)[0-9]+", "100个"), "\n";        // 100
}
```

## 场景八：找重复词（反向引用）

```yxg
import yxg.Regex;

pub void main() {
    // 连续重复的词（中间有空格）
    print Regex.firstMatch("([a-z]+) \\1", "the cat cat sat"), "\n";
    // → cat cat

    // 整串校验：两个词必须相同
    print Regex.matches("^([a-z]+) \\1$", "cat cat"), "\n";   // true
    print Regex.matches("^([a-z]+) \\1$", "cat dog"), "\n";   // false
}
```

`\1` 表示「和第 1 个分组匹配到的内容完全一样」。

> ⚠️ **已知限制**：分组的量词不会充分回溯。`^([a-z]+)\1$` 对 `"abcabc"`
> 会失败——`[a-z]+` 先吃掉整串 `abcabc`，之后不再回退去尝试 `abc`+`abc`。
> 中间有分隔符（空格、`-`、`/` 等）时正常工作，因为分隔符强制了分组边界。
> 这个限制对「找重复词」的常见用法没有影响。

> YXG 字符串里写 `\\1`（两个反斜杠），正则引擎收到的是 `\1`。

## 语法速查

| 类别 | 语法 | 说明 |
|---|---|---|
| 字面量 | `abc` | 就是这三个字符 |
| 通配 | `.` | 任意一个字符 |
| 锚点 | `^` `$` | 开头 / 结尾 |
| 量词 | `*` `+` `?` | 0+ / 1+ / 0或1 |
| 花括号 | `{n}` `{n,}` `{n,m}` | 精确次数 |
| 非贪婪 | `*?` `+?` `??` `{n,m}?` | 尽量少 |
| 字符类 | `[abc]` `[a-z]` `[^0-9]` | 集合 / 范围 / 否定 |
| 预定义 | `\d` `\w` `\s` `\D` `\W` `\S` | 数字/词/空白及其否定 |
| 分组 | `(abc)` `(?:abc)` | 捕获 / 非捕获 |
| 或 | `a\|b` | 二选一 |
| 前向断言 | `(?=X)` `(?!X)` | 后面是 / 不是 X（不消耗） |
| 后向断言 | `(?<=X)` `(?<!X)` | 前面是 / 不是 X（不消耗） |
| 反向引用 | `\1` ~ `\9` | 重复第 N 组的内容 |

**未支持**：命名分组 `(?<name>X)`、原子组 `(?>X)`、条件分组 `(?(1)X\|Y)`。
这些实际很少用到。

## API 速查

| 方法 | 返回 | 说明 |
|---|---|---|
| `matches(pattern, text)` | `bool` | 整串是否匹配 |
| `contains(pattern, text)` | `bool` | 是否包含 |
| `find(pattern, text)` | `int` | 首次匹配的起始位置，失败 -1 |
| `findFrom(pattern, text, from)` | `int` | 从 from 开始找 |
| `firstMatch(pattern, text)` | `string` | 首个匹配的整段文本 |
| `group(pattern, text, i)` | `string` | 第 i 个分组（0 = 整段） |
| `replaceFirst(pattern, text, rep)` | `string` | 替换首次 |
| `replaceAll(pattern, text, rep)` | `string` | 替换全部 |

找不到时 `firstMatch` / `group` 返回 `""`，`find` 返回 `-1`。

## 性能提醒

这是个**回溯引擎**，纯 YXG 实现，不是 C 的正则库。注意两点：

1. **不要在大文本上跑复杂正则**。几千字节还行，几 MB 会明显慢。
2. **避免嵌套量词**。`(a+)+b` 这类在长串上会指数爆炸。

需要处理大文本时，用 `String.indexOf` / `String.sub` 手写解析更稳。

## 已知限制汇总

| 限制 | 说明 | 绕法 |
|---|---|---|
| `group()` 配非贪婪内层返回 `""` | `Regex.group("<t>(.*?)</t>", s, 1)` 拿不到内容 | 用 `firstMatch()` 再截取 |
| 替换串不支持 `$1` | `replaceAll(p, s, "$1")` 原样输出 | 用 `group()` 取值后手工拼 |
| 分组量词不充分回溯 | `^([a-z]+)\1$` 对 `"abcabc"` 失败 | 加分隔符，或用 `{n}` 固定长度 |
| 命名分组 / 原子组 | `(?<n>X)` `(?>X)` 未实现 | 用编号分组 |
| 回溯性能 | 纯 YXG 实现，大文本慢 | 大文本用手写解析 |

## 继续阅读

- [../part2/stdlib-modules-2.5.md](../part2/stdlib-modules-2.5.md) —— 标准库模块
- [text-analysis.md](text-analysis.md) —— 文本分析工具
- [../part2/strings.md](../part2/strings.md) —— 字符串操作