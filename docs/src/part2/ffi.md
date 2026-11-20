# 外部函数接口（FFI）

YXG 可以直接调用系统 DLL 里导出的 C 函数——Win32 API、msvcrt、任何第三方库都行。

## 三个关键字

| 关键字 | 作用 |
|---|---|
| `link "xxx.dll";` | 告诉链接器要依赖哪个库（自动转成 `-lxxx`） |
| `native` | 声明这个函数没有 YXG 函数体，实现在外部 |
| `stdcall` | 这个函数用 Windows 的 stdcall 调用约定（绝大多数 Win32 API 都是） |

## 例子：调用 MessageBoxA

```yxg
link "user32.dll";

pub native stdcall int MessageBoxA(int hwnd, string text, string caption, int type);

pub void main() {
    MessageBoxA(0, "hello", "YXG FFI", 0);
}
```

编译运行：

```bash
yxg run hello.yxg
```

## 例子：调用 C 运行时（cdecl）

msvcrt 是默认链接的，不需要 `link`，也不需要 `stdcall`：

```yxg
pub native int puts(string s);

pub void main() {
    puts("from libc via FFI");
}
```

## 什么时候要写 `stdcall`

**Win32 API 几乎全是 stdcall**，必须写。C 运行时函数（`puts`、`malloc`、`strlen`）是 cdecl，不写。

判断方法：函数在 `windows.h` 里声明时带 `WINAPI` / `APIENTRY` / `CALLBACK` 的，就是 stdcall。

## 类型对应

| YXG 类型 | C 类型 |
|---|---|
| `int` | `int` / `DWORD` / `BOOL` / `HWND`（指针类） |
| `string` | `char*` / `LPCSTR` |
| `float` | `float` |
| `bool` | `bool`（1 字节，**不要**用于 Win32 的 `BOOL`） |
| `int*` | `int*` |

> ⚠️ Win32 的 `BOOL` 是 4 字节 int，用 `int` 接，别用 `bool`。

### 返回值

| C 返回 | YXG 用 | 说明 |
|---|---|---|
| `int` / `DWORD` / 句柄 / 指针 | `int` | 32 位下都是 4 字节 |
| `int64` / `unsigned __int64` | `int64` | 走 `EDX:EAX`，实测正常 |
| `float` / `double` | `float` | 浮点返回值走 x87 `ST0`，`atof("3.25")` → `3.25` |
| `char*` | **`int`**，再用 `Library.cstr` 转 | 见下节，不能直接声明成 `string` |
| `void` | `void` | |

### `char*` 返回值：不要直接声明成 `string`

`string` 在 YXG 里是**带长度头的指针**，而 C 返回的 `char*` 只是 NUL 结尾的裸指针。
直接写 `pub native string getenv(string);` 编译能过、`print` 甚至看着正常，但
`String.len()` 会读到垃圾：

```yxg
import yxg.String;

pub native string getenv(string name);

pub void main() {
    string p = getenv("USERNAME");
    print p, "\n";              // 看着是 yxy
    print String.len(p), "\n";  // 1027951937  ← 垃圾
}
```

正确做法：声明成 `int`（拿地址），再用 `Library.cstr(addr)` 逐字节转成 YXG 字符串。

```yxg
import yxg.Library;
import yxg.String;

pub native int getenv(string name);     // 返回 char*，用 int 接

pub void main() {
    string name = Library.cstr(getenv("USERNAME"));
    print "len = ", String.len(name), "\n";  // 3
    print "value = [", name, "]\n";        // [yxy]
}
```

`Library.cstr(int addr)` 遇 `0` 停（上限 8192 字节），`addr == 0` 返回 `""`。
自己写的话也可以直接走指针，但**要用下标，不要用 `*p` 和 `p + 1`**（实测那种写法在循环里会崩）：

```yxg
pub static unsafe string cstr(int addr) {
    char* p = addr as char*;
    string r = "";
    int i = 0;
    while i < 8192 {
        char c = p[i];          // ✓ 下标访问
        if c == 0 { break; }
        r = r + c;
        i = i + 1;
    }
    return r;
}
```

## 常见 API 速查

```yxg
link "user32.dll";
link "kernel32.dll";

pub native stdcall int MessageBoxA(int hwnd, string text, string caption, int type);
pub native stdcall void Sleep(int ms);
pub native stdcall int MessageBeep(int type);
pub native stdcall int GetSystemMetrics(int index);
```

```yxg
pub void main() {
    Sleep(300);
    MessageBeep(0);
    int w = GetSystemMetrics(0);   // 屏幕宽
    print "screen width = ", w, "\n";
}
```

`GetTickCount()`、`GetLastError()` 这类零参数 API 也可以直接声明调用。

## 找不到符号怎么办

链接时报 `undefined symbol: XXX`，按这个顺序排查：

1. **漏了 `link`** —— 该 API 所在 DLL 没声明
2. **漏了 `stdcall`** —— 名字对不上（stdcall 的真实符号是 `_Name@字节数`）
3. **参数个数不对** —— stdcall 的符号名末尾带参数总字节数，写错参数就找不到

第 3 点很隐蔽：`MessageBoxA` 有 4 个参数 = 16 字节，所以真实符号是 `_MessageBoxA@16`。参数写错就变成 `@12`，链接器自然找不到。


## 类里的 `native` 方法

`native` 也可以写在类里面（标准库的 `Window`、`Socket` 都是这么做的）：

```yxg
pub class Cursor {
    pub int x;
    pub int y;
    pub native int move(int dx, int dy);   // 没有函数体
}
```

但要注意：**类内 `native` 方法带隐藏的 `this` 参数，符号名是 `类名_方法名`**，
不是 DLL 的原始导出名。拿它直接绑 DLL 会在链接阶段报：

```
output.asm:118: error: symbol `Tick_GetTickCount' not defined
```

所以：

| 想做的事 | 该用什么 |
|---|---|
| 调 DLL 导出函数 | 模块级 `pub native` 函数（`link` + `stdcall`），或 `Library` 动态调用 |
| 给自己的类加一个汇编实现的方法 | 类内 `native`，并自己导出 `类名_方法名` 符号（写 `runtime/*.asm`） |

标准库就是这么分工的：`lib/yxg/Window.yxg` 里声明 `pub native int is_key_down(int key);`，
实现在 `runtime/runtime_window.asm` 里导出 `Window_is_key_down`。
## 动态加载（运行时才知道 DLL / 函数名）

静态声明要求编译时就知道符号。需要运行时加载时用 `yxg.Library`：

```yxg
import yxg.Library;

pub void main() {
    Library lib = Library.open("user32.dll");
    if !lib.isValid() {
        print "加载失败, err=", lib.lastError(), "\n";
        return;
    }

    int addr = lib.sym("MessageBoxA");   // GetProcAddress
    print "MessageBoxA @ ", addr, "\n";

    // stdcall 调用，4 个参数，其中第 3、4 个是 string
    lib.callStdcall4ss(addr, 0, "hello", "YXG", 0);

    lib.close();
}
```

实测输出：

```
user32.dll loaded
MessageBoxA @ 1985379488
```

### 调用接口一览

| 方法 | 说明 |
|---|---|
| `callCdecl0` ~ `callCdecl16` | cdecl 调用，0~16 个 int 参数 |
| `callStdcall0` ~ `callStdcall16` | stdcall 调用，0~16 个 int 参数 |
| `callStdcall4ss` | stdcall，第 3、4 个参数是 `string` |
| `callStdcall6s` / `6ss` / `8ss` | stdcall，指定位置为 `string` |
| `callNStdcall(addr, &args[0], n)` | **任意参数个数**（通用调用器） |
| `callNCdecl(addr, &args[0], n)` | **任意参数个数**（通用调用器） |
| `messageBox(hwnd, text, caption, type)` | `MessageBoxA` 快捷方式 |

> ⚠️ **这是 unsafe 区域**：地址错、参数个数错、调用约定错，都会直接
> `0xC0000005` 崩溃，编译器不做任何检查。开了 `-g` 编译的话，崩溃时能看到
> 具体行号。

### Library 类完整接口

**加载与符号**

| 方法 | 返回 | 说明 |
|---|---|---|
| `Library.open(path)` | `Library` | 加载 DLL（走 `LoadLibraryA`） |
| `lib.isValid()` | `bool` | 是否加载成功 |
| `lib.lastError()` | `int` | 最后一次错误的 `GetLastError()` 值 |
| `lib.sym(name)` | `int` | 取函数地址（`GetProcAddress`），失败返回 0 |
| `lib.handleValue()` | `int` | 原始 `HMODULE` 句柄 |
| `lib.close()` | `void` | 卸载 DLL（`FreeLibrary`） |

**调用封装**

| 方法 | 参数 |
|---|---|
| `callCdecl0(addr)` | 无 |
| `callCdecl1(addr, a1)` ~ `callCdecl16` | 1~16 个 int |
| `callStdcall0(addr)` | 无 |
| `callStdcall1` ~ `callStdcall16` | 1~16 个 int |
| `callStdcall4ss(addr, a1, s2, s3, a4)` | 第 3、4 个是 `string` |
| `callStdcall6s(addr, a1, a2, s3, a4, a5, a6)` | 第 3 个是 `string` |
| `callStdcall6ss(...)` | 第 3、5 个是 `string` |
| `callStdcall8ss(...)` | 第 3、5 个是 `string` |
| `callNStdcall(addr, argptr, count)` | 任意个数，stdcall |
| `callNCdecl(addr, argptr, count)` | 任意个数，cdecl |
| `messageBox(hwnd, text, caption, type)` | `MessageBoxA` 快捷方式 |

**完整示例：动态调用并检查错误**

```yxg
import yxg.Library;

pub void main() {
    Library k32 = Library.open("kernel32.dll");
    if !k32.isValid() {
        print "kernel32 加载失败: ", k32.lastError(), "\n";
        return;
    }

    int p = k32.sym("GetTickCount");
    if p == 0 {
        print "找不到 GetTickCount: ", k32.lastError(), "\n";
        k32.close();
        return;
    }

    // GetTickCount 是 stdcall、零参数
    int tick = k32.callStdcall0(p);
    print "ticks = ", tick, "\n";

    // 传参示例：GetSystemMetrics(int nIndex)
    int p2 = k32.sym("GetSystemMetrics");
    if p2 != 0 {
        int screenW = k32.callStdcall1(p2, 0);
        int screenH = k32.callStdcall1(p2, 1);
        print "screen = ", screenW, "x", screenH, "\n";
    }

    k32.close();
}
```

**注意**：`GetSystemMetrics` 实际在 `user32.dll`，上面写 `kernel32` 只为演示流程；
真实使用时 `sym` 会返回 0，此时按错误分支处理。

### 为什么需要 callStdcall / callCdecl 之分

- **cdecl**（C 运行时）：被调用者返回后，**调用者**清栈
- **stdcall**（Win32 API）：被调用者自己清栈（`ret N`）

选错的话栈会失衡，第一次调用可能看起来正常，返回后立刻崩。

## 实现说明

- `stdcall` 函数的内部符号名是 `_Name@(参数个数×4)`
- 生成汇编时会自动加一行 `%define Name _Name@N`，让调用点写的名字能解析到正确符号
- `link "a.dll"` 会去掉路径和 `.dll` 后缀，转成 `-la`

## 任意参数个数（通用调用器）

定长版本到 16 个参数，实际已经够用。但如果你遇到参数更多的接口，
或者参数个数到运行时才能确定，用通用调用器：把参数放进 `int` 数组，
传数组地址和个数。

```yxg
import yxg.Library;

pub void main() {
    Library lib = Library.open("tffi.dll");
    int addr = lib.sym("sc13@52");

    // 准备参数：args[0] 是第一个参数
    int[20] args;
    int i = 0;
    while i < 13 {
        args[i] = 1;
        i = i + 1;
    }

    // 注意：必须用 &args[0] 取地址，不能直接写 args
    int r = lib.callNStdcall(addr, &args[0], 13);
    print "结果 = ", r, "\n";

    lib.close();
}
```

| 方法 | 说明 |
|---|---|
| `callNStdcall(int addr, int argptr, int count)` | stdcall，被调用者清栈 |
| `callNCdecl(int addr, int argptr, int count)` | cdecl，运行时清栈 |

入栈顺序由运行时自动倒排（`args[0]` 最后压栈，即第一个参数）。

### 实测

用测试 DLL（13/16 参数的加权校验和函数）验证：

```
1 stdcallN 13 = 91   want 91      ✓
2 stdcallN 16 = 136  want 136     ✓
3 cdeclN  13 = 91   want 91      ✓
4 cdeclN  16 = 136  want 136     ✓
```

### 为什么不直接传数组

YXG 的数组名不是指针，`int p = args;` 会编译报错。必须取元素地址：

```yxg
int p = &args[0];     // ✓
print *p;             // → args[0]
```

指针算术按元素缩放（`p + 4` 前进 16 字节），
所以运行时内部用 `count*4` 做字节偏移遍历数组。

### 通用调用器的限制

- **只能传 4 字节整型**。`int64`/`double`/结构体要用定长版本。
- **`string` 参数要先转指针**。通用调用器只接受 `int` 数组。
- **参数个数必须和目标函数真实参数个数一致**，否则栈失衡立刻崩溃。

---

## 已知限制

- 不支持回调（把 YXG 函数指针传给 C）
- 定长版本参数按 4 字节计算堆栈，`int64`/`double` 参数需拆成两个 `int` 或用通用调用器
- 返回 `char*` 的函数不要声明成 `string`，用 `int` + `Library.cstr` 转
- 通用调用器只支持 4 字节整型参数

## 继续阅读

- [graphics.md](../part5/graphics.md) —— 图形库
- [../part2/functions.md](../part2/functions.md) —— 函数定义
