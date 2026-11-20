# Path 模块

`yxg.Path` 提供路径字符串的处理函数。全部是 `pub static`，不需要实例化。

```yxg
import yxg.Path;
import yxg.String;
```

## 方法一览

| 方法 | 返回 | 说明 |
|---|---|---|
| `fileName(path)` | `string` | 取文件名部分（含扩展名） |
| `dirName(path)` | `string` | 取目录部分 |
| `ext(path)` | `string` | 扩展名，**不带点**，没有则返回 `""` |
| `stem(path)` | `string` | 去掉扩展名的路径（**保留目录部分**） |
| `replaceExt(path, newExt)` | `string` | 替换扩展名（`newExt` 不带点） |
| `join(a, b)` | `string` | 拼路径，自动补分隔符 |
| `isAbsolute(path)` | `bool` | 是否绝对路径 |
| `hasDrive(path)` | `bool` | 是否带盘符（`C:`） |
| `drive(path)` | `string` | 盘符字符的字节值（`C:` 返回 67） |
| `canonical(path)` | `string` | 规范化：合并 `..`、`.`、统一分隔符 |
| `isSep(ch)` | `bool` | 该字符是否是路径分隔符 |
| `normalize(path)` | `string` | 把 `/` 统一成 `\` |

## 示例

```yxg
import yxg.Path;
import yxg.String;

pub void main() {
    string p = "C:\\work\\logs\\app-2026.txt";

    print Path.fileName(p), "\n";       // app-2026.txt
    print Path.dirName(p), "\n";        // C:\work\logs
    print Path.ext(p), "\n";            // txt
    print Path.stem(p), "\n";           // C:\work\logs\app-2026（保留目录）
    print Path.drive(p), "\n";          // 67（"C" 的 ASCII）

    print Path.isAbsolute(p), "\n";     // true
    print Path.hasDrive(p), "\n";       // true

    string j = Path.join("C:\\a", "b.txt");
    print j, "\n";                      // C:\a\b.txt

    // 规范化：合并 ..
    string c = Path.canonical("C:\\work\\..\\work\\logs\\app.txt");
    print c, "\n";                      // C:\work\logs\app.txt
}
```

## 错误处理

失败时返回空串 `""`，调用方需要自己判断：

```yxg
string name = Path.fileName(somePath);
if String.len(name) > 0 {
    print "文件名: ", name, "\n";
}
```

## 注意事项

- **Windows 风格**：默认处理 `\`，`normalize` 会把 `/` 转成 `\`
- `ext` 返回**不带点**的扩展名，`replaceExt` 的第二个参数也不带点
- 输入为空串时返回空串，不会崩溃

---

# Date 模块

`yxg.Date` 处理时间戳与日期换算。

```yxg
import yxg.Date;
```

## 时间源

```yxg
Date now = Date.now();          // 当前时间（UTC）
Date local = Date.nowLocal();   // 当前时间（本地，UTC+8）
int ts = Date.nowUnix();        // 当前 Unix 时间戳
```

## 构造与转换

| 方法 | 说明 |
|---|---|
| `Date.fromUnix(ts)` | 从 Unix 时间戳构造 |
| `d.toUnix()` | 转回 Unix 时间戳 |
| `d.year()` `d.month()` `d.day()` | 年月日 |
| `d.hour()` `d.minute()` `d.second()` | 时分秒 |
| `d.weekday()` | 星期，0=周日 |

## 格式化

| 方法 | 输出示例 |
|---|---|
| `d.asText()` | `2026-01-01 00:00:00` |
| `d.toDateString()` | `2026-01-01` |
| `d.toTimeString()` | `00:00:00` |
| `d.toIsoString()` | `2026-01-01T00:00:00` |
| `d.weekdayName()` | `周四` |

> ⚠️ 格式化方法叫 **`asText()`**，不是 `toString()`。
> `toString` 是编译器保留名，用户定义会被静默遮蔽（返回空串，不报错）。

## 日期计算

| 方法 | 说明 |
|---|---|
| `Date.isLeapYear(y)` | 是否闰年 |
| `Date.daysInMonth(y, m)` | 该月天数 |
| `d.addSeconds(n)` | 加 n 秒，返回新 Date |
| `d.addDays(n)` | 加 n 天，返回新 Date |
| `d.daysUntil(other)` | 相差天数 |

## 示例

```yxg
import yxg.Date;

pub void main() {
    Date d = Date.fromUnix(1000000000);
    print d.asText(), "\n";             // 2001-09-09 01:46:40
    print d.weekdayName(), "\n";        // 周日
    print d.toUnix(), "\n";             // 1000000000

    Date d2 = Date.fromUnix(1767225600);
    print d2.toIsoString(), "\n";       // 2026-01-01T00:00:00

    print Date.isLeapYear(2024), "\n";  // true
    print Date.daysInMonth(2024, 2), "\n"; // 29

    Date now = Date.nowLocal();
    print now.asText(), "\n";

    Date later = now.addDays(7);
    print later.asText(), "\n";
}
```

## 实现说明

- 时间源走 msvcrt 的 `time()`
- 公历换算用纯算术（Howard Hinnant 算法），不依赖系统时区数据库
- `nowLocal()` 固定按 UTC+8 偏移，不读取系统时区设置

---

# Crypto 模块

`yxg.Crypto` 提供哈希与编码。

```yxg
import yxg.Crypto;
```

## 十六进制

| 方法 | 说明 |
|---|---|
| `toHex(data)` | 字符串转十六进制 |
| `toHexByte(b)` | 单字节转两位十六进制 |
| `hexToText(hex)` | 十六进制转文本（不可打印字符用 `.`） |

```yxg
print Crypto.toHex("Hello"), "\n";    // 48656c6c6f
```

## Base64

| 方法 | 说明 |
|---|---|
| `base64Encode(data)` | 编码 |
| `base64Decode(b64)` | 解码，**返回十六进制形式** |
| `base64Value(ch)` | 单个 Base64 字符转 0..63 |

```yxg
string b = Crypto.base64Encode("Man");
print b, "\n";                        // TWFu

// 解码结果先拿到十六进制，再转文本
string hex = Crypto.base64Decode("SGVsbG8=");
print hex, "\n";                      // 48656c6c6f
print Crypto.hexToText(hex), "\n";    // Hello
```

> 为什么解码返回十六进制？因为 YXG 目前无法可靠地把字节码拼成字符串，
> 所以统一走「字节 → 十六进制 → 文本」这条路径。

## MD5

```yxg
print Crypto.md5("abc"), "\n";
// 900150983cd24fb0d6963f7d28e17f72
```

## SHA-256

```yxg
print Crypto.sha256("abc"), "\n";
// ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
```

## 已验证的测试向量

| 输入 | MD5 | SHA-256 |
|---|---|---|
| `""` | `d41d8cd98f00b204e9800998ecf8427e` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `"abc"` | `900150983cd24fb0d6963f7d28e17f72` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |
| `"The quick brown fox jumps over the lazy dog"` | `9e107d9d372bb6826bd81d3542a419d6` | — |

## 限制

- `md5` / `sha256` 输入上限 **4000 字节**，超出部分截断
- Base64 编码需输入是完整字节

---

# Encoding 模块

`yxg.Encoding` 处理字符编码转换。

```yxg
import yxg.Encoding;
```

## UTF-8 ↔ UTF-16

纯 YXG 实现，不依赖系统 API。

| 方法 | 说明 |
|---|---|
| `utf8ToUtf16(s)` | UTF-8 → UTF-16LE 字节串 |
| `utf16ToUtf8(s)` | UTF-16LE → UTF-8 |
| `utf8Length(s)` | UTF-8 码点个数（多字节算一个） |
| `utf16Length(s)` | UTF-16 码元个数 |
| `isAscii(s)` | 是否纯 ASCII |
| `codePointAt(s, i)` | 取第 i 个码点，越界返回 -1 |
| `fromCodePoint(cp)` | 码点 → UTF-8 字符串 |

```yxg
import yxg.Encoding;
import yxg.Crypto;

pub void main() {
    // "Hi" 的 UTF-16LE = 48 00 69 00
    string u = Encoding.utf8ToUtf16("Hi");
    print Crypto.toHex(u), "\n";          // 48006900

    // "你" = U+4F60，UTF-16LE = 60 4F
    string cjk = Encoding.utf8ToUtf16("\u4f60");
    print Crypto.toHex(cjk), "\n";        // 604f

    // 往返
    print Crypto.toHex(Encoding.utf16ToUtf8(cjk)), "\n";  // e4bda0

    print Encoding.utf8Length("\u4f60\u597d"), "\n";      // 2
    print Encoding.codePointAt("\u4f60", 0), "\n";        // 20320
    print Crypto.toHex(Encoding.fromCodePoint(20320)), "\n"; // e4bda0
}
```

## GBK ↔ UTF-8

走 Win32 `MultiByteToWideChar` / `WideCharToMultiByte`，实现在 `runtime_ffi.asm`。

| 方法 | 说明 |
|---|---|
| `gbkToUtf8(gbk)` | GBK(936) → UTF-8 |
| `utf8ToGbk(utf8)` | UTF-8 → GBK(936) |

```yxg
import yxg.Encoding;
import yxg.Crypto;

pub void main() {
    string u = "\u4f60\u597d";              // 你好
    string g = Encoding.utf8ToGbk(u);
    print Crypto.toHex(g), "\n";            // c4e3bac3

    string back = Encoding.gbkToUtf8(g);
    print Crypto.toHex(back), "\n";         // e4bda0e5a5bd
}
```

## 限制

- `gbkToUtf8` / `utf8ToGbk` 缓冲区上限 **8192 字节**
- 字符串在 YXG 里是字节序列，`utf16ToUtf8` 期待的是「两字节一个码元」的布局

---

# Regex 模块

`yxg.Regex` 是纯 YXG 实现的小型回溯正则引擎，不需要外部依赖。

```yxg
import yxg.Regex;
```

## 支持的语法

| 语法 | 说明 |
|---|---|
| `abc` | 字面量 |
| `.` | 任意单字节 |
| `*` `+` `?` | 量词（贪婪） |
| `*?` `+?` `??` | 惰性（非贪婪）量词 |
| `{n}` `{n,}` `{n,m}` `{n,m}?` | 花括号量词，可加 `?` 转惰性 |
| `[abc]` `[a-z]` `[^0-9]` `[\d\s]` | 字符类，支持区间与反集 |
| `^` `$` | 串首 / 串尾 |
| `\d` `\w` `\s` | 数字 / 单词字符 / 空白 |
| `\D` `\W` `\S` | 上述的反集 |
| `(abc)` | 捕获分组（`\1`..`\9` 可引用） |
| `(?:abc)` | 非捕获分组 |
| `a\|b` | 或 |
| `\1`..`\9` | 反向引用 |
| `(?=...)` `(?!...)` | 前瞻 / 否定前瞻 |
| `(?<=...)` `(?<!...)` | 后顾 / 否定后顾 |
| `(ab)+` `(a\|b){3}` | **分组后面也可以跟量词** |

## 接口

| 方法 | 返回 | 说明 |
|---|---|---|
| `matches(pat, text)` | `bool` | **整个串**是否匹配 |
| `contains(pat, text)` | `bool` | 是否包含匹配 |
| `find(pat, text)` | `int` | 第一个匹配起点，找不到返回 `-1` |
| `findFrom(pat, text, from)` | `int` | 从指定位置开始找 |
| `firstMatch(pat, text)` | `string` | 第一个匹配的子串 |
| `group(pat, text, n)` | `string` | 第 n 个捕获组（0 = 整个匹配，`\1` 对应 1） |
| `replaceFirst(pat, text, rep)` | `string` | 替换第一个 |
| `replaceAll(pat, text, rep)` | `string` | 替换全部 |

## 示例

```yxg
import yxg.Regex;

pub void main() {
    // 整体匹配
    print Regex.matches("abc", "abc"), "\n";            // true
    print Regex.matches("^abc$", "abc"), "\n";          // true

    // 量词
    print Regex.matches("ab*c", "abbbc"), "\n";         // true
    print Regex.matches("ab+c", "ac"), "\n";            // false
    print Regex.matches("[0-9]+", "12345"), "\n";       // true

    // 或
    print Regex.matches("cat|dog", "dog"), "\n";        // true

    // 分组
    print Regex.matches("(ab)+", "abab"), "\n";         // true
    print Regex.matches("(a|b)c", "bc"), "\n";          // true

    // 查找
    print Regex.find("b+", "aabbbcc"), "\n";            // 2
    print Regex.firstMatch("[0-9]+", "abc12345xyz"), "\n"; // 12345

    // 捕获组
    print Regex.group("([0-9]+)-([0-9]+)", "tel 123-456", 1), "\n"; // 123
    print Regex.group("([0-9]+)-([0-9]+)", "tel 123-456", 2), "\n"; // 456
}
```

### 惰性量词与分组

```yxg
string s = "<t>A</t><t>B</t>";

Regex.firstMatch("<t>(.*?)</t>", s);          // "<t>A</t>"
Regex.group("<t>(.*?)</t>", s, 1);            // "A"    ← 惰性：取最短
Regex.group("<t>(.*)</t>", s, 1);             // "A</t><t>B" ← 贪婪：取最长

Regex.matches("^<t>(.*?)</t>", s);            // true（惰性会为整个串放宽）
Regex.group("^((a|b)+)$", "abab", 2);         // "b"    ← 嵌套分组取最后一次迭代
```

### 反向引用与回溯

```yxg
Regex.matches("^([a-z]+)\\1$", "abcabc");     // true：会回溯去试 "abc"+"abc"
Regex.matches("^(a|ab)\\1$", "abab");         // true：或分支也会重新拆
Regex.matches("^([a-z]+) \\1$", "cat cat");   // true（带分隔符的重复词）
Regex.matches("^([a-z]+) \\1$", "abcabc");    // false：文本里没有空格
```

### 断言

```yxg
Regex.firstMatch("[0-9]+(?=元)", "100元");      // 100
Regex.firstMatch("(?<=元)[0-9]+", "100元200");  // 200
```

## 错误处理

| 情况 | 返回 |
|---|---|
| `matches` / `contains` 不匹配 | `false` |
| `find` 找不到 | `-1` |
| `firstMatch` / `group` 找不到（或该捕获组没参与匹配） | `""` |

## 实现与语义

引擎是「带目标终点的回溯」：每次递归都带着「本次必须停在哪」往下走，模式吃完时必须正好停在目标位置才算成功，否则回头换一种拆法。所以量词、或、分组里的选择都会被外层的需求逼着重试。

- `matches()` 的目标终点是文本长度（整串匹配），`find()` / `firstMatch()` 不限定终点（由贪婪性决定停在哪）。
- `group(n)` 直接读匹配过程中记下的捕获区间，不再事后反推，因此惰性、贪婪、嵌套分组都是准的。
- 反向引用的捕获在**最后一次迭代**更新，与主流引擎一致（`(a|b)+` 取最后一个匹配到的字符）。

## 限制

- **按字节匹配**：UTF-8 文本里一个汉字占 3 个位置，`.` 只匹配单个字节。处理中文时请用 `\d+` 之类锚定 ASCII 的模式，或者先在外部转码。
- **没有 `s` / `m` / `i` 等模式标志**：`.` 默认不匹配换行；需要忽略大小写时把字符类写全（如 `[a-zA-Z]`）或先 `String.toLowerStr`。
- **分组的终点按贪婪优先枚举**，不是「交替顺序优先」。例如 `(a|ab)(c|bcd)` 匹配 `"abcd"` 时取 `"abc"`（`ab`+`c`），PCRE 会取 `"abcd"`。两者都是合法匹配，`matches()` 结果一致，只有 `firstMatch()` 的具体切法不同。
- **回溯是递归实现的**，几 KB 的文本没问题；避免 `(a+)+b` 这类嵌套量词配长串，会指数级回溯。
- 捕获组只保留 **1..9**（`g_capStart` / `g_capEnd` 是长度 10 的数组），超过 9 个分组的部分无法用 `group()` 取。
