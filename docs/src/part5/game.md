# 游戏辅助库 Game

`yxg.Game` 是一组跑在 [Window](graphics.md) 之上的轻量工具：二维向量、矩形、输入状态机、摄像机、帧时钟。它不负责画图，只负责「算」和「记状态」。

| 模块 | 导入 | 内容 |
|---|---|---|
| Vec2 | `import yxg.Game.Vec2;` | 二维向量与向量运算 |
| Rect | `import yxg.Game.Rect;` | 矩形与碰撞判断 |
| Input | `import yxg.Game.Input;` | 按键 / 鼠标的「按下、按住、抬起」状态 |
| Camera | `import yxg.Game.Camera;` | 世界坐标 → 屏幕坐标、视口裁剪 |
| Time | `import yxg.Game.Time;` | 帧时钟（`deltaMs`、FPS） |

> **命名空间大小写**：导入时写 `yxg.Game.Vec2`，和磁盘上 `lib/yxg/Game/Vec2.yxg` 一一对应。

## Vec2 二维向量

`Vec2` 是普通对象，用 `vec2(x, y)` 或 `new Vec2(x, y)` 创建。所有运算都是**全局函数**，不是方法。

```yxg
import yxg.Game.Vec2;

pub void main() {
    Vec2 a = vec2(3.0, 4.0);
    Vec2 b = vec2(1.0, 1.0);

    print "len(a)  = ", vec2_len(a), "\n";        // 5.0
    print "len2(a) = ", vec2_len2(a), "\n";       // 25.0（不开方，比较距离时更快）
    print "add     = ", vec2_add(a, b).x, ",", vec2_add(a, b).y, "\n";   // 4.0,5.0
    print "sub     = ", vec2_sub(a, b).x, ",", vec2_sub(a, b).y, "\n";   // 2.0,3.0
    print "mul     = ", vec2_mul(a, 2.0).x, ",", vec2_mul(a, 2.0).y, "\n"; // 6.0,8.0
    print "dot     = ", vec2_dot(a, b), "\n";     // 7.0
    print "dist    = ", vec2_dist(a, b), "\n";    // 3.605551
    print "norm    = ", vec2_norm(a).x, ",", vec2_norm(a).y, "\n";       // 0.6,0.8
}
```

| 函数 | 返回 | 说明 |
|---|---|---|
| `vec2(x, y)` | `Vec2` | 构造 |
| `vec2_zero()` | `Vec2` | `(0, 0)` |
| `vec2_add(a, b)` / `vec2_sub(a, b)` | `Vec2` | 加 / 减 |
| `vec2_mul(v, s)` | `Vec2` | 标量乘 |
| `vec2_len(v)` / `vec2_len2(v)` | `float` | 长度 / 长度平方 |
| `vec2_dist(a, b)` / `vec2_dist2(a, b)` | `float` | 距离 / 距离平方 |
| `vec2_norm(v)` | `Vec2` | 单位化（长度为 0 时返回零向量） |
| `vec2_dot(a, b)` | `float` | 点积 |
| `vec2_set(v, x, y)` | `void` | 原地修改 |

**比较距离用 `vec2_dist2` 更划算**：省掉一次开方，只做大小比较时结果一致。

## Rect 矩形

```yxg
import yxg.Game.Rect;

pub void main() {
    Rect r = rect(10.0, 10.0, 100.0, 50.0);        // x, y, 宽, 高

    print "contains(50,30) = ", rect_contains(r, 50.0, 30.0), "\n";   // true
    print "contains(5,30)  = ", rect_contains(r, 5.0, 30.0), "\n";    // false
    print "overlap        = ", rect_overlap(r, rect(50.0, 20.0, 100.0, 100.0)), "\n"; // true
    print "contains_rect  = ", rect_contains_rect(r, rect(20.0, 20.0, 10.0, 10.0)), "\n"; // true
}
```

| 函数 | 说明 |
|---|---|
| `rect(x, y, w, h)` | 构造 |
| `rect_contains(r, px, py)` | 点是否在矩形内（左闭右开） |
| `rect_contains_vec(r, v)` | 同上，传 `Vec2` |
| `rect_overlap(a, b)` | 两个矩形是否相交（贴边算不相交） |
| `rect_contains_rect(outer, inner)` | `inner` 是否完全在 `outer` 内 |

## Input 输入状态机

`Input` 把「这一帧的按键状态」缓存下来，于是能区分三种语义：

| 函数 | 含义 |
|---|---|
| `key_down(k)` | 按住中 |
| `key_pressed(k)` | 这一帧刚按下（上一帧没按） |
| `key_released(k)` | 这一帧刚松开 |
| `mouse_down()` / `mouse_pressed()` | 左键按住 / 刚按下 |
| `mouse_x()` / `mouse_y()` | 鼠标坐标 |

用法是固定的三步循环：

```yxg
import yxg.Window;
import yxg.Game.Input;

Window win;                                // 必须放全局：GC 只扫描全局变量

pub void main() {
    win = new Window();
    win.create("Input 示例", 640, 480);
    win.show();
    input_init();                          // 1. 初始化（整局一次）

    while win.is_valid() != 0 {
        input_begin_frame(win);            // 2. 帧首：同步窗口状态进来

        if key_pressed(WindowKey.KEY_SPACE) {        // 空格：只在按下的那一帧为真
            print "jump\n";
        }
        if key_down(WindowKey.KEY_LEFT) {            // 左箭头：按住期间一直为真
            print "left\n";
        }

        input_end_frame();                 // 3. 帧尾：curr → prev

        win.clear(WindowColor.COLOR_BLACK);
        win.flush();
        win.update();                      // 处理窗口消息
        win.sleep(16);
    }
}
```

**只同步 16 个常用键。** `input_begin_frame` 内部维护一张固定表（方向键、`Z`/`X`/`C`、`SHIFT`、`SPACE`、`ENTER`、`ESC`、`A`/`S`/`D`/`W`、`CTRL`）。要加键，得改这张表 —— 直接用 `win.is_key_down(k)` 更灵活，只是拿不到 `pressed` / `released` 的边沿判断。

**忘记调 `input_end_frame` 是最常见的 bug**：`key_pressed` 会一直返回 `true`，角色每帧都在起跳。

## Camera 摄像机

摄像机只管两件事：把世界坐标换成屏幕坐标、判断物体要不要渲染。

```yxg
import yxg.Game.Camera;

pub void main() {
    camera_init(800, 600);                  // 视口大小
    camera_follow(400.0, 300.0);            // 让世界坐标 (400,300) 居中

    print "cam_sx(400) = ", cam_sx(400.0), "\n";   // 400
    print "cam_sy(300) = ", cam_sy(300.0), "\n";   // 300
    print "visible     = ", cam_visible(0.0, 0.0, 100.0, 100.0), "\n";  // true
}
```

| 函数 | 说明 |
|---|---|
| `camera_init(viewW, viewH)` | 初始化，镜头对准世界原点 |
| `camera_follow(wx, wy)` | 把镜头中心移到该世界坐标 |
| `cam_sx(wx)` / `cam_sy(wy)` | 世界坐标 → 屏幕坐标（取整） |
| `cam_visible(x, y, w, h)` | 该世界矩形是否与视口相交 |

绘制时先 `cam_visible` 过滤，再 `cam_sx` / `cam_sy` 换算，能省掉大量看不见的绘制调用。

## Time 帧时钟

```yxg
import yxg.Window;
import yxg.Game.Time;

Window win;                                // 必须放全局：GC 只扫描全局变量

pub void main() {
    win = new Window();
    win.create("Clock", 640, 480);
    win.show();
    clock_init(win);

    float x = 0.0;
    while win.is_valid() != 0 {
        clock_tick(win);                    // 每帧开头结算一次耗时

        float dt = g_clock.deltaSec;        // 本帧耗时（秒）
        x = x + 300.0 * dt;                 // 每秒 300 像素，与帧率无关

        win.clear(WindowColor.COLOR_BLACK);
        win.draw_rect(x as int, 100, 32, 32, WindowColor.COLOR_WHITE);
        win.flush();
        win.update();
        win.sleep(1);
    }
}
```

`g_clock` 是模块级全局对象，字段直接读：

| 字段 | 说明 |
|---|---|
| `deltaMs` / `deltaSec` | 上一帧耗时 |
| `totalSec` | 累计运行时间 |
| `fps` | 每 500ms 统计一次 |
| `frameCount` / `fpsTimer` | 内部计数，一般不用管 |

**凡是和「速度」有关的量都要乘 `deltaSec`**，否则帧率一变，角色速度就跟着变。

## 继续阅读

- [graphics.md](graphics.md) —— Window / Canvas / 组件库
- [../part2/class-definition.md](../part2/class-definition.md) —— 类的定义
