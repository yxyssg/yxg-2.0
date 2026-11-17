# 实战一：成绩统计

## 目标

从一组分数里算出人数、总分、平均分、最高分、最低分，并按分数段统计人数分布。

练习点：

- 数组的声明与遍历
- 多个累积变量的同时维护
- 数组作为函数参数
- 用 `while` 做计数循环

## 先写核心统计

```yxg
pub void main() {
    int[] scores = {85, 92, 78, 96, 61, 88};
    int n = scores.length;

    int sum = 0;
    int maxv = scores[0];
    int minv = scores[0];

    int i = 0;
    while i < n {
        sum = sum + scores[i];
        if scores[i] > maxv {
            maxv = scores[i];
        }
        if scores[i] < minv {
            minv = scores[i];
        }
        i = i + 1;
    }

    print "人数: ", n, "\n";
    print "总分: ", sum, "\n";
    print "平均: ", sum / n, "\n";
    print "最高: ", maxv, "\n";
    print "最低: ", minv, "\n";
}
```

输出：

```
人数: 6
总分: 500
平均: 83
最高: 96
最低: 61
```

### 几个细节

**最大值和最小值的初值**：`int maxv = scores[0];` 用第一个元素作初值，而不是用 `0`。如果用 `0`，当所有分数都是负数时结果就错了。

**平均分是整除**：`sum / n` = `500 / 6` = `83`，小数部分被丢弃。想要小数得转成浮点：

```yxg
float avg = (sum as float) / (n as float);
print "平均: ", avg, "\n";      // 83.3333
```

## 拆成函数

统计逻辑抽成函数，可以复用：

```yxg
pub int sumOf(int[] arr) {
    int s = 0;
    int i = 0;
    while i < arr.length {
        s = s + arr[i];
        i = i + 1;
    }
    return s;
}

pub int maxOf(int[] arr) {
    int m = arr[0];
    int i = 1;
    while i < arr.length {
        if arr[i] > m {
            m = arr[i];
        }
        i = i + 1;
    }
    return m;
}

pub int minOf(int[] arr) {
    int m = arr[0];
    int i = 1;
    while i < arr.length {
        if arr[i] < m {
            m = arr[i];
        }
        i = i + 1;
    }
    return m;
}
```

注意 `maxOf` / `minOf` 的循环从 `i = 1` 开始——第 0 个已经是初值了，不用再比。

### 数组参数怎么传

```yxg
pub void main() {
    int[] scores = {85, 92, 78};
    print sumOf(scores), "\n";
}
```

传的是 `int[]`，函数里用 `arr.length` 拿长度、`arr[i]` 读元素。

> **注意**：参数必须传**动态数组**（`new int[n]` 或 `{}` 字面量初始化过的变量）。如果数组是函数返回的，也正常。

## 分数段统计

统计各分数段的人数，练习「用数组当计数器」：

```yxg
pub void main() {
    int[] scores = {85, 92, 78, 96, 61, 88, 45, 100};

    // 索引 0: 0-59, 1: 60-69, 2: 70-79, 3: 80-89, 4: 90-100
    int[] buckets = new int[5];

    int i = 0;
    while i < scores.length {
        int s = scores[i];
        int idx = 0;
        if s >= 90 {
            idx = 4;
        } else if s >= 80 {
            idx = 3;
        } else if s >= 70 {
            idx = 2;
        } else if s >= 60 {
            idx = 1;
        }
        buckets[idx] = buckets[idx] + 1;
        i = i + 1;
    }

    print "0-59  : ", buckets[0], " 人\n";
    print "60-69 : ", buckets[1], " 人\n";
    print "70-79 : ", buckets[2], " 人\n";
    print "80-89 : ", buckets[3], " 人\n";
    print "90-100: ", buckets[4], " 人\n";
}
```

输出：

```
0-59  : 1 人
60-69 : 1 人
70-79 : 1 人
80-89 : 2 人
90-100: 3 人
```

### 为什么用 buckets 数组

不用写 5 个独立的计数器变量，而是把「分数段」映射成「数组下标」，然后 `buckets[idx] = buckets[idx] + 1` 一行完成统计。

### 一个容易踩的坑

直觉上可能想用 `scores[i] / 10` 当作下标：

```yxg
int idx = 85 / 10;        // 得到 8，不是 3！
buckets[idx] = ...        // 数组只有 5 个元素 → 越界崩溃
```

`85 / 10 = 8` 是**十位数**，不是段号。段号要从 0 开始连续编号，所以必须用 `if` 链显式映射。

也可以做算术映射，但要处理边界：

```yxg
int idx = s / 10 - 5;      // 85 → 8 - 5 = 3
if idx < 0 { idx = 0; }    // 60 以下都归到第 0 段
if idx > 4 { idx = 4; }    // 100 分是 10 - 5 = 5，要夹到 4
```

这种写法更短，但要求分数段严格按 10 分一档。分数段不规整时（比如 0-59 / 60-84 / 85-100）还是 `if` 链更清楚。

## 完整程序

合并所有功能：

```yxg
pub int sumOf(int[] arr) {
    int s = 0;
    int i = 0;
    while i < arr.length {
        s = s + arr[i];
        i = i + 1;
    }
    return s;
}

pub int maxOf(int[] arr) {
    int m = arr[0];
    int i = 1;
    while i < arr.length {
        if arr[i] > m { m = arr[i]; }
        i = i + 1;
    }
    return m;
}

pub int minOf(int[] arr) {
    int m = arr[0];
    int i = 1;
    while i < arr.length {
        if arr[i] < m { m = arr[i]; }
        i = i + 1;
    }
    return m;
}

pub int passCount(int[] arr, int line) {
    int n = 0;
    int i = 0;
    while i < arr.length {
        if arr[i] >= line {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub void main() {
    int[] scores = {85, 92, 78, 96, 61, 88, 45, 100};
    int n = scores.length;
    int total = sumOf(scores);

    print "===== 成绩统计 =====\n";
    print "人数: ", n, "\n";
    print "总分: ", total, "\n";
    print "平均: ", total / n, "\n";
    print "最高: ", maxOf(scores), "\n";
    print "最低: ", minOf(scores), "\n";
    print "及格(>=60): ", passCount(scores, 60), " 人\n";
    print "优秀(>=90): ", passCount(scores, 90), " 人\n";

    print "\n===== 分布 =====\n";
    int[] buckets = new int[5];
    int i = 0;
    while i < scores.length {
        int s = scores[i];
        int idx = 0;
        if s >= 90 {
            idx = 4;
        } else if s >= 80 {
            idx = 3;
        } else if s >= 70 {
            idx = 2;
        } else if s >= 60 {
            idx = 1;
        }
        buckets[idx] = buckets[idx] + 1;
        i = i + 1;
    }
    print "0-59  : ", buckets[0], "\n";
    print "60-69 : ", buckets[1], "\n";
    print "70-79 : ", buckets[2], "\n";
    print "80-89 : ", buckets[3], "\n";
    print "90-100: ", buckets[4], "\n";
}
```

输出：

```
===== 成绩统计 =====
人数: 8
总分: 645
平均: 80
最高: 100
最低: 45
及格(>=60): 7 人
优秀(>=90): 3 人

===== 分布 =====
0-59  : 1
60-69 : 1
70-79 : 1
80-89 : 2
90-100: 3
```

## 知识点总结

| 知识点 | 用法 |
|---|---|
| 数组字面量 | `int[] scores = {85, 92};` |
| 动态数组 | `int[] buckets = new int[5];` 默认全是 0 |
| 数组长度 | `arr.length` |
| 数组传参 | `sumOf(scores)`，函数内用 `arr.length` |
| 多变量累积 | 一次循环里同时更新 `sum` / `max` / `min` |
| 下标当分类键 | `buckets[score / 10] += 1` |
| 整数除法 | `sum / n` 丢弃小数 |
| 浮点除法 | `(sum as float) / (n as float)` |

## 继续阅读

下一篇做一个文本统计工具。
