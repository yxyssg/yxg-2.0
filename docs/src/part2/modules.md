# 模块系统（import）

## 基本语法

YXG 用 `import` 语句引入其他源文件：

    import yxg.Math;
    import yxg.String;

    int main() {
        print "sqrt(16) = ", Math.sqrt(16.0), "\n";
        print "len = ", String.len("hello"), "\n";
        return 0;
    }

输出：

    sqrt(16) = 4.000000
    len = 5

`import` 语句必须放在文件顶部，在所有其他声明之前。

## 导入方式

### 点号路径导入

用点号分隔模块路径，编译器会转换成目录分隔符并补上 `.yxg` 后缀：

    import yxg.String;
    import yxg.Math;
    import yxg.System;
    import yxg.Collections.List;
    import yxg.Collections.Map;

`import yxg.String` 等价于查找 `yxg/String.yxg`（相对编译器的 `lib/` 目录）。

### 字符串路径导入

也可以用字符串指定相对/绝对文件路径：

    import "mylib/custom.yxg";

相对路径以当前工作目录为基准。

## 静态方法怎么调

标准库的工具类（`String`、`Math`、`System`）里的方法**全部声明为 `static`**：

    pub class String {
        pub static int len(str s) { ... }
        pub static string toUpperStr(str s) { ... }
    }

`static` 方法的调用方式是 **`类名.方法()`**，不要通过实例调用：

    import yxg.Math;
    import yxg.String;
    import yxg.System;

    int main() {
        // 正确
        print Math.abs(-5), "\n";             // 5
        print String.len("hello"), "\n";      // 5
        print System.time(), "\n";            // Unix 时间戳
        return 0;
    }

### 可以 new，但没用

这三个类本身是普通类，`new String()` **语法上是合法的**，能编译通过：

    import yxg.String;

    int main() {
        String s = new String();    // 合法，但 s 里没有任何实例成员
        // s.len("hello");          // 错误：len 是静态方法，不能通过实例调
        print String.len("hello"), "\n";   // 正确
        return 0;
    }

原因：`String` 里只有 `static` 方法和 `static` 常量，**没有实例字段、没有实例方法**。所以 `new` 出来的对象是空壳，只能用 `String.xxx()` 的写法。

常见错误对照：

| 错误写法 | 正确写法 | 原因 |
|---|---|---|
| `m.sqrt(16.0)`（`m` 是 `new Math()`） | `Math.sqrt(16.0)` | 静态方法不能通过实例调 |
| `s.len("hi")`（`s` 是 `new String()`） | `String.len("hi")` | 同上 |
| `sys.time()`（`sys` 是 `new System()`） | `System.time()` | 同上 |

### 容器类必须 new

`List`、`Map` 这类泛型容器有实例状态（内部缓冲区、长度等），必须实例化：

    import yxg.Collections.List;
    import yxg.Json;

    int main() {
        List<JsonValue> list = new List<JsonValue>();
        list.add(Json.makeInt(1));
        list.add(Json.makeInt(2));
        print "size = ", list.size(), "\n";            // 2
        print "get(0) = ", list.get(0).asInt(), "\n";  // 1
        return 0;
    }

## 模块搜索路径

编译器按以下顺序查找模块：

1. 当前工作目录
2. 编译器可执行文件所在目录下的 `lib/` 子目录
3. `YXG_PATH` 环境变量指定的目录（多个用分号分隔）

假设目录结构：

    yxg/
      main.yxg
      lib/
        yxg/
          String.yxg
          Math.yxg
      mylib/
        custom.yxg

则：

    import yxg.String;              // 从 lib/yxg/String.yxg 找到
    import "mylib/custom.yxg";      // 从 ./mylib/custom.yxg 找到

### 设置自定义搜索路径

Windows 命令提示符：

    set YXG_PATH=C:\my_yxg_libs;D:\shared_yxg

PowerShell：

    $env:YXG_PATH = "C:\my_yxg_libs;D:\shared_yxg"

## 重复导入

同一模块只会被导入一次，重复 `import` 会被自动忽略：

    import yxg.String;
    import yxg.String;              // 忽略，不会重复导入

    int main() {
        print String.len("hi"), "\n";
        return 0;
    }

## 循环导入

YXG 不支持循环导入。如果 A 导入 B、B 又导入 A，编译器会报错。出现循环依赖时应把公共部分抽到第三个模块。

## 完整示例

项目结构：

    project/
      main.yxg
      lib/
        yxg/
          String.yxg
          Math.yxg
          System.yxg

`main.yxg`：

    import yxg.String;
    import yxg.Math;
    import yxg.System;

    int main() {
        string greeting = "Hello, YXG!";

        print "长度: ", String.len(greeting), "\n";
        print "大写: ", String.toUpperStr(greeting), "\n";
        print "sqrt(144) = ", Math.sqrt(144.0), "\n";
        print "随机数: ", Math.random(), "\n";
        print "时间戳: ", System.time(), "\n";

        return 0;
    }

编译运行：

    yxg run main.yxg

## 常见错误

### 模块未找到

    import nonexistent.lib;

报错：

    error[E2006]: module 'nonexistent.lib' not found

检查路径拼写，以及文件是否在搜索路径中。

### 忘记 import

    int main() {
        print Math.abs(-5);      // 错误：Math 未导入
        return 0;
    }

报错：

    error[E3001]: variable 'Math' is not declared

补上 `import yxg.Math;` 即可。

### 用 str 当变量名

`str` 是 YXG 的类型关键字（等价于 `string`），不能用作变量名：

    import yxg.String;

    int main() {
        String str = new String();     // 错误：str 是类型名
        return 0;
    }

报错：

    error[E2002]: expected 'Semicolon' before 'Str'

换个变量名即可：

    import yxg.String;

    int main() {
        String s = new String();
        print String.len("hello"), "\n";
        return 0;
    }

### 通过实例调静态方法

    import yxg.Math;

    int main() {
        Math m = new Math();
        print m.sqrt(16.0);      // 错误：静态方法
        return 0;
    }

报错：

    error[E3015]: no matching method 'sqrt'

改为 `Math.sqrt(16.0)`。

## 常用标准库模块一览

| 模块 | 导入路径 | 用法 |
|---|---|---|
| 字符串 | `yxg.String` | `String.len(s)` / `String.sub(s,0,3)` |
| 数学 | `yxg.Math` | `Math.sqrt(x)` / `Math.random()` |
| 系统 | `yxg.System` | `System.time()` / `System.sleep(1000)` |
| 文件 | `yxg.File` | `File.readAllText(p)` / `File.writeAllText(p,s)` |
| JSON | `yxg.Json` | `Json.parse(s)` / `Json.stringify(v)` |
| 列表 | `yxg.Collections.List` | `new List<T>()` |
| 映射 | `yxg.Collections.Map` | `new Map<K,V>()` |
| 数据库 | `yxg.Collections.Database` | `new Database(path)` |
| 网络 | `yxg.Socket` | `new Socket()` |
| 线程池/HTTP | `yxg.ThreadPool` | `new TPServer(port)` |

## 继续阅读

掌握了模块系统后，下一章将介绍 YXG 的内置函数参考。
