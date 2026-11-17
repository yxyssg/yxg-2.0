# 实战二：文本分析工具

## 目标

分析一段文本，统计：

- 字符总数
- 元音字母个数
- 大写字母个数
- 空白字符个数
- 单词个数

练习点：

- `String` 标准库的使用
- 字符的 ASCII 码操作
- `Char` 标准库的字符判断
- 手写字符串扫描算法

## 字符怎么表示

YXG 里 `char` 是单字符，但 `String.charAt(s, i)` 返回的是 **ASCII 码（`int`）**，不是字符。所以比较字符要用数字或字符字面量：

```yxg
int ch = String.charAt("hello", 0);
if ch == 'h' {                  // 'h' 的字面量是 104
    print "是 h\n";
}
```

字符字面量 `'h'` 在比较时会被当作 `int` 处理。

## 第一步：字符计数

```yxg
import yxg.String;

pub void main() {
    string text = "Hello World";
    print "长度: ", String.len(text), "\n";
    print "首字符: ", String.charAt(text, 0), "\n";
    print "末字符: ", String.charAt(text, String.len(text) - 1), "\n";
}
```

输出：

```
长度: 11
首字符: 72
末字符: 100
```

`72` 是 `'H'`，`100` 是 `'d'`。

## 第二步：统计元音

元音是 `a e i o u`（大小写都要算）。手写扫描：

```yxg
import yxg.String;
import yxg.Char;

pub int countVowels(string s) {
    int n = 0;
    int i = 0;
    int len = String.len(s);
    while i < len {
        int ch = String.charAt(s, i);
        if ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u'
            || ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' || ch == 'U' {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}
```

另一种写法是先统一转小写再比：

```yxg
pub int countVowels2(string s) {
    int n = 0;
    int i = 0;
    int len = String.len(s);
    while i < len {
        int ch = Char.toLower(String.charAt(s, i));
        if ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}
```

`Char.toLower` 接受 ASCII 码，返回转小写后的 ASCII 码。

## 第三步：统计大写与空白

```yxg
import yxg.String;
import yxg.Char;

pub int countUpper(string s) {
    int n = 0;
    int i = 0;
    while i < String.len(s) {
        if Char.isUpper(String.charAt(s, i)) {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub int countSpaces(string s) {
    int n = 0;
    int i = 0;
    while i < String.len(s) {
        if Char.isSpace(String.charAt(s, i)) {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}
```

`Char.isUpper` / `Char.isSpace` 都返回 `bool`，参数是 ASCII 码。

## 第四步：数单词

单词数等于「空白段」的个数加 1。关键是处理连续空格：

```yxg
import yxg.String;
import yxg.Char;

pub int countWords(string s) {
    int n = 0;
    bool inWord = false;
    int i = 0;
    while i < String.len(s) {
        if Char.isSpace(String.charAt(s, i)) {
            if inWord {
                n = n + 1;
                inWord = false;
            }
        } else {
            inWord = true;
        }
        i = i + 1;
    }
    if inWord {
        n = n + 1;
    }
    return n;
}
```

思路：

- `inWord` 记录「当前是否在单词内」
- 遇到空白且之前在单词内 → 一个单词结束，计数加 1
- 遇到非空白 → 标记在单词内
- 循环结束后如果还在单词内，补加 1（处理末尾没有空格的情况）

这段代码对 `"a  b"`（两个空格）也正确返回 2，不会误算成 3。

## 完整程序

```yxg
import yxg.String;
import yxg.Char;

pub int countVowels(string s) {
    int n = 0;
    int i = 0;
    while i < String.len(s) {
        int ch = Char.toLower(String.charAt(s, i));
        if ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u' {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub int countUpper(string s) {
    int n = 0;
    int i = 0;
    while i < String.len(s) {
        if Char.isUpper(String.charAt(s, i)) {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub int countSpaces(string s) {
    int n = 0;
    int i = 0;
    while i < String.len(s) {
        if Char.isSpace(String.charAt(s, i)) {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub int countWords(string s) {
    int n = 0;
    bool inWord = false;
    int i = 0;
    while i < String.len(s) {
        if Char.isSpace(String.charAt(s, i)) {
            if inWord {
                n = n + 1;
                inWord = false;
            }
        } else {
            inWord = true;
        }
        i = i + 1;
    }
    if inWord {
        n = n + 1;
    }
    return n;
}

pub void report(string text) {
    print "文本: \"", text, "\"\n";
    print "  字符数: ", String.len(text), "\n";
    print "  元音数: ", countVowels(text), "\n";
    print "  大写数: ", countUpper(text), "\n";
    print "  空白数: ", countSpaces(text), "\n";
    print "  单词数: ", countWords(text), "\n";
}

pub void main() {
    report("Hello World");
    report("The Quick Brown Fox");
    report("  multiple   spaces  ");
    report("a");
}
```

输出：

```
文本: "Hello World"
  字符数: 11
  元音数: 3
  大写数: 2
  空白数: 1
  单词数: 2
文本: "The Quick Brown Fox"
  字符数: 19
  元音数: 5
  大写数: 4
  空白数: 3
  单词数: 4
文本: "  multiple   spaces  "
  字符数: 21
  元音数: 5
  大写数: 0
  空白数: 7
  单词数: 2
文本: "a"
  字符数: 1
  元音数: 1
  大写数: 0
  空白数: 0
  单词数: 1
```

### 边界情况验证

| 输入 | 字符数 | 单词数 | 说明 |
|---|---|---|---|
| `"Hello World"` | 11 | 2 | 单个空格分隔 |
| `"  multiple   spaces  "` | 21 | 2 | 前后空格 + 连续空格 |
| `"a"` | 1 | 1 | 单字符，末尾没空格 |

`countWords` 对连续空格、首尾空格都能正确处理。

## 扩展：找出最长的单词

```yxg
import yxg.String;
import yxg.Char;

pub string longestWord(string s) {
    int bestStart = 0;
    int bestLen = 0;
    int curStart = 0;
    int i = 0;
    while i < String.len(s) {
        if Char.isSpace(String.charAt(s, i)) {
            int curLen = i - curStart;
            if curLen > bestLen {
                bestLen = curLen;
                bestStart = curStart;
            }
            curStart = i + 1;
        }
        i = i + 1;
    }
    int curLen = String.len(s) - curStart;
    if curLen > bestLen {
        bestLen = curLen;
        bestStart = curStart;
    }
    if bestLen == 0 {
        return "";
    }
    return String.sub(s, bestStart, bestLen);
}

pub void main() {
    print longestWord("The Quick Brown Fox"), "\n";     // Quick
    print longestWord("a bb ccc dddd"), "\n";          // dddd
}
```

`String.sub(s, start, len)` 的**第三个参数是长度**，不是结束下标。

## 知识点总结

| 知识点 | 用法 |
|---|---|
| 字符取码 | `String.charAt(s, i)` 返回 ASCII 码 |
| 字符比较 | `ch == 'a'` 直接用字符字面量 |
| 大小写转换 | `Char.toUpper(ch)` / `Char.toLower(ch)` |
| 字符判断 | `Char.isUpper` / `Char.isSpace` / `Char.isDigit` |
| 子串 | `String.sub(s, start, len)` 第三参数是长度 |
| 状态标记 | 用 `bool inWord` 跟踪「是否在单词内」 |
| 手写扫描 | `while i < String.len(s)` + `charAt(s, i)` |

## 继续阅读

下一篇用类实现一个库存管理。
