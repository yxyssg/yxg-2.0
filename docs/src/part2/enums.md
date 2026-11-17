# 枚举

## 基本语法

枚举是一组具名整型常量的集合。使用 enum 关键字定义：

    enum Color {
        Red,
        Green,
        Blue
    }

    int main() {
        Color c = Color.Red;
        print c, "\n";                       // 0（枚举值本质是 int）

        if c == Color.Red {
            print "红色\n";
        }

        return 0;
    }

输出：

    0
    红色

if 的条件可以加括号，`if (c == Color.Red) { ... }` 与上面等价。

print 也支持括号形式，但括号里只能放一个参数：`print c` 和 `print(c)` 等价。

枚举成员默认从 0 开始自动递增。上面 Red = 0，Green = 1，Blue = 2。

## 指定枚举值

可以手动指定枚举成员的值：

    enum Status {
        Pending = 10,
        Active = 20,
        Completed = 30,
        Failed = 40
    }

也可以部分指定，未指定的成员在前一个值的基础上递增：

    enum ErrorCode {
        Success = 0,
        NotFound,                    // 1
        PermissionDenied,            // 2
        InternalError = 100,
        ServiceUnavailable,          // 101
        Timeout                      // 102
    }

    int main() {
        print ErrorCode.Success, " ", ErrorCode.NotFound, " ", ErrorCode.PermissionDenied, "\n";
        print ErrorCode.InternalError, " ", ErrorCode.ServiceUnavailable, " ", ErrorCode.Timeout, "\n";
        return 0;
    }

输出：

    0 1 2
    100 101 102

## 枚举与 switch

枚举和 switch 配合使用非常自然：

    enum Day {
        Monday = 1,
        Tuesday,
        Wednesday,
        Thursday,
        Friday,
        Saturday,
        Sunday
    }

    int main() {
        Day today = Day.Wednesday;

        switch today {
            case Day.Monday:    print "星期一"; break;
            case Day.Tuesday:   print "星期二"; break;
            case Day.Wednesday: print "星期三"; break;
            case Day.Thursday:  print "星期四"; break;
            case Day.Friday:    print "星期五"; break;
            case Day.Saturday:  print "星期六"; break;
            case Day.Sunday:    print "星期日"; break;
            default:            print "无效日期";
        }

        return 0;
    }

输出：

    星期三

switch 的条件也可以带括号：`switch (today) { ... }`。

## 枚举本质

枚举成员本质是 int 类型的编译时常量，可以参与整数运算：

    int main() {
        int x = Color.Red + 10;      // 10
        int y = Color.Blue * 2;      // 4

        print "x = ", x, ", y = ", y, "\n";

        // 遍历枚举（利用递增特性）
        // for 不支持括号，初始化段必须是声明
        for int i = Color.Red; i <= Color.Blue; i++ {
            print i, "\n";           // 0, 1, 2
        }

        return 0;
    }

输出：

    x = 10, y = 4
    0
    1
    2

`for (int i = Color.Red; i <= Color.Blue; i++)` 这种带括号的写法会直接报语法错误。

## 枚举作为函数参数

    enum LogLevel {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    }

    void log(LogLevel level, string message) {
        switch level {
            case LogLevel.Debug:   print "[DEBUG] ", message, "\n"; break;
            case LogLevel.Info:    print "[INFO] ", message, "\n"; break;
            case LogLevel.Warning: print "[WARN] ", message, "\n"; break;
            case LogLevel.Error:   print "[ERROR] ", message, "\n"; break;
        }
    }

    int main() {
        log(LogLevel.Info, "程序启动");
        log(LogLevel.Warning, "磁盘空间不足");
        log(LogLevel.Error, "连接失败");
        return 0;
    }

输出：

    [INFO] 程序启动
    [WARN] 磁盘空间不足
    [ERROR] 连接失败

注意 print 不会自动插空格：写成 `print "[INFO] ", message` 才会在 `[INFO]` 和消息之间留出空格；写成 `print "[INFO]", message` 会输出 `[INFO]程序启动`。

另外 print 的括号形式只接受一个参数，`print("[INFO]", message)` 会编译报错，多个参数必须用无括号形式。

## 完整示例：HTTP 状态码

    enum HttpStatus {
        OK = 200,
        Created = 201,
        NoContent = 204,
        BadRequest = 400,
        Unauthorized = 401,
        Forbidden = 403,
        NotFound = 404,
        InternalServerError = 500,
        BadGateway = 502,
        ServiceUnavailable = 503
    }

    string getStatusMessage(HttpStatus status) {
        switch status {
            case HttpStatus.OK:                  return "OK";
            case HttpStatus.Created:             return "Created";
            case HttpStatus.NoContent:           return "No Content";
            case HttpStatus.BadRequest:          return "Bad Request";
            case HttpStatus.Unauthorized:        return "Unauthorized";
            case HttpStatus.Forbidden:           return "Forbidden";
            case HttpStatus.NotFound:            return "Not Found";
            case HttpStatus.InternalServerError: return "Internal Server Error";
            case HttpStatus.BadGateway:          return "Bad Gateway";
            case HttpStatus.ServiceUnavailable:  return "Service Unavailable";
            default:                             return "Unknown";
        }
    }

    int main() {
        HttpStatus s1 = HttpStatus.OK;
        HttpStatus s2 = HttpStatus.NotFound;
        HttpStatus s3 = HttpStatus.InternalServerError;

        print s1, " ", getStatusMessage(s1), "\n";   // 200 OK
        print s2, " ", getStatusMessage(s2), "\n";   // 404 Not Found
        print s3, " ", getStatusMessage(s3), "\n";   // 500 Internal Server Error

        return 0;
    }

## 常见错误

### 枚举成员重名

不同枚举里可以有同名成员，编译器不会报错，但取到的值可能和直觉不符，建议不同枚举的成员名不要重复：

    enum Color { Red, Green, Blue }
    enum Traffic { Red, Yellow, Green }

    int main() {
        print Color.Red, "\n";       // 0
        print Traffic.Red, "\n";     // 0（同名成员会复用已注册的值）
        return 0;
    }

访问时始终写全 `枚举名.成员`，不要只写成员名。

### 将枚举当作类使用

    Color c = Color.Red;
    // c.someMethod();     // 错误！error[E3016]: cannot determine class of object 'c'

枚举只是 int 的别名集合，不具备类的特性。

### 用带括号的 for 遍历枚举

    // for (int i = Color.Red; i <= Color.Blue; i++) { ... }   // 错误！for 不允许括号
    for int i = Color.Red; i <= Color.Blue; i++ { print i, "\n"; }   // 正确

## 继续阅读

掌握了枚举后，下一章将学习模块系统与 import 语句。
