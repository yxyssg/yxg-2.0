# HTTPS 客户端（2.5.3）

`yxg.Https` 提供基于 Windows SChannel 的 TLS 客户端。

```yxg
import yxg.Https;
```

## 两层接口

| 层次 | 类 | 用途 |
|---|---|---|
| 高层 | `HttpClient` | 给 URL 拿响应，自动处理 TLS、chunked、头部解析 |
| 底层 | `HttpsClient` | 面向连接的收发，接口模仿 `Socket` |

大部分场景用 `HttpClient` 就够了。

---

## 高层：HttpClient

### 快速开始

```yxg
import yxg.Https;
import yxg.String;

pub void main() {
    HttpClient http = new HttpClient();
    HttpResult r = http.get("https://example.com/");

    print "status = ", r.status, "\n";        // 200
    print "server = ", r.header("Server"), "\n";
    print "body   = ", String.len(r.body), " 字节\n";
}
```

实测输出：

```
status = 200
server = cloudflare
body   = 577 字节
```

### 方法

| 方法 | 说明 |
|---|---|
| `get(url)` | GET 请求 |
| `post(url, body, contentType)` | POST 请求 |
| `request(method, url, body, contentType)` | 通用 |
| `setTimeout(ms)` | 超时，默认 15000 |

`url` 必须是 `https://` 开头。传 `http://` 会返回错误 `"only https is supported"`（不做明文 HTTP，明文用 `Socket`）。

### HttpResult

| 成员 | 类型 | 说明 |
|---|---|---|
| `status` | `int` | HTTP 状态码，`0` 表示请求失败 |
| `headers` | `string` | 原始响应头（含状态行） |
| `body` | `string` | 正文，**已自动解 chunked** |
| `error` | `string` | 失败原因，成功时为空 |
| `headerLines` | `string[]` | 头按行拆开 |
| `isOk()` | `bool` | 是否 200 |
| `header(name)` | `string` | 取响应头，**大小写不敏感** |

```yxg
HttpResult r = http.get("https://example.com/");
if r.status == 200 {
    print r.body, "\n";
} else {
    print "失败: ", r.error, " status=", r.status, "\n";
}

// 头名大小写不敏感
print r.header("content-type"), "\n";
print r.header("Content-Type"), "\n";   // 等价
```

实测：

```
status  = 200
chunked = chunked
bodylen = 577
has html tag = true     ← 正文确实解出来了
404 status = 404        ← 非 200 也能拿到状态码
```

### 自动处理的事

`HttpClient.request` 内部会做：

1. 解析 URL（拆 host、port、path）
2. 建立 TCP + TLS 握手
3. 拼请求头（`Host`、`User-Agent: yxg/2.5.3`、`Accept`、`Connection: close`）
4. 有 body 时自动加 `Content-Type` 和 `Content-Length`
5. **循环接收直到对端关闭**（单次 `recv` 只有 16KB）
6. 拆头 / 正文
7. **自动解 `Transfer-Encoding: chunked`**
8. 关闭连接

### 手动解 chunked

```yxg
string raw = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
string text = HttpClient.dechunk(raw);
print text, "\n";    // Hello World
```

---

## 底层：HttpsClient

需要自己控制请求、或用非 HTTP 协议时用这个。

### 方法

| 方法 | 返回 | 说明 |
|---|---|---|
| `connect(host, port)` | `int` | `0`=成功 `-1`=建 socket 失败 `-2`=TCP 失败 `-3`=TLS 握手失败 |
| `isValid()` | `bool` | 是否已连接 |
| `setTimeout(ms)` | — | 超时，默认 10000 |
| `send(data)` | `int` | 加密发送，返回字节数 |
| `recv(maxlen)` | `string` | 解密接收，**单次最多 16KB** |
| `recvAll(maxTotal)` | `string` | 循环接收直到对端关闭 |
| `recvHeaders()` | `string` | 读到 `\r\n\r\n` 为止（keep-alive 用） |
| `close()` | — | 释放 |
| `lastError()` | `string` | 失败原因 |
| `lastSecStatus()` | `int` | SSPI 原始状态码 |

### 示例

```yxg
import yxg.Https;
import yxg.String;

pub void main() {
    HttpsClient c = new HttpsClient();
    int rc = c.connect("example.com", 443);
    print "connect = ", rc, "\n";

    if rc != 0 {
        print "错误: ", c.lastError(), "\n";
        print "SSPI  : 0x", c.lastSecStatus(), "\n";
        return;
    }

    c.send("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    // 单次读（最多 16KB）
    // string part = c.recv(8192);

    // 读完整响应
    string all = c.recvAll(1048576);
    print "recv len = ", String.len(all), "\n";

    int eol = String.indexOfStr(all, "\r\n");
    print "状态行   = ", String.sub(all, 0, eol), "\n";

    c.close();
}
```

实测：

```
connect = 0
recvAll len = 923
状态行   = HTTP/1.1 200 OK
```

### 错误码

`lastError()` 返回值：

| 值 | 含义 |
|---|---|
| `ok` | 无错误 |
| `AcquireCredentialsHandle failed` | 拿不到 SChannel 凭据 |
| `InitializeSecurityContext (initial) failed` | 握手初始阶段失败 |
| `TLS handshake failed` | 握手过程失败 |
| `too many TLS connections` | 超出 16 个并发上下文 |

排查时用 `lastSecStatus()` 拿 SSPI 原始状态码，比如 `0x80090305` = `SEC_E_SECPKG_NOT_FOUND`。

---

## 实现说明

- 底层走 `secur32.dll` 的 SChannel（`AcquireCredentialsHandleA` / `InitializeSecurityContextA` / `EncryptMessage` / `DecryptMessage`）
- 协议版本：TLS 1.0 / 1.1 / 1.2
- 证书校验用 SChannel 默认策略（**会校验证书链**）
- 每条连接独立上下文，最多同时 16 条
- 单条 TLS 记录最大约 18437 字节，内部接收缓冲 20KB

## 编译器自动链接

代码里调用了 `_tls_*` 函数时，编译器自动：

1. 编译 `runtime/runtime_tls.asm`
2. 链接 `-lsecur32`

**不需要手动 `link` 声明**。这是通过 `has_tls_lib` 标志检测的，逻辑在 `Codegen_win32.c`。

## 已知限制

| 限制 | 说明 |
|---|---|
| 只做客户端 | 服务器端 TLS 需要证书管理，本版本不做 |
| `recv` 单次 16KB | 大响应用 `recvAll` |
| 不做重定向 | 3xx 需要自己处理 |
| 不做 HTTP/2 | 只支持 HTTP/1.1 |
| 不校验证书链的宽松模式 | 未提供；当前总是走默认严格校验 |
| 只支持 HTTPS | 明文 HTTP 请用 `Socket` 手写或走 `TPServer` 的反向 |

## 与 Http 模块的关系

`yxg.Http` 处理**报文本身**（`HttpRequest.parse` / `HttpResponse.build`），不涉及网络传输。
`yxg.Https` 处理**传输层**（TLS 加密收发）。

服务端（`TPServer` + `Router`）目前只提供明文 HTTP；需要 HTTPS 服务端时，应在前面挂 nginx 之类的反向代理做 TLS 终结。

## 完整示例：拉取网页并提取标题

```yxg
import yxg.Https;
import yxg.Regex;
import yxg.String;

pub void main() {
    HttpClient http = new HttpClient();
    HttpResult r = http.get("https://example.com/");

    if r.status != 200 {
        print "请求失败: ", r.status, " ", r.error, "\n";
        return;
    }

    // 提取 <title>...</title> 之间的内容
    string title = Regex.firstMatch("<title>(.*)</title>", r.body);
    print "标题: ", title, "\n";
    print "正文长度: ", String.len(r.body), "\n";
}
```

---
