# 异常处理

## 为什么需要异常

函数出错时需要一种方式把错误传出去。用返回值表示错误，调用者容易漏检；异常可以强制中断正常流程，交给专门的错误处理代码。

YXG 的异常基于 `try` / `catch` / `end` 语句和 `ERROR` 基类。

## 抛出异常

用 `throw` 抛出对象。抛出的对象必须继承自 `ERROR`：

```yxg
pub class MyError : ERROR {
    pub int code;

    pub MyError(string msg, int c) {
        this.info = msg;
        this.code = c;
    }
}

pub void checkAge(int age) {
    if age < 0 {
        throw new MyError("age cannot be negative", 1001);
    }
    print "age = ", age, "\n";
}

pub void main() {
    checkAge(20);

    try {
        checkAge(-5);
    } catch (ERROR e) {
        print "caught: ", e.info, "\n";
    }
}
```

输出：

```
age = 20
caught: age cannot be negative
```

`ERROR` 基类有一个 `info` 字段存放错误信息。

## try / catch

基本形式：

```yxg
try {
    // 可能出错的代码
} catch (ERROR e) {
    // 处理错误
}
```

`catch` 后面的括号里写「异常类型 变量名」，类型决定捕获范围：

- `catch (ERROR e)` —— 捕获所有 `ERROR` 子类
- `catch (MyError e)` —— 只捕获 `MyError` 及其子类

```yxg
pub class AppError : ERROR {
    pub AppError(string msg) {
        this.info = msg;
    }
}

pub class IOError : AppError {
    pub IOError(string msg) {
        this.info = msg;
    }
}

pub void main() {
    // 只捕获 IOError
    try {
        throw new AppError("not an IO error");
    } catch (IOError e) {
        print "io error caught\n";
    } catch (AppError e) {
        print "app error: ", e.info, "\n";
    }
}
```

输出：

```
app error: not an IO error
```

因为 `AppError` 不是 `IOError`，第一个 `catch` 不匹配，落到第二个。

### 多个 catch

按声明顺序尝试匹配，第一个匹配的就执行：

```yxg
try {
    // ...
} catch (IOError e) {
    print "IO problem\n";
} catch (AppError e) {
    print "App problem\n";
} catch (ERROR e) {
    print "Unknown: ", e.info, "\n";
}
```

**注意**：`catch (ERROR e)` 放最后，否则会拦截所有其他 catch。

## try / end（收尾块）

**收尾块用 `end` 关键字**（不是 `finally`）。它无论有没有异常都会执行，适合做清理：

```yxg
pub void main() {
    try {
        print "opening resource\n";
        throw new MyError("boom", 1);
    } catch (ERROR e) {
        print "caught: ", e.info, "\n";
    } end {
        print "cleaning up\n";
    }
    print "after try\n";
}
```

输出：

```
opening resource
caught: boom
cleaning up
after try
```

## try / catch / end

三者可以组合（收尾块用 `end`）：

```yxg
try {
    // ...
} catch (ERROR e) {
    // ...
} end {
    // ...
}
```

## 未捕获的异常

如果异常一路抛到 `main` 之外没人捕获，程序会打印错误信息并终止：

```yxg
pub void main() {
    throw new MyError("unhandled", 1);
}
```

输出类似：

```
Error: unhandled
```

## 自定义异常层次

按业务需要设计继承结构：

```yxg
pub class AppError : ERROR {
    pub AppError(string msg) {
        this.info = msg;
    }
}

pub class ValidationError : AppError {
    pub string field;

    pub ValidationError(string msg, string f) {
        this.info = msg;
        this.field = f;
    }
}

pub class DatabaseError : AppError {
    pub int code;

    pub DatabaseError(string msg, int c) {
        this.info = msg;
        this.code = c;
    }
}

pub void validate(int v) {
    if v < 0 {
        throw new ValidationError("must be positive", "v");
    }
}

pub void main() {
    try {
        validate(-1);
    } catch (ValidationError e) {
        print "invalid field: ", e.field, " (", e.info, ")\n";
    } catch (AppError e) {
        print "app error: ", e.info, "\n";
    }
}
```

输出：

```
invalid field: v (must be positive)
```

## 实际示例：除法

```yxg
pub class DivByZero : ERROR {
    pub DivByZero() {
        this.info = "division by zero";
    }
}

pub int divide(int a, int b) {
    if b == 0 {
        throw new DivByZero();
    }
    return a / b;
}

pub void main() {
    print "10 / 2 = ", divide(10, 2), "\n";

    try {
        print "10 / 0 = ", divide(10, 0), "\n";
    } catch (DivByZero e) {
        print "error: ", e.info, "\n";
    }

    print "done\n";
}
```

输出：

```
10 / 2 = 5
error: division by zero
done
```

## 标准库里的异常

一些标准库模块会抛异常，使用时需要捕获：

```yxg
import yxg.Collections.Database;

pub void main() {
    try {
        Database db = new Database("/invalid/path/x.db");
        if !db.isOpen() {
            print "cannot open database\n";
            return;
        }
        db.close();
    } catch (ERROR e) {
        print "db error: ", e.info, "\n";
    }
}
```

`List`、`Map` 的 `get(i)` 越界时也会抛 `ContainerError`：

```yxg
import yxg.Collections.List;

pub void main() {
    List<int> l = new List<int>();
    l.add(1);

    try {
        int v = l.get(5);          // 越界
        print "value = ", v, "\n";
    } catch (ERROR e) {
        print "list error: ", e.info, "\n";
    }
}
```

## 注意事项

- **抛出对象必须 new**：`throw new MyError(...)`，不能 `throw MyError(...)`
- **异常对象必须继承 `ERROR`**，否则编译报错
- **`catch (ERROR e)` 放最后**，否则会屏蔽其他 catch
- 运行时错误（数组越界、空指针）**不走异常机制**，会直接打印错误并终止程序：`Error: Array index out of bounds` / `Error: null pointer dereference`

## 继续阅读

下一章介绍泛型。
