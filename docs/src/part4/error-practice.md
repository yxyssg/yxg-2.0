# 实战九：错误处理与资源管理

真实程序里，「正常路径」只是一半。这一篇讲怎么把错误和资源管利落。

## 问题：三种错法混在一起

写一个读配置文件的函数，很容易变成这样：

```yxg
// 反例：三种信号混在一起
pub string readConfig(string path) {
    string s = File.readAllText(path);
    if String.len(s) == 0 { return ""; }  // 文件不存在？读失败？还是本来就是空文件？
    return s;
}
```

调用方拿到 `""` 根本分不清是「文件不存在」「读失败」还是「文件真的是空的」。

## 方案一：统一错误状态

`yxg.Error` 提供一条全局错误状态线：设置、查询、描述。

```yxg
import yxg.Error;
import yxg.File;

pub bool loadConfig(string path) {
    SysError.clear();

    if !File.exists(path) {
        SysError.set("Config", 1001, "配置文件不存在");
        return false;
    }

    string s = "";
    try {
        File f = new File(path, "r");
        s = f.readAll();
        f.close();
    } catch (ERROR e) {
        SysError.set("Config", 1003, "读取失败: " + e.getInfo());
        return false;
    }

    if String.len(s) == 0 {
        SysError.set("Config", 1002, "配置文件为空");
        return false;
    }

    SysError.clear();
    return true;
}

pub void main() {
    if !loadConfig("app.conf") {
        // 一行拿到全部信息
        print "失败: ", SysError.describe(), "\n";
        print "  模块 = ", SysError.module(), "\n";
        print "  码   = ", SysError.code(), "\n";
        print "  消息 = ", SysError.message(), "\n";
        return;
    }
    print "加载成功\n";
}
```

`describe()` 输出形如 `[Config:1001] 无法打开配置文件`。

### SysError 接口

| 方法 | 说明 |
|---|---|
| `set(module, code, message)` | 设置错误状态 |
| `clear()` | 清除 |
| `has()` / `ok()` | 是否有错 / 是否无错 |
| `code()` / `module()` / `message()` | 各部分 |
| `count()` | 错误计数（累计出现次数） |
| `describe()` | 一行描述 |
| `printToStderr()` | 打到 stderr |
| `printAndClear()` | 打印后清除 |

需要分级时把等级编码进 `code`（比如 1xxx=警告 2xxx=错误）。

## 方案二：异常（用于「不该继续」的情况）

错误码适合「调用方要判断」的情况。真正不该继续时，用异常：

```yxg
import yxg.Error;

pub class ConfigException : SysException {
    pub ConfigException(string msg) {
        this.info = msg;
    }
}

pub string requireText(string s, string what) {
    if String.len(s) == 0 {
        throw new ConfigException(what + " 不能为空");
    }
    return s;
}

pub void main() {
    try {
        string v = requireText("", "用户名");
        print "值 = ", v, "\n";
    } catch (ERROR e) {
        print "捕获: ", e.getInfo(), "\n";
    }
    print "程序继续\n";
}
```

### 异常相关的实用点

| 需求 | 写法 |
|---|---|
| 读消息 | `e.getInfo()`（**推荐**） |
| 判断类型 | `catch (ConfigException e)` 精确捕获 |
| 往上抛 | `catch` 里不处理就直接 `throw e;` |
| finally | `try { } catch { } finally { }` |

> **注意**：`catch (ERROR e)` 里 `e` 的静态类型是 `ERROR`，
> 只能访问 `ERROR` 的成员。
>
> 读异常消息**统一用 `e.getInfo()`**。直接读字段 `e.info` 在
> `SysException` 派生的子类上会崩溃（字段偏移不一致），
> 只在你自己的类直接继承 `ERROR` 时才可靠。
>
> 另外，`toString()` 是保留名（编译器会重写成数组转字符串），
> 自定义方法请用 `asText()` 之类的名字。

## 方案三：资源泄漏防护

文件、套接字、数据库连接忘了关，程序跑久了句柄就耗尽。
`yxg.Resource` 提供登记表，能查出谁没关。

```yxg
import yxg.Resource;
import yxg.File;

pub void main() {
    Resource.enable(true);

    File f = new File("data.txt", "r");
    string s = f.readAll();
    // 故意不 close，看看能不能查出来

    if Resource.hasLeak() {
        print "检出泄漏 ", Resource.leakCount(), " 个：\n";
        Resource.dump();          // 逐条打印
    } else {
        print "没有泄漏\n";
    }

    Resource.releaseAll();        // 兜底释放
}
```

`File`、`Socket`、`Database` 会在打开时自动登记、关闭时自动注销，
不需要你手动 `track`。

### Resource 接口

| 方法 | 说明 |
|---|---|
| `enable(bool)` / `enabled()` | 开关追踪（默认开） |
| `hasLeak()` | 是否有未关闭句柄 |
| `leakCount()` | 泄漏数量 |
| `dump()` | 逐条打印泄漏详情 |
| `report()` | 汇总报告 |
| `leakCheck()` | 检查并返回 bool |
| `releaseAll()` | 强制释放全部 |
| `kindName(kind)` | 类型名（1=File 2=Socket 3=Database 4=Https） |

### 推荐的写法

```yxg
import yxg.File;

pub string readSafe(string path) {
    if !File.exists(path) { return null; }
    File f = new File(path, "r");
    string s = f.readAll();
    f.close();              // 所有分支都记得关
    return s;
}
```

YXG 目前**没有析构函数**（计划 3.0），所以 `close()` 必须手动写。
`Resource` 是事后检测手段，不是自动回收。

## 组合起来：一个完整的加载函数

```yxg
import yxg.Error;
import yxg.Resource;
import yxg.File;

pub class App {
    pub static File cfg = null;
    pub static bool loaded = false;
}

pub bool startup(string path) {
    SysError.clear();

    if !File.exists(path) {
        SysError.set("App", 2001, "配置不存在: " + path);
        return false;
    }
    App.cfg = new File(path, "r");

    string text = App.cfg.readAll();
    if String.len(text) == 0 {
        App.cfg.close();
        App.cfg = null;
        SysError.set("App", 2002, "配置为空");
        return false;
    }

    App.loaded = true;
    return true;
}

pub void shutdown() {
    if App.cfg != null {
        App.cfg.close();
        App.cfg = null;
    }
    App.loaded = false;
}

pub void main() {
    Resource.enable(true);

    if !startup("app.conf") {
        print "启动失败: ", SysError.describe(), "\n";
        return;
    }

    print "启动成功\n";
    shutdown();

    if Resource.hasLeak() {
        print "警告：仍有 ", Resource.leakCount(), " 个句柄未关\n";
        Resource.dump();
    } else {
        print "资源已全部释放\n";
    }
}
```

## 该用哪个

| 场景 | 选择 |
|---|---|
| 调用方需要判断并分支 | 返回 `bool` + `SysError` |
| 多层调用、不想每层都传错误码 | 异常 |
| 参数非法、配置缺失 | 异常（`require*`） |
| 可恢复的运行时问题（文件锁、网络抖动） | `SysError` + 重试 |
| 检查有没有漏关句柄 | `Resource` |

## 继续阅读

- [../part2/exceptions.md](../part2/exceptions.md) —— 异常语法
- [../part2/error-resource.md](../part2/error-resource.md) —— 错误与资源完整参考
- [config-app.md](config-app.md) —— 配置读写实战