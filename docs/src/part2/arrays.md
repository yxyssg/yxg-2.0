# 数组

## 数组声明与初始化

数组是一组相同类型元素的集合。声明数组的语法：

    类型[] 数组名;

### 静态初始化

在声明时用花括号提供初始值：

```yxg
pub void main() {
    int[] numbers = {10, 20, 30, 40, 50};
    float[] scores = {95.5, 88.0, 76.5};
    string[] names = {"Alice", "Bob", "Charlie"};
    bool[] flags = {true, false, true};
}
```

方括号里可以留空（大小由初值个数决定），也可以显式写长度：

```yxg
int[] a = {1, 2, 3};        // 长度 3
int b[3] = {1, 2, 3};       // 长度 3
```

### 混合字面量

`float` / `float64` 数组里可以混写整数和浮点字面量，整数会自动转成浮点：

```yxg
float[] scores = {123, 123.5, 150, 90};
print scores[0], " ", scores[1], " ", scores[2], " ", scores[3], "\n";
// 123.0 123.5 150.0 90.0
```

变量也可以放进初始化列表，同样会做类型转换：

```yxg
int x = 7;
float[] a = {x, 2.0};
print a[0], " ", a[1], "\n";       // 7.0 2.0
```

### 64 位元素

`int64` / `float64` 数组一样支持初始化列表：

```yxg
int64[] big = {1000000000000, 2000000000000};
float64[] dbl = {1.25, 2.5};

print big[0], " ", big[1], "\n";       // 1000000000000 2000000000000
print dbl[0], " ", dbl[1], "\n";       // 1.25 2.5
```

### 全局数组

全局数组同样支持初始化列表：

```yxg
pub int64   gi64[] = {1, 2};
pub float64 gf64[] = {1.5, 2.5};
pub float   gf[]   = {1.5, 2.5};
pub int     gn[]   = {1, 2, 3};

pub void main() {
    print gi64[0], " ", gf64[0], " ", gf[1], " ", gn[2], "\n";
    // 1 1.5 2.5 3
}
```

### 空数组

花括号留空得到长度为 0 的数组，等价于 `new T[0]`：

```yxg
pub void main() {
    int[] a = {};
    float[] b = {};
    string[] c = {};

    print a.length, " ", b.length, " ", c.length, "\n";   // 0 0 0
}
```

**数组不能追加元素。** YXG 的数组是定长的，没有 `push`/`append`，也不能用 `+` 往数组里加东西：

```yxg
int[] a = {};
a = a + 1;     // 错误[E3003]: array cannot be used with '+' or '-': fixed-size arrays have no append
```

需要不定长容器时用 `List<T>`：

```yxg
import yxg.Collections.List;

pub void main() {
    List<int> l = new List<int>();
    l.add(1);
    l.add(2);
    print l.size(), "\n";      // 2
}
```

### 动态分配

使用 new 关键字创建指定大小的数组：

```yxg
pub void main() {
    int[] arr = new int[10];
    float[] floats = new float[5];
    string[] strings = new string[3];

    int size = 25;
    int[] dynamic = new int[size];

    int64[] big = new int64[2];
    big[0] = 5;
    big[1] = 6;
    print big[0], " ", big[1], "\n";       // 5 6
}
```

### 声明与赋值分离

声明数组时必须同时给出大小或初值，`int[] arr;` 这样光声明会报 `error[E3003]: array 'arr' of unknown size must be initialized`。数组字面量也只能出现在声明语句里，不能单独用来赋值：

```yxg
pub void main() {
    int[] arr = new int[5];

    // arr = {1, 2, 3, 4, 5};   // 错误！error[E2004] illegal operand '{'

    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;

    print arr[0], " ", arr[1], " ", arr[2], "\n";
}
```

输出：

```
1 2 3
```

要一次性给出初值，就在声明的时候写：

```yxg
pub void main() {
    int[] arr = {1, 2, 3, 4, 5};
    print arr.length, "\n";       // 5
}
```

## 访问元素

通过方括号和索引访问数组元素，索引从 0 开始：

    int main() {
        int[] arr = {10, 20, 30, 40, 50};

        int first = arr[0];
        int third = arr[2];
        int last = arr[4];

        arr[0] = 100;
        arr[1] = arr[1] + 5;

        print first, " ", third, " ", last, "\n";
        print arr[0], " ", arr[1], "\n";

        return 0;
    }

输出：

    10 30 50
    100 25

索引必须是整数类型。负数索引到运行时才报错：

    int main() {
        int[] arr = {1, 2, 3};
        // int x = arr[-1];      // 运行时错误：Error: Array index out of bounds
        // int y = arr[100];     // 编译期直接报错 error[E3007] array index 100 out of bounds
        return 0;
    }

## 获取数组长度

使用 .length 属性获取数组的元素个数：

    int main() {
        int[] arr = {10, 20, 30, 40, 50};

        int len = arr.length;
        print "数组长度: ", len, "\n";

        for int i = 0; i < arr.length; i++ {
            print "arr[", i, "] = ", arr[i], "\n";
        }

        return 0;
    }

输出：

    数组长度: 5
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    arr[3] = 40
    arr[4] = 50

.length 返回的是 int 类型，可以参与运算：

    int main() {
        int[] a = {1, 2, 3};
        int[] b = {4, 5};
        int total = a.length + b.length;
        print total, "\n";             // 5
        return 0;
    }

## 多维数组

### 声明与初始化

    int main() {
        int[][] matrix = {
            {1, 2, 3},
            {4, 5, 6},
            {7, 8, 9}
        };

        int[][][] cube = {
            {{1, 2}, {3, 4}},
            {{5, 6}, {7, 8}}
        };

        return 0;
    }

### 动态分配多维数组

多维数组用 `new int[行][列]` 一次分配：

    int main() {
        int[][] matrix = new int[3][4];
        matrix[0][0] = 5;

        print matrix.length, " ", matrix[0].length, "\n";    // 12 4
        return 0;
    }

输出：

    12 4

注意 `new int[3][4]` 的 `.length` 是 12（元素总数），不是 3（行数）。

不支持不规则数组（jagged array），`new int[3][]` 这种写法会报 `error[E3007]: array dimension must be non-negative`。

### 访问多维数组

    int main() {
        int[][] matrix = {
            {1, 2, 3},
            {4, 5, 6},
            {7, 8, 9}
        };

        int elem = matrix[1][2];
        matrix[0][0] = 99;

        int total = matrix.length;       // 9：元素总数
        int cols = matrix[0].length;     // 3：列数
        int rows = total / cols;         // 行数要自己算

        print "元素: ", elem, "\n";
        print "行数: ", rows, " 列数: ", cols, "\n";

        for int i = 0; i < rows; i++ {
            for int j = 0; j < cols; j++ {
                print "matrix[", i, "][", j, "] = ", matrix[i][j], "\n";
            }
        }

        return 0;
    }

输出：

    元素: 6
    行数: 3 列数: 3
    matrix[0][0] = 99
    matrix[0][1] = 2
    matrix[0][2] = 3
    matrix[1][0] = 4
    matrix[1][1] = 5
    matrix[1][2] = 6
    matrix[2][0] = 7
    matrix[2][1] = 8
    matrix[2][2] = 9

关键点是：多维数组的 `.length` 返回的是元素总数，直接拿它当循环上界会越界，必须除以列数。

## 数组的存储

YXG 的数组是值语义：`int[] b = a;` 会把整个数组复制一份，改 b 不影响 a：

    int main() {
        int[] a = {1, 2, 3};
        int[] b = a;

        b[0] = 99;
        print a[0], "\n";                // 1（a 没有被修改）
        print b[0], "\n";                // 99

        return 0;
    }

输出：

    1
    99

正因为是值语义，`int[] copy = original;` 本身就是一份独立副本，不需要逐个元素手动拷贝：

    int main() {
        int[] original = {1, 2, 3, 4, 5};
        int[] copy = original;

        copy[0] = 99;
        print "original[0]: ", original[0], "\n";
        print "copy[0]: ", copy[0], "\n";

        return 0;
    }

输出：

    original[0]: 1
    copy[0]: 99

需要显式逐元素拷贝时也可以这样写：

    int main() {
        int[] original = {1, 2, 3, 4, 5};
        int[] copy = new int[original.length];

        for int i = 0; i < original.length; i++ {
            copy[i] = original[i];
        }

        copy[0] = 99;
        print "original[0]: ", original[0], "\n";
        print "copy[0]: ", copy[0], "\n";

        return 0;
    }

## 数组作为函数参数

    void printArray(int[] arr) {
        print "数组长度: ", arr.length, "\n";
        for int i = 0; i < arr.length; i++ {
            print "  [", i, "] = ", arr[i], "\n";
        }
    }

    int[] doubleElements(int[] arr) {
        int[] result = new int[arr.length];
        for int i = 0; i < arr.length; i++ {
            result[i] = arr[i] * 2;
        }
        return result;
    }

    int main() {
        int[] nums = {1, 2, 3, 4, 5};

        printArray(nums);

        int[] doubled = doubleElements(nums);
        print "翻倍后长度: ", doubled.length, "\n";
        for int i = 0; i < doubled.length; i++ {
            print "  [", i, "] = ", doubled[i], "\n";
        }

        return 0;
    }

输出：

    数组长度: 5
      [0] = 1
      [1] = 2
      [2] = 3
      [3] = 4
      [4] = 5
    翻倍后长度: 5
      [0] = 2
      [1] = 4
      [2] = 6
      [3] = 8
      [4] = 10

注意：函数返回的数组不要再直接传给另一个函数，它在被再次当作参数时长度会变成 0。遍历请直接在调用方用 for 循环。

数组参数传递的是引用，函数内直接修改元素会影响原数组：

    void modify(int[] arr) {
        arr[0] = 777;
    }

    int main() {
        int[] nums = {1, 2, 3};
        modify(nums);
        print nums[0], "\n";      // 777
        return 0;
    }

但要注意返回值：函数里用 `{1, 2, 3}` 字面量创建的数组，退出函数后数据会失效。

    int[] makeBad() {
        int[] r = {1, 2, 3};
        return r;
    }

    int main() {
        int[] x = makeBad();
        print x.length, "\n";      // 0！字面量数组不能返回
        return 0;
    }

正确做法是用 `new` 分配再填值：

    int[] makeGood() {
        int[] r = new int[3];
        r[0] = 1;
        r[1] = 2;
        r[2] = 3;
        return r;
    }

    int main() {
        int[] x = makeGood();
        print x.length, " ", x[0], "\n";    // 3 1
        return 0;
    }

## 数组与 foreach

    int main() {
        int[] arr = {10, 20, 30, 40, 50};

        for int v : arr {
            print v, "\n";
        }

        return 0;
    }

输出：

    10
    20
    30
    40
    50

foreach 只支持一维数组。`for int[] row : matrix` 这种写法会报 `error[E2002]: expected variable name`，遍历多维数组要用下标：

    int main() {
        int[][] matrix = {{1, 2}, {3, 4}, {5, 6}};
        int cols = matrix[0].length;
        int rows = matrix.length / cols;

        for int i = 0; i < rows; i++ {
            for int j = 0; j < cols; j++ {
                print matrix[i][j], "\n";
            }
        }

        return 0;
    }

## 数组转字符串

print 可以直接打印数组，格式是 `[...]`，元素之间用 `, ` 分隔：

    int main() {
        int[] nums = {1, 2, 3, 4, 5};
        print nums, "\n";

        string[] names = {"Alice", "Bob"};
        print names, "\n";

        int[][] matrix = {{1, 2}, {3, 4}};
        print matrix, "\n";

        return 0;
    }

输出：

    [1, 2, 3, 4, 5]
    ["Alice", "Bob"]
    [1, 2, 3, 4]

注意两点：字符串数组的每个元素都会被加上双引号；多维数组打印成一维的样子，不会显示内层方括号。每次 print 都不会自动换行，所以上面每行末尾都加了 `"\n"`。

## 常见模式

### 查找元素

    int findIndex(int[] arr, int target) {
        for int i = 0; i < arr.length; i++ {
            if arr[i] == target {
                return i;
            }
        }
        return -1;
    }

### 反转数组

    void reverse(int[] arr) {
        int left = 0;
        int right = arr.length - 1;

        while left < right {
            int temp = arr[left];
            arr[left] = arr[right];
            arr[right] = temp;
            left++;
            right--;
        }
    }

### 数组合并

    int[] concat(int[] a, int[] b) {
        int[] result = new int[a.length + b.length];

        for int i = 0; i < a.length; i++ {
            result[i] = a[i];
        }
        for int i = 0; i < b.length; i++ {
            result[a.length + i] = b[i];
        }

        return result;
    }

## 常见错误

### 数组越界

    int main() {
        int[] arr = {1, 2, 3};
        // arr[5] = 10;          // 编译期报错 error[E3007] array index 5 out of bounds (size 3)
        return 0;
    }

索引是常量时编译器直接报错。索引是变量时，越界要到运行期才被发现，程序输出 `Error: Array index out of bounds` 并终止（退出码 1）。

### 未分配就使用

    int main() {
        int[] arr;
        // arr[0] = 10;          // 编译报错 error[E3003] array 'arr' of unknown size must be initialized
        return 0;
    }

没写大小的数组必须先初始化才能用：`int[] arr = new int[5];` 或 `int[] arr = {1, 2, 3};`。

### 类型不匹配

    int main() {
        // int[] arr = {1, 2, "hello"};    // 错误！error[E3003] type mismatch in initializer
        int[] arr = {1, 2, 3};
        return 0;
    }

### .length 不可赋值

    int main() {
        int[] arr = {1, 2, 3};
        // arr.length = 10;      // 错误！length 是只读的
        return 0;
    }

## 继续阅读

- [指针](pointers.md)：用指针遍历和修改数组，指针运算与下标的关系
- [多返回值与可变参数](variadic.md)：把数组作为变参传入
- [字符串](strings.md)：字符序列的详细用法
