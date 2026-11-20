# 调试（2.5 新增）

YXG 2.5 提供三层调试能力：编译期映射、崩溃报告、图形化断点调试。

## 一、编译期调试信息

用 `-g` 编译：

```bash
yxg build app.yxg -o app.exe -g
```

会额外生成 `app.exe.map`，内容形如：

```
# yxg dbgmap v1
# src app.yxg
00002be0 1 _main
00002c0d 2 _main
00002c23 5 _main
# local . 124 alpha
# local . 128 beta
# local . 132 gamma
# local . 136 total
```

- 前三列：机器码地址、源码行号、函数名
- `# local`：局部变量在栈上的偏移和名字（供调试器使用）

**没有 `-g` 就不生成 map，崩溃时也没有源码位置。**

## 二、崩溃报告

用 `-g` 编译的程序，运行时崩溃会自动打印定位信息：

```
*** CRASH ***
  fault address : 0x00D12C0A
  module offset : 0x00002C0A
  source        : crash.yxg:3   (in  deep_int)
  stack:
    0x00D12C4C       crash.yxg:6   (in  middle_int)
```

这意味着**不用调试器也能定位崩溃点**——直接把 exe 发给别人，崩了也能看到是哪个文件哪一行。

示例程序：

```yxg
int deep(int n) {
    int* p = null;
    return *p + n;        // 第 3 行，这里崩溃
}

int middle(int n) { return deep(n) + 1; }   // 第 5 行

pub void main() {
    print "before\n";
    int r = middle(7);
    print "r=", r, "\n";
}
```

输出：

```
before

*** CRASH ***
  source : crash.yxg:3   (in  deep_int)
  stack:
    0x...            crash.yxg:6   (in  middle_int)
```

## 三、VSCode 断点调试

### 安装扩展

```powershell
code --install-extension yxg-0.7.1.vsix --force
```

装完**重启 VSCode**。用 `Ctrl+Shift+X` 搜索 YXG，确认版本是 0.7.1。

### 配置 launch.json

在项目里建 `.vscode/launch.json`：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "yxg",
            "request": "launch",
            "name": "编译并运行当前文件",
            "program": "${file}",
            "cwd": "${workspaceFolder}"
        }
    ]
}
```

也可以按 F5 时选「YXG 程序」，VSCode 会自动生成。

### 下断点

打开 `.yxg` 文件，**点击行号左边的空白处**，出现实心红点即为断点。

> ⚠️ 如果点击没反应，检查扩展版本。0.7.0 之前的版本缺少
> `contributes.breakpoints` 声明，VSCode 不会为自定义语言开放断点边栏。

### 启动调试

按 `F5`。程序会用 `-g` 自动重新编译，然后启动、命中断点并停下。

### 快捷键

| 按键 | 功能 |
|---|---|
| `F5` | 继续运行到下一个断点 |
| `F10` | 步过（不进函数） |
| `F11` | 步入（进入函数） |
| `Shift+F11` | 步出（跑完当前函数） |
| `Shift+F5` | 停止调试 |

### 变量面板

断点命中后，左侧「变量」面板会显示当前栈帧：

```
变量
  ▼ Stack frame
      total  [ebp-136] = 666  (0x0000029a)
      gamma  [ebp-132] = 333  (0x0000014d)
      beta   [ebp-128] = 222  (0x000000de)
      alpha  [ebp-124] = 111  (0x0000006f)
  ▼ Execution
      EIP    = 0x...
      line   = 6
      func   = _main
```

- `Stack frame`：栈上 80 个槽位的值，带源码变量名
- `Execution`：当前指令地址、源码行号、函数名

### 调用栈

左侧「调用堆栈」面板显示函数调用链，点击任一层可以查看该层的变量。

## 四、常见问题

### 断点是灰色空心圈

说明当前文件没有可用的调试器。检查：

1. 右下角语言模式是否是 `YXG`
2. 扩展版本是否 ≥ 0.7.0
3. 项目里是否有 `.vscode/launch.json`

### 断点是实心红点但不停

该行没有对应机器码。常见于：

- 空行、注释行
- 单独一行的 `{` 或 `}`
- 变量声明但没有实际赋值的行

把断点挪到有实际语句的行上。

### 变量面板显示 `.t4` 而不是变量名

说明编译时没有输出局部变量表。确认：

1. 调试时是 `-g` 编译的
2. `app.exe.map` 里有没有 `# local` 行
3. 扩展版本是否 ≥ 0.7.1

### 崩溃报告不显示源码位置

检查编译时是否加了 `-g`，以及 `app.exe.map` 是否和 exe 在同一目录。

## 五、调试信息与性能

- `-g` 只影响生成的 map 文件和崩溃处理器，**不影响代码优化**
- 发布版本建议不加 `-g`，减小体积
- 需要排查线上崩溃时再加

---
