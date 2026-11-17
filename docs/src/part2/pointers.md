# 指针

## 指针是什么

指针是**存地址的变量**。普通变量存值，指针存「值放在哪」。

```yxg
pub void main() {
    int x = 42;
    int* p = &x;        // p 存 x 的地址
    print "*p = ", *p, "\n";   // 42，通过地址读回值
}
```

两个符号：

| 符号 | 名字 | 作用 |
|---|---|---|
| `&` | 取地址 | 拿到变量的内存地址 |
| `*` | 解引用 | 按地址读取或写入那个位置 |

## `unsafe` 是可选的

指针语法不需要 `unsafe`，写了也不影响：

```yxg
// 完全等价
unsafe int* p = &x;
int* p = &x;
```

文档里用 `unsafe` 只是提醒「这段在操作裸内存」。

## 基础用法

```yxg
pub void main() {
    int x = 42;
    unsafe int* p = &x;

    print "x = ", x, "\n";       // 42
    print "*p = ", *p, "\n";     // 42  读

    *p = 100;                    // 写
    print "改后 x = ", x, "\n";  // 100  ← x 真的变了
}
```

## 多级指针

```yxg
pub void main() {
    int x = 7;
    unsafe int*   p   = &x;
    unsafe int**  pp  = &p;
    unsafe int*** ppp = &pp;

    print "x=", x, " *p=", *p, " **pp=", **pp, " ***ppp=", ***ppp, "\n";

    ***ppp = 99;
    print "改后 x=", x, "\n";
}
```

输出：

```
x=7 *p=7 **pp=7 ***ppp=7
改后 x=99
```

每一层 `*` 剥一层指针。

## 指针作函数参数

### 修改调用方的变量

```yxg
pub unsafe void increment(int* p) {
    *p = *p + 1;
}

pub void main() {
    int a = 10;
    increment(&a);
    increment(&a);
    print "a = ", a, "\n";     // 12
}
```

### 多返回值：out 参数

```yxg
pub unsafe void divmod(int a, int b, int* q, int* r) {
    *q = a / b;
    *r = a % b;
}

pub void main() {
    int q = 0;
    int r = 0;
    divmod(17, 5, &q, &r);
    print "商=", q, " 余=", r, "\n";     // 商=3 余=2
}
```

### 交换两个变量

```yxg
pub unsafe void swap(int* a, int* b) {
    int t = *a;
    *a = *b;
    *b = t;
}

pub void main() {
    int x = 1;
    int y = 2;
    swap(&x, &y);
    print x, " ", y, "\n";     // 2 1
}
```

### float 指针

`float*` 作参数也正常工作：

```yxg
pub unsafe float readF(float* p) {
    return *p;
}

pub unsafe void setF(float* p, float v) {
    *p = v;
}

pub void main() {
    float f = 2.5;
    unsafe float* pf = &f;

    print "读回 = ", readF(pf), "\n";     // 2.5
    setF(pf, 9.5);
    print "写回 = ", f, "\n";             // 9.5
}
```

## 指针作返回值

```yxg
pub unsafe int* passthrough(int* p) {
    return p;
}

pub void main() {
    int a = 12;
    unsafe int* q = passthrough(&a);
    print "*q = ", *q, "\n";     // 12
}
```

**不要返回局部变量的地址**，函数返回后栈帧销毁：

```yxg
// ❌ 返回后 local 已失效
pub unsafe int* bad() {
    int local = 5;
    return &local;
}
```

## 指针运算

### 加减整数

```yxg
pub void main() {
    int[] buf = new int[10];
    unsafe int* p = buf;

    p[0] = 111;
    p[1] = 222;
    print "p[0]=", p[0], " p[1]=", p[1], "\n";

    print "p+1 与 p 的差值 = ", (p + 1) - p, "\n";    // 4
    print "p+4 与 p 的差值 = ", (p + 4) - p, "\n";    // 16
}
```

**注意**：`p + 1` 移动的字节数取决于元素大小（`int` 是 4），但**两个指针相减返回的是字节差**，不是元素个数：

```yxg
unsafe int* q = p + 2;
print q - p, "\n";          // 8，不是 2
print (q - p) / 4, "\n";    // 2，要自己除
```

### 下标访问

```yxg
unsafe int* p = buf;
p[0] = 1;
p[1] = 2;
```

和 `*(p + 1)` 等价。

### 批量填充

```yxg
pub unsafe void fill(int* p, int n, int v) {
    int i = 0;
    while i < n {
        *(p + i) = v;
        i = i + 1;
    }
}

pub void main() {
    int[] buf = new int[5];
    fill(buf, 5, 7);

    int i = 0;
    while i < 5 {
        print buf[i], " ";
        i = i + 1;
    }
    print "\n";                // 7 7 7 7 7
}
```

## 类指针

```yxg
pub class Node {
    pub int val;
    pub int next;
}

pub void main() {
    Node n = new Node();
    n.val = 42;

    unsafe Node* pn = &n;
    print "(*pn).val = ", (*pn).val, "\n";     // 42
    print "pn->val = ", pn->val, "\n";         // 42

    pn->val = 99;
    print "n.val = ", n.val, "\n";             // 99
}
```

**读取和写入**都支持两种写法：`(*pn).field` 和 `pn->field`。

```yxg
(*pn).val = 10;      // 等价
pn->val = 20;        // 等价
```


## 各类型指针

| 类型 | 声明 | 解引用 | 作参数 |
|---|---|---|---|
| `int*` | ✅ | ✅ | ✅ |
| `int**` / `int***` | ✅ | ✅ | ✅ |
| `int64*` | ✅ | ✅ | ✅ |
| `char*` | ✅ | ✅ | ✅ |
| `bool*` | ✅ | ✅ | ✅ |
| `float*` | ✅ | ✅ | ✅ |
| `Node*`（类） | ✅ | ✅ | ✅ |

### 指针与整数互转

地址可以存成 `int` 再转回来：

```yxg
pub void main() {
    int a = 11;
    unsafe int* p = &a;

    int addr = p as int;
    print "地址 = ", addr, "\n";

    unsafe int* q = addr as int*;
    print "解回 = ", *q, "\n";     // 11
}
```

## 函数指针

### 声明与调用

```yxg
pub int add(int a, int b) { return a + b; }

pub void main() {
    unsafe int (*fp)(int, int) = &add;
    print fp(3, 4), "\n";      // 7
}
```

语法：`返回类型 (*变量名)(参数类型列表)`。

### 作参数（回调）

```yxg
pub int add(int a, int b) { return a + b; }
pub int sub(int a, int b) { return a - b; }

pub unsafe int apply(int (*f)(int, int), int x, int y) {
    return f(x, y);
}

pub void main() {
    print apply(&add, 4, 5), "\n";     // 9
    print apply(&sub, 4, 5), "\n";     // -1
}
```

### 多个不同签名的函数指针

```yxg
pub int addInt(int a, int b) { return a + b; }
pub float addFloat(int a, float b) { return a as float + b; }

pub unsafe int test1(int (*p)(int, int), int (*r)(int, float)) {
    return p(1, 2) + (r(3, 4.0) as int);
}

pub void main() {
    print test1(&addInt, &addFloat), "\n";     // 6
}
```

### 返回函数指针

返回类型部分写完整的函数指针类型：

```yxg
pub int test(int d) { return d; }

pub int (*)(int) a() {
    return &test;
}

pub void main() {
    print a()(123), "\n";      // 123
}
```

`a()` 返回一个 `int(*)(int)`，后面直接跟 `(123)` 就是调用它。

### 函数指针数组

```yxg
pub int add(int a, int b) { return a + b; }
pub int sub(int a, int b) { return a - b; }

pub void main() {
    unsafe int (*ops[2])(int, int);
    ops[0] = &add;
    ops[1] = &sub;

    print ops[0](10, 3), " ", ops[1](10, 3), "\n";     // 13 7
}
```

典型用法——命令分发表：

```yxg
pub int inc(int x) { return x + 1; }
pub int dec(int x) { return x - 1; }
pub int dbl(int x) { return x * 2; }

pub void main() {
    unsafe int (*fns[3])(int);
    fns[0] = &inc;
    fns[1] = &dec;
    fns[2] = &dbl;

    int i = 0;
    int v = 10;
    while i < 3 {
        v = fns[i](v);
        print "step", i, " -> ", v, "\n";
        i = i + 1;
    }
}
```

输出：

```
step0 -> 11
step1 -> 10
step2 -> 20
```

### 嵌套函数指针参数

参数本身也可以是「接受函数指针的函数指针」：

```yxg
pub int test(int d) { return d; }

pub int a(int (*d)(int)) {
    return d(123);
}

pub int b(int (*p)(int (*r)(int))) {
    return p(&test);
}

pub void main() {
    print b(&a), "\n";      // 123
}
```

`b` 的参数 `p` 是「接受 `int(*)(int)` 返回 `int`」的函数指针，传进去的 `a` 正好匹配。

### 多返回值函数指针

```yxg
pub (int, int) pair(int a) {
    return (a, a * 2);
}

pub void main() {
    unsafe (int, int) (*fp)(int) = &pair;
    print fp(5).getReturn(0), " ", fp(5).getReturn(1), "\n";     // 5 10
}
```

## 函数指针能力总表

| 用法 | 示例 | 状态 |
|---|---|---|
| 声明与调用 | `int (*fp)(int)` | ✅ |
| 作参数 | `f(int (*p)(int))` | ✅ |
| 多个不同签名 | `f(int(*a)(int), int(*b)(float))` | ✅ |
| 返回函数指针 | `int (*)(int) f()` | ✅ |
| 函数指针数组 | `int (*ops[2])(int,int)` | ✅ |
| 嵌套函数指针参数 | `f(int(*p)(int(*)(int)))` | ✅ |
| 多返回值函数指针 | `(int,int)(*fp)(int)` | ✅ |
| 可变返回值函数指针 | `(int...)(*fp)(int)` | ✅ |
| 返回多返回值函数指针 | `(int,int)(*)(int) f()` | ✅ |

## 完整示例：手写数组反转

```yxg
pub unsafe void reverse(int* p, int n) {
    int i = 0;
    int j = n - 1;
    while i < j {
        int t = p[i];
        p[i] = p[j];
        p[j] = t;
        i = i + 1;
        j = j - 1;
    }
}

pub void main() {
    int[] buf = new int[6];
    int i = 0;
    while i < 6 {
        buf[i] = i + 1;
        i = i + 1;
    }

    print "原数组:";
    i = 0;
    while i < 6 {
        print " ", buf[i];
        i = i + 1;
    }
    print "\n";

    reverse(buf, 6);

    print "反转后:";
    i = 0;
    while i < 6 {
        print " ", buf[i];
        i = i + 1;
    }
    print "\n";
}
```

输出：

```
原数组: 1 2 3 4 5 6
反转后: 6 5 4 3 2 1
```

## 什么时候该用指针

| 场景 | 建议 |
|---|---|
| 让函数修改调用方的变量 | ✅ 用 `T*` 参数 |
| 返回多个值 | ✅ out 参数，或[多返回值](variadic.md) |
| 回调 / 分发表 | ✅ 函数指针 |
| 遍历数组 | ⚠️ 数组下标更安全 |
| 访问类成员 | ✅ `pn->field` 可用，但直接用对象更简单 |

## 常见错误

### 解引用非指针

```
error[E3003]: cannot dereference non-pointer type
```

`*x` 里 `x` 不是指针。

### 对 null 指针解引用

```
Error: null pointer dereference
```

程序直接终止。

### 判断 null

`== null` 和 `!= null` 都正常工作：

```yxg
pub class Box { pub int v; }

pub void main() {
    Box b;
    if b == null {
        print "b 是 null\n";
    }
}
```

### 返回栈上变量的地址

函数返回后栈帧销毁，指针悬空。

### 指针数组写法

```yxg
unsafe int* arr[2];        // ❌ 不支持，会报 cannot dereference non-pointer type
```

用数组然后取指针：

```yxg
int[] buf = new int[2];
unsafe int* p = buf;       // ✅
```

## 继续阅读

- [多返回值与可变参数](variadic.md)：函数返回多个值的各种方案
- [数组](arrays.md)：值语义与堆数组
