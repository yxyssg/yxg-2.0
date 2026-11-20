# 网络与文件库

## Socket 网络库

导入：`import yxg.Socket;`

### 常量

```
Socket_AF_INET = 2          IPv4
Socket_SOCK_STREAM = 1      TCP
Socket_SOCK_DGRAM = 2       UDP
Socket_IPPROTO_TCP = 6
Socket_IPPROTO_UDP = 17
Socket_E_WOULDBLOCK = 10035
Socket_E_CONNREFUSED = 10061
Socket_E_TIMEDOUT = 10060
```

### TCP 服务器

```yxg
SocketServer server = new SocketServer(8000);   // 构造即 bind
if !server.isValid() {
    print "端口绑定失败\n";
}
server.listen(1);
server.setNonblock(1);

Socket peer = server.accept();      // 非阻塞时无连接返回 null
if peer != null {
    peer.setNonblock(1);
    peer.setNoDelay(1);             // 关闭 Nagle，降低延迟
}
```

### TCP 客户端

```yxg
SocketClient client = new SocketClient();
int r = client.connectWait("127.0.0.1", 8000, 5000);   // 带超时
if r == 0 {
    client.setNonblock(1);
    client.setNoDelay(1);
}
```

### 收发

| 方法 | 说明 |
|---|---|
| `sendAll(string)` | 保证全部发送 |
| `send(string)` | 发送（可能只发一部分） |
| `recvLine(int maxLen)` | 收一行（到换行符） |
| `recvExact(int len)` | 收定长字节 |
| `selectRead(int ms)` | >0 可读；0 表示非阻塞查询 |
| `selectWrite(int ms)` | 可写检查 |

非阻塞模式的标准收发：

```yxg
peer.sendAll("hello\n");

if peer.selectRead(0) > 0 {
    string line = peer.recvLine(256);
    // 处理 line
}
```

### UDP

```yxg
SocketUDP udp = new SocketUDP();
udp.bindPort(9000);

udp.sendTo("192.168.1.5", 9001, "数据");
string data = udp.recvFrom(512);
string fromIp = udp.senderIp();
int fromPort = udp.senderPort();
```

### 底层 Socket 类

需要更细的控制时直接用 `Socket`：

```yxg
import yxg.Socket;

pub void main() {
    Socket s = new Socket();
    int rc = s.open(Socket_AF_INET, Socket_SOCK_STREAM, Socket_IPPROTO_TCP);
    print "open = ", rc, " valid = ", s.isValid(), "\n";
    s.setReuseAddr(1);
    print "bind = ", s.bind(17800), "\n";
    print "listen = ", s.listen(8), "\n";
    print "port = ", s.localPort(), "\n";
    s.close();
}
```

常用方法：`open`、`connect`、`bind`、`bindAddr`、`listen`、`accept`、`recv`、`send`、`sendAll`、`close`、`setTimeout`、`setNonblock`、`setReuseAddr`、`setNoDelay`、`localPort`、`peerIp`、`peerPort`、`lastError`、`errorText`。

### 地址转换

```yxg
int ip = s.inetAddr("127.0.0.1");     // 16777343
print s.inetNtoa(ip), "\n";           // 127.0.0.1
print s.htons(80), "\n";              // 20480
```

## File 文件库

导入：`import yxg.File;`

### 快捷静态方法

```yxg
import yxg.File;

pub void main() {
    File.writeAllText("test.txt", "hello yxg");
    print File.readAllText("test.txt"), "\n";       // hello yxg
    print "exists = ", File.exists("test.txt"), "\n";   // true
}
```

完整静态方法：

| 方法 | 说明 |
|---|---|
| `exists(path)` | 是否存在 |
| `delete(path)` | 删除 |
| `copy(src, dst)` / `move(src, dst)` | 复制 / 移动 |
| `createDir(path)` / `removeDir(path)` | 建 / 删目录 |
| `isDir(path)` | 是否目录 |
| `getFileSize(path)` | 文件大小 |
| `getCurrentDir()` / `setCurrentDir(path)` | 当前目录 |
| `getTempDir()` | 临时目录 |
| `readAllText(path)` | 读全文 |
| `writeAllText(path, text)` | 写全文 |
| `appendAllText(path, text)` | 追加 |
| `readAllLines(path)` | 读成 `string[]` |
| `copyFile(src, dst)` | 复制文件 |
| `listDir(path)` | 目录列表 `string[]` |
| `pathJoin(a, b)` | 路径拼接 |
| `pathDirName(path)` / `pathBaseName(path)` / `pathExtName(path)` | 路径拆分 |
| `byteLen(str)` | 二进制安全长度 |

### 文件对象

需要逐行读或随机定位时用 `File` 对象：

```yxg
File f = new File("data.txt", "r");    // r=读 w=写 a=追加
if f.isOpen() {
    string line = f.readLine();
    print line, "\n";
    f.close();
}
```

打开失败会抛 `FileError` 异常，可以用 `try` 捕获。

| 方法 | 说明 |
|---|---|
| `readAll()` | 读全部 |
| `readLine()` | 读一行 |
| `readBytes(int count)` | 读指定字节 |
| `readInto(string buf, int len)` | 流式读入，返回实际字节数 |
| `atEnd()` | 是否到尾部 |
| `write(string)` | 写字符串 |
| `writeBytes(string data, int len)` | 写二进制 |
| `writeLine(string)` | 写一行 + 换行 |
| `flush()` | 刷缓冲 |
| `seek(int offset, int origin)` | 定位，origin: 0=头 1=当前 2=尾 |
| `tell()` | 当前位置 |
| `getSize()` | 文件大小 |
| `isOpen()` | 是否打开 |
| `close()` | 关闭 |

### 二进制读写

```yxg
File f = new File("data.bin", "r");
if f.isOpen() {
    string raw = f.readBytes(f.getSize());
    int len = File.byteLen(raw);       // 二进制安全长度
    f.close();
}
```

`File.byteLen` 只对 `File` 读出的字符串有效（有长度头），不能用于字面量。

### 保存与读取设置

```yxg
import yxg.File;
import yxg.Utils;

pub void saveScore(int score) {
    File.writeAllText("save.txt", Convert.formatInt(score));
}

pub int loadScore() {
    if !File.exists("save.txt") {
        return 0;
    }
    return Convert.parseInt(File.readAllText("save.txt"));
}
```

## System 系统库

导入：`import yxg.System;`

| 方法 | 说明 |
|---|---|
| `System.exit(int code)` | 退出程序 |
| `System.argc()` | 命令行参数个数 |
| `System.argv(int i)` | 第 i 个参数 |
| `System.printArgs()` | 打印全部参数 |
| `System.system(string cmd)` | 执行系统命令 |
| `System.time()` | 秒级时间戳 |
| `System.sleep(int ms)` | 休眠 |
| `System.env(string name)` | 读环境变量 |

命令行参数两种方式：

```yxg
// 方式一：main 带参数
pub void main(string[] args) {
    // args[0] 是程序名，args[1] 起是用户参数
}

// 方式二：System.argv
string mode = System.argv(1);
```

## 网络库常见问题

### 联机时两个玩家在同一侧

双方都成了服务器。原因通常是连接失败后没重试，或者 `listen` 没有紧跟 `bind`。

修复：`listen` 紧跟构造，客户端连接失败重试 3~5 次。

### 同机测试联机

启动两个实例，第一个自动成服务器，第二个连上成客户端。点哪个窗口哪个窗口收键盘输入（焦点决定）。

### 端口被占用

`SocketServer` 构造即 bind，失败时 `isValid()` 返回 0。换端口或先关闭占用进程。

## 继续阅读

- [graphics.md](graphics.md) —— 图形与多媒体库
- [game.md](game.md) —— 游戏辅助库（Vec2 / Rect / Input / Camera / Time）
- [web.md](web.md) —— Web 服务：Http / Route / Session / ThreadPool
- [../part2/https.md](../part2/https.md) —— HTTPS 客户端下一章是附录