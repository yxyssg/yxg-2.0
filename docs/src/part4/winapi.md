# 实战七：调用 Windows API

这一篇把 FFI 用起来——从加载 DLL、取函数地址，到真正操作窗口、读系统信息、
枚举进程。所有例子都可直接编译运行。

## 第一步：加载 DLL、取函数地址

```yxg
import yxg.Library;

pub void main() {
    Library k32 = Library.open("kernel32.dll");
    if !k32.isValid() {
        print "加载失败: ", k32.lastError(), "\n";
        return;
    }

    int p = k32.sym("GetTickCount");
    if p == 0 {
        print "取符号失败\n";
        k32.close();
        return;
    }

    print "系统已运行(ms) = ", k32.callStdcall0(p), "\n";
    k32.close();
}
```

`GetTickCount` 无参数，所以用 `callStdcall0`。

## 第二步：带参数的调用

`GetSystemMetrics(int nIndex)` 接收一个参数，返回屏幕尺寸：

```yxg
Library u32 = Library.open("user32.dll");
int gm = u32.sym("GetSystemMetrics");

int w = u32.callStdcall1(gm, 0);   // SM_CXSCREEN
int h = u32.callStdcall1(gm, 1);   // SM_CYSCREEN
print "屏幕 = ", w, "x", h, "\n";   // 例如 2560x1440
```

## 第三步：字符串参数

`MessageBoxA(HWND, LPCSTR, LPCSTR, UINT)` 有 4 个参数，其中第 2、3 个是字符串。
用带 `ss` 后缀的变体：

```yxg
Library u32 = Library.open("user32.dll");
int mb = u32.sym("MessageBoxA");
u32.callStdcall4ss(mb, 0, "你好，YXG", "来自 FFI", 0);
```

> 字符串按 UTF-8 传给 `A` 系列 API 会乱码吗？YXG 的 `string` 传给 native 时
> 会自动按当前代码页转换，`A` 系列 API 拿到的就是正确的 GBK 字节。

## 第四步：参数很多——通用调用器

Win32 里有些接口参数很多。比如 `CreateWindowExA` 有 12 个：

```yxg
int cw = u32.sym("CreateWindowExA");
int hwnd = u32.callStdcall12(cw,
    0,                        // dwExStyle
    "STATIC",                 // lpClassName
    "Hello",                  // lpWindowName
    0x80000000,               // dwStyle = WS_POPUP
    100, 100, 400, 200,       // x y w h
    0, 0, 0, 0);              // hWndParent hMenu hInstance lpParam
```

参数个数超过 16、或运行时才知道时，用数组：

```yxg
int[20] args;
args[0] = 0;
args[1] = "STATIC";
// ... 填满需要的个数
int r = u32.callNStdcall(cw, &args[0], 12);
```

注意 `args` 是整型数组，字符串要先转成指针——纯整型参数的 API 才能这样用。

## 第五步：读结构化数据

`GetSystemInfo` 填充一个 `SYSTEM_INFO` 结构体（24 字节）。用数组当缓冲区：

```yxg
int si = k32.sym("GetSystemInfo");
int[8] buf;                 // 32 字节，够放 SYSTEM_INFO
k32.callStdcall1(si, &buf[0]);

// SYSTEM_INFO 布局：
//   +0  wProcessorArchitecture (WORD)
//   +4  dwPageSize
//   +8  lpMinimumApplicationAddress
//   +12 lpMaximumApplicationAddress
//   +16 dwActiveProcessorMask
//   +20 dwNumberOfProcessors
//   +24 dwProcessorType
int pageSize = buf[1];      // 每个 int 是 4 字节，buf[1] = +4
int cpus     = buf[5];      // buf[5] = +20
print "页大小 = ", pageSize, "  处理器数 = ", cpus, "\n";
```

> `int[N]` 数组在内存里是连续的，`&buf[0]` 就是这块缓冲区的起始地址，
> 可以直接交给 C 函数填充。

## 第六步：判断错误

很多 Win32 API 失败时返回 0，错误码在 `GetLastError()`：

```yxg
int lasterr = k32.sym("GetLastError");
int h = k32.sym("OpenProcess");
int proc = k32.callStdcall3(h, 0x1F0FFF, 0, 4);   // 打开 PID 4

if proc == 0 {
    print "打开失败，错误码 = ", k32.callStdcall0(lasterr), "\n";
    // 5 = ERROR_ACCESS_DENIED
}
```

## 常见 API 速查

| API | 参数个数 | 约定 | 用途 |
|---|---|---|---|
| `GetTickCount` | 0 | stdcall | 系统运行毫秒数 |
| `GetSystemMetrics` | 1 | stdcall | 屏幕/系统尺寸 |
| `GetLastError` | 0 | stdcall | 最后错误码 |
| `Sleep` | 1 | stdcall | 休眠毫秒 |
| `MessageBoxA` | 4（2 个 string） | stdcall | 弹窗 |
| `GetSystemInfo` | 1（指针） | stdcall | 系统信息 |
| `CreateWindowExA` | 12 | stdcall | 创建窗口 |
| `printf` | 可变 | **cdecl** | C 运行时输出 |

`cdecl` 和 `stdcall` 的区别：`cdecl` 由**调用者**清栈（可变参数必须是它），
`stdcall` 由**被调用者**清栈。选错不会立刻出错，而是返回后栈失衡崩溃。

## 完整例子：弹窗并显示系统信息

```yxg
import yxg.Library;

pub void main() {
    Library u32 = Library.open("user32.dll");
    Library k32 = Library.open("kernel32.dll");

    int gm = u32.sym("GetSystemMetrics");
    int w = u32.callStdcall1(gm, 0);
    int h = u32.callStdcall1(gm, 1);

    int mb = u32.sym("MessageBoxA");
    u32.callStdcall4ss(mb, 0, "屏幕分辨率已获取", "YXG FFI", 0);

    print "屏幕 = ", w, "x", h, "\n";

    u32.close();
    k32.close();
}
```

## 踩坑清单

| 现象 | 原因 |
|---|---|
| `symbol not found` | stdcall 函数名带 `@N` 后缀，`N` = 参数字节数。`MessageBoxA` 是 `MessageBoxA@16` |
| 取到地址但调用崩 | 参数个数和真实个数不一致 → 栈失衡 |
| 返回后崩 | cdecl/stdcall 选错了 |
| 字符串乱码 | 用了 `W` 系列（宽字符）API，要换 `A` 系列 |
| 崩溃无信息 | 用 `yxg build -g` 编译，崩溃会打印行号 |

## 继续阅读

- [../part2/ffi.md](../part2/ffi.md) —— FFI 完整参考
- [../part2/error-resource.md](../part2/error-resource.md) —— 错误处理
- [../part5/network-file.md](../part5/network-file.md) —— 网络与文件