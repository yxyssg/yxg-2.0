# 项目配置文件 yxg.toml

## 为什么需要它

不带配置文件也能编译：

```
yxg build hello.yxg -o hello.exe
```

但项目一大，每次都要写一长串参数就烦了：

```
yxg build src/main.yxg src/util.yxg src/net.yxg -o bin/app.exe -noassert
```

`yxg.toml` 把这些固定下来，之后在项目目录里直接：

```
yxg build
```

就够了。

## 最快上手

```
yxg new demo
```

生成的项目骨架：

```
demo/
├── yxg.toml          ← 配置
├── README.md
├── .gitignore
└── src/
    └── Main.yxg      ← 入口
```

进去直接编译：

```
cd demo
yxg build
```

输出 `bin/demo.exe`。

## 配置文件长什么样

`yxg new` 生成的默认内容：

```toml
[project]
name = "demo"
version = "0.1.0"
entry = "src/Main.yxg"

[output]
path = "bin/demo.exe"

[build]
# Additional parameters at compile time, equivalent to the command line effect, with the command line taking higher priority
# support : -a -w -r -noassert --force -o <path>
flags = ""
```

一共三个节：`[project]`、`[output]`、`[build]`，外加可选的 `[dependencies]`。

---

## [project] 项目信息

```toml
[project]
name = "demo"
version = "0.1.0"
entry = "src/Main.yxg"
```

| 键 | 必填 | 说明 |
|---|---|---|
| `name` | 建议填 | 项目名 |
| `version` | 建议填 | 版本号（字符串） |
| `entry` | **关键** | 入口源文件路径，相对 `yxg.toml` 所在目录 |

`entry` **不是**唯一影响编译的键——编译器实际按下面的优先级决定编译哪些文件：

1. **命令行给出的文件或目录**（`yxg build a.yxg b.yxg`）
2. **`yxg.toml` 所在目录下的 `src/`**（递归扫描所有 `.yxg`）
3. 当前目录下的 `src/`（递归扫描）
4. **`entry` 指向的文件**（只有前面都没找到文件时才用）

所以只要项目按 `src/` 布局，`entry` 写不写都能编译。`entry` 的真正意义是「明确告诉别人入口在哪」，以及在 `src/` 不存在时兜底。

### entry 怎么写

```toml
entry = "src/Main.yxg"        # 单入口
```

标准布局：

```
demo/
├── yxg.toml              entry 指向 src/Main.yxg
├── src/
│   ├── Main.yxg          ← 入口，含 pub void main()
│   ├── Util.yxg          ← Main.yxg 里 import 它
│   └── sub/
│       └── Net.yxg       ← 子目录也会被扫描到
```

`Main.yxg`：

```yxg
import "Util.yxg";

pub void main() {
    print helper(), "\n";
}
```

因为 `src/` 会被整个扫描，`import` 的模块和非入口文件都会被一起编译进去。

### 一个容易踩的坑

`src/` 目录下**只能有一个 `main` 函数**。如果你把测试文件也放在 `src/` 里，两个文件都有 `main()` 就会报：

```
error[E3002]: duplicate function 'main'
```

解决办法：测试代码放到 `src/` 外面（比如 `tests/`），编译时用命令行显式指定：

```
yxg build tests/playground.yxg -o play.exe
```

---

## [output] 输出位置

```toml
[output]
path = "bin/demo.exe"
```

| 键 | 说明 |
|---|---|
| `path` | 可执行文件输出路径，相对项目根目录 |

不写这个节的话，默认输出到当前工作目录下的 `output.exe`。

**目录会自动创建**——`bin/` 不存在也能编，编译器会建好。

### 命令行覆盖

配置文件里的路径可以被命令行参数覆盖：

```
yxg build -o debug.exe
```

输出到 `debug.exe`，`yxg.toml` 里的 `bin/demo.exe` 被忽略。

**规则：命令行优先级高于配置文件。**

---

## [build] 编译参数

```toml
[build]
flags = "-noassert -w"
```

`flags` 是字符串，内容会被当作命令行参数解析。支持：

| 标志 | 效果 |
|---|---|
| `-a` | 只生成汇编 `output.asm` |
| `-w` | 窗口程序（不弹控制台） |
| `-r` | 编译后立即运行 |
| `-noassert` | 关闭 `assert` 检查 |
| `--force` | 强制重编 |
| `-o <path>` | 指定输出（会被 `[output] path` 覆盖） |

多个标志用空格分隔：

```toml
[build]
flags = "-noassert --force"
```

留空字符串等价于不加任何标志：

```toml
[build]
flags = ""
```

---

## [dependencies] 依赖管理

这是 `yxg.toml` 最有用的部分。

### 本地路径依赖

```toml
[dependencies]
mylib = { path = "../mylib" }
```

`path` 相对当前项目的 `yxg.toml` 所在目录。

**实测流程**：

目录结构：

```
project/
├── app/
│   ├── yxg.toml
│   └── src/Main.yxg
└── mylib/
    └── Greet.yxg
```

`app/yxg.toml`：

```toml
[project]
name = "app"
version = "0.1.0"
entry = "src/Main.yxg"

[dependencies]
greet = { path = "../mylib" }
```

`app/src/Main.yxg`：

```yxg
import "Greet.yxg";

pub void main() {
    Greeter g = new Greeter("YXG");
    g.hello();
}
```

`mylib/Greet.yxg`：

```yxg
pub class Greeter {
    pub string name;
    pub Greeter(string n) { this.name = n; }
    pub void hello() {
        print "你好, ", this.name, "!\n";
    }
}
```

在 `app/` 下执行 `yxg build`，输出：

```
Compilation complete, output file is: ...\app\bin\app.exe
```

运行 `bin/app.exe`：

```
你好, YXG!
```

### Git 依赖

```toml
[dependencies]
mylib = { git = "https://github.com/user/repo", branch = "main" }
```

支持三种版本定位方式：

```toml
# 分支
demo = { git = "https://github.com/user/repo", branch = "main" }

# 标签
demo = { git = "https://github.com/user/repo", tag = "v1.0.0" }

# 提交
demo = { git = "https://github.com/user/repo", commit = "abc1234" }
```

不写版本时默认用 `main` 分支。

### 缓存机制

下载的依赖放在用户主目录下：

```
~/.yxg/cache/
├── demo@main/
└── mylib@v1.0.0/
```

Windows 上是：

```
C:\Users\<你的用户名>\.yxg\cache\
```

目录名格式是 `依赖名@版本`。**目录已存在就跳过下载**——所以第二次编译不会重复 clone。

删掉缓存目录可以强制重新拉取：

```powershell
Remove-Item "$env:USERPROFILE\.yxg\cache\demo@main" -Recurse -Force
```

### 依赖要求

用 git 依赖需要满足：

1. **本地安装了 git**，并且在 `PATH` 里
2. **`~/.yxg/cache` 目录可写**
3. 有网络访问权限

没装 git 会报：

```
error: git clone failed for 'demo'
```

---

## 配置文件查找规则

`yxg build` 会**从当前工作目录一路向上查找** `yxg.toml`：

```
project/
├── yxg.toml        ← 找到这个
└── src/
    └── sub/
```

所以这些位置都能用：

```
cd project         && yxg build    # 项目根目录
cd project/src     && yxg build    # 子目录，向上找到 yxg.toml
cd project/src/sub && yxg build    # 更深也能找到
```

向上找到文件系统根为止。找到后：

- 输出路径按 `cfg.config_dir` 计算，**始终落在项目根下的 `bin/` 里**，不管你在哪个子目录执行
- `src/` 扫描也按 `config_dir` 定位

实测：在 `project/src/sub/` 里执行 `yxg build`，产物仍然是 `project/bin/demo.exe`。

一个 `yxg.toml` 都没找到的话，退化到「必须命令行传源文件」的模式。

---

## 完整示例

一个带本地依赖、关掉断言、输出到 `bin/` 的项目：

```toml
[project]
name = "myserver"
version = "1.2.0"
entry = "src/Main.yxg"

[output]
path = "bin/server.exe"

[build]
flags = "-noassert"

[dependencies]
utils = { path = "../shared/utils" }
netlib = { git = "https://github.com/yxyssg/yxg-libs", tag = "v2.0.0" }
```

编译与运行：

```
cd myserver
yxg build          # 产出 bin/server.exe
yxg run            # 编译并运行
```

---

## 常见问题

### 不写 entry，`yxg build` 报「没有输入文件」

```
fatal error[E9001]: no input files
```

`[project]` 里补上 `entry = "src/Main.yxg"`。

### 找不到 yxg.toml

确认你在这个项目的**任意子目录**里，并且 `yxg.toml` 在某个上级目录中。用 `dir yxg.toml` 逐层往上找。

### git 依赖报 Permission denied

`~/.yxg/cache` 目录没有写权限。检查：

```powershell
Test-Path "$env:USERPROFILE\.yxg\cache" -PathType Container
```

不存在的话手动创建：

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.yxg\cache"
```

### 改了 yxg.toml 但没生效

编译有增量检查。加 `--force` 强制重编：

```
yxg build --force
```

### flags 里写了 -o 但没生效

`[output] path` 优先级高于 `[build] flags` 里的 `-o`。想改输出位置就改 `[output] path`，或者命令行 `-o`。

---

## 配置文件字段总表

| 节 | 键 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| `[project]` | `name` | string | 否 | 项目名 |
| | `version` | string | 否 | 版本号 |
| | `entry` | string | 建议 | 入口源文件路径 |
| `[output]` | `path` | string | 否 | 输出 exe 路径，默认 `output.exe` |
| `[build]` | `flags` | string | 否 | 附加编译参数，空格分隔 |
| `[dependencies]` | `<别名>` | inline table | 否 | 见下 |

依赖表支持的键：

| 键 | 说明 |
|---|---|
| `path` | 本地目录（相对项目根） |
| `git` | 仓库地址，与下面三者之一搭配 |
| `branch` | 分支名，默认 `main` |
| `tag` | 标签名 |
| `commit` | 提交哈希 |

## 继续阅读
下一章学习基本的类型
