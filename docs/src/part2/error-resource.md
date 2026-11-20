# 错误处理与资源管理（2.6）

2.6 解决两个生产环境的关键问题：

1. **错误处理不统一** —— 各模块失败约定五花八门，调用方必然漏判
2. **句柄泄漏无防护** —— 没有析构函数、没有 `finally`，忘了 `close()` 就泄漏

---

## 一、统一错误状态：`yxg.Error`

### 问题

2.5 之前，每个模块用自己的方式报错：

| 模块 | 失败表现 |
|---|---|
| `Path` | 返回 `""` |
| `Regex.find` | 返回 `-1` |
| `Socket` | 有 `lastError()` |
| `Database` | 有 `errmsg()` |
| `Https` | 有 `lastError()` |
| `Crypto` | 无反馈 |

**五套约定**。写生产代码时每调一个函数都要查文档确认它怎么报错。

### 2.6 的做法：一个地方查

```yxg
import yxg.Error;
import yxg.String;

pub void main() {
    // 所有模块失败都会记录到这里
    if SysError.has() {
        print "失败: ", SysError.describe(), "\n";
    }
}
```

### API

| 方法 | 说明 |
|---|---|
| `SysError.set(module, code, msg)` | 记录一次失败（模块内部用） |
| `SysError.clear()` | 清除 |
| `SysError.has()` | 是否有错误 |
| `SysError.ok()` | 是否无错误 |
| `SysError.code()` | 错误码 |
| `SysError.module()` | 出错模块名 |
| `SysError.message()` | 错误说明 |
| `SysError.describe()` | `"模块: 说明 (代码 N)"` |
| `SysError.count()` | 累计发生次数（监控用） |
| `SysError.check()` | 有错就抛 `SysException` |
| `SysError.checkWith(ctx)` | 抛异常并带前缀 |

**断言式辅助**（只记录，不抛）：

| 方法 | 说明 |
|---|---|
| `require(cond, module, code, msg)` | 条件不成立时记录 |
| `requireNonEmpty(s, module, what)` | 字符串非空 |
| `requireOk(rc, module, code, msg)` | `rc >= 0` |

### 两种使用风格

**风格 A：检查返回值（适合可恢复错误）**

```yxg
Regex.find("[0-9]+", text);
if SysError.has() {
    print "无匹配: ", SysError.describe(), "\n";
    SysError.clear();
}
```

**风格 B：直接抛异常（适合致命错误）**

```yxg
import yxg.Error;

pub void mustSucceed() {
    doSomething();
    SysError.checkWith("初始化失败");
}

pub void main() {
    try {
        mustSucceed();
    } catch (ERROR e) {
        print "致命: ", e.info, "\n";
    }
}
```

> ⚠️ 读取捕获到的异常用 **`e.info`**。
> 内置的 `e.getInfo()` 目前在异常路径上有缺陷（会崩），用字段访问代替。

### 实测

```
1 初始 has = false  want false
2 has      = true  want true
3 describe = Test: 模拟失败 (代码 1001)
4 clear 后 = false  want false
5 捕获     = 初始化 -> Db: 连接断开 (代码 3)
```

### 输出到 stderr：`print_err`

YXG 内置 `print_err(...)`，输出到 **stderr**（与 `print` 分开）：

```yxg
print "正常输出\n";
print_err("错误: ", 42, "\n");
```

`SysError` 上封装了四个便捷方法：

| 方法 | 说明 |
|---|---|
| `SysError.printToStderr()` | 打到 stderr（保留错误状态） |
| `SysError.printAndClear()` | 打到 stderr 并清除 |
| `SysError.warn(tag)` | 带自定义标签 |
| `SysError.report()` | 有错就打 stderr 并返回 `false` |

```yxg
SysError.set("Db", 5, "连接超时");
SysError.warn("警告");
// stderr: [警告] Db: 连接超时 (代码 5)

bool ok = SysError.report();   // stderr: [错误] ... ；返回 false
```

> ⚠️ 方法不能命名为 `print`（与内置语句冲突），所以叫 `printToStderr`。

### 向后兼容

**旧的哨兵返回值全部保留**（`""`、`-1`、`false`），2.6 只是在失败时**额外记录**到 `SysError`。已有代码不用改。

---

## 二、句柄泄漏防护：`yxg.Resource`

### 问题

YXG 没有析构函数、没有 `finally`、没有 `defer`。所以：

```yxg
File f = new File("a.txt", "r");
if someCondition { return; }   // ← 忘了 f.close()，句柄泄漏
```

泄漏不会报错，也不会崩溃，只是慢慢耗尽句柄——典型的生产环境隐性故障。

### 2.6 的做法：自动登记 + 主动检测

`File` / `Socket` / `Database` 现在**自动登记句柄**：

```yxg
import yxg.File;
import yxg.Resource;

pub void main() {
    File f = new File("a.txt", "r");   // 自动 track
    print Resource.leakCount();        // 1
    f.close();                          // 自动 untrack
    print Resource.leakCount();        // 0

    Resource.leakCheck();               // 程序结束前查一次
}
```

### API

| 方法 | 说明 |
|---|---|
| `track(kind, handle)` | 登记（模块内部用） |
| `untrack(handle)` | 注销（模块内部用） |
| `leakCount()` | 未释放数量 |
| `hasLeak()` | 是否有泄漏 |
| `report()` | 一句话摘要 |
| `dump()` | 逐条列出 |
| `leakCheck()` | 有泄漏时打印并通过，返回 `false` |
| `releaseAll()` | 用注册的关闭函数批量释放 |
| `registerCloser(kind, fn)` | 各模块注册关闭函数 |
| `enable(on)` / `enabled()` | 开关追踪（**有性能开销**，可在热路径关掉） |

### kind 约定

| 值 | 类型 |
|---|---|
| 1 | `File` |
| 2 | `Socket` |
| 3 | `Database` |
| 4 | `Https` |

### 实测

```
6 初始泄漏 = 0  want 0
7 打开后   = 1  want 1
8 关闭后   = 0  want 0
9 泄漏     = 1  want 1
10 检测    = [资源警告] 资源泄漏: 1 个句柄未释放
  [File] 句柄 268
false  want false
11 清理后  = 0  want 0
```

### 生产用法建议

```yxg
pub void main() {
    runApplication();
    // 退出前统一检查
    if !Resource.leakCheck() {
        // 可选：强制清理
        Resource.releaseAll();
    }
}
```

**注意**：`releaseAll()` 依赖各模块注册的关闭函数。目前 `File`、`Socket`、`Database` 都支持，`Https` 需要在模块里调一次 `Resource.registerCloser(4, ...)`。

---

## 三、Regex 花括号量词（2.6）

2.5 的正则不支持 `{n,m}`，输入校验场景基本没法用。2.6 补上了：

```yxg
import yxg.Regex;

pub void main() {
    // 固定长度
    Regex.matches("[0-9]{3}", "123");            // true
    Regex.matches("[0-9]{3}", "1234");           // false

    // 范围
    Regex.matches("^[0-9]{2,4}$", "123");        // true
    Regex.matches("^[0-9]{2,4}$", "12345");      // false

    // 下限
    Regex.matches("a{2,}", "aaa");               // true

    // 实用场景
    Regex.matches("^1[3-9][0-9]{9}$", "13812345678");              // 手机号
    Regex.matches("^[0-9]{4}-[0-9]{2}-[0-9]{2}$", "2026-11-20");   // 日期

    // 捕获组配合
    Regex.group("([0-9]{4})-([0-9]{2})", "2026-11", 2);            // "11"
}
```

| 语法 | 含义 |
|---|---|
| `{n}` | 恰好 n 次 |
| `{n,}` | 至少 n 次 |
| `{n,m}` | n 到 m 次 |

---

## 四、FFI 参数上限提升（2.6）

2.5 最多 4 个参数，`MultiByteToWideChar`（6 个）、`CreateWindowEx`（12 个）都调不了。

2.6 扩到 **8 个**，并支持 `string` 混合：

| 方法 | 参数 |
|---|---|
| `callCdecl5` ~ `callCdecl8` | 5~8 个 int |
| `callStdcall5` ~ `callStdcall8` | 5~8 个 int |
| `callStdcall6s(addr, a1, a2, str3, a4, a5, a6)` | 第 3 个是 string |
| `callStdcall6ss(addr, a1, a2, str3, a4, str5, a6)` | 第 3、5 是 string |
| `callStdcall8ss(addr, a1, a2, str3, a4, str5, a6, a7, a8)` | 第 3、5 是 string |

### 实测：6 参数真实 API

```yxg
import yxg.Library;

pub void main() {
    Library k32 = Library.open("kernel32.dll");
    int fn = k32.sym("MultiByteToWideChar");

    // MultiByteToWideChar(65001, 0, "ABC", 3, NULL, 0) -> 需要 3 个宽字符
    print k32.callStdcall6s(fn, 65001, 0, "ABC", 3, 0, 0), "\n";   // 3

    // UTF-8 "你好" 是 6 字节 -> 2 个宽字符
    print k32.callStdcall6s(fn, 65001, 0, "\u4f60\u597d", 6, 0, 0), "\n";  // 2

    k32.close();
}
```

结果 `3` 和 `2` 都正确，说明 6 个参数按顺序完整传递。

### 注意事项

- 如果某个参数是 `string`，必须用带 `s` 的变体，否则类型检查会报「no matching method」
- 参数超过 8 个的 API（如 `CreateWindowEx` 12 个）仍需手写汇编

---

## 五、Regex 非贪婪量词（2.6）

在任意量词后加 `?` 即为非贪婪（最短匹配）：

| 语法 | 含义 |
|---|---|
| `*?` | 0 次或多次，尽量少 |
| `+?` | 1 次或多次，尽量少 |
| `??` | 0 或 1 次，优先 0 |
| `{n,m}?` | n 到 m 次，尽量少 |

```yxg
import yxg.Regex;

pub void main() {
    Regex.firstMatch("a.*b",  "axxbyyb");   // "axxbyyb"（贪婪）
    Regex.firstMatch("a.*?b", "axxbyyb");   // "axxb"    （非贪婪）

    Regex.firstMatch("<.*?>", "<x>");       // "<x>"
    Regex.firstMatch("a{2,4}?", "aaaa");    // "aa"
    Regex.firstMatch("a+?", "aaa");         // "a"
}
```

**分组内的惰性也支持**：

```yxg
string s = "a<title>Ex</title>b<title>Yy</title>";

Regex.firstMatch("<title>(.*?)</title>", s);
// "<title>Ex</title>"              ← 停在第一个 </title>

Regex.firstMatch("<title>(.*)</title>", s);
// "<title>Ex</title>b<title>Yy</title>"   ← 贪婪吃到最后
```

实测 10 项全过（见 `repro/StdRegexLazy.yxg`）。

> 实现说明：分组内的惰性需要「整段必须匹配」的判断，
> 而惰性匹配只返回最短终点，所以内部会把内层模式去掉惰性标记后再验证覆盖范围。

---

## 六、`toString` 保留名诊断（2.6）

编译器把**任何** `.toString()` 重写成数组转字符串（`parser.c` 里硬编码），
所以用户定义的同名方法永远不可达。现在定义时会报警告：

```
ts.yxg:4:16: warning[E3003]: toString is a reserved name; the compiler rewrites
.toString() into array-to-string conversion, so this method is unreachable.
Rename it (e.g. asText).
```

改用 `asText` / `toText` 等名字。

---

## 七、FFI 参数上限 → 12（2.6）

| 方法 | 参数 |
|---|---|
| `callCdecl5` ~ `callCdecl12` | 5~12 个 int |
| `callStdcall5` ~ `callStdcall12` | 5~12 个 int |
| `callStdcall6s` | 第 3 个是 string |
| `callStdcall6ss` | 第 3、5 是 string |
| `callStdcall8ss` | 第 3、5 是 string |

5~8 参用真实 API 验证过（`MultiByteToWideChar` 返回正确的字符数）。
9~12 参用同一模板生成，但没有参数匹配的简单 Win32 API 做功能验证。

---

## 八、还没解决的

| 问题 | 说明 |
|---|---|
| `e.getInfo()` 崩溃 | 异常路径的编译器缺陷，用 `e.info` 代替 |
| 无析构函数 | `Resource` 提供检测，仍需手动 `close()`（计划 3.0 实现） |
| FFI 超过 12 参数 | 需手写汇编 |


---

---

## 九、`format()` 参与 `+` 拼接（2.6 修复）

2.5 时这个写法会**静默丢值**：

```yxg
string a = "x" + format("{}", 2);   // 旧版本得到 "x"，值丢了
```

### 根因

`_format_string`（`runtime_system.asm`）返回的是**指向静态环形缓冲的裸指针**，
而 YXG 的 `string` 是**胖指针**（数据指针 + 前 4 字节存长度）。
拼接时按 `string` 读长度，读到的是缓冲区里的垃圾，于是整段被丢弃。

而且这个 bug **与前序调用有关**：连续调用 `format` 会轮转缓冲槽，
表现时好时坏，比稳定出错更难排查。

### 修法

在 `_format_string` 的 `.finish_fs` 里按 ABI 重新包装返回值：

```asm
.finish_fs:
    mov  byte [edi], 0
    mov  eax, [ebp-20]
    mov  ecx, edi
    sub  ecx, eax          ; 已写入长度
    mov  ebx, ecx
    lea  eax, [ebx + 8]
    add  eax, 3
    and  eax, -4
    push eax
    call _gc_alloc         ; 分配 gc 字符串
    add  esp, 4
    mov  [eax], ebx        ; 长度头
    lea  edi, [eax + 4]
    ...                     ; 拷贝 + 返回数据指针
```

同时在编译器里让 `has_format` 隐含 `has_gc`（`Codegen_win32.c`），
否则用到 `format` 但不用 GC 的程序会链接失败。

### 实测

```
1 先赋值     = [v5]
2 右侧       = [x2]           ✓  "x" + format(...)
3 左侧       = [2x]           ✓  format(...) + "x"
4 两侧       = [12]           ✓  format + format
5 夹中间     = [pre3-4post]   ✓  "pre" + format(...) + "post"
6 有前序     = [y8]           ✓  先调用过一次 format
7 格式串保留 = [9]            ✓
```

见 `repro/StdFormatConcat.yxg`。
---

## 十、Regex 断言与反向引用（2.6 补齐）

至此 `Regex` 支持的语法已经覆盖日常使用的绝大部分场景。

### 断言（零宽，不消耗字符）

| 语法 | 含义 |
|---|---|
| `(?=X)` | **前向**：后面必须是 X |
| `(?!X)` | 前向否定：后面不能是 X |
| `(?<=X)` | **后向**：前面必须以 X 结尾 |
| `(?<!X)` | 后向否定：前面不能以 X 结尾 |

```yxg
import yxg.Regex;

pub void main() {
    // 数字后面必须跟「元」
    Regex.firstMatch("[0-9]+(?=元)", "100元");     // "100"
    Regex.firstMatch("[0-9]+(?=元)", "100个");     // ""（不匹配）

    // 数字后面不能是「元」
    Regex.firstMatch("[0-9]+(?!元)", "100个");     // "100"

    // 零宽：断言不吃字符
    Regex.matches("^(?=a)abc", "abc");             // true
    Regex.matches("^(?=b)abc", "abc");             // false
}
```

### 反向引用 `\1` ~ `\9`

匹配「跟第 N 个分组匹配到的内容完全相同」的部分：

```yxg
Regex.firstMatch("(ca)\\1", "caca");                        // "caca"
Regex.firstMatch("(c)\\1", "cc");                           // "cc"
Regex.firstMatch("([a-z]+) \\1", "the cat cat sat");        // "cat cat"（找重复词）
Regex.matches("^([a-z]+) \\1$", "cat cat");                 // true
Regex.matches("^([a-z]+) \\1$", "cat dog");                 // false
```

> YXG 字符串里 `\\1` 写两个反斜杠，正则引擎收到的是 `\1`。

### 实测

```
=== 断言 ===
1 前瞻        = [100]    want 100
2 否定前瞻    = [100]    want 100
3 开头断言    = true     want true
4 断言失败    = false    want false
=== 反向引用 ===
5 (ca)\1/caca = [caca]   want caca
6 (c)\1/cc    = [cc]     want cc
7 重复词      = [cat cat] want cat cat
8 matches     = true     want true
9 不匹配      = false    want false
=== 原有功能不退化 ===
10 花括号     = true
11 非贪婪     = [axxb]
12 分组惰性   = [<t>A</t>]
13 捕获组     = [34]     want 34
14 或         = true
```

见 `repro/StdRegexAssert.yxg`。

### 实现要点

**断言**：在 `matchSub` 里识别 `(?=` / `(?!`，对内部子模式做一次匹配试探，
不消耗位置，直接从头继续；否定前瞻则把结果取反。

**反向引用**：需要捕获状态，为此加了三件事：

1. 模块级数组 `g_capStart[10]` / `g_capEnd[10]` 记录每个分组的起止
2. `groupThen` 在**调外层匹配之前**先写入捕获（否则 `\1` 读不到）
3. `findFrom` 入口清空捕获

第 2 点是关键——最初写成「外层匹配成功后再记录」，
导致 `\1` 永远读到未捕获状态（`-1`），整个反向引用失效。

### 现在 Regex 的能力

| 支持 | |
|---|---|
| 字面量 `.` | `^ $` |
| `*` `+` `?` | `{n}` `{n,}` `{n,m}` |
| `*?` `+?` `??` `{n,m}?` | 非贪婪 |
| `[]` `[^]` `[a-z]` | 字符类 |
| `\d \w \s \D \W \S` | 预定义类 |
| `()` `(?:)` `\|` | 分组与或 |
| `(?=)` `(?!)` | 断言 |
| `\1` ~ `\9` | 反向引用 |

### 后向断言

```yxg
// 「元」后面的数字
Regex.firstMatch("(?<=元)[0-9]+", "100元200");      // "200"
Regex.firstMatch("(?<=元)[0-9]+", "100个200");      // ""

// 否定：前面不是「元」的数字
Regex.firstMatch("(?<!元)[0-9]+", "100个");         // "100"

// 提取 $ 后面的价格
Regex.firstMatch("(?<=\\$)[0-9]+", "price $99");    // "99"

// find 定位
Regex.find("(?<=a)b", "ab");                       // 1
Regex.firstMatch("(?<=a)b", "ab");                 // "b"
```

实测：

```
=== 后向断言 ===
15 (?<=元)[0-9]+ = [200]  want 200
16 不匹配         = []
17 (?<!元)[0-9]+  = [100]
18 find (?<=a)b   = 1     want 1
19 (?<=\$)[0-9]+  = [99]  want 99
```

> 注意：`matches("(?<=a)b", "ab")` 返回 `false` 是**正确行为**——
> `matches` 要求整个字符串匹配，从位置 0 开始时前面还没有 `a`。
> 要用 `find` / `firstMatch`，或写成 `^.*(?<=a)b$`。

### 现在 Regex 支持的完整语法

| 类别 | 语法 |
|---|---|
| 字面量 | `abc` |
| 通配 | `.` |
| 锚点 | `^` `$` |
| 量词 | `*` `+` `?` `{n}` `{n,}` `{n,m}` |
| 非贪婪 | `*?` `+?` `??` `{n,m}?` |
| 字符类 | `[abc]` `[a-z]` `[^0-9]` |
| 预定义类 | `\d` `\w` `\s` `\D` `\W` `\S` |
| 分组 | `(abc)` 捕获、`(?:abc)` 非捕获 |
| 或 | `a\|b` |
| 前向断言 | `(?=X)` `(?!X)` |
| 后向断言 | `(?<=X)` `(?<!X)` |
| 反向引用 | `\1` ~ `\9` |

**未实现**：原子组 `(?>X)`、条件分组 `(?(1)X\|Y)`、命名分组 `(?<name>X)`。
这些在实际使用中极少遇到。
---

## 十一、静态 string 字段与 catch 传参（2.6 修复）

这一节记录两个相互纠缠的深层缺陷，它们都表现为「值对了但长度是垃圾」或「直接崩溃」。

### 一、类 `static string` 字段

```yxg
pub class Cfg {
    pub static string name = "hello";
}
```

**修前**：`String.len(Cfg.name)` 返回 **10**（而不是 5），访问未初始化字段直接 `0xC0000005`。

**根因**：`string` 是 **4 字节的 GC 指针，长度存在 `ptr-4`**（不是 8 字节胖指针——
那是 `str` 类型）。编译器给静态字段生成的初始化字面量是裸 `db "hello",0`，
前面没有长度头，于是 `[ptr-4]` 读到的是前一个常量的尾巴。

对照 `get_type_size`：

```c
case DATA_STRING: return 4;     /* string：gc 指针，长度在 ptr-4 */
case DATA_STR:    return 8;     /* str：{ptr, len} 胖指针 */
```

**修法**（`Codegen_win32.c` 数据段）：在字面量**之前**紧跟一个长度 dword：

```asm
Cfg_name_len dd 5
Cfg_name_lit db `hello`,0
Cfg_name     dd Cfg_name_lit     ; string 字段本身只占 4 字节
```

这样 `Cfg_name_lit - 4` 正好是长度，`[ptr-4] == 5` 成立。
`_empty_str` 也补上了配套的 `_empty_str_len dd 0`。

**实测**：

```
1 name  = [hello] len=5
2 empty = [] len=0
3 short = [xy] len=2
4 hex   = 68656c6c6f
5 num   = 7
6 flag  = true
7 after = [changed] len=7
```

### 二、`catch` 块里把异常对象作参数传递

**修前**：`catch (ERROR e)` 里 `e.info` 读得对，但 `readVia(e)` 或 `e.getInfo()` 崩溃。

**根因**：`IR.c` 的 `catch` 用 `IR_COPY_BLOCK` 把**对象体（16 字节）**拷进 catch 变量槽，
而 catch 变量本该存**对象指针**。后果是：

- `e.info` 走 `[[槽]+offset]`，偏移恰好对上，**看起来正常**
- 把 `e` 当参数传出去时，传的是对象首字段的值（被当成指针），**解引用即崩**

```c
/* 修前 */
IR* copy = create_IR(IR_COPY_BLOCK);
copy->args1 = ...dst; copy->args2 = ...exc_t; copy->copy_bytes = total_bytes;

/* 修后：只存指针 */
IR* store = create_IR(IR_VAR_ASS);
store->result = strdup(tn->catches[i].internal_name);
store->args1  = strdup(exc_t);
```

**实测**：

```
8  catch info = [boom]
9  getInfo    = [boom]
10 传参       = [boom]
```

### 三、顺带修掉的方法返回类型

`IR.c` 里成员方法调用的返回类型写成了 `member_type`（恒为 `DATA_INT`）：

```c
/* 修前 */
lit->data_type = access->member_type;
/* 修后 */
lit->data_type = (access->member_return_type != DATA_VOID
                  && access->member_return_type != DATA_ERROR)
                 ? access->member_return_type : access->member_type;
```

这是 `e.getInfo()` 三层缺陷里的第一层。见 `repro/StdStaticString.yxg`（10 项断言）。
---

## 十二、FFI 通用调用器：任意参数个数（2.6）

定长包装 `callCdecl0..16` / `callStdcall0..16` 覆盖绝大多数 Win32 API，
但个别接口参数更多。为此提供通用调用器：把参数放进数组，传地址和个数。

```yxg
import yxg.Library;

pub void main() {
    Library lib = Library.open("user32.dll");
    int addr = lib.sym("MessageBoxA");

    int[6] args;
    args[0] = 0;          // hWnd
    args[1] = "hi";       // 注意：string 参数需自行转成指针，见下
    args[2] = "title";    // 
    args[3] = 0;          // uType
    // 只用 int 参数的 API 才是安全的直接用法

    // 任意个数：args[0] 是第一个参数
    int r = lib.callNStdcall(addr, &args[0], 4);
    lib.close();
}
```

### 签名

| 方法 | 说明 |
|---|---|
| `callNStdcall(int addr, int argptr, int count)` | stdcall，被调用者清栈 |
| `callNCdecl(int addr, int argptr, int count)` | cdecl，运行时清栈 |

`argptr` 用 `&args[0]` 传入；`args[0]` 是**第一个**参数（入栈顺序自动倒排）。

### 实测

```
1 stdcallN 13 = 91   want 91
2 stdcallN 16 = 136  want 136
3 cdeclN  13 = 91   want 91
4 cdeclN  16 = 136  want 136
```

用测试 DLL（13/16 参数的加权校验和函数）验证，参数值与顺序都正确。
见 `repro/StdFfiN.yxg`。

### 注意

- 参数全是 4 字节整型才能这么传。`int64`/`double`/结构体要按 8 字节拆或用定长版本。
- `string` 参数需要先转成 C 指针，通用调用器只接受整型数组。
- 参数个数必须和目标函数的真实参数个数一致，否则栈失衡直接崩溃。