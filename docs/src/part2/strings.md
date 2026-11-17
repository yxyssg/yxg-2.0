# 字符串

## 字符串的创建

字符串用双引号包裹：

    int main() {
        string s1 = "Hello, World!";
        string s2 = "";
        string s3 = "包含中文和日本語";

        print s1, "\n";
        print s2, "\n";
        print s3, "\n";
        return 0;
    }

输出：

    Hello, World!

    包含中文和日本語

注意 print 不会自动加空格，也不会自动换行，要换行必须自己写 `"\n"`。

转义序列：

    string s = "第一行\n第二行";
    string t = "列1\t列2\t列3";
    string q = "他说：\"你好\"";
    string b = "路径：C:\\Users\\Admin";

## 字符串拼接

使用 + 运算符拼接字符串：

    string hello = "Hello";
    string world = "World";
    string s1 = hello + " " + world;
    string s2 = hello + "!";

    print s1, "\n";
    print s2, "\n";

输出：

    Hello World
    Hello!

与字符拼接：

    string s = "ABC";
    char ch = 'D';
    string result = s + ch;         // "ABCD"

注意：字符串只能和字符（char）拼接，不能直接和 int、float、bool 相加：

    // string s = "Value: " + 42;   // 错误！

要输出数字，直接把数字作为 print 的另一个参数：

    int n = 42;
    print "Value: ", n, "\n";

## 使用 String 库

String 库中的所有方法都是 static 的，直接用类名 `String.方法名()` 调用，不需要、也不能 new 实例：

    import yxg.String;

    int main() {
        int len = String.len("Hello");
        print len, "\n";                    // 5

        return 0;
    }

输出：

    5

下面的写法是错的：String 是 static 库类，拿不到实例来调用方法：

    // String text = new String();      // 错误！String 没有可实例化的对象
    // text.len("Hello");               // 错误！cannot determine class of object 'text'
    // s.len("Hello");                  // 错误！string 变量本身也没有方法

另外 `str` 是保留字，不能当变量名：

    // string str = "Hello";            // 错误！expected variable name

变量名请用 `s`、`text`、`msg` 等。

## 字符串长度

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        int len = String.len(s);
        print len, "\n";                    // 13

        bool empty = String.isEmpty("");
        print empty, "\n";                  // true

        return 0;
    }

输出：

    13
    true

## 访问单个字符

    import yxg.String;

    int main() {
        string s = "Hello";

        int c0 = String.charAt(s, 0);       // 72（'H' 的 ASCII 码）
        int c1 = String.charAt(s, 1);       // 101（'e'）
        int c4 = String.charAt(s, 4);       // 111（'o'）

        print c0, " ", c1, " ", c4, "\n";

        // 遍历
        for int i = 0; i < String.len(s); i++ {
            print "s[", i, "] = ", String.charAt(s, i), "\n";
        }

        return 0;
    }

输出：

    72 101 111
    s[0] = 72
    s[1] = 101
    s[2] = 108
    s[3] = 108
    s[4] = 111

首尾字符快捷方法：

    int first = String.first(s);            // 72
    int last = String.last(s);              // 111

## 子串操作

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        string sub1 = String.sub(s, 0, 5);  // "Hello"
        string sub2 = String.sub(s, 7, 5);  // "World"
        string left = String.left(s, 5);    // "Hello"
        string right = String.right(s, 6);  // "World!"

        print sub1, "\n";
        print sub2, "\n";
        print left, "\n";
        print right, "\n";

        return 0;
    }

输出：

    Hello
    World
    Hello
    World!

## 字符串比较

    string s1 = "apple";
    string s2 = "apple";
    string s3 = "banana";

    if s1 == s2 { print "相同\n"; }
    if s1 != s3 { print "不同\n"; }
    if s1 < s3  { print "apple 在 banana 之前\n"; }

== 比较的是字符串内容，不是内存地址。比较区分大小写。

if 的条件也可以加括号，两种写法等价：`if (s1 == s2) { ... }`。

## 前后缀判断

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        bool b1 = String.startsWith(s, "Hello");    // true
        bool b2 = String.startsWith(s, "World");    // false
        bool b3 = String.endsWith(s, "World!");     // true
        bool b4 = String.endsWith(s, "Hello");      // false

        print b1, " ", b2, " ", b3, " ", b4, "\n";

        return 0;
    }

输出：

    true false true false

## 字符与子串查找

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        int pos1 = String.indexOf(s, 'o');           // 4
        int pos2 = String.lastIndexOf(s, 'o');       // 8
        bool has = String.contains(s, 'W');          // true

        int pos3 = String.indexOfStr(s, "World");    // 7
        bool has2 = String.containsStr(s, "llo");    // true

        print pos1, " ", pos2, " ", has, " ", pos3, " ", has2, "\n";

        return 0;
    }

输出：

    4 8 true 7 true

## 大小写转换

String 库只提供整串转换的 `toUpperStr` / `toLowerStr`，没有单字符版本 `toUpper` / `toLower`：

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        string up = String.toUpperStr(s);            // "HELLO, WORLD!"
        string lo = String.toLowerStr(s);            // "hello, world!"

        print up, "\n";
        print lo, "\n";

        return 0;
    }

输出：

    HELLO, WORLD!
    hello, world!

## 空白处理

    import yxg.String;

    int main() {
        string s = "   Hello, World!   ";

        string t1 = String.trimL(s);
        string t2 = String.trimR(s);
        string t3 = String.trim(s);

        print "'", t1, "'\n";
        print "'", t2, "'\n";
        print "'", t3, "'\n";

        bool space = String.isSpace(' ');
        bool tab   = String.isSpace('\t');

        print space, " ", tab, "\n";

        return 0;
    }

输出：

    'Hello, World!   '
    '   Hello, World!'
    'Hello, World!'
    true true

## 重复与填充

    import yxg.String;

    int main() {
        string r1 = String.repeat("Hi", 3);          // "HiHiHi"
        string r2 = String.repeat("AB", 2);          // "ABAB"

        string p1 = String.padL("42", 5, '0');       // "00042"
        string p2 = String.padR("YXG", 6, '.');      // "YXG..."

        print r1, "\n";
        print r2, "\n";
        print p1, "\n";
        print p2, "\n";

        return 0;
    }

输出：

    HiHiHi
    ABAB
    00042
    YXG...

## 替换

    import yxg.String;

    int main() {
        string r = String.replace("Hello, World!", "World", "YXG");
        string r2 = String.replace("one two one two", "one", "1");

        print r, "\n";                              // Hello, YXG!
        print r2, "\n";                             // 1 two 1 two

        return 0;
    }

输出：

    Hello, YXG!
    1 two 1 two

## 统计

    import yxg.String;

    int main() {
        string s = "Hello, World!";

        int n1 = String.count(s, 'l');               // 3
        int n2 = String.countStr(s, "lo");           // 1

        print n1, "\n";
        print n2, "\n";

        return 0;
    }

输出：

    3
    1

## 字符串不可变性

YXG 中字符串是不可变的。每次操作都创建新字符串：

    string s = "Hello";
    string t = s;
    s = s + " World";
    print "s:", s, "\n";
    print "t:", t, "\n";

输出：

    s: Hello World
    t: Hello

旧字符串由 GC 自动回收。

## 字符串与数组

字符串不是数组，不能用下标或 foreach 取字符。遍历字符串请结合 `String.len` 和 `String.charAt`：

    import yxg.String;

    int main() {
        string s = "YXG";

        for int i = 0; i < String.len(s); i++ {
            print "s[", i, "] = ", String.charAt(s, i), "\n";
        }

        return 0;
    }

输出：

    s[0] = 89
    s[1] = 88
    s[2] = 71

以下写法都会报错：

    // for char ch : s { ... }        // 错误！E3006: cannot iterate over non-array type
    // int c = s[0];                  // 错误！字符串不能用下标取字符

字符串不是数组，也不能赋值给 string[] 类型。

## 常见模式

### 统计字符出现次数

    import yxg.String;

    int countChar(string s, char target) {
        int count = 0;
        for int i = 0; i < String.len(s); i++ {
            if String.charAt(s, i) == target {
                count++;
            }
        }
        return count;
    }

### 反转字符串

用 `String.sub(s, i, 1)` 取出单字符子串再拼接（`string + int` 不被允许，`String.charAt` 返回 int）：

    import yxg.String;

    string reverse(string s) {
        string result = "";
        for int i = String.len(s) - 1; i >= 0; i-- {
            result = result + String.sub(s, i, 1);
        }
        return result;
    }

    int main() {
        print reverse("Hello"), "\n";       // olleH
        return 0;
    }

### 判断是否以某子串开头

    import yxg.String;

    bool startsWith(string s, string prefix) {
        if String.len(prefix) > String.len(s) { return false; }

        for int i = 0; i < String.len(prefix); i++ {
            if String.charAt(s, i) != String.charAt(prefix, i) {
                return false;
            }
        }
        return true;
    }

标准库里已有 `String.startsWith`，实际开发中直接用即可。

## 常见错误

### 混淆字符和字符串

    string s = "A";
    char ch = 'A';
    // string wrong = 'A';     // 错误
    // char wrong2 = "A";      // 错误

### 空字符串与 null

    import yxg.String;

    int main() {
        string s;
        // int len = String.len(s);    // 运行时错误：s 是 null

        string t = "";
        int len = String.len(t);       // 正确：0
        print len, "\n";               // 0

        return 0;
    }

未初始化的字符串默认是 null，传给 String 库方法会出错。建议始终初始化字符串。

### 用实例方式调用 String 库

    import yxg.String;

    int main() {
        // String text = new String();  // 错误！String 是 static 库类
        // text.len("Hello");           // 错误！

        int len = String.len("Hello");  // 正确
        print len, "\n";
        return 0;
    }

## 继续阅读

掌握了字符串后，下一章将学习类的定义与面向对象编程。
