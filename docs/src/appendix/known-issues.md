# 附录 D：已知问题

这一版（2.6）在发布前实测过全部标准库与示例，以下是**确认存在但尚未修复**的问题。
每条都写了现象、绕法、以及是否影响日常使用。

## 影响较大的（2.6.1 已全部修复）

下面三条是 2.6 发布前实测确认的缺陷，**2.6.1 已修复**，留着是为了记录根因。

### 1. 跨模块继承的异常类，读 `e.info` 会崩 —— 已修复

**现象**

```yxg
import yxg.Error;

pub class MyErr : SysException {          // SysException 来自模块
    pub MyErr(string m) { this.info = m; }
}

pub void main() {
    try { throw new MyErr("boom"); }
    catch (ERROR e) {
        print e.getInfo(), "\n";          // 正常
        print e.info, "\n";               // 2.6：崩 0xC0000005
    }
}
```

**原因（两处叠加）**

1. `catch` 变量存的是**对象指针**（4 字节），但 IR 按类体大小（`ERROR` 是 12 字节）
   给它分配栈槽。代码生成于是把它当成聚合体，读成员时取的是**变量地址**而不是
   指针值 —— `e.info` 等于在栈上按野指针偏移取值，直接 0xC0000005。
   `e.getInfo()` 走的是普通方法调用，用的是真正的对象指针，所以没事。
2. 派生类构造函数隐式调用父类构造函数时，只传了 `this`，没有转发用户给的 `m`，
   父类构造拿到的 msg 就是 `this` 指针。若子类构造自己不重设字段，父类字段会被写坏：

   ```yxg
   pub class MyErr2 : SysException {
       pub MyErr2(string m) { }        // 自己不赋值
   }
   // 2.6：new MyErr2("cross").getInfo() → ""
   // 2.6.1：→ "cross"
   ```

**修复**

- `IR.c`（`AST_TRY`）：catch 变量的栈槽固定 4 字节（放指针），不再按类体大小分配。
- `Semantics.c`：隐式父类构造调用改为 `this` + 派生构造的形参逐个转发；
  形参不够时不再调用（避免写入垃圾），而不是塞 `this`。

### 2. `Regex.group()` 配非贪婪内层返回空串 —— 已修复

**现象**

```yxg
Regex.firstMatch("<t>(.*?)</t>", "<t>A</t><t>B</t>");  // "<t>A</t>"  ✓
Regex.group("<t>(.*?)</t>", "<t>A</t><t>B</t>", 1);   // 2.6：""  应该是 "A"
```

**原因**：`group()` 当时是「事后反推」——把分组内容当子模式，要求它**整段吃掉**一段文本。
但惰性模式（`.*?`）在引擎里只会返回**最短**匹配，这个"整段"判定永远不成立，
于是所有候选都被拒，返回 `""`。

**修复**：`Regex.yxg` 里 `group()` 改为直接读引擎在匹配过程中记下的捕获区间
（`g_capStart` / `g_capEnd`），不再反推。惰性/贪婪、嵌套分组、未参与的分组都对。

### 3. 反向引用时分组量词不充分回溯 —— 已修复

**现象**

```yxg
Regex.matches("^(a|ab)\\1$", "abab");        // 2.6：false  应该是 true
Regex.matches("^<t>(.*?)</t>", "<t>A</t><t>B</t>");  // 2.6：false  应该是 true
```

**原因**：`matchSub` 把「第一次匹配成功」直接当最终结果返回，外层需要继续吃字符时
没有回去要别的拆法。`[a-z]+` 一口气吃到串尾之后，就再也不会退回来试更短的组合。

**修复**：给 `matchSub` 加了一个「本次必须停在哪」参数（`must`，`< 0` 表示随便），
并在整个递归里往下传。模式吃完时只有正好停在目标位置才算成功，否则继续回溯。
`matches()` 传文本长度（整串匹配），`find()`/`firstMatch()` 传 `-1`。
顺带补上了分组后面的 `{n,m}` 量词（`(ab){2}`）。

**仍有的语义差别**：分组的终点按**贪婪优先**枚举，不是「交替顺序优先」。
所以 `(a|ab)(c|bcd)` 匹配 `"abcd"` 时取 `"abc"`（`ab`+`c`），PCRE 取 `"abcd"`。
两者都是合法匹配，`matches()` 结果一致，只有 `firstMatch()` 的具体切法不同。

## 影响较小的

### 4. 定长 FFI 调用按 4 字节计参

`callCdecl0`~`callCdecl16` 系列假设每个参数 4 字节。
`int64` / `double` 参数（8 字节）会算错栈大小。

**绕法**：拆成两个 `int` 传，或用通用调用器 `callNStdcall` / `callNCdecl`
（它们也按 4 字节，同样要拆）。

### 5. 不支持回调

不能把 YXG 函数指针传给 C 函数（比如 `EnumWindows`、`qsort`）。

**绕法**：目前没有。需要 C 侧主动轮询。

### 6. `toString` 是保留名

编译器会把任何 `.toString()` 重写成「数组转字符串」，
自定义的 `toString` 方法永远不可达。定义时会收到警告。

**绕法**：改用 `asText()` / `toText()`。

### 7. 没有析构函数

`File`、`Socket`、`Database` 都必须手动 `close()`。
`yxg.Resource` 只能**事后检测**泄漏，不能自动回收。

**计划**：3.0 实现。

### 8. 类 `static string` 字段的初始化

已在 2.6 修复（生成时带长度头），可以正常用：

```yxg
pub class Cfg { pub static string name = "hello"; }
String.len(Cfg.name);      // 5  ✓
```

### 9. 大正则性能

`yxg.Regex` 是纯 YXG 的回溯引擎，不是 C 的正则库。
几 KB 的文本没问题，几 MB 会明显慢。避免 `(a+)+b` 这类嵌套量词。

**绕法**：大文本用手写解析（`String.indexOf` + `String.sub`）。

### 10. `input` 只能从真实控制台读

`input(...)` 底层走 `ReadConsoleA`，读的是**控制台输入句柄**。以下两种方式都读不到内容，
`input` 会原样返回 0 / `0.0` / `""`，程序看起来像「跳过了一个输入」：

```
prog.exe < input.txt          # 重定向文件
type input.txt | prog.exe     # 管道
```

**绕法**：

- 交互程序就老老实实在终端里跑（配合 `yxg run`）。
- 需要自动化测试时，把输入改成命令行参数（`main(string[] args)` 或 `System.argv`），
  或者用 `File.readAllLines` 读文件自己解析，不要依赖 `input`。
- 需要从管道读时，用 `File` 打开 `CONIN$`/文件，或直接用 FFI 调 `ReadFile`。

### 11. `InputSet(...)` 是空操作

`InputSet("utf8");` 能被解析通过，但**不做任何事** —— 编译器把它变成一个空语句（`AST_NULL`）。
它原本是给控制台输入编码/模式预留的位置，目前没有实现。

**绕法**：不用写。需要控制台编码请用 `SetConsoleOutputCP` / `SetConsoleCP`（FFI 调用）。

### 12. `yxg.Music` / `yxg.Video` 的运行时对象没人汇编 —— 已修复

2.6 里 `main.c` 的 `has_music_lib` / `has_video_lib` 分支只编译了 `*_loader.c`，
**从没汇编 `runtime_music.asm` / `runtime_video.asm`**，链接命令里写的是裸文件名
（相对当前目录）而不是 `runtime_cache` 里的路径。于是用到音频/视频的程序在链接阶段就会报
`could not open 'runtime_music.obj'`。

连带一个问题：`runtime_window.asm` 的 `Canvas_load` 引用 `_image_load`，
所以只要链 `runtime_window` 就必然需要 `image_loader.obj`；它原来只在 `has_image_lib` 时才编译。
用 Video（内部要用 Canvas）时会缺符号。

**修复**：

- 补 `runtime_obj("runtime_music")` / `runtime_obj("runtime_video")`；
- 链接改用 `append_runtime_to_command(...)`，走 `runtime_cache` 里的路径；
- `image_loader.obj` 改成「`has_image_lib` 或 `has_window_lib` 都编译并链接」。

现在 `import yxg.Music` / `import yxg.Video` 都能正常构建。
---

## 不是问题但容易踩

| 现象 | 说明 |
|---|---|
| `int p = arr;` 报错 | YXG 数组名不是指针，要用 `&arr[0]` |
| `print` 后直接跟字符串字面量报错 | 已知解析限制，加个变量或改成 `print("...")` |
| `x.toString()` 结果奇怪 | 保留名，见第 6 条 |
| `e.info` 崩 | 见第 1 条，用 `e.getInfo()` |
| `callStdcall` 后崩 | cdecl/stdcall 选错，或参数个数不对 |
| 找不到 `MessageBoxA` | stdcall 符号带 `@N` 后缀，`N` = 参数字节数 |
| `input` 读到 0 / 空串 | stdin 被重定向或管道，见第 10 条 |
| `InputSet(...)` 没反应 | 空操作，见第 11 条 |
| `columnFloat()` 少了小数 | 见第 13 条，用 `columnText` + `parseFloat` |

---

## 回归基线

当前发布版本经 `repro/` 下 110 个用例验证：**101 通过**（新增 `CatchCrossModule` / `StdRegexGroup` 覆盖上面已修的三条）。

9 个「失败」是**刻意的负向用例**（应当编译失败）：

`BT1` `ConstBad` `E_brace` `E_main` `E_semi` `E_type` `E_undecl` `SockTest` `SockTest2`

其中 `SockTest` / `SockTest2` 用的是不存在的 `Socket.inetAddr` / `inetNtoa` / `htons`
以及不支持的函数指针强制转换，属于测试用例本身过时，不是编译器缺陷。