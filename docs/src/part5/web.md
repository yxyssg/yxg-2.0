# Web 服务：Http / Route / Session / ThreadPool

YXG 标准库自带一套够用的 HTTP 服务栈：`ThreadPool` 提供线程池和多线程服务器，`Route` 做路由和静态文件，`Http` 处理请求/响应的解析与组装，`Session` 存会话。

| 模块 | 导入 | 作用 |
|---|---|---|
| ThreadPool | `import yxg.ThreadPool;` | `TPServer`、`TPHandler`、`TPRequest`、`TPResponse` |
| Route | `import yxg.Route;` | `Router`（路径参数、静态挂载）、`MimeTypes` |
| Http | `import yxg.Http;` | `HttpRequest.parse`、`HttpResponse.build`（自己写协议时用） |
| Session | `import yxg.Session;` | `Session`、`SessionStore`（TTL + LRU 淘汰） |

## 最小可用的服务器

`TPServer` 构造时就 bind 端口，`run()` 会阻塞并起 128 个工作线程。handler 继承 `TPHandler` 并 `override handle`。

```yxg
import yxg.ThreadPool;

pub class HelloHandler : TPHandler {
    pub override void handle(TPRequest req, TPResponse res) {
        res.text("hello yxg");
    }
}

pub void main() {
    TPServer server = new TPServer(8080);
    print "listening on 8080\n";
    server.run(new HelloHandler());     // 阻塞
}
```

打开 <http://127.0.0.1:8080/> 就能看到 `hello yxg`。

端口被占用时构造函数会抛 `TPError`：

```yxg
try {
    TPServer server = new TPServer(80);
    server.run(new HelloHandler());
} catch (ERROR e) {
    print "启动失败: ", e.getInfo(), "\n";
}
```

## TPRequest / TPResponse

### 读请求

| 成员 | 说明 |
|---|---|
| `method` | `GET` / `POST` / ... |
| `path` | 不带动查询串的路径 |
| `query` | 原始查询串 |
| `body` | 请求体 |
| `version` | `HTTP/1.1` |
| `headers` | 原始头部文本 |

| 方法 | 说明 |
|---|---|
| `queryOf(name)` | 查询串参数 |
| `param(name)` | 路径参数（由 `Router` 填） |
| `cookieOf(name)` | 某个 cookie 的值 |

### 写响应

| 方法 | 说明 |
|---|---|
| `text(s)` | 纯文本，自动带 `Content-Type: text/plain; charset=utf-8` |
| `json(s)` | JSON |
| `html(s)` | HTML |
| `setCookie(name, value)` | 下发 cookie |
| `redirect(location)` | 302 跳转 |
| `status` | 状态码，默认 200 |

```yxg
pub class ApiHandler : TPHandler {
    pub override void handle(TPRequest req, TPResponse res) {
        string name = req.queryOf("name");
        if String.len(name) == 0 {
            res.status = 400;
            res.json("{\"error\":\"name required\"}");
            return;
        }
        res.json("{\"hello\":\"" + name + "\"}");
    }
}
```

handler 里**抛出的异常会被服务器接住**并回 `500 Internal Server Error`，不会连带把服务打挂。

## Router 路由

`Router` 本身就是一个 `TPHandler`，把它交给 `TPServer.run()` 即可。支持路径参数（`:name`）和静态目录挂载。

```yxg
import yxg.ThreadPool;
import yxg.Route;

pub class HelloHandler : TPHandler {
    pub override void handle(TPRequest req, TPResponse res) {
        res.text("hello yxg");
    }
}

pub class UserHandler : TPHandler {
    pub override void handle(TPRequest req, TPResponse res) {
        res.text("user=" + req.param("id"));
    }
}

pub void main() {
    Router router = new Router();
    router.get("/hello", new HelloHandler());
    router.get("/user/:id", new UserHandler());
    router.mount("/static", "public");      // 把 ./public 挂到 /static 下

    TPServer server = new TPServer(18080);
    print "listening on 18080\n";
    server.run(router);
}
```

实测：

```
GET /hello      -> 200 hello yxg
GET /user/42    -> 200 user=42
GET /nope       -> 404 404 Not Found
```

| 方法 | 说明 |
|---|---|
| `get(path, handler)` / `post` / `put` / `del` | 注册路由 |
| `add(method, path, handler)` | 其他方法 |
| `mount(prefix, dir)` | 挂载静态目录；带 `..` 的请求返回 403 |

**静态挂载自带目录穿越防护**：解析出相对路径后逐段检查 `..`，命中就回 `403 Forbidden`。MIME 类型由 `MimeTypes.of(path)` 决定，认不出的回 `application/octet-stream`。

**路由按注册顺序匹配**，路径段数必须完全相等。`:id` 这样的段只占位、不做类型校验，拿到的是字符串。

## Session 会话

`SessionStore` 管一批 `Session`：创建、按 sid 取、过期回收、超容量时淘汰最老的。

```yxg
import yxg.Session;
import yxg.Http;

pub void main() {
    SessionStore store = new SessionStore(60, 100);   // TTL 60 秒，最多 100 个

    Session s = store.create();
    s.set("user", "alice");
    print "sid = ", s.sid, "\n";
    print "size = ", store.size(), "\n";

    Session got = store.get(s.sid);                   // 取到会顺便 touch
    if got != null {
        print "user = ", got.get("user"), "\n";
    }

    store.destroy_sid(s.sid);                         // 主动销毁
    store.destroy();                                  // 释放整个 store
}
```

| 成员 | 说明 |
|---|---|
| `Session.sid` / `data` / `created_at` / `expires_at` / `last_access` | 字段 |
| `isExpired()` / `touch()` | 是否过期 / 刷新访问时间 |
| `get(k)` / `set(k, v)` / `remove(k)` | 键值操作 |
| `destroy()` | 清空该会话 |

| SessionStore | 说明 |
|---|---|
| `new SessionStore(ttlSec, maxCount)` | TTL 秒数、最大会话数 |
| `create()` | 新建会话（带锁） |
| `get(sid)` | 取会话，过期返回 `null` |
| `destroy_sid(sid)` / `gc()` / `size()` / `destroy()` | 同上 |

`create()` 每 100 次调用会自动跑一轮过期回收；容量满了淘汰**最早创建**的会话。

> `SessionStore` 内部用 `Mutex` 加锁，可以给多线程服务器共用；但 `Session` 对象本身没有逐字段加锁，多个请求同时改同一个会话需要自己保证顺序。

## Http：自己处理协议

不想走 `ThreadPool` 时，可以用 `Http` 手动解析/组装报文（例如把请求转发到别处、或者做测试）。

```yxg
import yxg.Http;

pub void main() {
    // 解析一段原始请求
    HttpRequest req = HttpRequest.parse(
        "GET /a?x=1&y=2 HTTP/1.1\r\nHost: h\r\nCookie: sid=abc; theme=dark\r\n\r\n");

    print "method = ", req.method, "\n";        // GET
    print "path   = ", req.path, "\n";          // /a
    print "x      = ", req.getQuery("x"), "\n"; // 1
    print "sid    = ", req.getCookie("sid"), "\n"; // abc

    // 组装一段响应
    HttpResponse res = new HttpResponse();
    res.status = 200;
    res.setHeader("Content-Type", "application/json");
    res.setCookie("sid", "abc");
    res.write("{}");
    print res.build(), "\n";
}
```

`build()` 的输出形如：

```
HTTP/1.1 200 OK
Content-Length: 2
content-type: application/json
Set-Cookie: sid=abc; Path=/
Connection: close

{}
```

注意两点：头部名统一**转小写**；`Connection: close` 固定加上，即每个请求一个连接（没有 keep-alive）。

## 完整例子：带会话的计数器

```yxg
import yxg.ThreadPool;
import yxg.Route;
import yxg.Session;
import yxg.Utils;

SessionStore g_store;                       // 全局：GC 只扫全局变量

pub class CounterHandler : TPHandler {
    pub override void handle(TPRequest req, TPResponse res) {
        string sid = req.cookieOf("sid");
        Session s = null;
        if String.len(sid) > 0 {
            s = g_store.get(sid);
        }
        bool isNew = false;
        if s == null {
            s = g_store.create();
            isNew = true;
        }

        string n = s.get("n");
        int count = 0;
        if String.len(n) > 0 {
            count = Convert.parseInt(n);
        }
        count = count + 1;
        s.set("n", Convert.formatInt(count));

        if isNew {
            res.setCookie("sid", s.sid);
        }
        res.text("count = " + Convert.formatInt(count));
    }
}

pub void main() {
    g_store = new SessionStore(300, 1000);
    Router router = new Router();
    router.get("/count", new CounterHandler());

    TPServer server = new TPServer(18081);
    print "listening on 18081\n";
    server.run(router);
}
```

第一次请求会 `Set-Cookie: sid=...`，带上这个 cookie 再请求，`count` 就会一直累加。

## 常见问题

### 端口被占用

`TPServer` 构造即 bind，失败抛 `TPError`。换端口，或先关掉占用的进程（`netstat -ano | findstr 8080`）。

### 静态文件一直 404

`mount` 的第二个参数是**相对当前工作目录**的路径，不是相对 exe。用 `yxg run` 时工作目录通常是项目根，直接用 `File.exists` 确认拼出来的路径对不对。

### 响应中文变乱码

`res.text()` / `res.json()` / `res.html()` 都自带 `charset=utf-8`。自己 `setHeader("Content-Type", ...)` 时要手动带上 `; charset=utf-8`。

### 请求体读不全

`TPServer` 读到 `\r\n\r\n` 就认为头部结束，`body` 取剩下的字节。大文件（表单上传）没有分块支持，上限是头部 64KB 的判断，需要更大请求体时要自己扩展 `read_request`。

## 继续阅读

- [../part2/https.md](../part2/https.md) —— HTTPS 客户端
- [../part5/network-file.md](network-file.md) —— Socket / File / System
- [../part2/error-resource.md](../part2/error-resource.md) —— 异常与资源管理
