# 多返回值与可变参数

## 一句话结论

| 特性 | 状态 |
|---|---|
| 返回多个值 | ✅ `(int, string)` |
| 可变返回值 | ✅ `(int...)` |
| 多返回值 + 可变返回混合 | ✅ `(int..., string..., char)` |
| 取值 | ✅ `getReturn(i)` / `getReturn(i, j)` |
| 可变参数 | ✅ `f([1,2,3])` |
| 多个可变参数 | ✅ `f([1,2,3], [4,5,6], op)` |
| 多返回值函数指针 | ✅ |
| 元组 / 解构赋值 | ❌ |
| 变参传数组变量 | ❌ 必须写 `[...]` |

## 多参数

函数参数数量没有限制，类型可以混用：

```yxg
pub int mix(int a, float b, string c, bool d) {
    print a, " ", b, " ", c, " ", d, "\n";
    return a;
}

pub void main() {
    mix(1, 2.5, "hi", true);
}
```

输出：

```
1 2.5 hi true
```

### 参数传值

基本类型传副本：

```yxg
pub void tryChange(int x) {
    x = 999;
}

pub void main() {
    int a = 1;
    tryChange(a);
    print a, "\n";        // 1，没变
}
```

要修改调用方，传指针（见[指针](pointers.md)）。

数组传参共享底层数据，函数内能改元素：

```yxg
pub void bump(int[] arr) {
    arr[0] = arr[0] + 1;
}

pub void main() {
    int[] a = new int[3];
    a[0] = 10;
    bump(a);
    print a[0], "\n";     // 11
}
```

## 多返回值

### 基本语法

返回类型写成括号里的类型列表：

```yxg
pub (int, string) pair() {
    return (123, "aaa");
}

pub void main() {
    int n = pair().getReturn(0);
    string s = pair().getReturn(1);
    print n, " ", s, "\n";
}
```

输出：

```
123 aaa
```

- `return (a, b)` —— 用括号包住多个值
- `expr.getReturn(i)` —— 取第 i 个，**从 0 开始**

### 三个类型

```yxg
pub (int, float, string) triple() {
    return (7, 2.5, "hi");
}

pub void main() {
    print triple().getReturn(0), "\n";     // 7
    print triple().getReturn(1), "\n";     // 2.5
    print triple().getReturn(2), "\n";     // hi
}
```

### 可变返回值

类型后面加 `...`，返回值用 `[...]` 包：

```yxg
pub (int...) many() {
    return ([5, 6, 7]);
}

pub void main() {
    int[] arr = many().getReturn(0);
    print "len = ", arr.length, "\n";      // 3
    print arr[0], " ", arr[1], " ", arr[2], "\n";   // 5 6 7
}
```

`getReturn(0)` 不加第二参数时，返回整个数组。

### 可变返回值取单个元素

加第二个参数指定下标：

```yxg
pub (int...) many() {
    return ([5, 6, 7]);
}

pub void main() {
    print many().getReturn(0, 0), "\n";    // 5
    print many().getReturn(0, 1), "\n";    // 6
    print many().getReturn(0, 2), "\n";    // 7
}
```

### 混合：可变 + 固定

```yxg
pub (int..., string..., char) mixed() {
    return ([1, 2], ["x", "y"], 'Z');
}

pub void main() {
    int[] nums = mixed().getReturn(0);
    string[] strs = mixed().getReturn(1);
    char c = mixed().getReturn(2);

    print "nums len = ", nums.length, "\n";     // 2
    print "strs len = ", strs.length, "\n";     // 2
    print "c = ", c, "\n";                      // Z
}
```

### 规则总结

```
getReturn(i)      第 i 个返回值
  - 如果该位置是可变返回值 → 返回整个数组
  - 如果是固定值 → 直接返回值

getReturn(i, j)   第 i 个返回值里，可变数组的第 j 个元素
  - 只对可变返回值有效
```

## 可变参数

### 基本用法

`...` 紧跟在参数名后面：

```yxg
pub int sum(int a, ...) {
    int s = 0;
    int i = 0;
    while i < a.length {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}

pub void main() {
    print sum([1, 2, 3, 4]), "\n";     // 10
}
```

**关键点**：变参位置收到的是一**个数组**，用 `a.length` 和 `a[i]` 访问。

### 调用时必须写中括号

```yxg
sum([1, 2, 3]);        // ✅ 正确

int[] data = {1, 2, 3};
sum(data);             // ❌ 报错
```

错误信息：

```
error[E3009]: variadic parameter of "sum" requires bracketed groups like [1,2,3]
```

中括号是语法的一部分，用来**标记这一组实参属于哪个 `...`**，不是普通数组。

### 多个可变参数

```yxg
pub int mul(int a, ..., int b, ..., char op) {
    print "a.len = ", a.length, "\n";
    print "b.len = ", b.length, "\n";
    print "op = ", op, "\n";
    return 0;
}

pub void main() {
    mul([1, 2, 3], [4, 5, 6], '+');
}
```

输出：

```
a.len = 3
b.len = 3
op = +
```

每个 `[ ... ]` 对应一个 `...` 位置，顺序一一对应。

### 空变参

```yxg
pub int count(int a, ...) {
    return a.length;
}

pub void main() {
    print count([]), "\n";     // 0
}
```

`[]` 表示一个空组。

### 定参 + 变参混合

```yxg
pub void log(level, string tag, int items, ...) {
    // 这里的 level 和 tag 是普通参数，items 是可变参数
}
```

`...` 只能修饰**紧邻的前一个参数**，所以变参前面可以有任意多个普通参数。

## 函数指针与多返回值

### 多返回值函数指针

```yxg
pub (int, int) pair(int a) {
    return (a, a * 2);
}

pub void main() {
    unsafe (int, int) (*fp)(int) = &pair;
    print fp(5).getReturn(0), "\n";     // 5
    print fp(5).getReturn(1), "\n";     // 10
}
```

### 可变返回值函数指针

```yxg
pub (int...) many(int n) {
    return ([n, n + 1, n + 2]);
}

pub void main() {
    unsafe (int...) (*fp)(int) = &many;
    print fp(10).getReturn(0).length, "\n";     // 3
}
```

### 返回函数指针

```yxg
pub (int, int) pair(int a) {
    return (a, a * 2);
}

pub (int, int) (*)(int) pick() {
    return &pair;
}

pub void main() {
    print pick()(7).getReturn(1), "\n";     // 14
}
```

## 不支持的部分

### 元组和解构

没有独立的元组类型，也没有解构赋值：

```yxg
// ❌ 这些都不行
(int, int) t = (1, 2);
(int q, int r) = divmod(17, 5);
```

只能用 `getReturn` 一个个取：

```yxg
int q = divmod(17, 5).getReturn(0);
int r = divmod(17, 5).getReturn(1);
```

**注意**：每次调用 `divmod()` 都会重新执行函数。要避免重复计算，先存到变量：

```yxg
// 需要多次取值时，这样不行——每次都重新调用
print divmod(17, 5).getReturn(0);
print divmod(17, 5).getReturn(1);

// 如果函数有副作用（如打印），会执行两遍
```

目前的写法只能接受这一点，或者改用「返回数组 / 返回类」的方案（见下）。

### 变参传数组变量

```yxg
int[] data = {1, 2, 3};
sum(data);        // ❌ E3009
```

必须写 `sum([1, 2, 3])`。

需要动态数量的参数时，改用普通数组参数：

```yxg
pub int sumArr(int[] nums) {
    int s = 0;
    int i = 0;
    while i < nums.length {
        s = s + nums[i];
        i = i + 1;
    }
    return s;
}

pub void main() {
    int[] data = {1, 2, 3, 4};
    print sumArr(data), "\n";     // 10
}
```

## 替代方案

### 返回数组

同类型多个值时，也可以直接返回数组：

```yxg
pub int[] divmod(int a, int b) {
    int[] res = new int[2];
    res[0] = a / b;
    res[1] = a % b;
    return res;
}

pub void main() {
    int[] r = divmod(17, 5);
    print r[0], " ", r[1], "\n";     // 3 2
}
```

好处：可以存到变量里反复用。坏处：元素必须同类型。

### 返回类

类型混合且需要反复访问时：

```yxg
pub class DivResult {
    pub int quotient;
    pub int remainder;
    pub bool ok;
}

pub DivResult divmodSafe(int a, int b) {
    DivResult r = new DivResult();
    if b == 0 {
        r.ok = false;
        return r;
    }
    r.quotient = a / b;
    r.remainder = a % b;
    r.ok = true;
    return r;
}

pub void main() {
    DivResult a = divmodSafe(17, 5);
    print a.quotient, " ", a.remainder, "\n";     // 3 2
}
```

### 指针 out 参数

C 风格，适合修改已有变量。详见[指针](pointers.md)。

```yxg
pub unsafe void divmod(int a, int b, int* q, int* r) {
    *q = a / b;
    *r = a % b;
}

pub void main() {
    int q = 0;
    int r = 0;
    divmod(17, 5, &q, &r);
    print q, " ", r, "\n";     // 3 2
}
```

## 方案对比

| 方案 | 适合 | 不适合 |
|---|---|---|
| 多返回值 `(T1,T2)` | 2~5 个值，一次取值 | 需要反复访问同一结果 |
| 可变返回值 `(T...)` | 数量不定 | 元素类型不一致 |
| 返回数组 | 同类型多个值，可复用 | 类型混合 |
| 返回类 | 类型混合，语义清晰 | 只返回两三个数时略重 |
| 指针 out | 修改已有变量 | 浮点场景（`float*` 现在支持了） |

## 常见错误

### 忘记写中括号

```
error[E3009]: variadic parameter of "sum" requires bracketed groups like [1,2,3]
```

改成 `sum([1, 2, 3])`。

### getReturn 下标越界

```
error[E3007]: array index 3 out of bounds (size 3)
```

`getReturn` 下标从 0 开始，3 个返回值只能取 0~2。

### 在非多返回值函数上用 getReturn

```
error[E3003]: getReturn() called on non-multi-return function
```

只有声明成 `(T1, T2)` 形式的函数才能用。

## 继续阅读

- [指针](pointers.md)：`&`、`*`、多级指针、函数指针
- [函数](functions.md)：参数与返回值基本规则
- [数组](arrays.md)：值语义与堆数组
