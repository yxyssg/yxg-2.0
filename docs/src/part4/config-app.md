# 实战五：带异常处理的配置读写

## 目标

做一个读取配置文件的工具，演示：

- 自定义异常类
- `try` / `catch` / `end` 的完整用法
- 文件读写
- 字符串解析
- 错误码分级处理

练习点：

- 继承 `ERROR` 定义自己的异常
- 多 `catch` 分支的类型匹配
- `end` 收尾块保证资源释放
- `File` 库的读写

## 为什么需要异常

假设要读一个配置项：

```yxg
int readPort(string path) {
    string text = File.readAllText(path);
    return parsePort(text);
}
```

如果文件不存在、内容格式不对，怎么办？用返回值表示错误，调用方容易漏检；用异常能强制中断并交给专门的处理代码。

## 第一步：定义异常类

异常必须继承 `ERROR`，`ERROR` 有一个 `info` 字段存错误信息：

```yxg
pub class ConfigError : ERROR {
    pub int code;

    pub ConfigError(string msg, int c) {
        this.info = msg;
        this.code = c;
    }
}
```

按具体类型再分层：

```yxg
pub class ConfigNotFound : ConfigError {
    pub ConfigNotFound(string path) {
        this.info = "配置文件不存在: " + path;
        this.code = 1001;
    }
}

pub class ConfigBadFormat : ConfigError {
    pub ConfigBadFormat(string msg) {
        this.info = "配置格式错误: " + msg;
        this.code = 1002;
    }
}
```

## 第二步：抛出异常

```yxg
import yxg.File;

pub string loadConfig(string path) {
    if !File.exists(path) {
        throw new ConfigNotFound(path);
    }
    string text = File.readAllText(path);
    if String.len(text) == 0 {
        throw new ConfigBadFormat("文件为空");
    }
    return text;
}
```

`throw new XxxError(...)` —— 必须 `new` 一个异常对象。

## 第三步：捕获

```yxg
import yxg.File;
import yxg.String;

pub void main() {
    try {
        string cfg = loadConfig("config.txt");
        print "配置内容: ", cfg, "\n";
    } catch (ConfigNotFound e) {
        print "错误 ", e.code, ": ", e.info, "\n";
    } catch (ConfigError e) {
        print "配置错误 ", e.code, ": ", e.info, "\n";
    }
}
```

如果 `config.txt` 不存在，输出：

```
错误 1001: 配置文件不存在: config.txt
```

### catch 的语法

```yxg
} catch (类型 变量名) {
```

**括号不能省**。写成 `catch ConfigError e {` 会报 `expected 'ParenLeft' before 'Identifier'`。

### catch 的匹配顺序

按声明顺序尝试，第一个匹配的执行。所以：

```yxg
} catch (ConfigNotFound e) {      // 先匹配具体类型
    // ...
} catch (ConfigError e) {         // 再匹配父类型
    // ...
}
```

反过来写的话，`ConfigError` 会拦截所有子类，第二个 catch 永远不执行。

## 第四步：end 收尾块

`end` 对应其他语言的 `finally`，无论有没有异常都会执行：

```yxg
pub void main() {
    print "开始\n";
    try {
        print "读取中\n";
        throw new ConfigBadFormat("测试错误");
    } catch (ConfigError e) {
        print "捕获: ", e.info, "\n";
    } end {
        print "清理资源\n";
    }
    print "结束\n";
}
```

输出：

```
开始
读取中
捕获: 配置格式错误: 测试错误
清理资源
结束
```

> **注意关键字是 `end`，不是 `finally`。**

## 第五步：解析配置内容

假设配置格式是 `key=value` 每行一条：

```yxg
import yxg.String;
import yxg.Char;

pub int parsePort(string text) {
    // 找 "port=" 后面的数字
    int pos = String.indexOfStr(text, "port=");
    if pos < 0 {
        throw new ConfigBadFormat("缺少 port 配置项");
    }
    int start = pos + 5;
    int n = 0;
    int i = start;
    while i < String.len(text) {
        int ch = String.charAt(text, i);
        if !Char.isDigit(ch) {
            break;
        }
        n = n * 10 + (ch - '0');
        i = i + 1;
    }
    if n <= 0 || n > 65535 {
        throw new ConfigBadFormat("端口号超出范围");
    }
    return n;
}
```

`ch - '0'` 把 ASCII 数字转成实际数值：`'5' - '0'` = `53 - 48` = `5`。

## 完整程序

```yxg
import yxg.File;
import yxg.String;
import yxg.Char;

pub class ConfigError : ERROR {
    pub int code;

    pub ConfigError(string msg, int c) {
        this.info = msg;
        this.code = c;
    }
}

pub class ConfigNotFound : ConfigError {
    pub ConfigNotFound(string path) {
        this.info = "配置文件不存在: " + path;
        this.code = 1001;
    }
}

pub class ConfigBadFormat : ConfigError {
    pub ConfigBadFormat(string msg) {
        this.info = "配置格式错误: " + msg;
        this.code = 1002;
    }
}

pub string loadConfig(string path) {
    if !File.exists(path) {
        throw new ConfigNotFound(path);
    }
    string text = File.readAllText(path);
    if String.len(text) == 0 {
        throw new ConfigBadFormat("文件为空");
    }
    return text;
}

pub int parsePort(string text) {
    int pos = String.indexOfStr(text, "port=");
    if pos < 0 {
        throw new ConfigBadFormat("缺少 port 配置项");
    }
    int start = pos + 5;
    int n = 0;
    int i = start;
    while i < String.len(text) {
        int ch = String.charAt(text, i);
        if !Char.isDigit(ch) {
            break;
        }
        n = n * 10 + (ch - '0');
        i = i + 1;
    }
    if n <= 0 || n > 65535 {
        throw new ConfigBadFormat("端口号超出范围");
    }
    return n;
}

pub int tryLoad(string path) {
    print "尝试加载: ", path, "\n";
    try {
        string cfg = loadConfig(path);
        int port = parsePort(cfg);
        print "  成功，端口 = ", port, "\n";
        return port;
    } catch (ConfigNotFound e) {
        print "  [", e.code, "] ", e.info, "\n";
        return -1;
    } catch (ConfigError e) {
        print "  [", e.code, "] ", e.info, "\n";
        return -1;
    }
}

pub void main() {
    // 准备一个正常的配置文件
    File.writeAllText("good.txt", "host=localhost\nport=8080");

    // 准备一个缺少 port 的配置
    File.writeAllText("bad.txt", "host=localhost\n");

    // 准备一个端口越界的配置
    File.writeAllText("oops.txt", "port=99999");

    tryLoad("good.txt");
    tryLoad("bad.txt");
    tryLoad("oops.txt");
    tryLoad("missing.txt");

    print "\n全部处理完成\n";
}
```

输出：

```
尝试加载: good.txt
  成功，端口 = 8080
尝试加载: bad.txt
  [1002] 配置格式错误: 缺少 port 配置项
尝试加载: oops.txt
  [1002] 配置格式错误: 端口号超出范围
尝试加载: missing.txt
  [1001] 配置文件不存在: missing.txt

全部处理完成
```

### 为什么 tryLoad 里 return 不会跳过 catch

`return` 在 `try` 块内正常返回；抛异常时跳到对应的 `catch`。`catch` 里的 `return` 也正常返回。所以四种情况都能拿到值（成功返回端口，失败返回 -1）。

## 知识点总结

| 知识点 | 用法 |
|---|---|
| 异常基类 | 所有异常继承 `ERROR`，用 `this.info` 存信息 |
| 定义异常 | `pub class XxxError : ERROR { ... }` |
| 抛出 | `throw new XxxError(...)` |
| 捕获 | `catch (类型 变量名)` —— 带括号 |
| 收尾块 | `} end { ... }` —— 关键字是 `end` |
| 匹配顺序 | 具体类型在前，父类在后 |
| 数字解析 | `ch - '0'` 把 ASCII 数字转数值 |
| 文件检查 | `File.exists(path)` |
| 文件读写 | `File.readAllText` / `File.writeAllText` |

## 常见错误

### catch 没写括号

```yxg
} catch ConfigError e {        // ❌
```

报错：`expected 'ParenLeft' before 'Identifier'`

### 用 finally 而不是 end

```yxg
} finally {                    // ❌
```

报错：`expected 'Semicolon' before 'BracketLeft'`

### 抛出时忘了 new

```yxg
throw ConfigError("msg");      // ❌
throw new ConfigError("msg", 1);   // ✅
```

### catch 顺序写反

```yxg
} catch (ConfigError e) {      // 父类在前
    // 所有子类都会被这里吞掉
} catch (ConfigNotFound e) {   // 永远不执行
    // ...
}
```

### 异常类没继承 ERROR

```yxg
pub class MyError { ... }       // 没有 : ERROR
throw new MyError();            // 编译报错
```

## 继续阅读

下一篇用泛型容器做一个数据聚合。
