# 实战三：库存管理

## 目标

用类建一个简单的库存管理系统：

- 商品类，带名称、单价、数量
- 计算单项价值和总价值
- 按条件筛选商品
- 用继承实现不同类型的商品

练习点：

- 类的定义、构造函数、方法
- 对象的 `new` 和字段访问
- 用类数组组织数据
- `virtual` / `override` 多态

## 第一步：最简商品类

```yxg
pub class Item {
    pub string name;
    pub int qty;
    pub int price;

    pub Item(string n, int q, int p) {
        this.name = n;
        this.qty = q;
        this.price = p;
    }

    pub int value() {
        return this.qty * this.price;
    }
}

pub void main() {
    Item apple = new Item("苹果", 10, 5);
    print apple.name, " 数量 ", apple.qty, " 单价 ", apple.price, "\n";
    print "价值 ", apple.value(), "\n";
}
```

输出：

```
苹果 数量 10 单价 5
价值 50
```

### 构造函数与 this

```yxg
pub Item(string n, int q, int p) {
    this.name = n;
    this.qty = q;
    this.price = p;
}
```

- 构造函数名和类名相同，**没有返回类型**
- 参数名和字段名相同时，必须用 `this.` 区分
- 不写 `this.` 的话，`name = n;` 会去赋值参数 `name` 而不是字段

### 对象必须 new

```yxg
Item a;              // ❌ a 是 null
a.name = "x";        // 运行时错误：Error: null pointer dereference

Item b = new Item("x", 1, 2);    // ✅
```

## 第二步：多个商品

用数组组织，但数组元素是对象怎么放？

YXG 的数组元素可以是类类型，声明成 `Item[]`：

```yxg
pub void main() {
    Item[] items = new Item[3];
    items[0] = new Item("苹果", 10, 5);
    items[1] = new Item("香蕉", 20, 3);
    items[2] = new Item("橙子", 5, 8);

    int total = 0;
    int i = 0;
    while i < items.length {
        print items[i].name, " × ", items[i].qty,
              " = ", items[i].value(), "\n";
        total = total + items[i].value();
        i = i + 1;
    }
    print "总计: ", total, "\n";
}
```

输出：

```
苹果 × 10 = 50
香蕉 × 20 = 60
橙子 × 5 = 40
总计: 150
```

`items[i].name` 直接链式访问。

> **注意**：`new Item[3]` 只分配了 3 个**指针槽**，每个槽都是 null。必须逐个 `new` 赋值，不能直接用。

## 第三步：带查询的方法

把统计逻辑封装成函数，接收 `Item[]`：

```yxg
pub int totalValue(Item[] items) {
    int total = 0;
    int i = 0;
    while i < items.length {
        total = total + items[i].value();
        i = i + 1;
    }
    return total;
}

pub int countCheap(Item[] items, int limit) {
    int n = 0;
    int i = 0;
    while i < items.length {
        if items[i].price < limit {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}
```

调用：

```yxg
print "总价值: ", totalValue(items), "\n";
print "单价低于 5 的商品: ", countCheap(items, 5), " 个\n";
```

## 第四步：继承与多态

不同类型的商品用不同的计价方式，比如「散装商品」按重量算，「折扣商品」打折：

```yxg
pub class Item {
    pub string name;
    pub int qty;
    pub int price;

    pub Item(string n, int q, int p) {
        this.name = n;
        this.qty = q;
        this.price = p;
    }

    pub virtual int value() {
        return this.qty * this.price;
    }
}

pub class DiscountItem : Item {
    pub int percent;

    pub DiscountItem(string n, int q, int p, int pct) {
        this.name = n;
        this.qty = q;
        this.price = p;
        this.percent = pct;
    }

    pub override int value() {
        return this.qty * this.price * (100 - this.percent) / 100;
    }
}
```

父类 `value()` 加 `virtual`，子类加 `override`，这样通过父类类型调用也能派发到子类的实现：

```yxg
pub void main() {
    Item normal = new Item("苹果", 10, 5);          // 50
    DiscountItem sale = new DiscountItem("香蕉", 20, 3, 50);  // 60 * 50% = 30

    print normal.name, " = ", normal.value(), "\n";
    print sale.name, " = ", sale.value(), "\n";
}
```

输出：

```
苹果 = 50
香蕉 = 30
```

### 用父类数组装子类

```yxg
pub void main() {
    Item[] items = new Item[2];
    items[0] = new Item("苹果", 10, 5);
    items[1] = new DiscountItem("香蕉", 20, 3, 50);

    int i = 0;
    int total = 0;
    while i < items.length {
        print items[i].name, ": ", items[i].value(), "\n";
        total = total + items[i].value();
        i = i + 1;
    }
    print "总计: ", total, "\n";
}
```

输出：

```
苹果: 50
香蕉: 30
总计: 80
```

`items[i].value()` 会按实际类型派发——第 1 个走 `Item.value()`，第 2 个走 `DiscountItem.value()`。这就是 `virtual` / `override` 的作用。

> 如果不加 `virtual` / `override`，`items[i].value()` 在编译期就绑定到 `Item.value()`，两个都返回 50。

## 完整程序

```yxg
pub class Item {
    pub string name;
    pub int qty;
    pub int price;

    pub Item(string n, int q, int p) {
        this.name = n;
        this.qty = q;
        this.price = p;
    }

    pub virtual int value() {
        return this.qty * this.price;
    }

    pub void show() {
        print this.name, "\t", this.qty, "\t", this.price,
              "\t", this.value(), "\n";
    }
}

pub class DiscountItem : Item {
    pub int percent;

    pub DiscountItem(string n, int q, int p, int pct) {
        this.name = n;
        this.qty = q;
        this.price = p;
        this.percent = pct;
    }

    pub override int value() {
        return this.qty * this.price * (100 - this.percent) / 100;
    }
}

pub int totalValue(Item[] items) {
    int total = 0;
    int i = 0;
    while i < items.length {
        total = total + items[i].value();
        i = i + 1;
    }
    return total;
}

pub int countCheap(Item[] items, int limit) {
    int n = 0;
    int i = 0;
    while i < items.length {
        if items[i].price < limit {
            n = n + 1;
        }
        i = i + 1;
    }
    return n;
}

pub void main() {
    Item[] items = new Item[4];
    items[0] = new Item("苹果", 10, 5);
    items[1] = new Item("香蕉", 20, 3);
    items[2] = new DiscountItem("橙子", 5, 8, 20);
    items[3] = new DiscountItem("葡萄", 2, 30, 50);

    print "名称\t数量\t单价\t价值\n";
    print "------------------------\n";
    int i = 0;
    while i < items.length {
        items[i].show();
        i = i + 1;
    }
    print "------------------------\n";
    print "总价值: ", totalValue(items), "\n";
    print "单价 < 5 的商品: ", countCheap(items, 5), " 个\n";
}
```

输出：

```
名称	数量	单价	价值
------------------------
苹果	10	5	50
香蕉	20	3	60
橙子	5	8	32
葡萄	2	30	30
------------------------
总价值: 172
单价 < 5 的商品: 1 个
```

### 验算

| 商品 | 类型 | 计算 | 价值 |
|---|---|---|---|
| 苹果 | 普通 | 10 × 5 | 50 |
| 香蕉 | 普通 | 20 × 3 | 60 |
| 橙子 | 8 折 | 5 × 8 × 80 / 100 | 32 |
| 葡萄 | 5 折 | 2 × 30 × 50 / 100 | 30 |

总计 50 + 60 + 32 + 30 = **172**。单价小于 5 的只有香蕉（3），共 1 个。

## 知识点总结

| 知识点 | 用法 |
|---|---|
| 类定义 | `pub class Item { ... }` |
| 构造函数 | 名字与类同名，无返回类型 |
| `this` | 区分字段和同名参数 |
| 对象创建 | 必须 `new Item(...)` |
| 类数组 | `Item[] items = new Item[3];` 逐项 new |
| 链式访问 | `items[i].name` |
| 方法调用 | `items[i].value()` |
| 多态 | 父类 `virtual` + 子类 `override` |
| 父类数组装子类 | `Item[]` 里放 `DiscountItem` |
| 整数除法 | `qty * price * 80 / 100` 注意运算顺序 |

## 常见错误

### 忘记 new

```yxg
Item a;
print a.name;              // Error: null pointer dereference
```

### 数组没逐项初始化

```yxg
Item[] items = new Item[3];
print items[0].name;       // items[0] 是 null，崩溃
```

必须先 `items[0] = new Item(...)`。

### override 但父类没有 virtual

```yxg
pub class Item {
    pub int value() { return 0; }         // 没有 virtual
}
pub class SaleItem : Item {
    pub override int value() { return 1; }  // 报错 E3015
}
```

报错：`override method 'value' has no matching virtual method in parent class`

### 整数除法丢精度

```yxg
this.qty * this.price * (100 - this.percent) / 100
```

如果写成 `this.qty * this.price * (100 - this.percent) / 100 * 1.0`，顺序会先整除再乘浮点，结果不对。要浮点结果就把乘法提前：

```yxg
return (this.qty * this.price * (100 - this.percent)) as float / 100.0;
```

## 继续阅读

下一篇用集合和 JSON 处理结构化数据。
