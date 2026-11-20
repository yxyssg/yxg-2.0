# 标准库约定（2.5.2）

2.5.2 新增的五个模块（`Path` / `Date` / `Crypto` / `Encoding` / `Regex`）遵循同一套约定，
写代码时按这个来就不会踩坑。

## 1. 命名

| 规则 | 例子 |
|---|---|
| 类名首字母大写，模块名与类名一致 | `Path`、`Crypto` |
| 方法一律 `pub static`（无需实例） | `Path.fileName(...)` |
| 方法名小驼峰，动词开头 | `fileName`、`replaceAll`、`fromUnix` |
| 布尔返回值以 `is` / `has` / `contains` 开头 | `isAbsolute`、`hasDrive`、`contains` |
| 转换函数 `aToB` 形式 | `utf8ToUtf16`、`gbkToUtf8`、`replaceExt` |

## 2. 错误处理

**YXG 没有异常机制可以跨 FFI 边界**，所以统一用「哨兵值 + 调用方检查」：

| 返回类型 | 失败时返回 | 例子 |
|---|---|---|
| `string` | `""`（空串） | `Path.fileName("")` → `""` |
| `int`（下标/位置） | `-1` | `Regex.find(...)` 找不到 → `-1` |
| `int`（计数/长度） | `0` | `Encoding.utf8Length("")` → `0` |
| `bool` | `false` | `Regex.matches(...)` 不匹配 → `false` |

**调用方必须自己判断**，不要假设成功：

```yxg
int pos = Regex.find("[0-9]+", text);
if pos >= 0 {
    print "found at ", pos, "\n";
}

string name = Path.fileName(path);
if String.len(name) > 0 {
    print "file: ", name, "\n";
}
```

## 3. 输入约定

- 字符串一律按 **字节序列** 处理（YXG 的 `string` 就是字节）
- `Encoding` 里除了 `gbkToUtf8` / `utf8ToGbk`，其余都是纯字节运算
- 正则匹配按字节，UTF-8 下一个汉字算 3 个位置
- 所有模块**不修改入参**，返回新字符串

## 4. 边界与上限

| 模块 | 限制 | 说明 |
|---|---|---|
| `Crypto.md5` / `sha256` | 输入 ≤ 4000 字节 | 内部固定 4096 缓冲 |
| `Encoding.gbkToUtf8` | ≤ 8192 字节 | Win32 API 缓冲上限 |
| `Regex` | 无 `{n,m}` / 无反向引用 / 无断言 | 支持分组 `()`、或 `\|`、非捕获 `(?:)` |

## 5. 时间与哈希的可用性

- `Date` 的字符串格式化方法叫 **`asText()`**，不是 `toString()`
  —— `toString` 是编译器保留名，用户定义会被静默遮蔽
- `Crypto.base64Decode` 返回**十六进制形式**，用 `hexToText` 转回文本

## 6. 已知的编译器坑（写标准库时撞到过，记录下来）

| 现象 | 规避方式 |
|---|---|
| `format()` 的返回值不能直接参与 `+` 拼接 | 先赋给变量 |
| `format()` 的参数不能是函数调用 | 先赋给变量 |
| `string` 与 `null` 比较 | 编译错误，改用 `String.len(s) == 0` |
| `str[]` 数组元素赋值静默失败 | 用 `string[]`，先过一层 `string` 变量 |
| 字符码 → 单字符字符串 | 已修复（2.5 前置修复） |
| 原生函数返回字符串 | 返回数据指针，长度在 `data-4`，用 `gc_alloc(len+8)` |
| 原生函数接收字符串 | 长度必须由调用方另传一个参数（字面量没有长度头） |
